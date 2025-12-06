#include "TTSComponent.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundWaveProcedural.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#if WITH_AUDIO2FACE
#include "ACEBlueprintLibrary.h"
#include "ACERuntimeModule.h"
#include "ACEAudioCurveSourceComponent.h"
#endif

UTTSComponent::UTTSComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UTTSComponent::BeginPlay()
{
    Super::BeginPlay();

    if (!IFileManager::Get().DirectoryExists(*OutputAudioFolder))
    {
        IFileManager::Get().MakeDirectory(*OutputAudioFolder, true);
    }

#if !PLATFORM_WINDOWS
    if (LipSyncMode == ELipSyncMode::NeuroSync)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | TTS | LipSync] NeuroSync is only supported on Windows platform for now. Disabling LipSync."));
        LipSyncMode = ELipSyncMode::Disabled;
    }
#endif

    if (LipSyncMode == ELipSyncMode::Audio2Face)
    {
#if WITH_AUDIO2FACE
        FACERuntimeModule::Get().AllocateA2F3DResources(FName(Audio2FaceProvider));
#else
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | TTS | LipSync] Audio2Face integration is not enabled in this build. Disabling LipSync."));
        LipSyncMode = ELipSyncMode::Disabled;
#endif
    }
}

void UTTSComponent::CreateSoundWave(const FString& Text)
{
    if (Text.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | TTS] Text is empty. Skipping..."));

        OnSoundReady.Broadcast(TArray<uint8>(), Text);
        return;
    }

    if (Voice.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | TTS] Voice is not set. Please set a voice before generating audio."));

        OnSoundReady.Broadcast(TArray<uint8>(), Text);
        return;
    }

    FString Url = FString::Printf(TEXT("http://localhost:%d/v1/audio/speech"), Port);
    FString Content = CreateJsonRequest(Text);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Url);
    Request->SetVerb("POST");
    Request->SetHeader("Content-Type", "application/json");
    Request->SetContentAsString(Content);

    Request->OnProcessRequestComplete().BindLambda([this, Text](FHttpRequestPtr Req, FHttpResponsePtr Response, bool bWasSuccessful)
        {
            if (!bWasSuccessful || !Response.IsValid())
            {
                UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | Whisper] Request failed."));

                AsyncTask(ENamedThreads::GameThread, [this, Text]()
                    {
                        OnSoundReady.Broadcast(TArray<uint8>(), Text);
                    });

                return;
            }

            int32 Code = Response->GetResponseCode();
            if (Code != 200)
            {
                UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | Whisper] HTTP %d: %s"), Code, *Response->GetContentAsString());

                AsyncTask(ENamedThreads::GameThread, [this, Text]()
                    {
                        OnSoundReady.Broadcast(TArray<uint8>(), Text);
                    });

                return;
            }

            const TArray<uint8>& AudioData = Response->GetContent();
            if (!AudioData.IsEmpty())
            {
                UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | TTS] Audio generated."));

                AsyncTask(ENamedThreads::GameThread, [this, AudioData, Text]()
                    {
                        OnSoundReady.Broadcast(AudioData, Text);
                    });
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | TTS] Received empty response."));

                AsyncTask(ENamedThreads::GameThread, [this, Text]()
                    {
                        OnSoundReady.Broadcast(TArray<uint8>(), Text);
                    });
            }
        });

    Request->ProcessRequest();
    UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | TTS] Request sent to %s."), *Url);
}

FString UTTSComponent::CreateJsonRequest(FString Input) const
{
    TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
    RootObject->SetStringField("input", Input);
    RootObject->SetStringField("voice", Voice);
    RootObject->SetStringField("response_format", TEXT("wav"));

    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

    return OutputString;
}

