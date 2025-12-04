#include "ChatWidget.h"
#include "PlayerComponent.h"

void UChatWidget::SendMessage(const FString& MessageText)
{
    APawn* PlayerPawn = GetWorld()->GetFirstPlayerController()->GetPawn();
    if (PlayerPawn)
    {
        UPlayerComponent* PlayerComponent = PlayerPawn->FindComponentByClass<UPlayerComponent>();
        if (PlayerComponent)
        {
            PlayerComponent->SendText(MessageText);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | ChatWidget] PlayerComponent not found. Add PlayerComponent to Player Pawn to send chat messages."));
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | ChatWidget] No player pawn found to send message."));
    }
}