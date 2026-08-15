// Generates primitive, convex, decomposed, and through-surface collision for static meshes.
#include "SmartCollisionGenerator.h"

#include "ConvexDecompTool.h"
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

    static FPart MakeAxisAlignedPart(const TArray<FVector>& Points)
    {
        FPart Part;
        Part.Points = Points;
        Part.Centroid = FVector::ZeroVector;
        for (const FVector& Point : Points)
        {
            Part.Centroid += Point;
        }
        Part.Centroid /= static_cast<double>(Points.Num());

        Part.Axis[0] = FVector::ForwardVector;
        Part.Axis[1] = FVector::RightVector;
        Part.Axis[2] = FVector::UpVector;
        Part.MinProjection = FVector(DBL_MAX, DBL_MAX, DBL_MAX);
        Part.MaxProjection = FVector(-DBL_MAX, -DBL_MAX, -DBL_MAX);

        for (const FVector& Point : Points)
        {
            const FVector D = Point - Part.Centroid;
            Part.MinProjection.X = FMath::Min(Part.MinProjection.X, D.X);
            Part.MinProjection.Y = FMath::Min(Part.MinProjection.Y, D.Y);
            Part.MinProjection.Z = FMath::Min(Part.MinProjection.Z, D.Z);
            Part.MaxProjection.X = FMath::Max(Part.MaxProjection.X, D.X);
            Part.MaxProjection.Y = FMath::Max(Part.MaxProjection.Y, D.Y);
            Part.MaxProjection.Z = FMath::Max(Part.MaxProjection.Z, D.Z);
        }

        Part.Extents = (Part.MaxProjection - Part.MinProjection) * 0.5;
        Part.Center = Part.Centroid
            + (Part.MaxProjection + Part.MinProjection) * 0.5;
        return Part;
    }

    static double PaddedBoxVolume(const FPart& Part, float Padding)
    {
        return FMath::Max(0.1, Part.Extents.X * 2.0 + Padding * 2.0)
            * FMath::Max(0.1, Part.Extents.Y * 2.0 + Padding * 2.0)
            * FMath::Max(0.1, Part.Extents.Z * 2.0 + Padding * 2.0);
    }

    static FPart ChooseTighterBoxPart(const FPart& PrincipalPart, float Padding)
    {
        const FPart AxisAligned = MakeAxisAlignedPart(PrincipalPart.Points);
        return PaddedBoxVolume(AxisAligned, Padding)
                < PaddedBoxVolume(PrincipalPart, Padding)
            ? AxisAligned
            : PrincipalPart;
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
        const FPart BoxPart = ChooseTighterBoxPart(Part, Padding);

        FKBoxElem Box;
        Box.Center = BoxPart.Center;
        Box.Rotation =
            FRotationMatrix::MakeFromXY(
                BoxPart.Axis[0],
                BoxPart.Axis[1]).Rotator();
        Box.X = FMath::Max(
            0.1,
            BoxPart.Extents.X * 2.0 + Padding * 2.0);
        Box.Y = FMath::Max(
            0.1,
            BoxPart.Extents.Y * 2.0 + Padding * 2.0);
        Box.Z = FMath::Max(
            0.1,
            BoxPart.Extents.Z * 2.0 + Padding * 2.0);
        BodySetup->AggGeom.BoxElems.Add(Box);
    }

    static void AddCapsule(
        UBodySetup* BodySetup,
        const FPart& Part,
        float Padding)
    {
        const FVector Axis = Part.Axis[0].GetSafeNormal();
        double MinimumAxis = DBL_MAX;
        double MaximumAxis = -DBL_MAX;
        double Radius = 0.0;

        for (const FVector& Point : Part.Points)
        {
            const FVector D = Point - Part.Centroid;
            const double AxisDistance = FVector::DotProduct(D, Axis);
            MinimumAxis = FMath::Min(MinimumAxis, AxisDistance);
            MaximumAxis = FMath::Max(MaximumAxis, AxisDistance);
            Radius = FMath::Max(
                Radius,
                (D - Axis * AxisDistance).Length());
        }

        Radius = FMath::Max(0.05, Radius + Padding);
        const double TotalLength =
            MaximumAxis - MinimumAxis + Padding * 2.0;

        FKSphylElem Capsule;
        Capsule.Center = Part.Centroid
            + Axis * ((MinimumAxis + MaximumAxis) * 0.5);
        Capsule.Rotation = FRotationMatrix::MakeFromZ(Axis).Rotator();
        Capsule.Radius = Radius;
        Capsule.Length = FMath::Max(0.0, TotalLength - Radius * 2.0);
        BodySetup->AggGeom.SphylElems.Add(Capsule);
    }

    static void AddSphere(
        UBodySetup* BodySetup,
        const FPart& Part,
        float Padding)
    {
        int32 ExtremeIndices[6] = {};
        for (int32 Index = 1; Index < Part.Points.Num(); ++Index)
        {
            const FVector& Point = Part.Points[Index];
            if (Point.X < Part.Points[ExtremeIndices[0]].X) ExtremeIndices[0] = Index;
            if (Point.X > Part.Points[ExtremeIndices[1]].X) ExtremeIndices[1] = Index;
            if (Point.Y < Part.Points[ExtremeIndices[2]].Y) ExtremeIndices[2] = Index;
            if (Point.Y > Part.Points[ExtremeIndices[3]].Y) ExtremeIndices[3] = Index;
            if (Point.Z < Part.Points[ExtremeIndices[4]].Z) ExtremeIndices[4] = Index;
            if (Point.Z > Part.Points[ExtremeIndices[5]].Z) ExtremeIndices[5] = Index;
        }

        int32 PairA = ExtremeIndices[0];
        int32 PairB = ExtremeIndices[1];
        double LargestDistance = FVector::DistSquared(
            Part.Points[PairA],
            Part.Points[PairB]);

        for (int32 AxisIndex = 1; AxisIndex < 3; ++AxisIndex)
        {
            const int32 A = ExtremeIndices[AxisIndex * 2];
            const int32 B = ExtremeIndices[AxisIndex * 2 + 1];
            const double Distance = FVector::DistSquared(
                Part.Points[A],
                Part.Points[B]);
            if (Distance > LargestDistance)
            {
                LargestDistance = Distance;
                PairA = A;
                PairB = B;
            }
        }

        FVector Center =
            (Part.Points[PairA] + Part.Points[PairB]) * 0.5;
        double Radius = FMath::Sqrt(LargestDistance) * 0.5;

        for (const FVector& Point : Part.Points)
        {
            const double Distance = FVector::Distance(Point, Center);
            if (Distance > Radius && Distance > UE_DOUBLE_SMALL_NUMBER)
            {
                const double NewRadius = (Radius + Distance) * 0.5;
                Center += (Point - Center)
                    * ((NewRadius - Radius) / Distance);
                Radius = NewRadius;
            }
        }

        FKSphereElem Sphere;
        Sphere.Center = Center;
        Sphere.Radius = FMath::Max(0.05, Radius + Padding);
        BodySetup->AggGeom.SphereElems.Add(Sphere);
    }

    struct FSurfaceRayTriangle
    {
        FVector Vertices[3];
    };

    static TArray<FVector> ReduceConvexPoints(
        const TArray<FVector>& Points,
        int32 MaxPoints);

    static void BuildSurfaceRayTriangles(
        UStaticMesh* StaticMesh,
        TArray<FSurfaceRayTriangle>& OutTriangles)
    {
        OutTriangles.Reset();

        const FMeshDescription* MeshDescription =
            StaticMesh ? StaticMesh->GetMeshDescription(0) : nullptr;
        if (!MeshDescription)
        {
            return;
        }

        const FStaticMeshConstAttributes Attributes(*MeshDescription);
        const TVertexAttributesConstRef<FVector3f> Positions =
            Attributes.GetVertexPositions();
        OutTriangles.Reserve(MeshDescription->Triangles().Num());

        for (const FTriangleID TriangleID :
             MeshDescription->Triangles().GetElementIDs())
        {
            const TArrayView<const FVertexID> Vertices =
                MeshDescription->GetTriangleVertices(TriangleID);
            if (Vertices.Num() != 3)
            {
                continue;
            }

            FSurfaceRayTriangle& Triangle =
                OutTriangles.AddDefaulted_GetRef();
            for (int32 Corner = 0; Corner < 3; ++Corner)
            {
                Triangle.Vertices[Corner] =
                    FVector(Positions[Vertices[Corner]]);
            }
        }
    }

    static bool FindRayTriangleDistance(
        const FVector& RayOrigin,
        const FVector& RayDirection,
        const FSurfaceRayTriangle& Triangle,
        double& OutDistance)
    {
        const FVector EdgeAB =
            Triangle.Vertices[1] - Triangle.Vertices[0];
        const FVector EdgeAC =
            Triangle.Vertices[2] - Triangle.Vertices[0];
        const FVector DirectionCrossEdgeAC =
            FVector::CrossProduct(RayDirection, EdgeAC);
        const double Determinant =
            FVector::DotProduct(EdgeAB, DirectionCrossEdgeAC);
        constexpr double IntersectionTolerance = SMALL_NUMBER;

        if (FMath::Abs(Determinant) <= IntersectionTolerance)
        {
            return false;
        }

        const double InverseDeterminant = 1.0 / Determinant;
        const FVector OriginFromA =
            RayOrigin - Triangle.Vertices[0];
        const double BarycentricB =
            FVector::DotProduct(OriginFromA, DirectionCrossEdgeAC)
            * InverseDeterminant;
        if (BarycentricB < -IntersectionTolerance
            || BarycentricB > 1.0 + IntersectionTolerance)
        {
            return false;
        }

        const FVector OriginCrossEdgeAB =
            FVector::CrossProduct(OriginFromA, EdgeAB);
        const double BarycentricC =
            FVector::DotProduct(RayDirection, OriginCrossEdgeAB)
            * InverseDeterminant;
        if (BarycentricC < -IntersectionTolerance
            || BarycentricB + BarycentricC
                > 1.0 + IntersectionTolerance)
        {
            return false;
        }

        OutDistance =
            FVector::DotProduct(EdgeAC, OriginCrossEdgeAB)
            * InverseDeterminant;
        return OutDistance > IntersectionTolerance;
    }

    static bool FindOppositeSurfaceDepth(
        const TArray<FSurfaceRayTriangle>& MeshTriangles,
        const FVector& SurfacePoint,
        const FVector& InwardDirection,
        double& OutDepth)
    {
        constexpr double SurfaceOffset = 0.01;
        constexpr double MinimumOppositeDistance = 0.01;
        const FVector RayOrigin =
            SurfacePoint + InwardDirection * SurfaceOffset;
        double ClosestDistance = DBL_MAX;

        for (const FSurfaceRayTriangle& Triangle : MeshTriangles)
        {
            double Distance = DBL_MAX;
            if (FindRayTriangleDistance(
                    RayOrigin,
                    InwardDirection,
                    Triangle,
                    Distance)
                && Distance >= MinimumOppositeDistance
                && Distance < ClosestDistance)
            {
                ClosestDistance = Distance;
            }
        }

        if (ClosestDistance == DBL_MAX)
        {
            return false;
        }

        OutDepth = SurfaceOffset + ClosestDistance;
        return true;
    }

    static FVector ComputeSurfaceNormal(
        const FSmartCollisionSelectionGroup& Group)
    {
        FVector Normal = FVector::ZeroVector;
        for (int32 Index = 0;
             Index + 2 < Group.TriangleVertices.Num();
             Index += 3)
        {
            Normal += FVector::CrossProduct(
                Group.TriangleVertices[Index + 1]
                    - Group.TriangleVertices[Index],
                Group.TriangleVertices[Index + 2]
                    - Group.TriangleVertices[Index]);
        }
        return Normal.GetSafeNormal();
    }

    struct FSurfaceHullPoint
    {
        FVector Position;
        FVector2D Projected;
    };

    static double Cross2D(
        const FVector2D& O,
        const FVector2D& A,
        const FVector2D& B)
    {
        return (A.X - O.X) * (B.Y - O.Y)
            - (A.Y - O.Y) * (B.X - O.X);
    }

    static bool BuildSurfaceHull(
        const FSmartCollisionSelectionGroup& Group,
        TArray<FVector>& OutHull,
        FVector& OutNormal)
    {
        OutHull.Reset();
        OutNormal = FVector::ZeroVector;

        OutNormal = ComputeSurfaceNormal(Group);
        if (OutNormal.IsNearlyZero() || Group.Points.Num() < 3)
        {
            return false;
        }

        FVector BasisX = FVector::CrossProduct(
            FMath::Abs(OutNormal.Z) < 0.9
                ? FVector::UpVector
                : FVector::RightVector,
            OutNormal).GetSafeNormal();
        const FVector BasisY =
            FVector::CrossProduct(OutNormal, BasisX).GetSafeNormal();

        TArray<FSurfaceHullPoint> Points;
        Points.Reserve(Group.Points.Num());
        for (const FVector& Point : Group.Points)
        {
            FSurfaceHullPoint& HullPoint =
                Points.AddDefaulted_GetRef();
            HullPoint.Position = Point;
            HullPoint.Projected = FVector2D(
                FVector::DotProduct(Point, BasisX),
                FVector::DotProduct(Point, BasisY));
        }

        Points.Sort(
            [](const FSurfaceHullPoint& A, const FSurfaceHullPoint& B)
            {
                return A.Projected.X != B.Projected.X
                    ? A.Projected.X < B.Projected.X
                    : A.Projected.Y < B.Projected.Y;
            });

        TArray<int32> HullIndices;
        HullIndices.Reserve(Points.Num() * 2);

        for (int32 Index = 0; Index < Points.Num(); ++Index)
        {
            while (HullIndices.Num() >= 2
                && Cross2D(
                    Points[HullIndices[HullIndices.Num() - 2]].Projected,
                    Points[HullIndices.Last()].Projected,
                    Points[Index].Projected) <= 0.0)
            {
                HullIndices.Pop(EAllowShrinking::No);
            }
            HullIndices.Add(Index);
        }

        const int32 LowerCount = HullIndices.Num();
        for (int32 Index = Points.Num() - 2; Index >= 0; --Index)
        {
            while (HullIndices.Num() > LowerCount
                && Cross2D(
                    Points[HullIndices[HullIndices.Num() - 2]].Projected,
                    Points[HullIndices.Last()].Projected,
                    Points[Index].Projected) <= 0.0)
            {
                HullIndices.Pop(EAllowShrinking::No);
            }
            HullIndices.Add(Index);
        }

        if (HullIndices.Num() > 1)
        {
            HullIndices.Pop(EAllowShrinking::No);
        }
        if (HullIndices.Num() < 3)
        {
            return false;
        }

        double HullAreaTwice = 0.0;
        for (int32 Index = 0; Index < HullIndices.Num(); ++Index)
        {
            const FVector2D& A =
                Points[HullIndices[Index]].Projected;
            const FVector2D& B =
                Points[HullIndices[
                    (Index + 1) % HullIndices.Num()]].Projected;
            HullAreaTwice += A.X * B.Y - A.Y * B.X;
        }
        HullAreaTwice = FMath::Abs(HullAreaTwice);

        double TriangleAreaTwice = 0.0;
        for (int32 Index = 0;
             Index + 2 < Group.TriangleVertices.Num();
             Index += 3)
        {
            const FVector2D A(
                FVector::DotProduct(
                    Group.TriangleVertices[Index],
                    BasisX),
                FVector::DotProduct(
                    Group.TriangleVertices[Index],
                    BasisY));
            const FVector2D B(
                FVector::DotProduct(
                    Group.TriangleVertices[Index + 1],
                    BasisX),
                FVector::DotProduct(
                    Group.TriangleVertices[Index + 1],
                    BasisY));
            const FVector2D C(
                FVector::DotProduct(
                    Group.TriangleVertices[Index + 2],
                    BasisX),
                FVector::DotProduct(
                    Group.TriangleVertices[Index + 2],
                    BasisY));
            TriangleAreaTwice += FMath::Abs(Cross2D(A, B, C));
        }

        if (HullAreaTwice <= UE_DOUBLE_SMALL_NUMBER
            || TriangleAreaTwice / HullAreaTwice < 0.96)
        {
            return false;
        }

        OutHull.Reserve(HullIndices.Num());
        for (const int32 Index : HullIndices)
        {
            OutHull.Add(Points[Index].Position);
        }
        return true;
    }

    static void AddThroughConvexFromPoints(
        UBodySetup* BodySetup,
        const TArray<FVector>& SurfacePoints,
        const FVector& Normal,
        const TArray<FSurfaceRayTriangle>& MeshTriangles,
        double FallbackDepth)
    {
        if (SurfacePoints.IsEmpty() || Normal.IsNearlyZero())
        {
            return;
        }

        FVector SurfaceCenter = FVector::ZeroVector;
        for (const FVector& Point : SurfacePoints)
        {
            SurfaceCenter += Point;
        }
        SurfaceCenter /= static_cast<double>(SurfacePoints.Num());

        const FVector InwardDirection = Normal.GetSafeNormal();
        double CenterDepth = FallbackDepth;
        const bool bHasCenterDepth = FindOppositeSurfaceDepth(
            MeshTriangles,
            SurfaceCenter,
            InwardDirection,
            CenterDepth);

        FKConvexElem Convex;
        FVector BottomCenter = FVector::ZeroVector;
        Convex.VertexData.Reserve(SurfacePoints.Num() * 2);
        for (const FVector& Point : SurfacePoints)
        {
            const FVector SamplePoint =
                FMath::Lerp(Point, SurfaceCenter, 0.02);
            double PointDepth = FallbackDepth;
            if (!FindOppositeSurfaceDepth(
                    MeshTriangles,
                    SamplePoint,
                    InwardDirection,
                    PointDepth)
                && bHasCenterDepth)
            {
                PointDepth = CenterDepth;
            }

            const FVector BackPoint =
                Point + InwardDirection * PointDepth;
            Convex.VertexData.Add(Point);
            Convex.VertexData.Add(BackPoint);
            BottomCenter += BackPoint;
        }

        BottomCenter /= static_cast<double>(SurfacePoints.Num());
        for (FVector& Vertex : Convex.VertexData)
        {
            Vertex -= BottomCenter;
        }
        Convex.SetTransform(FTransform(BottomCenter));
        Convex.UpdateElemBox();
        BodySetup->AggGeom.ConvexElems.Add(MoveTemp(Convex));
    }

    static int32 AddSurfacePatch(
        UBodySetup* BodySetup,
        const FSmartCollisionSelectionGroup& Group,
        float FallbackThickness,
        int32 ShapeBudget,
        const TArray<FSurfaceRayTriangle>& MeshTriangles,
        int32 MaxConvexVertices,
        bool bForceSingleHull)
    {
        if (ShapeBudget <= 0)
        {
            return 0;
        }

        const double FallbackDepth =
            FMath::Max(0.1, static_cast<double>(FallbackThickness));

        TArray<FVector> ConvexHull;
        FVector SurfaceNormal;
        if (BuildSurfaceHull(Group, ConvexHull, SurfaceNormal))
        {
            ConvexHull = ReduceConvexPoints(
                ConvexHull,
                FMath::Clamp(MaxConvexVertices / 2, 4, 128));
            AddThroughConvexFromPoints(
                BodySetup,
                ConvexHull,
                SurfaceNormal,
                MeshTriangles,
                FallbackDepth);
            return 1;
        }

        if (bForceSingleHull)
        {
            SurfaceNormal = ComputeSurfaceNormal(Group);
            if (SurfaceNormal.IsNearlyZero())
            {
                return 0;
            }

            const TArray<FVector> ReducedPoints = ReduceConvexPoints(
                Group.Points,
                FMath::Clamp(MaxConvexVertices / 2, 4, 128));
            AddThroughConvexFromPoints(
                BodySetup,
                ReducedPoints,
                SurfaceNormal,
                MeshTriangles,
                FallbackDepth);
            return 1;
        }

        const int32 TriangleCount = Group.TriangleVertices.Num() / 3;
        int32 Added = 0;
        for (int32 TriangleIndex = 0;
             TriangleIndex < TriangleCount && Added < ShapeBudget;
             ++TriangleIndex)
        {
            const FVector A =
                Group.TriangleVertices[TriangleIndex * 3];
            const FVector B =
                Group.TriangleVertices[TriangleIndex * 3 + 1];
            const FVector C =
                Group.TriangleVertices[TriangleIndex * 3 + 2];
            const FVector Normal =
                FVector::CrossProduct(B - A, C - A).GetSafeNormal();

            if (Normal.IsNearlyZero())
            {
                continue;
            }

            TArray<FVector> Triangle;
            Triangle.Reserve(3);
            Triangle.Add(A);
            Triangle.Add(B);
            Triangle.Add(C);
            AddThroughConvexFromPoints(
                BodySetup,
                Triangle,
                Normal,
                MeshTriangles,
                FallbackDepth);
            ++Added;
        }

        return Added;
    }

    static bool BuildIndexedMesh(
        const FSmartCollisionSelectionGroup& Group,
        TArray<FVector3f>& OutVertices,
        TArray<uint32>& OutIndices)
    {
        OutVertices.Reset();
        OutIndices.Reset();

        TMap<FIntVector, uint32> PositionToIndex;
        constexpr double PositionScale = 100.0;

        for (const FVector& Point : Group.TriangleVertices)
        {
            const FIntVector Key(
                FMath::RoundToInt(Point.X * PositionScale),
                FMath::RoundToInt(Point.Y * PositionScale),
                FMath::RoundToInt(Point.Z * PositionScale));

            uint32* ExistingIndex = PositionToIndex.Find(Key);
            if (ExistingIndex)
            {
                OutIndices.Add(*ExistingIndex);
                continue;
            }

            const uint32 NewIndex =
                static_cast<uint32>(OutVertices.Num());
            PositionToIndex.Add(Key, NewIndex);
            OutVertices.Add(FVector3f(Point));
            OutIndices.Add(NewIndex);
        }

        return OutVertices.Num() >= 4
            && OutIndices.Num() >= 12
            && OutIndices.Num() % 3 == 0;
    }

    static int32 AddMultiConvex(
        UBodySetup* BodySetup,
        const FSmartCollisionSelectionGroup& Group,
        const FSmartCollisionSettings& Settings)
    {
        TArray<FVector3f> Vertices;
        TArray<uint32> Indices;
        if (!BuildIndexedMesh(Group, Vertices, Indices))
        {
            return 0;
        }

        const FKAggregateGeom SavedGeometry = BodySetup->AggGeom;
        BodySetup->AggGeom.EmptyElements();
        DecomposeMeshToHulls(
            BodySetup,
            Vertices,
            Indices,
            static_cast<uint32>(
                FMath::Clamp(Settings.MaxConvexHulls, 1, 64)),
            FMath::Clamp(Settings.MaxConvexVertices, 4, 256),
            static_cast<uint32>(FMath::Clamp(
                Settings.ConvexDecompositionResolution,
                10000,
                16000000)));

        TArray<FKConvexElem> GeneratedHulls =
            MoveTemp(BodySetup->AggGeom.ConvexElems);
        BodySetup->AggGeom = SavedGeometry;

        for (FKConvexElem& Hull : GeneratedHulls)
        {
            BodySetup->AggGeom.ConvexElems.Add(MoveTemp(Hull));
        }
        return GeneratedHulls.Num();
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
        float Padding,
        int32 MaxVertices)
    {
        TArray<FVector> ExpandedPoints = Part.Points;
        if (Padding > 0.0f)
        {
            for (FVector& Point : ExpandedPoints)
            {
                Point += (Point - Part.Centroid).GetSafeNormal()
                    * Padding;
            }
        }

        FKConvexElem Convex;
        Convex.VertexData =
            ReduceConvexPoints(ExpandedPoints, MaxVertices);
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
        const double HalfThickness = FMath::Max(
            0.05,
            static_cast<double>(RequestedThickness) * 0.5);
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
                Settings.Padding,
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
        Settings.bMergeSelection ? AddedShapes : Result.NumComponents,
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
    FSmartCollisionSelectionGroup Group;
    Group.Points = SelectedPoints;

    TArray<FSmartCollisionSelectionGroup> Groups;
    Groups.Add(MoveTemp(Group));
    return GenerateFromGroups(StaticMesh, Groups, Mode, Settings);
}