FSoundWaveWithDuration UTTSComponent::LoadSoundWaveFromWav(const TArray<uint8>& AudioData)
{
    FWaveModInfo WaveInfo;
    if (!WaveInfo.ReadWaveInfo(AudioData.GetData(), AudioData.Num()))
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | TTS] Failed to parse WAV"));
        return { nullptr, 0.0f };
    }

    const uint16* Channels = WaveInfo.pChannels;
    const uint32* SampleRate = WaveInfo.pSamplesPerSec;
    const uint16* BitsPerSample = WaveInfo.pBitsPerSample;
    const uint32 ByteRate = (*SampleRate) * (*Channels) * (*BitsPerSample) / 8;
    const uint8* PcmData = WaveInfo.SampleDataStart;
    const int32 PcmDataSize = WaveInfo.SampleDataSize;

    if (!PcmData || PcmDataSize <= 0)
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | TTS] No valid PCM data in WAV."));
        return { nullptr, 0.0f };
    }

    USoundWaveProcedural* SoundWave = NewObject<USoundWaveProcedural>();
    SoundWave->SetSampleRate(*SampleRate);
    SoundWave->NumChannels = *Channels;
    SoundWave->SoundGroup = SOUNDGROUP_Default;
    SoundWave->bLooping = false;
    SoundWave->Duration = INDEFINITELY_LOOPING_DURATION;
    SoundWave->QueueAudio(PcmData, PcmDataSize);

    float Duration = static_cast<float>(PcmDataSize) / ByteRate;

    return { SoundWave, Duration };
}

void UTTSComponent::PlaySpeech(const TArray<uint8>& AudioData)
{
    switch (LipSyncMode)
    {
    case ELipSyncMode::Disabled:
    {
        PlaySoundWave(AudioData);
        break;
    }
    case ELipSyncMode::NeuroSync:
    {
        PlaySoundWithNeuroSync(AudioData);
        break;
    }
    case ELipSyncMode::Audio2Face:
    {
        PlaySoundWithAudio2Face(AudioData);
        break;
    }
    default:
    {
        PlaySoundWave(AudioData);
        break;
    }
    }
}

void UTTSComponent::PlaySoundWave(const TArray<uint8>& AudioData)
{
    FSoundWaveWithDuration Sound = LoadSoundWaveFromWav(AudioData);

    if (!Sound.SoundWave)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | TTS] SoundWave is null. Skipping..."));
        return;
    }

    FScopeLock Lock(&SoundQueueLock);
    SoundQueue.Enqueue(Sound);

    if (!bIsPlayingSound)
    {
        PlayNextInQueue();
    }
}

void UTTSComponent::PlayNextInQueue()
{
    FSoundWaveWithDuration NextSound;
    FScopeLock Lock(&SoundQueueLock);
    if (bIsPlayingSound || SoundQueue.IsEmpty() || !SoundQueue.Dequeue(NextSound))
    {
        return;
    }

    bIsPlayingSound = true;

    FVector Location = GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
    USoundAttenuation* CustomAttenuation = NewObject<USoundAttenuation>();
    CustomAttenuation->Attenuation.bAttenuate = true;
    CustomAttenuation->Attenuation.bAttenuateWithLPF = true;
    CustomAttenuation->Attenuation.bSpatialize = true;
    CustomAttenuation->Attenuation.AbsorptionMethod = EAirAbsorptionMethod::Linear;
    CustomAttenuation->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
    CustomAttenuation->Attenuation.FalloffDistance = 1000.f;
    UGameplayStatics::PlaySoundAtLocation(this, NextSound.SoundWave, Location, 1.0f, 1.0f, 0.0f, CustomAttenuation);

    UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | TTS] Playing sound for %.2f seconds."), NextSound.Duration);
    GetWorld()->GetTimerManager().SetTimer(AudioFinishTimer, this, &UTTSComponent::AudioFinishedHandler, NextSound.Duration, false);
}

void UTTSComponent::AudioFinishedHandler()
{
    bIsPlayingSound = false;
    GetWorld()->GetTimerManager().ClearTimer(AudioFinishTimer);

    PlayNextInQueue();
}

