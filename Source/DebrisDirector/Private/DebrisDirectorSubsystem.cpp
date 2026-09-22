// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "DebrisDirectorSubsystem.h"

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/DecalComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DebrisDirectorLog.h"
#include "DebrisDirectorSettings.h"
#include "DebrisDirectorStatics.h"
#include "DebrisPiece.h"
#include "DebrisProfile.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "GlobalRenderResources.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/StringBuilder.h"

namespace DebrisDirectorDraw
{
	/** Lines the counter box always draws. */
	static constexpr int32 FixedStatsLines = 10;

	static constexpr float LineHeight = 15.0f;
	static constexpr float BoxPadding = 8.0f;

	static const FLinearColor PanelBackground(0.0f, 0.0f, 0.0f, 0.62f);
	static const FLinearColor HeadingColor(1.0f, 0.72f, 0.36f, 1.0f);
	static const FLinearColor BodyColor(0.9f, 0.9f, 0.9f, 1.0f);
	static const FLinearColor GoodColor(0.55f, 0.95f, 0.55f, 1.0f);
	static const FLinearColor WarnColor(0.98f, 0.78f, 0.35f, 1.0f);
	static const FLinearColor DimColor(0.62f, 0.62f, 0.62f, 1.0f);

	static void DrawFilledRect(UCanvas* Canvas, const FVector2D& Position, const FVector2D& Size, const FLinearColor& Color)
	{
		FCanvasTileItem Tile(Position, GWhiteTexture, Size, Color);
		Tile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Tile);
	}

	static UDebrisDirectorSubsystem* GetSubsystem(UWorld* World)
	{
		return World ? World->GetSubsystem<UDebrisDirectorSubsystem>() : nullptr;
	}
}

//~ Lifetime -----------------------------------------------------------------------------------------------

void UDebrisDirectorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	ApplySettings();
	LoadStartupProfiles();

	UE_LOG(LogDebrisDirector, Log,
		TEXT("DebrisDirector up: %s, budget %d/frame, population cap %d, merge %s, physics expiry %s (%.2fs)."),
		bEnabled ? TEXT("enabled") : TEXT("disabled"),
		PlanRules.SpawnBudget,
		PopulationCap,
		PlanRules.bMergeEnabled ? TEXT("on") : TEXT("off"),
		bPhysicsSleepEnabled ? TEXT("on") : TEXT("off"),
		DefaultMaxSimSeconds);
}

void UDebrisDirectorSubsystem::Deinitialize()
{
	if (HudPostRenderHandle.IsValid())
	{
		AHUD::OnHUDPostRender.Remove(HudPostRenderHandle);
		HudPostRenderHandle.Reset();
	}

	// Parked pieces are hidden, unticked and moved out of the level, which means nothing else in a world
	// teardown is going to notice them. They are destroyed explicitly rather than left to a garbage
	// collection pass that may run after the pools themselves have gone.
	for (TPair<TObjectPtr<UDebrisProfile>, FDebrisPiecePool>& Pair : Pools)
	{
		for (ADebrisPiece* Piece : Pair.Value.Parked)
		{
			if (IsValid(Piece))
			{
				Piece->Destroy();
			}
		}
	}

	for (FDebrisLiveDecal& Decal : LiveDecals)
	{
		if (IsValid(Decal.Decal))
		{
			Decal.Decal->DestroyComponent();
		}
	}

	Pools.Reset();
	LivePieces.Reset();
	LiveDecals.Reset();
	PendingRequests.Reset();
	Viewers.Reset();
	ProfilesByName.Reset();
	ProfilesByPhysicalSurface.Reset();

	Super::Deinitialize();
}

bool UDebrisDirectorSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// Game and PIE, and deliberately not Editor. A designer building a level did not ask for four hundred
	// rubble actors to appear in the viewport the first time something is dropped onto the floor.
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UDebrisDirectorSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDebrisDirectorSubsystem, STATGROUP_Tickables);
}

UDebrisDirectorSubsystem* UDebrisDirectorSubsystem::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}

	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: WorldContextObject->GetWorld();

	return World ? World->GetSubsystem<UDebrisDirectorSubsystem>() : nullptr;
}

void UDebrisDirectorSubsystem::ApplySettings()
{
	const UDebrisDirectorSettings& Settings = UDebrisDirectorSettings::Get();

	bEnabled = Settings.bEnabled;
	PlanRules = Settings.PlanRules;
	EvictionWeights = Settings.EvictionWeights;

	PopulationCap = FMath::Max(0, Settings.PopulationCap);
	MaxEvictionsPerFrame = FMath::Max(1, Settings.MaxEvictionsPerFrame);
	MaxPendingRequests = FMath::Max(16, Settings.MaxPendingRequests);
	MaxPooledPerProfile = FMath::Max(0, Settings.MaxPooledPerProfile);
	DecalCap = FMath::Max(0, Settings.DecalCap);

	bPhysicsSleepEnabled = Settings.bPhysicsSleepEnabled;
	DefaultMaxSimSeconds = FMath::Max(0.0f, Settings.DefaultMaxSimSeconds);
	DefaultLifetimeSeconds = FMath::Max(0.1f, Settings.DefaultLifetimeSeconds);
	DefaultDecalLifetimeSeconds = FMath::Max(0.1f, Settings.DefaultDecalLifetimeSeconds);
	FadeSeconds = FMath::Max(0.0f, Settings.FadeSeconds);

	bDecalsEnabled = Settings.bDecalsEnabled;
	bParkPooledPieces = Settings.bParkPooledPieces;
	ParkLocation = Settings.ParkLocation;

	bShowStats = Settings.bShowStatsByDefault;
	bAutoDrawStatsOnAnyHUD = Settings.bAutoDrawStatsOnAnyHUD;

	RebindHudDelegate();
}

