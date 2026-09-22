// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Chaos/ChaosEngineInterface.h"
#include "DebrisDirectorTypes.generated.h"

/**
 * One thing that happened in the world. Not one piece of debris - one *event*.
 *
 * This is the whole interface a game needs. It says where something hit, how hard, which way the surface
 * faced and how much the moment mattered. It deliberately says nothing about meshes, counts or lifetimes:
 * those belong to the profile, because the weapon that fired does not know what the wall is made of, and the
 * wall does not know what hit it.
 */
USTRUCT(BlueprintType)
struct DEBRISDIRECTOR_API FDebrisRequest
{
	GENERATED_BODY()

	/** Where it hit, in world space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DebrisDirector")
	FVector Location = FVector::ZeroVector;

	/** The surface normal at the point of impact. Pieces leave along it; a zero normal becomes straight up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DebrisDirector")
	FVector Normal = FVector::UpVector;

	/**
	 * How hard it hit, in whatever unit the game already uses for damage or impulse.
	 *
	 * Two things read it: the launch impulse given to the piece, and the merge, which keeps the strongest
	 * impact's position when several are folded into one. A zero impulse is legal and produces a piece that
	 * simply drops.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DebrisDirector", meta = (ClampMin = "0.0"))
	float Impulse = 400.0f;

	/**
	 * Which profile to use, by name. Empty falls back to PhysicalSurface, then to the default profile.
	 *
	 * A name rather than a hard asset reference, so a weapon Blueprint can say "Stone" without loading -
	 * or knowing about - the data asset that decides what stone looks like.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DebrisDirector")
	FName SurfaceType = NAME_None;

	/**
	 * The physical surface off the hit result, used when SurfaceType is empty.
	 *
	 * This is the path that costs a project nothing: a game that already sets physical materials on its
	 * walls gets the right debris out of an existing FHitResult without touching the code that fires.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DebrisDirector")
	TEnumAsByte<EPhysicalSurface> PhysicalSurface = SurfaceType_Default;

	/**
	 * How much this impact matters, 0..1. The profile's value is used when this is negative.
	 *
	 * Importance is the first term of the eviction order, which makes it the one number worth setting by
	 * hand: the boss's shattered armour plate should still be lying there when the corridor's bullet chips
	 * have long been recycled.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DebrisDirector", meta = (UIMin = "-1.0", UIMax = "1.0"))
	float Importance = -1.0f;

	/** Whether this impact should also leave a decal, when the profile has one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DebrisDirector")
	bool bWantsDecal = true;
};

/**
 * One piece the director has decided to create, after merging.
 *
 * Note MergedCount. It is not a statistic, it is the receipt: the sum of MergedCount over a whole plan is
 * always exactly the number of requests that went in, which is how "over budget, nothing is lost" stops
 * being a claim and becomes something a test can assert.
 */
USTRUCT(BlueprintType)
struct DEBRISDIRECTOR_API FDebrisPlannedSpawn
{
	GENERATED_BODY()

	/** Where the piece goes. With several requests merged, this is the impulse-weighted centre of them. */
	UPROPERTY(BlueprintReadWrite, Category = "DebrisDirector")
	FVector Location = FVector::ZeroVector;

	/** Averaged surface normal of everything merged into this piece. */
	UPROPERTY(BlueprintReadWrite, Category = "DebrisDirector")
	FVector Normal = FVector::UpVector;

	/** The strongest impulse of everything merged in. The loudest hit wins, rather than the average. */
	UPROPERTY(BlueprintReadWrite, Category = "DebrisDirector")
	float Impulse = 0.0f;

	/** The highest importance of everything merged in, so a merge can never demote an important impact. */
	UPROPERTY(BlueprintReadWrite, Category = "DebrisDirector")
	float Importance = 0.0f;

	/** Resolved profile key. */
	UPROPERTY(BlueprintReadWrite, Category = "DebrisDirector")
	FName SurfaceType = NAME_None;

	/** Physical surface of the first request that landed in this piece. */
	UPROPERTY(BlueprintReadWrite, Category = "DebrisDirector")
	TEnumAsByte<EPhysicalSurface> PhysicalSurface = SurfaceType_Default;

