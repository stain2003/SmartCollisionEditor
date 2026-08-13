#include "SmartCollisionEditorModule.h"

#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IStaticMeshEditor.h"
#include "SSmartCollisionPanel.h"
#include "StaticMeshEditorModule.h"
#include "Styling/AppStyle.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"
#include "ToolMenus.h"
#include "Toolkits/AssetEditorToolkitMenuContext.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "FSmartCollisionEditorModule"

namespace
{
    const FName SmartCollisionTabName(TEXT("StaticMeshEditor_SmartCollision"));

    void OpenSmartCollisionTab(TWeakPtr<IStaticMeshEditor> WeakEditor)
    {
        if (const TSharedPtr<IStaticMeshEditor> Editor = WeakEditor.Pin())
        {
            if (const TSharedPtr<FTabManager> TabManager = Editor->GetTabManager())
            {
                TabManager->TryInvokeTab(SmartCollisionTabName);
            }
        }
    }
}

void FSmartCollisionEditorModule::StartupModule()
{
    UToolMenus::RegisterStartupCallback(
        FSimpleMulticastDelegate::FDelegate::CreateRaw(
            this,
            &FSmartCollisionEditorModule::RegisterMenus));

    IStaticMeshEditorModule& StaticMeshEditorModule =
        FModuleManager::LoadModuleChecked<IStaticMeshEditorModule>(TEXT("StaticMeshEditor"));

    EditorOpenedHandle = StaticMeshEditorModule.OnStaticMeshEditorOpened().AddRaw(
        this,
        &FSmartCollisionEditorModule::HandleStaticMeshEditorOpened);

    IStaticMeshEditorModule::FStaticMeshEditorToolbarExtender ToolbarExtender =
        IStaticMeshEditorModule::FStaticMeshEditorToolbarExtender::CreateRaw(
            this,
            &FSmartCollisionEditorModule::ExtendStaticMeshEditorToolbar);
    ToolbarExtenderHandle = ToolbarExtender.GetHandle();
    StaticMeshEditorModule.GetAllStaticMeshEditorToolbarExtenders().Add(
        MoveTemp(ToolbarExtender));
}

void FSmartCollisionEditorModule::ShutdownModule()
{
    UToolMenus::UnRegisterStartupCallback(this);
    UToolMenus::UnregisterOwner(this);

    if (!FModuleManager::Get().IsModuleLoaded(TEXT("StaticMeshEditor")))
    {
        return;
    }

    IStaticMeshEditorModule& StaticMeshEditorModule =
        FModuleManager::GetModuleChecked<IStaticMeshEditorModule>(TEXT("StaticMeshEditor"));

    StaticMeshEditorModule.OnStaticMeshEditorOpened().Remove(EditorOpenedHandle);
    StaticMeshEditorModule.GetAllStaticMeshEditorToolbarExtenders().RemoveAll(
        [this](const IStaticMeshEditorModule::FStaticMeshEditorToolbarExtender& Extender)
        {
            return Extender.GetHandle() == ToolbarExtenderHandle;
        });
}