void UDebrisDirectorSubsystem::LoadStartupProfiles()
{
	const UDebrisDirectorSettings& Settings = UDebrisDirectorSettings::Get();

	for (const TSoftObjectPtr<UDebrisProfile>& Soft : Settings.StartupProfiles)
	{
		if (UDebrisProfile* Profile = Soft.LoadSynchronous())
		{
			RegisterProfile(Profile);
		}
	}

	DefaultProfile = Settings.DefaultProfile.LoadSynchronous();
	if (DefaultProfile)
	{
		RegisterProfile(DefaultProfile);
	}
}

//~ Profiles -----------------------------------------------------------------------------------------------

void UDebrisDirectorSubsystem::RegisterProfile(UDebrisProfile* Profile)
{
	if (!Profile)
	{
		return;
	}

	if (!Profile->SurfaceType.IsNone())
	{
		ProfilesByName.Add(Profile->SurfaceType, Profile);
	}

	for (const TEnumAsByte<EPhysicalSurface>& Surface : Profile->PhysicalSurfaces)
	{
		// Deliberately last-registration-wins rather than first, so a project can override a plugin default
		// simply by registering its own profile later, without having to unregister anything.
		ProfilesByPhysicalSurface.Add(static_cast<uint8>(Surface.GetValue()), Profile);
	}

	if (!DefaultProfile)
	{
		DefaultProfile = Profile;
	}
}

UDebrisProfile* UDebrisDirectorSubsystem::ResolveProfile(const FDebrisRequest& Request) const
{
	// Name first: it is what a call site says on purpose. Physical surface second: it is what the geometry
	// happens to be made of. Default last, so a request that names nothing still produces something rather
	// than silently producing nothing, which is the failure mode nobody notices until a level review.
	if (!Request.SurfaceType.IsNone())
	{
		if (const TObjectPtr<UDebrisProfile>* Found = ProfilesByName.Find(Request.SurfaceType))
		{
			return *Found;
		}
	}

	const uint8 Surface = static_cast<uint8>(Request.PhysicalSurface.GetValue());
	if (Surface != static_cast<uint8>(SurfaceType_Default))
	{
		if (const TObjectPtr<UDebrisProfile>* Found = ProfilesByPhysicalSurface.Find(Surface))
		{
			return *Found;
		}
	}

	return DefaultProfile;
}

void UDebrisDirectorSubsystem::GetRegisteredProfiles(TArray<UDebrisProfile*>& OutProfiles) const
{
	OutProfiles.Reset();

	for (const TPair<FName, TObjectPtr<UDebrisProfile>>& Pair : ProfilesByName)
	{
		if (Pair.Value)
		{
			OutProfiles.AddUnique(Pair.Value);
		}
	}

	for (const TPair<uint8, TObjectPtr<UDebrisProfile>>& Pair : ProfilesByPhysicalSurface)
	{
		if (Pair.Value)
		{
			OutProfiles.AddUnique(Pair.Value);
		}
	}
}

//~ Requests -----------------------------------------------------------------------------------------------

bool UDebrisDirectorSubsystem::RequestBurst(const FDebrisRequest& Request)
{
	if (PendingRequests.Num() >= MaxPendingRequests)
	{
		// The queue only ever holds one frame's worth, so reaching this means a single frame reported
		// thousands of impacts. Counted rather than dropped in silence: the counter box showing a dropped
		// count is a design decision, a request that vanishes is a bug report six weeks later.
		++TotalDropped;
		return false;
	}

	PendingRequests.Add(Request);
	return true;
}

int32 UDebrisDirectorSubsystem::RequestBurstMany(const TArray<FDebrisRequest>& Requests)
{
	int32 Accepted = 0;
	for (const FDebrisRequest& Request : Requests)
	{
		Accepted += RequestBurst(Request) ? 1 : 0;
	}
	return Accepted;
}

int32 UDebrisDirectorSubsystem::RequestStress(int32 Count, float Radius)
{
	const int32 Wanted = FMath::Max(0, Count);
	if (Wanted == 0)
	{
		return 0;
	}

	UpdateViewers();

	// Fired in front of the viewer, because a stress test whose debris lands behind the camera proves the
	// budget and nothing else. In shot, the eviction order is visible too.
	FVector Origin = FVector::ZeroVector;
	FVector Forward = FVector::ForwardVector;
	if (Viewers.Num() > 0)
	{
		Origin = Viewers[0].Location + Viewers[0].Forward * 600.0f;
		Forward = Viewers[0].Forward;
	}

	const float Spread = FMath::Max(1.0f, Radius);

	TArray<FDebrisRequest> Burst;
	Burst.Reserve(Wanted);

	for (int32 Index = 0; Index < Wanted; ++Index)
	{
		FDebrisRequest& Request = Burst.AddDefaulted_GetRef();
		Request.Location = Origin + FMath::VRand() * FMath::FRandRange(0.0f, Spread);
		Request.Normal = -Forward;
		Request.Impulse = FMath::FRandRange(200.0f, 800.0f);
		Request.Importance = -1.0f;
		Request.bWantsDecal = true;
	}

	return RequestBurstMany(Burst);
}

