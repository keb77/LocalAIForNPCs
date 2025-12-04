#include "LLMComponent.h"
#include "Misc/Paths.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

ULLMComponent::ULLMComponent()
{
    PrimaryComponentTick.bCanEverTick = false;

    OnStreamTokenReceived.AddDynamic(this, &ULLMComponent::HandleStreamChunk);
}

void ULLMComponent::BeginPlay()
{
    Super::BeginPlay();

    if (RagMode != ERagMode::Disabled)
    {
        UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | LLM | RAG] RAG mode enabled, generating knowledge..."));

        Async(EAsyncExecution::Thread, [this]()
            {
                GenerateKnowledge();
                UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | LLM | RAG] Knowledge generation complete."));
            });
    }

    if (!KnownActions.IsEmpty())
    {
        SystemMessage += TEXT("\n\n") + BuildActionsSystemMessage();
        UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | LLM | Actions] SystemMessage updated with known actions and objects."));
    }

    if (RagMode != ERagMode::Disabled)
    {
        SystemMessage.Append(TEXT("\n\n"));
        SystemMessage.Append(TEXT("Answer questions using the provided context: \n"));
    }
}

void ULLMComponent::SendChatMessage(FString Message)
{
    if (Message.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAIForNPCs | LLM] Empty message received, ignoring."));

        OnResponseReceived.Broadcast(TEXT(""));

        return;
    }

    FChatMessage NewMessage;
    NewMessage.Role = "user";
    NewMessage.Content = Message;
    ChatHistory.Add(NewMessage);

    if (RagMode != ERagMode::Disabled)
    {
        UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | LLM | RAG] Starting RAG process"));

        Async(EAsyncExecution::Thread, [this, Message]()
            {
                TArray<float> Embedding = EmbedText(Message);

                TArray<FString> RagDocuments = GetTopKDocuments(Embedding);

                for (const FString& Doc : RagDocuments)
                {
                    UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | LLM | RAG] Embedding selected document: %s"), *Doc);
                }

                if (RagMode == ERagMode::EmbeddingPlusReranker)
                {
                    RagDocuments = RerankDocuments(Message, RagDocuments);

                    for (const FString& Doc : RagDocuments)
                    {
                        UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | LLM | RAG] Reranking selected document: %s"), *Doc);
                    }
                }

                AsyncTask(ENamedThreads::GameThread, [this, RagDocuments]()
                    {
                        for (const FString& Doc : RagDocuments)
                        {
                            if (!SystemMessage.Contains(Doc))
                            {
                                SystemMessage.Append(Doc);
                                SystemMessage.Append(TEXT("\n"));
                            }
                        }

                        if (!bStream)
                        {
                            SendRequest();
                        }
                        else
                        {
                            SendRequestStreaming();
                        }
                    });
            });
    }
    else
    {
        if (!bStream)
        {
            SendRequest();
        }
        else
        {
            SendRequestStreaming();
        }
    }
}

