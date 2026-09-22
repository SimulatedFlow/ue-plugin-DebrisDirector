// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectPtr.h"
#include "DebrisDirectorTypes.h"
#include "DebrisDirectorSubsystem.generated.h"

class ADebrisPiece;
class AHUD;
class UCanvas;
class UDebrisProfile;
class UDecalComponent;

/** Fired once per created piece, after it is in the world. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDebrisPieceSpawned, ADebrisPiece*, Piece, UDebrisProfile*, Profile);

/** Fired when a piece starts fading out because the ceiling needed its slot. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDebrisPieceEvicted, ADebrisPiece*, Piece);

/** One live piece: the actor, its numbers, and the profile it came from. */
USTRUCT()
struct FDebrisLivePiece
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<ADebrisPiece> Piece = nullptr;

	UPROPERTY()
	TObjectPtr<UDebrisProfile> Profile = nullptr;

	UPROPERTY()
	FDebrisPieceState State;
};

/** The parked pieces of one profile. */
USTRUCT()
struct FDebrisPiecePool
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<TObjectPtr<ADebrisPiece>> Parked;
};

/** One decal the director owns, so the decal ceiling can take the oldest away. */
USTRUCT()
struct FDebrisLiveDecal
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UDecalComponent> Decal = nullptr;

	/** Seconds left before it is removed. */
	float SecondsRemaining = 0.0f;
};

/** What one viewer of the world looks like to the eviction ranking. */
struct FDebrisViewer
{
	FVector Location = FVector::ZeroVector;
	FVector Forward = FVector::ForwardVector;
	float CosHalfFov = 0.0f;
};

/**
 * The whole plugin, running once per frame in one place.
 *
 * A game reports impacts. This decides how many of them become pieces, which pieces stop existing when the
 * scene is full, and when a piece stops costing physics time. It does not decide what a piece looks like -
 * that is the profile - and it never breaks anything apart.
 *
 * **The budget is not a cut-off.** Requests that arrive in one frame are planned together, not serviced one
 * at a time. A grenade reporting forty impacts against a budget of twenty-four does not lose sixteen of
 * them: impacts in the same grid cell become one larger piece, and if that is still over budget the
 * remainder are folded into their nearest neighbour. The frame creates twenty-four objects and the pattern
 * on the wall is complete. That is why an explosion costs no more frame time than a rifle shot.
 *
 * **A full scene evicts, it does not refuse.** Debris is the one thing a player expects the instant they
 * pull the trigger, so at the ceiling the new piece is created and the worst-ranked old one is taken away -
 * least important first, then furthest, then oldest, and never the one the player is looking at while
 * there is anything else to take. It leaves over a short fade rather than vanishing.
 *
 * **Physics has an expiry date.** Every piece simulates for at most MaxSimSeconds and is then frozen where
 * it lies. Engine sleep thresholds only settle a body that has genuinely come to rest; the piece jittering
 * in a corner or resting on another moving piece never does, and a hundred of those is a permanent slice of
 * every physics tick. The simulating-against-sleeping line on the counter box is that difference, live.
 *
 * Game and PIE only. Filling a level designer's viewport with rubble is not help.
 */
