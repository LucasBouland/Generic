using UnrealBuildTool;

public class PEPItemUI : ModuleRules
{
	public PEPItemUI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "GameplayTags", "UMG", "CommonUI", "PEPCore", "PEPCoreUI", "PEPItemCore" });
		PrivateDependencyModuleNames.AddRange(new[] { "SlateCore" });
	}
}