void UDebrisDirectorSubsystem::Clear()
{
	for (int32 Index = LivePieces.Num() - 1; Index >= 0; --Index)
	{
		RecyclePiece(Index);
	}

	for (FDebrisLiveDecal& Decal : LiveDecals)
	{
		if (IsValid(Decal.Decal))
		{
			Decal.Decal->DestroyComponent();
		}
	}

	LivePieces.Reset();
	LiveDecals.Reset();
	PendingRequests.Reset();
}

//~ Budget and switches ------------------------------------------------------------------------------------

void UDebrisDirectorSubsystem::SetBudget(int32 SpawnBudget)
{
	PlanRules.SpawnBudget = FMath::Max(0, SpawnBudget);
	UE_LOG(LogDebrisDirector, Verbose, TEXT("Spawn budget is now %d per frame."), PlanRules.SpawnBudget);
}

void UDebrisDirectorSubsystem::SetPlanRules(const FDebrisPlanRules& NewRules)
{
	PlanRules = NewRules;
	PlanRules.SpawnBudget = FMath::Max(0, PlanRules.SpawnBudget);
	PlanRules.MergeCellSize = FMath::Max(1.0f, PlanRules.MergeCellSize);
	PlanRules.MaxScaleMultiplier = FMath::Max(1.0f, PlanRules.MaxScaleMultiplier);
}

void UDebrisDirectorSubsystem::SetPopulationCap(int32 NewPopulationCap)
{
	PopulationCap = FMath::Max(0, NewPopulationCap);

	// Lowering the ceiling has to take effect now rather than on the next impact, or a player who turns the
	// quality setting down sees nothing change until they fire again.
	const int32 Excess = UDebrisDirectorStatics::PlanEvictionCount(GetPopulation(), 0, PopulationCap);
	if (Excess > 0)
	{
		EvictWorst(FMath::Min(Excess, MaxEvictionsPerFrame));
	}
}

void UDebrisDirectorSubsystem::SetMergeEnabled(bool bNewEnabled)
{
	PlanRules.bMergeEnabled = bNewEnabled;
}

void UDebrisDirectorSubsystem::SetPhysicsSleepEnabled(bool bNewEnabled)
{
	bPhysicsSleepEnabled = bNewEnabled;

	// Switching the expiry date back on must catch the bodies that are already over their time, otherwise
	// the simulating count only falls as new pieces arrive and the demo button looks like it does nothing.
	if (bPhysicsSleepEnabled)
	{
		for (FDebrisLivePiece& Live : LivePieces)
		{
			if (Live.State.bSimulating && Live.State.SimSeconds >= Live.State.MaxSimSeconds && IsValid(Live.Piece))
			{
				Live.State.bSimulating = false;
				Live.Piece->FreezePhysics();
			}
		}
	}
}

void UDebrisDirectorSubsystem::SetEnabled(bool bNewEnabled)
{
	bEnabled = bNewEnabled;
}

void UDebrisDirectorSubsystem::SetShowStats(bool bShow)
{
	bShowStats = bShow;
}

//~ Counts -------------------------------------------------------------------------------------------------

int32 UDebrisDirectorSubsystem::GetPopulation() const
{
	int32 Count = 0;
	for (const FDebrisLivePiece& Live : LivePieces)
	{
		Count += Live.State.bFading ? 0 : 1;
	}
	return Count;
}

int32 UDebrisDirectorSubsystem::GetFadingCount() const
{
	int32 Count = 0;
	for (const FDebrisLivePiece& Live : LivePieces)
	{
		Count += Live.State.bFading ? 1 : 0;
	}
	return Count;
}

int32 UDebrisDirectorSubsystem::GetSimulatingCount() const
{
	int32 Count = 0;
	for (const FDebrisLivePiece& Live : LivePieces)
	{
		Count += Live.State.bSimulating ? 1 : 0;
	}
	return Count;
}

int32 UDebrisDirectorSubsystem::GetSleepingCount() const
{
	return LivePieces.Num() - GetSimulatingCount();
}

//~ Tick ---------------------------------------------------------------------------------------------------

void UDebrisDirectorSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const double TickStart = FPlatformTime::Seconds();

	Stats.SpawnedThisFrame = 0;
	Stats.MergedThisFrame = 0;
	Stats.DroppedThisFrame = 0;
	Stats.EvictedThisFrame = 0;
	Stats.RequestsThisFrame = PendingRequests.Num();

	// Cleared here rather than left standing from the last burst. A "[budgeted]" tag that stays on screen
	// through a quiet minute is a counter box that has stopped describing this frame.
	Stats.bBudgetSaturated = false;

	UpdateViewers();
	ServiceRequests();
	AdvancePieces(DeltaTime);
	AdvanceDecals(DeltaTime);
	UpdateStats(TickStart, DeltaTime);
}

void UDebrisDirectorSubsystem::UpdateViewers()
{
	Viewers.Reset();

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (!PC || !PC->IsLocalController() || !PC->PlayerCameraManager)
		{
			continue;
		}

		FDebrisViewer& Viewer = Viewers.AddDefaulted_GetRef();
		Viewer.Location = PC->PlayerCameraManager->GetCameraLocation();
		Viewer.Forward = PC->PlayerCameraManager->GetCameraRotation().Vector();

		// Widened past the real field of view on purpose. A piece just outside the frustum is a piece the
		// player is about to see the moment they turn, and taking that one away is exactly the pop the
		// eviction order exists to avoid.
		const float HalfFov = FMath::DegreesToRadians(FMath::Clamp(PC->PlayerCameraManager->GetFOVAngle(), 10.0f, 170.0f) * 0.5f) * 1.25f;
		Viewer.CosHalfFov = FMath::Cos(FMath::Min(HalfFov, PI * 0.5f));
	}
}

