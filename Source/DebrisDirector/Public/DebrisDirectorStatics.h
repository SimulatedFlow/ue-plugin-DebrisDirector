// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DebrisDirectorTypes.h"
#include "DebrisDirectorStatics.generated.h"

class UDebrisProfile;

/**
 * The Blueprint face of DebrisDirector, and - more importantly - the place the two decisions live.
 *
 * PlanBurst and RankForEviction are static and take nothing but plain structs. That is not tidiness, it is
 * the reason those two functions have tests at all: a UTickableWorldSubsystem cannot be created in an
 * automation test, so every rule written inside one is a rule that ships unverified. Here, the merge and the
 * eviction order can be driven with a thousand fabricated requests in under a millisecond, and the running
 * subsystem calls exactly these functions rather than a second copy of them that agrees until somebody edits
 * one of the two.
 *
 * Everything below the pure-logic section needs a world and simply forwards to the subsystem, so a Blueprint
 * never has to fetch a subsystem to report an impact.
 */
UCLASS(meta = (DisplayName = "Debris Director Statics"))
class DEBRISDIRECTOR_API UDebrisDirectorStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	//~ Pure logic - no world, fully testable -------------------------------------------------------------

	/**
	 * Turn a frame's worth of impact requests into at most SpawnBudget pieces.
	 *
	 * Two passes, and the second one is the product:
	 *
	 *   1. Requests are bucketed by grid cell and surface. Everything in one bucket becomes one piece,
	 *      placed at the impulse-weighted centre of the bucket and enlarged by the cube root of how many
	 *      requests it stands for, because volume goes as the cube of length and a piece that stands for
	 *      eight impacts should look like eight, not like eight hundred.
	 *
	 *   2. If that still leaves more pieces than the budget, the survivors are the most important ones, and
	 *      every piece that did not survive is folded into its nearest surviving neighbour instead of being
	 *      thrown away. Its MergedCount is added, so the sum of MergedCount over the result is always
	 *      exactly the number of requests that came in, and the piece it joined grows accordingly.
	 *
	 * That invariant - nothing is lost, things only get bigger - is what separates a budget from a cut-off,
	 * and it is asserted by the tests rather than described in a marketing line.
	 *
	 * With Rules.bMergeEnabled off, neither pass runs: requests above the budget are dropped and counted as
	 * dropped. That path exists so the difference can be seen on one screen, not because it is a good idea.
	 */
	UFUNCTION(BlueprintPure, Category = "DebrisDirector|Planning", meta = (AutoCreateRefTerm = "Requests,Rules"))
	static void PlanBurst(const TArray<FDebrisRequest>& Requests, const FDebrisPlanRules& Rules,
		TArray<FDebrisPlannedSpawn>& OutPlanned, FDebrisPlanReport& OutReport);

	/**
	 * How evictable one piece is. Higher goes first.
	 *
	 * Importance dominates, distance decides between equals, age breaks what is left, being on screen is
	 * worth a discount rather than an exemption. The weights that produce that ordering are documented on
	 * FDebrisEvictionWeights; this function is just the sum.
	 */
	UFUNCTION(BlueprintPure, Category = "DebrisDirector|Eviction", meta = (AutoCreateRefTerm = "Candidate,Weights"))
	static float ScoreForEviction(const FDebrisEvictionCandidate& Candidate, const FDebrisEvictionWeights& Weights);

	/**
	 * The indices of the Count worst-ranked pieces, worst first.
	 *
	 * Returns fewer than Count only when there are fewer candidates than that. The sort is stable, so two
	 * pieces that score identically are evicted in the order they were created, which keeps a scene that is
	 * being cleared from flickering between two equally worthless chips.
	 */
	UFUNCTION(BlueprintPure, Category = "DebrisDirector|Eviction", meta = (AutoCreateRefTerm = "Candidates,Weights"))
	static void RankForEviction(const TArray<FDebrisEvictionCandidate>& Candidates, const FDebrisEvictionWeights& Weights,
		int32 Count, TArray<int32>& OutIndices);

	/**
	 * How many pieces have to go before Incoming new ones fit under the ceiling.
	 *
	 * One line, and it is here rather than inline in the subsystem because a ceiling that is enforced in the
	 * middle of a spawn loop is a ceiling that is enforced differently on the day somebody adds a second
	 * spawn path.
	 */
	UFUNCTION(BlueprintPure, Category = "DebrisDirector|Eviction")
	static int32 PlanEvictionCount(int32 Population, int32 Incoming, int32 PopulationCap);

	/**
	 * Advance one piece by DeltaSeconds and report the one thing that changed, if anything.
	 *
	 * The four-state machine of the whole plugin - simulating, frozen, fading, gone - in one testable
	 * function. Returns at most one event per call, so a caller never has to guess which of two things to
	 * react to first.
	 */
	UFUNCTION(BlueprintCallable, Category = "DebrisDirector|Lifetime")
	static EDebrisLifetimeEvent AdvancePiece(UPARAM(ref) FDebrisPieceState& State, float DeltaSeconds);

	/** Start the fade-out on a piece that is not already fading. False when it was already on its way out. */
	UFUNCTION(BlueprintCallable, Category = "DebrisDirector|Lifetime")
	static bool BeginFade(UPARAM(ref) FDebrisPieceState& State);

	/** How much bigger a piece standing for MergedCount impacts should be. The cube root, clamped. */
	UFUNCTION(BlueprintPure, Category = "DebrisDirector|Planning")
	static float MergedScaleMultiplier(int32 MergedCount, float MaxScaleMultiplier);

	/** The merge grid cell a point falls into. Exposed because a test that cannot see the key proves less. */
	UFUNCTION(BlueprintPure, Category = "DebrisDirector|Planning")
	static FIntVector MergeCellForLocation(const FVector& Location, float CellSize);

	//~ World entries -------------------------------------------------------------------------------------

	/** Report one impact. False when there is no director, or the queue is full. */
	UFUNCTION(BlueprintCallable, Category = "DebrisDirector",
		meta = (WorldContext = "WorldContextObject", AutoCreateRefTerm = "Request"))
	static bool ReportImpact(const UObject* WorldContextObject, const FDebrisRequest& Request);

	/** Report an impact from the parts a hit result already has. The call a weapon Blueprint actually makes. */
	UFUNCTION(BlueprintCallable, Category = "DebrisDirector",
		meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "4"))
	static bool ReportImpactAt(const UObject* WorldContextObject, const FVector& Location, const FVector& Normal,
		float Impulse, FName SurfaceType, float Importance, bool bWantsDecal);

	/** Report a whole burst at once. Returns how many were accepted into the queue. */
	UFUNCTION(BlueprintCallable, Category = "DebrisDirector",
		meta = (WorldContext = "WorldContextObject", AutoCreateRefTerm = "Requests"))
	static int32 ReportImpacts(const UObject* WorldContextObject, const TArray<FDebrisRequest>& Requests);

	/** Fire Count fabricated impacts around the viewer in a single frame. What Debris.Stress runs. */
	UFUNCTION(BlueprintCallable, Category = "DebrisDirector", meta = (WorldContext = "WorldContextObject"))
	static int32 RequestStress(const UObject* WorldContextObject, int32 Count, float Radius);

	/** Take every piece and decal away at once, without a fade. */
	UFUNCTION(BlueprintCallable, Category = "DebrisDirector", meta = (WorldContext = "WorldContextObject"))
	static void Clear(const UObject* WorldContextObject);

	/** Set the per-frame spawn budget. The demo map's 4 / 24 / 128 buttons. */
	UFUNCTION(BlueprintCallable, Category = "DebrisDirector", meta = (WorldContext = "WorldContextObject"))
	static void SetBudget(const UObject* WorldContextObject, int32 SpawnBudget);

	/** The per-frame spawn budget in force. */
	UFUNCTION(BlueprintPure, Category = "DebrisDirector", meta = (WorldContext = "WorldContextObject"))
	static int32 GetBudget(const UObject* WorldContextObject);

	/** Set the population ceiling. Lowering it below the current population evicts the excess. */
	UFUNCTION(BlueprintCallable, Category = "DebrisDirector", meta = (WorldContext = "WorldContextObject"))
	static void SetPopulationCap(const UObject* WorldContextObject, int32 PopulationCap);

	/** The population ceiling. */
	UFUNCTION(BlueprintPure, Category = "DebrisDirector", meta = (WorldContext = "WorldContextObject"))
	static int32 GetPopulationCap(const UObject* WorldContextObject);

	/** Merge requests above the budget, or drop them. The demo map's comparison button. */
	UFUNCTION(BlueprintCallable, Category = "DebrisDirector", meta = (WorldContext = "WorldContextObject"))
	static void SetMergeEnabled(const UObject* WorldContextObject, bool bEnabled);

	/** Whether requests above the budget are merged. */
	UFUNCTION(BlueprintPure, Category = "DebrisDirector", meta = (WorldContext = "WorldContextObject"))
	static bool IsMergeEnabled(const UObject* WorldContextObject);

	/** Freeze pieces when their simulation time is up, or leave them to the engine's sleep thresholds. */
	UFUNCTION(BlueprintCallable, Category = "DebrisDirector", meta = (WorldContext = "WorldContextObject"))
	static void SetPhysicsSleepEnabled(const UObject* WorldContextObject, bool bEnabled);

	/** Whether the physics expiry date is in force. */
	UFUNCTION(BlueprintPure, Category = "DebrisDirector", meta = (WorldContext = "WorldContextObject"))
	static bool IsPhysicsSleepEnabled(const UObject* WorldContextObject);

	/** Show or hide the counter box. */
	UFUNCTION(BlueprintCallable, Category = "DebrisDirector", meta = (WorldContext = "WorldContextObject"))
	static void SetShowStats(const UObject* WorldContextObject, bool bShow);

	/** Whether the counter box is on. */
	UFUNCTION(BlueprintPure, Category = "DebrisDirector", meta = (WorldContext = "WorldContextObject"))
	static bool AreStatsShown(const UObject* WorldContextObject);

	/** Everything the counter box draws. */
	UFUNCTION(BlueprintPure, Category = "DebrisDirector", meta = (WorldContext = "WorldContextObject"))
	static FDebrisDirectorStats GetStats(const UObject* WorldContextObject);

	/** Pieces alive and counted against the ceiling. */
	UFUNCTION(BlueprintPure, Category = "DebrisDirector", meta = (WorldContext = "WorldContextObject"))
	static int32 GetPopulation(const UObject* WorldContextObject);

	/** Bodies still costing physics time. */
	UFUNCTION(BlueprintPure, Category = "DebrisDirector", meta = (WorldContext = "WorldContextObject"))
	static int32 GetSimulatingCount(const UObject* WorldContextObject);

	/** Bodies frozen by the expiry date. */
	UFUNCTION(BlueprintPure, Category = "DebrisDirector", meta = (WorldContext = "WorldContextObject"))
	static int32 GetSleepingCount(const UObject* WorldContextObject);

	/** Make a profile answerable under its own SurfaceType and physical surfaces from now on. */
	UFUNCTION(BlueprintCallable, Category = "DebrisDirector", meta = (WorldContext = "WorldContextObject"))
	static void RegisterProfile(const UObject* WorldContextObject, UDebrisProfile* Profile);
};
