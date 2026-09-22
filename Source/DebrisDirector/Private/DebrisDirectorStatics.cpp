// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "DebrisDirectorStatics.h"

#include "DebrisDirectorSubsystem.h"

namespace DebrisDirectorPrivate
{
	/**
	 * What makes two impacts "the same place".
	 *
	 * The surface is part of the key and not just the cell, because a bullet that clips a metal pipe bolted
	 * to a stone wall reports two impacts a few centimetres apart, and merging those into one piece would
	 * have to pick a material - and would pick the wrong one half the time.
	 */
	struct FMergeKey
	{
		FIntVector Cell = FIntVector::ZeroValue;
		FName SurfaceType = NAME_None;
		uint8 PhysicalSurface = 0;

		bool operator==(const FMergeKey& Other) const
		{
			return Cell == Other.Cell
				&& SurfaceType == Other.SurfaceType
				&& PhysicalSurface == Other.PhysicalSurface;
		}
	};

	/**
	 * Hash for the merge key.
	 *
	 * The two composite members are hashed through unqualified calls so argument-dependent lookup finds
	 * their own overloads - FIntVector's lives with the maths types and FName's is a hidden friend, so
	 * neither is reachable from the global namespace. The plain byte is qualified for the opposite reason:
	 * a fundamental type brings no associated namespace with it, so unqualified lookup would find only the
	 * function being declared here and stop.
	 */
	FORCEINLINE uint32 GetTypeHash(const FMergeKey& Key)
	{
		uint32 Hash = GetTypeHash(Key.Cell);
		Hash = HashCombine(Hash, GetTypeHash(Key.SurfaceType));
		Hash = HashCombine(Hash, ::GetTypeHash(Key.PhysicalSurface));
		return Hash;
	}

	/** One bucket of requests being folded into a single piece. */
	struct FMergeAccumulator
	{
		FVector WeightedLocation = FVector::ZeroVector;
		FVector PlainLocationSum = FVector::ZeroVector;
		FVector NormalSum = FVector::ZeroVector;
		double WeightSum = 0.0;
		float MaxImpulse = 0.0f;
		float MaxImportance = 0.0f;
		int32 Count = 0;
		int32 FirstRequestIndex = INDEX_NONE;
		FName SurfaceType = NAME_None;
		TEnumAsByte<EPhysicalSurface> PhysicalSurface = SurfaceType_Default;
		bool bWantsDecal = false;
	};

	/** A normal that is safe to launch a piece along, whatever the caller passed in. */
	static FVector SafeNormal(const FVector& In)
	{
		const FVector Normalised = In.GetSafeNormal();
		return Normalised.IsNearlyZero() ? FVector::UpVector : Normalised;
	}
}

//~ Planning -----------------------------------------------------------------------------------------------

FIntVector UDebrisDirectorStatics::MergeCellForLocation(const FVector& Location, float CellSize)
{
	const double Size = FMath::Max(1.0f, CellSize);
	return FIntVector(
		FMath::FloorToInt32(Location.X / Size),
		FMath::FloorToInt32(Location.Y / Size),
		FMath::FloorToInt32(Location.Z / Size));
}

float UDebrisDirectorStatics::MergedScaleMultiplier(int32 MergedCount, float MaxScaleMultiplier)
{
	const int32 Count = FMath::Max(1, MergedCount);
	const float Ceiling = FMath::Max(1.0f, MaxScaleMultiplier);

	// Cube root, because a piece is a volume. Doubling the linear size of a merged chunk already makes it
	// eight times the material; scaling linearly with the count would turn a grenade into a boulder.
	const float Cube = FMath::Pow(static_cast<float>(Count), 1.0f / 3.0f);
	return FMath::Clamp(Cube, 1.0f, Ceiling);
}

