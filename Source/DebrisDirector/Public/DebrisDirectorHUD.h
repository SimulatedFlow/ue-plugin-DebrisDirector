// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "DebrisDirectorHUD.generated.h"

class UCanvas;

/**
 * The counter box for DebrisDirector, drawn on UCanvas from AHUD.
 *
 * A budget plugin without visible numbers is unprovable on a screenshot. Everything this plugin claims -
 * that a frame never creates more than the budget, that nothing over budget is lost, that the population
 * holds at the ceiling, that the simulating count falls to zero while the pieces stay lying there - is a
 * number, and all of them are on this box beside the ceiling they are measured against.
 *
 * Canvas rather than UMG, and there are deliberately no buttons on it. Two reasons pulling the same way:
 *
 *   - It has to survive a cooked Shipping build. DrawDebug is compiled out there and a debug widget is
 *     usually stripped; a Canvas overlay is not.
 *
 *   - Anything that has to be *clicked* belongs in UMG instead. An AHUD hit box is tested against
 *     UGameViewportClient::GetMousePosition(), which reports nothing on a machine with no mouse attached -
 *     a capture rig, a build agent, a headless test - so the click never lands. The numbers live here,
 *     where they cost nothing and always draw; controls live in a widget, where they always get the click.
 *
 * A project that already has a HUD class does not have to reparent it: turn on bAutoDrawStatsOnAnyHUD in
 * Project Settings and the same box is drawn through AHUD::OnHUDPostRender instead. The two paths know
 * about each other and cannot stack.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Debris Director HUD"))
class DEBRISDIRECTOR_API ADebrisDirectorHUD : public AHUD
{
	GENERATED_BODY()

public:
	ADebrisDirectorHUD();

	//~ AActor interface
	virtual void BeginPlay() override;

	//~ AHUD interface
	virtual void DrawHUD() override;

	/** Draw the counter box. Toggled from Blueprint, from a widget button, or with Debris.Show. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DebrisDirector|HUD")
	bool bShowStats = true;

	/** Top-left corner of the counter box, in pixels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DebrisDirector|HUD")
	FVector2D StatsBoxOrigin = FVector2D(28.0f, 90.0f);

	/** Width of the counter box, in pixels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DebrisDirector|HUD", meta = (ClampMin = "260.0"))
	float StatsBoxWidth = 470.0f;

	/** Flip the counter box on and off. This is what a STATS button in a widget calls. */
	UFUNCTION(BlueprintCallable, Category = "DebrisDirector|HUD")
	void ToggleStats();
};
