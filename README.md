# Smart Collision Editor

An Unreal Engine 5.8.1 editor plugin for fitting simple collision to individual connected parts of a complex Static Mesh.

The first release is aimed at imported CAD/mechanical assemblies where manually positioning collision boxes for every rail, plate, pipe, and bracket is slow.

## What it does

- Reads LOD0 editor mesh data.
- Splits the mesh into topologically connected vertex components.
- Computes a PCA-aligned local frame for each component.
- Generates one collision primitive per component:
  - **Auto:** capsule for long, approximately round parts; oriented box otherwise.
  - **Oriented Box:** PCA-aligned box for every part.
  - **Capsule:** PCA-aligned capsule for every part.
  - **Convex:** reduced convex point cloud for every part.
- Filters tiny components such as screws and wires.
- Supports collision padding.
- Can replace existing simple collision or append to it.
- Participates in Unreal Editor Undo/Redo.
- Marks the Static Mesh asset dirty so the generated collision can be saved normally.

The generated collision is stored in the Static Mesh `UBodySetup::AggGeom`, so it remains standard Unreal simple collision and can be inspected with the normal Static Mesh Editor collision display.

## Installation

1. Clone or download this repository.
2. Create a `Plugins` directory beside your project's `.uproject` file if it does not exist.
3. Copy this repository into:

   ```text
   YourProject/
     Plugins/
       SmartCollisionEditor/
         SmartCollisionEditor.uplugin
   ```

4. Right-click the `.uproject` file and regenerate project files if required.
5. Open the project and allow Unreal to build the plugin.
6. Enable **Smart Collision Editor** in **Edit > Plugins**.
7. Restart the editor.

A source build or an installed C++ toolchain compatible with Unreal Engine 5.8.1 is required.

## Usage

1. Select one Static Mesh asset in the Content Browser.
2. Open **Tools > Smart Collision Editor**.
3. Click **Use Content Browser Selection**.
4. Start with **Generate Auto (recommended)**.
5. Review the result in the Static Mesh Editor with collision visibility enabled.
6. Adjust padding or minimum part size and regenerate if necessary.
7. Save the Static Mesh asset.

Recommended starting values for mechanical assemblies:

- Collision padding: `0.1` to `0.5` cm
- Ignore parts smaller than: `1` to `3` cm
- Convex vertices: `32` to `64`

## Important behavior

- Component detection is based on shared mesh vertices. Two parts that merely touch but do not share vertices remain separate.
- A welded CAD assembly may appear as one component; material-slot and viewport face selection are planned follow-up features.
- Auto mode intentionally favors cheap boxes and capsules. Use Convex only for parts whose shape cannot be represented adequately by a primitive.
- The tool caps output at 256 shapes per run to prevent accidental collision explosions.
- Convex mode supplies a reduced point cloud to Chaos cooking. Always inspect convex results before using them for simulation.
- This is an editor-only plugin. It does not add runtime code to packaged games.

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
    SmartCollisionGenerator.h
    SmartCollisionGenerator.cpp
```

## Roadmap

- Material-slot filtering
- Click-to-select connected parts in a preview viewport
- Per-part include/exclude list
- Collision fit-error heatmap
- Negative-space protection for frames and hollow assemblies
- Batch processing for multiple Static Mesh assets

## License

No license has been selected yet. Keep the repository private until you choose one and confirm that publishing the code is allowed by your organization.
