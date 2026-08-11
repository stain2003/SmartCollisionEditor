#include "SmartCollisionGenerator.h"

#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/BodySetup.h"
#include "ScopedTransaction.h"
#include "StaticMeshAttributes.h"

#include <cfloat>

#define LOCTEXT_NAMESPACE "SmartCollisionGenerator"

namespace SmartCollision
{
    struct FDisjointSet
    {
        explicit FDisjointSet(int32 Count)
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
            int32 Root = Value;
            while (Parent[Root] != Root)
            {
                Root = Parent[Root];
            }

            while (Parent[Value] != Value)
            {
                const int32 Next = Parent[Value];
                Parent[Value] = Root;
                Value = Next;
            }
            return Root;
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

    struct FPart
    {
        TArray<FVector> Points;
        FVector Centroid = FVector::ZeroVector;
        FVector Axis[3] = {FVector::ForwardVector, FVector::RightVector, FVector::UpVector};
        FVector MinProjection = FVector::ZeroVector;
        FVector MaxProjection = FVector::ZeroVector;
        FVector Center = FVector::ZeroVector;
        FVector Extents = FVector::ZeroVector;
    };

    static void ComputePrincipalAxes(FPart& Part)
    {
        Part.Centroid = FVector::ZeroVector;
        for (const FVector& Point : Part.Points)
        {
            Part.Centroid += Point;
        }
        Part.Centroid /= static_cast<double>(Part.Points.Num());

        double Matrix[3][3] = {};
        for (const FVector& Point : Part.Points)
        {
            const FVector D = Point - Part.Centroid;
            Matrix[0][0] += D.X * D.X;
            Matrix[0][1] += D.X * D.Y;
            Matrix[0][2] += D.X * D.Z;
            Matrix[1][1] += D.Y * D.Y;
            Matrix[1][2] += D.Y * D.Z;
            Matrix[2][2] += D.Z * D.Z;
        }

        Matrix[1][0] = Matrix[0][1];
        Matrix[2][0] = Matrix[0][2];
        Matrix[2][1] = Matrix[1][2];

        double Eigenvectors[3][3] =
        {
            {1.0, 0.0, 0.0},
            {0.0, 1.0, 0.0},
            {0.0, 0.0, 1.0}
        };

        for (int32 Iteration = 0; Iteration < 32; ++Iteration)
        {
            int32 P = 0;
            int32 Q = 1;
            double Largest = FMath::Abs(Matrix[0][1]);

            if (FMath::Abs(Matrix[0][2]) > Largest)
            {
                P = 0;
                Q = 2;
                Largest = FMath::Abs(Matrix[0][2]);
            }
            if (FMath::Abs(Matrix[1][2]) > Largest)
            {
                P = 1;
                Q = 2;
                Largest = FMath::Abs(Matrix[1][2]);
            }
            if (Largest < UE_DOUBLE_SMALL_NUMBER)
            {
                break;
            }

            const double Angle = 0.5 * FMath::Atan2(
                2.0 * Matrix[P][Q],
                Matrix[Q][Q] - Matrix[P][P]);
            const double C = FMath::Cos(Angle);
            const double S = FMath::Sin(Angle);

            for (int32 K = 0; K < 3; ++K)
            {
                if (K == P || K == Q)
                {
                    continue;
                }

                const double MKP = Matrix[K][P];
                const double MKQ = Matrix[K][Q];
                Matrix[K][P] = Matrix[P][K] = C * MKP - S * MKQ;
                Matrix[K][Q] = Matrix[Q][K] = S * MKP + C * MKQ;
            }

            const double MPP = Matrix[P][P];
            const double MQQ = Matrix[Q][Q];
            const double MPQ = Matrix[P][Q];
            Matrix[P][P] = C * C * MPP - 2.0 * S * C * MPQ + S * S * MQQ;
            Matrix[Q][Q] = S * S * MPP + 2.0 * S * C * MPQ + C * C * MQQ;
            Matrix[P][Q] = Matrix[Q][P] = 0.0;

            for (int32 K = 0; K < 3; ++K)
            {
                const double VKP = Eigenvectors[K][P];
                const double VKQ = Eigenvectors[K][Q];
                Eigenvectors[K][P] = C * VKP - S * VKQ;
                Eigenvectors[K][Q] = S * VKP + C * VKQ;
            }
        }

        int32 Order[3] = {0, 1, 2};
        if (Matrix[Order[0]][Order[0]] < Matrix[Order[1]][Order[1]]) Swap(Order[0], Order[1]);
        if (Matrix[Order[1]][Order[1]] < Matrix[Order[2]][Order[2]]) Swap(Order[1], Order[2]);
        if (Matrix[Order[0]][Order[0]] < Matrix[Order[1]][Order[1]]) Swap(Order[0], Order[1]);

        for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
        {
            const int32 Column = Order[AxisIndex];
            Part.Axis[AxisIndex] = FVector(
                Eigenvectors[0][Column],
                Eigenvectors[1][Column],
                Eigenvectors[2][Column]).GetSafeNormal();
        }

        Part.Axis[2] = FVector::CrossProduct(Part.Axis[0], Part.Axis[1]).GetSafeNormal();
        Part.Axis[1] = FVector::CrossProduct(Part.Axis[2], Part.Axis[0]).GetSafeNormal();

        Part.MinProjection = FVector(DBL_MAX, DBL_MAX, DBL_MAX);
        Part.MaxProjection = FVector(-DBL_MAX, -DBL_MAX, -DBL_MAX);

        for (const FVector& Point : Part.Points)
        {
            const FVector D = Point - Part.Centroid;
            for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
            {
                const double Projection = FVector::DotProduct(D, Part.Axis[AxisIndex]);
                Part.MinProjection[AxisIndex] = FMath::Min(Part.MinProjection[AxisIndex], Projection);
                Part.MaxProjection[AxisIndex] = FMath::Max(Part.MaxProjection[AxisIndex], Projection);
            }
        }

        Part.Extents = (Part.MaxProjection - Part.MinProjection) * 0.5;
        const FVector LocalCenter = (Part.MaxProjection + Part.MinProjection) * 0.5;
        Part.Center = Part.Centroid
            + Part.Axis[0] * LocalCenter.X
            + Part.Axis[1] * LocalCenter.Y
            + Part.Axis[2] * LocalCenter.Z;
    }

