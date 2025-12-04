#include "ASRComponent.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "AudioResampler.h"

UASRComponent::UASRComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UASRComponent::BeginPlay()
{
    Super::BeginPlay();

    if (!IFileManager::Get().DirectoryExists(*RecordedAudioFolder))
    {
        IFileManager::Get().MakeDirectory(*RecordedAudioFolder, true);
    }

#if !(PLATFORM_WINDOWS && PLATFORM_64BITS)
    if (VadMode == EVadMode::WebRTC || VadMode == EVadMode::TEN)
    {
        // TODO: build libfvad and ten-vad for other platforms
        UE_LOG(LogTemp, Warning, TEXT("[LocalAIForNPCs | ASR] WebRTC and TEN VAD are only supported on Windows x64 platform for now. Switching to Energy-based VAD."));
        VadMode = EVadMode::EnergyBased;
    }
#endif

    Audio::FCaptureDeviceInfo DeviceInfo;
    if (!AudioCapture.GetCaptureDeviceInfo(DeviceInfo))
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | ASR] Failed to get capture device info."));
        return;
    }
    DeviceSampleRate = DeviceInfo.PreferredSampleRate;
    DeviceChannels = DeviceInfo.InputChannels;

    Audio::FAudioCaptureDeviceParams CaptureParams;
    Audio::FOnAudioCaptureFunction CaptureCallback = [this](const void* InAudio, int32 NumFrames, int32 NumChannels, int32 SampleRate, double StreamTime, bool bOverflow)
        {
            const float* AudioBuffer = static_cast<const float*>(InAudio);
            const int32 NumSamples = NumFrames * NumChannels;

            TArray<float> MonoBuffer;
            MonoBuffer.SetNumUninitialized(NumFrames);
            for (int32 i = 0; i < NumFrames; i++)
            {
                float Sum = 0.f;
                for (int32 c = 0; c < NumChannels; c++)
                {
                    Sum += AudioBuffer[i * NumChannels + c];
                }
                MonoBuffer[i] = Sum / NumChannels;
            }

            FScopeLock Lock(&AudioDataLock);

            if (IsSpeechFrame(MonoBuffer.GetData(), MonoBuffer.Num(), SampleRate))
            {
                CapturedAudioData.Append(MonoBuffer.GetData(), MonoBuffer.Num());
                SilenceSamplesCount = 0;
            }
            else if (CapturedAudioData.Num() > 0)
            {
                SilenceSamplesCount += MonoBuffer.Num();
                CapturedAudioData.Append(MonoBuffer.GetData(), MonoBuffer.Num());

                if (SilenceSamplesCount >= SecondsOfSilenceBeforeSend * SampleRate)
                {
                    if (CapturedAudioData.Num() >= MinSpeechDuration * SampleRate)
                    {
                        TArray<float> AudioToSave;
                        AudioToSave = CapturedAudioData;
                        CapturedAudioData.Empty();
                        SilenceSamplesCount = 0;

                        FString Guid = FGuid::NewGuid().ToString(EGuidFormats::Short);
                        FString AudioPath = FPaths::Combine(RecordedAudioFolder, FString::Printf(TEXT("ASR-%s.wav"), *Guid));

                        SaveWavFile(AudioToSave, AudioPath);
                        TranscribeAudio(AudioPath);
                    }
                    else
                    {
                        CapturedAudioData.Empty();
                        SilenceSamplesCount = 0;
                    }
                }
            }
        };

    if (!AudioCapture.OpenAudioCaptureStream(CaptureParams, CaptureCallback, 1024))
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | ASR] Failed to open audio capture stream."));
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | ASR] Audio capture stream opened successfully on device %s"), *DeviceInfo.DeviceName);

#if PLATFORM_WINDOWS && PLATFORM_64BITS
    if (VadMode == EVadMode::WebRTC)
    {
        WebRtcInstance = fvad_new();
        if (WebRtcInstance)
        {
            if (fvad_set_mode(WebRtcInstance, WebRtcVadAggressiveness) < 0)
            {
                UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | ASR | VAD] Failed to set aggressiveness mode! Switching to Energy-based VAD."));
                VadMode = EVadMode::EnergyBased;
            }

            if (fvad_set_sample_rate(WebRtcInstance, WebRtcSampleRate) < 0)
            {
                UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | ASR | VAD] Failed to set sample rate! Switching to Energy-based VAD."));
                VadMode = EVadMode::EnergyBased;
            }
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | ASR | VAD] Failed to create VAD instance! Switching to Energy-based VAD."));
            VadMode = EVadMode::EnergyBased;
        }
    }
    else if (VadMode == EVadMode::TEN)
    {
        if (ten_vad_create(&TenVadHandle, TenVadHopSize, TenVadThreshold) < 0)
        {
            UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | ASR | VAD] Failed to create TEN VAD instance! Switching to Energy-based VAD."));
            VadMode = EVadMode::EnergyBased;
        }
    }
