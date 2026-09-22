// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/HitResult.h"
#include "DebrisDirectorTypes.h"
#include "DebrisDirectorComponent.generated.h"

class UDebrisProfile;

/**
 * The convenient way to report impacts: drop it on an actor and its hits become debris.
 *
 * Everything here is one line of forwarding to the subsystem, and that is the point. A weapon, a destructible
 * prop or a vehicle does not need to know that a director exists, what a profile is, or how a budget works -
 * it reports where it was hit, and the component fills in the surface, the importance and the default
 * impulse scaling that its owner has been configured with.
 *
 * There is a naming rule in this class that is worth stating: nothing here is called IsRegistered().
 * UActorComponent already has that method, it is not virtual, and a component that shadows it compiles
 * cleanly and then answers the engine's question with its own unrelated answer. Registration state is
 * IsReportingEnabled() here for exactly that reason.
 */
UCLASS(ClassGroup = (DebrisDirector), meta = (BlueprintSpawnableComponent, DisplayName = "Debris Director Component"))
class DEBRISDIRECTOR_API UDebrisDirectorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDebrisDirectorComponent();

	//~ UActorComponent interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	//~ Configuration ------------------------------------------------------------------------------------

	/** Surface name used by every impact this component reports, unless one is given per call. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DebrisDirector")
	FName SurfaceType = NAME_None;

	/** Importance for every impact this component reports. Negative takes the profile's own value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DebrisDirector",
		meta = (UIMin = "-1.0", UIMax = "1.0"))
	float Importance = -1.0f;

	/** Multiplier applied to every impulse this component passes on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DebrisDirector", meta = (ClampMin = "0.0", UIMax = "10.0"))
	float ImpulseScale = 1.0f;

	/** Impulse used when a caller does not give one - a point damage event, for instance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DebrisDirector", meta = (ClampMin = "0.0"))
	float DefaultImpulse = 400.0f;

	/** Let this component's impacts leave decals, when the profile has one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DebrisDirector")
	bool bWantsDecals = true;

	/**
	 * Report the owner's point damage automatically.
	 *
	 * On, the owner's OnTakePointDamage gives a location, a normal and an amount, which is exactly an impact
	 * request - so a prop that already takes damage produces debris without a single new node. Off is for
	 * actors whose damage and whose visible impacts are not the same event.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DebrisDirector")
	bool bAutoReportPointDamage = false;

	/** How much of a damage amount becomes impulse, when point damage is reported automatically. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DebrisDirector",
		meta = (EditCondition = "bAutoReportPointDamage", ClampMin = "0.0", UIMax = "100.0"))
	float DamageToImpulse = 20.0f;

	//~ Reporting ----------------------------------------------------------------------------------------

	/** Report one impact, with this component's surface, importance and impulse scaling applied. */
	UFUNCTION(BlueprintCallable, Category = "DebrisDirector")
	bool ReportImpact(const FVector& Location, const FVector& Normal, float Impulse);

	/** Report one impact using DefaultImpulse. The call a simple "I was hit here" Blueprint makes. */
	UFUNCTION(BlueprintCallable, Category = "DebrisDirector")
	bool ReportImpactSimple(const FVector& Location, const FVector& Normal);

	/**
	 * Report one impact straight out of a hit result.
	 *
	 * Takes the physical surface off the hit's physical material as well, so a project that already paints
	 * its geometry with physical materials gets the right debris without naming a surface anywhere.
	 */
	UFUNCTION(BlueprintCallable, Category = "DebrisDirector")
	bool ReportHit(const FHitResult& Hit, float Impulse);

	/** Report a burst of impacts around a point - an explosion, in one call. Returns how many were accepted. */
	UFUNCTION(BlueprintCallable, Category = "DebrisDirector")
	int32 ReportRadialBurst(const FVector& Origin, float Radius, int32 Count, float Impulse);

	/** Build the request this component would send, without sending it. Useful for tests and for tweaking. */
	UFUNCTION(BlueprintPure, Category = "DebrisDirector")
	FDebrisRequest MakeRequest(const FVector& Location, const FVector& Normal, float Impulse) const;

	//~ State --------------------------------------------------------------------------------------------

	/**
	 * Whether this component is currently passing impacts on.
	 *
	 * Not called IsRegistered(). See the class comment - that name belongs to UActorComponent and taking it
	 * would silently answer the engine's registration question with this component's business logic.
	 */
	UFUNCTION(BlueprintPure, Category = "DebrisDirector")
	bool IsReportingEnabled() const { return bReportingEnabled; }

	/** Stop or resume passing impacts on, without removing the component. */
	UFUNCTION(BlueprintCallable, Category = "DebrisDirector")
	void SetReportingEnabled(bool bEnabled);

	/** How many impacts this component has passed on since it began play. */
	UFUNCTION(BlueprintPure, Category = "DebrisDirector")
	int32 GetReportedCount() const { return ReportedCount; }

private:
	/** Bound to the owner's OnTakePointDamage while bAutoReportPointDamage is set. */
	UFUNCTION()
	void HandlePointDamage(AActor* DamagedActor, float Damage, class AController* InstigatedBy, FVector HitLocation,
		class UPrimitiveComponent* HitComponent, FName BoneName, FVector ShotFromDirection,
		const class UDamageType* DamageType, AActor* DamageCauser);

	bool bReportingEnabled = true;
	bool bBoundToPointDamage = false;
	int32 ReportedCount = 0;
};