    static bool BuildParts(UStaticMesh* StaticMesh, TArray<FPart>& OutParts, FString& OutError)
    {
        if (!StaticMesh)
        {
            OutError = TEXT("No Static Mesh selected.");
            return false;
        }

        const FMeshDescription* MeshDescription = StaticMesh->GetMeshDescription(0);
        if (!MeshDescription)
        {
            OutError = TEXT("LOD0 MeshDescription is unavailable. Reimport the mesh with editor data enabled.");
            return false;
        }

        const FStaticMeshConstAttributes Attributes(*MeshDescription);
        const TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();
        FDisjointSet Sets(MeshDescription->Vertices().GetArraySize());

        for (const FTriangleID TriangleID : MeshDescription->Triangles().GetElementIDs())
        {
            const TArrayView<const FVertexID> Vertices = MeshDescription->GetTriangleVertices(TriangleID);
            if (Vertices.Num() == 3)
            {
                Sets.Union(Vertices[0].GetValue(), Vertices[1].GetValue());
                Sets.Union(Vertices[1].GetValue(), Vertices[2].GetValue());
            }
        }

        TMap<int32, int32> RootToPart;
        for (const FVertexID VertexID : MeshDescription->Vertices().GetElementIDs())
        {
            const int32 Root = Sets.Find(VertexID.GetValue());
            int32* ExistingIndex = RootToPart.Find(Root);
            int32 PartIndex = INDEX_NONE;

            if (ExistingIndex)
            {
                PartIndex = *ExistingIndex;
            }
            else
            {
                PartIndex = OutParts.AddDefaulted();
                RootToPart.Add(Root, PartIndex);
            }

            OutParts[PartIndex].Points.Add(FVector(Positions[VertexID]));
        }

        for (FPart& Part : OutParts)
        {
            if (Part.Points.Num() >= 4)
            {
                ComputePrincipalAxes(Part);
            }
        }

        OutParts.RemoveAll([](const FPart& Part)
        {
            return Part.Points.Num() < 4;
        });

        if (OutParts.IsEmpty())
        {
            OutError = TEXT("No valid connected parts were found in LOD0.");
            return false;
        }

        return true;
    }

