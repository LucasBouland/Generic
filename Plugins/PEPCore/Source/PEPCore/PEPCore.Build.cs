// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PEPCore : ModuleRules
{
	public PEPCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"DeveloperSettings"
		});
	}
}
