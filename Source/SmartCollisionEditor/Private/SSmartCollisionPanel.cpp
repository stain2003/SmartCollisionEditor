// Builds the Smart Collision panel, edits convex transforms/origins, and routes selections to generation.
#include "SSmartCollisionPanel.h"

#include "EditorModeManager.h"
#include "Engine/StaticMesh.h"
#include "IStaticMeshEditor.h"
#include "InteractiveToolManager.h"
#include "PhysicsEngine/BodySetup.h"
#include "ScopedTransaction.h"
#include "SmartCollisionGenerator.h"
#include "SmartCollisionSelectionTool.h"
#include "Styling/AppStyle.h"
#include "Tools/EdModeInteractiveToolsContext.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SVectorInputBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SSmartCollisionPanel"

void SSmartCollisionPanel::Construct(const FArguments& InArgs)
{
    StaticMeshEditor = InArgs._StaticMeshEditor;
    SelectionMode = ESmartCollisionSelectionMode::ConnectedPart;

    ChildSlot
    [
        SNew(SBorder)
        .Padding(10.0f)
        [
            SNew(SScrollBox)
            + SScrollBox::Slot()
            [
                SNew(SVerticalBox)

                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("Title", "Interactive Smart Collision"))
                    .Font(FAppStyle::GetFontStyle(TEXT("BoldFont")))
                ]

                + SVerticalBox::Slot().AutoHeight().Padding(0, 6)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT(
                        "Instructions",
                        "1. Start picking. 2. Click to add/remove parts or surfaces; Alt+Click replaces. "
                        "3. Auto extends face selections inward to the opposite mesh surface."))
                    .AutoWrapText(true)
                ]

                + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1)
                    [
                        SNew(SCheckBox)
                        .Style(FAppStyle::Get(), TEXT("RadioButton"))
                        .IsChecked(this, &SSmartCollisionPanel::IsSelectionModeChecked,
                            ESmartCollisionSelectionMode::ConnectedPart)
                        .OnCheckStateChanged_Lambda([this](ECheckBoxState)
                        {
                            SetSelectionMode(ESmartCollisionSelectionMode::ConnectedPart);
                        })
                        [
                            SNew(STextBlock).Text(LOCTEXT("PartMode", "Connected part"))
                        ]
                    ]
                    + SHorizontalBox::Slot().FillWidth(1)
                    [
                        SNew(SCheckBox)
                        .Style(FAppStyle::Get(), TEXT("RadioButton"))
                        .IsChecked(this, &SSmartCollisionPanel::IsSelectionModeChecked,
                            ESmartCollisionSelectionMode::Face)
                        .OnCheckStateChanged_Lambda([this](ECheckBoxState)
                        {
                            SetSelectionMode(ESmartCollisionSelectionMode::Face);
                        })
                        [
                            SNew(STextBlock).Text(LOCTEXT("FaceMode", "Surface / face"))
                        ]
                    ]
                ]

                + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("StartPicking", "Start viewport picking"))
                        .ToolTipText(LOCTEXT(
                            "StartPickingTip",
                            "Activates geometry picking inside this Static Mesh Editor viewport."))
                        .OnClicked(this, &SSmartCollisionPanel::StartPicking)
                    ]
                    + SHorizontalBox::Slot().FillWidth(1).Padding(4, 0, 0, 0)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("StopPicking", "Stop viewport picking"))
                        .ToolTipText(LOCTEXT(
                            "StopPickingTip",
                            "Stops viewport picking but keeps the current selection available for collision generation."))
                        .OnClicked(this, &SSmartCollisionPanel::StopPicking)
                    ]
                ]

                + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("ClearSelection", "Clear selection"))
                        .OnClicked(this, &SSmartCollisionPanel::ClearSelection)
                    ]
                    + SHorizontalBox::Slot().FillWidth(1).Padding(4, 0, 0, 0)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("SelectAll", "Select entire mesh"))
                        .OnClicked(this, &SSmartCollisionPanel::SelectAll)
                    ]
                ]

                + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
                [
                    SAssignNew(SelectionText, STextBlock)
                    .Text(LOCTEXT("NoSelection", "Selection: 0 triangles, 0 points"))
                ]

                + SVerticalBox::Slot().AutoHeight().Padding(0, 6)
                [
                    SNew(SSeparator)
                ]

                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(STextBlock).Text(LOCTEXT("PaddingLabel", "Collision padding / fallback surface depth (cm)"))
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
                    SNew(STextBlock).Text(LOCTEXT("ConvexVertices", "Maximum convex vertices"))
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 6)
                [
                    SNew(SNumericEntryBox<int32>)
                    .MinValue(8)
                    .MaxValue(256)
                    .Value_Lambda([this] { return MaxConvexVertices; })
                    .OnValueChanged_Lambda([this](int32 Value) { MaxConvexVertices = Value; })
                ]

                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(STextBlock).Text(LOCTEXT(
                        "MaxConvexHulls",
                        "Smart decomposition: maximum hulls"))
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 6)
                [
                    SNew(SNumericEntryBox<int32>)
                    .MinValue(1)
                    .MaxValue(64)
                    .Value_Lambda([this] { return MaxConvexHulls; })
                    .OnValueChanged_Lambda([this](int32 Value)
                    {
                        MaxConvexHulls = Value;
                    })
                ]

                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(STextBlock).Text(LOCTEXT(
                        "DecompositionResolution",
                        "Smart decomposition resolution"))
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 6)
                [
                    SNew(SNumericEntryBox<int32>)
                    .MinValue(10000)
                    .MaxValue(16000000)
                    .Value_Lambda([this]
                    {
                        return ConvexDecompositionResolution;
                    })
                    .OnValueChanged_Lambda([this](int32 Value)
                    {
                        ConvexDecompositionResolution = Value;
                    })
                ]

                + SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 8)
                [
                    SNew(SCheckBox)
                    .IsChecked_Lambda([this]
                    {
                        return bReplaceExisting
                            ? ECheckBoxState::Checked
                            : ECheckBoxState::Unchecked;
                    })
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState State)
                    {
                        bReplaceExisting = State == ECheckBoxState::Checked;
                    })
                    [
                        SNew(STextBlock).Text(LOCTEXT(
                            "ReplaceExisting",
                            "Replace all existing collision before adding"))
                    ]
                ]

                + SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 8)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("GroupingTitle", "Generation grouping"))
                    .Font(FAppStyle::GetFontStyle(TEXT("BoldFont")))
                ]

                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1)
                    [
                        SNew(SCheckBox)
                        .Style(FAppStyle::Get(), TEXT("RadioButton"))
                        .IsChecked_Lambda([this]
                        {
                            return !bMergeSelection
                                ? ECheckBoxState::Checked
                                : ECheckBoxState::Unchecked;
                        })
                        .OnCheckStateChanged_Lambda([this](ECheckBoxState State)
                        {
                            if (State == ECheckBoxState::Checked)
                            {
                                bMergeSelection = false;
                            }
                        })
                        [
                            SNew(STextBlock).Text(LOCTEXT(
                                "SeparateRegions",
                                "One per selected region"))
                        ]
                    ]
                    + SHorizontalBox::Slot().FillWidth(1).Padding(4, 0, 0, 0)
                    [
                        SNew(SCheckBox)
                        .Style(FAppStyle::Get(), TEXT("RadioButton"))
                        .IsChecked_Lambda([this]
                        {
                            return bMergeSelection
                                ? ECheckBoxState::Checked
                                : ECheckBoxState::Unchecked;
                        })
                        .OnCheckStateChanged_Lambda([this](ECheckBoxState State)
                        {
                            if (State == ECheckBoxState::Checked)
                            {
                                bMergeSelection = true;
                            }
                        })
                        [
                            SNew(STextBlock).Text(LOCTEXT(
                                "MergeRegions",
                                "Merge selection into one"))
                        ]
                    ]
                ]

                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SButton)
                    .Text(LOCTEXT(
                        "Auto",
                        "Auto fit selected geometry"))
                    .ToolTipText(LOCTEXT(
                        "AutoTip",
                        "Connected parts are fitted separately. Face selections extend inward to the opposite mesh surface."))
                    .OnClicked(this, &SSmartCollisionPanel::GenerateAutomatic)
                ]

                + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
                [
                    SNew(SButton)
                    .Text(LOCTEXT(
                        "SurfacePatch",
                        "Through surface collision"))
                    .ToolTipText(LOCTEXT(
                        "SurfacePatchTip",
                        "Keeps one side on the selected face and extends inward until the opposite mesh surface."))
                    .OnClicked(
                        this,
                        &SSmartCollisionPanel::GenerateSurfacePatch)
                ]

                + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("Box", "Box"))
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
                        .Text(LOCTEXT("Sphere", "Sphere"))
                        .OnClicked(this, &SSmartCollisionPanel::GenerateSphere)
                    ]
                ]

                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("Convex", "Convex hull"))
                    .OnClicked(this, &SSmartCollisionPanel::GenerateConvex)
                ]

                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
                [
                    SNew(SButton)
                    .Text(LOCTEXT(
                        "MultiConvex",
                        "Smart multi-convex (irregular shapes)"))
                    .ToolTipText(LOCTEXT(
                        "MultiConvexTip",
                        "Uses UE VHACD to split selected geometry into tight convex hulls. Merged surface selections produce one combined convex collision."))
                    .OnClicked(
                        this,
                        &SSmartCollisionPanel::GenerateMultiConvex)
                ]

                + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
                [
                    SNew(SVerticalBox)
                    .Visibility(
                        this,
                        &SSmartCollisionPanel::GetOriginSectionVisibility)

                    + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT("OriginTitle", "Origin"))
                        .Font(FAppStyle::GetFontStyle(TEXT("BoldFont")))
                    ]

                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().FillWidth(1)
                        [
                            SNew(SButton)
                            .Text(LOCTEXT("OriginTop", "Top"))
                            .ToolTipText(LOCTEXT(
                                "OriginTopTip",
                                "Moves the selected convex collision origin to its local top center without moving its geometry."))
                            .OnClicked_Lambda([this]()
                            {
                                return SetSelectedCollisionOrigin(
                                    ECollisionOriginPreset::Top);
                            })
                        ]
                        + SHorizontalBox::Slot().FillWidth(1).Padding(4, 0)
                        [
                            SNew(SButton)
                            .Text(LOCTEXT("OriginBottom", "Bottom"))
                            .ToolTipText(LOCTEXT(
                                "OriginBottomTip",
                                "Moves the selected convex collision origin to its local bottom center without moving its geometry."))
                            .OnClicked_Lambda([this]()
                            {
                                return SetSelectedCollisionOrigin(
                                    ECollisionOriginPreset::Bottom);
                            })
                        ]
                        + SHorizontalBox::Slot().FillWidth(1)
                        [
                            SNew(SButton)
                            .Text(LOCTEXT("OriginVolume", "Volume"))
                            .ToolTipText(LOCTEXT(
                                "OriginVolumeTip",
                                "Moves the selected convex collision origin to its volume center without moving its geometry."))
                            .OnClicked_Lambda([this]()
                            {
                                return SetSelectedCollisionOrigin(
                                    ECollisionOriginPreset::Volume);
                            })
                        ]
                    ]

                    + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 2)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot()
                        .FillWidth(0.18f)
                        .VAlign(VAlign_Center)
                        [
                            SNew(STextBlock)
                            .Text(LOCTEXT("CollisionPosition", "Position"))
                            .ToolTipText(LOCTEXT(
                                "CollisionPositionTip",
                                "Selected convex collision origin position in centimeters."))
                        ]
                        + SHorizontalBox::Slot()
                        .FillWidth(0.82f)
                        [
                            SNew(SNumericVectorInputBox<FVector::FReal>)
                            .X_Lambda([this]()
                            {
                                return GetSelectedCollisionTransformComponent(
                                    false,
                                    0);
                            })
                            .Y_Lambda([this]()
                            {
                                return GetSelectedCollisionTransformComponent(
                                    false,
                                    1);
                            })
                            .Z_Lambda([this]()
                            {
                                return GetSelectedCollisionTransformComponent(
                                    false,
                                    2);
                            })
                            .bColorAxisLabels(true)
                            .AllowSpin(false)
                            .OnXCommitted_Lambda(
                                [this](
                                    FVector::FReal Value,
                                    ETextCommit::Type)
                                {
                                    SetSelectedCollisionTransformComponent(
                                        Value,
                                        false,
                                        0);
                                })
                            .OnYCommitted_Lambda(
                                [this](
                                    FVector::FReal Value,
                                    ETextCommit::Type)
                                {
                                    SetSelectedCollisionTransformComponent(
                                        Value,
                                        false,
                                        1);
                                })
                            .OnZCommitted_Lambda(
                                [this](
                                    FVector::FReal Value,
                                    ETextCommit::Type)
                                {
                                    SetSelectedCollisionTransformComponent(
                                        Value,
                                        false,
                                        2);
                                })
                        ]
                    ]

                    + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot()
                        .FillWidth(0.18f)
                        .VAlign(VAlign_Center)
                        [
                            SNew(STextBlock)
                            .Text(LOCTEXT("CollisionScale", "Scale"))
                            .ToolTipText(LOCTEXT(
                                "CollisionScaleTip",
                                "Selected convex collision scale around its current origin."))
                        ]
                        + SHorizontalBox::Slot()
                        .FillWidth(0.82f)
                        [
                            SNew(SNumericVectorInputBox<FVector::FReal>)
                            .X_Lambda([this]()
                            {
                                return GetSelectedCollisionTransformComponent(
                                    true,
                                    0);
                            })
                            .Y_Lambda([this]()
                            {
                                return GetSelectedCollisionTransformComponent(
                                    true,
                                    1);
                            })
                            .Z_Lambda([this]()
                            {
                                return GetSelectedCollisionTransformComponent(
                                    true,
                                    2);
                            })
                            .MinVector(FVector(0.001))
                            .bColorAxisLabels(true)
                            .AllowSpin(false)
                            .OnXCommitted_Lambda(
                                [this](
                                    FVector::FReal Value,
                                    ETextCommit::Type)
                                {
                                    SetSelectedCollisionTransformComponent(
                                        Value,
                                        true,
                                        0);
                                })
                            .OnYCommitted_Lambda(
                                [this](
                                    FVector::FReal Value,
                                    ETextCommit::Type)
                                {
                                    SetSelectedCollisionTransformComponent(
                                        Value,
                                        true,
                                        1);
                                })
                            .OnZCommitted_Lambda(
                                [this](
                                    FVector::FReal Value,
                                    ETextCommit::Type)
                                {
                                    SetSelectedCollisionTransformComponent(
                                        Value,
                                        true,
                                        2);
                                })
                        ]
                    ]
                ]

                + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("ClearCollision", "Clear all simple collision"))
                    .OnClicked(this, &SSmartCollisionPanel::ClearCollision)
                ]

                + SVerticalBox::Slot().AutoHeight().Padding(0, 8)
                [
                    SAssignNew(StatusText, STextBlock)
                    .Text(LOCTEXT("Ready", "Ready. Start viewport picking."))
                    .AutoWrapText(true)
                ]
            ]
        ]
    ];
}