	/** How many requests this one piece stands for. Never zero. */
	UPROPERTY(BlueprintReadWrite, Category = "DebrisDirector")
	int32 MergedCount = 1;

	/** Size multiplier that follows from MergedCount - the cube root of it, so volume tracks the count. */
	UPROPERTY(BlueprintReadWrite, Category = "DebrisDirector")
	float ScaleMultiplier = 1.0f;

	/** Index of the request that created this entry, so a caller can recover whatever it hung off it. */
	UPROPERTY(BlueprintReadWrite, Category = "DebrisDirector")
	int32 SourceRequestIndex = INDEX_NONE;

	/** At least one of the merged requests asked for a decal. */
	UPROPERTY(BlueprintReadWrite, Category = "DebrisDirector")
	bool bWantsDecal = true;
};

/**
 * What one frame of planning is allowed to do.
 *
 * The comment that matters is on SpawnBudget.
 */
USTRUCT(BlueprintType)
struct DEBRISDIRECTOR_API FDebrisPlanRules
{
	GENERATED_BODY()

	/**
	 * The hard ceiling on pieces created in one frame.
	 *
	 * This is the number the whole plugin turns on, so here is the arithmetic it exists for. A grenade in a
	 * corridor typically reports around 40 impacts inside a single frame - one per trace that found a wall.
	 * The budget is 24. Without merging, 16 of those impacts are simply not there: the player sees a hole in
	 * the pattern on the side of the blast the code happened to reach last, which is worse than seeing fewer
	 * pieces, because it is asymmetric. With merging, none of the 40 is missing. Requests that land in the
	 * same grid cell become one piece that is bigger by the cube root of how many they were, and the frame
	 * still creates 24 objects. That is the difference between a budget and a cut-off.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Plan", meta = (ClampMin = "0", UIMax = "256"))
	int32 SpawnBudget = 24;

	/**
	 * Edge length of the merge grid, in centimetres. Requests in the same cell become one piece.
	 *
	 * Roughly "how far apart do two hits have to be before a player would count them separately". Too small
	 * and a shotgun pattern stays 9 pieces; too large and a whole wall becomes one boulder.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Plan",
		meta = (ClampMin = "1.0", UIMax = "1000.0", ForceUnits = "cm"))
	float MergeCellSize = 120.0f;

	/** How large a merged piece may get relative to a single one. Stops one bad frame producing a boulder. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Plan", meta = (ClampMin = "1.0", UIMax = "8.0"))
	float MaxScaleMultiplier = 3.0f;

	/**
	 * Merge at all.
	 *
	 * Off is not a saving, it is the comparison: with merging off, everything above the budget is dropped and
	 * counted as dropped, which is what a hand-written spawner does. The demo map has this on a button for
	 * exactly that reason - the same grenade, twice, with the counter box in shot.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Plan")
	bool bMergeEnabled = true;

	/** Requests weaker than this are dropped before anything else happens. Zero accepts everything. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Plan", meta = (ClampMin = "0.0"))
	float MinImpulse = 0.0f;
};

/** What one call to PlanBurst actually did. Every number here ends up on the counter box. */
USTRUCT(BlueprintType)
struct DEBRISDIRECTOR_API FDebrisPlanReport
{
	GENERATED_BODY()

	/** Requests that went in. */
	UPROPERTY(BlueprintReadOnly, Category = "DebrisDirector")
	int32 RequestCount = 0;

	/** Pieces that came out. Never more than the budget. */
	UPROPERTY(BlueprintReadOnly, Category = "DebrisDirector")
	int32 SpawnCount = 0;

	/** Requests that were folded into another piece rather than becoming one of their own. */
	UPROPERTY(BlueprintReadOnly, Category = "DebrisDirector")
	int32 MergedAway = 0;

	/** Requests lost - below MinImpulse, or above the budget with merging switched off. */
	UPROPERTY(BlueprintReadOnly, Category = "DebrisDirector")
	int32 Dropped = 0;

