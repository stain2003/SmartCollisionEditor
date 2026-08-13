#include "SmartCollisionSelectionTool.h"

#include "SmartCollisionGenerator.h"
#include "Engine/StaticMesh.h"
#include "IStaticMeshEditor.h"
#include "InteractiveToolManager.h"
#include "PrimitiveDrawInterface.h"
#include "StaticMeshResources.h"
#include "Framework/Application/SlateApplication.h"
#include "ToolContextInterfaces.h"

#include <cfloat>

namespace
{
    struct FEdgeKey
    {
        FIntVector A;
        FIntVector B;

        friend bool operator==(const FEdgeKey& Left, const FEdgeKey& Right)
        {
            return Left.A == Right.A && Left.B == Right.B;
        }

        friend uint32 GetTypeHash(const FEdgeKey& Key)
        {
            const uint32 HashA = HashCombineFast(
                HashCombineFast(::GetTypeHash(Key.A.X), ::GetTypeHash(Key.A.Y)),
                ::GetTypeHash(Key.A.Z));
            const uint32 HashB = HashCombineFast(
                HashCombineFast(::GetTypeHash(Key.B.X), ::GetTypeHash(Key.B.Y)),
                ::GetTypeHash(Key.B.Z));
            return HashCombineFast(HashA, HashB);
        }
    };

    static bool PositionLess(const FIntVector& A, const FIntVector& B)
    {
        return A.X != B.X ? A.X < B.X
            : A.Y != B.Y ? A.Y < B.Y
            : A.Z < B.Z;
    }

    static FEdgeKey MakeEdgeKey(const FVector& A, const FVector& B)
    {
        constexpr double WeldToleranceScale = 100.0;
        FIntVector QA(
            FMath::RoundToInt(A.X * WeldToleranceScale),
            FMath::RoundToInt(A.Y * WeldToleranceScale),
            FMath::RoundToInt(A.Z * WeldToleranceScale));
        FIntVector QB(
            FMath::RoundToInt(B.X * WeldToleranceScale),
            FMath::RoundToInt(B.Y * WeldToleranceScale),
            FMath::RoundToInt(B.Z * WeldToleranceScale));

        if (PositionLess(QB, QA))
        {
            Swap(QA, QB);
        }
        return {QA, QB};
    }

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
            "Click to add or remove a surface/part. Alt+Click replaces the selection. Multi-selection is fitted per region."),
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

    FTriangleDisjointSet ComponentSets(TriangleCount);
    FTriangleDisjointSet SurfaceSets(TriangleCount);
    TMap<FEdgeKey, int32> FirstTriangleAtEdge;
    constexpr double CoplanarNormalDot = 0.996194698; // five degrees

    for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
    {
        FTriangle& Triangle = Triangles[TriangleIndex];
        Triangle.Normal = FVector::CrossProduct(
            Triangle.Vertices[1] - Triangle.Vertices[0],
            Triangle.Vertices[2] - Triangle.Vertices[0]).GetSafeNormal();

        for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
        {
            const FEdgeKey Edge = MakeEdgeKey(
                Triangle.Vertices[EdgeIndex],
                Triangle.Vertices[(EdgeIndex + 1) % 3]);

            if (const int32* Existing = FirstTriangleAtEdge.Find(Edge))
            {
                ComponentSets.Union(TriangleIndex, *Existing);
                const double NormalDot = FVector::DotProduct(
                    Triangle.Normal,
                    Triangles[*Existing].Normal);
                if (NormalDot >= CoplanarNormalDot)
                {
                    SurfaceSets.Union(TriangleIndex, *Existing);
                }
            }
            else
            {
                FirstTriangleAtEdge.Add(Edge, TriangleIndex);
            }
        }
    }

    for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
    {
        Triangles[TriangleIndex].Component = ComponentSets.Find(TriangleIndex);
        Triangles[TriangleIndex].Surface = SurfaceSets.Find(TriangleIndex);
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
    const bool bReplace = Modifiers.IsAltDown();

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
        const int32 Surface = Triangles[HitTriangle].Surface;
        for (int32 TriangleIndex = 0; TriangleIndex < Triangles.Num(); ++TriangleIndex)
        {
            if (Triangles[TriangleIndex].Surface == Surface)
            {
                ClickedSet.Add(TriangleIndex);
            }
        }
    }

    if (bReplace)
    {
        SelectedTriangles.Reset();
    }

    bool bRemove = !bReplace;
    for (const int32 TriangleIndex : ClickedSet)
    {
        if (!SelectedTriangles.Contains(TriangleIndex))
        {
            bRemove = false;
            break;
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

void USmartCollisionSelectionTool::GetSelectedGroups(
    TArray<FSmartCollisionSelectionGroup>& OutGroups) const
{
    OutGroups.Reset();

    TMap<int32, int32> GroupToOutput;
    const bool bSurfaceMode =
        SelectionMode == ESmartCollisionSelectionMode::Face;

    for (const int32 TriangleIndex : SelectedTriangles)
    {
        if (!Triangles.IsValidIndex(TriangleIndex))
        {
            continue;
        }

        const FTriangle& Triangle = Triangles[TriangleIndex];
        const int32 GroupId =
            bSurfaceMode ? Triangle.Surface : Triangle.Component;

        int32* ExistingIndex = GroupToOutput.Find(GroupId);
        int32 OutputIndex = INDEX_NONE;
        if (ExistingIndex)
        {
            OutputIndex = *ExistingIndex;
        }
        else
        {
            OutputIndex = OutGroups.AddDefaulted();
            GroupToOutput.Add(GroupId, OutputIndex);
            OutGroups[OutputIndex].bSurfacePatch = bSurfaceMode;
        }

        FSmartCollisionSelectionGroup& Group = OutGroups[OutputIndex];
        for (int32 Corner = 0; Corner < 3; ++Corner)
        {
            Group.TriangleVertices.Add(Triangle.Vertices[Corner]);
        }
    }

    for (FSmartCollisionSelectionGroup& Group : OutGroups)
    {
        TSet<FIntVector> UniquePositions;
        for (const FVector& Point : Group.TriangleVertices)
        {
            const FIntVector Key = QuantizePosition(Point);
            if (!UniquePositions.Contains(Key))
            {
                UniquePositions.Add(Key);
                Group.Points.Add(Point);
            }
        }
    }

    OutGroups.RemoveAll(
        [](const FSmartCollisionSelectionGroup& Group)
        {
            return Group.Points.Num() < 3;
        });
}

void USmartCollisionSelectionTool::GetSelectedTriangleIndices(
    TArray<int32>& OutTriangleIndices) const
{
    OutTriangleIndices = SelectedTriangles.Array();
}

void USmartCollisionSelectionTool::SetSelectedTriangleIndices(
    const TArray<int32>& TriangleIndices)
{
    SelectedTriangles.Reset();
    for (const int32 TriangleIndex : TriangleIndices)
    {
        if (Triangles.IsValidIndex(TriangleIndex))
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