void ULLMComponent::SendRequest()
{
    FString Url = FString::Printf(TEXT("http://localhost:%d/v1/chat/completions"), Port);
    FString Content = CreateJsonRequest();

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Url);
    Request->SetVerb("POST");
    Request->SetHeader("Content-Type", "application/json");
    Request->SetContentAsString(Content);

    Request->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr Req, FHttpResponsePtr Response, bool bWasSuccessful)
        {
            if (!bWasSuccessful || !Response.IsValid())
            {
                UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | LLM] Request failed."));

                AsyncTask(ENamedThreads::GameThread, [this]()
                    {
                        OnResponseReceived.Broadcast(TEXT(""));
                    });

                return;
            }

            int32 Code = Response->GetResponseCode();
            if (Code != 200)
            {
                UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | LLM] HTTP %d: %s"), Code, *Response->GetContentAsString());

                AsyncTask(ENamedThreads::GameThread, [this]()
                    {
                        OnResponseReceived.Broadcast(TEXT(""));
                    });

                return;
            }

            FString JsonResponse = Response->GetContentAsString();
            TSharedPtr<FJsonObject> JsonObject;
            TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonResponse);
            if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
            {
                UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | LLM] Failed to parse JSON response"));
                UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | LLM] Response: %s"), *JsonResponse);

                AsyncTask(ENamedThreads::GameThread, [this]()
                    {
                        OnResponseReceived.Broadcast(TEXT(""));
                    });

                return;
            }
            const TArray<TSharedPtr<FJsonValue>>* Choices;
            if (!JsonObject->TryGetArrayField(TEXT("choices"), Choices) || Choices->Num() == 0)
            {
                UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | LLM] No choices found in response"));
                UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | LLM] Response: %s"), *JsonResponse);

                AsyncTask(ENamedThreads::GameThread, [this]()
                    {
                        OnResponseReceived.Broadcast(TEXT(""));
                    });

                return;
            }
            TSharedPtr<FJsonObject> ChoiceObj = (*Choices)[0]->AsObject();
            if (!ChoiceObj.IsValid())
            {
                UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | LLM] Invalid choice object in response"));
                UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | LLM] Response: %s"), *JsonResponse);

                AsyncTask(ENamedThreads::GameThread, [this]()
                    {
                        OnResponseReceived.Broadcast(TEXT(""));
                    });

                return;
            }
            const TSharedPtr<FJsonObject>* MessageObj;
            if (!ChoiceObj->TryGetObjectField(TEXT("message"), MessageObj) || !MessageObj->IsValid())
            {
                UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | LLM] No message object found in choice"));
                UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | LLM] Response: %s"), *JsonResponse);

                AsyncTask(ENamedThreads::GameThread, [this]()
                    {
                        OnResponseReceived.Broadcast(TEXT(""));
                    });

                return;
            }
            FString ResponseContent;
            if (!(*MessageObj)->TryGetStringField(TEXT("content"), ResponseContent) || ResponseContent.IsEmpty())
            {
                UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | LLM] No content field found in message"));
                UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | LLM] Response: %s"), *JsonResponse);

                AsyncTask(ENamedThreads::GameThread, [this]()
                    {
                        OnResponseReceived.Broadcast(TEXT(""));
                    });

                return;
            }

            FString SanitizedResponse = SanitizeString(ResponseContent);
            UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | LLM] Response received."));
            UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | LLM] Response: %s"), *ResponseContent);

            AsyncTask(ENamedThreads::GameThread, [this, SanitizedResponse]()
                {
                    OnResponseReceived.Broadcast(SanitizedResponse);
                });

            FChatMessage NewResponse;
            NewResponse.Role = "assistant";
            NewResponse.Content = SanitizedResponse;
            ChatHistory.Add(NewResponse);

            FRegexPattern ActionPattern(TEXT("\\[\\[action: (.+?)\\]\\]"));
            FRegexMatcher Matcher(ActionPattern, ResponseContent);
            while (Matcher.FindNext())
            {
                FString ActionCommand = Matcher.GetCaptureGroup(1);
                HandleNpcAction(ActionCommand);
            }
        });

    Request->ProcessRequest();
    UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | LLM] Request sent to %s"), *Url);
}

