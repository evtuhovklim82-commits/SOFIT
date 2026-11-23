# SOFIT 2.4 Windows Professional

Free/open-source clothing-pattern editor targeting Windows 10/11 x64. The native Linux build remains available.

## Windows .exe via GitHub Actions
1. In the SOFIT repository, open **Actions → Windows portable build → Run workflow**.
2. After completion, download the `SOFIT-Windows-x64` artifact and unpack the ZIP inside it.
3. Run `SOFIT.exe` on Windows. Keep it alongside the bundled Qt DLLs, platform plugins and `examples` folder.
4. Open a bundled example from **File → Open**, or pass a quoted project path to `SOFIT.exe`. **Save As** makes an editable copy at a new location.

### Canvas and workspace

- Drag from empty space left to right to select objects fully enclosed by the rectangle. Drag right to left to select intersecting objects. Hold **Ctrl** to add the rectangle results or toggle an individual object.
- Right-click an object in Select mode for geometry editing, deletion and Fit Drawing. A right-click on a selected object preserves the current selection. In drawing tools it cancels the active drawing.
- Inspect the selection in **Drawing Objects**. Double-click a free point, line or curve, or use **F2**, to open exact geometry editing. Change the shortcut under **View → Keyboard Shortcuts**.
- Move or hide toolbars and panels, then close SOFIT to save the workspace. **View → Reset Panel Layout** restores the default arrangement.

### Exchange with GRAFIS