float UDebrisDirectorSubsystem::DistanceToNearestViewer(const FVector& Point, bool* bOutVisible) const
{
	if (bOutVisible)
	{
		*bOutVisible = false;
	}

	if (Viewers.Num() == 0)
	{
		return 0.0f;
	}

	float BestDistance = TNumericLimits<float>::Max();
	bool bVisible = false;

	for (const FDebrisViewer& Viewer : Viewers)
	{
		const FVector ToPoint = Point - Viewer.Location;
		const float Distance = static_cast<float>(ToPoint.Size());

		if (Distance < BestDistance)
		{
			BestDistance = Distance;
		}

		if (!bVisible && Distance > KINDA_SMALL_NUMBER)
		{
			bVisible = FVector::DotProduct(ToPoint / Distance, Viewer.Forward) >= Viewer.CosHalfFov;
		}
	}

	if (bOutVisible)
	{
		*bOutVisible = bVisible;
	}

	return BestDistance;
}

void UDebrisDirectorSubsystem::ServiceRequests()
{
	if (PendingRequests.Num() == 0)
	{
		return;
	}

	if (!bEnabled)
	{
		// Switched off still counts. A project that wants to know how much debris a scene asks for before
		// deciding to pay for any of it gets that number without changing a line of the scene.
		Stats.DroppedThisFrame = PendingRequests.Num();
		TotalDropped += PendingRequests.Num();
		PendingRequests.Reset();
		return;
	}

	TArray<FDebrisPlannedSpawn> Planned;
	FDebrisPlanReport Report;
	UDebrisDirectorStatics::PlanBurst(PendingRequests, PlanRules, Planned, Report);

	Stats.MergedThisFrame = Report.MergedAway;
	Stats.DroppedThisFrame = Report.Dropped;
	Stats.bBudgetSaturated = Report.bBudgetSaturated;
	TotalMerged += Report.MergedAway;
	TotalDropped += Report.Dropped;

	// Everything that is going to be created is known before anything is created, so the ceiling can be made
	// room for in one ranked pass instead of evicting one piece per spawn and re-ranking in between.
	const int32 WantedEvictions = UDebrisDirectorStatics::PlanEvictionCount(GetPopulation(), Planned.Num(), PopulationCap);
	if (WantedEvictions > 0)
	{
		EvictWorst(FMath::Min(WantedEvictions, MaxEvictionsPerFrame));
	}

	for (const FDebrisPlannedSpawn& Entry : Planned)
	{
		UDebrisProfile* Profile = nullptr;
		if (PendingRequests.IsValidIndex(Entry.SourceRequestIndex))
		{
			Profile = ResolveProfile(PendingRequests[Entry.SourceRequestIndex]);
		}
		else
		{
			FDebrisRequest Synthetic;
			Synthetic.SurfaceType = Entry.SurfaceType;
			Synthetic.PhysicalSurface = Entry.PhysicalSurface;
			Profile = ResolveProfile(Synthetic);
		}

		if (ADebrisPiece* Piece = SpawnPlanned(Entry, Profile))
		{
			++Stats.SpawnedThisFrame;
			++TotalSpawned;
			OnPieceSpawned.Broadcast(Piece, Profile);
		}

		if (Entry.bWantsDecal)
		{
			SpawnDecal(Entry, Profile);
		}
	}

	PendingRequests.Reset();
}

ADebrisPiece* UDebrisDirectorSubsystem::SpawnPlanned(const FDebrisPlannedSpawn& Planned, UDebrisProfile* Profile)
{
	UWorld* World = GetWorld();
	if (!World || !Profile)
	{
		return nullptr;
	}

	UStaticMesh* Mesh = Profile->PickMesh(FMath::FRand());
	if (!Mesh)
	{
		// A profile with no meshes is a decal-only surface. Legitimate, and not worth a warning per impact.
		return nullptr;
	}

	ADebrisPiece* Piece = nullptr;

	if (FDebrisPiecePool* Pool = Pools.Find(Profile))
	{
		while (Pool->Parked.Num() > 0 && !Piece)
		{
			ADebrisPiece* Candidate = Pool->Parked.Pop(EAllowShrinking::No);
			if (IsValid(Candidate))
			{
				Piece = Candidate;
			}
		}
	}

	if (Piece)
	{
		++PoolHits;
	}
	else
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;

		Piece = World->SpawnActor<ADebrisPiece>(ADebrisPiece::StaticClass(), FTransform::Identity, Params);
		if (!Piece)
		{
			return nullptr;
		}

		++NewAllocations;
	}

	const float BaseScale = FMath::FRandRange(
		FMath::Max(0.01f, static_cast<float>(Profile->ScaleRange.X)),
		FMath::Max(0.01f, static_cast<float>(Profile->ScaleRange.Y)));

	const FVector Normal = Planned.Normal.GetSafeNormal().IsNearlyZero() ? FVector::UpVector : Planned.Normal.GetSafeNormal();

	// Sat slightly off the surface along the normal, scaled with the piece: a merged chunk is bigger, and a
	// bigger chunk spawned at the exact contact point starts the frame inside the wall.
	const float Scale = BaseScale * Planned.ScaleMultiplier;
	const FTransform Transform(FRotator(FMath::FRand() * 360.0f, FMath::FRand() * 360.0f, FMath::FRand() * 360.0f),
		Planned.Location + Normal * (2.0f * Scale),
		FVector(Scale));

	const FVector LaunchDirection = FMath::VRandCone(Normal, FMath::DegreesToRadians(Profile->LaunchConeAngle));
	const FVector Impulse = LaunchDirection * Planned.Impulse * Profile->ImpulseScale;

	Piece->ActivateFor(Profile, Mesh, Transform, Impulse, Profile->bCollides);

	FDebrisLivePiece& Live = LivePieces.AddDefaulted_GetRef();
	Live.Piece = Piece;
	Live.Profile = Profile;
	Live.State.LifetimeSeconds = (Profile->LifetimeSeconds >= 0.0f) ? Profile->LifetimeSeconds : DefaultLifetimeSeconds;
	Live.State.MaxSimSeconds = bPhysicsSleepEnabled
		? ((Profile->MaxSimSeconds >= 0.0f) ? Profile->MaxSimSeconds : DefaultMaxSimSeconds)
		: -1.0f;
	Live.State.FadeSeconds = FadeSeconds;
	Live.State.Importance = FMath::Clamp(Planned.Importance >= 0.0f ? Planned.Importance : Profile->Importance, 0.0f, 1.0f);
	Live.State.bSimulating = Profile->bCollides;

	return Piece;
}

