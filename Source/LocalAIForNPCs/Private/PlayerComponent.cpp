#include "PlayerComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedInputComponent.h"

UPlayerComponent::UPlayerComponent()
{
    PrimaryComponentTick.bCanEverTick = false;

    InteractionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionSphere"));
    InteractionSphere->SetupAttachment(this);
    InteractionSphere->InitSphereRadius(InteractionRadius);
    InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    InteractionSphere->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Overlap);
    InteractionSphere->SetGenerateOverlapEvents(true);
}

void UPlayerComponent::BeginPlay()
{
    Super::BeginPlay();

    SetupInput();

    if (InteractionSphere)
    {
        InteractionSphere->SetSphereRadius(InteractionRadius);

        InteractionSphere->OnComponentBeginOverlap.AddDynamic(this, &UPlayerComponent::OnBeginOverlap);
        InteractionSphere->OnComponentEndOverlap.AddDynamic(this, &UPlayerComponent::OnEndOverlap);

        TArray<AActor*> OverlappingActors;
        InteractionSphere->GetOverlappingActors(OverlappingActors);

        for (AActor* Actor : OverlappingActors)
        {
            if (UNPCComponent* NpcComp = Actor->FindComponentByClass<UNPCComponent>())
            {
                NearbyNpcs.AddUnique(NpcComp);
                UE_LOG(LogTemp, Verbose, TEXT("[LocalAINpc | PlayerComponent] NPC already in range at start: %s"), *NpcComp->Name);
            }
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | PlayerComponent] InteractionSphere is not initialized in PlayerComponent."));
    }

    if (ChatWidgetClass)
    {
        ChatWidgetInstance = CreateWidget<UChatWidget>(GetWorld(), ChatWidgetClass);
        if (ChatWidgetInstance)
        {
            ChatWidgetInstance->AddToViewport();

            GetWorld()->GetTimerManager().SetTimer(UpdateHintTextTimerHandle, this, &UPlayerComponent::UpdateHintText, 0.25f, true, 0.0f);
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | PlayerComponent] Failed to create ChatWidgetInstance in PlayerComponent."));
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | PlayerComponent] ChatWidgetClass is not set in PlayerComponent."));
    }

    if (VadMode != EVadMode::Disabled)
    {
        ASRComponent = NewObject<UASRComponent>(this, UASRComponent::StaticClass(), TEXT("ASRComponent"));
        if (ASRComponent)
        {
            ASRComponent->Port = ASRPort;
            ASRComponent->VadMode = VadMode;
            ASRComponent->SecondsOfSilenceBeforeSend = SecondsOfSilenceBeforeSend;
            ASRComponent->MinSpeechDuration = MinSpeechDuration;
            ASRComponent->EnergyThreshold = EnergyThreshold;
            ASRComponent->WebRtcVadAggressiveness = WebRtcVadAggressiveness;

            ASRComponent->RegisterComponent();

            ASRComponent->OnTranscriptionComplete.AddDynamic(this, &UPlayerComponent::SendTextVad);
        }
    }
}

void UPlayerComponent::OnBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
    bool bFromSweep, const FHitResult& SweepResult)
{
    if (!OtherActor || OtherActor == GetOwner())
    {
        return;
    }

    if (UNPCComponent* NpcComp = OtherActor->FindComponentByClass<UNPCComponent>())
    {
        NearbyNpcs.AddUnique(NpcComp);
        UE_LOG(LogTemp, Verbose, TEXT("[LocalAINpc | PlayerComponent] NPC entered interaction range: %s"), *NpcComp->Name);
    }
}

void UPlayerComponent::OnEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
    if (!OtherActor || OtherActor == GetOwner())
    {
        return;
    }

    if (UNPCComponent* NpcComp = OtherActor->FindComponentByClass<UNPCComponent>())
    {
        if (NearbyNpcs.Remove(NpcComp) > 0)
        {
            UE_LOG(LogTemp, Verbose, TEXT("[LocalAINpc | PlayerComponent] NPC left interaction range: %s"), *NpcComp->Name);
        }
        else
        {
            UE_LOG(LogTemp, Verbose, TEXT("[LocalAINpc | PlayerComponent] NPC not found in nearby list: %s"), *NpcComp->Name);
        }
    }
}

