using UnrealBuildTool;

public class PEPInteractionGMAS : ModuleRules
{
	public PEPInteractionGMAS(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		/*
		 * FInstancedStruct lives in CoreUObject (StructUtils/InstancedStruct.h), as in GMAS itself.
		 * GMC and EnhancedInput headers reached through GMAS headers come from its public dependencies.
		 */
		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"GameplayTasks",
			"GMCAbilitySystem",
			"PEPInteraction"
		});
	}
}