void UDebrisDirectorSubsystem::SpawnDecal(const FDebrisPlannedSpawn& Planned, UDebrisProfile* Profile)
{
	if (!bDecalsEnabled || !Profile || !Profile->DecalMaterial || DecalCap <= 0)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// The decal ceiling is a plain oldest-first queue and not the ranked eviction the pieces get. A decal
	// costs a fraction of what a rigid body costs and never simulates, so ranking them would spend more on
	// the decision than on the thing being decided.
	while (LiveDecals.Num() >= DecalCap)
	{
		if (IsValid(LiveDecals[0].Decal))
		{
			LiveDecals[0].Decal->DestroyComponent();
		}
		LiveDecals.RemoveAt(0, EAllowShrinking::No);
	}

	const float Lifetime = (Profile->DecalLifetimeSeconds >= 0.0f) ? Profile->DecalLifetimeSeconds : DefaultDecalLifetimeSeconds;
	const FRotator Rotation = (-Planned.Normal).Rotation();
	const FVector Size = Profile->DecalSize * FMath::Max(1.0f, Planned.ScaleMultiplier);

	UDecalComponent* Decal = UGameplayStatics::SpawnDecalAtLocation(World, Profile->DecalMaterial, Size, Planned.Location, Rotation, Lifetime);
	if (!Decal)
	{
		return;
	}

	Decal->SetFadeScreenSize(0.001f);

	FDebrisLiveDecal& Live = LiveDecals.AddDefaulted_GetRef();
	Live.Decal = Decal;
	Live.SecondsRemaining = Lifetime;
}

int32 UDebrisDirectorSubsystem::EvictWorst(int32 Count)
{
	if (Count <= 0 || LivePieces.Num() == 0)
	{
		return 0;
	}

	// Only pieces that are not already leaving are candidates. A fading piece has given its slot up already;
	// ranking it again would evict the same piece twice and count it twice on the counter box.
	TArray<FDebrisEvictionCandidate> Candidates;
	TArray<int32> CandidateToLive;
	Candidates.Reserve(LivePieces.Num());
	CandidateToLive.Reserve(LivePieces.Num());

	for (int32 Index = 0; Index < LivePieces.Num(); ++Index)
	{
		const FDebrisLivePiece& Live = LivePieces[Index];
		if (Live.State.bFading || !IsValid(Live.Piece))
		{
			continue;
		}

		bool bVisible = false;
		const float Distance = DistanceToNearestViewer(Live.Piece->GetActorLocation(), &bVisible);

		FDebrisEvictionCandidate& Candidate = Candidates.AddDefaulted_GetRef();
		Candidate.Importance = Live.State.Importance;
		Candidate.DistanceToViewer = Distance;
		Candidate.AgeSeconds = Live.State.AgeSeconds;
		Candidate.bVisible = bVisible;
		Candidate.bSimulating = Live.State.bSimulating;

		CandidateToLive.Add(Index);
	}

	TArray<int32> Chosen;
	UDebrisDirectorStatics::RankForEviction(Candidates, EvictionWeights, Count, Chosen);

	int32 Evicted = 0;
	for (const int32 CandidateIndex : Chosen)
	{
		const int32 LiveIndex = CandidateToLive[CandidateIndex];
		FDebrisLivePiece& Live = LivePieces[LiveIndex];

		if (!UDebrisDirectorStatics::BeginFade(Live.State))
		{
			continue;
		}

		// Frozen the moment it is evicted, not when the fade ends. A piece that is on its way out has no
		// business still being in the solver for another third of a second.
		if (IsValid(Live.Piece))
		{
			Live.Piece->FreezePhysics();
			OnPieceEvicted.Broadcast(Live.Piece);
		}
		Live.State.bSimulating = false;

		++Evicted;
		++TotalEvicted;
		++EvictionsInWindow;
		++Stats.EvictedThisFrame;
	}

	return Evicted;
}

