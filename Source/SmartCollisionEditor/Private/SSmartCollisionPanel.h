#pragma once

#include "CoreMinimal.h"
#include "SmartCollisionGenerator.h"
#include "Widgets/SCompoundWidget.h"

class IStaticMeshEditor;
class STextBlock;
class USmartCollisionSelectionTool;
enum class ESmartCollisionSelectionMode : uint8;

class SSmartCollisionPanel final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SSmartCollisionPanel) {}
        SLATE_ARGUMENT(TWeakPtr<IStaticMeshEditor>, StaticMeshEditor)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual ~SSmartCollisionPanel() override;

private:
    FReply StartPicking();
    FReply StopPicking();
    FReply ClearSelection();
    FReply SelectAll();
    FReply GenerateAutomatic();
    FReply GenerateBox();
    FReply GenerateCapsule();
    FReply GenerateSphere();
    FReply GenerateConvex();
    FReply GenerateSurfacePatch();
    FReply ClearCollision();

    void SetSelectionMode(ESmartCollisionSelectionMode Mode);
    ECheckBoxState IsSelectionModeChecked(ESmartCollisionSelectionMode Mode) const;
    void Generate(ESmartCollisionMode Mode);
    void CacheSelectionFromTool();
    void SetStatus(const FString& Message);
    void UpdateSelectionSummary(int32 TriangleCount, int32 PointCount);

    bool EnsureSelectionToolRegistered();
    class UInteractiveToolManager* GetToolManager() const;
    USmartCollisionSelectionTool* GetSelectionTool() const;

    TWeakPtr<IStaticMeshEditor> StaticMeshEditor;
    TWeakObjectPtr<class USmartCollisionSelectionToolBuilder> SelectionToolBuilder;
    TSharedPtr<STextBlock> SelectionText;
    TSharedPtr<STextBlock> StatusText;
    TArray<int32> CachedSelectedTriangleIndices;
    TArray<FSmartCollisionSelectionGroup> CachedSelectionGroups;

    ESmartCollisionSelectionMode SelectionMode;
    float Padding = 0.25f;
    int32 MaxConvexVertices = 64;
    bool bReplaceExisting = false;
    bool bMergeSelection = false;
};

