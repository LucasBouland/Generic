// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PEPPlayer : ModuleRules
{
	public PEPPlayer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GMCCore",
			"EnhancedInput",
			"InputCore",
			"UMG"
		});

		PrivateDependencyModuleNames.AddRange(new[] { "ApplicationCore", "Slate", "SlateCore" });
	}
}