void UDebrisDirectorStatics::PlanBurst(const TArray<FDebrisRequest>& Requests, const FDebrisPlanRules& Rules,
	TArray<FDebrisPlannedSpawn>& OutPlanned, FDebrisPlanReport& OutReport)
{
	using namespace DebrisDirectorPrivate;

	OutPlanned.Reset();
	OutReport = FDebrisPlanReport();
	OutReport.RequestCount = Requests.Num();

	const int32 Budget = FMath::Max(0, Rules.SpawnBudget);
	if (Requests.Num() == 0)
	{
		return;
	}

	if (Budget == 0)
	{
		// A budget of zero is a legitimate way to switch debris off for a scene, a platform or a bug hunt.
		// The requests are counted as dropped rather than silently ignored, so the counter box shows what
		// the scene was asking for while nothing at all is being created.
		OutReport.Dropped = Requests.Num();
		OutReport.bBudgetSaturated = true;
		return;
	}

	// Below MinImpulse never becomes anything, merged or not. Counted, so a MinImpulse set too high shows up
	// as a dropped count rather than as debris that mysteriously stopped appearing.
	TArray<int32> Accepted;
	Accepted.Reserve(Requests.Num());
	for (int32 Index = 0; Index < Requests.Num(); ++Index)
	{
		if (Requests[Index].Impulse >= Rules.MinImpulse)
		{
			Accepted.Add(Index);
		}
		else
		{
			++OutReport.Dropped;
		}
	}

	if (Accepted.Num() == 0)
	{
		return;
	}

	if (!Rules.bMergeEnabled)
	{
		// The comparison path: first come, first served, and everything above the budget is gone. This is
		// what a hand-written spawner does, and what the demo map's MERGE OFF button shows.
		const int32 Take = FMath::Min(Budget, Accepted.Num());
		OutPlanned.Reserve(Take);

		for (int32 Slot = 0; Slot < Take; ++Slot)
		{
			const int32 RequestIndex = Accepted[Slot];
			const FDebrisRequest& Request = Requests[RequestIndex];

			FDebrisPlannedSpawn& Planned = OutPlanned.AddDefaulted_GetRef();
			Planned.Location = Request.Location;
			Planned.Normal = SafeNormal(Request.Normal);
			Planned.Impulse = Request.Impulse;
			Planned.Importance = Request.Importance;
			Planned.SurfaceType = Request.SurfaceType;
			Planned.PhysicalSurface = Request.PhysicalSurface;
			Planned.MergedCount = 1;
			Planned.ScaleMultiplier = 1.0f;
			Planned.SourceRequestIndex = RequestIndex;
			Planned.bWantsDecal = Request.bWantsDecal;
		}

		OutReport.Dropped += Accepted.Num() - Take;
		OutReport.SpawnCount = OutPlanned.Num();
		OutReport.bBudgetSaturated = Accepted.Num() > Budget;
		return;
	}

	//~ Pass one: fold everything that happened in the same cell on the same surface.

	TMap<FMergeKey, int32> BucketByKey;
	TArray<FMergeAccumulator> Buckets;
	BucketByKey.Reserve(Accepted.Num());
	Buckets.Reserve(Accepted.Num());

	for (const int32 RequestIndex : Accepted)
	{
		const FDebrisRequest& Request = Requests[RequestIndex];

		FMergeKey Key;
		Key.Cell = MergeCellForLocation(Request.Location, Rules.MergeCellSize);
		Key.SurfaceType = Request.SurfaceType;
		Key.PhysicalSurface = static_cast<uint8>(Request.PhysicalSurface.GetValue());

		int32* Existing = BucketByKey.Find(Key);
		if (!Existing)
		{
			const int32 NewIndex = Buckets.AddDefaulted();
			FMergeAccumulator& Fresh = Buckets[NewIndex];
			Fresh.FirstRequestIndex = RequestIndex;
			Fresh.SurfaceType = Request.SurfaceType;
			Fresh.PhysicalSurface = Request.PhysicalSurface;
			Existing = &BucketByKey.Add(Key, NewIndex);
		}

		FMergeAccumulator& Bucket = Buckets[*Existing];

		// The position is weighted by impulse, so a merged piece sits where the hardest hit landed rather
		// than at the geometric middle of a spray. The unweighted sum is kept as well, for the case where
		// every impulse in the bucket is zero and the weighted average would divide by nothing.
		const double Weight = FMath::Max(0.0, static_cast<double>(Request.Impulse));
		Bucket.WeightedLocation += Request.Location * Weight;
		Bucket.PlainLocationSum += Request.Location;
		Bucket.WeightSum += Weight;
		Bucket.NormalSum += SafeNormal(Request.Normal);
		Bucket.MaxImpulse = FMath::Max(Bucket.MaxImpulse, Request.Impulse);
		Bucket.MaxImportance = FMath::Max(Bucket.MaxImportance, Request.Importance);
		Bucket.bWantsDecal |= Request.bWantsDecal;
		++Bucket.Count;
	}

	OutPlanned.Reserve(Buckets.Num());
	for (const FMergeAccumulator& Bucket : Buckets)
	{
		FDebrisPlannedSpawn& Planned = OutPlanned.AddDefaulted_GetRef();
		Planned.Location = (Bucket.WeightSum > 0.0)
			? (Bucket.WeightedLocation / Bucket.WeightSum)
			: (Bucket.PlainLocationSum / FMath::Max(1, Bucket.Count));
		Planned.Normal = SafeNormal(Bucket.NormalSum);
		Planned.Impulse = Bucket.MaxImpulse;
		Planned.Importance = Bucket.MaxImportance;
		Planned.SurfaceType = Bucket.SurfaceType;
		Planned.PhysicalSurface = Bucket.PhysicalSurface;
		Planned.MergedCount = Bucket.Count;
		Planned.SourceRequestIndex = Bucket.FirstRequestIndex;
		Planned.bWantsDecal = Bucket.bWantsDecal;
	}

	OutReport.bBudgetSaturated = OutPlanned.Num() > Budget;

	//~ Pass two: still over budget. Nothing is thrown away - the losers join the nearest winner.

	if (OutPlanned.Num() > Budget)
	{
		// Most important first, then hardest hit, then the one that already stands for the most impacts. The
		// sort is stable, so requests that tie stay in the order they arrived and a burst does not reshuffle
		// itself between frames.
		OutPlanned.StableSort([](const FDebrisPlannedSpawn& A, const FDebrisPlannedSpawn& B)
		{
			if (!FMath::IsNearlyEqual(A.Importance, B.Importance))
			{
				return A.Importance > B.Importance;
			}
			if (!FMath::IsNearlyEqual(A.Impulse, B.Impulse))
			{
				return A.Impulse > B.Impulse;
			}
			return A.MergedCount > B.MergedCount;
		});

		for (int32 LoserIndex = Budget; LoserIndex < OutPlanned.Num(); ++LoserIndex)
		{
			const FDebrisPlannedSpawn& Loser = OutPlanned[LoserIndex];

			int32 BestSurvivor = 0;
			double BestCost = TNumericLimits<double>::Max();

			for (int32 SurvivorIndex = 0; SurvivorIndex < Budget; ++SurvivorIndex)
			{
				const FDebrisPlannedSpawn& Survivor = OutPlanned[SurvivorIndex];

				// Distance decides, but a different surface is expensive: joining a stone chip to a metal
				// shell would give the merged piece one material for two surfaces, and the player would see
				// the wrong one. Sixteen is "only if there is nothing of your own kind anywhere near".
				double Cost = FVector::DistSquared(Loser.Location, Survivor.Location);
				if (Survivor.SurfaceType != Loser.SurfaceType || Survivor.PhysicalSurface != Loser.PhysicalSurface)
				{
					Cost *= 16.0;
				}

				if (Cost < BestCost)
				{
					BestCost = Cost;
					BestSurvivor = SurvivorIndex;
				}
			}

			FDebrisPlannedSpawn& Survivor = OutPlanned[BestSurvivor];

			// The survivor keeps its position. It stands for more impacts now, and it gets bigger for it,
			// but it does not drift towards whatever it absorbed - a piece that moved after the fact would
			// no longer be where the player saw the impact land.
			Survivor.MergedCount += Loser.MergedCount;
			Survivor.Impulse = FMath::Max(Survivor.Impulse, Loser.Impulse);
			Survivor.Importance = FMath::Max(Survivor.Importance, Loser.Importance);
			Survivor.bWantsDecal |= Loser.bWantsDecal;
		}

		OutPlanned.SetNum(Budget, EAllowShrinking::No);
	}

	for (FDebrisPlannedSpawn& Planned : OutPlanned)
	{
		Planned.ScaleMultiplier = MergedScaleMultiplier(Planned.MergedCount, Rules.MaxScaleMultiplier);
	}

	OutReport.SpawnCount = OutPlanned.Num();
	OutReport.MergedAway = Accepted.Num() - OutPlanned.Num();
}

