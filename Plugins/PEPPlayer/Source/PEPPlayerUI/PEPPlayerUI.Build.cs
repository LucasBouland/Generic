// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PEPPlayerUI : ModuleRules
{
	public PEPPlayerUI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"CommonUI",
			"InputCore",
			"UMG",
			"PEPPlayer",
			"PEPCoreUI"
		});

		PrivateDependencyModuleNames.AddRange(new[] { "Slate", "SlateCore" });
	}
}
