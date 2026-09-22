// Copyright 2026 Silvan Teufel. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "DebrisDirectorStatics.h"
#include "DebrisDirectorTypes.h"

/**
 * Everything under test here is static and world-free, which is exactly why the merge, the eviction order
 * and the piece lifetime live in a Blueprint function library and in plain structs rather than inside the
 * subsystem. A UTickableWorldSubsystem needs a world, cannot be NewObject'd in an automation test, and every
 * rule that lives in one is a rule that ships unverified.
 *
 * These call the same functions the running director calls on every frame. There is no second implementation
 * here that agrees with the first until somebody edits one of the two.
 */
namespace DebrisDirectorTest
{
	static FDebrisRequest MakeRequest(const FVector& Location, float Impulse = 400.0f, float Importance = 0.5f,
		FName SurfaceType = NAME_None)
	{
		FDebrisRequest Request;
		Request.Location = Location;
		Request.Normal = FVector::UpVector;
		Request.Impulse = Impulse;
		Request.Importance = Importance;
		Request.SurfaceType = SurfaceType;
		return Request;
	}

	static int32 SumMergedCount(const TArray<FDebrisPlannedSpawn>& Planned)
	{
		int32 Total = 0;
		for (const FDebrisPlannedSpawn& Entry : Planned)
		{
			Total += Entry.MergedCount;
		}
		return Total;
	}

	static FDebrisEvictionCandidate MakeCandidate(float Importance, float Distance, float Age, bool bVisible = false)
	{
		FDebrisEvictionCandidate Candidate;
		Candidate.Importance = Importance;
		Candidate.DistanceToViewer = Distance;
		Candidate.AgeSeconds = Age;
		Candidate.bVisible = bVisible;
		return Candidate;
	}
}

//~ 1. The budget holds, and impacts in one cell become one piece ------------------------------------------

/**
 * PlanBurst never returns more than the budget, and requests that landed in the same grid cell arrive as a
 * single larger piece rather than as several small ones.
 *
 * The second half is the part that is easy to get wrong in a way nobody notices: a merge that keeps the
 * count but forgets to grow the piece produces a wall that looks under-damaged after a grenade, and a merge
 * that grows the piece per request rather than per cube root produces a boulder.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDebrisDirectorPlanBudgetTest, "DebrisDirector.Plan.BudgetAndMerge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDebrisDirectorPlanBudgetTest::RunTest(const FString& /*Parameters*/)
{
	using namespace DebrisDirectorTest;

	FDebrisPlanRules Rules;
	Rules.SpawnBudget = 24;
	Rules.MergeCellSize = 100.0f;
	Rules.MaxScaleMultiplier = 3.0f;

	// Eight impacts inside one 100 cm cell, and four more each in their own cell far away.
	TArray<FDebrisRequest> Requests;
	for (int32 Index = 0; Index < 8; ++Index)
	{
		Requests.Add(MakeRequest(FVector(10.0f + Index * 5.0f, 10.0f, 10.0f)));
	}
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Requests.Add(MakeRequest(FVector(5000.0f + Index * 1000.0f, 0.0f, 0.0f)));
	}

	TArray<FDebrisPlannedSpawn> Planned;
	FDebrisPlanReport Report;
	UDebrisDirectorStatics::PlanBurst(Requests, Rules, Planned, Report);

	TestEqual(TEXT("Twelve requests in five cells become five pieces"), Planned.Num(), 5);
	TestTrue(TEXT("Never more than the budget"), Planned.Num() <= Rules.SpawnBudget);
	TestEqual(TEXT("Every request is accounted for"), SumMergedCount(Planned), Requests.Num());
	TestEqual(TEXT("Seven requests were folded away"), Report.MergedAway, 7);
	TestEqual(TEXT("Nothing was dropped"), Report.Dropped, 0);

	// The crowded cell has to be the one that grew.
	const FDebrisPlannedSpawn* Crowded = Planned.FindByPredicate([](const FDebrisPlannedSpawn& Entry)
	{
		return Entry.MergedCount == 8;
	});

	if (!Crowded)
	{
		AddError(TEXT("No piece stands for the eight impacts that shared a cell."));
		return false;
	}

	TestTrue(TEXT("The merged piece is larger than a single one"), Crowded->ScaleMultiplier > 1.0f);
	TestTrue(TEXT("And not larger than the ceiling allows"), Crowded->ScaleMultiplier <= Rules.MaxScaleMultiplier + KINDA_SMALL_NUMBER);
	TestTrue(TEXT("It sits inside the cell it came from"),
		FVector::Dist(Crowded->Location, FVector(27.5f, 10.0f, 10.0f)) < 100.0f);

	// Eight impacts is a cube root of two, not a factor of eight. This is the line that stops a grenade
	// turning a wall chip into a boulder.
	TestEqual(TEXT("Eight merged impacts double the size, they do not multiply it by eight"),
		UDebrisDirectorStatics::MergedScaleMultiplier(8, 8.0f), 2.0f, 0.001f);

	return true;
}

