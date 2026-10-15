using UnrealBuildTool;

public class PEPInteractionTests : ModuleRules
{
	public PEPInteractionTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core"
		});

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"CoreUObject",
			"DeveloperSettings",
			"Engine",
			"FunctionalTesting",
			"GameplayTags",
			"PEPCore",
			"PEPInteraction"
		});
	}
}