SSmartCollisionPanel::~SSmartCollisionPanel()
{
    if (SelectionToolBuilder.IsValid())
    {
        if (UInteractiveToolManager* ToolManager = GetToolManager())
        {
            if (ToolManager->GetActiveToolType(EToolSide::Left)
                == SmartCollisionSelection::ToolIdentifier)
            {
                ToolManager->DeactivateTool(
                    EToolSide::Left,
                    EToolShutdownType::Cancel);
            }
            ToolManager->UnregisterToolType(
                SmartCollisionSelection::ToolIdentifier);
        }
    }
}

bool SSmartCollisionPanel::EnsureSelectionToolRegistered()
{
    if (SelectionToolBuilder.IsValid())
    {
        return true;
    }

    UInteractiveToolManager* ToolManager = GetToolManager();
    if (!ToolManager)
    {
        return false;
    }

    USmartCollisionSelectionToolBuilder* Builder =
        NewObject<USmartCollisionSelectionToolBuilder>(ToolManager);
    if (!Builder)
    {
        return false;
    }

    Builder->Initialize(StaticMeshEditor);
    ToolManager->RegisterToolType(
        SmartCollisionSelection::ToolIdentifier,
        Builder);
    SelectionToolBuilder = Builder;
    return true;
}

UInteractiveToolManager* SSmartCollisionPanel::GetToolManager() const
{
    const TSharedPtr<IStaticMeshEditor> Editor = StaticMeshEditor.Pin();
    if (!Editor)
    {
        return nullptr;
    }

    UModeManagerInteractiveToolsContext* ToolsContext =
        Editor->GetEditorModeManager().GetInteractiveToolsContext();
    return ToolsContext ? ToolsContext->ToolManager : nullptr;
}

