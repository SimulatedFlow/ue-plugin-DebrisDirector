// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * Runtime module for DebrisDirector. Loads at PreDefault so the world subsystem, the piece actor and the
 * Debris.* console commands all exist before the first game world is created - a level that starts with a
 * scripted explosion has to be able to report impacts on its very first frame.
 */
class FDebrisDirectorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
