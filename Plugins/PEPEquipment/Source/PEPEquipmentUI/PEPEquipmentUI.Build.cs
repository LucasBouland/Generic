using UnrealBuildTool;

public class PEPEquipmentUI : ModuleRules
{
	public PEPEquipmentUI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		/*
		 * Public: the slot widget derives from UCommonUserWidget (CommonUI, UMG), and its headers
		 * expose Equipment types, PEPItemUI's FPEPItemViewData and PEPCoreUI's drop target state.
		 */
		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"UMG",
			"CommonUI",
			"PEPCore",
			"PEPCoreUI",
			"PEPItemCore",
			"PEPItemUI",
			"PEPEquipment"
		});
	}
}