UNPCComponent* UPlayerComponent::GetClosestNpc()
{
    UNPCComponent* ClosestNpc = nullptr;
    float ClosestDistanceSq = TNumericLimits<float>::Max();

    if (!GetOwner())
    {
        return nullptr;
    }

    const FVector PlayerLocation = GetOwner()->GetActorLocation();

    for (UNPCComponent* NpcComp : NearbyNpcs)
    {
        if (!IsValid(NpcComp) || !IsValid(NpcComp->GetOwner()))
        {
            continue;
        }

        const float DistSq = FVector::DistSquared(PlayerLocation, NpcComp->GetOwner()->GetActorLocation());
        if (DistSq < ClosestDistanceSq)
        {
            ClosestDistanceSq = DistSq;
            ClosestNpc = NpcComp;
        }
    }

    return ClosestNpc;
}

void UPlayerComponent::StartRecording()
{
    if (bIsRecording)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | PlayerComponent] Already recording."));
        return;
    }

    CurrentRecordingNpc = GetClosestNpc();

    if (!CurrentRecordingNpc)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | PlayerComponent] No NPCs in range to start recording."));
        return;
    }

    if (!IsUsersConversationTurn())
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | PlayerComponent] Not the user's turn to speak."));
        return;
    }

    if (ChatWidgetInstance)
    {
        FString Hint = FString::Printf(TEXT("Talking to %s..."), *CurrentRecordingNpc->Name);
        ChatWidgetInstance->SetHintText(Hint);
        LastHintText = Hint;
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | PlayerComponent] ChatWidgetInstance is not initialized. Cannot update hint text."));
    }

    bIsRecording = true;

    CurrentRecordingNpc->StartRecording();
}

void UPlayerComponent::StopRecordingAndSendAudio()
{
    if (!bIsRecording)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | PlayerComponent] Not currently recording."));

        bIsRecording = false;
        CurrentRecordingNpc = nullptr;
        return;
    }

    if (!CurrentRecordingNpc)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | PlayerComponent] No NPCs currently recording."));

        bIsRecording = false;
        CurrentRecordingNpc = nullptr;
        return;
    }

    bIsRecording = false;

    CurrentRecordingNpc->StopRecordingAndSendAudio();

    CurrentRecordingNpc = nullptr;
}

void UPlayerComponent::SendText(const FString& Input)
{
    if (!bIsTyping)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | PlayerComponent] Not currently typing."));

        bIsTyping = false;
        CurrentTypingNpc = nullptr;
        return;
    }

    if (!CurrentTypingNpc)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | PlayerComponent] Not typing to any NPC."));

        bIsTyping = false;
        CurrentTypingNpc = nullptr;
        return;
    }

    if (Input.IsEmpty())
    {
        UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | PlayerComponent] Empty input text. Skipping send."));

        bIsTyping = false;
        CurrentTypingNpc = nullptr;
        return;
    }

    if (ChatWidgetInstance)
    {
        ChatWidgetInstance->AddMessage(Name, Input);
    }

    bIsTyping = false;

    CurrentTypingNpc->SendText(Input);

    CurrentTypingNpc = nullptr;
}

void UPlayerComponent::SendTextVad(const FString& Input)
{
    CurrentRecordingNpc = GetClosestNpc();

    if (!CurrentRecordingNpc)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | PlayerComponent] No NPC in range to send request."));

        CurrentRecordingNpc = nullptr;
        return;
    }

    if (Input.IsEmpty())
    {
        UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | PlayerComponent] Empty input text. Skipping send."));

        CurrentRecordingNpc = nullptr;
        return;
    }

    if (ChatWidgetInstance)
    {
        ChatWidgetInstance->AddMessage(Name, Input);
    }

    CurrentRecordingNpc->SendText(Input);

    CurrentRecordingNpc = nullptr;
}

void UPlayerComponent::SetupInput()
{
    if (!GetOwner())
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | PlayerComponent] Owner is not valid in PlayerComponent."));
        return;
    }

    if (APlayerController* PC = Cast<APlayerController>(GetOwner()->GetInstigatorController()))
    {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
            ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
        {
            if (PlayerMappingContext)
            {
                Subsystem->AddMappingContext(PlayerMappingContext, 0);
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | PlayerComponent] PlayerMappingContext is not set in PlayerComponent."));
            }
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | PlayerComponent] EnhancedInputLocalPlayerSubsystem not found for PlayerController."));
        }

        if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PC->InputComponent))
        {
            if (PushToTalkAction)
            {
                EIC->BindAction(PushToTalkAction, ETriggerEvent::Started, this, &UPlayerComponent::OnPushToTalkStarted);
                EIC->BindAction(PushToTalkAction, ETriggerEvent::Completed, this, &UPlayerComponent::OnPushToTalkReleased);
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | PlayerComponent] PushToTalkAction is not set in PlayerComponent."));
            }

            if (FocusChatAction)
            {
                EIC->BindAction(FocusChatAction, ETriggerEvent::Started, this, &UPlayerComponent::OnFocusChat);
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | PlayerComponent] FocusChatAction is not set in PlayerComponent."));
            }
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | PlayerComponent] EnhancedInputComponent not found for PlayerController."));
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | PlayerComponent] PlayerController is not valid in PlayerComponent."));
    }
}

