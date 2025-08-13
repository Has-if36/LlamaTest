// Copyright 2025-current Getnamo.

#include "LlamaSubsystem.h"
#include "HAL/PlatformTime.h"
#include "Tickable.h"
#include "LlamaNative.h"
#include "LlamaUtility.h"

#if !PLATFORM_ANDROID
#include "Embedding/VectorDatabase.h"
#endif

int32 ULlamaSubsystem::GetCoreProcessorCount()
{
    return FPlatformMisc::NumberOfCores();
}

int32 ULlamaSubsystem::GetLogicalProcessorCount()
{
    return FPlatformMisc::NumberOfCoresIncludingHyperthreads();
}

void ULlamaSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
    LlamaNative = new FLlamaNative();

    //Hookup native callbacks
    LlamaNative->OnModelStateChanged = [this](const FLLMModelState& UpdatedModelState)
    {
        ModelState = UpdatedModelState;
    };

    LlamaNative->OnTokenGenerated = [this](const FString& Token)
    {
        OnTokenGenerated.Broadcast(Token);
    };

    LlamaNative->OnPartialGenerated = [this](const FString& Partial)
    {
        OnPartialGenerated.Broadcast(Partial);
    };
    LlamaNative->OnPromptProcessed = [this](int32 TokensProcessed, EChatTemplateRole Role, float Speed)
    {
        OnPromptProcessed.Broadcast(TokensProcessed, Role, Speed);
    };
    LlamaNative->OnResponseGenerated = [this](const FString& Response)
    {
        OnResponseGenerated.Broadcast(Response);
        OnEndOfStream.Broadcast(true, ModelState.LastTokenGenerationSpeed);
    };
    LlamaNative->OnError = [this](const FString& ErrorMessage, int32 ErrorCode)
    {
        OnError.Broadcast(ErrorMessage, ErrorCode);
    };

    //All sentence ending formatting.
    ModelParams.Advanced.PartialsSeparators.Add(TEXT("."));
    ModelParams.Advanced.PartialsSeparators.Add(TEXT("?"));
    ModelParams.Advanced.PartialsSeparators.Add(TEXT("!"));
}

void ULlamaSubsystem::Deinitialize()
{
	if (LlamaNative)
	{
		delete LlamaNative;
		LlamaNative = nullptr;
	}

    Super::Deinitialize();
}

void ULlamaSubsystem::PrepareModel()
{
	const TCHAR* FunctionName = TEXT("ULlamaSubsystem::PrepareModel");
    UE_LOG(LogTemp, Log, TEXT("%s"), FunctionName);

    PrepareModel_Internal();
}

void ULlamaSubsystem::PrepareModel_Internal()
{
    const TCHAR* FunctionName = TEXT("ULlamaSubsystem::PrepareModel_Internal");
    UE_LOG(LogTemp, Log, TEXT("%s"), FunctionName);

    bool _bhasUnmergeModel = false;

#if defined(LLAMA_MODELS)
    // Check File
    FString modelString(ANSI_TO_TCHAR(LLAMA_MODELS));

    if (!modelString.IsEmpty()) {
        TArray<FString> models;
        modelString.ParseIntoArray(models, TEXT("|"), true);

        FString writePathAbs = FLlamaPaths::ModelsRelativeRootPath();

        UE_LOG(LogTemp, Log, TEXT("%s - Perform Merge"), FunctionName);
        models = models.FilterByPredicate([&, writePathAbs](const FString& model) {
            FString writeFileAbs = FPaths::Combine(writePathAbs, model);
            return !FPlatformFileManager::Get().GetPlatformFile().FileExists(*writeFileAbs);
            });

        {
            FScopeLock Lock(&ProcessEventMutex);

            totalUnmergedModel.Set(models.Num());
            mergedModelCounter.Set(0);

#if PLATFORM_ANDROID
            FPlatformMisc::MemoryBarrier();
#endif
        }

        _bhasUnmergeModel = models.Num() > 0;
        for (int i = 0; i < models.Num(); i++) {
            FString eachModel = models[i];
            FString writeFileAbs = FPaths::Combine(writePathAbs, eachModel);
            FLLamaModelUtils::MergeModelFromPak(writeFileAbs, [this](const FString& path, bool bSuccess) {
                //FLlamaString::LogPrint(ELogVerbosity::Type::Verbose, TEXT("ULlamaSubsystem::PostMergeModelFromPak"));

                int counter = 0;
                int total = 1;
                {
                    FScopeLock Lock(&ProcessEventMutex);

                    mergedModelCounter.Increment();

#if PLATFORM_ANDROID
                    FPlatformMisc::MemoryBarrier();
#endif

                    total = totalUnmergedModel.GetValue();
                    counter = mergedModelCounter.GetValue();
                }

                if (counter >= total) {
                    this->EventTrigger();
                }
                }, EAsyncExecution::TaskGraphMainThread);
        }
    }
#endif // LLAMA_MODELS

    if (_bhasUnmergeModel) this->EventReset();
    else this->EventTrigger();
}

void ULlamaSubsystem::EventTrigger() {
    //FLlamaString::LogPrint(ELogVerbosity::Type::Verbose, TEXT("ULlamaSubsystem::EventTrigger"));

    FScopeLock Lock(&ProcessEventMutex);

    ProcessingDoneEvent->Trigger();
	bIsProcessing = true;

#if PLATFORM_ANDROID
    FPlatformMisc::MemoryBarrier();
#endif
}