void UTTSComponent::PlaySoundWithNeuroSync(const TArray<uint8>& AudioData)
{
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(FString::Printf(TEXT("http://127.0.0.1:%d/audio_to_blendshapes"), NeuroSyncPort));
    Request->SetVerb("POST");
    Request->SetHeader(TEXT("Content-Type"), "application/octet-stream");
    Request->SetContent(AudioData);

    Request->OnProcessRequestComplete().BindLambda([this, AudioData](FHttpRequestPtr Req, FHttpResponsePtr Response, bool bSuccess)
        {
            UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | TTS | LipSync] NeuroSync response received."));

            if (!bSuccess || !Response.IsValid())
            {
                UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | TTS | LipSync] NeuroSync request failed"));
                return;
            }

            FString ResponseStr = Response->GetContentAsString();
            TSharedPtr<FJsonObject> Json;
            TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseStr);
            if (!FJsonSerializer::Deserialize(Reader, Json))
            {
                UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | TTS | LipSync] Failed to parse NeuroSync JSON response: %s"), *ResponseStr);
                return;
            }

            const TArray<TSharedPtr<FJsonValue>>* Frames;
            if (!Json->TryGetArrayField(TEXT("blendshapes"), Frames))
            {
                UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | TTS | LipSync] NeuroSync response missing 'blendshapes' field."));
                return;
            }

            FNeuroSyncData Data;
            Data.AudioData = AudioData;

            for (auto& FrameVal : *Frames)
            {
                TArray<TSharedPtr<FJsonValue>> FrameArray = FrameVal->AsArray();
                TArray<float> FrameData;
                for (auto& Value : FrameArray)
                {
                    FrameData.Add(Value->AsNumber());
                }
                Data.BlendshapeFrames.Add(FrameData);
            }

            FScopeLock Lock(&NeuroQueueLock);
            NeuroQueue.Enqueue(Data);
            if (!bIsPlayingNeuro)
            {
                PlayNextNeuroInQueue();
            }
        });

    Request->ProcessRequest();
}

