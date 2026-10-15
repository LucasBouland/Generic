using UnrealBuildTool;

public class PEPInventoryUI : ModuleRules
{
	public PEPInventoryUI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core", "CoreUObject", "Engine", "DeveloperSettings", "GameplayTags", "UMG", "PEPCore", "PEPCoreUI", "PEPItemCore", "PEPItemUI", "PEPInventory", "PEPPlayer"
		});
		PrivateDependencyModuleNames.AddRange(new[] { "SlateCore", "InputCore" });
	}
}