void ULlamaSubsystem::EventReset() {
    //FLlamaString::LogPrint(ELogVerbosity::Type::Verbose, TEXT("ULlamaSubsystem::EventReset"));

    FScopeLock Lock(&ProcessEventMutex);

    ProcessingDoneEvent->Reset();
    bIsProcessing = false;

#if PLATFORM_ANDROID
    FPlatformMisc::MemoryBarrier();
#endif
}

bool ULlamaSubsystem::WaitForCompletion(uint32 WaitTimeMs)
{
    //FLlamaString::LogPrint(ELogVerbosity::Type::Verbose, TEXT("ULlamaSubsystem::WaitForCompletion"));

    bool _bIsProcessing = false;

    {
#if PLATFORM_ANDROID
        FPlatformMisc::MemoryBarrier();
#endif
        FScopeLock Lock(&ProcessEventMutex);
        _bIsProcessing = bIsProcessing;
    }

    return _bIsProcessing || ProcessingDoneEvent->Wait(WaitTimeMs);
}

void ULlamaSubsystem::InsertTemplatedPrompt(const FString& Text, EChatTemplateRole Role, bool bAddAssistantBOS, bool bGenerateReply)
{
    FLlamaChatPrompt ChatPrompt;
    ChatPrompt.Prompt = Text;
    ChatPrompt.Role = Role;
    ChatPrompt.bAddAssistantBOS = bAddAssistantBOS;
    ChatPrompt.bGenerateReply = bGenerateReply;
    InsertTemplatedPromptStruct(ChatPrompt);
}

void ULlamaSubsystem::InsertTemplatedPromptStruct(const FLlamaChatPrompt& ChatPrompt)
{
    LlamaNative->InsertTemplatedPrompt(ChatPrompt);/*, [this, ChatPrompt](const FString& Response)
    {
        if (ChatPrompt.bGenerateReply)
        {
            OnResponseGenerated.Broadcast(Response);
            OnEndOfStream.Broadcast(true, ModelState.LastTokenGenerationSpeed);
        }
    });*/
}

void ULlamaSubsystem::InsertRawPrompt(const FString& Text, bool bGenerateReply)
{
    LlamaNative->InsertRawPrompt(Text, bGenerateReply);/*, [this, bGenerateReply](const FString& Response)
    {
        if (bGenerateReply)
        {
            OnResponseGenerated.Broadcast(Response);
            OnEndOfStream.Broadcast(true, ModelState.LastTokenGenerationSpeed);
        }
    })*/;
}

void ULlamaSubsystem::LoadModel(bool bForceReload)
{
    //Sync gt params
    LlamaNative->SetModelParams(ModelParams);

    //If ticker isn't active right now, start it. This will stay active until subsystem gets destroyed.
    if (!LlamaNative->IsNativeTickerActive())
    {
        LlamaNative->AddTicker();
    }

    LlamaNative->LoadModel(bForceReload, [this](const FString& ModelPath, int32 StatusCode)
    {
        //We errored, the emit will happen before we reach here so just exit
        if (StatusCode != 0)
        {
            return;
        }

        OnModelLoaded.Broadcast(ModelPath);
    });
}

void ULlamaSubsystem::UnloadModel()
{
    LlamaNative->UnloadModel([this](int32 StatusCode)
    {
        if (StatusCode != 0)
        {
            FString ErrorMessage = FString::Printf(TEXT("UnloadModel return error code: %d"), StatusCode);
            UE_LOG(LlamaLog, Warning, TEXT("%s"), *ErrorMessage);
            OnError.Broadcast(ErrorMessage, StatusCode);
        }
    });
}

bool ULlamaSubsystem::IsModelLoaded()
{
    return ModelState.bModelIsLoaded;
}

void ULlamaSubsystem::ResetContextHistory(bool bKeepSystemPrompt)
{
    LlamaNative->ResetContextHistory(bKeepSystemPrompt);
}

void ULlamaSubsystem::RemoveLastAssistantReply()
{
    LlamaNative->RemoveLastReply();
}

void ULlamaSubsystem::RemoveLastUserInput()
{
    LlamaNative->RemoveLastUserInput();
}

void ULlamaSubsystem::StopGeneration()
{
    LlamaNative->StopGeneration();
}

void ULlamaSubsystem::ResumeGeneration()
{
    LlamaNative->ResumeGeneration();
}

void ULlamaSubsystem::TestVectorSearch()
{
#if !PLATFORM_ANDROID
    FVectorDatabase* VectorDb = new FVectorDatabase();;

    UE_LOG(LogTemp, Log, TEXT("VectorDB Pre"));
    VectorDb->BasicsTest();
    UE_LOG(LogTemp, Log, TEXT("VectorDB Post"));

    delete VectorDb;
    VectorDb = nullptr;
#endif
}

FString ULlamaSubsystem::RawContextHistory()
{
    return ModelState.ContextHistory;
}

FStructuredChatHistory ULlamaSubsystem::GetStructuredChatHistory()
{
    return ModelState.ChatHistory;
}