USmartCollisionSelectionTool* SSmartCollisionPanel::GetSelectionTool() const
{
    UInteractiveToolManager* ToolManager = GetToolManager();
    if (!ToolManager
        || ToolManager->GetActiveToolType(EToolSide::Left)
            != SmartCollisionSelection::ToolIdentifier)
    {
        return nullptr;
    }

    return Cast<USmartCollisionSelectionTool>(
        ToolManager->GetActiveTool(EToolSide::Left));
}

FReply SSmartCollisionPanel::StartPicking()
{
    UInteractiveToolManager* ToolManager = GetToolManager();
    if (!ToolManager || !EnsureSelectionToolRegistered())
    {
        SetStatus(TEXT("The Static Mesh Editor interactive tools context is not ready."));
        return FReply::Handled();
    }

    const bool bSmartCollisionAlreadyActive =
        ToolManager->HasActiveTool(EToolSide::Left)
        && ToolManager->GetActiveToolType(EToolSide::Left)
            == SmartCollisionSelection::ToolIdentifier;

    if (!bSmartCollisionAlreadyActive)
    {
        if (ToolManager->HasActiveTool(EToolSide::Left))
        {
            ToolManager->DeactivateTool(
                EToolSide::Left,
                EToolShutdownType::Cancel);
        }

        // After StopPicking the tool type may remain selected even though
        // there is no active tool. SelectActiveToolType can return false in
        // that state, so activation is the authoritative restart check.
        ToolManager->SelectActiveToolType(
            EToolSide::Left,
            SmartCollisionSelection::ToolIdentifier);

        if (!ToolManager->ActivateTool(EToolSide::Left))
        {
            SetStatus(TEXT("Unable to restart viewport picking."));
            return FReply::Handled();
        }
    }

    if (USmartCollisionSelectionTool* Tool = GetSelectionTool())
    {
        Tool->SetSelectionMode(SelectionMode);
        if (!CachedSelectedTriangleIndices.IsEmpty())
        {
            Tool->SetSelectedTriangleIndices(
                CachedSelectedTriangleIndices);
        }
        const TWeakPtr<SSmartCollisionPanel> WeakThis = SharedThis(this);
        Tool->SetSelectionChangedCallback(
            [WeakThis](int32 TriangleCount, int32 PointCount)
            {
                if (const TSharedPtr<SSmartCollisionPanel> Panel = WeakThis.Pin())
                {
                    Panel->UpdateSelectionSummary(TriangleCount, PointCount);
                }
            });
        SetStatus(TEXT("Picking is active. Click geometry in the viewport."));
    }

    return FReply::Handled();
}

