using UnrealBuildTool;

public class SmartCollisionEditor : ModuleRules
{
    public SmartCollisionEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "AssetRegistry",
            "ContentBrowser",
            "InputCore",
            "LevelEditor",
            "MeshDescription",
            "Slate",
            "SlateCore",
            "StaticMeshDescription",
            "ToolMenus",
            "UnrealEd"
        });
    }
}
