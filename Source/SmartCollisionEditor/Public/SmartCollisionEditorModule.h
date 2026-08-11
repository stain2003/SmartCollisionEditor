#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FExtender;
class FTabManager;
class FUICommandList;
class IStaticMeshEditor;

class FSmartCollisionEditorModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    void RegisterMenus();
    void HandleStaticMeshEditorOpened(TWeakPtr<IStaticMeshEditor> Editor);
    TSharedRef<FExtender> ExtendStaticMeshEditorToolbar(
        const TSharedRef<FUICommandList> CommandList,
        TSharedRef<IStaticMeshEditor> Editor);

    FDelegateHandle EditorOpenedHandle;
    FDelegateHandle ToolbarExtenderHandle;
};
