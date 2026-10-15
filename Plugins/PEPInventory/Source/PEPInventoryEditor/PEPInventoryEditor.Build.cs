using UnrealBuildTool;

public class PEPInventoryEditor : ModuleRules
{
	public PEPInventoryEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "GameplayTags", "PEPInventory", "PEPItemCore" });
		PrivateDependencyModuleNames.AddRange(new[]
		{
			"AssetRegistry", "Blutility", "InputCore", "LevelEditor", "MessageLog", "PropertyEditor",
			"Slate", "SlateCore", "ToolMenus", "UMG", "UMGEditor", "UnrealEd"
		});
	}
}
