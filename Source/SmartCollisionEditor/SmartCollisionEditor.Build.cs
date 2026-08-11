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
            "ApplicationCore",
            "AssetRegistry",
            "ContentBrowser",
            "InputCore",
            "InteractiveToolsFramework",
            "LevelEditor",
            "MeshDescription",
            "Slate",
            "SlateCore",
            "StaticMeshDescription",
            "StaticMeshEditor",
            "ToolMenus",
            "UnrealEd"
        });
    }
}