void FSmartCollisionEditorModule::RegisterMenus()
{
    FToolMenuOwnerScoped OwnerScoped(this);

    UToolMenu* CollisionMenu =
        UToolMenus::Get()->ExtendMenu(TEXT("StaticMeshEditor.Collision"));
    if (!CollisionMenu)
    {
        return;
    }

    FToolMenuSection& Section = CollisionMenu->AddSection(
        TEXT("SmartCollisionEditor"),
        LOCTEXT("SmartCollisionSection", "Smart Collision"));
    Section.InsertPosition = FToolMenuInsert(
        TEXT("CollisionAutoConvexCollision"),
        EToolMenuInsertType::After);

    Section.AddDynamicEntry(
        TEXT("OpenSmartCollisionDynamic"),
        FNewToolMenuSectionDelegate::CreateLambda(
            [](FToolMenuSection& InSection)
            {
                const UAssetEditorToolkitMenuContext* Context =
                    InSection.FindContext<UAssetEditorToolkitMenuContext>();
                if (!Context)
                {
                    return;
                }

                const TSharedPtr<FAssetEditorToolkit> Toolkit =
                    Context->Toolkit.Pin();
                if (!Toolkit
                    || Toolkit->GetToolkitFName() != FName(TEXT("StaticMeshEditor")))
                {
                    return;
                }

                const TSharedPtr<IStaticMeshEditor> Editor =
                    StaticCastSharedPtr<IStaticMeshEditor>(Toolkit);
                const TWeakPtr<IStaticMeshEditor> WeakEditor = Editor;

                InSection.AddMenuEntry(
                    TEXT("OpenSmartCollision"),
                    LOCTEXT(
                        "OpenSmartCollisionMenuLabel",
                        "Smart Collision (Selected Geometry)"),
                    LOCTEXT(
                        "OpenSmartCollisionMenuTooltip",
                        "Open the embedded panel for selecting mesh parts or surfaces and fitting collision."),
                    FSlateIcon(
                        FAppStyle::GetAppStyleSetName(),
                        TEXT("StaticMeshEditor.SetShowCollision")),
                    FUIAction(FExecuteAction::CreateStatic(
                        &OpenSmartCollisionTab,
                        WeakEditor)));
            }));
}

void FSmartCollisionEditorModule::HandleStaticMeshEditorOpened(
    TWeakPtr<IStaticMeshEditor> WeakEditor)
{
    const TSharedPtr<IStaticMeshEditor> Editor = WeakEditor.Pin();
    if (!Editor)
    {
        return;
    }

    Editor->OnStaticMeshEditorDockingExtentionTabs().AddLambda(
        [](const TSharedRef<FTabManager::FStack>& ExtensionStack)
        {
            ExtensionStack->AddTab(SmartCollisionTabName, ETabState::ClosedTab);
        });

    Editor->OnRegisterTabSpawners().AddLambda(
        [WeakEditor](const TSharedRef<FTabManager>& TabManager)
        {
            TabManager->RegisterTabSpawner(
                SmartCollisionTabName,
                FOnSpawnTab::CreateLambda(
                    [WeakEditor](const FSpawnTabArgs&)
                    {
                        return SNew(SDockTab)
                            .TabRole(ETabRole::PanelTab)
                            [
                                SNew(SSmartCollisionPanel)
                                .StaticMeshEditor(WeakEditor)
                            ];
                    }))
                .SetDisplayName(LOCTEXT("EmbeddedTabTitle", "Smart Collision"))
                .SetTooltipText(LOCTEXT(
                    "EmbeddedTabTooltip",
                    "Select mesh faces or connected parts and fit collision."));
        });

    Editor->OnUnregisterTabSpawners().AddLambda(
        [](const TSharedRef<FTabManager>& TabManager)
        {
            TabManager->UnregisterTabSpawner(SmartCollisionTabName);
        });
}

TSharedRef<FExtender> FSmartCollisionEditorModule::ExtendStaticMeshEditorToolbar(
    const TSharedRef<FUICommandList> CommandList,
    TSharedRef<IStaticMeshEditor> Editor)
{
    const TSharedRef<FExtender> Extender = MakeShared<FExtender>();
    const TWeakPtr<IStaticMeshEditor> WeakEditor = Editor;

    Extender->AddToolBarExtension(
        TEXT("Extensions"),
        EExtensionHook::After,
        CommandList,
        FToolBarExtensionDelegate::CreateLambda(
            [WeakEditor](FToolBarBuilder& ToolbarBuilder)
            {
                ToolbarBuilder.AddToolBarButton(
                    FUIAction(FExecuteAction::CreateStatic(
                        &OpenSmartCollisionTab,
                        WeakEditor)),
                    NAME_None,
                    LOCTEXT("ToolbarLabel", "Smart Collision"),
                    LOCTEXT(
                        "ToolbarTooltip",
                        "Open interactive face and connected-part collision fitting."),
                    FSlateIcon(
                        FAppStyle::GetAppStyleSetName(),
                        TEXT("StaticMeshEditor.SetShowCollision")));
            }));

    return Extender;
}

IMPLEMENT_MODULE(FSmartCollisionEditorModule, SmartCollisionEditor)

#undef LOCTEXT_NAMESPACE