void ULLMComponent::SendRequestStreaming()
{
    FString Url = FString::Printf(TEXT("http://localhost:%d/v1/chat/completions"), Port);
    FString Content = CreateJsonRequest();

    FTCHARToUTF8 Utf8Content(*Content);
    FString RequestHeaders = FString::Printf(
        TEXT("POST /v1/chat/completions HTTP/1.1\r\n")
        TEXT("Host: localhost:%d\r\n")
        TEXT("Content-Type: application/json\r\n")
        TEXT("Accept: text/event-stream\r\n")
        TEXT("Content-Length: %d\r\n")
        TEXT("Connection: close\r\n\r\n"),
        Port, Utf8Content.Length());

    FString FullRequest = RequestHeaders + Content;

    Async(EAsyncExecution::Thread, [this, FullRequest = MoveTemp(FullRequest)]()
        {
            ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
            TSharedRef<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
            bool bIsValid;
            Addr->SetIp(TEXT("127.0.0.1"), bIsValid);
            Addr->SetPort(Port);

            if (!bIsValid)
            {
                UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | LLM] Invalid IP address"));

                AsyncTask(ENamedThreads::GameThread, [this]()
                    {
                        OnResponseReceived.Broadcast(TEXT(""));
                    });

                return;
            }

            FSocket* Socket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("LLMStreamSocket"), false);
            if (!Socket || !Socket->Connect(*Addr))
            {
                UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | LLM] Failed to connect to LLM server on port %d"), Port);

                AsyncTask(ENamedThreads::GameThread, [this]()
                    {
                        OnResponseReceived.Broadcast(TEXT(""));
                    });

                return;
            }

            int32 BytesSent = 0;
            auto ConvertedRequest = StringCast<UTF8CHAR>(*FullRequest);
            Socket->Send((const uint8*)ConvertedRequest.Get(), ConvertedRequest.Length(), BytesSent);

            UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | LLM] Sent streaming request, waiting for response..."));

            constexpr int32 BufferSize = 8192;
            uint8 Buffer[BufferSize];
            FString StreamedData;
            bool bDone = false;

            FString FullResponse;

            const double TimeoutSeconds = 60.0;
            double StartTime = FPlatformTime::Seconds();

            while (!bDone && (FPlatformTime::Seconds() - StartTime) < TimeoutSeconds)
            {
                if (Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromSeconds(1.0f)))
                {
                    int32 BytesRead = 0;
                    if (Socket->Recv(Buffer, BufferSize, BytesRead))
                    {
                        FString Chunk = FString(StringCast<TCHAR>((const UTF8CHAR*)Buffer, BytesRead).Get(), BytesRead);
                        StreamedData += Chunk;

                        TArray<FString> Lines;
                        Chunk.ParseIntoArrayLines(Lines);
                        for (const FString& Line : Lines)
                        {
                            if (Line.StartsWith("data: "))
                            {
                                FString Payload = Line.Mid(6).TrimStartAndEnd();
                                if (Payload == TEXT("[DONE]"))
                                {
                                    UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | LLM] Response received."));
                                    bDone = true;
                                    AsyncTask(ENamedThreads::GameThread, [this, bDone]()
                                        {
                                            OnStreamTokenReceived.Broadcast(TEXT(""), bDone);
                                        });
                                    break;
                                }

                                TSharedPtr<FJsonObject> JsonObject;
                                TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Payload);
                                if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
                                {
                                    const TArray<TSharedPtr<FJsonValue>>* Choices;
                                    if (JsonObject->TryGetArrayField(TEXT("choices"), Choices) && Choices->Num() > 0)
                                    {
                                        const TSharedPtr<FJsonObject>* Delta;
                                        if ((*Choices)[0]->AsObject()->TryGetObjectField(TEXT("delta"), Delta))
                                        {
                                            FString PartialText;
                                            if ((*Delta)->TryGetStringField(TEXT("content"), PartialText))
                                            {
                                                UE_LOG(LogTemp, Verbose, TEXT("[LocalAIForNPCs | LLM] Token received."));
                                                FullResponse.Append(PartialText);
                                                AsyncTask(ENamedThreads::GameThread, [this, PartialText, bDone]()
                                                    {
                                                        OnStreamTokenReceived.Broadcast(PartialText, bDone);
                                                        UE_LOG(LogTemp, Verbose, TEXT("[LocalAIForNPCs | LLM] Streamed token: %s"), *PartialText);
                                                    });
                                            }
                                            else
                                            {
                                                UE_LOG(LogTemp, Warning, TEXT("[LocalAIForNPCs | LLM] No content field in delta"));
                                                UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | LLM] Payload: %s"), *Payload);
                                            }
                                        }
                                        else
                                        {
                                            UE_LOG(LogTemp, Warning, TEXT("[LocalAIForNPCs | LLM] No delta field in choice"));
                                            UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | LLM] Payload: %s"), *Payload);
                                        }
                                    }
                                    else
                                    {
                                        UE_LOG(LogTemp, Warning, TEXT("[LocalAIForNPCs | LLM] No choices found in streamed data"));
                                        UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | LLM] Payload: %s"), *Payload);
                                    }
                                }
                                else
                                {
                                    UE_LOG(LogTemp, Warning, TEXT("[LocalAIForNPCs | LLM] Failed to parse JSON from streamed data: %s"), *Payload);
                                }
                            }
                            else
                            {
                                UE_LOG(LogTemp, Verbose, TEXT("[LocalAIForNPCs | LLM] Ignoring line: %s"), *Line);
                            }
                        }
                    }
                    else
                    {
                        UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | LLM] Failed to read data from socket"));
                        bDone = true;
                    }
                }
                else
                {
                    UE_LOG(LogTemp, Verbose, TEXT("[LocalAIForNPCs | LLM] No data ready to read yet..."));
                }
            }
            if (!bDone)
            {
                UE_LOG(LogTemp, Warning, TEXT("[LocalAIForNPCs | LLM] Streaming timed out after %.2f seconds"), TimeoutSeconds);

                AsyncTask(ENamedThreads::GameThread, [this]()
                    {
                        OnResponseReceived.Broadcast(TEXT(""));
                    });
            }

            Socket->Close();
            ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
            UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | LLM] Streamed response complete. Full response: %s"), *FullResponse);
            UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | LLM] Socket closed after streaming"));

            if (FullResponse.IsEmpty())
            {
                UE_LOG(LogTemp, Warning, TEXT("[LocalAIForNPCs | LLM] No response received from server"));

                AsyncTask(ENamedThreads::GameThread, [this]()
                    {
                        OnResponseReceived.Broadcast(TEXT(""));
                    });

                return;
            }

            AsyncTask(ENamedThreads::GameThread, [this, FullResponse]()
                {
                    FString SanitizedResponse = SanitizeString(FullResponse);
                    OnResponseReceived.Broadcast(SanitizedResponse);

                    FChatMessage NewResponse;
                    NewResponse.Role = "assistant";
                    NewResponse.Content = SanitizedResponse;
                    ChatHistory.Add(NewResponse);

                    FRegexPattern ActionPattern(TEXT("\\[\\[action: (.+?)\\]\\]"));
                    FRegexMatcher Matcher(ActionPattern, FullResponse);
                    while (Matcher.FindNext())
                    {
                        FString ActionCommand = Matcher.GetCaptureGroup(1);
                        HandleNpcAction(ActionCommand);
                    }
                });
        });
}

