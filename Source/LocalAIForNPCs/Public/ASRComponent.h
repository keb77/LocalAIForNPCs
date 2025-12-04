#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AudioCaptureCore.h"
#include "fvad.h"
#include "ten_vad.h"
#include "ASRComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTranscriptionComplete, const FString&, Transcription);

UENUM(BlueprintType)
enum class EVadMode : uint8
{
    Disabled        UMETA(DisplayName = "Disabled"),
    EnergyBased     UMETA(DisplayName = "Energy-based"),
    WebRTC          UMETA(DisplayName = "WebRTC"),
    TEN             UMETA(DisplayName = "TEN")
};

UCLASS(ClassGroup = (LocalAIForNPCs), meta = (BlueprintSpawnableComponent))
class LOCALAIFORNPCS_API UASRComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UASRComponent();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs | ASR", meta = (ToolTip = "Port of the whisper.cpp server used for speech-to-text."))
    int32 Port = 8000;

    UFUNCTION(BlueprintCallable, Category = "LocalAIForNPCs | ASR", meta = (ToolTip = "Begin capturing microphone audio for transcription."))
    void StartRecording();

    UFUNCTION(BlueprintCallable, Category = "LocalAIForNPCs | ASR", meta = (ToolTip = "Stop audio capture and return the recorded file path."))
    FString StopRecording();

    UFUNCTION(BlueprintCallable, Category = "LocalAIForNPCs | ASR", meta = (ToolTip = "Transcribe the specified audio file using whisper.cpp."))
    void TranscribeAudio(const FString& AudioPath);

    UPROPERTY(BlueprintAssignable, Category = "LocalAIForNPCs | ASR", meta = (ToolTip = "Event fired when audio transcription is complete."))
    FOnTranscriptionComplete OnTranscriptionComplete;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs | ASR | VAD", meta = (ToolTip = "Voice Activity Detection mode for automatic speech segmentation."))
    EVadMode VadMode = EVadMode::Disabled;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs | ASR | VAD", meta = (EditCondition = "VadMode != EVadMode::Disabled", EditConditionHides, ClampMin = "0.1", ToolTip = "Duration of silence (in seconds) required to finalize a speech segment."))
    float SecondsOfSilenceBeforeSend = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs | ASR | VAD", meta = (EditCondition = "VadMode != EVadMode::Disabled", EditConditionHides, ClampMin = "0.1", ToolTip = "Minimum speech duration (in seconds) before audio is accepted for transcription."))
    float MinSpeechDuration = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs | ASR | VAD", meta = (EditCondition = "VadMode == EVadMode::EnergyBased", EditConditionHides, ClampMin = "0.0", ClampMax = "1.0", ToolTip = "Energy threshold for speech detection when using Energy-based VAD."))
    float EnergyThreshold = 0.1f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs | ASR | VAD", meta = (EditCondition = "VadMode == EVadMode::WebRTC", EditConditionHides, ClampMin = "0", ClampMax = "3", ToolTip = "Aggressiveness level for WebRTC VAD (0–3). Higher values filter more noise."))
    int32 WebRtcVadAggressiveness = 3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs | ASR | VAD", meta = (EditCondition = "VadMode == EVadMode::TEN", EditConditionHides, ClampMin = "0", ClampMax = "1", ToolTip = "Confidence threshold for speech detection when using TEN VAD."))
    float TenVadThreshold = 0.75f;

private:
    FString RecordedAudioFolder = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ASRAudio"));

    Audio::FAudioCapture AudioCapture;
    int32 DeviceSampleRate;
    int32 DeviceChannels;

    TArray<float> CapturedAudioData;
    FCriticalSection AudioDataLock;

    void SaveWavFile(const TArray<float>& InAudioData, FString OutputPath) const;

    TArray<uint8> CreateMultiPartRequest(FString FilePath);
    FString CurrentBoundary;

    FString SanitizeString(const FString& String);


    bool IsSpeechFrame(const float* Samples, int32 NumSamples, int32 SampleRate);
    int32 SilenceSamplesCount = 0;

    Fvad* WebRtcInstance = nullptr;
    TArray<int16> WebRtcInputBuffer;
    Audio::VectorOps::FAlignedFloatBuffer WebRtcResampledBuffer;
    const int32 WebRtcSampleRate = 16000;
    const int32 WebRtcFrameDurationMs = 20;
    FCriticalSection WebRtcMutex;

    ten_vad_handle_t TenVadHandle = nullptr;
    TArray<int16> TenVadInputBuffer;
    Audio::VectorOps::FAlignedFloatBuffer TenVadResampledBuffer;
    const int32 TenVadSampleRate = 16000;
    const size_t TenVadHopSize = 256;
    FCriticalSection TenVadMutex;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};