# SOFIT 1.3 — independent CAD prototype

Free/open-source clothing-pattern editor prototype. **Not GRAFIS 26**, no proprietary code or assets. Windows build can be run through Wine/Proton on Arch, but a native Linux build is also supported and recommended.

## Windows .exe via GitHub Actions
1. In the SOFIT repository, open **Actions → Windows portable build → Run workflow**.
2. After completion, download the `SOFIT-Windows-x64` artifact and unpack the ZIP inside it.
3. Run `SOFIT.exe` on Windows. Qt runtime DLLs and plugins are included by `windeployqt`.

The workflow uses Qt 6.8.3 and MSVC 2022 x64 and runs CTest before packaging. The executable also requires the Microsoft Visual C++ x64 runtime.

## Arch Linux — native (recommended)
```bash
sudo pacman -S --needed base-devel cmake ninja qt6-base
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/sofit
```

To build the native Linux archive and SHA-256 checksum, run:

```bash
cpack --config build/CPackConfig.cmake -B dist
```

The archive contains the app and documentation. It uses the Qt and system libraries on the target machine. The **Linux native build** workflow builds, tests and packages on Ubuntu 24.04.

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
- SOFIT opens `.sofit` formats 1–9 and legacy format 1 `.opcad` drawings. It writes format 9 for drafts with size measurements enabled and at least one size, format 8 for other drafts, and format 7 for projects without a draft. SVG export includes enabled layers and uses millimeters.
- SOFIT prompts to save changes before replacing or closing a modified drawing.

## Modeling tools

- **Modeling → Split Line or Curve…** divides a line or quadratic Bézier curve into two edges with a shared endpoint. The percentage sets the distance along a line or the Bézier parameter on a curve.
- **Modeling → Join Free Endpoints** moves the unique closest pair of free endpoints to their midpoint when the two edges share a layer and piece assignment and the gap is at most 5 mm.
- **Dart** takes the first leg end, the apex, and the second leg end. It previews the legs, adds them to the Construction layer, and records both legs as one undoable action.

Split and join work on manually drawn edges in pieces without point grading rules, or on unassigned edges.

### Cut, spread and transfer a dart

Start with a piece that has one closed outline of straight lines or a rectangle. Draw a line with **Exact Input** or with snapping, then select it and use **Pattern → Move Selection to Construction Layer**. For cuts and spreads, both endpoints must lie on the outline and the segment between them must stay inside the piece.

- **Modeling → Cut Piece…** uses one selected construction line to make pieces A and B. Each receives a closed outline with its own copy of the cut edge. The panels keep their original positions, so the cut edges coincide on the canvas. Use the piece names in the objects list to distinguish them.
- **Modeling → Slash and Spread…** uses one selected construction line. Enter the additional width in millimeters. One panel moves perpendicular to the cut, and the new outline bridges the gap at both ends. Use the added width for a pleat or gathering. Fold lines and gathering marks are added manually.
- **Modeling → Close and Transfer Dart…** uses three selected construction lines. Draw two equal legs from an interior apex to the same straight edge with **Dart**. Draw an outlet from the same apex to a different edge. Select all three lines in the objects list. SOFIT rotates a section around the apex, closes the old dart, and opens the transferred dart in the main outline.

The commands consume the selected guides and replace the outline in one undoable action. SOFIT checks the source and result for closure, self-intersections and overlapping edges, and recalculates the stored seam allowance. If validation fails, the editor reports the reason and keeps the original geometry. New outlines use ordinary Main-layer line entities saved and exported through the existing SOFIT, PDF and DXF paths.

This workflow supports one straight internal cut at a time, parallel spread, and dart closure with a transfer outlet. It requires manual geometry and one simple straight-sided outline. Curves, holes, multiple cuts, unequal or shaped dart mouths, and pivot spreading are outside this workflow. Unassign or remove other Construction and Allowance objects from the target piece before modeling. Pieces with generated geometry, point grading rules, notches, or saved fabric placements cannot be modeled with these commands.

## Parametric drafting

1. Open **Pattern → Parametric Draft…**. In **Measurements**, add `chest` with value `920` mm. In **Parameters**, add `ease` with formula `15`.
2. In **Points**, enter `A` with X and Y formulas `0`, `B` with X `chest / 4 + ease` and Y `A.y`, and `C` with X `B.x / 2` and Y `-25`.
3. In **Lines and curves**, enter `AB` with start `A` and end `B`. Leave the control point blank for a straight line, or enter `C` for a quadratic Bézier curve.
4. Click **Validate**, then **Apply**. Reopen the draft and change `chest` to `1000`. The X coordinate of `B` increases from 245 to 265 mm. Undo and Redo restore the inputs and geometry together.
5. Save and reopen the project to continue editing the formulas.

Identifiers are case-sensitive and use ASCII letters, digits and underscores, with a letter or underscore first. Measurements and parameters share one namespace. Formulas can use `+`, `-`, `*`, `/`, parentheses, a decimal point, scientific notation and named point coordinates such as `B.x` and `B.y`. Coordinates are in mm and Y increases downwards. Points start on the Construction layer and edges on the Main layer.

Edit or delete generated objects in the draft editor. Piece and layer assignments are retained when a generated object's name stays the same. Renaming a point also requires updating formulas and edges that reference it. Review grade rules and fabric placements after editing the base contour.