FString ULLMComponent::CreateJsonRequest()
{
    TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
    RootObject->SetBoolField("stream", bStream);

    TArray<TSharedPtr<FJsonValue>> JsonMessages;
    if (!SystemMessage.IsEmpty())
    {
        TSharedPtr<FJsonObject> SystemObj = MakeShared<FJsonObject>();
        SystemObj->SetStringField("role", "system");
        SystemObj->SetStringField("content", SystemMessage);
        JsonMessages.Add(MakeShared<FJsonValueObject>(SystemObj));
    }
    for (const FChatMessage& Msg : ChatHistory)
    {
        TSharedPtr<FJsonObject> MsgObj = MakeShared<FJsonObject>();
        MsgObj->SetStringField("role", Msg.Role);
        MsgObj->SetStringField("content", Msg.Content);
        JsonMessages.Add(MakeShared<FJsonValueObject>(MsgObj));
    }
    RootObject->SetArrayField("messages", JsonMessages);

    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

    return OutputString;
}

void ULLMComponent::HandleStreamChunk(const FString& Token, bool bDone)
{
    FScopeLock Lock(&ChunkMutex);

    AccumulatedChunk += Token;

    if (bDone)
    {
        FString SanitizedChunk = SanitizeString(AccumulatedChunk);
        if (SanitizedChunk.Len() > 1)
        {
            OnStreamChunkReceived.Broadcast(SanitizedChunk, bDone);
            UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | LLM] Chunk: %s"), *SanitizedChunk);
        }
        else
        {
            OnStreamChunkReceived.Broadcast(TEXT(""), bDone);
        }

        AccumulatedChunk.Empty();
        return;
    }

    TSet<FString> AbbreviationWhitelist = {
    TEXT("Mr."), TEXT("Mrs."), TEXT("Ms."), TEXT("Dr."), TEXT("Jr.")
    };

    int32 LastDelimiterIndex = -1;

    for (int32 i = 0; i < AccumulatedChunk.Len(); ++i)
    {
        TCHAR CurrentChar = AccumulatedChunk[i];
        if (CurrentChar == '.' || CurrentChar == '!' || CurrentChar == '?' || CurrentChar == ';' || CurrentChar == '\n' || CurrentChar == '\r')
        {
            bool bIsAbbreviation = false;
            if (CurrentChar == '.')
            {
                int32 Lookbehind = 8;
                int32 Start = FMath::Max(0, i - Lookbehind + 1);
                FString Sub = AccumulatedChunk.Mid(Start, i - Start + 1);
                for (const FString& Abbr : AbbreviationWhitelist)
                {
                    if (Sub.EndsWith(Abbr))
                    {
                        bIsAbbreviation = true;
                        break;
                    }
                }
            }
            if (!bIsAbbreviation)
            {
                LastDelimiterIndex = i;
            }
        }
    }

    if (LastDelimiterIndex == -1)
    {
        return;
    }

    FString Chunk = AccumulatedChunk.Left(LastDelimiterIndex + 1);
    AccumulatedChunk = AccumulatedChunk.Mid(LastDelimiterIndex + 1);

    Chunk = SanitizeString(Chunk);

    if (Chunk.Len() > 1)
    {
        OnStreamChunkReceived.Broadcast(Chunk, bDone);
        UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | LLM] Chunk: %s"), *Chunk);
    }
    else
    {
        UE_LOG(LogTemp, Verbose, TEXT("[LocalAIForNPCs | LLM] Empty chunk received, ignoring"));
    }
}

