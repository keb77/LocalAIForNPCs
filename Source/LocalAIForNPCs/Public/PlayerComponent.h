#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "NPCComponent.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "ChatWidget.h"
#include "PlayerComponent.generated.h"

UCLASS(ClassGroup = (LocalAIForNPCs), meta = (BlueprintSpawnableComponent))
class LOCALAIFORNPCS_API UPlayerComponent : public USceneComponent
{
    GENERATED_BODY()

public:
    UPlayerComponent();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs|General", meta = (ToolTip = "Display name for the player character."))
    FString Name = TEXT("You");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs|General", meta = (ClampMin = "0.0", ToolTip = "Maximum distance at which the player can interact with NPCs."))
    float InteractionRadius = 1000.f;

    UFUNCTION(BlueprintCallable, Category = "LocalAIForNPCs", meta = (ToolTip = "Begin recording player speech to send to NPCs."))
    void StartRecording();

    UFUNCTION(BlueprintCallable, Category = "LocalAIForNPCs", meta = (ToolTip = "Stop recording and automatically send the captured audio for transcription and processing."))
    void StopRecordingAndSendAudio();

    UFUNCTION(BlueprintCallable, Category = "LocalAIForNPCs", meta = (ToolTip = "Send a text message from the player to the NPC for processing by the LLM."))
    void SendText(const FString& Input);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs|UI", meta = (ToolTip = "Widget class used for displaying the player chat interface. Set this to W_ChatWidget."))
    TSubclassOf<UUserWidget> ChatWidgetClass;

    UPROPERTY(BlueprintReadOnly, Category = "LocalAIForNPCs|UI", meta = (ToolTip = "Instance of the player chat widget, created at runtime."))
    UChatWidget* ChatWidgetInstance;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs|Input", meta = (ToolTip = "Input mapping context used to bind player actions for LocalAI interactions. Set this to IMC_Player."))
    UInputMappingContext* PlayerMappingContext;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs|Input", meta = (ToolTip = "Input action for push-to-talk functionality. Set this to IA_PushToTalk."))
    UInputAction* PushToTalkAction;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs|Input", meta = (ToolTip = "Input action to focus or open the chat interface. Set this to IA_FocusChat."))
    UInputAction* FocusChatAction;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs|VAD", meta = (EditCondition = "VadMode != EVadMode::Disabled", EditConditionHides, ToolTip = "Port of the whisper.cpp server used for speech-to-text."))
    int32 ASRPort = 8000;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs|VAD", meta = (ToolTip = "Voice Activity Detection mode for automatic speech segmentation."))
    EVadMode VadMode = EVadMode::Disabled;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs|VAD", meta = (EditCondition = "VadMode != EVadMode::Disabled", EditConditionHides, ClampMin = "0.1", ToolTip = "Duration of silence (in seconds) required to finalize a speech segment."))
    float SecondsOfSilenceBeforeSend = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs|VAD", meta = (EditCondition = "VadMode != EVadMode::Disabled", EditConditionHides, ClampMin = "0.1", ToolTip = "Minimum speech duration (in seconds) before audio is accepted for transcription."))
    float MinSpeechDuration = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs|VAD", meta = (EditCondition = "VadMode == EVadMode::EnergyBased", EditConditionHides, ClampMin = "0.0", ClampMax = "1.0", ToolTip = "Energy threshold for speech detection when using Energy-based VAD."))
    float EnergyThreshold = 0.1f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs|VAD", meta = (EditCondition = "VadMode == EVadMode::WebRTC", EditConditionHides, ClampMin = "0", ClampMax = "3", ToolTip = "Aggressiveness level for WebRTC VAD (0–3). Higher values filter more noise."))
    int32 WebRtcVadAggressiveness = 3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs|VAD", meta = (EditCondition = "VadMode == EVadMode::TEN", EditConditionHides, ClampMin = "0", ClampMax = "1", ToolTip = "Confidence threshold for speech detection when using TEN VAD."))
    float TenVadThreshold = 0.75f;

private:
    UPROPERTY(VisibleAnywhere)
    USphereComponent* InteractionSphere;
    UFUNCTION()
    void OnBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
        bool bFromSweep, const FHitResult& SweepResult);
    UFUNCTION()
    void OnEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

    TArray<UNPCComponent*> NearbyNpcs;
    UFUNCTION()
    UNPCComponent* GetClosestNpc();

    UNPCComponent* CurrentRecordingNpc;
    bool bIsRecording = false;

    UNPCComponent* CurrentTypingNpc;
    bool bIsTyping = false;

    void SetupInput();
    void OnPushToTalkStarted(const FInputActionValue& Value);
    void OnPushToTalkReleased(const FInputActionValue& Value);
    void OnFocusChat(const FInputActionValue& Value);

    FTimerHandle UpdateHintTextTimerHandle;
    void UpdateHintText();
    FString LastHintText;

    UFUNCTION()
    bool IsUsersConversationTurn();

    UPROPERTY()
    UASRComponent* ASRComponent;
    UFUNCTION()
    void SendTextVad(const FString& Input);

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};