void UTTSComponent::PlayNextNeuroInQueue()
{
    FNeuroSyncData NextData;
    FScopeLock Lock(&NeuroQueueLock);
    if (bIsPlayingNeuro || NeuroQueue.IsEmpty() || !NeuroQueue.Dequeue(NextData))
    {
        return;
    }

    bIsPlayingNeuro = true;

    FSoundWaveWithDuration Sound = LoadSoundWaveFromWav(NextData.AudioData);

    if (!Sound.SoundWave)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | TTS] SoundWave is null. Skipping..."));
        bIsPlayingNeuro = false;
        PlayNextNeuroInQueue();
        return;
    }

    if (NextData.BlendshapeFrames.Num() == 0)
    {
        FVector Location = GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
        USoundAttenuation* CustomAttenuation = NewObject<USoundAttenuation>();
        CustomAttenuation->Attenuation.bAttenuate = true;
        CustomAttenuation->Attenuation.bAttenuateWithLPF = true;
        CustomAttenuation->Attenuation.bSpatialize = true;
        CustomAttenuation->Attenuation.AbsorptionMethod = EAirAbsorptionMethod::Linear;
        CustomAttenuation->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
        CustomAttenuation->Attenuation.FalloffDistance = 1000.f;
        UGameplayStatics::PlaySoundAtLocation(this, Sound.SoundWave, Location, 1.0f, 1.0f, 0.0f, CustomAttenuation);

        UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | TTS] Playing sound for %.2f seconds."), Sound.Duration);
        GetWorld()->GetTimerManager().SetTimer(NeuroFinishTimer, this, &UTTSComponent::NeuroFinishedHandler, Sound.Duration, false);
        return;
    }

    FString BlendshapeString = "[";
    for (int32 i = 0; i < NextData.BlendshapeFrames.Num(); ++i)
    {
        BlendshapeString += "[";
        for (int32 j = 0; j < NextData.BlendshapeFrames[i].Num(); ++j)
        {
            BlendshapeString += FString::SanitizeFloat(NextData.BlendshapeFrames[i][j]);
            if (j < NextData.BlendshapeFrames[i].Num() - 1)
            {
                BlendshapeString += ", ";
            }
        }
        BlendshapeString += "]";
        if (i < NextData.BlendshapeFrames.Num() - 1)
        {
            BlendshapeString += ", ";
        }
    }
    BlendshapeString += "]";

    FString Guid = FGuid::NewGuid().ToString(EGuidFormats::Short);
    FString TempFilePath = FPaths::Combine(OutputAudioFolder, FString::Printf(TEXT("blendshapes-%s.txt"), *Guid));
    FFileHelper::SaveStringToFile(BlendshapeString, *TempFilePath);

    FString ScriptPath = FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("LocalAIForNPCs"), TEXT("Source"), TEXT("ThirdParty"),
        TEXT("NeuroSync"), TEXT("dist"), TEXT("run_neurosync"), TEXT("run_neurosync.exe"));

    Async(EAsyncExecution::Thread, [this, ScriptPath, TempFilePath, Sound]()
        {
            void* PipeRead = nullptr;
            void* PipeWrite = nullptr;
            FPlatformProcess::CreatePipe(PipeRead, PipeWrite);

            FString Args = FString::Printf(TEXT("\"%s\" \"%s\""), *TempFilePath, *FaceSubjectName);
            FProcHandle ProcHandle = FPlatformProcess::CreateProc(
                *ScriptPath, *Args,
                true, true, true,
                nullptr, 0, nullptr,
                PipeWrite);

            if (!ProcHandle.IsValid())
            {
                UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | TTS | LipSync] Failed to launch NeuroSync process."));
            }

            FString Output;
            bool bReadyFound = false;

            while (FPlatformProcess::IsProcRunning(ProcHandle) && !bReadyFound)
            {
                FString NewOutput = FPlatformProcess::ReadPipe(PipeRead);
                if (!NewOutput.IsEmpty())
                {
                    Output += NewOutput;
                    if (Output.Contains("READY"))
                    {
                        bReadyFound = true;

                        AsyncTask(ENamedThreads::GameThread, [this, Sound]()
                            {
                                FVector Location = GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
                                USoundAttenuation* CustomAttenuation = NewObject<USoundAttenuation>();
                                CustomAttenuation->Attenuation.bAttenuate = true;
                                CustomAttenuation->Attenuation.bAttenuateWithLPF = true;
                                CustomAttenuation->Attenuation.bSpatialize = true;
                                CustomAttenuation->Attenuation.AbsorptionMethod = EAirAbsorptionMethod::Linear;
                                CustomAttenuation->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
                                CustomAttenuation->Attenuation.FalloffDistance = 1000.f;
                                UGameplayStatics::PlaySoundAtLocation(this, Sound.SoundWave, Location, 1.0f, 1.0f, 0.0f, CustomAttenuation);

                                UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | TTS] Playing sound for %.2f seconds (synced)."), Sound.Duration);
                                GetWorld()->GetTimerManager().SetTimer(NeuroFinishTimer, this, &UTTSComponent::NeuroFinishedHandler, Sound.Duration, false);
                            });
                    }
                }
                FPlatformProcess::Sleep(0.01f);
            }

            if (!bReadyFound)
            {
                AsyncTask(ENamedThreads::GameThread, [this, Sound]()
                    {
                        FVector Location = GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
                        USoundAttenuation* CustomAttenuation = NewObject<USoundAttenuation>();
                        CustomAttenuation->Attenuation.bAttenuate = true;
                        CustomAttenuation->Attenuation.bAttenuateWithLPF = true;
                        CustomAttenuation->Attenuation.bSpatialize = true;
                        CustomAttenuation->Attenuation.AbsorptionMethod = EAirAbsorptionMethod::Linear;
                        CustomAttenuation->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
                        CustomAttenuation->Attenuation.FalloffDistance = 1000.f;
                        UGameplayStatics::PlaySoundAtLocation(this, Sound.SoundWave, Location, 1.0f, 1.0f, 0.0f, CustomAttenuation);

                        UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | TTS] Playing sound for %.2f seconds."), Sound.Duration);
                        GetWorld()->GetTimerManager().SetTimer(NeuroFinishTimer, this, &UTTSComponent::NeuroFinishedHandler, Sound.Duration, false);
                    });
            }

            FPlatformProcess::ClosePipe(PipeRead, PipeWrite);
        });
}

void UTTSComponent::NeuroFinishedHandler()
{
    bIsPlayingNeuro = false;
    GetWorld()->GetTimerManager().ClearTimer(NeuroFinishTimer);

    PlayNextNeuroInQueue();
}

void UTTSComponent::PlaySoundWithAudio2Face(const TArray<uint8>& AudioData)
{
#if WITH_AUDIO2FACE
    FScopeLock Lock(&A2FQueueLock);
    A2FQueue.Enqueue(AudioData);

    if (!bIsPlayingA2F)
    {
        PlayNextA2FInQueue();
    }
#else
    UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | TTS | LipSync] Audio2Face integration is not enabled in this build."));
    PlaySoundWave(AudioData);
