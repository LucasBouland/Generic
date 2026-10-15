using UnrealBuildTool;

public class PEPInventory : ModuleRules
{
    public PEPInventory(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new[]
            {
                "Core",
                "CoreUObject",
                "DeveloperSettings",
                "Engine",
                "GameplayTags",
                "NetCore",
                "PEPCore",
                "PEPItemCore"
            });

        /* PEPLoadoutMutation asks the asset registry whether its initial scan is still running. */
        PrivateDependencyModuleNames.AddRange(new string[] { "AssetRegistry" });
    }
}