#endif

    if (VadMode != EVadMode::Disabled)
    {
        StartRecording();
    }
}

void UASRComponent::StartRecording()
{
    if (!AudioCapture.IsStreamOpen())
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAIForNPCs | ASR] Stream is not open. Cannot start recording."));
        return;
    }

    if (AudioCapture.IsCapturing())
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAIForNPCs | ASR] Already recording."));
        return;
    }

    {
        FScopeLock Lock(&AudioDataLock);
        CapturedAudioData.Empty();
    }

    if (!AudioCapture.StartStream())
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | ASR] Failed to start audio capture stream."));
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | ASR] Audio capture stream started."));
}

FString UASRComponent::StopRecording()
{
    if (VadMode != EVadMode::Disabled)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAIForNPCs | ASR] VAD is enabled. Recording will stop automatically when speech is detected."));
        return TEXT("");
    }

    if (!AudioCapture.IsStreamOpen() || !AudioCapture.IsCapturing())
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAIForNPCs | ASR] Not currently recording."));
        return TEXT("");
    }

    if (!AudioCapture.StopStream())
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | ASR] Failed to stop audio capture stream."));
        return TEXT("");
    }

    UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | ASR] Recording stopped. Saving WAV file..."));

    TArray<float> AudioToSave;
    {
        FScopeLock Lock(&AudioDataLock);
        AudioToSave = CapturedAudioData;
        CapturedAudioData.Empty();
    }

    FString Guid = FGuid::NewGuid().ToString(EGuidFormats::Short);
    FString AudioPath = FPaths::Combine(RecordedAudioFolder, FString::Printf(TEXT("ASR-%s.wav"), *Guid));

    SaveWavFile(AudioToSave, AudioPath);

    return AudioPath;
}

void UASRComponent::SaveWavFile(const TArray<float>& InAudioData, FString OutputPath) const
{
    if (InAudioData.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAIForNPCs | ASR] No audio data provided to save as WAV."));
        return;
    }

    int32 SampleRate = DeviceSampleRate;
    int32 NumChannels = DeviceChannels;
    int32 BitsPerSample = 16;

    TArray<uint8> WavData;
    int32 NumSamples = InAudioData.Num();
    int32 DataSize = NumSamples * sizeof(int16);
    int32 FileSize = 44 + DataSize - 8;

    WavData.Append((uint8*)"RIFF", 4);
    WavData.Append((uint8*)&FileSize, 4);
    WavData.Append((uint8*)"WAVE", 4);

    WavData.Append((uint8*)"fmt ", 4);
    uint32 Subchunk1Size = 16;
    uint16 AudioFormat = 1;
    WavData.Append((uint8*)&Subchunk1Size, 4);
    WavData.Append((uint8*)&AudioFormat, 2);
    WavData.Append((uint8*)&NumChannels, 2);
    WavData.Append((uint8*)&SampleRate, 4);

    uint32 ByteRate = SampleRate * NumChannels * BitsPerSample / 8;
    uint16 BlockAlign = NumChannels * BitsPerSample / 8;
    WavData.Append((uint8*)&ByteRate, 4);
    WavData.Append((uint8*)&BlockAlign, 2);
    WavData.Append((uint8*)&BitsPerSample, 2);

    WavData.Append((uint8*)"data", 4);
    WavData.Append((uint8*)&DataSize, 4);

    for (int32 i = 0; i < InAudioData.Num(); i += NumChannels)
    {
        float MonoSample = 0.0f;
        for (int32 c = 0; c < NumChannels; ++c)
            MonoSample += InAudioData[i + c];
        MonoSample /= NumChannels;

        int16 IntSample = static_cast<int16>(FMath::Clamp(MonoSample, -0.999f, 0.999f) * 32767.0f);
        WavData.Append((uint8*)&IntSample, sizeof(int16));
    }

    if (FFileHelper::SaveArrayToFile(WavData, *OutputPath))
    {
        UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | ASR] WAV file saved to: %s"), *OutputPath);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | ASR] Failed to save WAV file to: %s"), *OutputPath);
    }
}

