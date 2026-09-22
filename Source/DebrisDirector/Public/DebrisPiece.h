// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/ObjectPtr.h"
#include "DebrisPiece.generated.h"

class UDebrisProfile;
class UMaterialInstanceDynamic;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * One piece of debris: a static mesh that can simulate, be frozen, fade out and be handed back to a pool.
 *
 * An actor rather than an instanced-static-mesh slot, and that is a decision worth stating. Instancing wins
 * on draw calls and loses everything else here: an ISM instance cannot simulate physics, cannot carry its
 * own dynamic material for a fade, and cannot be collided with. Debris that neither bounces nor can be shot
 * off a ledge is a decal with extra steps. The cost that instancing would have saved is instead removed by
 * the two rules this plugin is built on - the piece stops simulating on a timer, and the population never
 * grows past a ceiling - and both of those are visible on the counter box.
 *
 * Blueprintable so a project can subclass it for a sound on impact or a trail, but it is never required to:
 * the director spawns this class directly unless a profile says otherwise.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Debris Piece"))
class DEBRISDIRECTOR_API ADebrisPiece : public AActor
{
	GENERATED_BODY()

public:
	ADebrisPiece();

	/** The mesh, and the only component. Everything a piece does, it does through this. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DebrisDirector")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	/**
	 * Take this piece out of the pool and make it a live piece of the given profile.
	 *
	 * Everything that varies between profiles is set here rather than in a constructor, which is what lets
	 * one pool serve a stone chip on one frame and a metal shell on the next without allocating either.
	 */
	void ActivateFor(UDebrisProfile* Profile, UStaticMesh* Mesh, const FTransform& Transform,
		const FVector& Impulse, bool bSimulate);

	/**
	 * Stop simulating and stay exactly where it is.
	 *
	 * The whole point of the expiry date. It is safe to call on a piece that is already frozen, because the
	 * director calls it once per state change and a state machine that only works when driven perfectly is
	 * a state machine that will be driven imperfectly.
	 */
	void FreezePhysics();

	/** Write the fade parameter, 0 solid and 1 gone. Does nothing when the profile has no such parameter. */
	void SetFadeAmount(float Alpha);

	/** Hide it, take it off collision and the tick, and park it. What a pool return does. */
	void ParkForPool(bool bMoveAway, const FVector& ParkLocation);

	/** The profile this piece was last activated for, or null while it is parked. */
	const UDebrisProfile* GetActiveProfile() const { return ActiveProfile; }

private:
	/** Kept only to know which fade parameter to write and whether a fade is wanted at all. */
	UPROPERTY(Transient)
	TObjectPtr<UDebrisProfile> ActiveProfile = nullptr;

	/** Made on first activation per material slot and then reused, because a MID per piece per spawn is not free. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> FadeMaterials;

	/** The parent materials the current MIDs were built from, so a profile change rebuilds them and nothing else does. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> FadeMaterialParents;

	/** Last value written to the fade parameter, so an unchanged fade costs no material update at all. */
	float LastFadeAmount = -1.0f;
};