FSmartCollisionResult FSmartCollisionGenerator::GenerateFromGroups(
    UStaticMesh* StaticMesh,
    const TArray<FSmartCollisionSelectionGroup>& SelectionGroups,
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
    if (SelectionGroups.IsEmpty())
    {
        Result.Message = TEXT("Select at least one surface or connected part.");
        return Result;
    }

    TArray<FSmartCollisionSelectionGroup> GroupsToGenerate;
    if (Settings.bMergeSelection)
    {
        FSmartCollisionSelectionGroup& MergedGroup =
            GroupsToGenerate.AddDefaulted_GetRef();
        MergedGroup.bSurfacePatch =
            SelectionGroups[0].bSurfacePatch;

        for (const FSmartCollisionSelectionGroup& Group : SelectionGroups)
        {
            for (const FVector& Point : Group.Points)
            {
                MergedGroup.Points.AddUnique(Point);
            }
            MergedGroup.TriangleVertices.Append(Group.TriangleVertices);
        }
    }
    else
    {
        GroupsToGenerate = SelectionGroups;
    }

    const FScopedTransaction Transaction(
        LOCTEXT(
            "GenerateSelectedTransaction",
            "Generate Collision From Selected Geometry"));
    StaticMesh->Modify();

    if (!StaticMesh->GetBodySetup())
    {
        StaticMesh->CreateBodySetup();
    }

    UBodySetup* BodySetup = StaticMesh->GetBodySetup();
    if (!BodySetup)
    {
        Result.Message =
            TEXT("Unable to create BodySetup for the Static Mesh.");
        return Result;
    }

    BodySetup->Modify();
    if (Settings.bReplaceExisting)
    {
        BodySetup->AggGeom.EmptyElements();
    }

    int32 AddedShapes = 0;
    Result.NumComponents = SelectionGroups.Num();
    TArray<FSurfaceRayTriangle> SurfaceRayTriangles;
    bool bSurfaceRayTrianglesBuilt = false;

    for (const FSmartCollisionSelectionGroup& Group : GroupsToGenerate)
    {
        if (AddedShapes >= Settings.MaxShapes)
        {
            ++Result.NumSkipped;
            continue;
        }
        if (Group.Points.Num() < 3)
        {
            ++Result.NumSkipped;
            continue;
        }

        ESmartCollisionMode EffectiveMode = Mode;
        if (Mode == ESmartCollisionMode::Automatic)
        {
            EffectiveMode = Group.bSurfacePatch
                ? ESmartCollisionMode::SurfacePatch
                : ESmartCollisionMode::Automatic;
        }

        if (EffectiveMode == ESmartCollisionMode::MultiConvex
            && Group.bSurfacePatch)
        {
            EffectiveMode = ESmartCollisionMode::SurfacePatch;
        }

        if (EffectiveMode == ESmartCollisionMode::SurfacePatch)
        {
            if (!bSurfaceRayTrianglesBuilt)
            {
                BuildSurfaceRayTriangles(
                    StaticMesh,
                    SurfaceRayTriangles);
                bSurfaceRayTrianglesBuilt = true;
            }

            const int32 AddedSurfaceShapes = AddSurfacePatch(
                BodySetup,
                Group,
                Settings.Padding,
                Settings.bMergeSelection
                    ? 1
                    : Settings.MaxShapes - AddedShapes,
                SurfaceRayTriangles,
                FMath::Clamp(
                    Settings.MaxConvexVertices,
                    8,
                    256),
                Settings.bMergeSelection);
            AddedShapes += AddedSurfaceShapes;
            Result.NumConvex += AddedSurfaceShapes;
            if (AddedSurfaceShapes == 0)
            {
                ++Result.NumSkipped;
            }
            continue;
        }

        FPart Part;
        Part.Points = Group.Points;
        ComputePrincipalAxes(Part);

        if (EffectiveMode == ESmartCollisionMode::Automatic)
        {
            EffectiveMode = ChooseAutomaticMode(Part);
        }

        switch (EffectiveMode)
        {
        case ESmartCollisionMode::MultiConvex:
        {
            const int32 AddedHulls =
                AddMultiConvex(BodySetup, Group, Settings);
            AddedShapes += AddedHulls;
            Result.NumConvex += AddedHulls;
            if (AddedHulls == 0)
            {
                ++Result.NumSkipped;
            }
            continue;
        }

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
                Settings.Padding,
                FMath::Clamp(Settings.MaxConvexVertices, 8, 256));
            ++Result.NumConvex;
            break;

        case ESmartCollisionMode::Automatic:
        case ESmartCollisionMode::SurfacePatch:
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
        Result.Message =
            TEXT("No collision could be generated from the selection.");
        return Result;
    }

    BodySetup->InvalidatePhysicsData();
    BodySetup->CreatePhysicsMeshes();
    StaticMesh->SetCustomizedCollision(true);
    StaticMesh->MarkPackageDirty();
    StaticMesh->PostEditChange();

    Result.bSuccess = true;
    if (Settings.bMergeSelection)
    {
        Result.Message = FString::Printf(
            TEXT("Merged %d selected regions into %d collision shape: %d boxes, %d capsules, %d spheres, %d convex/surface hulls. Skipped %d. Save the Static Mesh asset to keep the result."),
            Result.NumComponents,
            AddedShapes,
            Result.NumBoxes,
            Result.NumCapsules,
            Result.NumSpheres,
            Result.NumConvex,
            Result.NumSkipped);
    }
    else
    {
        Result.Message = FString::Printf(
            TEXT("Generated %d collision shapes from %d selected regions: %d boxes, %d capsules, %d spheres, %d convex/surface prisms. Skipped %d. Save the Static Mesh asset to keep the result."),
            AddedShapes,
            Result.NumComponents,
            Result.NumBoxes,
            Result.NumCapsules,
            Result.NumSpheres,
            Result.NumConvex,
            Result.NumSkipped);
    }
    return Result;
}

#undef LOCTEXT_NAMESPACE