FString ULLMComponent::SanitizeString(const FString& String)
{
    FString Result = String;

    auto RegexReplace = [](const FString& InputStr, const FString& PatternStr) -> FString
        {
            FRegexPattern Pattern(PatternStr);
            FString Output = InputStr;

            FRegexMatcher Matcher(Pattern, Output);
            while (Matcher.FindNext())
            {
                int32 Start = Matcher.GetMatchBeginning();
                int32 Length = Matcher.GetMatchEnding() - Start;
                Output.RemoveAt(Start, Length);

                Matcher = FRegexMatcher(Pattern, Output);
            }

            return Output;
        };

    Result = RegexReplace(Result, TEXT("\\[\\[action: .*?\\]\\]"));

    Result = RegexReplace(Result, TEXT("\\[[^\\]]*\\]"));

    Result = RegexReplace(Result, TEXT("\\*[^\\*]*\\*"));

    Result = RegexReplace(Result, TEXT("<[^>]*>"));

    Result = Result.Replace(TEXT("\""), TEXT(""));

    Result = Result.Replace(TEXT("\n"), TEXT(" "));
    Result = Result.Replace(TEXT("\r"), TEXT(" "));

    Result = Result.TrimStartAndEnd();

    return Result;
}

void ULLMComponent::ClearChatHistory()
{
    ChatHistory.Empty();
    UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | LLM] Chat history cleared"));
}

