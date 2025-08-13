// Copyright 2025-current Getnamo.

#include "LlamaUtility.h"
#include "Misc/Paths.h"
#include "IPlatformFilePak.h"

DEFINE_LOG_CATEGORY(LlamaLog);

FPakPlatformFile* FLLamaModelUtils::InitPakPlatformFile()
{
    FPakPlatformFile* PakPlatformFile = StaticCast<FPakPlatformFile*>(FPlatformFileManager::Get().FindPlatformFile(TEXT("PakFile")));
    if (!PakPlatformFile)
    {
        // Initialize if needed
        IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
        PakPlatformFile = new FPakPlatformFile();
        PakPlatformFile->Initialize(&PlatformFile, TEXT(""));
        FPlatformFileManager::Get().SetPlatformFile(*PakPlatformFile);
    }

    return PakPlatformFile;
}

void FLLamaModelUtils::CleanUpPakPlatformFile(FPakPlatformFile* PakPlatformFile)
{
	TArray<FString> MountedPakFilenames;
    PakPlatformFile->GetMountedPakFilenames(MountedPakFilenames);

    for (auto eachMounted : MountedPakFilenames) {
        PakPlatformFile->Unmount(*eachMounted);
    }

    IPlatformFile* InnerPlatformFile = PakPlatformFile->GetLowerLevel();
    FPlatformFileManager::Get().SetPlatformFile(*InnerPlatformFile);

    delete PakPlatformFile;
    PakPlatformFile = nullptr;
}

//FLlamaModelUtils
TArray<FString> FLLamaModelUtils::FetchPakFileModelList()
{
    TArray<FString> FileList;

#if defined(LLAMA_PAK_MIN_INDEX) && defined(LLAMA_PAK_MAX_INDEX)
    FLlamaString::LogPrint(ELogVerbosity::Type::Verbose, TEXT("FLLamaModelUtils::FetchPakFileModelList"));

    const uint8 MIN_PAK_INDEX = 11, MAX_PAK_INDEX = 20;
    FString SearchFile = FString();
    FString pakPath;
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile(); // Required here because of android DIR check

#if PLATFORM_ANDROID
    // FAndroidMisc::GamePersistentDownloadDir() - /storage/emulated/0/Android/data/[PackageName]/files
    //pakPath = FPaths::Combine(FPaths::Combine(FString(FAndroidMisc::GamePersistentDownloadDir()), "pak"));

    for (FString eachPath : FLLamaModelUtils::GetPathListAndroid()) {
        if (PlatformFile.DirectoryExists(*eachPath)) {
            pakPath = FPaths::Combine(FPaths::Combine(FString(eachPath), "pak"));
            break;
        }
    }
#else
    pakPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), "Models/pak")); // For External (Saved)
#endif

    
    if (!PlatformFile.DirectoryExists(*pakPath)) {
        FLlamaString::LogPrint(ELogVerbosity::Type::Verbose, TEXT("FLLamaModelUtils::FetchPakFileModelList - Unable to Find Directory"));
        return TArray<FString>();
    }

    PlatformFile.FindFiles(FileList, *pakPath, TEXT(".pak"));

#endif // LLAMA_PAK_MIN_INDEX && LLAMA_PAK_MAX_INDEX

    return FileList;
}