//~ Eviction -----------------------------------------------------------------------------------------------

float UDebrisDirectorStatics::ScoreForEviction(const FDebrisEvictionCandidate& Candidate, const FDebrisEvictionWeights& Weights)
{
	float Score = 0.0f;

	// Unimportant first. This term is an order of magnitude larger than the others, which is what makes the
	// ordering "importance, then distance, then age" rather than "some blend of the three".
	Score += (1.0f - FMath::Clamp(Candidate.Importance, 0.0f, 1.0f)) * Weights.ImportanceWeight;

	// Far away next, and it stops counting past DistanceClamp reference lengths - beyond a certain distance
	// everything is equally out of sight, and letting the term keep growing would let a very distant but
	// important piece lose to a nearby worthless one.
	const float Reference = FMath::Max(1.0f, Weights.DistanceReference);
	const float DistanceUnits = FMath::Clamp(Candidate.DistanceToViewer / Reference, 0.0f, FMath::Max(1.0f, Weights.DistanceClamp));
	Score += DistanceUnits * Weights.DistanceWeight;

	// Age is the tie-breaker, not a rule of its own. Evicting oldest-first is what a plain ring buffer does,
	// and it is exactly the behaviour that takes away the piece the player is standing over.
	Score += Candidate.AgeSeconds * Weights.AgeWeight;

	if (Candidate.bVisible)
	{
		Score -= Weights.VisibleBonus;
	}

	if (Candidate.bSimulating)
	{
		Score += Weights.SimulatingWeight;
	}

	return Score;
}

