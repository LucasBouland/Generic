using UnrealBuildTool;

public class PEPItemCore : ModuleRules
{
	public PEPItemCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"DeveloperSettings",
			"Engine",
			"GameplayTags",
			"PEPCore"
		});

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"AssetRegistry"
		});
	}
}