void SSmartCollisionPanel::CacheSelectionFromTool()
{
    if (USmartCollisionSelectionTool* Tool = GetSelectionTool())
    {
        Tool->GetSelectedTriangleIndices(
            CachedSelectedTriangleIndices);
        Tool->GetSelectedGroups(CachedSelectionGroups);
    }
}

FReply SSmartCollisionPanel::StopPicking()
{
    UInteractiveToolManager* ToolManager = GetToolManager();
    if (!ToolManager
        || ToolManager->GetActiveToolType(EToolSide::Left)
            != SmartCollisionSelection::ToolIdentifier)
    {
        SetStatus(TEXT("Viewport picking is not active."));
        return FReply::Handled();
    }

    CacheSelectionFromTool();
    ToolManager->DeactivateTool(
        EToolSide::Left,
        EToolShutdownType::Accept);
    SetStatus(TEXT("Picking stopped. The cached selection is ready for collision generation."));
    return FReply::Handled();
}

void SSmartCollisionPanel::SetSelectionMode(ESmartCollisionSelectionMode Mode)
{
    SelectionMode = Mode;
    if (USmartCollisionSelectionTool* Tool = GetSelectionTool())
    {
        Tool->SetSelectionMode(Mode);
    }
}

ECheckBoxState SSmartCollisionPanel::IsSelectionModeChecked(
    ESmartCollisionSelectionMode Mode) const
{
    return SelectionMode == Mode
        ? ECheckBoxState::Checked
        : ECheckBoxState::Unchecked;
}

