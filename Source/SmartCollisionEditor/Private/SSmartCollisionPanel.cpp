#include "SSmartCollisionPanel.h"

#include "ContentBrowserModule.h"
#include "Engine/StaticMesh.h"
#include "Framework/Application/SlateApplication.h"
#include "IContentBrowserSingleton.h"
#include "PhysicsEngine/BodySetup.h"
#include "ScopedTransaction.h"
#include "SmartCollisionGenerator.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SSmartCollisionPanel"

void SSmartCollisionPanel::Construct(const FArguments& InArgs)
{
    ChildSlot
    [
        SNew(SBorder)
        .Padding(12.0f)
        [
            SNew(SScrollBox)
            + SScrollBox::Slot()
            [
                SNew(SVerticalBox)

                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("Title", "Smart Collision Editor"))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
                ]

                + SVerticalBox::Slot().AutoHeight().Padding(0, 6)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("Help", "Select one Static Mesh in the Content Browser. LOD0 is split into connected parts, then each part is fitted independently."))
                    .AutoWrapText(true)
                ]

                + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1)
                    [
                        SAssignNew(SelectedMeshText, STextBlock)
                        .Text(LOCTEXT("NoSelection", "Selected mesh: none"))
                    ]
                    + SHorizontalBox::Slot().AutoWidth().Padding(6, 0, 0, 0)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("Refresh", "Use Content Browser Selection"))
                        .OnClicked(this, &SSmartCollisionPanel::RefreshSelection)
                    ]
                ]

                + SVerticalBox::Slot().AutoHeight().Padding(0, 8)
                [
                    SNew(SSeparator)
                ]

                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(STextBlock).Text(LOCTEXT("PaddingLabel", "Collision padding (cm)"))
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 6)
                [
                    SNew(SNumericEntryBox<float>)
                    .MinValue(0.0f)
                    .MaxValue(25.0f)
                    .Value_Lambda([this] { return Padding; })
                    .OnValueChanged_Lambda([this](float Value) { Padding = Value; })
                ]

                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(STextBlock).Text(LOCTEXT("MinSizeLabel", "Ignore parts smaller than (cm)"))
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 6)
                [
                    SNew(SNumericEntryBox<float>)
                    .MinValue(0.0f)
                    .MaxValue(1000.0f)
                    .Value_Lambda([this] { return MinimumPartSize; })
                    .OnValueChanged_Lambda([this](float Value) { MinimumPartSize = Value; })
                ]

                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(STextBlock).Text(LOCTEXT("ConvexVerticesLabel", "Maximum vertices per convex hull"))
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 6)
                [
                    SNew(SNumericEntryBox<int32>)
                    .MinValue(8)
                    .MaxValue(256)
                    .Value_Lambda([this] { return MaxConvexVertices; })
                    .OnValueChanged_Lambda([this](int32 Value) { MaxConvexVertices = Value; })
                ]

                + SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 8)
                [
                    SNew(SCheckBox)
                    .IsChecked_Lambda([this]
                    {
                        return bReplaceExisting ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                    })
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState State)
                    {
                        bReplaceExisting = State == ECheckBoxState::Checked;
                    })
                    [
                        SNew(STextBlock).Text(LOCTEXT("ReplaceExisting", "Replace existing simple collision"))
                    ]
                ]

                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SButton)
                    .Text(LOCTEXT("Auto", "Generate Auto (recommended)"))
                    .ToolTipText(LOCTEXT("AutoTip", "Long round parts become capsules; other parts become oriented boxes."))
                    .OnClicked(this, &SSmartCollisionPanel::GenerateAutomatic)
                ]

                + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("Box", "Oriented Box"))
                        .OnClicked(this, &SSmartCollisionPanel::GenerateBox)
                    ]
                    + SHorizontalBox::Slot().FillWidth(1).Padding(4, 0)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("Capsule", "Capsule"))
                        .OnClicked(this, &SSmartCollisionPanel::GenerateCapsule)
                    ]
                    + SHorizontalBox::Slot().FillWidth(1)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("Convex", "Convex"))
                        .OnClicked(this, &SSmartCollisionPanel::GenerateConvex)
                    ]
                ]

                + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("Clear", "Clear Simple Collision"))
                    .OnClicked(this, &SSmartCollisionPanel::ClearCollision)
                ]

                + SVerticalBox::Slot().AutoHeight().Padding(0, 10)
                [
                    SAssignNew(StatusText, STextBlock)
                    .Text(LOCTEXT("Ready", "Ready."))
                    .AutoWrapText(true)
                ]
            ]
        ]
    ];

    RefreshSelection();
}

