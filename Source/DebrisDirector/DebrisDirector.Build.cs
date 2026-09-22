// Copyright 2026 Silvan Teufel. All Rights Reserved.

using UnrealBuildTool;

public class DebrisDirector : ModuleRules
{
	public DebrisDirector(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// One runtime module and nothing else.
		//
		// Deliberately NOT here:
		//   Niagara   - a particle has no collision and leaves nothing behind, which is the whole reason this
		//               plugin exists. Forcing the dependency would also force the plugin on projects that
		//               spawn their debris from static meshes, which is most of them.
		//   Chaos     - DebrisDirector does not break anything apart. It never asks how a piece came to exist;
		//               it manages how many of them there are and what they cost. Geometry Collections and
		//               this plugin are complementary, not alternatives.
		//   UMG       - the counter box is drawn on UCanvas from AHUD so it survives a cooked Shipping build.
		//               The demo map's buttons are UMG assets in Content that call the Blueprint library,
		//               exactly as a project would.
		//   UnrealEd  - everything here ships. There is no editor module, so nothing can go missing between
		//               what a designer places in the editor and what the packaged game runs.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"PhysicsCore",
			"DeveloperSettings",
		});

		// RenderCore gives us GWhiteTexture, the one-pixel texture the counter box is tiled from.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RenderCore",
		});
	}
}
