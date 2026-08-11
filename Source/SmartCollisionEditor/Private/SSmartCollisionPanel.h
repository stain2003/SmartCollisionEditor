#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class STextBlock;
class UStaticMesh;

class SSmartCollisionPanel final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SSmartCollisionPanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    FReply RefreshSelection();
    FReply GenerateAutomatic();
    FReply GenerateBox();
    FReply GenerateCapsule();
    FReply GenerateConvex();
    FReply ClearCollision();

    void Generate(uint8 ModeValue);
    void SetStatus(const FString& Message);
    UStaticMesh* FindSelectedStaticMesh() const;

    TWeakObjectPtr<UStaticMesh> SelectedMesh;
    TSharedPtr<STextBlock> SelectedMeshText;
    TSharedPtr<STextBlock> StatusText;

    float Padding = 0.25f;
    float MinimumPartSize = 1.0f;
    int32 MaxConvexVertices = 64;
    bool bReplaceExisting = true;
};