## Draft a skirt from measurements

1. Open **Pattern → New Pattern from Template…** and choose a gathered or A-line skirt.
2. Enter hip circumference, finished length, extra opening circumference, and casing depth in mm. The A-line template also takes extra hem circumference.
3. Finish the wizard. It creates a new editable drawing with one full panel to cut twice, a casing fold guide, a lengthwise grainline, and a 10 mm perimeter allowance.
4. Join the side seams, finish the raw edges, and turn the top down along the casing fold. Fit the elastic at the waist and finish the hem. Check the fit with a test garment before cutting final fabric.

To make another size, add `hip` and optionally `length` in the size table. Opening ease, flare and casing depth are shared draft parameters.

## Pattern pieces

1. Draw the boundary and any construction lines. Select the objects in **Drawing Objects** (Ctrl or Shift for several).
2. Choose **Pattern → Create Piece from Selection…** and name the piece. Use **Assign Selection to Piece…** to add or transfer objects later.
3. Move guides and markings to the construction layer using **Pattern → Move Selection to Construction Layer**. The contour uses the piece's main-layer lines, rectangles and quadratic curves.
4. Open **Pattern → Piece, Contour and Allowance…** to check the contour, read its perimeter and axis-aligned width and height, and set an outward seam allowance in millimeters. The allowance follows the boundary when it changes. Use **View → Layers** to control which layers are visible.

Each piece can have one simple outer contour. The checker accepts unordered edges and either winding direction. It joins endpoints within 0.000001 mm. Open ends, branching, disconnected loops and intersecting edges are rejected. Construction geometry stays part of the piece and does not contribute to contour measurements.

A piece has a single allowance width. Straight sections are offset by that distance, with mitered joins. Quadratics use an adaptive polygonal approximation with a chord deviation of at most 0.01 mm. Perimeter estimation uses a total error budget of 0.01 mm for the quadratic segments. The allowance of a curved section is approximate. Ambiguous close curve approaches, overlong miters and offsets that would overlap or collapse are rejected. Holes, several contours in one piece and per-edge allowances are outside this release. When an edit invalidates an existing allowance, the document retains the entered width and the allowance disappears until geometry supports it again.

SOFIT 5 saves piece names, membership, layer assignments, layer visibility and allowance widths. Contours, measurements and allowance geometry are rebuilt on load. Individual object visibility is a session setting.

## Grading sizes and fabric layout

1. Open **Pattern → Sizes, Measurements and Grade Rules…**. On **Measurements**, enter the size and one measurement in mm per row. Repeat the size name for more measurements. A size can also have an empty measurement list. With **Use size measurements in draft formulas** enabled, a row overrides a base measurement with the exact same name. Omitted measurements use the base values. Other rows remain available for reference. Opening a format 8 file with sizes keeps its old grading behavior. Enable the option in the grading dialog and save to use format 9.
2. On **Grade Rules**, add a rule for each point that moves in a target size. Select the piece and the point in base coordinates, then enter ΔX and ΔY in mm. Coincident points of the same piece share the offset. Rectangle corners and quadratic control points are available. Offsets are added after formula evaluation and are looked up by the original base coordinates. Manual geometry continues to move by the existing offsets.
3. Enable the sizes to compare in **Pattern → Size Overlays**. The menu swatches match the canvas colors. **File → Export DXF…** and **Export PDF for Printing…** offer the base pattern or a single target size.
4. Open **Pattern → Fabric Layout…**, set the width in mm, choose a piece and size, and click **Add copy**. Add each required copy, drag the copies inside the fabric, and use **Turn 180°** where the fabric allows it. The declared grainline is aligned with the fabric length. Without a grainline the original orientation is used. The outline includes the seam allowance when defined.
5. Choose a cut mode before adding copies. **Left + right pair** adds one original and one mirrored contour per unit of quantity. You can also add either hand by itself. The reflection preserves the lengthwise grain direction.
6. For on-fold pieces, enter the working width of the folded fabric and select the side with a straight outer cutting edge parallel to the grain. Use a finished cutting contour with a zero automatic allowance. Dragging is restricted to movement along the fold.
7. Enable **One-way nap or print** to require the original grain direction across the layout. Use **Fix orientation for new copies** to apply the same restriction to individual copies. Otherwise an aligned 180° turn is available. **Auto arrange** tries three deterministic orders with up to 200 plotted copies, places fold copies along their assigned edge, and checks the outlines for overlap and fit.
8. Open **Cutting report…** to review counts by piece and size, working width, used length, utilization, and warnings. Counts refer to plotted copies per cutting layer. Each on-fold entry yields one unfolded piece. The project file retains these settings.
9. Use **Export layout DXF…** to export a valid layout in millimeters with separate CUT, FOLD, GRAINLINE and FABRIC layers. On-fold CUT paths are open along the marked fold. For cutting, use the CUT layer.
10. Read the length from the fabric start to the furthest cut edge. Resolve reported overlaps and pieces outside the fabric before using the estimate. Click **OK** and save the pattern to retain the width and positions.

Grade rules reference the piece and exact base point coordinates. After moving base geometry, review rules labelled **Missing point**. If a contour joins endpoints with slightly different coordinates, give both points the same offset to keep the join. Fabric layout validates contours and uses flattened cut polygons. Recheck saved placements after changing size measurements. The PDF and DXF actions export patterns.

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
