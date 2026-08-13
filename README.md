# Smart Collision Editor

An Unreal Engine 5.8.1 editor plugin for interactively fitting simple collision to selected geometry inside the native Static Mesh Editor.

> V2 is under development on `feature/interactive-selection-v2`. The stable V1 implementation remains on `main`.

## V3 selection and fitting

- Ordinary click toggles a connected part or surface region, so multi-selection does not depend on Shift/Ctrl.
- **Alt + Click** replaces the entire selection with the clicked region.
- Choose **One per selected region** to create collision separately for every selected part/surface, or **Merge selection into one** to create one collision shape around the complete selection.
- **Stop viewport picking** pauses picking while preserving the selection for generation; restarting picking restores the cached highlight.
- In **Surface / face** mode, **Auto** creates thin collision following the selected surface instead of a box.
- Convex planar regions are merged into one thin convex shell; concave regions fall back to multiple thin triangle prisms to preserve holes and indentations.
- Box fitting compares PCA orientation with the mesh axes and keeps the tighter result.
- Capsule, sphere, and convex fitting use tighter geometry-based bounds.

## V2 workflow

1. Double-click a Static Mesh asset.
2. Open the embedded **Smart Collision** tab in the Static Mesh Editor. If the saved layout hides it, click the **Smart Collision** toolbar button.
3. Choose a selection mode:
   - **Connected part** selects the complete edge-connected topology island under the cursor.
   - **Surface / face** selects the connected, approximately coplanar surface under the cursor.
4. Click **Start viewport picking**.
5. Click geometry in the native viewport. ordinary clicks add/remove regions; **Alt + Click** replaces the selection.
6. Choose **One per selected region** or **Merge selection into one**.
7. Fit **Auto**, **Thin surface collision**, **Box**, **Capsule**, **Sphere**, or **Convex hull** around only the selected geometry.
8. Leave **Replace all existing collision** disabled to preserve existing shapes.
9. Click **Stop viewport picking** when you want normal viewport interaction without losing the current selection.
10. Inspect the standard simple collision overlay and save the Static Mesh asset.

Selected triangles are outlined in orange. The generated shapes are stored in the mesh's standard `UBodySetup::AggGeom`, participate in Undo/Redo, and require no runtime module.

## Collision fitting

- **Auto** chooses a sphere for near-uniform bounds, a capsule for long round parts, and an oriented box otherwise.
- **Box** uses a PCA-aligned oriented bounding box.
- **Capsule** aligns its long axis to the selection's principal axis.
- **Sphere** encloses all selected vertices.
- **Convex hull** submits a reduced selected point cloud to Chaos cooking.
- For a planar face selection, padding also supplies a small thickness so Box and Convex results remain volumetric.

## Installation

Clone the V2 branch directly:

```powershell
git clone --branch feature/interactive-selection-v2 https://github.com/fuyutianji/SmartCollisionEditor.git
```

Place the repository so the descriptor is at:

```text
YourProject/
  Plugins/
    SmartCollisionEditor/
      SmartCollisionEditor.uplugin
      Source/
```

Then:

1. Close Unreal Editor.
2. Right-click the project's `.uproject` and regenerate project files if that command is available.
3. Open the project and allow Unreal to build the plugin.
4. Enable **Smart Collision Editor** under **Edit > Plugins** if necessary.
5. Restart the editor, then open a Static Mesh asset.

A C++ toolchain compatible with Unreal Engine 5.8.1 is required.

## Updating an existing checkout

```powershell
git fetch origin
git switch feature/interactive-selection-v2
git pull
```

If the plugin was previously built, close Unreal and delete only the plugin's generated `Binaries` and `Intermediate` folders before rebuilding. Do not delete `Source` or the `.uplugin` file.

## Selection details and current limits

- Selection operates on render LOD0.
- Connected-part detection uses shared quantized edges. Parts that only touch at a point normally remain separate.
- Surface selection expands across shared edges whose triangle normals differ by no more than five degrees.
- Multiple clicks select several parts or surfaces. Generation can fit each region separately or merge all selected geometry into one collision shape.
- The current preview outlines selected triangle edges; a translucent filled highlight is a possible follow-up.
- Always inspect convex cooking before using the mesh for physics simulation.

## Source layout

```text
SmartCollisionEditor.uplugin
Source/SmartCollisionEditor/
  SmartCollisionEditor.Build.cs
  Public/SmartCollisionEditorModule.h
  Private/
    SmartCollisionEditorModule.cpp
    SSmartCollisionPanel.h
    SSmartCollisionPanel.cpp
    SmartCollisionSelectionTool.h
    SmartCollisionSelectionTool.cpp
    SmartCollisionGenerator.h
    SmartCollisionGenerator.cpp
```

## License and company policy

No license has been selected. Keep the repository private until you choose one and confirm that publishing or moving the code is allowed by your organization.
