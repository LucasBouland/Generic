// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PEPUtility : ModuleRules
{
	public PEPUtility(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"PEPCore"
		});
	}
}