UCLASS(meta = (DisplayName = "Debris Director Subsystem"))
class DEBRISDIRECTOR_API UDebrisDirectorSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	//~ UWorldSubsystem interface
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	//~ FTickableGameObject interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** The director for whatever world the context object belongs to, or null. */
	static UDebrisDirectorSubsystem* Get(const UObject* WorldContextObject);

	//~ Requests -----------------------------------------------------------------------------------------

	/**
	 * Report one impact. False when the queue is full or the director is switched off.
	 *
	 * Nothing is created here. The request waits for the end of the frame it arrived in, because merging
	 * five impacts into one piece is only possible once all five are known - a spawner that acts on the
	 * first request it sees has already spent the budget by the time the fifth arrives.
	 */
	UFUNCTION(BlueprintCallable, Category = "DebrisDirector", meta = (AutoCreateRefTerm = "Request"))
	bool RequestBurst(const FDebrisRequest& Request);

	/** Report a whole burst at once. Returns how many were accepted. */
	UFUNCTION(BlueprintCallable, Category = "DebrisDirector", meta = (AutoCreateRefTerm = "Requests"))
	int32 RequestBurstMany(const TArray<FDebrisRequest>& Requests);

	/** Fire Count fabricated impacts around the viewer in one frame. What Debris.Stress runs. */
	UFUNCTION(BlueprintCallable, Category = "DebrisDirector")
	int32 RequestStress(int32 Count, float Radius = 400.0f);

	/** Take every piece and decal away at once, with no fade. */
	UFUNCTION(BlueprintCallable, Category = "DebrisDirector")
	void Clear();

	//~ Budget -------------------------------------------------------------------------------------------

	/** How many pieces one frame may create. */
	UFUNCTION(BlueprintCallable, Category = "DebrisDirector")
	void SetBudget(int32 SpawnBudget);

	/** How many pieces one frame may create. */
	UFUNCTION(BlueprintPure, Category = "DebrisDirector")
	int32 GetBudget() const { return PlanRules.SpawnBudget; }

	/** How many pieces may exist. Lowering it below the current population evicts the excess. */
	UFUNCTION(BlueprintCallable, Category = "DebrisDirector")
	void SetPopulationCap(int32 NewPopulationCap);

	/** How many pieces may exist. */
	UFUNCTION(BlueprintPure, Category = "DebrisDirector")
	int32 GetPopulationCap() const { return PopulationCap; }

	/** Pieces alive and counted against the ceiling. Fading ones have already left the count. */
	UFUNCTION(BlueprintPure, Category = "DebrisDirector")
	int32 GetPopulation() const;

	/** Pieces on their way out. */
	UFUNCTION(BlueprintPure, Category = "DebrisDirector")
	int32 GetFadingCount() const;

	/** Bodies still costing physics time. */
	UFUNCTION(BlueprintPure, Category = "DebrisDirector")
	int32 GetSimulatingCount() const;

	/** Bodies frozen by the expiry date. */
	UFUNCTION(BlueprintPure, Category = "DebrisDirector")
	int32 GetSleepingCount() const;

	/** The planning rules in force, budget included. */
	UFUNCTION(BlueprintPure, Category = "DebrisDirector")
	FDebrisPlanRules GetPlanRules() const { return PlanRules; }

	/** Replace the planning rules wholesale. */
	UFUNCTION(BlueprintCallable, Category = "DebrisDirector", meta = (AutoCreateRefTerm = "NewRules"))
	void SetPlanRules(const FDebrisPlanRules& NewRules);

	//~ Switches -----------------------------------------------------------------------------------------

	/** Merge requests above the budget, or drop them. */
	UFUNCTION(BlueprintCallable, Category = "DebrisDirector")
	void SetMergeEnabled(bool bEnabled);

	/** Whether requests above the budget are merged. */
	UFUNCTION(BlueprintPure, Category = "DebrisDirector")
	bool IsMergeEnabled() const { return PlanRules.bMergeEnabled; }

	/** Freeze pieces when their simulation time is up. Off leaves them to the engine's sleep thresholds. */
	UFUNCTION(BlueprintCallable, Category = "DebrisDirector")
	void SetPhysicsSleepEnabled(bool bEnabled);

	/** Whether the physics expiry date is in force. */
	UFUNCTION(BlueprintPure, Category = "DebrisDirector")
	bool IsPhysicsSleepEnabled() const { return bPhysicsSleepEnabled; }

	/** Run the director at all. Off counts requests and creates nothing. */
	UFUNCTION(BlueprintCallable, Category = "DebrisDirector")
	void SetEnabled(bool bNewEnabled);

	/** Whether the director is running. */
	UFUNCTION(BlueprintPure, Category = "DebrisDirector")
	bool IsEnabled() const { return bEnabled; }

	/** Show or hide the counter box. */
	UFUNCTION(BlueprintCallable, Category = "DebrisDirector")
	void SetShowStats(bool bShow);

	/** Whether the counter box is on. */
	UFUNCTION(BlueprintPure, Category = "DebrisDirector")
	bool AreStatsShown() const { return bShowStats; }

	//~ Profiles -----------------------------------------------------------------------------------------

	/** Make a profile answerable under its SurfaceType and its physical surfaces. Harmless to call twice. */
	UFUNCTION(BlueprintCallable, Category = "DebrisDirector")
	void RegisterProfile(UDebrisProfile* Profile);

	/** Which profile a request would use. Null when nothing matches and there is no default. */
	UFUNCTION(BlueprintPure, Category = "DebrisDirector", meta = (AutoCreateRefTerm = "Request"))
	UDebrisProfile* ResolveProfile(const FDebrisRequest& Request) const;

	/** Every profile the director knows about. */
	UFUNCTION(BlueprintPure, Category = "DebrisDirector")
	void GetRegisteredProfiles(TArray<UDebrisProfile*>& OutProfiles) const;

	//~ Statistics ---------------------------------------------------------------------------------------

	/** Everything the counter box draws, as of the last tick. */
	UFUNCTION(BlueprintPure, Category = "DebrisDirector")
	FDebrisDirectorStats GetStats() const { return Stats; }

	/** Draw the counter box at this corner and width. Called by ADebrisDirectorHUD. */
	void DrawStatsBox(UCanvas* Canvas, const FVector2D& Origin, float Width) const;

	/** How many lines the box will draw, so a caller can size a background around it. */
	int32 GetStatsLineCount() const;

	/** Print the same numbers to the log. What Debris.Stats runs. */
	void LogStats() const;

	//~ Events -------------------------------------------------------------------------------------------

	/** A piece has just been created. */
	UPROPERTY(BlueprintAssignable, Category = "DebrisDirector|Events")
	FDebrisPieceSpawned OnPieceSpawned;

	/** A piece has started fading out because the ceiling needed its slot. */
	UPROPERTY(BlueprintAssignable, Category = "DebrisDirector|Events")
	FDebrisPieceEvicted OnPieceEvicted;

