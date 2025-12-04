#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "ASRComponent.h"
#include "LLMComponent.h"
#include "TTSComponent.h"
#include "NPCComponent.generated.h"

class UPlayerComponent;

UCLASS(ClassGroup = (NpcAI), meta = (BlueprintSpawnableComponent))
class LOCALAIFORNPCS_API UNPCComponent : public USceneComponent
{
    GENERATED_BODY()

public:
    UNPCComponent();

    UPROPERTY(BlueprintReadOnly, Category = "LocalAIForNPCs | Components", meta = (ToolTip = "Reference to the NPC's ASR component used for speech recognition."))
    UASRComponent* ASRComponent;

    UPROPERTY(BlueprintReadOnly, Category = "LocalAIForNPCs | Components", meta = (ToolTip = "Reference to the NPC's LLM component used for text generation and RAG."))
    ULLMComponent* LLMComponent;

    UPROPERTY(BlueprintReadOnly, Category = "LocalAIForNPCs | Components", meta = (ToolTip = "Reference to the NPC's TTS component used for speech synthesis and lip-sync."))
    UTTSComponent* TTSComponent;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs | General", meta = (ToolTip = "Display name of the NPC."))
    FString Name = TEXT("NPC");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs | ASR", meta = (ToolTip = "Port of the whisper.cpp server used for speech-to-text."))
    int32 ASRPort = 8000;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs | LLM", meta = (ToolTip = "Port used to communicate with the local LLaMA.cpp text-generation server."))
    int32 LLMPort = 8080;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs | LLM", meta = (ToolTip = "Optional system prompt sent to the model before any messages, defining base behavior and personality."))
    FString SystemMessage;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs | LLM", meta = (ToolTip = "If enabled, responses stream token-by-token. If disabled, responses are returned all at once."))
    bool bStream = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs | LLM | RAG", meta = (ToolTip = "Retrieval mode. Controls whether external knowledge is used and how it is retrieved."))
    ERagMode RagMode = ERagMode::Disabled;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs | LLM | RAG", meta = (EditCondition = "RagMode != ERagMode::Disabled", EditConditionHides, ToolTip = "Port for the LLaMA.cpp embedding server used for vectorizing documents."))
    int32 EmbeddingPort = 8081;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs | LLM | RAG", meta = (EditCondition = "RagMode == ERagMode::EmbeddingPlusReranker", EditConditionHides, ToolTip = "Port for the reranker server used in advanced RAG pipelines."))
    int32 RerankerPort = 8082;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs | LLM | RAG", meta = (EditCondition = "RagMode != ERagMode::Disabled", EditConditionHides, ToolTip = "Path to the knowledge directory or file(s) used for retrieval."))
    FString KnowledgePath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs | LLM | RAG", meta = (EditCondition = "RagMode != ERagMode::Disabled", EditConditionHides, ClampMin = "1", ToolTip = "Number of top embedding results to retrieve before reranking or passing to the model."))
    int32 EmbeddingTopK = 5;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs | LLM | RAG", meta = (EditCondition = "RagMode == ERagMode::EmbeddingPlusReranker", EditConditionHides, ClampMin = "1", ToolTip = "Number of candidates kept after reranking."))
    int32 RerankingTopN = 3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs | LLM | RAG", meta = (EditCondition = "RagMode != ERagMode::Disabled", EditConditionHides, ClampMin = "1", ToolTip = "How many sentences to include in each knowledge chunk before embedding."))
    int32 SentencesPerChunk = 2;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs | LLM | RAG", meta = (EditCondition = "RagMode != ERagMode::Disabled", EditConditionHides, ClampMin = "0", ToolTip = "Overlap between chunks, in number of sentences. Prevents context fragmentation."))
    int32 SentenceOverlap = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs | LLM | RAG", meta = (EditCondition = "RagMode != ERagMode::Disabled", EditConditionHides, ClampMin = "-1", ClampMax = "1", ToolTip = "Minimum cosine similarity a retrieved chunk must meet to be considered relevant."))
    float SimilarityThreshold = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs | LLM | Actions", meta = (ToolTip = "List of actions the NPC is allowed to perform. Used for function-calling-style model outputs."))
    TArray<FNpcAction> KnownActions;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs | LLM | Actions", meta = (ToolTip = "List of objects the NPC can reference or interact with, usable by the model during reasoning."))
    TArray<FNpcObject> KnownObjects;

    UPROPERTY(BlueprintAssignable, Category = "LocalAiNpc | Actions")
    FOnActionReceived OnActionReceived;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs | TTS", meta = (ToolTip = "Port used to communicate with the Kokoro-FastAPI TTS server."))
    int32 TTSPort = 8880;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs | TTS", meta = (ToolTip = "Name of the voice to use when generating speech."))
    FString Voice;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs | TTS | LipSync", meta = (ToolTip = "Select which lip-sync system to use with generated speech."))
    ELipSyncMode LipSyncMode = ELipSyncMode::Disabled;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs | TTS | LipSync", meta = (EditCondition = "LipSyncMode == ELipSyncMode::NeuroSync", EditConditionHides, ToolTip = "Port used to communicate with the NeuroSync lip-sync server."))
    int32 NeuroSyncPort = 8881;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs | TTS | LipSync", meta = (EditCondition = "LipSyncMode == ELipSyncMode::NeuroSync", EditConditionHides, ToolTip = "Face subject name used by the NeuroSync server to identify the target face mesh. Set the LiveLink FaceSubject to the same name."))
    FString FaceSubjectName = TEXT("face1");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs | TTS | LipSync", meta = (EditCondition = "LipSyncMode == ELipSyncMode::Audio2Face", EditConditionHides, ToolTip = "Provider name used for NVIDIA Audio2Face lip-sync generation."))
    FString Audio2FaceProvider = TEXT("LocalA2F-Mark");

    UFUNCTION(BlueprintCallable, Category = "LocalAIForNPCs", meta = (ToolTip = "Start recording player speech to send to the NPC."))
    void StartRecording();

    UFUNCTION(BlueprintCallable, Category = "LocalAIForNPCs", meta = (ToolTip = "Stop recording and automatically send the captured audio for transcription and processing."))
    void StopRecordingAndSendAudio();

    UFUNCTION(BlueprintCallable, Category = "LocalAIForNPCs", meta = (ToolTip = "Send a text message from the player to the NPC for processing by the LLM."))
    void SendText(FString Input);

    UPROPERTY()
    bool bIsUsersConversationTurn = true;

private:
    UFUNCTION()
    void HandleTranscriptionComplete(const FString& Transcription);
    UFUNCTION()
    void HandleResponseReceived(const FString& Response);
    UFUNCTION()
    void HandleChunkReceived(const FString& Chunk, bool bDone);
    UFUNCTION()
    void HandleActionReceived(const FString& Action, AActor* Object);
    UFUNCTION()
    void HandleSoundReady(const TArray<uint8>& AudioData, FString InputText);

    bool bIsRecording = false;

    bool bIsFirstChunk = true;

    UPlayerComponent* PlayerComponent;

protected:
    virtual void BeginPlay() override;
};