void FLLamaModelUtils::MergeModelFromPak(const FString& writePathFile, TFunction<void(const FString&, bool)> PostMergeCallback, EAsyncExecution CallbackAsyncExec)
{
    const TCHAR* FunctionName = TEXT("FLlamaPaths::MergeModelFromPak");
    FLlamaString::LogPrint(ELogVerbosity::Type::Verbose, TEXT("%s"), FunctionName);

    if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*writePathFile)) {
        FString errStr = FString::Printf(TEXT("%s - Failed: File already exist"), FunctionName);
        PostMergeModel(false, errStr, writePathFile, PostMergeCallback, CallbackAsyncExec);
        return;
    }

    TArray<FString> FileList = FetchPakFileModelList();

    if (FileList.Num() == 0) {
		FString errStr = FString::Printf(TEXT("%s - Failed: No Pak file found"), FunctionName);
        PostMergeModel(false, errStr, writePathFile, PostMergeCallback, CallbackAsyncExec);
        return;
    }

    Async(EAsyncExecution::Thread, [FunctionName, writePathFile, PostMergeCallback, CallbackAsyncExec, FileList]()
        {
            bool bSuccess = false;
            FString errStr = FString();

			TMap<FString, FString> MappedFileInPak;
            FPakPlatformFile* PakPlatformFile = FLLamaModelUtils::InitPakPlatformFile();
            for (FString eachPak : FileList) {
                PakPlatformFile->Mount(*eachPak, 0);

                TArray<FString> outFileList;
                PakPlatformFile->GetPrunedFilenamesInPakFile(eachPak, outFileList);
                for (FString eachFile : outFileList) {
                    MappedFileInPak.Add(eachFile, eachPak);
                }
            }
            
            FString writeDir = FPaths::GetPath(writePathFile);
            PakPlatformFile->CreateDirectoryTree(*writeDir);

            TUniquePtr<IFileHandle> OutputFile(PakPlatformFile->OpenWrite(*writePathFile));
            if (!OutputFile)
            {
                errStr = FString::Printf(TEXT("Failed to create output file"));
            }

            // Load all chunks from APK
            int32 PartIndex = 1;
            FString filename = FPaths::GetCleanFilename(writePathFile);
            FString baseFilename = FPaths::GetBaseFilename(writePathFile);
            while (errStr.IsEmpty()) // Infinite Loop until no more parts are found
            {
                // Phi-3-mini-4k-instruct-q4.gguf.part1
                //FString PartPath = FPaths::Combine(ContentDir, FString::Printf(TEXT("%s-part%d"), *baseFilename, PartIndex));
                FString filenamePart = FString::Printf(TEXT("%s.part%d"), *filename, PartIndex);

                FString fullVirtualPathFile; 
                FString pakPath;
                for (auto& eachPair : MappedFileInPak) {
                    if (eachPair.Key.Contains(filenamePart)) {
                        fullVirtualPathFile = eachPair.Key;
                        pakPath = eachPair.Value;
                        break;
                    }
                }

                // PakPlatformFile->GetFilenameOnDisk(*filenamePart);

                if (fullVirtualPathFile.IsEmpty() || pakPath.IsEmpty()) {
                    if (PartIndex > 1) {
                        FString filenamePart2 = FString::Printf(TEXT("%s-part%d"), *baseFilename, PartIndex - 1);
                        FLlamaString::LogPrint(ELogVerbosity::Type::Verbose, TEXT("%s Finish Reading at %s"), FunctionName, *filenamePart2);
                    }
                    else {
                        errStr = FString::Printf(TEXT("Failed to find part file - %s"), *filenamePart);
                    }
                    break;
                }

                FLlamaString::LogPrint(ELogVerbosity::Type::Verbose, TEXT("%s - Merging %s"), FunctionName, *filenamePart);
                FLlamaString::LogPrint(ELogVerbosity::Type::Verbose, TEXT("%s - Located %s"), FunctionName, *fullVirtualPathFile);

                IFileHandle* FileHandle = PakPlatformFile->OpenRead(*fullVirtualPathFile);
                if (!FileHandle)
                {
                    errStr = FString::Printf(TEXT("Failed to read the part file - %s"), *filenamePart);
                    break;
                }

                const int64 FileSize = FileHandle->Size();
                TArray<uint8> PartFileData;
                PartFileData.SetNumUninitialized(FileSize);
                
                FileHandle->Read(PartFileData.GetData(), FileSize);

                bSuccess = OutputFile->Write(PartFileData.GetData(), FileSize);
                if (!bSuccess) {
                    errStr = FString::Printf(TEXT("Failed to write the part file - %s"), *filenamePart);
                }

                PartIndex++;
            }


            FLLamaModelUtils::CleanUpPakPlatformFile(PakPlatformFile);

            // Notify main thread
            PostMergeModel(bSuccess, errStr, writePathFile, PostMergeCallback, CallbackAsyncExec);
        });
}