	/** The plan stopped on the budget rather than on running out of requests. */
	UPROPERTY(BlueprintReadOnly, Category = "DebrisDirector")
	bool bBudgetSaturated = false;
};

/**
 * One live piece, as the eviction ranking sees it. Deliberately free of pointers and of any world.
 *
 * The ranking is the second half of the product and it is the half that is easy to get wrong in a way nobody
 * notices for a month, so it is a pure function over plain numbers that a test can drive.
 */
USTRUCT(BlueprintType)
struct DEBRISDIRECTOR_API FDebrisEvictionCandidate
{
	GENERATED_BODY()

	/** How much this piece mattered when it was created, 0..1. Low is evicted first. */
	UPROPERTY(BlueprintReadWrite, Category = "DebrisDirector", meta = (UIMin = "0.0", UIMax = "1.0"))
	float Importance = 0.5f;

	/** Distance to the nearest viewer, in centimetres. Far is evicted first. */
	UPROPERTY(BlueprintReadWrite, Category = "DebrisDirector", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float DistanceToViewer = 0.0f;

	/** How long it has existed, in seconds. Old is evicted first, but only as a tie-breaker. */
	UPROPERTY(BlueprintReadWrite, Category = "DebrisDirector", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float AgeSeconds = 0.0f;

	/** Inside the player's view cone. A piece the player is looking at is the last thing to be taken away. */
	UPROPERTY(BlueprintReadWrite, Category = "DebrisDirector")
	bool bVisible = false;

	/** Still simulating. Ties are broken towards evicting the ones that still cost physics time. */
	UPROPERTY(BlueprintReadWrite, Category = "DebrisDirector")
	bool bSimulating = false;
};

/**
 * The weights behind the eviction order.
 *
 * The order the design asks for is importance, then distance, then age, and these defaults produce exactly
 * that: the importance term spans a thousand points, the distance term a hundred, the age term a handful. A
 * far-away piece therefore never outranks an unimportant one - distance only decides between pieces that are
 * equally important. They are settable because a top-down game and a shooter disagree about how much
 * "far away" is worth, not because the order is up for debate.
 */
USTRUCT(BlueprintType)
struct DEBRISDIRECTOR_API FDebrisEvictionWeights
{
	GENERATED_BODY()

	/** Points added for being unimportant. The dominant term. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eviction", meta = (ClampMin = "0.0", UIMax = "10000.0"))
	float ImportanceWeight = 1000.0f;

	/** Points added for being far away, at most DistanceWeight * DistanceClamp. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eviction", meta = (ClampMin = "0.0", UIMax = "1000.0"))
	float DistanceWeight = 100.0f;

	/** The distance, in centimetres, that counts as one full point of "far". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eviction",
		meta = (ClampMin = "1.0", UIMax = "20000.0", ForceUnits = "cm"))
	float DistanceReference = 2000.0f;

	/** How many DistanceReference lengths still count. Beyond this, further away is not more evictable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eviction", meta = (ClampMin = "1.0", UIMax = "16.0"))
	float DistanceClamp = 4.0f;

	/** Points added per second of age. The tie-breaker, and deliberately small. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eviction", meta = (ClampMin = "0.0", UIMax = "100.0"))
	float AgeWeight = 1.0f;

	/**
	 * Points removed for being on screen.
	 *
	 * Smaller than ImportanceWeight on purpose. Being looked at is a strong argument, not an absolute one:
	 * a worthless chip in the middle of the screen should still go before an important piece behind the
	 * player, or a player standing still in a firefight would fill the ceiling with rubbish they happen to
	 * be facing and then have nothing left for anything that mattered.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eviction", meta = (ClampMin = "0.0", UIMax = "10000.0"))
	float VisibleBonus = 250.0f;

	/** Points added for still simulating physics, so the expensive ones leave first among equals. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eviction", meta = (ClampMin = "0.0", UIMax = "1000.0"))
	float SimulatingWeight = 10.0f;
};

/** What AdvancePiece did on one step. One event per call, in the order they can actually happen. */
UENUM(BlueprintType)
enum class EDebrisLifetimeEvent : uint8
{
	/** Nothing to do. The overwhelmingly common answer. */
	None			UMETA(DisplayName = "None"),

	/** MaxSimSeconds ran out - the caller must now call SetSimulatePhysics(false) on this piece. */
	FrozePhysics	UMETA(DisplayName = "Froze Physics"),

	/** The piece has started fading out, either from old age or because it was evicted. */
	StartedFade		UMETA(DisplayName = "Started Fade"),

	/** The fade is finished. The piece goes back into its pool. */
	Expired			UMETA(DisplayName = "Expired"),
};

/**
 * Everything about one live piece that is a number rather than an object.
 *
 * Kept apart from the actor for one reason: the four-state machine below - simulating, frozen, fading, gone -
 * is the part that can be wrong, and a state machine that lives inside a world subsystem is a state machine
 * that never gets a unit test. AdvancePiece takes this struct and a delta, and a test can run it for ten
 * simulated seconds in a millisecond.
 */
USTRUCT(BlueprintType)
struct DEBRISDIRECTOR_API FDebrisPieceState
{
	GENERATED_BODY()

	/** Seconds since the piece was created. */
	UPROPERTY(BlueprintReadWrite, Category = "DebrisDirector")
	float AgeSeconds = 0.0f;

	/** Seconds it has actually been simulating physics. */
	UPROPERTY(BlueprintReadWrite, Category = "DebrisDirector")
	float SimSeconds = 0.0f;

	/** How long it may exist before it fades out on its own. */
	UPROPERTY(BlueprintReadWrite, Category = "DebrisDirector")
	float LifetimeSeconds = 20.0f;

	/**
	 * How long it may simulate.
	 *
	 * This is the one that keeps the frame flat. Engine sleep thresholds stop a body that has come to rest;
	 * they do nothing for the piece balanced on a slope, jittering against a corner, or resting on another
	 * piece that is itself still moving. Those never sleep, and a hundred of them is a measurable slice of
	 * the physics tick forever. An expiry date has no such failure mode: after MaxSimSeconds the body is
	 * frozen whether it agrees or not, and a frozen body is free.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "DebrisDirector")
	float MaxSimSeconds = 3.0f;

	/** How long the fade-out takes. */
	UPROPERTY(BlueprintReadWrite, Category = "DebrisDirector")
	float FadeSeconds = 0.35f;

	/** How much of the fade is done. */
	UPROPERTY(BlueprintReadWrite, Category = "DebrisDirector")
	float FadeElapsed = 0.0f;

	/** Importance, carried over from the request or the profile. Read by the eviction ranking. */
	UPROPERTY(BlueprintReadWrite, Category = "DebrisDirector")
	float Importance = 0.5f;

	/** The body is simulating right now. */
	UPROPERTY(BlueprintReadWrite, Category = "DebrisDirector")
	bool bSimulating = true;

	/** The piece is on its way out and no longer counts towards the population ceiling. */
	UPROPERTY(BlueprintReadWrite, Category = "DebrisDirector")
	bool bFading = false;

	/** How far the fade has got, 0 solid, 1 gone. This is what the material parameter is set to. */
	float GetFadeAlpha() const
	{
		return (FadeSeconds > KINDA_SMALL_NUMBER) ? FMath::Clamp(FadeElapsed / FadeSeconds, 0.0f, 1.0f) : 1.0f;
	}
};

/**
 * Everything the counter box draws, read once per frame from the running system.
 *
 * Nothing here is smoothed or estimated except EvictionsPerSecond, which says so in its name. A budget
 * plugin whose numbers cannot be checked against what is on screen is a budget plugin nobody has to believe.
 */
USTRUCT(BlueprintType)
struct DEBRISDIRECTOR_API FDebrisDirectorStats
{
	GENERATED_BODY()

	/** Pieces created on the last serviced frame. */
	UPROPERTY(BlueprintReadOnly, Category = "DebrisDirector|Stats")
	int32 SpawnedThisFrame = 0;

	/** The per-frame ceiling those spawns were measured against. */
	UPROPERTY(BlueprintReadOnly, Category = "DebrisDirector|Stats")
	int32 SpawnBudget = 0;

	/** Impact requests that arrived on the last serviced frame. */
	UPROPERTY(BlueprintReadOnly, Category = "DebrisDirector|Stats")
	int32 RequestsThisFrame = 0;

	/** Of those, how many were folded into another piece instead of becoming one. */
	UPROPERTY(BlueprintReadOnly, Category = "DebrisDirector|Stats")
	int32 MergedThisFrame = 0;

	/** Of those, how many were lost. Non-zero means merging is off or MinImpulse rejected them. */
	UPROPERTY(BlueprintReadOnly, Category = "DebrisDirector|Stats")
	int32 DroppedThisFrame = 0;

	/** Pieces alive and counted against the ceiling. Fading ones are not in here. */
	UPROPERTY(BlueprintReadOnly, Category = "DebrisDirector|Stats")
	int32 Population = 0;

	/** The world-wide ceiling. */
	UPROPERTY(BlueprintReadOnly, Category = "DebrisDirector|Stats")
	int32 PopulationCap = 0;

	/** Pieces on their way out. Already off the ceiling, still on screen for another fraction of a second. */
	UPROPERTY(BlueprintReadOnly, Category = "DebrisDirector|Stats")
	int32 FadingCount = 0;

	/** Bodies still costing physics time. */
	UPROPERTY(BlueprintReadOnly, Category = "DebrisDirector|Stats")
	int32 SimulatingCount = 0;

	/** Bodies frozen by the expiry date, which cost nothing. In a settled scene this is nearly all of them. */
	UPROPERTY(BlueprintReadOnly, Category = "DebrisDirector|Stats")
	int32 SleepingCount = 0;

	/** Pieces evicted on the last serviced frame. */
	UPROPERTY(BlueprintReadOnly, Category = "DebrisDirector|Stats")
	int32 EvictedThisFrame = 0;

	/** Evictions per second, over a one second window. The number that shows a ceiling doing its work. */
	UPROPERTY(BlueprintReadOnly, Category = "DebrisDirector|Stats")
	float EvictionsPerSecond = 0.0f;

	/** Pieces created since the world started. */
	UPROPERTY(BlueprintReadOnly, Category = "DebrisDirector|Stats")
	int32 TotalSpawned = 0;

	/** Requests merged away since the world started. */
	UPROPERTY(BlueprintReadOnly, Category = "DebrisDirector|Stats")
	int32 TotalMerged = 0;

	/** Requests dropped since the world started. */
	UPROPERTY(BlueprintReadOnly, Category = "DebrisDirector|Stats")
	int32 TotalDropped = 0;

	/** Pieces evicted since the world started. */
	UPROPERTY(BlueprintReadOnly, Category = "DebrisDirector|Stats")
	int32 TotalEvicted = 0;

	/** Spawns served out of a pool. */
	UPROPERTY(BlueprintReadOnly, Category = "DebrisDirector|Stats")
	int32 PoolHits = 0;

	/**
	 * Spawns that had to build a new actor.
	 *
	 * With a population ceiling this stops moving once the world has filled up once, which is the point:
	 * a scene that has already been shot at costs nothing to shoot at again.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "DebrisDirector|Stats")
	int32 NewAllocations = 0;

	/** Pieces parked in pools, hidden and unticked. */
	UPROPERTY(BlueprintReadOnly, Category = "DebrisDirector|Stats")
	int32 PooledPieces = 0;

	/** Decals alive, and the ceiling they are measured against. */
	UPROPERTY(BlueprintReadOnly, Category = "DebrisDirector|Stats")
	int32 DecalCount = 0;

	/** The decal ceiling. */
	UPROPERTY(BlueprintReadOnly, Category = "DebrisDirector|Stats")
	int32 DecalCap = 0;

	/** The whole director tick - plan, evict, spawn, age - in milliseconds. */
	UPROPERTY(BlueprintReadOnly, Category = "DebrisDirector|Stats")
	float TickMilliseconds = 0.0f;

	/** Merging is on. */
	UPROPERTY(BlueprintReadOnly, Category = "DebrisDirector|Stats")
	bool bMergeEnabled = true;

	/** The physics expiry date is on. */
	UPROPERTY(BlueprintReadOnly, Category = "DebrisDirector|Stats")
	bool bPhysicsSleepEnabled = true;

	/** The last serviced frame stopped on the budget. */
	UPROPERTY(BlueprintReadOnly, Category = "DebrisDirector|Stats")
	bool bBudgetSaturated = false;

	/** The director is running at all. */
	UPROPERTY(BlueprintReadOnly, Category = "DebrisDirector|Stats")
	bool bEnabled = true;
};