TArray<float> ULLMComponent::EmbedText(const FString& Text)
{
    TArray<float> EmbeddingResult;

    TSharedPtr<FJsonObject> JsonRequest = MakeShared<FJsonObject>();
    JsonRequest->SetStringField("input", Text);

    FString RequestString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestString);
    FJsonSerializer::Serialize(JsonRequest.ToSharedRef(), Writer);

    FString Url = FString::Printf(TEXT("http://localhost:%d/v1/embeddings"), EmbeddingPort);
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Url);
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Request->SetContentAsString(RequestString);

    FEvent* CompletionEvent = FPlatformProcess::GetSynchEventFromPool(true);

    Request->OnProcessRequestComplete().BindLambda([&](FHttpRequestPtr Req, FHttpResponsePtr Res, bool bConnected)
        {
            if (bConnected && Res.IsValid() && EHttpResponseCodes::IsOk(Res->GetResponseCode()))
            {
                TSharedPtr<FJsonObject> JsonObject;
                FString Content = Res->GetContentAsString();
                TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(Content);
                if (FJsonSerializer::Deserialize(JsonReader, JsonObject) && JsonObject.IsValid())
                {
                    const TArray<TSharedPtr<FJsonValue>>* Data;
                    if (JsonObject->TryGetArrayField(TEXT("data"), Data))
                    {
                        const TArray<TSharedPtr<FJsonValue>>* Embedding;
                        if (Data->Num() > 0 && (*Data)[0]->AsObject()->TryGetArrayField(TEXT("embedding"), Embedding))
                        {
                            for (const TSharedPtr<FJsonValue>& Value : *Embedding)
                            {
                                EmbeddingResult.Add(Value->AsNumber());
                            }

                            UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | LLM | RAG] Embedding received."));
                        }
                        else
                        {
                            UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | LLM | RAG] Embedding field not found in response: %s"), *Content);
                        }
                    }
                    else
                    {
                        UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | LLM | RAG] Data field not found in response: %s"), *Content);
                    }
                }
                else
                {
                    UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | LLM | RAG] Failed to parse JSON response: %s"), *Content);
                }
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | LLM | RAG] Embedding request failed: %s"), Res.IsValid() ? *Res->GetContentAsString() : TEXT("No response"));
            }

            CompletionEvent->Trigger();
        });

    Request->ProcessRequest();
    UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | LLM | RAG] Embedding request sent to %s"), *Url);

    CompletionEvent->Wait();
    FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);

    return EmbeddingResult;
}

void ULLMComponent::GenerateKnowledge()
{
    Knowledge.Empty();

    FString FileContent;
    if (!FFileHelper::LoadFileToString(FileContent, *KnowledgePath))
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | LLM | RAG] Failed to read document: %s"), *KnowledgePath);
        return;
    }

    TArray<FString> Sentences;
    FString AccumulatedSentence;
    for (int32 i = 0; i < FileContent.Len(); ++i)
    {
        const TCHAR c = FileContent[i];
        AccumulatedSentence.AppendChar(c);
        if ((c == '.' || c == '!' || c == '?') && (i + 1 >= FileContent.Len() || FileContent[i + 1] == ' '
            || FileContent[i + 1] == '\n' || FileContent[i + 1] == '\r' || FileContent[i + 1] == '\t'))
        {
            FString S = AccumulatedSentence.TrimStartAndEnd();
            if (!S.IsEmpty())
            {
                Sentences.Add(S);
            }
            AccumulatedSentence.Empty();
        }
    }
    if (!AccumulatedSentence.TrimStartAndEnd().IsEmpty())
    {
        Sentences.Add(AccumulatedSentence.TrimStartAndEnd());
    }

    int32 Step = FMath::Max(1, SentencesPerChunk - SentenceOverlap);
    for (int32 i = 0; i < Sentences.Num(); i += Step)
    {
        int32 EndIdx = FMath::Min(i + SentencesPerChunk, Sentences.Num());

        FString ChunkText;
        for (int32 j = i; j < EndIdx; j++)
        {
            if (!ChunkText.IsEmpty())
                ChunkText += TEXT(" ");
            ChunkText += Sentences[j];
        }

        TArray<float> Emb = EmbedText(ChunkText);

        FKnowledgeEntry Chunk;
        Chunk.Text = ChunkText;
        Chunk.Embedding = Emb;
        Knowledge.Add(Chunk);
    }
    UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | LLM | RAG] Generated knowledge."));
}

float ULLMComponent::ComputeCosineSimilarity(const TArray<float>& A, const TArray<float>& B)
{
    if (A.Num() == 0 || B.Num() == 0 || A.Num() != B.Num())
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAIForNPCs | LLM | RAG] Invalid vectors for cosine similarity calculation"));
        return 0;
    }
    double DotProduct = 0.0;
    double NormA = 0.0;
    double NormB = 0.0;

    for (int32 i = 0; i < A.Num(); i++)
    {
        DotProduct += A[i] * B[i];
        NormA += A[i] * A[i];
        NormB += B[i] * B[i];
    }

    double Denom = FMath::Sqrt(NormA) * FMath::Sqrt(NormB);
    if (Denom <= KINDA_SMALL_NUMBER)
    {
        return 0.0f;
    }

    return static_cast<float>(DotProduct / Denom);
}