FReply SSmartCollisionPanel::ClearSelection()
{
    if (USmartCollisionSelectionTool* Tool = GetSelectionTool())
    {
        Tool->ClearSelection();
    }
    else
    {
        SetStatus(TEXT("Start viewport picking first."));
    }
    return FReply::Handled();
}

FReply SSmartCollisionPanel::SelectAll()
{
    if (USmartCollisionSelectionTool* Tool = GetSelectionTool())
    {
        Tool->SelectAll();
    }
    else
    {
        StartPicking();
        if (USmartCollisionSelectionTool* ActivatedTool = GetSelectionTool())
        {
            ActivatedTool->SelectAll();
        }
    }
    return FReply::Handled();
}

FReply SSmartCollisionPanel::GenerateAutomatic()
{
    Generate(ESmartCollisionMode::Automatic);
    return FReply::Handled();
}

FReply SSmartCollisionPanel::GenerateBox()
{
    Generate(ESmartCollisionMode::OrientedBox);
    return FReply::Handled();
}

FReply SSmartCollisionPanel::GenerateCapsule()
{
    Generate(ESmartCollisionMode::Capsule);
    return FReply::Handled();
}

FReply SSmartCollisionPanel::GenerateSphere()
{
    Generate(ESmartCollisionMode::Sphere);
    return FReply::Handled();
}

