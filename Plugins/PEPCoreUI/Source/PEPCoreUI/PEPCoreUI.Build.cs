// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PEPCoreUI : ModuleRules
{
	public PEPCoreUI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"CommonUI",
			"CommonInput",
			"UMG"
		});

	}
}
