using UnrealBuildTool;

public class PEPEquipmentUI : ModuleRules
{
	public PEPEquipmentUI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"PEPCoreUI",
			"PEPItemCore"
		});
	}
}