    static void AddOrientedBox(
        UBodySetup* BodySetup,
        const FPart& Part,
        float Padding)
    {
        FKBoxElem Box;
        Box.Center = Part.Center;
        Box.Rotation = FRotationMatrix::MakeFromXY(Part.Axis[0], Part.Axis[1]).Rotator();
        Box.X = FMath::Max(0.1, Part.Extents.X * 2.0 + Padding * 2.0);
        Box.Y = FMath::Max(0.1, Part.Extents.Y * 2.0 + Padding * 2.0);
        Box.Z = FMath::Max(0.1, Part.Extents.Z * 2.0 + Padding * 2.0);
        BodySetup->AggGeom.BoxElems.Add(Box);
    }

    static void AddCapsule(
        UBodySetup* BodySetup,
        const FPart& Part,
        float Padding)
    {
        const double Radius = FMath::Max(Part.Extents.Y, Part.Extents.Z) + Padding;
        const double TotalLength = Part.Extents.X * 2.0 + Padding * 2.0;

        FKSphylElem Capsule;
        Capsule.Center = Part.Center;
        Capsule.Rotation = FRotationMatrix::MakeFromZ(Part.Axis[0]).Rotator();
        Capsule.Radius = FMath::Max(0.05, Radius);
        Capsule.Length = FMath::Max(0.0, TotalLength - Capsule.Radius * 2.0);
        BodySetup->AggGeom.SphylElems.Add(Capsule);
    }

    static void AddSphere(
        UBodySetup* BodySetup,
        const FPart& Part,
        float Padding)
    {
        FKSphereElem Sphere;
        Sphere.Center = Part.Center;

        double Radius = 0.0;
        for (const FVector& Point : Part.Points)
        {
            Radius = FMath::Max(Radius, FVector::Distance(Point, Part.Center));
        }

        Sphere.Radius = FMath::Max(0.05, Radius + Padding);
        BodySetup->AggGeom.SphereElems.Add(Sphere);
    }

    static TArray<FVector> ReduceConvexPoints(const TArray<FVector>& Points, int32 MaxPoints)
    {
        if (Points.Num() <= MaxPoints)
        {
            return Points;
        }

        FVector Centroid = FVector::ZeroVector;
        for (const FVector& Point : Points)
        {
            Centroid += Point;
        }
        Centroid /= static_cast<double>(Points.Num());

        TArray<FVector> Selected;
        Selected.Reserve(MaxPoints);

        int32 FirstIndex = 0;
        double GreatestDistance = -1.0;
        for (int32 Index = 0; Index < Points.Num(); ++Index)
        {
            const double Distance = FVector::DistSquared(Points[Index], Centroid);
            if (Distance > GreatestDistance)
            {
                GreatestDistance = Distance;
                FirstIndex = Index;
            }
        }
        Selected.Add(Points[FirstIndex]);

        while (Selected.Num() < MaxPoints)
        {
            int32 BestIndex = INDEX_NONE;
            double BestMinimumDistance = -1.0;

            for (int32 PointIndex = 0; PointIndex < Points.Num(); ++PointIndex)
            {
                double MinimumDistance = DBL_MAX;
                for (const FVector& Existing : Selected)
                {
                    MinimumDistance = FMath::Min(
                        MinimumDistance,
                        FVector::DistSquared(Points[PointIndex], Existing));
                }

                if (MinimumDistance > BestMinimumDistance)
                {
                    BestMinimumDistance = MinimumDistance;
                    BestIndex = PointIndex;
                }
            }

            if (BestIndex == INDEX_NONE || BestMinimumDistance <= UE_DOUBLE_SMALL_NUMBER)
            {
                break;
            }
            Selected.Add(Points[BestIndex]);
        }

        return Selected;
    }

    static void AddConvex(
        UBodySetup* BodySetup,
        const FPart& Part,
        int32 MaxVertices)
    {
        FKConvexElem Convex;
        Convex.VertexData = ReduceConvexPoints(Part.Points, MaxVertices);
        Convex.UpdateElemBox();
        BodySetup->AggGeom.ConvexElems.Add(MoveTemp(Convex));
    }

    static bool ShouldUseCapsule(const FPart& Part)
    {
        const double CrossSectionMax = FMath::Max(Part.Extents.Y, Part.Extents.Z);
        const double CrossSectionMin = FMath::Max(
            0.001,
            FMath::Min(Part.Extents.Y, Part.Extents.Z));

        const bool bLong = Part.Extents.X >= CrossSectionMax * 2.8;
        const bool bRoundEnough = CrossSectionMax / CrossSectionMin <= 1.6;
        return bLong && bRoundEnough;
    }

