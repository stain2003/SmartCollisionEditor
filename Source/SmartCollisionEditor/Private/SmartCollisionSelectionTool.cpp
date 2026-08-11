#include "SmartCollisionSelectionTool.h"

#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "IStaticMeshEditor.h"
#include "InteractiveToolManager.h"
#include "PrimitiveDrawInterface.h"
#include "Rendering/StaticMeshRenderData.h"
#include "SlateApplication.h"
#include "ToolContextInterfaces.h"

namespace
{
    struct FTriangleDisjointSet
    {
        explicit FTriangleDisjointSet(int32 Count)
        {
            Parent.SetNumUninitialized(Count);
            Rank.Init(0, Count);
            for (int32 Index = 0; Index < Count; ++Index)
            {
                Parent[Index] = Index;
            }
        }

        int32 Find(int32 Value)
        {
            if (Parent[Value] != Value)
            {
                Parent[Value] = Find(Parent[Value]);
            }
            return Parent[Value];
        }

        void Union(int32 A, int32 B)
        {
            A = Find(A);
            B = Find(B);
            if (A == B)
            {
                return;
            }

            if (Rank[A] < Rank[B])
            {
                Swap(A, B);
            }

            Parent[B] = A;
            if (Rank[A] == Rank[B])
            {
                ++Rank[A];
            }
        }

        TArray<int32> Parent;
        TArray<uint8> Rank;
    };
}

void USmartCollisionSelectionToolBuilder::Initialize(TWeakPtr<IStaticMeshEditor> InEditor)
{
    Editor = InEditor;
}

bool USmartCollisionSelectionToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
    const TSharedPtr<IStaticMeshEditor> PinnedEditor = Editor.Pin();
    return PinnedEditor.IsValid()
        && PinnedEditor->GetStaticMesh() != nullptr
        && PinnedEditor->GetStaticMeshComponent() != nullptr;
}

UInteractiveTool* USmartCollisionSelectionToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
    USmartCollisionSelectionTool* Tool =
        NewObject<USmartCollisionSelectionTool>(SceneState.ToolManager);
    Tool->Initialize(Editor);
    return Tool;
}

void USmartCollisionSelectionTool::Initialize(TWeakPtr<IStaticMeshEditor> InEditor)
{
    Editor = InEditor;
}

void USmartCollisionSelectionTool::Setup()
{
    Super::Setup();

    if (const TSharedPtr<IStaticMeshEditor> PinnedEditor = Editor.Pin())
    {
        StaticMesh = PinnedEditor->GetStaticMesh();
        StaticMeshComponent = PinnedEditor->GetStaticMeshComponent();
    }

    BuildTriangleCache();
    GetToolManager()->DisplayMessage(
        NSLOCTEXT("SmartCollisionEditor", "SelectionToolMessage",
            "Click a face to select it. Connected Part mode selects the complete welded island. Ctrl toggles; Shift adds."),
        EToolMessageLevel::UserNotification);
}

void USmartCollisionSelectionTool::Shutdown(EToolShutdownType ShutdownType)
{
    SelectionChangedCallback = nullptr;
    SelectedTriangles.Reset();
    Triangles.Reset();
    Super::Shutdown(ShutdownType);
}

FIntVector USmartCollisionSelectionTool::QuantizePosition(const FVector& Position)
{
    constexpr double WeldToleranceScale = 100.0; // 0.01 cm
    return FIntVector(
        FMath::RoundToInt(Position.X * WeldToleranceScale),
        FMath::RoundToInt(Position.Y * WeldToleranceScale),
        FMath::RoundToInt(Position.Z * WeldToleranceScale));
}

