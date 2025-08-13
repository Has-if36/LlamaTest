// Copyright 2025-current Getnamo.

#pragma once

#include <string>
#include <vector>
#include "CoreMinimal.h"
#include "Logging/LogCategory.h"

#include "IPlatformFilePak.h"

DECLARE_LOG_CATEGORY_EXTERN(LlamaLog, Log, All);
static constexpr const TCHAR* LlamaLogStr = TEXT("LlamaLog");

class FLLamaModelUtils {
private:
	struct FPakListEntry
	{
		FPakListEntry()
			: ReadOrder(0)
			, PakFile(nullptr)
		{
		}

		uint32					ReadOrder;
		TRefCountPtr<FPakFile>	PakFile;

		FORCEINLINE bool operator < (const FPakListEntry& RHS) const
		{
			return ReadOrder > RHS.ReadOrder;
		}
	};

private:
    static FPakPlatformFile* InitPakPlatformFile();
    static void CleanUpPakPlatformFile(FPakPlatformFile* PakPlatformFile);
    static TArray<FString> FetchPakFileModelList();

#if PLATFORM_ANDROID
public:
	static TArray<FString> GetPathListAndroid()
	{
		return {
			FPaths::ProjectPersistentDownloadDir(),
			FPaths::ProjectSavedDir(),
			"/sdcard/Android/data",
		};
	}
#endif

public:
    static void MergeModelFromPak(const FString& writePathFile, TFunction<void(const FString&, bool)> PostMergeCallback = nullptr, EAsyncExecution CallbackAsyncExec = EAsyncExecution::TaskGraphMainThread);
    static void MergeModel(const FString& readPath, const FString& writePathFile, TFunction<void(const FString&, bool)> PostMergeCallback = nullptr, EAsyncExecution CallbackAsyncExec = EAsyncExecution::TaskGraphMainThread);
    static void PostMergeModel(bool bSuccess, FString errStr, const FString& writePathFile, TFunction<void(const FString&, bool)> PostMergeCallback = nullptr, EAsyncExecution CallbackAsyncExec = EAsyncExecution::TaskGraphMainThread);
};

class FLlamaPaths
{
public:
	static FString ModelsRelativeRootPath();
	static FString ParsePathIntoFullPath(const FString& InRelativeOrAbsolutePath);

	//Utility function for debugging model location and file enumeration
	static TArray<FString> DebugListDirectoryContent(const FString& InPath);
};

class FLlamaString
{
public:
	template <typename FmtType, typename... Types>
	static void LogPrint(ELogVerbosity::Type verbosity, const FmtType& Fmt, Types... Args) {
        FString messageContent = FString::Printf(Fmt, Args...);
        FString formattedString = FString::Printf(TEXT("%s: %s"), LlamaLogStr, *messageContent);
        if (verbosity == ELogVerbosity::Type::Error) {
            UE_LOG(LlamaLog, Error, TEXT("%s"), *formattedString);
#if UE_BUILD_DEVELOPMENT
            if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Red, formattedString);
#endif
        }
        else if (verbosity == ELogVerbosity::Type::Warning) {
            UE_LOG(LlamaLog, Warning, TEXT("%s"), *formattedString);
#if UE_BUILD_DEVELOPMENT
            if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Yellow, formattedString);
#endif
        }
        else if (verbosity == ELogVerbosity::Type::Verbose) {
            UE_LOG(LlamaLog, Log, TEXT("%s"), *formattedString);
#if UE_BUILD_DEVELOPMENT
            if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor(222, 222, 222), formattedString);
#endif
        }
        else {
            UE_LOG(LlamaLog, Log, TEXT("%s"), *formattedString);
#if UE_BUILD_DEVELOPMENT
            if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::White, formattedString);
#endif
        }
	}

	static FString ToUE(const std::string& String);
	static std::string ToStd(const FString& String);

	//Simple utility functions to find the last sentence
	static bool IsSentenceEndingPunctuation(const TCHAR Char);
	static FString GetLastSentence(const FString& InputString);

	static void AppendToCharVector(std::vector<char>& VectorHistory, const std::string& Text);
};