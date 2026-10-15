// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PEPPawn : ModuleRules
{
	public PEPPawn(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTasks",
			"GMCCore",
			"GMCAbilitySystem",
			"GMCMotion"
		});
	}
}