private:
	//~ Tick stages
	void ApplySettings();
	void LoadStartupProfiles();
	void UpdateViewers();
	void ServiceRequests();
	void AdvancePieces(float DeltaTime);
	void AdvanceDecals(float DeltaTime);
	void UpdateStats(double TickStartSeconds, float DeltaTime);

	/** Take the worst-ranked Count pieces out of the population by starting their fade. Returns how many. */
	int32 EvictWorst(int32 Count);

	/** Create one piece for a planned spawn, out of the pool where possible. */
	ADebrisPiece* SpawnPlanned(const FDebrisPlannedSpawn& Planned, UDebrisProfile* Profile);

	/** Drop a decal for a planned spawn, if the profile has one and the decal ceiling allows it. */
	void SpawnDecal(const FDebrisPlannedSpawn& Planned, UDebrisProfile* Profile);

	/** Hand a live piece back to its pool, or destroy it when the pool is full. */
	void RecyclePiece(int32 LiveIndex);

	/** Distance from a point to the nearest viewer, and whether it is inside a camera cone. */
	float DistanceToNearestViewer(const FVector& Point, bool* bOutVisible = nullptr) const;

	void RebindHudDelegate();
	void OnAnyHUDPostRender(AHUD* HUD, UCanvas* Canvas);

	//~ State --------------------------------------------------------------------------------------------

	UPROPERTY()
	TArray<FDebrisRequest> PendingRequests;

	UPROPERTY()
	TArray<FDebrisLivePiece> LivePieces;

	UPROPERTY()
	TMap<TObjectPtr<UDebrisProfile>, FDebrisPiecePool> Pools;

	UPROPERTY()
	TArray<FDebrisLiveDecal> LiveDecals;

	UPROPERTY()
	TMap<FName, TObjectPtr<UDebrisProfile>> ProfilesByName;

	UPROPERTY()
	TMap<uint8, TObjectPtr<UDebrisProfile>> ProfilesByPhysicalSurface;

	UPROPERTY()
	TObjectPtr<UDebrisProfile> DefaultProfile = nullptr;

	TArray<FDebrisViewer> Viewers;

	FDebrisPlanRules PlanRules;
	FDebrisEvictionWeights EvictionWeights;

	int32 PopulationCap = 400;
	int32 MaxEvictionsPerFrame = 48;
	int32 MaxPendingRequests = 2048;
	int32 MaxPooledPerProfile = 256;
	int32 DecalCap = 128;

	float DefaultMaxSimSeconds = 3.0f;
	float DefaultLifetimeSeconds = 20.0f;
	float DefaultDecalLifetimeSeconds = 12.0f;
	float FadeSeconds = 0.35f;
	FVector ParkLocation = FVector(0.0f, 0.0f, -100000.0f);

	bool bEnabled = true;
	bool bPhysicsSleepEnabled = true;
	bool bDecalsEnabled = true;
	bool bParkPooledPieces = true;
	bool bShowStats = true;
	bool bAutoDrawStatsOnAnyHUD = false;

	/** Rolling one-second window behind the evictions-per-second line. */
	int32 EvictionsInWindow = 0;
	float EvictionWindowSeconds = 0.0f;

	int32 TotalSpawned = 0;
	int32 TotalMerged = 0;
	int32 TotalDropped = 0;
	int32 TotalEvicted = 0;
	int32 PoolHits = 0;
	int32 NewAllocations = 0;

	FDebrisDirectorStats Stats;

	/** Frame the box was last drawn on, so the HUD delegate cannot stack a second one on top of it. */
	mutable uint64 LastStatsDrawFrame = 0;

	FDelegateHandle HudPostRenderHandle;
};