Use `.sofit` as the editable SOFIT master. For SOFIT → GRAFIS, export the required size from **File → Export DXF** and use the **AutoCAD DXF** option in a new GRAFIS style. Check a known dimension and inspect contours and markings after import. GRAFIS documents that DXF without RUL enters the first size. [GRAFIS DXF import](https://help.grafis.de/en-gb/Content/ExportIImport/Import_von_Sprungwertschnitten.htm)

For GRAFIS → SOFIT, retain the original GRAFIS model and export production pieces for the required sizes. SOFIT 2.4 can export DXF. Import of DXF and AAMA/ASTM grading rules into SOFIT remains pending. Use PDF for printing and review. Print at actual size and measure the calibration mark. [GRAFIS export steps](https://help.grafis.de/en-gb/Content/ExportIImport/Schrittfolge_fuer_den_Export.htm)

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
- SOFIT opens `.sofit` formats 1–13 and legacy format 1 `.opcad` drawings. It writes format 9 for drafts with size measurements enabled and at least one size, format 8 for other drafts, and format 7 for projects without a draft. Edge allowances or automatic notches use format 10. Silhouette-only history uses format 11 (SOFIT 1.7+). Contour history uses format 12 (SOFIT 1.8+). Saved seam links use format 13 (SOFIT 2.1+). SVG export includes enabled layers and uses millimeters.
- SOFIT prompts to save changes before replacing or closing a modified drawing.

## Workspace

Use the menus or the Workflow toolbar to move among the five stages. Commands remain available
as the project evolves, so you can return to construction or modeling after checking graded pieces.

1. **Construction**. Start from a template, edit a parametric draft, or draw the base with exact input and drawing tools.
2. **Modeling**. Shape the silhouette, cut and spread, transfer darts, and inspect the construction tree.
3. **Grading**. Define sizes and rules, compare size overlays, and diagnose contours and marks by size.
4. **Pieces**. Group contours into production pieces and check seam allowances, grainlines, and notches. Create a piece earlier when a modeling command needs one.
5. **Layout**. Select sizes and pieces for fabric placement, check the cutting report, and export the marker.

**View → Keyboard Shortcuts** saves personal bindings for selected workflow commands.
**View → Workflow** controls the toolbar. Both settings are separate from the pattern file.
**Help → GRAFIS ⇄ SOFIT** links to the publisher's documentation for corresponding tasks.

## Rotating formula construction objects

1. Show **View → Layers → Construction Lines**. Select construction points, lines or curves in the Objects dock or Ctrl-click them on the canvas. Use **Construction → Move Selection to Construction Layer** for formula edges that are currently on the Main layer.
2. Open **Modeling → Rotate Construction…**. Choose a named formula point outside the selection. Enter an angle in degrees. Positive angles turn clockwise on the drawing.
3. Open **Modeling → Construction Tree** to see the inputs and pivot. Double-click the step to change its angle. SOFIT replays the history with each size's measurements and checks dependent operations before committing the edit. Undo and Redo restore the geometry and history together.

SOFIT rotates the selected objects as recorded. Include any separate construction points that need to move with an edge. The operation is limited to construction geometry and uses the same angle in each size.

## Checking linked seams

1. Select the named formula or modeling edges on both pieces. Open **Pieces → Seams → Match Selected Seam Sections**, assign the edges to A and B, confirm the sewing directions and set the target ease and tolerance. Use **Save Seam Link** to keep the relationship with the project.
2. Set a notch count and, optionally, a shared cutting allowance. SOFIT walks each entire section in every size and distributes the marks proportionally from the sewing starts. A mark can move to another edge in the same chain as measurements change. A shared width overrides any piece default or individual allowance on the covered edges. Removing it restores those settings.
3. Open **Modeling → Construction Tree** to review the base and every size. Activate a saved link to change the target ease, tolerance, notch count or shared width, or to remove the link. These edits support Undo and Redo. **Diagnose Pieces by Size** shows the seam checks with the piece and kit results. The cutting report in **Fabric Layout** includes the linked seam checks for the sizes on the marker.
4. Export the individual pattern for the required size in PDF or DXF to include the linked notches. Fabric layout uses the resulting cutting outlines. Its exports contain the cutting outlines and layout guides. Seam diagnostics display out-of-tolerance ease for review.

Each section is one open chain of named lines or quadratic curves on a single piece. A broken chain, missing edge, conflicting shared width, invalid linked cutting contour or notch at a junction rejects the edit for the entire project. A contour operation that consumes linked edges requires removing or replacing the link first. Ease is a measured difference with a tolerance. Shape correction and local ease distribution remain manual.

The trouser fitting block can create links for the side seam and the two sections of the inseam. The side seam tracks length and ease, while the upper and lower inseams each get a pair of notches. The [graded trouser example](examples/linked-trousers.sofit) includes these links and two additional sizes. Check the fit before setting final cutting allowances.

## Professional workflow in 2.0

- In **Modeling → Construction Tree**, activate a silhouette, slash-and-spread, pleat, or gathering step to edit its numeric settings. SOFIT rebuilds the base pattern and all formula sizes, keeps named edge references and restores the previous state if any step fails. Undo and Redo cover the entire edit. Clear the fabric layout before changing an existing step.
- **Shape Silhouette** can also change the length, scaling vertically from the top of each piece. Width settings remain independent. Existing shape operations keep their original length unless edited. Files using length scaling or the new `positive(expression)` formula require SOFIT 2.0.
- **Construction → New Pattern from Template → Bib apron with waist and neck ties** creates a one-piece body, two waist ties and two neck ties. The body uses half of the entered widths on either side of the centre. Each tie has a lengthwise fold guide at its centre and a seam-line width of twice the finished width. All three pieces start with 10 mm allowances. Named formulas check the relative dimensions of the bib and ties again whenever measurements or formula sizes change.
- **Diagnose Pieces by Size** groups contour, allowance, notch and cut-quantity checks by size. For a size in the marker it shows the copies present and any missing or extra copies for one kit. The Construction Tree compares opposite cut seams, including notch distances in millimetres from the sewing start.
- Open [the apron example](examples/bib-apron.sofit) to inspect a graded kit on 1,500 mm fabric. Open **Layout → Fabric Layout** for the cutting report and 1:1 tiled PDF or DXF export. Open [the skirt example](examples/graded-skirt.sofit) and edit its silhouette step to try recalculation and Undo/Redo with a second size.

## Professional workflow in 1.9

1. Select one manual point, line or quadratic Bézier curve and open **Edit → Edit Geometry**. Enter coordinates in millimeters, or use length and angle for a line. With **Move connected endpoints** enabled, coincident endpoints within 0.000001 mm on the same piece and layer move together. The curve control point remains independent. The whole edit is one Undo/Redo step.
2. Add labels, grainlines, notches, and allowances. Use **Diagnose Pieces by Size** to review seam width, height and perimeter, cut width and height where allowances exist, contour errors and marking warnings. Use the seam tools for length and ease checks.
3. Define the size table and grade rules, then inspect the overlaid contours and repeat the seam checks in the target size.
4. In **Fabric Layout**, arrange the copies and open **Cutting report**. It compares each piece's cut count with the plotted count for one kit per represented size, including pieces with zero copies. Review unmatched hands, unspecified grain, invalid outlines, overlaps, fabric bounds, fold and rotation conflicts, and material usage. For multiple kits, compare the reported counts against the intended quantities. Check materials, fit and assembly with a sample before cutting the production run.
5. Export the layout in millimeters as DXF or as a tiled A4 PDF. Print the PDF at actual size and measure the 50 mm calibration line before assembling the sheets. Layout files contain fabric boundaries, cut outlines, fold lines and configured grainlines. For labels, notch marks and seam geometry use the individual-pattern exports.

Numeric editing covers manually drawn points, straight segments and quadratic curves. Edit formula-driven geometry through **Parametric Draft**. The numeric editor does not move rectangles or circles and leaves pieces with point grading rules to the grading workflow. Valid layout exports can still be written for partial kits. SOFIT 1.9 uses the existing project format.

## Seam tools

- In the Objects list, select lines and quadratic Bézier curves. **Pieces → Seams → Measure Selected Lines and Curves** reports their individual lengths and total in millimeters. Quadratic lengths use subdivision with chord and control-polygon bounds. The absolute length error budget is 0.001 mm per curve within the supported coordinate range.
- With both seams selected, open **Match Selected Seam Sections** and assign the edges to A and B. Each section must be one connected open chain on one piece, or a chain of unassigned edges for measurement. The dialog shows both lengths, signed ease (B − A), and the result against the target ease and tolerance. **Compare graded size** checks the same source edges and target in the chosen size. Use **Save Seam Link** to keep the match in the project.
- **Place Paired Notches** distributes marks at equal fractions of each section length. The dialog displays each start endpoint and lets you reverse B. Both pieces need valid closed outlines. If a requested mark lands on a junction, choose a different count. Marks are stored on individual edges at normalized arc-length positions and are recomputed from the graded geometry. This distributes ease proportionally and does not prescribe where to concentrate it. The graded result preserves each mark's fraction on its own edge; matching a particular graded size again may require adjusting its layout separately. Use **Clear Automatic Notches on Selected Edges** to remove stored marks.
- **Allowance for Selected Edges** overrides the piece default on selected lines and quadratic curves. A width of zero follows the seam, and resetting an override uses the piece default. Rectangles use the piece default. Offsets use polygonal approximations for curves, mitered corners and steps at straight or nearly tangent width transitions. The editor rejects unsafe corners and outlines instead of clipping them automatically. Check the cut outline for each graded size before export.
- **Piece, Contour and Allowance** reports open outlines, intersecting or overlapping geometry, and objects with segments shorter than 1 mm. The short-edge report is advisory. Intersection diagnosis follows the existing simple-contour validator, which can reject ambiguous close curves and reports the first topology failure.
- **Grading → Diagnose Pieces by Size** lists contour failures, failed allowance generation, missing marks and marks whose outward projection misses the cut outline.
- Edge edits and paired notch placement can be undone and redone. The canvas, PDF and DXF derive cut outlines and notch marks from the document and the chosen size. Clear automatic notches before splitting their edge and clear edge metadata before using cut, spread or dart transfer on its piece.

## Modeling tools

- **Shape Silhouette** records a replayable step on pieces made entirely of assigned formula lines, quadratic curves and construction points. Each step stores the named objects, the axis relative to the current bounds, and the top and bottom scales. Unassigned formula points remain at their draft positions.
- **Modeling → Construction Tree…** opens a dock with the ordered formula and modeling steps, their inputs, new panels and paired cut edges. It follows Undo/Redo. Rejected recalculations identify the step and, when applicable, the size. **Remove Last Step** rebuilds from the formulas and restores consumed guides. Removing a split also removes its added panel and panel-specific settings. Clear manual objects and fabric placements from that panel first.
- Contour history covers straight cuts, straight relief seams and yokes, parallel spread, box pleats, gathering fullness and equal-leg dart transfer. To record a step, define the outline and guides in **Construction → Parametric Draft…**. Assign the named outline lines to the piece, and place the named guide lines on the Construction layer. Guides can stay unassigned or belong to the target piece. Keep formula points and unrelated marks unassigned. For moving an open dart, include the two named V edges in the outline and select them with the outlet guide.
- Each accepted edit replays from the formulas for the base and every size with measurement overrides. Guide attachment, edge order and contour connectivity must stay consistent across sizes. A changed attachment, missing source or invalid contour or allowance rejects the edit and keeps the last valid pattern. Undo/Redo restores geometry and history together. Generated edges keep their references through measurement changes and silhouette steps.
- History pieces use formula-based size measurements. Clear manual point grade rules, existing fabric placements and perimeter notches before adding a step. Contour operations that replace edges also require clearing edge allowances and automatic marks on their inputs. The tree reports whether a later contour step replaced an automatically identified cut pair. A saved seam link blocks operations that would consume its edges. Piece deletion is available after removing contour history.

- **Modeling → Split Line or Curve…** divides a line or quadratic Bézier curve into two edges with a shared endpoint. The percentage sets the distance along a line or the Bézier parameter on a curve.
- **Modeling → Join Free Endpoints** moves the unique closest pair of free endpoints to their midpoint when the two edges share a layer and piece assignment and the gap is at most 5 mm.
- **Dart** takes the first leg end, the apex, and the second leg end. It previews the legs, adds them to the Construction layer, and records both legs as one undoable action.

Split and join work on manually drawn edges in pieces without point grading rules, or on unassigned edges.

### Cut, straight relief seam, yoke, spread and dart transfer

Start with a piece that has one closed outline of straight lines. A manually drawn rectangle also works. Use a named formula line for a replayable operation or draw a manual line with **Exact Input** for a direct edit. Select the guide and use **Construction → Move Selection to Construction Layer**. For cuts and spreads, both endpoints must lie on the outline and the segment between them must stay inside the piece.

- **Modeling → Cut Piece…**, **Straight Relief Seam…** and **Straight Yoke…** use one straight construction line to split the piece. Each panel receives a closed outline and its own copy of the same sewing edge. The panels stay in position on the canvas. The guide direction and source contour order determine which side becomes the added panel. Use the piece list to check and rename either side.
- In the Construction Tree, expand **Paired cut edges** for a replayable split to compare lengths and corresponding notch fractions across all configured sizes. Activate the pair to open **Match Seam Sections** with both sides loaded and the correspondence reversed for the opposing edge directions. The dialog can place paired notches using the existing seam tools.
- **Modeling → Slash and Spread…** uses one selected construction line. Enter the additional width in millimeters. One panel moves perpendicular to the cut, and the new outline bridges the gap at both ends. For built-in placement guides, use the pleat or gathering command.
- **Modeling → Close and Transfer Dart…** uses three selected construction lines. Draw two equal legs from an interior apex to the same straight edge with **Dart**. Draw an outlet from the same apex to a different edge. Select all three lines in the objects list and identify the outlet in the dialog. SOFIT rotates a section around the apex, closes the old dart, and opens the transferred dart in the main outline.

- **Modeling → Move Open Contour Dart…** closes an inward V already cut into the main outline and transfers its angle to a new edge. Select the two main-layer legs and a construction line from their apex to the new outlet. The new legs retain equal lengths. This also works on the V made by the previous command.
- **Modeling → Add Box Pleat…** adds the entered total intake and places construction lines at both sides and the center of the added strip. Bring the outside marks to the center and true the pleat at the joining edges for the intended fold direction.
- **Modeling → Add Gathering Fullness…** adds the entered width and marks both sides of the gather zone on the Construction layer. Distribute the fullness to the joined seam length during assembly.

The cut, spread and dart commands consume the selected guides and replace the outline in one undoable action. SOFIT checks the source and result for closure, self-intersections and overlapping edges, and recalculates the stored seam allowance. If validation fails, the editor reports the reason and keeps the original geometry. New outlines use ordinary Main-layer line entities saved and exported through the existing SOFIT, PDF and DXF paths.

The contour tools support one straight cut at a time, parallel spread and equal-leg darts. A closed dart transfers its fullness into an outlet on a different edge. The straight relief and yoke commands divide existing geometry along the selected guide. Curved princess seams, dart intake incorporated into a seam, automatic seam truing, multi-cut spreads and parametric raglan remain outside this workflow. Unassign unrelated Construction and Allowance objects from the target piece before modeling. Remove existing point grading rules, notches, edge-specific allowances and saved fabric placements before commands that replace edges. A piece-wide allowance carries over when the resulting offset validates. Add matched notches to the new edges with the Seams menu.

**Modeling → Shape Silhouette…** scales the horizontal distance from a vertical axis at the center, left or right of the piece. Equal top and bottom percentages widen or narrow the piece uniformly. Different values taper or flare it. The operation keeps Y coordinates and uses the base outline's vertical bounds to interpolate scale between top and bottom. It transforms line endpoints and quadratic controls and converts a main-layer rectangle to four edges. A tapered quadratic uses transformed control points as a new quadratic. Check smooth joins and adjoining seam lengths after shaping.

Silhouette shaping retains edge-specific allowances and arc-length notch fractions and remaps existing manual grade offsets on manually drawn pieces. SOFIT rejects the edit if the resulting base or affected size has an invalid contour or allowance. Remove saved placements and perimeter-percentage notches before shaping. Keep Construction visible when exporting fold and gather guides. Undo and Redo restore the outline, guides and associated changes in a single step.

## Parametric drafting

1. Open **Construction → Parametric Draft…**. In **Measurements**, add `chest` with value `920` mm. In **Parameters**, add `ease` with formula `15`.
2. In **Points**, enter `A` with X and Y formulas `0`, `B` with X `chest / 4 + ease` and Y `A.y`, and `C` with X `B.x / 2` and Y `-25`.
3. In **Lines and curves**, enter `AB` with start `A` and end `B`. Leave the control point blank for a straight line, or enter `C` for a quadratic Bézier curve.
4. Click **Validate**, then **Apply**. Reopen the draft and change `chest` to `1000`. The X coordinate of `B` increases from 245 to 265 mm. Undo and Redo restore the inputs and geometry together.
5. Save and reopen the project to continue editing the formulas.

Identifiers are case-sensitive and use ASCII letters, digits and underscores, with a letter or underscore first. Measurements and parameters share one namespace. Formulas can use `+`, `-`, `*`, `/`, parentheses, a decimal point, scientific notation and named point coordinates such as `B.x` and `B.y`. The function `quadlen(ax, ay, cx, cy, bx, by)` measures a quadratic Bézier seam from start to end through its control point, using the same length calculation as the seam tools. The function `positive(expression)` returns its argument when it is greater than zero and produces a named formula error otherwise. Coordinates are in mm and Y increases downwards. Points start on the Construction layer and edges on the Main layer.

Edit or delete generated objects in the draft editor. Piece and layer assignments are retained when a generated object's name stays the same. Renaming a point also requires updating formulas and edges that reference it. Review grade rules and fabric placements after editing the base contour.

## Draft a skirt from measurements

1. Open **Construction → New Pattern from Template…** and choose a gathered or A-line skirt.
2. Enter hip circumference, finished length, extra opening circumference, and casing depth in mm. The A-line template also takes extra hem circumference.
3. Finish the wizard. It creates a new editable drawing with one full panel to cut twice, a casing fold guide, a lengthwise grainline, and a 10 mm perimeter allowance.
4. Join the side seams, finish the raw edges, and turn the top down along the casing fold. Fit the elastic at the waist and finish the hem. Check the fit with a test garment before cutting final fabric.

To make another size, add `hip` and optionally `length` in the size table. Opening ease, flare and casing depth are shared draft parameters.

### Bodice and set-in sleeve fitting block

In **Construction → New Pattern from Template…**, choose **Bodice and set-in sleeve (fitting block)**.
Enter the dimensions and wearing ease in millimetres. Use the waist level and the level of the
neck-side shoulder point as horizontal references. Measure circumference around the whole body
and horizontal shoulder run from the neck-side point to the shoulder tip. Measure shoulder drop
vertically between those two points. Sleeve length is measured from the crown to the finished
opening. Opening circumference includes the ease needed for the hand or a planned closure.

The draft uses its own geometric construction. Coordinates below are local to each piece,
with horizontal distance increasing away from the center on each bodice half and vertical
distance increasing downward from the neck-side shoulder level.

1. Use quarter bust width **C = (bust + bust ease) / 4** on each half. Compute quarter waist width
   **W = (waist + waist ease) / 4** and suppression **D = C − W**. Place the underarm at
   **(C, armhole depth)** and the side waist at **(C − D/2, bodice length)**. Use one waist dart
   centered on the waist edge of each half with total intake **D/2** and the entered dart length.
   The two sides have matching seam lengths. Closing the darts gives four quarters of width W.
2. Draw the neckline as a quadratic Bézier from **(0, neckline depth)** to **(neck width, 0)**
   with control point **(neck width, neckline depth)**. The front and back use their respective
   neckline depths. From the neck-side point, draw the shoulder by the entered run and drop.
   Draw each armhole from the shoulder tip to the underarm as a quadratic Bézier. The control
   point is at underarm level and its horizontal position is 8 mm inside the front shoulder tip
   and 2 mm outside the back shoulder tip.
3. For the sleeve, put the crown at **(0, 0)** and put the underarm points at **(−H, cap height)**
   and **(H, cap height)**, where **H = (upper arm + upper arm ease) / 2**. Use two quadratic
   Béziers with a common horizontal tangent at the crown and controls at **(−0.70H, 0)** for
   the front and **(0.55H, 0)** for the back. Join the underarms to the corresponding sides of
   a centered opening at the entered sleeve length using two equal-length straight edges.
4. The wizard reports **cap length minus armhole length** for each half separately and compares
   each value to the entered target and tolerance. Decline the preview to adjust cap height
   or the other inputs. The default target is a starting value for the built-in example.
   Choose the ease for the intended fabric and check its distribution in the sample.
   [Utah State University Extension, set-in sleeve judging guidance](https://extension.usu.edu/utah4h/files/fair-judging-manual.pdf)

The wizard requires at least 6 mm waist suppression per quarter and at most 30% of quarter bust
width, enough room below the underarm for the dart, a shoulder inside the bust width, and a cap
that fits within the sleeve length. It checks that all three sewing contours close. The preset
uses a 920 mm bust, 740 mm waist and 87 mm cap height. Equal-width front and back quarters and a
shared shoulder-to-waist length approximate a balanced figure. Plan a fitting sample to adjust
front/back balance, bust shaping, shoulder angle, armhole shape, and sleeve pitch. The block has
waist darts only, with no separate bust dart, elbow shaping, closures or finishing details.

The center edges of the bodice indicate folds. The sleeve is a mirrored pair. The drawing uses
sewing lines with zero allowance. Add allowances after fitting, keeping the fold edges at zero.
Keep the Construction layer visible to print or export the dart legs. Control points are also on
that layer. The parts and formulas use the existing project format, and the drawing exports
through the usual PDF and DXF commands. Edit the base inputs in the parametric draft or supply
per-size overrides under **Sizes, Measurements and Grade Rules…**. After changing measurements
or grading, recheck piece contours and both cap sections with **Check Fitting Block by Size…**. The wizard's
drafting checks, target and tolerance apply only at creation.

### Neckline finishes and sleeve variants

Choose a neckline finish in either the bodice or sleeveless dress wizard.

- **Flat collar.** A half collar on the center-back fold, with exact copies of both neckline curves oriented into one sewing edge. The construction guide identifies the shoulder join. The outer curve widens toward the shoulder. Cut upper and undercollar layers, then adjust the roll and turn of cloth for the fabric.
- **Straight stand.** A half stand on the center-back fold. Each straight sewing section equals the corresponding bodice neckline's measured arc length. The shoulder guide separates the front and back sections. This preset leaves closure overlap, shaping to the neck, interfacing and turn of cloth to the maker.
- **Neck facing.** Separate front and back pieces on the fold with copied neckline curves. The shoulder edges extend along the same slope as the bodice shoulder and have equal length.

The sleeve in the bodice wizard defaults to the fitted sleeve from 1.5. **Wide hem**
adds width at the opening and keeps the original cap. **Puff** raises and widens
the cap, adds width to the hem, and includes a straight sleeve band. The entered
crown-to-hem length stays the same. The entered added width applies to both the
upper arm and hem of the puff. The cuff measurement sets the band circumference.
Quarter marks pair the hem with the band. Midpoint marks pair each cap section
with its corresponding armhole section for distributing gathers.

For the puff, the cap target is normal ease plus the intended gathering per
section. The wizard and **Check Fitting Block by Size…** compare achieved and
target lengths. Adjust the added width, crown rise or target for the desired
gather distribution. The preset does not solve the cap shape for a target ease.
That report also compares front and back neckline sections with their finish,
and the puff hem with the band. The equations update with size measurements.
Manual grade offsets are included in the report and can create mismatches.

These are sewing-line patterns. Add allowances after checking the sample and
keep fabric-fold edges at zero. Raglan, tailored collars, closure extensions
and automatic sewing-seam truing are outside these presets. Opening a 1.5 project
in 1.6 preserves its original draft. Projects using `quadlen` require 1.6 or newer.

### Trouser fitting block

Choose **Trousers (fitting block)** in the same wizard. Enter the waist and hip
circumferences and wearing ease, four vertical distances from the waist (hip,
seat, knee and hem), and the finished knee and hem circumference of **one leg**.
The wizard creates separate front and back sewing contours, each for a mirrored
pair. The seat and knee lines and waist dart legs are on the Construction layer.

- For each half, start with eased quarter hip width **H = (hip + hip ease) / 4**.
  Use eased quarter waist **W = (waist + waist ease) / 4** and suppression **D = H − W**.
  The raw waist is **H − D/2**, with one centered dart of intake **D/2**. The
  wizard requires suppression between 6 mm and 35% of H, and the dart must end
  above the hip. A quadratic side seam blends the waist into the hip.
- Extend the front seat **0.17H** and the back seat **0.31H** inward from the center
  line. Each quadratic crotch curve joins that extension to the center at hip
  height, with its control at center and seat height. Hip, seat, knee and hem
  must occur in that order with clearance between levels.
- The leg reference axis is midway between the outer seat point and the mean
  crotch endpoint. The outer knee and hem sit one quarter of their entered
  circumferences outside the axis. Each inner endpoint sits one quarter inside
  the axis, shifted by the mean extension minus the piece's own extension.
  The two panel widths therefore sum to the entered circumference at each level,
  with more width in back. The matching segment vectors keep the front and back
  inseams and side seams equal through the knee.

The preset uses a level waist, proportional crotch extensions and straight
segments below the seat. Check rise, crotch shape, thigh room, front/back
balance, and knee and hem placement on a fitting sample. Add the waistband,
closure, pockets and finishing details after fitting.

### Sleeveless dress fitting block

Choose **Sleeveless dress with waist seam (fitting block)**. The upper section
reuses the bodice formulas above. The lower section uses two skirt halves on
the fold with a waist seam. Use the same eased waist on all four pieces.

- The skirt uses **H = (hip + hip ease) / 4** and suppression **D = H − W**, where
  **W = (waist + waist ease) / 4**. Each waist edge is **H − D/2**, with a centered
  dart of intake **D/2**. The sewn waist is **W** on each bodice and skirt half.
  In this construction the dart legs closest to the fold sit at **W/2**, so
  the closed dart locations meet across the waist seam.
- From the waist, a quadratic side seam runs to the hip at width **H**, with its
  control at width **H** and halfway down the hip depth. The side continues
  straight to a hem of width **H + hem flare / 4**. Skirt suppression must be
  between 6 mm and 35% of H, and the dart and hip levels must be above the hem.

The default front and back skirt halves share their shaping. Adjust balance,
sleeveless armhole fit, closure, and neckline and armhole finishing on a fitting
sample. The fitting blocks use zero allowance, with center folds on all dress
pieces. Review ease and fit on the intended figure before using the outlines to
cut final fabric.

### Check seams after grading

With **Use size measurements in draft formulas** enabled, add overrides by
measurement name in **Sizes, Measurements and Grade Rules…**. For trousers,
typical overrides are `waist`, `hip`, `rise`, `knee_depth`, `length`, `knee` and
`hem`. For the dress, use `bust`, `waist`, `hip`, `bodice_length`, `hip_depth` and
`skirt_length` as needed. Read the complete names in **Parametric Draft…**.

Open **Pieces → Seams → Check Fitting Block by Size…**. Select the base or
any named size. The report measures that size's final geometry, including manual
grade offsets, and flags invalid contours, missing seams and differences outside
the selected tolerance. It checks shoulders and bodice sides, both cap sections
and the sleeve seam, trouser sides and both inseam sections, and the dress
skirt sides and front/back waist lengths after subtracting dart intake.
The sleeve target and tolerance are temporary. Formula and size edits use the
general draft evaluator, so the wizard's input ranges apply at creation.
Repeat the contour and seam checks for every size before PDF or DXF export in
millimeters.

## Pattern pieces

1. Draw the boundary and any construction lines. Select the objects in **Drawing Objects** (Ctrl or Shift for several).
2. Choose **Pieces → Create Piece from Selection…** and name the piece. Use **Assign Selection to Piece…** to add or transfer objects later.
3. Move guides and markings to the construction layer using **Construction → Move Selection to Construction Layer**. The contour uses the piece's main-layer lines, rectangles and quadratic curves.
4. Open **Pieces → Piece, Contour and Allowance…** to check the contour, read its perimeter and axis-aligned width and height, and set an outward seam allowance in millimeters. The allowance follows the boundary when it changes. Use **View → Layers** to control which layers are visible.

Each piece can have one simple outer contour. The checker accepts unordered edges and either winding direction. It joins endpoints within 0.000001 mm. Open ends, branching, disconnected loops and intersecting edges are rejected. Construction geometry stays part of the piece and does not contribute to contour measurements.

A piece has a single allowance width. Straight sections are offset by that distance, with mitered joins. Quadratics use an adaptive polygonal approximation with a chord deviation of at most 0.01 mm. Perimeter estimation uses a total error budget of 0.01 mm for the quadratic segments. The allowance of a curved section is approximate. Ambiguous close curve approaches, overlong miters and offsets that would overlap or collapse are rejected. Holes and several contours in one piece are outside this release. For per-edge widths, see **Seam tools**. When an edit invalidates an existing allowance, the document retains the entered width and the allowance disappears until geometry supports it again.

SOFIT 5 saves piece names, membership, layer assignments, layer visibility and allowance widths. Contours, measurements and allowance geometry are rebuilt on load. Individual object visibility is a session setting.

## Grading sizes and fabric layout

1. Open **Grading → Sizes, Measurements and Grade Rules…**. On **Measurements**, enter the size and one measurement in mm per row. Repeat the size name for more measurements. A size can also have an empty measurement list. With **Use size measurements in draft formulas** enabled, a row overrides a base measurement with the exact same name. Omitted measurements use the base values. Other rows remain available for reference. Opening a format 8 file with sizes keeps its old grading behavior. Enable the option in the grading dialog and save to use format 9.
2. On **Grade Rules**, add a rule for each point that moves in a target size. Select the piece and the point in base coordinates, then enter ΔX and ΔY in mm. Coincident points of the same piece share the offset. Rectangle corners and quadratic control points are available. Offsets are added after formula evaluation and are looked up by the original base coordinates. Manual geometry continues to move by the existing offsets.
3. Enable the sizes to compare in **Grading → Size Overlays**. The menu swatches match the canvas colors. **File → Export DXF…** and **Export PDF for Printing…** offer the base pattern or a single target size.
4. Open **Layout → Fabric Layout…**, set the width in mm, choose a piece and size, and click **Add copy**. Add each required copy, drag the copies inside the fabric, and use **Turn 180°** where the fabric allows it. The declared grainline is aligned with the fabric length. Without a grainline the original orientation is used. The outline includes the seam allowance when defined.
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

CTest checks contour geometry, offsets, file compatibility, formula and size replay, panel seam correspondence, PDF/DXF export and the pattern workflow through the offscreen Qt interface. Use `-DBUILD_TESTING=OFF` for an application-only build.

## Licensing/legal
Code is intended for GPLv3 distribution, but check and include the complete GPLv3 license text before publishing. Qt library license obligations apply. Check SOFIT trademark availability before public branding. Independent implementation only.

## Piece markings and DXF

Choose **Pieces → Piece, Contour and Allowance…** to set the cut quantity, size,
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