#endif
}

void UTTSComponent::PlayNextA2FInQueue()
{
#if WITH_AUDIO2FACE
    TArray<uint8> NextData;
    FScopeLock Lock(&A2FQueueLock);
    if (bIsPlayingA2F || A2FQueue.IsEmpty() || !A2FQueue.Dequeue(NextData))
    {
        return;
    }

    bIsPlayingA2F = true;

    FSoundWaveWithDuration Sound = LoadSoundWaveFromWav(NextData);

    if (!Sound.SoundWave)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | TTS] SoundWave is null. Skipping..."));
        bIsPlayingA2F = false;
        PlayNextA2FInQueue();
        return;
    }

    AActor* Owner = GetOwner();
    UACEAudioCurveSourceComponent* AudioCurveComp;
    if (Owner)
    {
        AudioCurveComp = Owner->FindComponentByClass<UACEAudioCurveSourceComponent>();
        if (!AudioCurveComp)
        {

            UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | TTS | LipSync] No UACEAudioCurveSourceComponent found on %s."), *Owner->GetName());
            bIsPlayingA2F = false;
            PlayNextA2FInQueue();
            return;
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | TTS | LipSync] No Owner found."));
        bIsPlayingA2F = false;
        PlayNextA2FInQueue();
        return;
    }
    FAudio2FaceEmotion EmotionParams = FAudio2FaceEmotion();

    FWaveModInfo WaveInfo;
    if (!WaveInfo.ReadWaveInfo(NextData.GetData(), NextData.Num()))
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | TTS] Invalid WAV data."));
        return;
    }
    int32 NumChannels = *WaveInfo.pChannels;
    int32 SampleRate = *WaveInfo.pSamplesPerSec;
    const int32 NumSamples = WaveInfo.SampleDataSize / sizeof(int16);
    const int16* SamplePtr = reinterpret_cast<const int16*>(WaveInfo.SampleDataStart);
    TArrayView<const int16> SamplesView = TArrayView<const int16>(SamplePtr, NumSamples);

    Async(EAsyncExecution::Thread, [this, AudioCurveComp, SamplesView, NumChannels, SampleRate, EmotionParams]()
        {
            FACERuntimeModule::Get().AnimateFromAudioSamples(AudioCurveComp, SamplesView, NumChannels, SampleRate, true, EmotionParams, nullptr, FName(Audio2FaceProvider));
            // TODO: set bEndOfSamples to false to have ACE manage the queue automatically. 
            // All AnimateFromAudioSamples() and EndAudioSamples() (which should be called after the last chunk is received) calls must be sent from the same thread. I tried but it didn't work.
        });

    UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | TTS] Playing sound for %.2f seconds (synced)."), Sound.Duration);
    GetWorld()->GetTimerManager().SetTimer(AudioFinishTimer, this, &UTTSComponent::A2FFinishedHandler, Sound.Duration + 0.5, false);
#else
    UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | TTS | LipSync] Audio2Face integration is not enabled in this build."));
#endif
}

void UTTSComponent::A2FFinishedHandler()
{
#if WITH_AUDIO2FACE
    bIsPlayingA2F = false;
    GetWorld()->GetTimerManager().ClearTimer(A2FFinishTimer);

    PlayNextA2FInQueue();
#else
    UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | TTS | LipSync] Audio2Face integration is not enabled in this build."));
#endif
}

void UTTSComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Super::EndPlay(EndPlayReason);

    if (!OutputAudioFolder.IsEmpty() && IFileManager::Get().DirectoryExists(*OutputAudioFolder))
    {
        TArray<FString> FilesToDelete;
        IFileManager::Get().FindFiles(FilesToDelete, *OutputAudioFolder);

        for (const FString& File : FilesToDelete)
        {
            FString FullPath = FPaths::Combine(OutputAudioFolder, File);
            if (!IFileManager::Get().Delete(*FullPath, false, true))
            {
                UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | TTS] Failed to delete file: %s"), *FullPath);
            }
        }
        UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | TTS] Cleaned up files in %s"), *OutputAudioFolder);
    }
}