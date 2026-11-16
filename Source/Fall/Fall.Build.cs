// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Fall : ModuleRules
{
	public Fall(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"SlateCore",
			"ProceduralMeshComponent", "Niagara",
			"Json",
			"AudioExtensions"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"Fall",
			"Fall/Freefall",
			"Fall/Variant_Platforming",
			"Fall/Variant_Platforming/Animation",
			"Fall/Variant_Combat",
			"Fall/Variant_Combat/AI",
			"Fall/Variant_Combat/Animation",
			"Fall/Variant_Combat/Gameplay",
			"Fall/Variant_Combat/Interfaces",
			"Fall/Variant_Combat/UI",
			"Fall/Variant_SideScrolling",
			"Fall/Variant_SideScrolling/AI",
			"Fall/Variant_SideScrolling/Gameplay",
			"Fall/Variant_SideScrolling/Interfaces",
			"Fall/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