TArray<FString> ULLMComponent::GetTopKDocuments(const TArray<float>& QueryEmbedding)
{
    TArray<FString> TopChunks;

    if (Knowledge.Num() == 0 || QueryEmbedding.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAIForNPCs | LLM | RAG] No knowledge available or query embedding is empty."));
        return TopChunks;
    }

    struct FScoredChunk
    {
        float Score;
        FString Text;
    };

    TArray<FScoredChunk> ScoredChunks;
    ScoredChunks.Reserve(Knowledge.Num());

    for (const FKnowledgeEntry& Entry : Knowledge)
    {
        float Similarity = ComputeCosineSimilarity(QueryEmbedding, Entry.Embedding);
        ScoredChunks.Add({ Similarity, Entry.Text });
    }

    ScoredChunks.Sort([](const FScoredChunk& A, const FScoredChunk& B)
        {
            return A.Score > B.Score;
        });

    int32 Count = FMath::Min(EmbeddingTopK, ScoredChunks.Num());
    for (int32 i = 0; i < Count; i++)
    {
        if (ScoredChunks[i].Score >= SimilarityThreshold)
        {
            TopChunks.Add(ScoredChunks[i].Text);
        }
    }

    UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | LLM | RAG] Selected top-%d chunks out of %d knowledge entries."), Count, ScoredChunks.Num());

    return TopChunks;
}

TArray<FString> ULLMComponent::RerankDocuments(const FString& Query, const TArray<FString>& Documents)
{
    TArray<FString> RerankedDocs;

    if (Documents.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAIForNPCs | LLM | RAG] No documents provided for reranking."));
        return RerankedDocs;
    }

    TSharedPtr<FJsonObject> JsonRequest = MakeShared<FJsonObject>();
    JsonRequest->SetStringField("query", Query);
    JsonRequest->SetNumberField("top_n", RerankingTopN);

    TArray<TSharedPtr<FJsonValue>> JsonDocs;
    for (const FString& Doc : Documents)
    {
        JsonDocs.Add(MakeShared<FJsonValueString>(Doc));
    }
    JsonRequest->SetArrayField("documents", JsonDocs);

    FString RequestString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestString);
    FJsonSerializer::Serialize(JsonRequest.ToSharedRef(), Writer);

    FString Url = FString::Printf(TEXT("http://localhost:%d/v1/rerank"), RerankerPort);
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Url);
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Request->SetContentAsString(RequestString);

    FEvent* CompletionEvent = FPlatformProcess::GetSynchEventFromPool(true);

    Request->OnProcessRequestComplete().BindLambda([&](FHttpRequestPtr Req, FHttpResponsePtr Res, bool bConnected)
        {
            if (bConnected && Res.IsValid() && EHttpResponseCodes::IsOk(Res->GetResponseCode()))
            {
                FString Content = Res->GetContentAsString();
                TSharedPtr<FJsonObject> JsonObject;
                TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);

                if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
                {
                    const TArray<TSharedPtr<FJsonValue>>* Results;
                    if (JsonObject->TryGetArrayField(TEXT("results"), Results))
                    {
                        struct FScoredDoc
                        {
                            float Score;
                            int32 Index;
                        };

                        TArray<FScoredDoc> ScoredDocs;
                        for (const TSharedPtr<FJsonValue>& Value : *Results)
                        {
                            TSharedPtr<FJsonObject> Obj = Value->AsObject();
                            if (Obj.IsValid())
                            {
                                int32 Idx = Obj->GetIntegerField(TEXT("index"));
                                float Score = Obj->GetNumberField(TEXT("relevance_score"));
                                ScoredDocs.Add({ Score, Idx });
                            }
                            else
                            {
                                UE_LOG(LogTemp, Warning, TEXT("[LocalAIForNPCs | LLM | RAG] Invalid document object in reranker response: %s"), *Content);
                            }
                        }

                        ScoredDocs.Sort([](const FScoredDoc& A, const FScoredDoc& B)
                            {
                                return A.Score > B.Score;
                            });

                        int32 Count = FMath::Min(RerankingTopN, ScoredDocs.Num());
                        for (int32 i = 0; i < Count; i++)
                        {
                            int32 DocIdx = ScoredDocs[i].Index;
                            if (Documents.IsValidIndex(DocIdx))
                            {
                                RerankedDocs.Add(Documents[DocIdx]);
                            }
                            else
                            {
                                UE_LOG(LogTemp, Warning, TEXT("[LocalAIForNPCs | LLM | RAG] Document index %d out of bounds for reranked documents."), DocIdx);
                            }
                        }

                        UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | LLM | RAG] Reranked %d documents."), Count);
                    }
                    else
                    {
                        UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | LLM | RAG] No 'results' in reranker response: %s"), *Content);
                    }
                }
                else
                {
                    UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | LLM | RAG] Failed to parse reranker response: %s"), *Content);
                }
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | LLM | RAG] Rerank request failed: %s"), Res.IsValid() ? *Res->GetContentAsString() : TEXT("No response"));
            }

            CompletionEvent->Trigger();
        });

    Request->ProcessRequest();
    UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | LLM | RAG] Rerank request sent to %s"), *Url);

    CompletionEvent->Wait();
    FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);

    if (RerankedDocs.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAIForNPCs | LLM | RAG] No documents returned from reranker. Returning embedding mode results."));
        for (int i = 0; i < FMath::Min(RerankingTopN, Documents.Num()); i++)
        {
            RerankedDocs.Add(Documents[i]);
        }
    }

    return RerankedDocs;
}

