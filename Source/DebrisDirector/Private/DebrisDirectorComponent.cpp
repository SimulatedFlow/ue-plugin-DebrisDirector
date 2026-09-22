// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "DebrisDirectorComponent.h"

#include "DebrisDirectorSubsystem.h"
#include "Engine/HitResult.h"
#include "GameFramework/Actor.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

UDebrisDirectorComponent::UDebrisDirectorComponent()
{
	// A facade has nothing to do between impacts. Every piece of work in this plugin happens on the
	// director's one tick, and a component that ticked would add one per actor for nothing.
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UDebrisDirectorComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoReportPointDamage)
	{
		if (AActor* Owner = GetOwner())
		{
			Owner->OnTakePointDamage.AddDynamic(this, &UDebrisDirectorComponent::HandlePointDamage);
			bBoundToPointDamage = true;
		}
	}
}

void UDebrisDirectorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bBoundToPointDamage)
	{
		if (AActor* Owner = GetOwner())
		{
			Owner->OnTakePointDamage.RemoveDynamic(this, &UDebrisDirectorComponent::HandlePointDamage);
		}
		bBoundToPointDamage = false;
	}

	Super::EndPlay(EndPlayReason);
}

void UDebrisDirectorComponent::SetReportingEnabled(bool bEnabled)
{
	bReportingEnabled = bEnabled;
}

FDebrisRequest UDebrisDirectorComponent::MakeRequest(const FVector& Location, const FVector& Normal, float Impulse) const
{
	FDebrisRequest Request;
	Request.Location = Location;
	Request.Normal = Normal.IsNearlyZero() ? FVector::UpVector : Normal.GetSafeNormal();
	Request.Impulse = FMath::Max(0.0f, Impulse * ImpulseScale);
	Request.SurfaceType = SurfaceType;
	Request.Importance = Importance;
	Request.bWantsDecal = bWantsDecals;
	return Request;
}

bool UDebrisDirectorComponent::ReportImpact(const FVector& Location, const FVector& Normal, float Impulse)
{
	if (!bReportingEnabled)
	{
		return false;
	}

	UDebrisDirectorSubsystem* Subsystem = UDebrisDirectorSubsystem::Get(this);
	if (!Subsystem)
	{
		return false;
	}

	if (Subsystem->RequestBurst(MakeRequest(Location, Normal, Impulse)))
	{
		++ReportedCount;
		return true;
	}

	return false;
}

bool UDebrisDirectorComponent::ReportImpactSimple(const FVector& Location, const FVector& Normal)
{
	return ReportImpact(Location, Normal, DefaultImpulse);
}

bool UDebrisDirectorComponent::ReportHit(const FHitResult& Hit, float Impulse)
{
	if (!bReportingEnabled)
	{
		return false;
	}

	UDebrisDirectorSubsystem* Subsystem = UDebrisDirectorSubsystem::Get(this);
	if (!Subsystem)
	{
		return false;
	}

	FDebrisRequest Request = MakeRequest(Hit.ImpactPoint, Hit.ImpactNormal, Impulse);

	// The surface the trace actually landed on beats whatever this component was configured with. A single
	// component on a weapon can therefore serve a stone wall, a metal door and a wooden crate without the
	// weapon knowing which of the three it just hit.
	if (const UPhysicalMaterial* PhysicalMaterial = Hit.PhysMaterial.Get())
	{
		Request.PhysicalSurface = PhysicalMaterial->SurfaceType;

		// The explicit name is cleared so the physical surface is what resolves. Leaving both set would make
		// the component's default silently win over the geometry that was actually hit.
		if (Request.PhysicalSurface != SurfaceType_Default)
		{
			Request.SurfaceType = NAME_None;
		}
	}

	if (Subsystem->RequestBurst(Request))
	{
		++ReportedCount;
		return true;
	}

	return false;
}

int32 UDebrisDirectorComponent::ReportRadialBurst(const FVector& Origin, float Radius, int32 Count, float Impulse)
{
	if (!bReportingEnabled || Count <= 0)
	{
		return 0;
	}

	UDebrisDirectorSubsystem* Subsystem = UDebrisDirectorSubsystem::Get(this);
	if (!Subsystem)
	{
		return 0;
	}

	// The whole burst is handed over in one call rather than looped one request at a time. It is the same
	// number of requests either way, but this is the shape the director was built for: everything that
	// happened in one frame arriving together is what makes merging possible at all.
	const float Spread = FMath::Max(0.0f, Radius);

	TArray<FDebrisRequest> Burst;
	Burst.Reserve(Count);

	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector Direction = FMath::VRand();
		const FVector Location = Origin + Direction * FMath::FRandRange(0.0f, Spread);
		Burst.Add(MakeRequest(Location, Direction, Impulse));
	}

	const int32 Accepted = Subsystem->RequestBurstMany(Burst);
	ReportedCount += Accepted;
	return Accepted;
}

void UDebrisDirectorComponent::HandlePointDamage(AActor* DamagedActor, float Damage, AController* InstigatedBy,
	FVector HitLocation, UPrimitiveComponent* HitComponent, FName BoneName, FVector ShotFromDirection,
	const UDamageType* DamageType, AActor* DamageCauser)
{
	if (!bReportingEnabled || Damage <= 0.0f)
	{
		return;
	}

	// ShotFromDirection points from the shooter towards the target, so the surface faces back along it.
	const FVector Normal = ShotFromDirection.IsNearlyZero() ? FVector::UpVector : (-ShotFromDirection).GetSafeNormal();

	ReportImpact(HitLocation, Normal, Damage * DamageToImpulse);
}