void USmartCollisionSelectionTool::BuildTriangleCache()
{
    Triangles.Reset();
    SelectedTriangles.Reset();

    UStaticMesh* Mesh = StaticMesh.Get();
    if (!Mesh || !Mesh->HasValidRenderData(true, 0))
    {
        NotifySelectionChanged();
        return;
    }

    const FStaticMeshLODResources& LOD = Mesh->GetRenderData()->LODResources[0];
    const int32 TriangleCount = LOD.IndexBuffer.GetNumIndices() / 3;
    Triangles.SetNum(TriangleCount);

    for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
    {
        FTriangle& Triangle = Triangles[TriangleIndex];
        for (int32 Corner = 0; Corner < 3; ++Corner)
        {
            const uint32 VertexIndex = LOD.IndexBuffer.GetIndex(TriangleIndex * 3 + Corner);
            Triangle.Vertices[Corner] =
                FVector(LOD.VertexBuffers.PositionVertexBuffer.VertexPosition(VertexIndex));
        }
    }

    FTriangleDisjointSet Sets(TriangleCount);
    TMap<FIntVector, int32> FirstTriangleAtPosition;

    for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
    {
        for (const FVector& Vertex : Triangles[TriangleIndex].Vertices)
        {
            const FIntVector Key = QuantizePosition(Vertex);
            if (const int32* Existing = FirstTriangleAtPosition.Find(Key))
            {
                Sets.Union(TriangleIndex, *Existing);
            }
            else
            {
                FirstTriangleAtPosition.Add(Key, TriangleIndex);
            }
        }
    }

    for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
    {
        Triangles[TriangleIndex].Component = Sets.Find(TriangleIndex);
    }

    NotifySelectionChanged();
}

int32 USmartCollisionSelectionTool::FindHitTriangle(
    const FInputDeviceRay& ClickPos,
    double* OutDistance) const
{
    UStaticMeshComponent* Component = StaticMeshComponent.Get();
    if (!Component)
    {
        return INDEX_NONE;
    }

    const FTransform ComponentTransform = Component->GetComponentTransform();
    const FVector LocalOrigin =
        ComponentTransform.InverseTransformPosition(ClickPos.WorldRay.Origin);
    const FVector LocalDirection =
        ComponentTransform.InverseTransformVectorNoScale(ClickPos.WorldRay.Direction).GetSafeNormal();
    const FVector LocalEnd = LocalOrigin + LocalDirection * HALF_WORLD_MAX;

    int32 ClosestTriangle = INDEX_NONE;
    double ClosestDistanceSquared = DBL_MAX;

    for (int32 TriangleIndex = 0; TriangleIndex < Triangles.Num(); ++TriangleIndex)
    {
        const FTriangle& Triangle = Triangles[TriangleIndex];
        FVector Intersection;
        FVector Normal;
        if (FMath::SegmentTriangleIntersection(
            LocalOrigin,
            LocalEnd,
            Triangle.Vertices[0],
            Triangle.Vertices[1],
            Triangle.Vertices[2],
            Intersection,
            Normal))
        {
            const double DistanceSquared = FVector::DistSquared(LocalOrigin, Intersection);
            if (DistanceSquared < ClosestDistanceSquared)
            {
                ClosestDistanceSquared = DistanceSquared;
                ClosestTriangle = TriangleIndex;
            }
        }
    }

    if (OutDistance)
    {
        *OutDistance = ClosestTriangle == INDEX_NONE
            ? DBL_MAX
            : FMath::Sqrt(ClosestDistanceSquared);
    }
    return ClosestTriangle;
}

FInputRayHit USmartCollisionSelectionTool::IsHitByClick(const FInputDeviceRay& ClickPos)
{
    double Distance = DBL_MAX;
    const int32 TriangleIndex = FindHitTriangle(ClickPos, &Distance);
    return TriangleIndex == INDEX_NONE ? FInputRayHit() : FInputRayHit(Distance);
}

void USmartCollisionSelectionTool::OnClicked(const FInputDeviceRay& ClickPos)
{
    const int32 HitTriangle = FindHitTriangle(ClickPos);
    if (HitTriangle == INDEX_NONE)
    {
        return;
    }

    const FModifierKeysState Modifiers = FSlateApplication::Get().GetModifierKeys();
    const bool bToggle = Modifiers.IsControlDown();
    const bool bAdd = Modifiers.IsShiftDown() || bToggle;

    TArray<int32> ClickedSet;
    if (SelectionMode == ESmartCollisionSelectionMode::ConnectedPart)
    {
        const int32 Component = Triangles[HitTriangle].Component;
        for (int32 TriangleIndex = 0; TriangleIndex < Triangles.Num(); ++TriangleIndex)
        {
            if (Triangles[TriangleIndex].Component == Component)
            {
                ClickedSet.Add(TriangleIndex);
            }
        }
    }
    else
    {
        ClickedSet.Add(HitTriangle);
    }

    if (!bAdd)
    {
        SelectedTriangles.Reset();
    }

    bool bRemove = bToggle;
    if (bToggle)
    {
        for (const int32 TriangleIndex : ClickedSet)
        {
            if (!SelectedTriangles.Contains(TriangleIndex))
            {
                bRemove = false;
                break;
            }
        }
    }

    for (const int32 TriangleIndex : ClickedSet)
    {
        if (bRemove)
        {
            SelectedTriangles.Remove(TriangleIndex);
        }
        else
        {
            SelectedTriangles.Add(TriangleIndex);
        }
    }

    NotifySelectionChanged();

    if (const TSharedPtr<IStaticMeshEditor> PinnedEditor = Editor.Pin())
    {
        PinnedEditor->RefreshViewport();
    }
}

