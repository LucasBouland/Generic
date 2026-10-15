using UnrealBuildTool;

public class PEPEquipmentGMAS : ModuleRules
{
	public PEPEquipmentGMAS(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		/*
		 * GMCAbilitySystem brings GMCCore, GameplayTasks and EnhancedInput through its own public
		 * dependencies, which its headers need; nothing here names a GMC symbol directly.
		 * FInstancedStruct (StructUtils) lives in CoreUObject.
		 */
		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"GMCAbilitySystem",
			"PEPCore",
			"PEPEquipment",
			"PEPItemCore"
		});
	}
}
