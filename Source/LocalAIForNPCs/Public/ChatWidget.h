#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ChatWidget.generated.h"

UCLASS()
class LOCALAIFORNPCS_API UChatWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintImplementableEvent, Category = "LocalAIForNPCs")
    void AddMessage(const FString& Name, const FString& MessageText);

    UFUNCTION(BlueprintImplementableEvent, Category = "LocalAIForNPCs")
    void SetHintText(const FString& HintText);

    UFUNCTION(BlueprintImplementableEvent, Category = "LocalAIForNPCs")
    void FocusChatInput();

    UFUNCTION(BlueprintCallable, Category = "LocalAIForNPCs")
    void SendMessage(const FString& MessageText);
};