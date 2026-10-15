using UnrealBuildTool;

public class PEPEquipmentEditor : ModuleRules
{
	public PEPEquipmentEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"DataValidation",
			"Engine",
			"PEPEquipment",
			"PEPItemCore",
			"PEPInventory"
		});
	}
}
