# SOFIT 0.1 — independent CAD prototype

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
- Two clicks create a segment; right-click cancels.
- New/Open/Save (`.sofit` JSON format) and Export SVG.
- Can read legacy `.opcad` files from the initial prototype; newly saved projects use `.sofit`.
- No undo, parametric drafting, GRAFIS file compatibility, or unsaved-change warning yet.

## Licensing/legal
Code is intended for GPLv3 distribution, but check and include the complete GPLv3 license text before publishing. Qt library license obligations apply. Check SOFIT trademark availability before public branding. Independent implementation only.
