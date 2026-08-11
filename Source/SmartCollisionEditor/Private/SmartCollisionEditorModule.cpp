#include "SmartCollisionEditorModule.h"

#include "SSmartCollisionPanel.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Docking/TabManager.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "FSmartCollisionEditorModule"

namespace
{
    const FName SmartCollisionTabName(TEXT("SmartCollisionEditor"));
}

void FSmartCollisionEditorModule::StartupModule()
{
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        SmartCollisionTabName,
        FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&)
        {
            return SNew(SDockTab)
                .TabRole(ETabRole::NomadTab)
                [
                    SNew(SSmartCollisionPanel)
                ];
        }))
        .SetDisplayName(LOCTEXT("TabTitle", "Smart Collision Editor"))
        .SetTooltipText(LOCTEXT("TabTooltip", "Generate collision for connected Static Mesh parts."));

    UToolMenus::RegisterStartupCallback(
        FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FSmartCollisionEditorModule::RegisterMenus));
}

void FSmartCollisionEditorModule::ShutdownModule()
{
    UToolMenus::UnRegisterStartupCallback(this);
    UToolMenus::UnregisterOwner(this);
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(SmartCollisionTabName);
}

void FSmartCollisionEditorModule::RegisterMenus()
{
    FToolMenuOwnerScoped OwnerScoped(this);

    UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
    FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("Tools"));

    Section.AddMenuEntry(
        TEXT("OpenSmartCollisionEditor"),
        LOCTEXT("MenuLabel", "Smart Collision Editor"),
        LOCTEXT("MenuTooltip", "Open the connected-part collision fitting tool."),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateLambda([]
        {
            FGlobalTabmanager::Get()->TryInvokeTab(SmartCollisionTabName);
        })));
}

IMPLEMENT_MODULE(FSmartCollisionEditorModule, SmartCollisionEditor)

#undef LOCTEXT_NAMESPACE
