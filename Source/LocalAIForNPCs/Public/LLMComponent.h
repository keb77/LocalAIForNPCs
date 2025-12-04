#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LLMComponent.generated.h"

USTRUCT()
struct FChatMessage
{
    GENERATED_BODY()
    UPROPERTY()
    FString Role;
    UPROPERTY()
    FString Content;
};

UENUM(BlueprintType)
enum class ERagMode : uint8
{
    Disabled               UMETA(DisplayName = "Disabled"),
    Embedding              UMETA(DisplayName = "Embedding"),
    EmbeddingPlusReranker  UMETA(DisplayName = "Embedding + Reranker")
};

USTRUCT()
struct FKnowledgeEntry
{
    GENERATED_BODY()
    UPROPERTY()
    FString Text;
    UPROPERTY()
    TArray<float> Embedding;
};

USTRUCT(BlueprintType)
struct FNpcAction
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Description;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bHasTargetObject = false;
};

USTRUCT(BlueprintType)
struct FNpcObject
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Description;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    AActor* ActorRef;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnResponseReceived, const FString&, Response);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnStreamTokenReceived, const FString&, Token, bool, bDone);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnStreamChunkReceived, const FString&, Chunk, bool, bDone);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnActionReceived, const FString&, Action, AActor*, Object);

UCLASS(ClassGroup = (LocalAIForNPCs), meta = (BlueprintSpawnableComponent))
class LOCALAIFORNPCS_API ULLMComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    ULLMComponent();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs | LLM", meta = (ToolTip = "Port used to communicate with the local LLaMA.cpp text-generation server."))
    int32 Port = 8080;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs | LLM", meta = (ToolTip = "Optional system prompt sent to the model before any messages, defining base behavior and personality."))
    FString SystemMessage;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAIForNPCs | LLM", meta = (ToolTip = "If enabled, responses stream token-by-token. If disabled, responses are returned all at once."))
    bool bStream = false;

    UFUNCTION(BlueprintCallable, Category = "LocalAIForNPCs | LLM")
    void SendChatMessage(FString Message);

    UPROPERTY(BlueprintAssignable, Category = "LocalAIForNPCs | LLM", meta = (ToolTip = "Broadcast when a full response has been received from the model."))
    FOnResponseReceived OnResponseReceived;

    UPROPERTY(BlueprintAssignable, Category = "LocalAIForNPCs | LLM", meta = (ToolTip = "Broadcast whenever a new token is streamed from the model."))
    FOnStreamTokenReceived OnStreamTokenReceived;

    UPROPERTY(BlueprintAssignable, Category = "LocalAIForNPCs | LLM", meta = (ToolTip = "Broadcast whenever a partial chunk of streamed text is received."))
    FOnStreamChunkReceived OnStreamChunkReceived;

    UFUNCTION(BlueprintCallable, Category = "LocalAIForNPCs | LLM")
    void ClearChatHistory();

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

    UPROPERTY(BlueprintAssignable, Category = "LocalAIForNPCs | LLM | Actions", meta = (ToolTip = "Broadcast when the model outputs an action the NPC should perform."))
    FOnActionReceived OnActionReceived;

private:
    TArray<FChatMessage> ChatHistory;

    void SendRequest();
    void SendRequestStreaming();
    FString CreateJsonRequest();

    UFUNCTION()
    void HandleStreamChunk(const FString& PartialText, bool bDone);
    FString AccumulatedChunk;
    FCriticalSection ChunkMutex;

    FString SanitizeString(const FString& String);

    TArray<FKnowledgeEntry> Knowledge;
    TArray<float> EmbedText(const FString& Text);
    void GenerateKnowledge();
    float ComputeCosineSimilarity(const TArray<float>& A, const TArray<float>& B);
    TArray<FString> GetTopKDocuments(const TArray<float>& QueryEmbedding);
    TArray<FString> RerankDocuments(const FString& Query, const TArray<FString>& Documents);

    void HandleNpcAction(const FString& ActionCommand);
    FString BuildActionsSystemMessage();

protected:
    virtual void BeginPlay() override;
};