UStaticMesh* SSmartCollisionPanel::FindSelectedStaticMesh() const
{
    FContentBrowserModule& ContentBrowserModule =
        FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

    TArray<FAssetData> SelectedAssets;
    ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);

    for (const FAssetData& AssetData : SelectedAssets)
    {
        if (UStaticMesh* Mesh = Cast<UStaticMesh>(AssetData.GetAsset()))
        {
            return Mesh;
        }
    }

    return nullptr;
}

FReply SSmartCollisionPanel::RefreshSelection()
{
    SelectedMesh = FindSelectedStaticMesh();

    if (SelectedMesh.IsValid())
    {
        SelectedMeshText->SetText(FText::Format(
            LOCTEXT("SelectedFormat", "Selected mesh: {0}"),
            FText::FromString(SelectedMesh->GetPathName())));
        SetStatus(TEXT("Static Mesh selected. Choose a generation mode."));
    }
    else
    {
        SelectedMeshText->SetText(LOCTEXT("NoSelection", "Selected mesh: none"));
        SetStatus(TEXT("Select a Static Mesh in the Content Browser."));
    }

    return FReply::Handled();
}

FReply SSmartCollisionPanel::GenerateAutomatic()
{
    Generate(static_cast<uint8>(ESmartCollisionMode::Automatic));
    return FReply::Handled();
}

FReply SSmartCollisionPanel::GenerateBox()
{
    Generate(static_cast<uint8>(ESmartCollisionMode::OrientedBox));
    return FReply::Handled();
}

FReply SSmartCollisionPanel::GenerateCapsule()
{
    Generate(static_cast<uint8>(ESmartCollisionMode::Capsule));
    return FReply::Handled();
}

FReply SSmartCollisionPanel::GenerateConvex()
{
    Generate(static_cast<uint8>(ESmartCollisionMode::Convex));
    return FReply::Handled();
}

void SSmartCollisionPanel::Generate(uint8 ModeValue)
{
    if (!SelectedMesh.IsValid())
    {
        RefreshSelection();
    }

    UStaticMesh* Mesh = SelectedMesh.Get();
    if (!Mesh)
    {
        return;
    }

    FSmartCollisionSettings Settings;
    Settings.Padding = Padding;
    Settings.MinimumPartSize = MinimumPartSize;
    Settings.MaxConvexVertices = MaxConvexVertices;
    Settings.bReplaceExisting = bReplaceExisting;

    const FSmartCollisionResult Result = FSmartCollisionGenerator::Generate(
        Mesh,
        static_cast<ESmartCollisionMode>(ModeValue),
        Settings);

    SetStatus(Result.Message);
}

FReply SSmartCollisionPanel::ClearCollision()
{
    if (!SelectedMesh.IsValid())
    {
        RefreshSelection();
    }

    UStaticMesh* Mesh = SelectedMesh.Get();
    if (!Mesh)
    {
        return FReply::Handled();
    }

    const FScopedTransaction Transaction(LOCTEXT("ClearTransaction", "Clear Smart Collision"));
    Mesh->Modify();

    if (UBodySetup* BodySetup = Mesh->GetBodySetup())
    {
        BodySetup->Modify();
        BodySetup->AggGeom.EmptyElements();
        BodySetup->InvalidatePhysicsData();
        BodySetup->CreatePhysicsMeshes();
        Mesh->MarkPackageDirty();
        Mesh->PostEditChange();
        SetStatus(TEXT("Simple collision cleared."));
    }

    return FReply::Handled();
}

void SSmartCollisionPanel::SetStatus(const FString& Message)
{
    if (StatusText.IsValid())
    {
        StatusText->SetText(FText::FromString(Message));
    }
}

#undef LOCTEXT_NAMESPACE
