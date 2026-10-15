using UnrealBuildTool;

public class PEPPickup : ModuleRules
{
    public PEPPickup(ReadOnlyTargetRules Target) : base(Target)
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
                "PEPCore",
                "PEPItemCore",
                "PEPInventory",
                "PEPInteraction"
            });

        PrivateDependencyModuleNames.AddRange(new string[] { });
    }
}