//~ 2. Over budget, nothing is lost - it only gets bigger --------------------------------------------------

/**
 * The invariant the whole product rests on: forty impacts against a budget of twenty-four produce
 * twenty-four pieces, and the sum of what those pieces stand for is still forty.
 *
 * Deliberately arranged so the first pass cannot help - every request is in a cell of its own - which forces
 * the fold-into-nearest-neighbour pass that runs when merging alone is not enough.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDebrisDirectorPlanNoLossTest, "DebrisDirector.Plan.OverBudgetLosesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDebrisDirectorPlanNoLossTest::RunTest(const FString& /*Parameters*/)
{
	using namespace DebrisDirectorTest;

	FDebrisPlanRules Rules;
	Rules.SpawnBudget = 24;
	Rules.MergeCellSize = 100.0f;
	Rules.MaxScaleMultiplier = 4.0f;

	// The grenade: forty impacts in one frame, spread far enough apart that each lands in its own cell.
	TArray<FDebrisRequest> Requests;
	for (int32 Index = 0; Index < 40; ++Index)
	{
		const float Angle = (Index / 40.0f) * 2.0f * PI;
		Requests.Add(MakeRequest(FVector(FMath::Cos(Angle) * 1000.0f, FMath::Sin(Angle) * 1000.0f, 0.0f)));
	}

	TArray<FDebrisPlannedSpawn> Planned;
	FDebrisPlanReport Report;
	UDebrisDirectorStatics::PlanBurst(Requests, Rules, Planned, Report);

	TestEqual(TEXT("The frame creates exactly the budget"), Planned.Num(), 24);
	TestEqual(TEXT("All forty impacts are still represented"), SumMergedCount(Planned), 40);
	TestEqual(TEXT("Nothing was dropped"), Report.Dropped, 0);
	TestEqual(TEXT("Sixteen were folded into a neighbour"), Report.MergedAway, 16);
	TestTrue(TEXT("The plan reports that it hit the budget"), Report.bBudgetSaturated);

	int32 Enlarged = 0;
	for (const FDebrisPlannedSpawn& Entry : Planned)
	{
		if (Entry.MergedCount > 1)
		{
			++Enlarged;
			TestTrue(TEXT("A piece standing for several impacts is larger than one"), Entry.ScaleMultiplier > 1.0f);
		}
	}
	TestTrue(TEXT("Some pieces did take the extra impacts on"), Enlarged > 0);

	// And the comparison the demo map shows: with merging off, the same burst loses sixteen impacts outright.
	Rules.bMergeEnabled = false;
	UDebrisDirectorStatics::PlanBurst(Requests, Rules, Planned, Report);

	TestEqual(TEXT("Merging off still holds the budget"), Planned.Num(), 24);
	TestEqual(TEXT("But sixteen impacts are simply gone"), Report.Dropped, 16);
	TestEqual(TEXT("And nothing was merged"), Report.MergedAway, 0);

	return true;
}

