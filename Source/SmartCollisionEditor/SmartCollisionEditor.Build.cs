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

        // IStaticMeshEditor is a public editor interface, but some installed-engine
        // configurations do not propagate its include path from a private link
        // dependency. Request the module's public headers explicitly.
        PrivateIncludePathModuleNames.AddRange(new[]
        {
            "StaticMeshEditor"
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