void FLLamaModelUtils::MergeModel(const FString& readPath, const FString& writePathFile, TFunction<void(const FString&, bool)> PostMergeCallback, EAsyncExecution CallbackAsyncExec)
{
    const TCHAR* functionName = TEXT("FLlamaPaths::MergeModel");
    FLlamaString::LogPrint(ELogVerbosity::Type::Verbose, TEXT("%s"), functionName);

    Async(EAsyncExecution::Thread, [functionName, readPath, writePathFile, PostMergeCallback, CallbackAsyncExec]()
        {
            bool bSuccess = false;
            FString errStr = FString();
            if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*writePathFile)) {
                errStr = FString::Printf(TEXT("File already exist"));
            }

            IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

            FString writeDir = FPaths::GetPath(writePathFile);
            PlatformFile.CreateDirectoryTree(*writeDir);

            TUniquePtr<IFileHandle> OutputFile(PlatformFile.OpenWrite(*writePathFile));
            if (!OutputFile)
            {
                errStr = FString::Printf(TEXT("Failed to create output file"));
            }

            // Load all chunks from APK
            int32 PartIndex = 1;
            FString filename = FPaths::GetCleanFilename(writePathFile);
            FString baseFilename = FPaths::GetBaseFilename(writePathFile);
            while (errStr.IsEmpty()) // Infinite Loop until no more parts are found
            {
                FString PartPath = FPaths::Combine(readPath, FString::Printf(TEXT("%s-part%d"), *baseFilename, PartIndex));
                //FString::Printf(TEXT("%s.part%d"), *FPaths::Combine(readPath, filename), PartIndex);

                if (FPlatformFileManager::Get().GetPlatformFile().DirectoryExists(*PartPath)) {
                    PartPath = FPaths::Combine(PartPath, FString::Printf(TEXT("%s.part%d"), *filename, PartIndex));
                }
                else {
                    PartPath = FPaths::Combine(readPath, FString::Printf(TEXT("%s.part%d"), *filename, PartIndex));
                }

                IMappedFileHandle* MappedFile = PlatformFile.OpenMapped(*PartPath);
                if (!MappedFile) break; // Assuming no more parts if mapping fails

                IMappedFileRegion* mappedRegion = MappedFile->MapRegion(0, MappedFile->GetFileSize());

                FLlamaString::LogPrint(ELogVerbosity::Type::Verbose, TEXT("%s %s"), functionName, *FPaths::GetCleanFilename(PartPath));
                bSuccess = OutputFile->Write(mappedRegion->GetMappedPtr(), mappedRegion->GetMappedSize());

                // Closing the mapped region and file
                delete mappedRegion;
                delete MappedFile;

                PartIndex++;
            }

            // Notify main thread
            PostMergeModel(bSuccess, errStr, writePathFile, PostMergeCallback, CallbackAsyncExec);
        });
}

void FLLamaModelUtils::PostMergeModel(bool bSuccess, FString errStr, const FString& writePathFile, TFunction<void(const FString&, bool)> PostMergeCallback, EAsyncExecution CallbackAsyncExec)
{
    const TCHAR* functionName = TEXT("FLlamaPaths::PostMergeModel");
    Async(CallbackAsyncExec, [functionName, errStr, writePathFile, bSuccess, PostMergeCallback]()
        {
            FLlamaString::LogPrint(bSuccess ? ELogVerbosity::Type::Verbose : ELogVerbosity::Type::Warning, TEXT("%s %s"), functionName, bSuccess ? TEXT("Sucess!") : *FString::Printf(TEXT("Failed: %s"), *errStr));
            if (PostMergeCallback)
            {
                FLlamaString::LogPrint(ELogVerbosity::Type::Verbose, TEXT("%s Executing Callback"), functionName);
                PostMergeCallback(writePathFile, bSuccess);
            }
        });
}

//FLlamaPaths
FString FLlamaPaths::ModelsRelativeRootPath()
{
    FString AbsoluteFilePath;

    // For Packaging, need to add folder ref in "Additional Non-Asset Directories (To Copy/To Package)"
	// In this case, ./Content/Resources/Models/
#if PLATFORM_ANDROID
    //This is the path we're allowed to sample on android
    // FAndroidMisc::GamePersistentDownloadDir() - /storage/emulated/0/Android/data/[PackageName]/files
    //AbsoluteFilePath = FPaths::Combine(FString(FAndroidMisc::GamePersistentDownloadDir()), "Models/");

    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile(); // Required here because of android DIR check

    for (FString eachPath : FLLamaModelUtils::GetPathListAndroid()) {
        if (PlatformFile.DirectoryExists(*eachPath)) {
            AbsoluteFilePath = FPaths::Combine(FPaths::Combine(FString(eachPath), "Models/"));
            break;
        }
    }
#else
    AbsoluteFilePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), "Models/")); // For External (Saved)
#endif

    FLlamaString::LogPrint(ELogVerbosity::Type::Verbose, TEXT("ModelsRelativeRootPath: %s"), *AbsoluteFilePath);

    return AbsoluteFilePath;
}

FString FLlamaPaths::ParsePathIntoFullPath(const FString& InRelativeOrAbsolutePath)
{
    FString FinalPath;

    //Is it a relative path?
    if (InRelativeOrAbsolutePath.StartsWith(TEXT(".")))
    {
        //relative path
        //UE_LOG(LogTemp, Log, TEXT("model returning relative path"));
        FinalPath = FPaths::ConvertRelativePathToFull(FLlamaPaths::ModelsRelativeRootPath() + InRelativeOrAbsolutePath);
    }
    else
    {
        //Already an absolute path
        //UE_LOG(LogTemp, Log, TEXT("model returning absolute path"));
        FinalPath = FPaths::ConvertRelativePathToFull(InRelativeOrAbsolutePath);
    }

    return FinalPath;
}