    static bool ShouldUseSphere(const FPart& Part)
    {
        const double Largest = FMath::Max(0.001, Part.Extents.X);
        const double Smallest = Part.Extents.GetMin();
        return Smallest / Largest >= 0.78;
    }

    static ESmartCollisionMode ChooseAutomaticMode(const FPart& Part)
    {
        if (ShouldUseSphere(Part))
        {
            return ESmartCollisionMode::Sphere;
        }
        if (ShouldUseCapsule(Part))
        {
            return ESmartCollisionMode::Capsule;
        }
        return ESmartCollisionMode::OrientedBox;
    }

    static FPart MakeConvexPartWithThickness(
        const FPart& Part,
        float RequestedThickness)
    {
        const double HalfThickness = FMath::Max(0.05, RequestedThickness);
        if (Part.Extents.Z >= HalfThickness)
        {
            return Part;
        }

        FPart ThickPart;
        ThickPart.Points.Reserve(Part.Points.Num() * 2);
        for (const FVector& Point : Part.Points)
        {
            ThickPart.Points.Add(Point + Part.Axis[2] * HalfThickness);
            ThickPart.Points.Add(Point - Part.Axis[2] * HalfThickness);
        }
        ComputePrincipalAxes(ThickPart);
        return ThickPart;
    }
}

FSmartCollisionResult FSmartCollisionGenerator::Generate(
    UStaticMesh* StaticMesh,
    ESmartCollisionMode Mode,
    const FSmartCollisionSettings& Settings)
{
    using namespace SmartCollision;

    FSmartCollisionResult Result;
    TArray<FPart> Parts;
    FString Error;

    if (!BuildParts(StaticMesh, Parts, Error))
    {
        Result.Message = Error;
        return Result;
    }

    Result.NumComponents = Parts.Num();

    Parts.Sort([](const FPart& A, const FPart& B)
    {
        return A.Points.Num() > B.Points.Num();
    });

    int32 NumEligibleParts = 0;
    for (const FPart& Part : Parts)
    {
        if (Part.Extents.GetMax() * 2.0 >= Settings.MinimumPartSize)
        {
            ++NumEligibleParts;
            if (NumEligibleParts >= Settings.MaxShapes)
            {
                break;
            }
        }
    }

    if (NumEligibleParts == 0)
    {
        Result.Message = TEXT("Every connected part was filtered out. Lower the minimum part size.");
        return Result;
    }

    const FScopedTransaction Transaction(LOCTEXT("GenerateTransaction", "Generate Smart Collision"));
    StaticMesh->Modify();

    if (!StaticMesh->GetBodySetup())
    {
        StaticMesh->CreateBodySetup();
    }

    UBodySetup* BodySetup = StaticMesh->GetBodySetup();
    if (!BodySetup)
    {
        Result.Message = TEXT("Unable to create BodySetup for the Static Mesh.");
        return Result;
    }

    BodySetup->Modify();
    if (Settings.bReplaceExisting)
    {
        BodySetup->AggGeom.EmptyElements();
    }

    int32 AddedShapes = 0;
    for (const FPart& Part : Parts)
    {
        if (AddedShapes >= Settings.MaxShapes)
        {
            ++Result.NumSkipped;
            continue;
        }

        const double LargestSize = Part.Extents.GetMax() * 2.0;
        if (LargestSize < Settings.MinimumPartSize)
        {
            ++Result.NumSkipped;
            continue;
        }

        ESmartCollisionMode EffectiveMode = Mode;
        if (Mode == ESmartCollisionMode::Automatic)
        {
            EffectiveMode = ChooseAutomaticMode(Part);
        }

        switch (EffectiveMode)
        {
        case ESmartCollisionMode::Capsule:
            AddCapsule(BodySetup, Part, Settings.Padding);
            ++Result.NumCapsules;
            break;

        case ESmartCollisionMode::Sphere:
            AddSphere(BodySetup, Part, Settings.Padding);
            ++Result.NumSpheres;
            break;

        case ESmartCollisionMode::Convex:
            AddConvex(
                BodySetup,
                MakeConvexPartWithThickness(Part, Settings.Padding),
                FMath::Clamp(Settings.MaxConvexVertices, 8, 256));
            ++Result.NumConvex;
            break;

        case ESmartCollisionMode::Automatic:
        case ESmartCollisionMode::OrientedBox:
        default:
            AddOrientedBox(BodySetup, Part, Settings.Padding);
            ++Result.NumBoxes;
            break;
        }

        ++AddedShapes;
    }

    if (AddedShapes == 0)
    {
        Result.Message = TEXT("Every connected part was filtered out. Lower the minimum part size.");
        return Result;
    }

    BodySetup->InvalidatePhysicsData();
    BodySetup->CreatePhysicsMeshes();
    StaticMesh->SetCustomizedCollision(true);
    StaticMesh->MarkPackageDirty();
    StaticMesh->PostEditChange();

    Result.bSuccess = true;
    Result.Message = FString::Printf(
        TEXT("Generated %d shapes from %d connected parts: %d boxes, %d capsules, %d spheres, %d convex. Skipped %d. Save the Static Mesh asset to keep the result."),
        AddedShapes,
        Result.NumComponents,
        Result.NumBoxes,
        Result.NumCapsules,
        Result.NumSpheres,
        Result.NumConvex,
        Result.NumSkipped);
    return Result;
}

FSmartCollisionResult FSmartCollisionGenerator::GenerateFromPoints(
    UStaticMesh* StaticMesh,
    const TArray<FVector>& SelectedPoints,
    ESmartCollisionMode Mode,
    const FSmartCollisionSettings& Settings)
{
    using namespace SmartCollision;

    FSmartCollisionResult Result;
    if (!StaticMesh)
    {
        Result.Message = TEXT("The Static Mesh Editor has no active mesh.");
        return Result;
    }
    if (SelectedPoints.Num() < 3)
    {
        Result.Message = TEXT("Select at least one non-degenerate triangle.");
        return Result;
    }

    FPart Part;
    Part.Points = SelectedPoints;
    ComputePrincipalAxes(Part);

    ESmartCollisionMode EffectiveMode =
        Mode == ESmartCollisionMode::Automatic
            ? ChooseAutomaticMode(Part)
            : Mode;

    const FScopedTransaction Transaction(
        LOCTEXT("GenerateSelectedTransaction", "Generate Collision From Selected Geometry"));
    StaticMesh->Modify();

    if (!StaticMesh->GetBodySetup())
    {
        StaticMesh->CreateBodySetup();
    }

    UBodySetup* BodySetup = StaticMesh->GetBodySetup();
    if (!BodySetup)
    {
        Result.Message = TEXT("Unable to create BodySetup for the Static Mesh.");
        return Result;
    }

    BodySetup->Modify();
    if (Settings.bReplaceExisting)
    {
        BodySetup->AggGeom.EmptyElements();
    }

    switch (EffectiveMode)
    {
    case ESmartCollisionMode::Capsule:
        AddCapsule(BodySetup, Part, Settings.Padding);
        Result.NumCapsules = 1;
        break;

    case ESmartCollisionMode::Sphere:
        AddSphere(BodySetup, Part, Settings.Padding);
        Result.NumSpheres = 1;
        break;

    case ESmartCollisionMode::Convex:
        AddConvex(
            BodySetup,
            MakeConvexPartWithThickness(Part, Settings.Padding),
            FMath::Clamp(Settings.MaxConvexVertices, 8, 256));
        Result.NumConvex = 1;
        break;

    case ESmartCollisionMode::Automatic:
    case ESmartCollisionMode::OrientedBox:
    default:
        AddOrientedBox(BodySetup, Part, Settings.Padding);
        Result.NumBoxes = 1;
        break;
    }

    BodySetup->InvalidatePhysicsData();
    BodySetup->CreatePhysicsMeshes();
    StaticMesh->SetCustomizedCollision(true);
    StaticMesh->MarkPackageDirty();
    StaticMesh->PostEditChange();

    Result.bSuccess = true;
    Result.NumComponents = 1;
    const TCHAR* ShapeName =
        EffectiveMode == ESmartCollisionMode::Capsule ? TEXT("capsule") :
        EffectiveMode == ESmartCollisionMode::Sphere ? TEXT("sphere") :
        EffectiveMode == ESmartCollisionMode::Convex ? TEXT("convex hull") :
        TEXT("oriented box");

    Result.Message = FString::Printf(
        TEXT("Added one %s around %d selected points. Save the Static Mesh asset to keep it."),
        ShapeName,
        SelectedPoints.Num());
    return Result;
}

#undef LOCTEXT_NAMESPACE
