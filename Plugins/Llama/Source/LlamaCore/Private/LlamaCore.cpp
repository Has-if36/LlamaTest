// Copyright 2025-current Getnamo.

#include "LlamaCore.h"
#include "LlamaUtility.h"
#include "Misc/Paths.h"

#include "HAL/FileManager.h"
#include "IPlatformFilePak.h"

#include "Misc/PackageName.h"
#include "Misc/PackagePath.h"

#if PLATFORM_ANDROID
#include <Android/AndroidPlatformMisc.h>
#endif

#define LOCTEXT_NAMESPACE "FLlamaCoreModule"

void FLlamaCoreModule::StartupModule()
{
	IModuleInterface::StartupModule();
}

void FLlamaCoreModule::ShutdownModule()
{
	IModuleInterface::ShutdownModule();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FLlamaCoreModule, LlamaCore)