void UASRComponent::TranscribeAudio(const FString& AudioPath)
{
    if (!FPaths::FileExists(AudioPath))
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | ASR] Audio file not found: %s"), *AudioPath);

        OnTranscriptionComplete.Broadcast(TEXT(""));

        return;
    }

    FString Url = FString::Printf(TEXT("http://localhost:%d/inference"), Port);
    TArray<uint8> Content = CreateMultiPartRequest(AudioPath);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Url);
    Request->SetVerb("POST");
    Request->SetHeader("Content-Type", "multipart/form-data; boundary=" + CurrentBoundary);
    Request->SetContent(Content);

    Request->OnProcessRequestComplete().BindLambda([this, AudioPath](FHttpRequestPtr Req, FHttpResponsePtr Response, bool bWasSuccessful)
        {
            if (!bWasSuccessful || !Response.IsValid())
            {
                UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | ASR] Request failed."));

                AsyncTask(ENamedThreads::GameThread, [this]()
                    {
                        OnTranscriptionComplete.Broadcast(TEXT(""));
                    });

                return;
            }

            int32 Code = Response->GetResponseCode();
            if (Code != 200)
            {
                UE_LOG(LogTemp, Error, TEXT("[LocalAIForNPCs | ASR] HTTP %d: %s"), Code, *Response->GetContentAsString());

                AsyncTask(ENamedThreads::GameThread, [this]()
                    {
                        OnTranscriptionComplete.Broadcast(TEXT(""));
                    });

                return;
            }

            FString ResultText = Response->GetContentAsString();
            UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | ASR] Transcription completed."));
            UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | ASR] Result: %s"), *ResultText);

            FString SanitizedResult = SanitizeString(ResultText);

            AsyncTask(ENamedThreads::GameThread, [this, SanitizedResult]()
                {
                    OnTranscriptionComplete.Broadcast(SanitizedResult);
                });
        });

    Request->ProcessRequest();
    UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | ASR] Transcription request for file %s sent to %s"), *AudioPath, *Url);
}

TArray<uint8> UASRComponent::CreateMultiPartRequest(FString FilePath)
{
    TArray<uint8> Payload;
    FString Boundary = "----UEBoundary" + FGuid::NewGuid().ToString().Replace(TEXT("-"), TEXT(""));
    CurrentBoundary = Boundary;

    auto AppendLine = [&Payload](const FString& Line)
        {
            FString WithNewline = Line + TEXT("\r\n");
            FTCHARToUTF8 Convert(*WithNewline);
            Payload.Append((uint8*)Convert.Get(), Convert.Length());
        };

    AppendLine("--" + Boundary);
    AppendLine("Content-Disposition: form-data; name=\"file\"");
    AppendLine("");
    AppendLine(FilePath);

    AppendLine("--" + Boundary);
    AppendLine("Content-Disposition: form-data; name=\"temperature\"");
    AppendLine("");
    AppendLine("0.0");

    AppendLine("--" + Boundary);
    AppendLine("Content-Disposition: form-data; name=\"temperature_inc\"");
    AppendLine("");
    AppendLine("0.2");

    AppendLine("--" + Boundary);
    AppendLine("Content-Disposition: form-data; name=\"response_format\"");
    AppendLine("");
    AppendLine("text");

    AppendLine("--" + Boundary + "--");

    return Payload;
}

FString UASRComponent::SanitizeString(const FString& String)
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

    Result = RegexReplace(Result, TEXT("\\[[^\\]]*\\]"));

    Result = RegexReplace(Result, TEXT("\\*[^\\*]*\\*"));

    Result = Result.Replace(TEXT("\""), TEXT(""));

    Result = Result.Replace(TEXT("\n"), TEXT(" "));
    Result = Result.Replace(TEXT("\r"), TEXT(" "));

    Result = Result.TrimStartAndEnd();

    return Result;
}