void UDebrisDirectorSubsystem::AdvancePieces(float DeltaTime)
{
	for (int32 Index = LivePieces.Num() - 1; Index >= 0; --Index)
	{
		FDebrisLivePiece& Live = LivePieces[Index];

		if (!IsValid(Live.Piece))
		{
			// Something outside the plugin destroyed it. Drop the bookkeeping rather than warning every
			// frame - a game is allowed to clean up its own level.
			LivePieces.RemoveAtSwap(Index, EAllowShrinking::No);
			continue;
		}

		const EDebrisLifetimeEvent Event = UDebrisDirectorStatics::AdvancePiece(Live.State, DeltaTime);

		switch (Event)
		{
		case EDebrisLifetimeEvent::FrozePhysics:
			Live.Piece->FreezePhysics();
			break;

		case EDebrisLifetimeEvent::StartedFade:
			Live.Piece->FreezePhysics();
			Live.State.bSimulating = false;
			break;

		case EDebrisLifetimeEvent::Expired:
			RecyclePiece(Index);
			continue;

		case EDebrisLifetimeEvent::None:
		default:
			break;
		}

		if (Live.State.bFading)
		{
			Live.Piece->SetFadeAmount(Live.State.GetFadeAlpha());
		}
	}
}

void UDebrisDirectorSubsystem::AdvanceDecals(float DeltaTime)
{
	for (int32 Index = LiveDecals.Num() - 1; Index >= 0; --Index)
	{
		FDebrisLiveDecal& Live = LiveDecals[Index];
		Live.SecondsRemaining -= DeltaTime;

		// The decal component destroys itself on its own lifespan; this only keeps the bookkeeping - and
		// therefore the ceiling - honest about how many are really out there.
		if (Live.SecondsRemaining <= 0.0f || !IsValid(Live.Decal))
		{
			LiveDecals.RemoveAt(Index, EAllowShrinking::No);
		}
	}
}

void UDebrisDirectorSubsystem::RecyclePiece(int32 LiveIndex)
{
	if (!LivePieces.IsValidIndex(LiveIndex))
	{
		return;
	}

	FDebrisLivePiece Live = LivePieces[LiveIndex];
	LivePieces.RemoveAtSwap(LiveIndex, EAllowShrinking::No);

	if (!IsValid(Live.Piece))
	{
		return;
	}

	Live.Piece->ParkForPool(bParkPooledPieces, ParkLocation);

	FDebrisPiecePool& Pool = Pools.FindOrAdd(Live.Profile);
	if (Pool.Parked.Num() < MaxPooledPerProfile)
	{
		Pool.Parked.Add(Live.Piece);
	}
	else
	{
		// A pool is memory that is not being used for anything. Past the ceiling the piece is destroyed
		// rather than hoarded, so a scene that once had a spike does not keep paying for it.
		Live.Piece->Destroy();
	}
}

//~ Statistics ---------------------------------------------------------------------------------------------

void UDebrisDirectorSubsystem::UpdateStats(double TickStartSeconds, float DeltaTime)
{
	EvictionWindowSeconds += DeltaTime;
	if (EvictionWindowSeconds >= 1.0f)
	{
		Stats.EvictionsPerSecond = EvictionsInWindow / EvictionWindowSeconds;
		EvictionsInWindow = 0;
		EvictionWindowSeconds = 0.0f;
	}

	int32 Population = 0;
	int32 Fading = 0;
	int32 Simulating = 0;

	for (const FDebrisLivePiece& Live : LivePieces)
	{
		if (Live.State.bFading)
		{
			++Fading;
		}
		else
		{
			++Population;
		}

		if (Live.State.bSimulating)
		{
			++Simulating;
		}
	}

	int32 Parked = 0;
	for (const TPair<TObjectPtr<UDebrisProfile>, FDebrisPiecePool>& Pair : Pools)
	{
		Parked += Pair.Value.Parked.Num();
	}

	Stats.SpawnBudget = PlanRules.SpawnBudget;
	Stats.Population = Population;
	Stats.PopulationCap = PopulationCap;
	Stats.FadingCount = Fading;
	Stats.SimulatingCount = Simulating;
	Stats.SleepingCount = LivePieces.Num() - Simulating;
	Stats.TotalSpawned = TotalSpawned;
	Stats.TotalMerged = TotalMerged;
	Stats.TotalDropped = TotalDropped;
	Stats.TotalEvicted = TotalEvicted;
	Stats.PoolHits = PoolHits;
	Stats.NewAllocations = NewAllocations;
	Stats.PooledPieces = Parked;
	Stats.DecalCount = LiveDecals.Num();
	Stats.DecalCap = DecalCap;
	Stats.bMergeEnabled = PlanRules.bMergeEnabled;
	Stats.bPhysicsSleepEnabled = bPhysicsSleepEnabled;
	Stats.bEnabled = bEnabled;

	Stats.TickMilliseconds = static_cast<float>((FPlatformTime::Seconds() - TickStartSeconds) * 1000.0);
}

int32 UDebrisDirectorSubsystem::GetStatsLineCount() const
{
	return DebrisDirectorDraw::FixedStatsLines;
}