void USmartCollisionSelectionTool::Render(IToolsContextRenderAPI* RenderAPI)
{
    Super::Render(RenderAPI);

    UStaticMeshComponent* Component = StaticMeshComponent.Get();
    FPrimitiveDrawInterface* PDI = RenderAPI ? RenderAPI->GetPrimitiveDrawInterface() : nullptr;
    if (!Component || !PDI)
    {
        return;
    }

    const FTransform Transform = Component->GetComponentTransform();
    const FLinearColor SelectedColor(1.0f, 0.55f, 0.0f, 1.0f);

    for (const int32 TriangleIndex : SelectedTriangles)
    {
        if (!Triangles.IsValidIndex(TriangleIndex))
        {
            continue;
        }

        const FTriangle& Triangle = Triangles[TriangleIndex];
        const FVector A = Transform.TransformPosition(Triangle.Vertices[0]);
        const FVector B = Transform.TransformPosition(Triangle.Vertices[1]);
        const FVector C = Transform.TransformPosition(Triangle.Vertices[2]);

        PDI->DrawLine(A, B, SelectedColor, SDPG_Foreground, 2.5f, 0.0f, true);
        PDI->DrawLine(B, C, SelectedColor, SDPG_Foreground, 2.5f, 0.0f, true);
        PDI->DrawLine(C, A, SelectedColor, SDPG_Foreground, 2.5f, 0.0f, true);
    }
}

void USmartCollisionSelectionTool::SetSelectionMode(ESmartCollisionSelectionMode InMode)
{
    SelectionMode = InMode;
}

void USmartCollisionSelectionTool::ClearSelection()
{
    SelectedTriangles.Reset();
    NotifySelectionChanged();

    if (const TSharedPtr<IStaticMeshEditor> PinnedEditor = Editor.Pin())
    {
        PinnedEditor->RefreshViewport();
    }
}

void USmartCollisionSelectionTool::SelectAll()
{
    SelectedTriangles.Reset();
    for (int32 TriangleIndex = 0; TriangleIndex < Triangles.Num(); ++TriangleIndex)
    {
        SelectedTriangles.Add(TriangleIndex);
    }
    NotifySelectionChanged();

    if (const TSharedPtr<IStaticMeshEditor> PinnedEditor = Editor.Pin())
    {
        PinnedEditor->RefreshViewport();
    }
}

void USmartCollisionSelectionTool::GetSelectedPoints(TArray<FVector>& OutPoints) const
{
    OutPoints.Reset();
    TSet<FIntVector> UniquePositions;

    for (const int32 TriangleIndex : SelectedTriangles)
    {
        if (!Triangles.IsValidIndex(TriangleIndex))
        {
            continue;
        }

        for (const FVector& Vertex : Triangles[TriangleIndex].Vertices)
        {
            const FIntVector Key = QuantizePosition(Vertex);
            if (!UniquePositions.Contains(Key))
            {
                UniquePositions.Add(Key);
                OutPoints.Add(Vertex);
            }
        }
    }
}

void USmartCollisionSelectionTool::SetSelectionChangedCallback(
    TFunction<void(int32, int32)> InCallback)
{
    SelectionChangedCallback = MoveTemp(InCallback);
    NotifySelectionChanged();
}

void USmartCollisionSelectionTool::NotifySelectionChanged()
{
    if (!SelectionChangedCallback)
    {
        return;
    }

    TArray<FVector> Points;
    GetSelectedPoints(Points);
    SelectionChangedCallback(SelectedTriangles.Num(), Points.Num());
}
