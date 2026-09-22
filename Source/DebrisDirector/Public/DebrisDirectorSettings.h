// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "DebrisDirectorTypes.h"
#include "DebrisDirectorSettings.generated.h"

class UDebrisProfile;

/**
 * Project-wide defaults for DebrisDirector, under Project Settings -> Plugins -> DebrisDirector.
 *
 * Everything here is a cost or a ceiling. What a surface looks like is on the profile, because that is an art
 * decision a designer changes per material; what a frame may cost is here, because that is one decision a
 * project makes once and then holds to on every platform it ships on.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "DebrisDirector"))
class DEBRISDIRECTOR_API UDebrisDirectorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UDebrisDirectorSettings();

	//~ UDeveloperSettings interface
	virtual FName GetCategoryName() const override;
	virtual FName GetSectionName() const override;

	/** The settings object, never null. */
	static const UDebrisDirectorSettings& Get();

	//~ Master switch ------------------------------------------------------------------------------------

	/**
	 * Run the director at all.
	 *
	 * Off keeps the subsystem, the counter box and the requests - they are counted and then discarded, so a
	 * project can see how much debris a scene is asking for before deciding to let any of it exist.
	 */
	UPROPERTY(config, EditAnywhere, Category = "General")
	bool bEnabled = true;

	//~ Budget -------------------------------------------------------------------------------------------

	/** How many pieces one frame may create, how they are merged and what is too weak to bother with. */
	UPROPERTY(config, EditAnywhere, Category = "Budget")
	FDebrisPlanRules PlanRules;

	/**
	 * How many pieces may exist at once.
	 *
	 * Reached, the next spawn evicts the worst-ranked piece rather than being refused - debris is the one
	 * thing a player expects to appear immediately, and the honest way to pay for that is to take something
	 * else away, in the order the ranking decides, in front of the counter that shows it happening.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Budget", meta = (ClampMin = "0", UIMax = "8192"))
	int32 PopulationCap = 400;

	/**
	 * How many pieces may be evicted in one frame.
	 *
	 * An eviction is cheap, but it is not free, and the frame that lowers the ceiling from 400 to 100 should
	 * not be the frame that shows the spike this plugin exists to remove.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Budget", meta = (ClampMin = "1", UIMax = "512"))
	int32 MaxEvictionsPerFrame = 48;

	/** How many impact requests may wait in the queue. Above this, requests are dropped and counted. */
	UPROPERTY(config, EditAnywhere, Category = "Budget", meta = (ClampMin = "16", UIMax = "16384"))
	int32 MaxPendingRequests = 2048;

	/** The order pieces are taken away in. */
	UPROPERTY(config, EditAnywhere, Category = "Budget")
	FDebrisEvictionWeights EvictionWeights;

	//~ Physics ------------------------------------------------------------------------------------------

	/**
	 * Freeze a piece once its simulation time is up.
	 *
	 * This is the switch, not a fallback. Off is what a project has today: bodies that rely on engine sleep
	 * thresholds and therefore never quite stop. It is on a demo button so the difference can be read off
	 * the simulating-against-sleeping line instead of argued about.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Physics")
	bool bPhysicsSleepEnabled = true;

	/** Default simulation time for profiles that do not set their own, in seconds. */
	UPROPERTY(config, EditAnywhere, Category = "Physics",
		meta = (ClampMin = "0.0", UIMax = "30.0", ForceUnits = "s"))
	float DefaultMaxSimSeconds = 3.0f;

	//~ Lifetime -----------------------------------------------------------------------------------------

	/** Default lifetime for profiles that do not set their own, in seconds. */
	UPROPERTY(config, EditAnywhere, Category = "Lifetime",
		meta = (ClampMin = "0.1", UIMax = "600.0", ForceUnits = "s"))
	float DefaultLifetimeSeconds = 20.0f;

	/**
	 * How long a piece takes to fade out, in seconds.
	 *
	 * Short on purpose. Long enough that nothing blinks, short enough that an evicted piece has really gone
	 * by the time the ceiling needs the slot back. Zero removes pieces instantly, which is visible and is
	 * the thing this plugin was written to stop.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Lifetime",
		meta = (ClampMin = "0.0", UIMax = "3.0", ForceUnits = "s"))
	float FadeSeconds = 0.35f;

	//~ Decals -------------------------------------------------------------------------------------------

	/** Let profiles drop decals at all. */
	UPROPERTY(config, EditAnywhere, Category = "Decals")
	bool bDecalsEnabled = true;

	/** How many decals may exist at once. The oldest is removed to make room. */
	UPROPERTY(config, EditAnywhere, Category = "Decals", meta = (ClampMin = "0", UIMax = "2048"))
	int32 DecalCap = 128;

	/** Default decal lifetime for profiles that do not set their own, in seconds. */
	UPROPERTY(config, EditAnywhere, Category = "Decals",
		meta = (ClampMin = "0.1", UIMax = "600.0", ForceUnits = "s"))
	float DefaultDecalLifetimeSeconds = 12.0f;

	//~ Pooling ------------------------------------------------------------------------------------------

	/**
	 * How many parked pieces one profile may keep.
	 *
	 * A parked piece is hidden, unticked, uncollidable and costs one actor of memory. Keeping the pool at
	 * least as large as the share of the ceiling that profile usually holds is what makes the new-allocation
	 * counter stop moving.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Pooling", meta = (ClampMin = "0", UIMax = "4096"))
	int32 MaxPooledPerProfile = 256;

	/** Move parked pieces out of the level rather than leaving them where they died. */
	UPROPERTY(config, EditAnywhere, Category = "Pooling")
	bool bParkPooledPieces = true;

	/** Where parked pieces are put when bParkPooledPieces is set. */
	UPROPERTY(config, EditAnywhere, Category = "Pooling", meta = (EditCondition = "bParkPooledPieces"))
	FVector ParkLocation = FVector(0.0f, 0.0f, -100000.0f);

	//~ Profiles -----------------------------------------------------------------------------------------

	/**
	 * Profiles loaded when a world starts, so a request can name a surface that nothing has referenced yet.
	 *
	 * Without this, the first "Stone" request in a level whose stone profile is only referenced by an asset
	 * that has not streamed in yet would silently fall back to the default - the kind of bug that only shows
	 * up in a cooked build.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Profiles", meta = (AllowedClasses = "/Script/DebrisDirector.DebrisProfile"))
	TArray<TSoftObjectPtr<UDebrisProfile>> StartupProfiles;

	/** Profile used by a request that names no surface and whose physical surface matches nothing. */
	UPROPERTY(config, EditAnywhere, Category = "Profiles", meta = (AllowedClasses = "/Script/DebrisDirector.DebrisProfile"))
	TSoftObjectPtr<UDebrisProfile> DefaultProfile;

	//~ Presentation -------------------------------------------------------------------------------------

	/** Start with the counter box on. */
	UPROPERTY(config, EditAnywhere, Category = "Presentation")
	bool bShowStatsByDefault = true;

	/**
	 * Draw the counter box through AHUD::OnHUDPostRender as well, so a project keeps its own HUD class.
	 *
	 * ADebrisDirectorHUD exists for projects that have no HUD of their own. This is for the far more common
	 * case where a project already has one and is not about to reparent it to see a debris counter.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Presentation")
	bool bAutoDrawStatsOnAnyHUD = false;
};
