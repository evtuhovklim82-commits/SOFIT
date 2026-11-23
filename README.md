# SOFIT 0.9 — independent CAD prototype

Free/open-source clothing-pattern editor prototype. **Not GRAFIS 26**, no proprietary code or assets. Windows build can be run through Wine/Proton on Arch, but a native Linux build is also supported and recommended.

## Windows .exe via GitHub Actions
1. Create a GitHub repository and upload the contents of this folder to the repository root (including `.github/workflows/windows.yml`).
2. Open **Actions → Windows portable build → Run workflow**.
3. After completion, download the `SOFIT-Windows-x64` artifact and unpack the ZIP inside it.
4. Run `SOFIT.exe` on Windows. Qt runtime DLLs and plugins are included by `windeployqt`.

The ZIP in this starter project contains **source code, not a precompiled .exe**. Windows CI builds the actual .exe. CI build has not been executed here.

## Arch Linux — native (recommended)
```bash
sudo pacman -S --needed base-devel cmake ninja qt6-base
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/sofit
```

## Arch Linux — Windows build via Wine
After obtaining the Windows ZIP from GitHub Actions, extract it and run from the directory containing `SOFIT.exe`:
```bash
sudo pacman -S --needed wine
wine SOFIT.exe
```
You may also add `SOFIT.exe` as a non-Steam game and choose a Proton compatibility version. The native Linux build is more direct and avoids a compatibility layer.