TArray<FString> FLlamaPaths::DebugListDirectoryContent(const FString& InPath)
{
    TArray<FString> Entries;

    FString FullPathDirectory;

    if (InPath.Contains(TEXT("<ProjectDir>")))
    {
        FString Remainder = InPath.Replace(TEXT("<ProjectDir>"), TEXT(""));

        FullPathDirectory = FPaths::ProjectDir() + Remainder;
    }
    else if (InPath.Contains(TEXT("<Content>")))
    {
        FString Remainder = InPath.Replace(TEXT("<Content>"), TEXT(""));

        FullPathDirectory = FPaths::ProjectContentDir() + Remainder;
    }
    else if (InPath.Contains(TEXT("<External>")))
    {
        FString Remainder = InPath.Replace(TEXT("<Content>"), TEXT(""));

#if PLATFORM_ANDROID
        //FString ExternalStoragePath = FString(FAndroidMisc::GamePersistentDownloadDir());
        FString ExternalStoragePath = FString(FPaths::ProjectPersistentDownloadDir());
        FullPathDirectory = ExternalStoragePath + Remainder;
#else
        UE_LOG(LogTemp, Warning, TEXT("Externals not valid in this context!"));
        //FullPathDirectory = FLlamaNative::ParsePathIntoFullPath(Remainder);
#endif
    }
    else
    {
        //FullPathDirectory = FLlamaNative::ParsePathIntoFullPath(InPath);
    }

    IFileManager& FileManager = IFileManager::Get();

    FullPathDirectory = FPaths::ConvertRelativePathToFull(FullPathDirectory);

    FullPathDirectory = FileManager.ConvertToAbsolutePathForExternalAppForRead(*FullPathDirectory);

    Entries.Add(FullPathDirectory);

    UE_LOG(LogTemp, Log, TEXT("Listing contents of <%s>"), *FullPathDirectory);

    // Find directories
    TArray<FString> Directories;
    FString FinalPath = FullPathDirectory / TEXT("*");
    FileManager.FindFiles(Directories, *FinalPath, false, true);
    for (FString Entry : Directories)
    {
        FString FullPath = FullPathDirectory / Entry;
        if (FileManager.DirectoryExists(*FullPath)) // Filter for directories
        {
            UE_LOG(LogTemp, Log, TEXT("Found directory: %s"), *Entry);
            Entries.Add(Entry);
        }
    }

    // Find files
    TArray<FString> Files;
    FileManager.FindFiles(Files, *FullPathDirectory, TEXT("*.*")); // Find all entries
    for (FString Entry : Files)
    {
        FString FullPath = FullPathDirectory / Entry;
        if (!FileManager.DirectoryExists(*FullPath)) // Filter out directories
        {
            UE_LOG(LogTemp, Log, TEXT("Found file: %s"), *Entry);
            Entries.Add(Entry);
        }
    }

    return Entries;
}

//FLlamaString
FString FLlamaString::ToUE(const std::string& String)
{
    return FString(UTF8_TO_TCHAR(String.c_str()));
}

std::string FLlamaString::ToStd(const FString& String)
{
    return std::string(TCHAR_TO_UTF8(*String));
}

bool FLlamaString::IsSentenceEndingPunctuation(const TCHAR Char)
{
    return Char == TEXT('.') || Char == TEXT('!') || Char == TEXT('?');
}

FString FLlamaString::GetLastSentence(const FString& InputString)
{
    int32 LastPunctuationIndex = INDEX_NONE;
    int32 PrecedingPunctuationIndex = INDEX_NONE;

    // Find the last sentence-ending punctuation
    for (int32 i = InputString.Len() - 1; i >= 0; --i)
    {
        if (IsSentenceEndingPunctuation(InputString[i]))
        {
            LastPunctuationIndex = i;
            break;
        }
    }

    // If no punctuation found, return the entire string
    if (LastPunctuationIndex == INDEX_NONE)
    {
        return InputString;
    }

    // Find the preceding sentence-ending punctuation
    for (int32 i = LastPunctuationIndex - 1; i >= 0; --i)
    {
        if (IsSentenceEndingPunctuation(InputString[i]))
        {
            PrecedingPunctuationIndex = i;
            break;
        }
    }

    // Extract the last sentence
    int32 StartIndex = PrecedingPunctuationIndex == INDEX_NONE ? 0 : PrecedingPunctuationIndex + 1;
    return InputString.Mid(StartIndex, LastPunctuationIndex - StartIndex + 1).TrimStartAndEnd();
}

void FLlamaString::AppendToCharVector(std::vector<char>& VectorHistory, const std::string& Text)
{
    VectorHistory.insert(VectorHistory.end(), Text.begin(), Text.end());
}


