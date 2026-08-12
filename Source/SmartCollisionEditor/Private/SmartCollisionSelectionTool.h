#pragma once

#include "BaseTools/SingleClickTool.h"
#include "CoreMinimal.h"
#include "InteractiveToolBuilder.h"
#include "SmartCollisionSelectionTool.generated.h"

class IStaticMeshEditor;
class UStaticMesh;
class UStaticMeshComponent;
struct FSmartCollisionSelectionGroup;

enum class ESmartCollisionSelectionMode : uint8
{
    Face,
    ConnectedPart
};

UCLASS(Transient)
class USmartCollisionSelectionToolBuilder final : public UInteractiveToolBuilder
{
    GENERATED_BODY()

public:
    void Initialize(TWeakPtr<IStaticMeshEditor> InEditor);

    virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
    virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;

private:
    TWeakPtr<IStaticMeshEditor> Editor;
};

UCLASS(Transient)
class USmartCollisionSelectionTool final : public USingleClickTool
{
    GENERATED_BODY()

public:
    void Initialize(TWeakPtr<IStaticMeshEditor> InEditor);

    virtual void Setup() override;
    virtual void Shutdown(EToolShutdownType ShutdownType) override;
    virtual FInputRayHit IsHitByClick(const FInputDeviceRay& ClickPos) override;
    virtual void OnClicked(const FInputDeviceRay& ClickPos) override;
    virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

    void SetSelectionMode(ESmartCollisionSelectionMode InMode);
    ESmartCollisionSelectionMode GetSelectionMode() const { return SelectionMode; }

    void ClearSelection();
    void SelectAll();
    int32 GetSelectedTriangleCount() const { return SelectedTriangles.Num(); }
    void GetSelectedPoints(TArray<FVector>& OutPoints) const;
    void GetSelectedGroups(
        TArray<FSmartCollisionSelectionGroup>& OutGroups) const;
    void SetSelectionChangedCallback(TFunction<void(int32, int32)> InCallback);

private:
    struct FTriangle
    {
        FVector Vertices[3];
        FVector Normal = FVector::UpVector;
        int32 Component = INDEX_NONE;
        int32 Surface = INDEX_NONE;
    };

    void BuildTriangleCache();
    int32 FindHitTriangle(const FInputDeviceRay& ClickPos, double* OutDistance = nullptr) const;
    void NotifySelectionChanged();
    static FIntVector QuantizePosition(const FVector& Position);

    TWeakPtr<IStaticMeshEditor> Editor;
    TWeakObjectPtr<UStaticMesh> StaticMesh;
    TWeakObjectPtr<UStaticMeshComponent> StaticMeshComponent;
    TArray<FTriangle> Triangles;
    TSet<int32> SelectedTriangles;
    ESmartCollisionSelectionMode SelectionMode = ESmartCollisionSelectionMode::ConnectedPart;
    TFunction<void(int32, int32)> SelectionChangedCallback;
};

namespace SmartCollisionSelection
{
    inline const FString ToolIdentifier(TEXT("SmartCollisionEditor.SelectionTool"));
}