void UDebrisDirectorSubsystem::DrawStatsBox(UCanvas* Canvas, const FVector2D& Origin, float Width) const
{
	using namespace DebrisDirectorDraw;

	if (!Canvas)
	{
		return;
	}

	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (!Font)
	{
		return;
	}

	LastStatsDrawFrame = GFrameCounter;

	const float BoxHeight = GetStatsLineCount() * LineHeight + BoxPadding * 2.0f;
	DrawFilledRect(Canvas,
		FVector2D(Origin.X - BoxPadding, Origin.Y - BoxPadding),
		FVector2D(Width, BoxHeight),
		PanelBackground);

	float LineY = static_cast<float>(Origin.Y);
	auto DrawLine = [&](FStringView Line, const FLinearColor& Color)
	{
		FCanvasTextStringViewItem Item(FVector2D(Origin.X, LineY), Line, Font, Color);
		Canvas->DrawItem(Item);
		LineY += LineHeight;
	};

	TStringBuilder<192> Line;

	Line.Reset();
	Line.Appendf(TEXT("DebrisDirector%s"), Stats.bEnabled ? TEXT("") : TEXT("   (disabled)"));
	DrawLine(Line.ToView(), Stats.bEnabled ? HeadingColor : DimColor);

	// The line the whole plugin is judged on: what one frame created, next to what it was allowed to.
	Line.Reset();
	Line.Appendf(TEXT("Spawned        %d this frame   (budget %d)%s"),
		Stats.SpawnedThisFrame, Stats.SpawnBudget, Stats.bBudgetSaturated ? TEXT("   [budgeted]") : TEXT(""));
	DrawLine(Line.ToView(), Stats.bBudgetSaturated ? WarnColor : (Stats.SpawnedThisFrame > 0 ? GoodColor : BodyColor));

	// And the line that shows the budget is not a cut-off: requests in, merged away, and nothing lost.
	Line.Reset();
	Line.Appendf(TEXT("Requests       %d in   %d merged   %d dropped"),
		Stats.RequestsThisFrame, Stats.MergedThisFrame, Stats.DroppedThisFrame);
	DrawLine(Line.ToView(), Stats.DroppedThisFrame > 0 ? WarnColor : BodyColor);

	Line.Reset();
	Line.Appendf(TEXT("Merge          %s"), Stats.bMergeEnabled ? TEXT("ON  - over budget becomes bigger") : TEXT("OFF - over budget is lost"));
	DrawLine(Line.ToView(), Stats.bMergeEnabled ? GoodColor : WarnColor);

	Line.Reset();
	Line.Appendf(TEXT("Population     %d / %d   (+%d fading out)"), Stats.Population, Stats.PopulationCap, Stats.FadingCount);
	DrawLine(Line.ToView(), Stats.Population >= Stats.PopulationCap ? WarnColor : BodyColor);

	// The physics line. In a settled scene the left number goes to zero and stays there.
	Line.Reset();
	Line.Appendf(TEXT("Bodies         %d simulating   %d sleeping   expiry %s"),
		Stats.SimulatingCount, Stats.SleepingCount, Stats.bPhysicsSleepEnabled ? TEXT("ON") : TEXT("OFF"));
	DrawLine(Line.ToView(), Stats.bPhysicsSleepEnabled ? (Stats.SimulatingCount > 0 ? BodyColor : GoodColor) : WarnColor);

	Line.Reset();
	Line.Appendf(TEXT("Evicted        %d this frame   %.1f / s"), Stats.EvictedThisFrame, Stats.EvictionsPerSecond);
	DrawLine(Line.ToView(), Stats.EvictedThisFrame > 0 ? WarnColor : DimColor);

	Line.Reset();
	Line.Appendf(TEXT("Pool           hits %d   new %d   parked %d"), Stats.PoolHits, Stats.NewAllocations, Stats.PooledPieces);
	DrawLine(Line.ToView(), BodyColor);

	Line.Reset();
	Line.Appendf(TEXT("Decals         %d / %d"), Stats.DecalCount, Stats.DecalCap);
	DrawLine(Line.ToView(), DimColor);

	Line.Reset();
	Line.Appendf(TEXT("Totals         %d spawned   %d merged   %d evicted   %.3f ms"),
		Stats.TotalSpawned, Stats.TotalMerged, Stats.TotalEvicted, Stats.TickMilliseconds);
	DrawLine(Line.ToView(), DimColor);
}

void UDebrisDirectorSubsystem::LogStats() const
{
	UE_LOG(LogDebrisDirector, Display, TEXT("DebrisDirector: %s"), Stats.bEnabled ? TEXT("enabled") : TEXT("disabled"));
	UE_LOG(LogDebrisDirector, Display, TEXT("  Spawned this frame  %d (budget %d)%s"),
		Stats.SpawnedThisFrame, Stats.SpawnBudget, Stats.bBudgetSaturated ? TEXT(" [budgeted]") : TEXT(""));
	UE_LOG(LogDebrisDirector, Display, TEXT("  Requests            %d in, %d merged away, %d dropped, merge %s"),
		Stats.RequestsThisFrame, Stats.MergedThisFrame, Stats.DroppedThisFrame, Stats.bMergeEnabled ? TEXT("on") : TEXT("off"));
	UE_LOG(LogDebrisDirector, Display, TEXT("  Population          %d / %d (+%d fading)"),
		Stats.Population, Stats.PopulationCap, Stats.FadingCount);
	UE_LOG(LogDebrisDirector, Display, TEXT("  Bodies              %d simulating, %d sleeping, expiry %s"),
		Stats.SimulatingCount, Stats.SleepingCount, Stats.bPhysicsSleepEnabled ? TEXT("on") : TEXT("off"));
	UE_LOG(LogDebrisDirector, Display, TEXT("  Evictions           %d this frame, %.1f / s, %d total"),
		Stats.EvictedThisFrame, Stats.EvictionsPerSecond, Stats.TotalEvicted);
	UE_LOG(LogDebrisDirector, Display, TEXT("  Pool                %d hits, %d new, %d parked"),
		Stats.PoolHits, Stats.NewAllocations, Stats.PooledPieces);
	UE_LOG(LogDebrisDirector, Display, TEXT("  Decals              %d / %d"), Stats.DecalCount, Stats.DecalCap);
	UE_LOG(LogDebrisDirector, Display, TEXT("  Tick                %.3f ms"), Stats.TickMilliseconds);
}

