using UnrealBuildTool;
public class PEPPickupEditor : ModuleRules
{
    public PEPPickupEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new[] { "UnrealEd", "BlueprintGraph", "KismetCompiler", "PEPInventory", "PEPInventoryUI", "UMG" });
    }
}