//~ 3. The eviction order prefers far and unimportant ------------------------------------------------------

/**
 * RankForEviction takes the unimportant far-away piece before the important near one, and takes a piece
 * behind the player before an equally worthless one in front of them.
 *
 * The third assertion is the one that separates this from a ring buffer: the oldest piece is not the first
 * to go if it is the one the player is standing over.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDebrisDirectorEvictionOrderTest, "DebrisDirector.Evict.PrefersFarAndUnimportant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDebrisDirectorEvictionOrderTest::RunTest(const FString& /*Parameters*/)
{
	using namespace DebrisDirectorTest;

	const FDebrisEvictionWeights Weights;

	TArray<FDebrisEvictionCandidate> Candidates;
	Candidates.Add(MakeCandidate(0.9f, 200.0f, 30.0f, true));		// 0: important, near, old, on screen
	Candidates.Add(MakeCandidate(0.1f, 8000.0f, 1.0f, false));		// 1: worthless, far, new
	Candidates.Add(MakeCandidate(0.1f, 200.0f, 1.0f, false));		// 2: worthless, near
	Candidates.Add(MakeCandidate(0.9f, 8000.0f, 1.0f, false));		// 3: important, far

	TArray<int32> Chosen;
	UDebrisDirectorStatics::RankForEviction(Candidates, Weights, 4, Chosen);

	if (Chosen.Num() != 4)
	{
		AddError(TEXT("RankForEviction did not return every candidate when asked for all of them."));
		return false;
	}

	TestEqual(TEXT("The worthless far piece goes first"), Chosen[0], 1);
	TestEqual(TEXT("Then the worthless near one"), Chosen[1], 2);
	TestEqual(TEXT("Then the important far one"), Chosen[2], 3);
	TestEqual(TEXT("The important piece in front of the player is last, despite being the oldest"), Chosen[3], 0);

	// Importance is the first term and must beat distance outright, not merely on average.
	TestTrue(TEXT("Unimportant and near still outranks important and far"),
		UDebrisDirectorStatics::ScoreForEviction(Candidates[2], Weights)
		> UDebrisDirectorStatics::ScoreForEviction(Candidates[3], Weights));

	// Between equals, being looked at is what saves a piece.
	const FDebrisEvictionCandidate OnScreen = MakeCandidate(0.4f, 1000.0f, 5.0f, true);
	const FDebrisEvictionCandidate Behind = MakeCandidate(0.4f, 1000.0f, 5.0f, false);
	TestTrue(TEXT("A piece behind the player goes before an identical one in front of them"),
		UDebrisDirectorStatics::ScoreForEviction(Behind, Weights) > UDebrisDirectorStatics::ScoreForEviction(OnScreen, Weights));

	// Asking for fewer than there are returns the worst ones and only those.
	UDebrisDirectorStatics::RankForEviction(Candidates, Weights, 2, Chosen);
	TestEqual(TEXT("Asking for two returns two"), Chosen.Num(), 2);
	TestEqual(TEXT("And they are the two worst"), Chosen[0], 1);

	// Asking for more than there are returns everything rather than reading past the end.
	UDebrisDirectorStatics::RankForEviction(Candidates, Weights, 99, Chosen);
	TestEqual(TEXT("Asking for more than exist returns all of them"), Chosen.Num(), 4);

	return true;
}

//~ 4. The physics expiry date fires on time and stays fired ------------------------------------------------

