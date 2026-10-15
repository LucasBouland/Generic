using UnrealBuildTool;

public class PEPItemCoreEditor : ModuleRules
{
	public PEPItemCoreEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"PEPItemCore",
			"GameplayTags"
		});

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"AssetRegistry",
			"InputCore",
			"MessageLog",
			"PropertyEditor",
			"Slate",
			"SlateCore",
			"UnrealEd",
			"WorkspaceMenuStructure"
		});
	}
}
