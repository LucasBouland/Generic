using UnrealBuildTool;

public class PEPEquipment : ModuleRules
{
	public PEPEquipment(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"NetCore",
			"PEPCore",
			"PEPItemCore"
		});
	}
}