void UDebrisDirectorStatics::RankForEviction(const TArray<FDebrisEvictionCandidate>& Candidates,
	const FDebrisEvictionWeights& Weights, int32 Count, TArray<int32>& OutIndices)
{
	OutIndices.Reset();

	const int32 Take = FMath::Clamp(Count, 0, Candidates.Num());
	if (Take == 0)
	{
		return;
	}

	TArray<float> Scores;
	Scores.SetNumUninitialized(Candidates.Num());

	TArray<int32> Order;
	Order.SetNumUninitialized(Candidates.Num());

	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		Scores[Index] = ScoreForEviction(Candidates[Index], Weights);
		Order[Index] = Index;
	}

	// Stable, so equal scores are evicted in creation order. Without that, a scene sitting exactly on the
	// ceiling would pick a different one of two identical chips every frame and both would flicker.
	Order.StableSort([&Scores](int32 A, int32 B)
	{
		return Scores[A] > Scores[B];
	});

	OutIndices.Reserve(Take);
	for (int32 Slot = 0; Slot < Take; ++Slot)
	{
		OutIndices.Add(Order[Slot]);
	}
}

int32 UDebrisDirectorStatics::PlanEvictionCount(int32 Population, int32 Incoming, int32 PopulationCap)
{
	const int32 Cap = FMath::Max(0, PopulationCap);
	const int32 After = FMath::Max(0, Population) + FMath::Max(0, Incoming);
	return FMath::Max(0, After - Cap);
}

//~ Lifetime -----------------------------------------------------------------------------------------------

EDebrisLifetimeEvent UDebrisDirectorStatics::AdvancePiece(FDebrisPieceState& State, float DeltaSeconds)
{
	const float Delta = FMath::Max(0.0f, DeltaSeconds);
	State.AgeSeconds += Delta;

	// A piece that is already leaving has nothing else it can do. Checked first so that an eviction is never
	// undone by a lifetime rule two lines further down.
	if (State.bFading)
	{
		State.FadeElapsed += Delta;
		return (State.FadeElapsed >= State.FadeSeconds) ? EDebrisLifetimeEvent::Expired : EDebrisLifetimeEvent::None;
	}

	// The expiry date. A negative MaxSimSeconds means "no expiry", which is what a project gets if it wants
	// the engine's own sleep thresholds and nothing else.
	if (State.bSimulating && State.MaxSimSeconds >= 0.0f)
	{
		State.SimSeconds += Delta;
		if (State.SimSeconds >= State.MaxSimSeconds)
		{
			State.bSimulating = false;
			return EDebrisLifetimeEvent::FrozePhysics;
		}
	}

	if (State.AgeSeconds >= State.LifetimeSeconds)
	{
		State.bFading = true;
		State.FadeElapsed = 0.0f;
		return EDebrisLifetimeEvent::StartedFade;
	}

	return EDebrisLifetimeEvent::None;
}

bool UDebrisDirectorStatics::BeginFade(FDebrisPieceState& State)
{
	if (State.bFading)
	{
		return false;
	}

	State.bFading = true;
	State.FadeElapsed = 0.0f;
	return true;
}

//~ World entries ------------------------------------------------------------------------------------------

bool UDebrisDirectorStatics::ReportImpact(const UObject* WorldContextObject, const FDebrisRequest& Request)
{
	UDebrisDirectorSubsystem* Subsystem = UDebrisDirectorSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->RequestBurst(Request) : false;
}

bool UDebrisDirectorStatics::ReportImpactAt(const UObject* WorldContextObject, const FVector& Location, const FVector& Normal,
	float Impulse, FName SurfaceType, float Importance, bool bWantsDecal)
{
	FDebrisRequest Request;
	Request.Location = Location;
	Request.Normal = Normal;
	Request.Impulse = Impulse;
	Request.SurfaceType = SurfaceType;
	Request.Importance = Importance;
	Request.bWantsDecal = bWantsDecal;

	return ReportImpact(WorldContextObject, Request);
}

int32 UDebrisDirectorStatics::ReportImpacts(const UObject* WorldContextObject, const TArray<FDebrisRequest>& Requests)
{
	UDebrisDirectorSubsystem* Subsystem = UDebrisDirectorSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->RequestBurstMany(Requests) : 0;
}

