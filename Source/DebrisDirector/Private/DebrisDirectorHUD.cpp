// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "DebrisDirectorHUD.h"

#include "DebrisDirectorSettings.h"
#include "DebrisDirectorSubsystem.h"
#include "Engine/Canvas.h"

ADebrisDirectorHUD::ADebrisDirectorHUD()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ADebrisDirectorHUD::BeginPlay()
{
	Super::BeginPlay();

	bShowStats = UDebrisDirectorSettings::Get().bShowStatsByDefault;
}

void ADebrisDirectorHUD::ToggleStats()
{
	bShowStats = !bShowStats;

	// Kept in step with the subsystem, so Debris.Show, this button and a widget's toggle all report the same
	// answer to whoever asks next.
	if (UDebrisDirectorSubsystem* Subsystem = UDebrisDirectorSubsystem::Get(this))
	{
		Subsystem->SetShowStats(bShowStats);
	}
}

void ADebrisDirectorHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	const UDebrisDirectorSubsystem* Subsystem = UDebrisDirectorSubsystem::Get(this);
	if (!Subsystem)
	{
		return;
	}

	// Either switch hides the box. The HUD's own flag is what a Blueprint or a widget button touches; the
	// subsystem's is what Debris.Show touches, and a console command has to work on a project that never
	// used this HUD class in the first place.
	if (!bShowStats || !Subsystem->AreStatsShown())
	{
		return;
	}

	// Everything drawn is read from the director on the frame it is drawn. Nothing is cached here, so the
	// box cannot claim one thing while the director does another.
	Subsystem->DrawStatsBox(Canvas, StatsBoxOrigin, StatsBoxWidth);
}