## macOS
Install Qt 6, CMake, Ninja and Xcode command-line tools, then:
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
open build/sofit.app
```
Use `macdeployqt` when packaging the app for distribution.

## Features
- Draw points, lines, rectangles, circles and quadratic Bézier curves. Right-click or press Esc to cancel.
- Open **Exact Input** (I) to enter coordinates in millimeters. For a line you can also enter the start, length, and angle in degrees clockwise from +X. The canvas uses +Y down.
- **Snap to Grid (10 mm)** rounds mouse input to the nearest grid intersection. Typed coordinates keep their entered precision. The status bar shows cursor X/Y in millimeters.
- Use **Select** and click an object to select it. Press Delete to remove it, Ctrl+Z to undo, or Ctrl+Y to redo.
- The mouse wheel zooms around the cursor. Drag with the middle button to pan. Press F or click **Fit Drawing** to frame the drawing.
- Save patterns in `.sofit` format 8 when they contain a parametric draft, or format 7 otherwise. SOFIT opens formats 1–8 and legacy format 1 `.opcad` drawings. SVG export includes enabled layers and uses millimeters.
- SOFIT prompts to save changes before replacing or closing a modified drawing.

## Parametric drafting

1. Open **Pattern → Parametric Draft…**. In **Measurements**, add `chest` with value `920` mm. In **Parameters**, add `ease` with formula `15`.
2. In **Points**, enter `A` with X and Y formulas `0`, `B` with X `chest / 4 + ease` and Y `A.y`, and `C` with X `B.x / 2` and Y `-25`.
3. In **Lines and curves**, enter `AB` with start `A` and end `B`. Leave the control point blank for a straight line, or enter `C` for a quadratic Bézier curve.
4. Click **Validate**, then **Apply**. Reopen the draft and change `chest` to `1000`. The X coordinate of `B` increases from 245 to 265 mm. Undo and Redo restore the inputs and geometry together.
5. Save and reopen the project to continue editing the formulas.

Identifiers are case-sensitive and use ASCII letters, digits and underscores, with a letter or underscore first. Measurements and parameters share one namespace. Formulas can use `+`, `-`, `*`, `/`, parentheses, a decimal point, scientific notation and named point coordinates such as `B.x` and `B.y`. Coordinates are in mm and Y increases downwards. Points start on the Construction layer and edges on the Main layer.

Edit or delete generated objects in the draft editor. Piece and layer assignments are retained when a generated object's name stays the same. Renaming a point also requires updating formulas and edges that reference it. Parametric measurements are project-wide. Size grading uses its existing coordinate-based offsets, so review the grade rules and fabric layout after changing the base contour.

## Pattern pieces

1. Draw the boundary and any construction lines. Select the objects in **Drawing Objects** (Ctrl or Shift for several).
2. Choose **Pattern → Create Piece from Selection…** and name the piece. Use **Assign Selection to Piece…** to add or transfer objects later.
3. Move guides and markings to the construction layer using **Pattern → Move Selection to Construction Layer**. The contour uses the piece's main-layer lines, rectangles and quadratic curves.
4. Open **Pattern → Piece, Contour and Allowance…** to check the contour, read its perimeter and axis-aligned width and height, and set an outward seam allowance in millimeters. The allowance follows the boundary when it changes. Use **View → Layers** to control which layers are visible.

Each piece can have one simple outer contour. The checker accepts unordered edges and either winding direction. It joins endpoints within 0.000001 mm. Open ends, branching, disconnected loops and intersecting edges are rejected. Construction geometry stays part of the piece and does not contribute to contour measurements.

A piece has a single allowance width. Straight sections are offset by that distance, with mitered joins. Quadratics use an adaptive polygonal approximation with a chord deviation of at most 0.01 mm. Perimeter estimation uses a total error budget of 0.01 mm for the quadratic segments. The allowance of a curved section is approximate. Ambiguous close curve approaches, overlong miters and offsets that would overlap or collapse are rejected. Holes, several contours in one piece and per-edge allowances are outside this release. When an edit invalidates an existing allowance, the document retains the entered width and the allowance disappears until geometry supports it again.

SOFIT 5 saves piece names, membership, layer assignments, layer visibility and allowance widths. Contours, measurements and allowance geometry are rebuilt on load. Individual object visibility is a session setting.

## Grading sizes and fabric layout

1. Open **Pattern → Sizes, Measurements and Grade Rules…**. On **Measurements**, enter the size and one measurement in mm per row. Repeat the size name for more measurements. A size can also have an empty measurement list.
2. On **Grade Rules**, add a rule for each point that moves in a target size. Select the piece and the point in base coordinates, then enter ΔX and ΔY in mm. Coincident points of the same piece share the offset. Rectangle corners and quadratic control points are available. Measurements are recorded for reference and the offsets are entered explicitly for each size.
3. Enable the sizes to compare in **Pattern → Size Overlays**. The menu swatches match the canvas colors. **File → Export DXF…** and **Export PDF for Printing…** offer the base pattern or a single target size.
4. Open **Pattern → Fabric Layout…**, set the width in mm, choose a piece and size, and click **Add copy**. Add each required copy, drag the copies inside the fabric, and use **Turn 180°** where the fabric allows it. The declared grainline is aligned with the fabric length. Without a grainline the original orientation is used. The outline includes the seam allowance when defined.
5. Choose a cut mode before adding copies. **Left + right pair** adds one original and one mirrored contour per unit of quantity. You can also add either hand by itself. The reflection preserves the lengthwise grain direction.
6. For on-fold pieces, enter the working width of the folded fabric and select the side with a straight outer cutting edge parallel to the grain. Use a finished cutting contour with a zero automatic allowance. Dragging is restricted to movement along the fold.
7. Enable **One-way nap or print** to require the original grain direction across the layout. Use **Fix orientation for new copies** to apply the same restriction to individual copies. Otherwise an aligned 180° turn is available. **Auto arrange** tries three deterministic orders with up to 200 plotted copies, places fold copies along their assigned edge, and checks the outlines for overlap and fit.
8. Open **Cutting report…** to review counts by piece and size, working width, used length, utilization, and warnings. Counts refer to plotted copies per cutting layer. Each on-fold entry yields one unfolded piece. The project file retains these settings.
9. Use **Export layout DXF…** to export a valid layout in millimeters with separate CUT, FOLD, GRAINLINE and FABRIC layers. On-fold CUT paths are open along the marked fold. For cutting, use the CUT layer.
5. Read the length from the fabric start to the furthest cut edge. Resolve reported overlaps and pieces outside the fabric before using the estimate. Click **OK** and save the pattern to retain the width and positions.

Grade rules reference the piece and exact base point coordinates. After moving base geometry, review rules labelled **Missing point**. If a contour joins endpoints with slightly different coordinates, give both points the same offset to keep the join. Fabric layout uses existing contour validation and flattened cut polygons. Layout is manual and is saved with the project. The PDF and DXF actions export patterns.

## Full-scale printing

Choose **File → Export PDF for Printing…** and select A4 or A3 in portrait or landscape.
The PDF uses the drawing's millimeter coordinates at 1:1. It includes visible objects and
allowances enabled in **View → Layers**. Main lines are solid, construction lines are
dotted, and seam allowances are dashed.

Print at **Actual size (100%)** with fit-to-page disabled. Measure the 50 mm scale on
a printed sheet before assembling the pattern. Large drawings span numbered rows and
columns with 10 mm overlap. Match the crosses and overlapping drawing lines on
adjacent sheets. The dashed frames mark each sheet's drawing area.

PDF export tiles the drawing as arranged on the canvas. It supports up to 200 sheets
with fixed margins and overlap.

## Tests

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

CTest checks contour geometry, offsets, file compatibility and the pattern workflow through the offscreen Qt interface. Use `-DBUILD_TESTING=OFF` for an application-only build.

## Licensing/legal
Code is intended for GPLv3 distribution, but check and include the complete GPLv3 license text before publishing. Qt library license obligations apply. Check SOFIT trademark availability before public branding. Independent implementation only.

## Piece markings and DXF

Choose **Pattern → Piece, Contour and Allowance…** to set the cut quantity, size,
label visibility, grainline angle, and notch positions. Markings appear on
closed pieces. The grainline angle is measured clockwise from the right.
Enter notch positions as space-separated percentages of the perimeter, using a
decimal point. Positions start at the first contour edge and follow the connected
outline. With a visible usable allowance, notches are projected onto its cutting
edge when the outward projection intersects it.

Markings are saved in SOFIT files and included in the canvas, printing PDF and DXF.
Choose **File → Export DXF…** for a millimeter drawing with separate layers for
geometry and markings. The DXF uses standard CAD entities, including quadratic
splines and editable text. Apparel-specific AAMA/ASTM export is outside this format.