FReply SSmartCollisionPanel::GenerateConvex()
{
    Generate(ESmartCollisionMode::Convex);
    return FReply::Handled();
}

FReply SSmartCollisionPanel::GenerateMultiConvex()
{
    Generate(ESmartCollisionMode::MultiConvex);
    return FReply::Handled();
}

FReply SSmartCollisionPanel::GenerateSurfacePatch()
{
    Generate(ESmartCollisionMode::SurfacePatch);
    return FReply::Handled();
}

void SSmartCollisionPanel::Generate(ESmartCollisionMode Mode)
{
    if (Mode == ESmartCollisionMode::SurfacePatch
        && SelectionMode != ESmartCollisionSelectionMode::Face)
    {
        SetStatus(TEXT("Switch to Surface / face mode before creating through surface collision."));
        return;
    }

    const TSharedPtr<IStaticMeshEditor> Editor = StaticMeshEditor.Pin();
    USmartCollisionSelectionTool* Tool = GetSelectionTool();
    if (!Editor)
    {
        SetStatus(TEXT("The Static Mesh Editor has no active mesh."));
        return;
    }

    TArray<FSmartCollisionSelectionGroup> SelectedGroups;
    if (Tool)
    {
        CacheSelectionFromTool();
        SelectedGroups = CachedSelectionGroups;
    }
    else
    {
        SelectedGroups = CachedSelectionGroups;
    }
    if (SelectedGroups.IsEmpty())
    {
        SetStatus(TEXT("Select at least one surface or connected part."));
        return;
    }

    FSmartCollisionSettings Settings;
    Settings.Padding = Padding;
    Settings.MaxConvexVertices = MaxConvexVertices;
    Settings.MaxConvexHulls = MaxConvexHulls;
    Settings.ConvexDecompositionResolution =
        ConvexDecompositionResolution;
    Settings.bReplaceExisting = bReplaceExisting;
    Settings.bMergeSelection = bMergeSelection;

    const FSmartCollisionResult Result =
        FSmartCollisionGenerator::GenerateFromGroups(
            Editor->GetStaticMesh(),
            SelectedGroups,
            Mode,
            Settings);

    SetStatus(Result.Message);
    if (Result.bSuccess)
    {
        Editor->RefreshTool();
        if (!Editor->IsShowSimpleCollisionChecked())
        {
            Editor->ToggleShowSimpleCollision();
        }
        Editor->RefreshViewport();
    }
}

EVisibility SSmartCollisionPanel::GetOriginSectionVisibility() const
{
    const TSharedPtr<IStaticMeshEditor> Editor = StaticMeshEditor.Pin();
    if (!Editor)
    {
        return EVisibility::Collapsed;
    }

    for (const IStaticMeshEditor::FPrimData& PrimData :
         Editor->GetSelectedPrims())
    {
        if (PrimData.PrimType == EAggCollisionShape::Convex
            && Editor->IsPrimValid(PrimData))
        {
            return EVisibility::Visible;
        }
    }

    return EVisibility::Collapsed;
}

