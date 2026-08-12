#pragma once

#include "CoreMinimal.h"

class UStaticMesh;

enum class ESmartCollisionMode : uint8
{
    Automatic,
    OrientedBox,
    Capsule,
    Sphere,
    Convex,
    SurfacePatch
};

struct FSmartCollisionSettings
{
    float Padding = 0.25f;
    float MinimumPartSize = 1.0f;
    int32 MaxShapes = 256;
    int32 MaxConvexVertices = 64;
    bool bReplaceExisting = true;
};

struct FSmartCollisionSelectionGroup
{
    bool bSurfacePatch = false;
    TArray<FVector> Points;
    TArray<FVector> TriangleVertices;
};

struct FSmartCollisionResult
{
    bool bSuccess = false;
    int32 NumComponents = 0;
    int32 NumBoxes = 0;
    int32 NumCapsules = 0;
    int32 NumSpheres = 0;
    int32 NumConvex = 0;
    int32 NumSkipped = 0;
    FString Message;
};

class FSmartCollisionGenerator
{
public:
    static FSmartCollisionResult Generate(
        UStaticMesh* StaticMesh,
        ESmartCollisionMode Mode,
        const FSmartCollisionSettings& Settings);

    static FSmartCollisionResult GenerateFromPoints(
        UStaticMesh* StaticMesh,
        const TArray<FVector>& SelectedPoints,
        ESmartCollisionMode Mode,
        const FSmartCollisionSettings& Settings);

    static FSmartCollisionResult GenerateFromGroups(
        UStaticMesh* StaticMesh,
        const TArray<FSmartCollisionSelectionGroup>& SelectionGroups,
        ESmartCollisionMode Mode,
        const FSmartCollisionSettings& Settings);
};