/**
 * A piece stops simulating after exactly MaxSimSeconds and never starts again on its own.
 *
 * Driven one sixtieth of a second at a time, because that is how it runs, and a rule that is only correct
 * for large time steps is a rule that is wrong on a machine that hits frame rate.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDebrisDirectorPhysicsExpiryTest, "DebrisDirector.Lifetime.PhysicsExpiry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDebrisDirectorPhysicsExpiryTest::RunTest(const FString& /*Parameters*/)
{
	FDebrisPieceState State;
	State.MaxSimSeconds = 0.5f;
	State.LifetimeSeconds = 20.0f;
	State.FadeSeconds = 0.25f;
	State.bSimulating = true;

	const float Step = 1.0f / 60.0f;
	int32 FreezeEvents = 0;
	int32 StepsUntilFreeze = -1;

	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		const EDebrisLifetimeEvent Event = UDebrisDirectorStatics::AdvancePiece(State, Step);
		if (Event == EDebrisLifetimeEvent::FrozePhysics)
		{
			++FreezeEvents;
			StepsUntilFreeze = Frame + 1;
		}
	}

	TestEqual(TEXT("The freeze happens exactly once"), FreezeEvents, 1);

	// Half a second at a sixtieth each is thirty steps, give or take one step of float accumulation. The
	// tolerance is one step and not a fraction of a second on purpose: what matters is that a piece is
	// frozen on the frame its time is up, not several frames later.
	TestTrue(TEXT("And on the first frame past MaxSimSeconds"), StepsUntilFreeze >= 30 && StepsUntilFreeze <= 31);
	TestFalse(TEXT("The piece is no longer simulating"), State.bSimulating);
	TestTrue(TEXT("It never simulated longer than it was allowed to"), State.SimSeconds <= State.MaxSimSeconds + Step);

	// The same rule again with a step that is exact in binary, so the frame it fires on is not a matter of
	// rounding at all.
	FDebrisPieceState Exact;
	Exact.MaxSimSeconds = 0.5f;
	Exact.LifetimeSeconds = 20.0f;
	Exact.bSimulating = true;

	int32 ExactStepsUntilFreeze = -1;
	for (int32 Frame = 0; Frame < 16; ++Frame)
	{
		if (UDebrisDirectorStatics::AdvancePiece(Exact, 0.125f) == EDebrisLifetimeEvent::FrozePhysics)
		{
			ExactStepsUntilFreeze = Frame + 1;
			break;
		}
	}
	TestEqual(TEXT("Four eighth-second steps is exactly half a second"), ExactStepsUntilFreeze, 4);

	// A second on from there, still frozen and still not fading - the lifetime is twenty seconds.
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		UDebrisDirectorStatics::AdvancePiece(State, Step);
	}
	TestFalse(TEXT("A frozen piece stays frozen"), State.bSimulating);
	TestFalse(TEXT("And has not started fading yet"), State.bFading);

	// A negative MaxSimSeconds is the documented way to switch the expiry date off. It must never freeze.
	FDebrisPieceState NeverFreezes;
	NeverFreezes.MaxSimSeconds = -1.0f;
	NeverFreezes.LifetimeSeconds = 1000.0f;
	NeverFreezes.bSimulating = true;

	for (int32 Frame = 0; Frame < 600; ++Frame)
	{
		TestTrue(TEXT("With the expiry off, nothing is ever frozen"),
			UDebrisDirectorStatics::AdvancePiece(NeverFreezes, Step) != EDebrisLifetimeEvent::FrozePhysics);
	}
	TestTrue(TEXT("It is still simulating after ten seconds"), NeverFreezes.bSimulating);

	// And the way out: lifetime runs out, the piece fades, and the fade finishes exactly once.
	FDebrisPieceState Short;
	Short.MaxSimSeconds = 0.1f;
	Short.LifetimeSeconds = 0.5f;
	Short.FadeSeconds = 0.2f;
	Short.bSimulating = true;

	int32 FadeStarts = 0;
	int32 Expiries = 0;
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		switch (UDebrisDirectorStatics::AdvancePiece(Short, Step))
		{
		case EDebrisLifetimeEvent::StartedFade:	++FadeStarts; break;
		case EDebrisLifetimeEvent::Expired:		++Expiries; break;
		default: break;
		}

		if (Expiries > 0)
		{
			break;
		}
	}

	TestEqual(TEXT("The fade starts once"), FadeStarts, 1);
	TestEqual(TEXT("And ends once"), Expiries, 1);
	TestTrue(TEXT("A fully faded piece is fully transparent"), FMath::IsNearlyEqual(Short.GetFadeAlpha(), 1.0f, 0.001f));

	// An eviction starts the same fade, and starting it twice is refused rather than restarting it.
	FDebrisPieceState Evicted;
	TestTrue(TEXT("An eviction starts a fade"), UDebrisDirectorStatics::BeginFade(Evicted));
	TestFalse(TEXT("A second eviction of the same piece changes nothing"), UDebrisDirectorStatics::BeginFade(Evicted));

	return true;
}