void UDebrisDirectorSubsystem::RebindHudDelegate()
{
	if (bAutoDrawStatsOnAnyHUD && !HudPostRenderHandle.IsValid())
	{
		HudPostRenderHandle = AHUD::OnHUDPostRender.AddUObject(this, &UDebrisDirectorSubsystem::OnAnyHUDPostRender);
	}
	else if (!bAutoDrawStatsOnAnyHUD && HudPostRenderHandle.IsValid())
	{
		AHUD::OnHUDPostRender.Remove(HudPostRenderHandle);
		HudPostRenderHandle.Reset();
	}
}

void UDebrisDirectorSubsystem::OnAnyHUDPostRender(AHUD* HUD, UCanvas* Canvas)
{
	if (!bAutoDrawStatsOnAnyHUD || !bShowStats || !HUD || !Canvas)
	{
		return;
	}

	if (HUD->GetWorld() != GetWorld())
	{
		return;
	}

	// ADebrisDirectorHUD already drew this frame - do not stack a second box on top of it.
	if (LastStatsDrawFrame == GFrameCounter)
	{
		return;
	}

	DrawStatsBox(Canvas, FVector2D(28.0f, 90.0f), 470.0f);
}

//~ Console commands ---------------------------------------------------------------------------------------

namespace DebrisDirectorCommands
{
	using DebrisDirectorDraw::GetSubsystem;

	static FAutoConsoleCommandWithWorldAndArgs CmdShow(
		TEXT("Debris.Show"),
		TEXT("Debris.Show 0|1 - show or hide the debris counter box."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			UDebrisDirectorSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogDebrisDirector, Warning, TEXT("Debris.Show: no DebrisDirector in this world."));
				return;
			}

			const bool bShow = (Args.Num() > 0) ? (FCString::Atoi(*Args[0]) != 0) : !Subsystem->AreStatsShown();
			Subsystem->SetShowStats(bShow);
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdBudget(
		TEXT("Debris.Budget"),
		TEXT("Debris.Budget <n> - how many pieces one frame may create. No argument prints it."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			UDebrisDirectorSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogDebrisDirector, Warning, TEXT("Debris.Budget: no DebrisDirector in this world."));
				return;
			}

			if (Args.Num() == 0)
			{
				UE_LOG(LogDebrisDirector, Display, TEXT("Debris.Budget: %d pieces per frame."), Subsystem->GetBudget());
				return;
			}

			Subsystem->SetBudget(FCString::Atoi(*Args[0]));
			UE_LOG(LogDebrisDirector, Display, TEXT("Debris.Budget: %d pieces per frame."), Subsystem->GetBudget());
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdCap(
		TEXT("Debris.Cap"),
		TEXT("Debris.Cap <n> - how many pieces may exist. Lowering it evicts the excess. No argument prints it."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			UDebrisDirectorSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogDebrisDirector, Warning, TEXT("Debris.Cap: no DebrisDirector in this world."));
				return;
			}

			if (Args.Num() == 0)
			{
				UE_LOG(LogDebrisDirector, Display, TEXT("Debris.Cap: %d alive, ceiling %d."),
					Subsystem->GetPopulation(), Subsystem->GetPopulationCap());
				return;
			}

			Subsystem->SetPopulationCap(FCString::Atoi(*Args[0]));
			UE_LOG(LogDebrisDirector, Display, TEXT("Debris.Cap: %d alive, ceiling %d."),
				Subsystem->GetPopulation(), Subsystem->GetPopulationCap());
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdClear(
		TEXT("Debris.Clear"),
		TEXT("Debris.Clear - take every piece and decal away at once."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			UDebrisDirectorSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogDebrisDirector, Warning, TEXT("Debris.Clear: no DebrisDirector in this world."));
				return;
			}
			Subsystem->Clear();
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdStress(
		TEXT("Debris.Stress"),
		TEXT("Debris.Stress <n> [Radius] - report n impacts in front of the viewer in a single frame."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			UDebrisDirectorSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogDebrisDirector, Warning, TEXT("Debris.Stress: no DebrisDirector in this world."));
				return;
			}

			const int32 Count = (Args.Num() > 0) ? FCString::Atoi(*Args[0]) : 40;
			const float Radius = (Args.Num() > 1) ? FCString::Atof(*Args[1]) : 400.0f;

			const int32 Accepted = Subsystem->RequestStress(Count, Radius);
			UE_LOG(LogDebrisDirector, Display, TEXT("Debris.Stress: %d of %d requests queued for this frame, budget %d."),
				Accepted, Count, Subsystem->GetBudget());
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdStats(
		TEXT("Debris.Stats"),
		TEXT("Debris.Stats - print the measured debris counters to the log."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			const UDebrisDirectorSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogDebrisDirector, Warning, TEXT("Debris.Stats: no DebrisDirector in this world."));
				return;
			}
			Subsystem->LogStats();
		}));
}
