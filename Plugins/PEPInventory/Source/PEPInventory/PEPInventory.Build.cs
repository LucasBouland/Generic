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

        PrivateDependencyModuleNames.AddRange(new string[] { });
    }
}