//~ 5. The ceiling is never exceeded, whatever the frame throws at it ---------------------------------------

/**
 * A thousand impacts in a single frame, a hundred frames in a row, and the population never goes past the
 * ceiling on any of them.
 *
 * This is the arithmetic the running director does, driven directly: plan the frame against the budget, work
 * out how many pieces have to leave to fit what is arriving, and apply both. If the two numbers can ever
 * disagree, they disagree here rather than in somebody's shipped build.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDebrisDirectorPopulationCapTest, "DebrisDirector.Evict.CapIsNeverExceeded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDebrisDirectorPopulationCapTest::RunTest(const FString& /*Parameters*/)
{
	using namespace DebrisDirectorTest;

	FDebrisPlanRules Rules;
	Rules.SpawnBudget = 24;
	Rules.MergeCellSize = 120.0f;

	constexpr int32 Cap = 400;

	TArray<FDebrisRequest> Requests;
	Requests.Reserve(1000);
	for (int32 Index = 0; Index < 1000; ++Index)
	{
		// Spread over a wide area so most of them land in cells of their own, which is the expensive case.
		const float X = static_cast<float>((Index * 137) % 4000);
		const float Y = static_cast<float>((Index * 911) % 4000);
		Requests.Add(MakeRequest(FVector(X, Y, 0.0f), 100.0f + (Index % 7) * 50.0f, (Index % 10) * 0.1f));
	}

	int32 Population = 0;
	int32 TotalRepresented = 0;

	for (int32 Frame = 0; Frame < 100; ++Frame)
	{
		TArray<FDebrisPlannedSpawn> Planned;
		FDebrisPlanReport Report;
		UDebrisDirectorStatics::PlanBurst(Requests, Rules, Planned, Report);

		TestTrue(TEXT("A frame never plans more than the budget"), Planned.Num() <= Rules.SpawnBudget);
		TestEqual(TEXT("A thousand requests are still a thousand impacts"), SumMergedCount(Planned), 1000);

		const int32 Evictions = UDebrisDirectorStatics::PlanEvictionCount(Population, Planned.Num(), Cap);
		Population = Population - Evictions + Planned.Num();
		TotalRepresented += SumMergedCount(Planned);

		if (Population > Cap)
		{
			AddError(FString::Printf(TEXT("Population %d went past the ceiling of %d on frame %d."), Population, Cap, Frame));
			return false;
		}
	}

	TestTrue(TEXT("The scene did fill up"), Population == Cap);
	TestEqual(TEXT("Every frame's impacts were represented"), TotalRepresented, 100 * 1000);

	// The ceiling arithmetic on its own, including the cases a spawn loop gets wrong.
	TestEqual(TEXT("Room to spare evicts nothing"), UDebrisDirectorStatics::PlanEvictionCount(10, 5, 400), 0);
	TestEqual(TEXT("Exactly full evicts nothing"), UDebrisDirectorStatics::PlanEvictionCount(395, 5, 400), 0);
	TestEqual(TEXT("One over evicts one"), UDebrisDirectorStatics::PlanEvictionCount(396, 5, 400), 1);
	TestEqual(TEXT("A ceiling of zero evicts everything"), UDebrisDirectorStatics::PlanEvictionCount(10, 5, 0), 15);
	TestEqual(TEXT("Negative input is treated as none, not as a negative eviction"),
		UDebrisDirectorStatics::PlanEvictionCount(-5, -5, 400), 0);

	// And a budget of zero: nothing is created, and it says so rather than pretending.
	Rules.SpawnBudget = 0;
	TArray<FDebrisPlannedSpawn> Nothing;
	FDebrisPlanReport ZeroReport;
	UDebrisDirectorStatics::PlanBurst(Requests, Rules, Nothing, ZeroReport);

	TestEqual(TEXT("A budget of zero creates nothing"), Nothing.Num(), 0);
	TestEqual(TEXT("And reports every request as dropped"), ZeroReport.Dropped, 1000);

	return true;
}

