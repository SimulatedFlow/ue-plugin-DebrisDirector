// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "DebrisPiece.h"

#include "Components/StaticMeshComponent.h"
#include "DebrisProfile.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

ADebrisPiece::ADebrisPiece()
{
	// A piece has nothing to do on a tick. Its whole lifetime is driven from the director's one tick, which
	// is the difference between one tick for four hundred pieces and four hundred ticks.
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	SetRootComponent(MeshComponent);

	MeshComponent->SetMobility(EComponentMobility::Movable);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComponent->SetGenerateOverlapEvents(false);
	MeshComponent->SetCanEverAffectNavigation(false);
	MeshComponent->bReceivesDecals = false;

	// Debris is the textbook case for distance culling, and the textbook case for not casting shadows: a
	// four-hundred-piece shadow cast is a second four-hundred-piece draw for silhouettes nobody looks at.
	MeshComponent->SetCastShadow(false);

	SetActorEnableCollision(false);
	SetCanBeDamaged(false);

	// Debris is cosmetic. Replicating four hundred rigid bodies would cost more bandwidth than the game
	// they are decorating; every client makes its own from the impacts it already receives.
	bReplicates = false;
}

void ADebrisPiece::ActivateFor(UDebrisProfile* Profile, UStaticMesh* Mesh, const FTransform& Transform,
	const FVector& Impulse, bool bSimulate)
{
	if (!MeshComponent)
	{
		return;
	}

	ActiveProfile = Profile;

	// Physics must be off while the mesh, the transform and the collision profile are changed. Setting a
	// mesh on a simulating body re-creates the body mid-flight, which at best loses the velocity and at
	// worst puts a piece somewhere the impact never was.
	MeshComponent->SetSimulatePhysics(false);

	if (MeshComponent->GetStaticMesh() != Mesh)
	{
		MeshComponent->SetStaticMesh(Mesh);
		LastFadeAmount = -1.0f;
	}

	SetActorTransform(Transform, false, nullptr, ETeleportType::ResetPhysics);

	const bool bCollides = Profile ? Profile->bCollides : true;
	const FName CollisionProfileName = Profile ? Profile->CollisionProfileName : FName(TEXT("PhysicsActor"));

	SetActorEnableCollision(bCollides);
	MeshComponent->SetCollisionProfileName(CollisionProfileName);
	MeshComponent->SetCollisionEnabled(bCollides ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);

	// Material overrides, then the dynamic instances the fade is written to. Both are only rebuilt when the
	// parent material actually changed, so a pool that keeps handing out the same profile pays for neither.
	if (Profile)
	{
		for (int32 Slot = 0; Slot < Profile->MaterialOverrides.Num(); ++Slot)
		{
			if (UMaterialInterface* Override = Profile->MaterialOverrides[Slot])
			{
				MeshComponent->SetMaterial(Slot, Override);
			}
		}
	}

	const int32 SlotCount = MeshComponent->GetNumMaterials();
	const bool bWantsFade = Profile && !Profile->FadeParameterName.IsNone();

	if (bWantsFade)
	{
		FadeMaterials.SetNum(SlotCount);
		FadeMaterialParents.SetNum(SlotCount);

		for (int32 Slot = 0; Slot < SlotCount; ++Slot)
		{
			UMaterialInterface* Current = MeshComponent->GetMaterial(Slot);

			// GetMaterial returns the MID itself once one is applied, so comparing against the remembered
			// parent - and not against the current material - is what stops a new MID being made on every
			// single activation, which would be an allocation per piece per spawn.
			const bool bAlreadyOurs = FadeMaterials.IsValidIndex(Slot) && FadeMaterials[Slot] == Current;
			if (bAlreadyOurs && FadeMaterialParents[Slot] != nullptr)
			{
				continue;
			}

			if (Current && FadeMaterialParents[Slot] != Current)
			{
				FadeMaterialParents[Slot] = Current;
				FadeMaterials[Slot] = MeshComponent->CreateDynamicMaterialInstance(Slot, Current);
			}
		}
	}
	else
	{
		FadeMaterials.Reset();
		FadeMaterialParents.Reset();
	}

	LastFadeAmount = -1.0f;
	SetFadeAmount(0.0f);

	SetActorHiddenInGame(false);

	if (bSimulate && bCollides)
	{
		MeshComponent->SetSimulatePhysics(true);

		if (!Impulse.IsNearlyZero())
		{
			MeshComponent->AddImpulse(Impulse, NAME_None, true);
		}

		if (Profile && Profile->MaxAngularVelocity > 0.0f)
		{
			const float Spin = Profile->MaxAngularVelocity;
			const FVector AngularVelocity(
				FMath::FRandRange(-Spin, Spin),
				FMath::FRandRange(-Spin, Spin),
				FMath::FRandRange(-Spin, Spin));
			MeshComponent->SetPhysicsAngularVelocityInDegrees(AngularVelocity);
		}
	}
}

void ADebrisPiece::FreezePhysics()
{
	if (!MeshComponent || !MeshComponent->IsSimulatingPhysics())
	{
		return;
	}

	// Zero the velocities before switching simulation off. A body frozen while still carrying velocity
	// keeps that velocity in its state, and hands it straight back the moment anything - a Blueprint, a
	// radial impulse, this plugin re-activating the piece - switches simulation on again.
	MeshComponent->SetPhysicsLinearVelocity(FVector::ZeroVector);
	MeshComponent->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	MeshComponent->SetSimulatePhysics(false);

	// It still blocks a trace and it still stops a player walking through it; what it no longer does is take
	// part in the solver. That is the entire trade, and it is why a settled scene costs nothing.
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
}

void ADebrisPiece::SetFadeAmount(float Alpha)
{
	const float Clamped = FMath::Clamp(Alpha, 0.0f, 1.0f);
	if (FMath::IsNearlyEqual(Clamped, LastFadeAmount, KINDA_SMALL_NUMBER))
	{
		return;
	}

	LastFadeAmount = Clamped;

	if (!ActiveProfile || ActiveProfile->FadeParameterName.IsNone())
	{
		return;
	}

	const FName Parameter = ActiveProfile->FadeParameterName;
	for (UMaterialInstanceDynamic* Material : FadeMaterials)
	{
		if (Material)
		{
			Material->SetScalarParameterValue(Parameter, Clamped);
		}
	}
}

void ADebrisPiece::ParkForPool(bool bMoveAway, const FVector& ParkLocation)
{
	if (MeshComponent)
	{
		MeshComponent->SetSimulatePhysics(false);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	SetActorEnableCollision(false);
	SetActorHiddenInGame(true);
	SetActorTickEnabled(false);

	ActiveProfile = nullptr;
	LastFadeAmount = -1.0f;

	if (bMoveAway)
	{
		// Hidden and uncollidable is enough for the game. Moving it out of the level as well is for the
		// debug capture, the stray sphere overlap and the flying camera - and it makes a pool that has
		// accidentally been left visible obvious instead of subtle.
		SetActorLocation(ParkLocation, false, nullptr, ETeleportType::ResetPhysics);
	}
}
