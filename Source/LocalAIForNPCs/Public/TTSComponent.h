#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "TTSComponent.generated.h"

USTRUCT(BlueprintType)
struct FSoundWaveWithDuration
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite)
    USoundWave* SoundWave;

    UPROPERTY(BlueprintReadWrite)
    float Duration;
};

USTRUCT()
struct FNeuroSyncData
{
    GENERATED_BODY()
    TArray<uint8> AudioData;
    TArray<TArray<float>> BlendshapeFrames;
};

UENUM(BlueprintType)
enum class ELipSyncMode : uint8
{
    Disabled        UMETA(DisplayName = "Disabled"),
    NeuroSync       UMETA(DisplayName = "NeuroSync"),
    Audio2Face      UMETA(DisplayName = "Audio2Face")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSoundReady, const TArray<uint8>&, AudioData, FString, InputText);

UCLASS(ClassGroup = (LocalAIForNPCs), meta = (BlueprintSpawnableComponent))
class LOCALAIFORNPCS_API UTTSComponent : public USceneComponent
{
    GENERATED_BODY()

public:
    UTTSComponent();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs|TTS", meta = (ToolTip = "Port used to communicate with the Kokoro-FastAPI TTS server."))
    int32 Port = 8880;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs|TTS", meta = (ToolTip = "Name of the voice to use when generating speech."))
    FString Voice;

    UFUNCTION(BlueprintCallable, Category = "LocalAIForNPCs|TTS", meta = (ToolTip = "Generate a SoundWave from the given text."))
    void CreateSoundWave(const FString& Text);

    UPROPERTY(BlueprintAssignable, Category = "LocalAIForNPCs|TTS", meta = (ToolTip = "Event fired when a SoundWave is ready for playback."))
    FOnSoundReady OnSoundReady;

    UFUNCTION(BlueprintCallable, Category = "LocalAIForNPCs|TTS", meta = (ToolTip = "Play raw audio data as speech."))
    void PlaySpeech(const TArray<uint8>& AudioData);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs|TTS|LipSync", meta = (ToolTip = "Select which lip-sync system to use with generated speech."))
    ELipSyncMode LipSyncMode = ELipSyncMode::Disabled;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs|TTS|LipSync", meta = (EditCondition = "LipSyncMode == ELipSyncMode::NeuroSync", EditConditionHides, ToolTip = "Port used to communicate with the NeuroSync lip-sync server."))
    int32 NeuroSyncPort = 8881;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs|TTS|LipSync", meta = (EditCondition = "LipSyncMode == ELipSyncMode::NeuroSync", EditConditionHides, ToolTip = "Face subject name used by the NeuroSync server to identify the target face mesh. Set the LiveLink FaceSubject to the same name."))
    FString FaceSubjectName = TEXT("face1");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs|TTS|LipSync", meta = (EditCondition = "LipSyncMode == ELipSyncMode::Audio2Face", EditConditionHides, ToolTip = "Provider name used for NVIDIA Audio2Face lip-sync generation."))
    FString Audio2FaceProvider = TEXT("LocalA2F-Mark");

private:
    FString OutputAudioFolder = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TTSAudio"));

    FString CreateJsonRequest(FString Input) const;

    FSoundWaveWithDuration LoadSoundWaveFromWav(const TArray<uint8>& AudioData);

    TQueue<FSoundWaveWithDuration> SoundQueue;
    FCriticalSection SoundQueueLock;
    bool bIsPlayingSound = false;
    FTimerHandle AudioFinishTimer;
    UFUNCTION()
    void PlayNextInQueue();
    UFUNCTION()
    void AudioFinishedHandler();
    void PlaySoundWave(const TArray<uint8>& AudioData);

    TQueue<FNeuroSyncData> NeuroQueue;
    FCriticalSection NeuroQueueLock;
    bool bIsPlayingNeuro = false;
    FTimerHandle NeuroFinishTimer;
    void PlayNextNeuroInQueue();
    void NeuroFinishedHandler();
    void PlaySoundWithNeuroSync(const TArray<uint8>& AudioData);

    TQueue<TArray<uint8>> A2FQueue;
    FCriticalSection A2FQueueLock;
    bool bIsPlayingA2F = false;
    FTimerHandle A2FFinishTimer;
    void PlayNextA2FInQueue();
    void A2FFinishedHandler();
    void PlaySoundWithAudio2Face(const TArray<uint8>& AudioData);

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};