TOptional<FVector::FReal>
SSmartCollisionPanel::GetSelectedCollisionTransformComponent(
    bool bScale,
    int32 Component) const
{
    const TSharedPtr<IStaticMeshEditor> Editor = StaticMeshEditor.Pin();
    if (!Editor || Component < 0 || Component > 2)
    {
        return TOptional<FVector::FReal>();
    }

    TOptional<FVector::FReal> SharedValue;
    for (const IStaticMeshEditor::FPrimData& PrimData :
         Editor->GetSelectedPrims())
    {
        if (PrimData.PrimType != EAggCollisionShape::Convex
            || !Editor->IsPrimValid(PrimData))
        {
            continue;
        }

        const FTransform Transform = Editor->GetPrimTransform(PrimData);
        const FVector TransformValue =
            bScale ? Transform.GetScale3D() : Transform.GetLocation();
        const FVector::FReal ComponentValue = TransformValue[Component];

        if (!SharedValue.IsSet())
        {
            SharedValue = ComponentValue;
        }
        else if (!FMath::IsNearlyEqual(
                     SharedValue.GetValue(),
                     ComponentValue))
        {
            return TOptional<FVector::FReal>();
        }
    }

    return SharedValue;
}

void SSmartCollisionPanel::SetSelectedCollisionTransformComponent(
    FVector::FReal Value,
    bool bScale,
    int32 Component)
{
    const TSharedPtr<IStaticMeshEditor> Editor = StaticMeshEditor.Pin();
    UStaticMesh* Mesh = Editor ? Editor->GetStaticMesh() : nullptr;
    UBodySetup* BodySetup = Mesh ? Mesh->GetBodySetup() : nullptr;
    if (!Editor || !Mesh || !BodySetup
        || Component < 0 || Component > 2)
    {
        return;
    }

    TArray<IStaticMeshEditor::FPrimData> SelectedConvexPrims;
    for (const IStaticMeshEditor::FPrimData& PrimData :
         Editor->GetSelectedPrims())
    {
        if (PrimData.PrimType == EAggCollisionShape::Convex
            && Editor->IsPrimValid(PrimData)
            && BodySetup->AggGeom.ConvexElems.IsValidIndex(
                PrimData.PrimIndex))
        {
            SelectedConvexPrims.Add(PrimData);
        }
    }

    if (SelectedConvexPrims.IsEmpty())
    {
        return;
    }

    const FScopedTransaction Transaction(
        bScale
            ? LOCTEXT(
                "SetCollisionScaleTransaction",
                "Set Convex Collision Scale")
            : LOCTEXT(
                "SetCollisionPositionTransaction",
                "Set Convex Collision Position"));
    Mesh->Modify();
    BodySetup->Modify();

    const FVector::FReal AppliedValue =
        bScale ? FMath::Max(Value, 0.001) : Value;
    for (const IStaticMeshEditor::FPrimData& PrimData :
         SelectedConvexPrims)
    {
        FTransform Transform = Editor->GetPrimTransform(PrimData);
        FVector TransformValue =
            bScale ? Transform.GetScale3D() : Transform.GetLocation();
        TransformValue[Component] = AppliedValue;

        if (bScale)
        {
            Transform.SetScale3D(TransformValue);
        }
        else
        {
            Transform.SetLocation(TransformValue);
        }
        Editor->SetPrimTransform(PrimData, Transform);
    }

    BodySetup->InvalidatePhysicsData();
    BodySetup->CreatePhysicsMeshes();
    Mesh->SetCustomizedCollision(true);
    Mesh->MarkPackageDirty();
    Mesh->PostEditChange();

    Editor->RefreshTool();
    Editor->RefreshViewport();
    if (bScale)
    {
        SetStatus(FString::Printf(
            TEXT("Updated scale for %d selected convex collision(s)."),
            SelectedConvexPrims.Num()));
    }
    else
    {
        SetStatus(FString::Printf(
            TEXT("Updated position for %d selected convex collision(s)."),
            SelectedConvexPrims.Num()));
    }
}

