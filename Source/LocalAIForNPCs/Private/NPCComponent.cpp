#include "NPCComponent.h"
#include "PlayerComponent.h"

UNPCComponent::UNPCComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UNPCComponent::StartRecording()
{
    if (!ASRComponent)
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | NPCComponent] ASRComponent is not initialized."));
        return;
    }

    if (!bIsUsersConversationTurn)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAIForNPCs | NPCComponent] Not the user's turn to speak."));
        return;
    }

    if (bIsRecording)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAIForNPCs | NPCComponent] ASR recording is already active."));
        return;
    }

    bIsUsersConversationTurn = false;
    bIsRecording = true;

    ASRComponent->StartRecording();
}

void UNPCComponent::StopRecordingAndSendAudio()
{
    if (!ASRComponent)
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | NPCComponent] ASRComponent is not initialized."));
        return;
    }

    if (!bIsRecording)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAIForNPCs | NPCComponent] ASR recording is not active."));
        return;
    }

    FString AudioPath = ASRComponent->StopRecording();
    ASRComponent->TranscribeAudio(AudioPath);

    bIsRecording = false;
}

void UNPCComponent::SendText(FString Input)
{
    if (!LLMComponent)
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | NPCComponent] LLMComponent is not initialized."));
        return;
    }

    if (!bIsUsersConversationTurn)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAIForNPCs | NPCComponent] Not the user's turn to speak."));
        return;
    }

    bIsUsersConversationTurn = false;

    LLMComponent->SendChatMessage(Input);
}

void UNPCComponent::BeginPlay()
{
    Super::BeginPlay();

    AActor* Owner = GetOwner();

    ASRComponent = NewObject<UASRComponent>(this, UASRComponent::StaticClass(), TEXT("ASRComponent"));
    if (ASRComponent)
    {

        ASRComponent->Port = ASRPort;

        ASRComponent->RegisterComponent();

        ASRComponent->OnTranscriptionComplete.AddDynamic(this, &UNPCComponent::HandleTranscriptionComplete);
    }

    LLMComponent = NewObject<ULLMComponent>(this, ULLMComponent::StaticClass(), TEXT("LLMComponent"));
    if (LLMComponent)
    {
        LLMComponent->Port = LLMPort;
        LLMComponent->SystemMessage = SystemMessage;
        LLMComponent->bStream = bStream;

        LLMComponent->RagMode = RagMode;
        LLMComponent->EmbeddingPort = EmbeddingPort;
        LLMComponent->RerankerPort = RerankerPort;
        LLMComponent->KnowledgePath = KnowledgePath;
        LLMComponent->EmbeddingTopK = EmbeddingTopK;
        LLMComponent->RerankingTopN = RerankingTopN;
        LLMComponent->SentencesPerChunk = SentencesPerChunk;
        LLMComponent->SentenceOverlap = SentenceOverlap;

        LLMComponent->KnownActions = KnownActions;
        LLMComponent->KnownObjects = KnownObjects;

        LLMComponent->RegisterComponent();

        LLMComponent->OnResponseReceived.AddDynamic(this, &UNPCComponent::HandleResponseReceived);
        LLMComponent->OnStreamChunkReceived.AddDynamic(this, &UNPCComponent::HandleChunkReceived);
        LLMComponent->OnActionReceived.AddDynamic(this, &UNPCComponent::HandleActionReceived);
    }

    TTSComponent = NewObject<UTTSComponent>(this, UTTSComponent::StaticClass(), TEXT("TTSComponent"));
    if (TTSComponent)
    {
        TTSComponent->Port = TTSPort;
        TTSComponent->Voice = Voice;
        TTSComponent->LipSyncMode = LipSyncMode;
        TTSComponent->NeuroSyncPort = NeuroSyncPort;
        TTSComponent->FaceSubjectName = FaceSubjectName;
        TTSComponent->Audio2FaceProvider = Audio2FaceProvider;

        TTSComponent->RegisterComponent();
        TTSComponent->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);

        TTSComponent->OnSoundReady.AddDynamic(this, &UNPCComponent::HandleSoundReady);
    }

    if (!ASRComponent || !LLMComponent || !TTSComponent)
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | NPCComponent] Failed to initialize components."));
        return;
    }

    APawn* PlayerPawn = GetWorld()->GetFirstPlayerController()->GetPawn();
    if (PlayerPawn)
    {
        PlayerComponent = PlayerPawn->FindComponentByClass<UPlayerComponent>();
    }
}

void UNPCComponent::HandleTranscriptionComplete(const FString& Transcription)
{
    if (Transcription.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAIForNPCs | NPCComponent] Received empty transcription. Ignoring."));

        bIsUsersConversationTurn = true;
        return;
    }

    if (PlayerComponent && PlayerComponent->ChatWidgetInstance)
    {
        PlayerComponent->ChatWidgetInstance->AddMessage(PlayerComponent->Name, Transcription);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAIForNPCs | NPCComponent] PlayerAiComponent or ChatWidgetInstance not found."));
    }

    if (LLMComponent)
    {
        LLMComponent->SendChatMessage(Transcription);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | NPCComponent] LLMComponent is not initialized."));

        bIsUsersConversationTurn = true;
    }
}

void UNPCComponent::HandleResponseReceived(const FString& Response)
{
    if (Response.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAIForNPCs | NPCComponent] Received empty response."));

        if (TTSComponent)
        {
            TTSComponent->CreateSoundWave(TEXT("I'm sorry, I didn't understand that. Could you please repeat?"));
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | NPCComponent] TTSComponent is not initialized."));
        }

        return;
    }

    if (TTSComponent)
    {
        if (!bStream)
        {
            TTSComponent->CreateSoundWave(Response);
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | NPCComponent] TTSComponent is not initialized."));
    }
}

void UNPCComponent::HandleChunkReceived(const FString& Chunk, bool bDone)
{
    if (TTSComponent)
    {
        if (!Chunk.IsEmpty())
        {
            TTSComponent->CreateSoundWave(Chunk);

        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[LocalAIForNPCs | NPCComponent] Received empty chunk, skipping..."));
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | NPCComponent] TTSComponent is not initialized."));
    }
}

void UNPCComponent::HandleActionReceived(const FString& Action, AActor* Object)
{
    OnActionReceived.Broadcast(Action, Object);
}

void UNPCComponent::HandleSoundReady(const TArray<uint8>& AudioData, FString InputText)
{
    if (TTSComponent)
    {
        TTSComponent->PlaySpeech(AudioData);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | NPCComponent] TTSComponent is not initialized."));
    }

    if (PlayerComponent && PlayerComponent->ChatWidgetInstance)
    {
        PlayerComponent->ChatWidgetInstance->AddMessage(Name, InputText);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAIForNPCs | NPCComponent] PlayerAiComponent or ChatWidgetInstance not found."));
    }

    bIsUsersConversationTurn = true;
}