bool UASRComponent::IsSpeechFrame(const float* Samples, int32 NumSamples, int32 SampleRate)
{
    switch (VadMode)
    {
    case EVadMode::Disabled:
        return true;

    case EVadMode::EnergyBased:
    {
        double SumSquares = 0.0;
        for (int32 i = 0; i < NumSamples; i++)
        {
            SumSquares += Samples[i] * Samples[i];
        }
        double Rms = FMath::Sqrt(SumSquares / FMath::Max(1, NumSamples));

        return (Rms >= EnergyThreshold);
    }

#if PLATFORM_WINDOWS && PLATFORM_64BITS
    case EVadMode::WebRTC:
    {
        FScopeLock Lock(&WebRtcMutex);

        if (WebRtcInstance == nullptr)
        {
            UE_LOG(LogTemp, Warning, TEXT("[LocalAIForNPCs | ASR | VAD] WebRTC instance not initialized!"));
            return false;
        }

        const int32 FrameSize = WebRtcSampleRate * WebRtcFrameDurationMs / 1000;

        if (SampleRate != WebRtcSampleRate)
        {
            Audio::VectorOps::FAlignedFloatBuffer InputBuffer(const_cast<float*>(Samples), NumSamples);

            Audio::FResamplingParameters Params = {
                Audio::EResamplingMethod::BestSinc,
                1,
                SampleRate,
                WebRtcSampleRate,
                InputBuffer
            };

            int32 OutBufferSize = Audio::GetOutputBufferSize(Params);
            WebRtcResampledBuffer.Reset();
            WebRtcResampledBuffer.AddZeroed(OutBufferSize);

            Audio::FResamplerResults Results;
            Results.OutBuffer = &WebRtcResampledBuffer;
            Audio::Resample(Params, Results);
        }
        else
        {
            WebRtcResampledBuffer.Reset();
            WebRtcResampledBuffer.Append(Samples, NumSamples);
        }

        for (float F : WebRtcResampledBuffer)
        {
            float Clamped = FMath::Clamp(F, -1.f, 1.f);
            WebRtcInputBuffer.Add(static_cast<int16>(Clamped * 32767.f));
        }

        if (WebRtcInputBuffer.Num() < FrameSize)
        {
            return false;
        }

        TArray<int16> Frame;
        Frame.Append(WebRtcInputBuffer.GetData(), FrameSize);
        WebRtcInputBuffer.RemoveAt(0, FrameSize, EAllowShrinking::No);

        int Result = fvad_process(WebRtcInstance, Frame.GetData(), FrameSize);

        if (Result == -1)
        {
            UE_LOG(LogTemp, Warning, TEXT("[LocalAIForNPCs | ASR | VAD] WebRTC VAD process failed!"));
            return false;
        }

        return (Result == 1);
    }
    case EVadMode::TEN:
    {
        FScopeLock Lock(&TenVadMutex);

        if (TenVadHandle == nullptr)
        {
            UE_LOG(LogTemp, Warning, TEXT("[LocalAIForNPCs | ASR | VAD] TEN VAD instance not initialized!"));
            return false;
        }

        if (SampleRate != TenVadSampleRate)
        {
            Audio::VectorOps::FAlignedFloatBuffer InputBuffer(const_cast<float*>(Samples), NumSamples);

            Audio::FResamplingParameters Params = {
                Audio::EResamplingMethod::BestSinc,
                1,
                SampleRate,
                TenVadSampleRate,
                InputBuffer
            };

            int32 OutBufferSize = Audio::GetOutputBufferSize(Params);
            TenVadResampledBuffer.Reset();
            TenVadResampledBuffer.AddZeroed(OutBufferSize);

            Audio::FResamplerResults Results;
            Results.OutBuffer = &TenVadResampledBuffer;
            Audio::Resample(Params, Results);
        }
        else
        {
            TenVadResampledBuffer.Reset();
            TenVadResampledBuffer.Append(Samples, NumSamples);
        }

        for (float F : TenVadResampledBuffer)
        {
            float Clamped = FMath::Clamp(F, -1.f, 1.f);
            TenVadInputBuffer.Add(static_cast<int16>(Clamped * 32767.f));
        }

        if (TenVadInputBuffer.Num() < TenVadHopSize)
        {
            return false;
        }

        TArray<int16> Frame;
        Frame.Append(TenVadInputBuffer.GetData(), TenVadHopSize);
        TenVadInputBuffer.RemoveAt(0, TenVadHopSize, EAllowShrinking::No);

        float Probability;
        int Result;

        if (ten_vad_process(TenVadHandle, Frame.GetData(), TenVadHopSize, &Probability, &Result) < 0)
        {
            UE_LOG(LogTemp, Warning, TEXT("[LocalAIForNPCs | ASR | VAD] TEN VAD process failed!"));
            return false;
        }

        return (Result == 1);
    }
#endif
    default:
        return true;
    }
}

void UASRComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Super::EndPlay(EndPlayReason);

    if (AudioCapture.IsStreamOpen())
    {
        AudioCapture.AbortStream();
        AudioCapture.CloseStream();
    }

    if (!RecordedAudioFolder.IsEmpty() && IFileManager::Get().DirectoryExists(*RecordedAudioFolder))
    {
        TArray<FString> FilesToDelete;
        IFileManager::Get().FindFiles(FilesToDelete, *RecordedAudioFolder);

        for (const FString& File : FilesToDelete)
        {
            FString FullPath = FPaths::Combine(RecordedAudioFolder, File);
            if (!IFileManager::Get().Delete(*FullPath, false, true))
            {
                UE_LOG(LogTemp, Warning, TEXT("[LocalAIForNPCs | ASR] Failed to delete file: %s"), *FullPath);
            }
        }
        UE_LOG(LogTemp, Log, TEXT("[LocalAIForNPCs | ASR] Cleaned up files in %s"), *RecordedAudioFolder);
    }

#if PLATFORM_WINDOWS && PLATFORM_64BITS
    if (WebRtcInstance)
    {
        fvad_free(WebRtcInstance);
        WebRtcInstance = nullptr;
    }
    if (TenVadHandle)
    {
        ten_vad_destroy(&TenVadHandle);
        TenVadHandle = nullptr;
    }
#endif
}