FReply SSmartCollisionPanel::SetSelectedCollisionOrigin(
    ECollisionOriginPreset Preset)
{
    const TSharedPtr<IStaticMeshEditor> Editor = StaticMeshEditor.Pin();
    UStaticMesh* Mesh = Editor ? Editor->GetStaticMesh() : nullptr;
    UBodySetup* BodySetup = Mesh ? Mesh->GetBodySetup() : nullptr;
    if (!Editor || !Mesh || !BodySetup)
    {
        return FReply::Handled();
    }

    TArray<IStaticMeshEditor::FPrimData> SelectedConvexPrims;
    for (const IStaticMeshEditor::FPrimData& PrimData :
         Editor->GetSelectedPrims())
    {
        if (PrimData.PrimType == EAggCollisionShape::Convex
            && Editor->IsPrimValid(PrimData)
            && BodySetup->AggGeom.ConvexElems.IsValidIndex(
                PrimData.PrimIndex))
        {
            SelectedConvexPrims.Add(PrimData);
        }
    }

    if (SelectedConvexPrims.IsEmpty())
    {
        return FReply::Handled();
    }

    const FScopedTransaction Transaction(
        LOCTEXT(
            "SetCollisionOriginTransaction",
            "Set Convex Collision Origin"));
    Mesh->Modify();
    BodySetup->Modify();

    int32 UpdatedCount = 0;
    for (const IStaticMeshEditor::FPrimData& PrimData :
         SelectedConvexPrims)
    {
        FKConvexElem& Convex =
            BodySetup->AggGeom.ConvexElems[PrimData.PrimIndex];
        if (Convex.VertexData.IsEmpty())
        {
            continue;
        }

        Convex.UpdateElemBox();
        FVector LocalPivot = Convex.ElemBox.GetCenter();
        if (Preset == ECollisionOriginPreset::Top)
        {
            LocalPivot.Z = Convex.ElemBox.Max.Z;
        }
        else if (Preset == ECollisionOriginPreset::Bottom)
        {
            LocalPivot.Z = Convex.ElemBox.Min.Z;
        }

        const FTransform PreviousTransform = Convex.GetTransform();
        for (FVector& Vertex : Convex.VertexData)
        {
            Vertex -= LocalPivot;
        }

        FTransform NewTransform = PreviousTransform;
        NewTransform.SetTranslation(
            PreviousTransform.TransformPosition(LocalPivot));
        Convex.SetTransform(NewTransform);
        Convex.UpdateElemBox();
        Convex.ResetChaosConvexMesh();
        ++UpdatedCount;
    }

    if (UpdatedCount > 0)
    {
        BodySetup->InvalidatePhysicsData();
        BodySetup->CreatePhysicsMeshes();
        Mesh->SetCustomizedCollision(true);
        Mesh->MarkPackageDirty();
        Mesh->PostEditChange();

        Editor->RefreshTool();
        Editor->RefreshViewport();

        const TCHAR* PresetName =
            Preset == ECollisionOriginPreset::Top
                ? TEXT("top center")
                : Preset == ECollisionOriginPreset::Bottom
                    ? TEXT("bottom center")
                    : TEXT("volume center");
        SetStatus(FString::Printf(
            TEXT("Moved %d selected convex collision origin(s) to the %s."),
            UpdatedCount,
            PresetName));
    }

    return FReply::Handled();
}

FReply SSmartCollisionPanel::ClearCollision()
{
    const TSharedPtr<IStaticMeshEditor> Editor = StaticMeshEditor.Pin();
    UStaticMesh* Mesh = Editor ? Editor->GetStaticMesh() : nullptr;
    if (!Mesh)
    {
        return FReply::Handled();
    }

    const FScopedTransaction Transaction(
        LOCTEXT("ClearCollisionTransaction", "Clear Smart Collision"));
    Mesh->Modify();

    if (UBodySetup* BodySetup = Mesh->GetBodySetup())
    {
        BodySetup->Modify();
        BodySetup->AggGeom.EmptyElements();
        BodySetup->InvalidatePhysicsData();
        BodySetup->CreatePhysicsMeshes();
        Mesh->SetCustomizedCollision(true);
        Mesh->MarkPackageDirty();
        Mesh->PostEditChange();

        Editor->RefreshTool();
        Editor->RefreshViewport();
        SetStatus(TEXT("All simple collision was cleared."));
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

void SSmartCollisionPanel::UpdateSelectionSummary(
    int32 TriangleCount,
    int32 PointCount)
{
    if (SelectionText.IsValid())
    {
        SelectionText->SetText(FText::Format(
            LOCTEXT(
                "SelectionSummary",
                "Selection: {0} triangles, {1} unique points"),
            FText::AsNumber(TriangleCount),
            FText::AsNumber(PointCount)));
    }
}

#undef LOCTEXT_NAMESPACE