int32 UDebrisDirectorStatics::RequestStress(const UObject* WorldContextObject, int32 Count, float Radius)
{
	UDebrisDirectorSubsystem* Subsystem = UDebrisDirectorSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->RequestStress(Count, Radius) : 0;
}

void UDebrisDirectorStatics::Clear(const UObject* WorldContextObject)
{
	if (UDebrisDirectorSubsystem* Subsystem = UDebrisDirectorSubsystem::Get(WorldContextObject))
	{
		Subsystem->Clear();
	}
}

void UDebrisDirectorStatics::SetBudget(const UObject* WorldContextObject, int32 SpawnBudget)
{
	if (UDebrisDirectorSubsystem* Subsystem = UDebrisDirectorSubsystem::Get(WorldContextObject))
	{
		Subsystem->SetBudget(SpawnBudget);
	}
}

int32 UDebrisDirectorStatics::GetBudget(const UObject* WorldContextObject)
{
	const UDebrisDirectorSubsystem* Subsystem = UDebrisDirectorSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetBudget() : 0;
}

void UDebrisDirectorStatics::SetPopulationCap(const UObject* WorldContextObject, int32 PopulationCap)
{
	if (UDebrisDirectorSubsystem* Subsystem = UDebrisDirectorSubsystem::Get(WorldContextObject))
	{
		Subsystem->SetPopulationCap(PopulationCap);
	}
}

int32 UDebrisDirectorStatics::GetPopulationCap(const UObject* WorldContextObject)
{
	const UDebrisDirectorSubsystem* Subsystem = UDebrisDirectorSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetPopulationCap() : 0;
}

void UDebrisDirectorStatics::SetMergeEnabled(const UObject* WorldContextObject, bool bEnabled)
{
	if (UDebrisDirectorSubsystem* Subsystem = UDebrisDirectorSubsystem::Get(WorldContextObject))
	{
		Subsystem->SetMergeEnabled(bEnabled);
	}
}

bool UDebrisDirectorStatics::IsMergeEnabled(const UObject* WorldContextObject)
{
	const UDebrisDirectorSubsystem* Subsystem = UDebrisDirectorSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->IsMergeEnabled() : false;
}

void UDebrisDirectorStatics::SetPhysicsSleepEnabled(const UObject* WorldContextObject, bool bEnabled)
{
	if (UDebrisDirectorSubsystem* Subsystem = UDebrisDirectorSubsystem::Get(WorldContextObject))
	{
		Subsystem->SetPhysicsSleepEnabled(bEnabled);
	}
}

bool UDebrisDirectorStatics::IsPhysicsSleepEnabled(const UObject* WorldContextObject)
{
	const UDebrisDirectorSubsystem* Subsystem = UDebrisDirectorSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->IsPhysicsSleepEnabled() : false;
}

void UDebrisDirectorStatics::SetShowStats(const UObject* WorldContextObject, bool bShow)
{
	if (UDebrisDirectorSubsystem* Subsystem = UDebrisDirectorSubsystem::Get(WorldContextObject))
	{
		Subsystem->SetShowStats(bShow);
	}
}

bool UDebrisDirectorStatics::AreStatsShown(const UObject* WorldContextObject)
{
	const UDebrisDirectorSubsystem* Subsystem = UDebrisDirectorSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->AreStatsShown() : false;
}

FDebrisDirectorStats UDebrisDirectorStatics::GetStats(const UObject* WorldContextObject)
{
	const UDebrisDirectorSubsystem* Subsystem = UDebrisDirectorSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetStats() : FDebrisDirectorStats();
}

int32 UDebrisDirectorStatics::GetPopulation(const UObject* WorldContextObject)
{
	const UDebrisDirectorSubsystem* Subsystem = UDebrisDirectorSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetPopulation() : 0;
}

int32 UDebrisDirectorStatics::GetSimulatingCount(const UObject* WorldContextObject)
{
	const UDebrisDirectorSubsystem* Subsystem = UDebrisDirectorSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetSimulatingCount() : 0;
}

int32 UDebrisDirectorStatics::GetSleepingCount(const UObject* WorldContextObject)
{
	const UDebrisDirectorSubsystem* Subsystem = UDebrisDirectorSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetSleepingCount() : 0;
}

void UDebrisDirectorStatics::RegisterProfile(const UObject* WorldContextObject, UDebrisProfile* Profile)
{
	if (UDebrisDirectorSubsystem* Subsystem = UDebrisDirectorSubsystem::Get(WorldContextObject))
	{
		Subsystem->RegisterProfile(Profile);
	}
}