void UPlayerComponent::OnPushToTalkStarted(const FInputActionValue& Value)
{
    if (VadMode == EVadMode::Disabled)
    {
        StartRecording();
    }
}

void UPlayerComponent::OnPushToTalkReleased(const FInputActionValue& Value)
{
    if (VadMode == EVadMode::Disabled)
    {
        StopRecordingAndSendAudio();
    }
}

void UPlayerComponent::OnFocusChat(const FInputActionValue& Value)
{
    if (ChatWidgetInstance)
    {
        CurrentTypingNpc = GetClosestNpc();

        if (!CurrentTypingNpc)
        {
            UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | PlayerComponent] No NPCs in range to type to."));
            return;
        }

        if (!IsUsersConversationTurn())
        {
            UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | PlayerComponent] Not the user's turn to type."));
            return;
        }

        ChatWidgetInstance->FocusChatInput();
        bIsTyping = true;

        FString Hint = FString::Printf(TEXT("Talking to %s..."), *CurrentTypingNpc->Name);
        ChatWidgetInstance->SetHintText(Hint);
        LastHintText = Hint;
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | PlayerComponent] ChatWidgetInstance is not initialized. Cannot focus chat input."));
    }
}

void UPlayerComponent::UpdateHintText()
{
    if (!ChatWidgetInstance)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | PlayerComponent] ChatWidgetInstance is not initialized. Cannot update hint text."));
        return;
    }

    if (!bIsTyping && !bIsRecording)
    {
        UNPCComponent* ClosestNpc = GetClosestNpc();
        if (ClosestNpc)
        {
            FString Hint;
            if (IsUsersConversationTurn())
            {
                if (VadMode == EVadMode::Disabled)
                {
                    Hint = FString::Printf(TEXT("Hold V to talk or press Enter to type to %s"), *ClosestNpc->Name);
                }
                else
                {
                    Hint = FString::Printf(TEXT("Talk or press Enter to type to %s"), *ClosestNpc->Name);
                }
            }
            else
            {
                Hint = FString::Printf(TEXT("Talking to %s..."), *ClosestNpc->Name);
            }

            if (Hint != LastHintText)
            {
                ChatWidgetInstance->SetHintText(Hint);
                LastHintText = Hint;
            }
        }
        else
        {
            FString Hint = TEXT("No NPC in range to talk to");
            if (Hint != LastHintText)
            {
                ChatWidgetInstance->SetHintText(Hint);
                LastHintText = Hint;
            }
        }
    }
}

bool UPlayerComponent::IsUsersConversationTurn()
{
    for (UNPCComponent* NpcComp : NearbyNpcs)
    {
        if (!IsValid(NpcComp) || !IsValid(NpcComp->GetOwner()))
        {
            continue;
        }

        if (!NpcComp->bIsUsersConversationTurn)
        {
            return false;

            UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | PlayerComponent] %s"), *NpcComp->Name);
        }
    }

    return true;
}

void UPlayerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(UpdateHintTextTimerHandle);
        GetWorld()->GetTimerManager().ClearAllTimersForObject(this);
    }

    if (InteractionSphere)
    {
        InteractionSphere->OnComponentBeginOverlap.RemoveDynamic(this, &UPlayerComponent::OnBeginOverlap);
        InteractionSphere->OnComponentEndOverlap.RemoveDynamic(this, &UPlayerComponent::OnEndOverlap);
    }

    if (ChatWidgetInstance && ChatWidgetInstance->IsInViewport())
    {
        ChatWidgetInstance->RemoveFromParent();
    }
    ChatWidgetInstance = nullptr;

    if (ASRComponent)
    {
        ASRComponent->OnTranscriptionComplete.RemoveDynamic(this, &UPlayerComponent::SendTextVad);

        if (ASRComponent->IsRegistered())
        {
            ASRComponent->UnregisterComponent();
        }
        ASRComponent->DestroyComponent();
        ASRComponent = nullptr;
    }

    if (GetOwner())
    {
        if (APlayerController* PC = Cast<APlayerController>(GetOwner()->GetInstigatorController()))
        {
            if (ULocalPlayer* LP = PC->GetLocalPlayer())
            {
                if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
                    ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LP))
                {
                    if (PlayerMappingContext)
                    {
                        Subsystem->RemoveMappingContext(PlayerMappingContext);
                    }
                }
            }
        }
    }

    Super::EndPlay(EndPlayReason);
}