void ULLMComponent::HandleNpcAction(const FString& ActionCommand)
{
    FString Action;
    FString Object;

    FNpcAction* FoundAction = nullptr;
    for (FNpcAction& A : KnownActions)
    {
        if (ActionCommand.StartsWith(A.Name, ESearchCase::IgnoreCase))
        {
            if (!FoundAction || A.Name.Len() > FoundAction->Name.Len())
            {
                FoundAction = &A;
            }
        }
    }

    if (FoundAction)
    {
        if (FoundAction->bHasTargetObject)
        {
            Object = ActionCommand.Mid(FoundAction->Name.Len()).TrimStartAndEnd();

            FNpcObject* FoundObject = KnownObjects.FindByPredicate(
                [&](const FNpcObject& O) { return O.Name.Equals(Object, ESearchCase::IgnoreCase); });

            if (FoundObject)
            {
                UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | LLM | Actions] Executing action \"%s\" on object \"%s\""), *FoundAction->Name, *FoundObject->Name);
                OnActionReceived.Broadcast(FoundAction->Name, FoundObject->ActorRef);
                return;
            }
        }
        else
        {
            UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | LLM | Actions] Executing action \"%s\""), *FoundAction->Name);
            OnActionReceived.Broadcast(FoundAction->Name, nullptr);
            return;
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("[LocalAIForNPCs | LLM | Actions] Unknown action command: %s"), *ActionCommand);
}


FString ULLMComponent::BuildActionsSystemMessage()
{
    FString Message;
    Message += TEXT("You can do two things:\n");
    Message += TEXT("1. Speak normally as dialogue with the user.\n");
    Message += TEXT("2. Perform actions by inserting action tags directly into your dialogue, if suitable in the current context.\n\n");

    Message += TEXT("The action tag must always respect the following format:\n");
    Message += TEXT("[[action: <action name> <optional object name>]]\n");

    Message += TEXT("Examples:\n");
    Message += TEXT("- I'm so tired [[action: sit]]\n");
    Message += TEXT("- Follow me [[action: move to door]]\n\n");

    Message += TEXT("The only actions you can use in the action tags are:\n");
    for (const auto& Action : KnownActions)
    {
        Message += FString::Printf(TEXT("- %s: %s"), *Action.Name, *Action.Description);
        if (Action.bHasTargetObject)
        {
            Message += TEXT(" (requires an object)");
        }
        Message += TEXT("\n");
    }

    Message += TEXT("\nThe only objects you can use in the action tags are:\n");
    for (const auto& Object : KnownObjects)
    {
        Message += FString::Printf(TEXT("- %s: %s\n"), *Object.Name, *Object.Description);
    }

    return Message;
}