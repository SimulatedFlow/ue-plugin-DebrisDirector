// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Chaos/ChaosEngineInterface.h"
#include "Engine/DataAsset.h"
#include "Engine/EngineTypes.h"
#include "UObject/ObjectPtr.h"
#include "DebrisProfile.generated.h"

class UMaterialInterface;
class UStaticMesh;

/** One mesh a profile can pick, and how often it is picked relative to the others in the same profile. */
USTRUCT(BlueprintType)
struct DEBRISDIRECTOR_API FDebrisMeshEntry
{
	GENERATED_BODY()

	/** The mesh. An entry with no mesh is skipped rather than treated as an error. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debris")
	TObjectPtr<UStaticMesh> Mesh = nullptr;

	/** Relative pick weight. Zero never gets picked, which is a usable way to switch one off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debris", meta = (ClampMin = "0.0", UIMax = "10.0"))
	float PickWeight = 1.0f;
};

/**
 * What one kind of surface leaves behind when it is hit.
 *
 * A profile is the answer to "what is this wall made of", and nothing else. It does not know how often it
 * is hit, how many pieces are already lying around or how expensive the frame is - all three of those are
 * the director's business, and keeping them out of here is why the same stone profile works in a quiet
 * corridor and in the middle of a firefight without a designer maintaining two of them.
 *
 * A UPrimaryDataAsset rather than a DataTable row because the interesting fields are asset references, and
 * a table of asset references is a table that either loads everything or nothing.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Debris Profile"))
class DEBRISDIRECTOR_API UDebrisProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UDebrisProfile();

	//~ UPrimaryDataAsset interface
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	//~ Identity -----------------------------------------------------------------------------------------

	/**
	 * The name a request asks for - "Stone", "Metal", "Wood".
	 *
	 * Registered with the director at load, so a weapon can name a surface without holding a reference to
	 * the asset that describes it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FName SurfaceType = NAME_None;

	/**
	 * Physical surfaces this profile also answers for.
	 *
	 * The zero-work path: a project that already puts physical materials on its geometry gets matching
	 * debris straight out of an FHitResult, without a single call site learning a new name.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	TArray<TEnumAsByte<EPhysicalSurface>> PhysicalSurfaces;

	//~ Pieces -------------------------------------------------------------------------------------------

	/** The meshes this surface produces, with pick weights. Empty means decals only. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pieces")
	TArray<FDebrisMeshEntry> Meshes;

	/** Uniform scale range for a single, unmerged piece. A merged piece multiplies on top of this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pieces",
		meta = (ClampMin = "0.01", UIMax = "4.0"))
	FVector2D ScaleRange = FVector2D(0.6f, 1.2f);

	/** Materials applied over the mesh's own, by element index. Empty keeps whatever the mesh brought. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pieces")
	TArray<TObjectPtr<UMaterialInterface>> MaterialOverrides;

	/**
	 * Scalar parameter the fade-out is written to, 0 solid and 1 gone.
	 *
	 * A material that has no such parameter simply does not fade, and nothing warns about it every frame -
	 * but the piece then does blink out, which is the one visual defect this plugin set out to avoid, so it
	 * is worth wiring up on the one master material a project uses for rubble.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pieces")
	FName FadeParameterName = TEXT("DebrisFade");

	//~ Physics ------------------------------------------------------------------------------------------

	/** Pieces of this profile collide and simulate. Off makes them decoration that never touches anything. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics")
	bool bCollides = true;

	/** Collision profile used while a piece is live. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics", meta = (EditCondition = "bCollides"))
	FName CollisionProfileName = TEXT("PhysicsActor");

	/**
	 * How long a piece of this profile may simulate before it is frozen where it lies.
	 *
	 * Negative takes the project default from Project Settings, which is what nearly every profile should
	 * do: this is a performance number, and performance numbers belong in one place.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics",
		meta = (UIMin = "-1.0", UIMax = "20.0", ForceUnits = "s"))
	float MaxSimSeconds = -1.0f;

	/** Multiplier on the request's impulse when the piece is launched. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics", meta = (ClampMin = "0.0", UIMax = "10.0"))
	float ImpulseScale = 1.0f;

	/** How far, in degrees, a piece may be launched away from the surface normal. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float LaunchConeAngle = 35.0f;

	/** Random spin given on launch, in degrees per second. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics", meta = (ClampMin = "0.0", UIMax = "2000.0"))
	float MaxAngularVelocity = 360.0f;

	//~ Lifetime -----------------------------------------------------------------------------------------

	/** How long a piece lies around before it fades out on its own. Negative takes the project default. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lifetime",
		meta = (UIMin = "-1.0", UIMax = "120.0", ForceUnits = "s"))
	float LifetimeSeconds = 20.0f;

	/**
	 * How much a piece of this surface matters, 0..1. The first term of the eviction order.
	 *
	 * The useful spread is small and deliberate: bullet chips at 0.3, structural debris at 0.6, anything
	 * scripted at 0.9. Setting everything to 1.0 does not protect anything, it just turns the ranking back
	 * into distance and age.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lifetime", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Importance = 0.5f;

	//~ Decals -------------------------------------------------------------------------------------------

	/** Decal material dropped at the impact point. None means this surface leaves no mark. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decal")
	TObjectPtr<UMaterialInterface> DecalMaterial = nullptr;

	/** Half-size of the decal box, in centimetres. X is the projection depth. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decal", meta = (EditCondition = "DecalMaterial != nullptr"))
	FVector DecalSize = FVector(16.0f, 12.0f, 12.0f);

	/** How long the decal lives, including its fade. Negative takes the project default. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decal",
		meta = (UIMin = "-1.0", UIMax = "120.0", ForceUnits = "s"))
	float DecalLifetimeSeconds = -1.0f;

	//~ Queries ------------------------------------------------------------------------------------------

	/** Pick a mesh by weight. Returns null when the profile has none, which is legal. */
	UFUNCTION(BlueprintCallable, Category = "DebrisDirector")
	UStaticMesh* PickMesh(float Roll01) const;

	/** Total pick weight over all valid entries. Zero when there is nothing to pick. */
	UFUNCTION(BlueprintPure, Category = "DebrisDirector")
	float GetTotalPickWeight() const;
};
