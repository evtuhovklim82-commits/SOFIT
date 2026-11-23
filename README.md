# SOFIT 0.4 — independent CAD prototype

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
- Save patterns in `.sofit` version 4. SOFIT opens versions 1–3 and legacy version 1 `.opcad` drawings. SVG export includes enabled layers and uses millimeters.
- SOFIT prompts to save changes before replacing or closing a modified drawing.

## Pattern pieces

1. Draw the boundary and any construction lines. Select the objects in **Drawing Objects** (Ctrl or Shift for several).
2. Choose **Pattern → Create Piece from Selection…** and name the piece. Use **Assign Selection to Piece…** to add or transfer objects later.
3. Move guides and markings to the construction layer using **Pattern → Move Selection to Construction Layer**. The contour uses the piece's main-layer lines, rectangles and quadratic curves.
4. Open **Pattern → Piece, Contour and Allowance…** to check the contour, read its perimeter and axis-aligned width and height, and set an outward seam allowance in millimeters. The allowance follows the boundary when it changes. Use **View → Layers** to control which layers are visible.

Each piece can have one simple outer contour. The checker accepts unordered edges and either winding direction. It joins endpoints within 0.000001 mm. Open ends, branching, disconnected loops and intersecting edges are rejected. Construction geometry stays part of the piece and does not contribute to contour measurements.

A piece has a single allowance width. Straight sections are offset by that distance, with mitered joins. Quadratics use an adaptive polygonal approximation with a chord deviation of at most 0.01 mm. Perimeter estimation uses a total error budget of 0.01 mm for the quadratic segments. The allowance of a curved section is approximate. Ambiguous close curve approaches, overlong miters and offsets that would overlap or collapse are rejected. Holes, several contours in one piece and per-edge allowances are outside this release. When an edit invalidates an existing allowance, the document retains the entered width and the allowance disappears until geometry supports it again.

SOFIT 4 saves piece names, membership, layer assignments, layer visibility and allowance widths. Contours, measurements and allowance geometry are rebuilt on load. Individual object visibility is a session setting.

## Tests

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

CTest checks contour geometry, offsets, file compatibility and the pattern workflow through the offscreen Qt interface. Use `-DBUILD_TESTING=OFF` for an application-only build.

## Licensing/legal
Code is intended for GPLv3 distribution, but check and include the complete GPLv3 license text before publishing. Qt library license obligations apply. Check SOFIT trademark availability before public branding. Independent implementation only.
