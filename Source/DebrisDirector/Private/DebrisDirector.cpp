// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "DebrisDirector.h"
#include "DebrisDirectorLog.h"

DEFINE_LOG_CATEGORY(LogDebrisDirector);

#define LOCTEXT_NAMESPACE "FDebrisDirectorModule"

void FDebrisDirectorModule::StartupModule()
{
	UE_LOG(LogDebrisDirector, Log, TEXT("DebrisDirector started."));
}

void FDebrisDirectorModule::ShutdownModule()
{
	UE_LOG(LogDebrisDirector, Log, TEXT("DebrisDirector shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDebrisDirectorModule, DebrisDirector)