//~ 6. MinImpulse and empty input ---------------------------------------------------------------------------

/** The two boundary cases a planner meets in a real level long before it meets a grenade. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDebrisDirectorPlanEdgesTest, "DebrisDirector.Plan.Edges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDebrisDirectorPlanEdgesTest::RunTest(const FString& /*Parameters*/)
{
	using namespace DebrisDirectorTest;

	FDebrisPlanRules Rules;
	Rules.SpawnBudget = 24;
	Rules.MinImpulse = 100.0f;

	TArray<FDebrisPlannedSpawn> Planned;
	FDebrisPlanReport Report;

	UDebrisDirectorStatics::PlanBurst(TArray<FDebrisRequest>(), Rules, Planned, Report);
	TestEqual(TEXT("No requests plans nothing"), Planned.Num(), 0);
	TestEqual(TEXT("And drops nothing"), Report.Dropped, 0);
	TestFalse(TEXT("And is not saturated"), Report.bBudgetSaturated);

	TArray<FDebrisRequest> Requests;
	Requests.Add(MakeRequest(FVector(0.0f, 0.0f, 0.0f), 500.0f));
	Requests.Add(MakeRequest(FVector(1000.0f, 0.0f, 0.0f), 10.0f));		// too weak
	Requests.Add(MakeRequest(FVector(2000.0f, 0.0f, 0.0f), 100.0f));		// exactly on the threshold

	UDebrisDirectorStatics::PlanBurst(Requests, Rules, Planned, Report);
	TestEqual(TEXT("Two of three survive MinImpulse"), Planned.Num(), 2);
	TestEqual(TEXT("The weak one is counted as dropped"), Report.Dropped, 1);

	// Different surfaces in the same cell are never merged - a merged piece can only have one material.
	Rules.MinImpulse = 0.0f;
	TArray<FDebrisRequest> Mixed;
	Mixed.Add(MakeRequest(FVector::ZeroVector, 400.0f, 0.5f, TEXT("Stone")));
	Mixed.Add(MakeRequest(FVector(5.0f, 0.0f, 0.0f), 400.0f, 0.5f, TEXT("Metal")));
	Mixed.Add(MakeRequest(FVector(10.0f, 0.0f, 0.0f), 400.0f, 0.5f, TEXT("Stone")));

	UDebrisDirectorStatics::PlanBurst(Mixed, Rules, Planned, Report);
	TestEqual(TEXT("Stone and metal in one cell stay two pieces"), Planned.Num(), 2);
	TestEqual(TEXT("And the two stone impacts became one"), SumMergedCount(Planned), 3);

	// A cell key is a floor division, so it has to behave either side of the origin. Truncation towards zero
	// would put everything between -99 and +99 in the same cell and merge the two sides of a wall together.
	TestTrue(TEXT("Just below the origin is the cell before it"),
		UDebrisDirectorStatics::MergeCellForLocation(FVector(-1.0f, -1.0f, -1.0f), 100.0f) == FIntVector(-1, -1, -1));
	TestTrue(TEXT("Just above the origin is cell zero"),
		UDebrisDirectorStatics::MergeCellForLocation(FVector(1.0f, 1.0f, 1.0f), 100.0f) == FIntVector(0, 0, 0));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
