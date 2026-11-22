# AI Dynamic Vibrance Enhance (`v1.0.0`)

**Developer:** `arthurj119` | **License:** [MIT](LICENSE) | **Repository:** https://github.com/arthurj119/AI-Dynamic-Vibrance-Enhance

An open-source, cross-GPU real-time game visual enhancer for **AMD Radeon**, **NVIDIA GeForce**, and **Intel Arc / UHD Graphics** on Windows 10/11. Combines **adaptive dark-area visibility (Dynamic Contrast)**, **vivid perceptual Oklab color boost (Vivid Mode)**, **adaptive filmic exposure**, **dual-scale bilateral clarity**, **specular highlight glow**, **automatic skin-tone protection**, and **11 additional game filters** inside a dedicated **`Alt + X`** slide-out filter sidebar and a flat dark desktop **Control Hub**.

## Architecture

1. **Main Control Hub (Flat Matte Desktop Window)**
   - Frameless Windows 11 dark flat UI with centered horizontal navigation (`Overview`, `Games`, `Presets`, `Legacy`, `About`), live **`Grid | List`** layout switcher (persisted in `profiles.json`), full screenshot/recording capturability, and launches cleanly at center screen (or `--minimized` to tray).
   - **Overview Tab**: Master switch for **In-Game Overlay**, clickable **Sidebar Hotkey** (`Alt + X` default) & **Quick Filter Toggle** (`Alt + V` default) recorder, plus 6 concise tiles/rows (**Game-Only Mode**, **Per-Game Profiles**, **Capture Privacy**, **Split Preview**, **Start with Windows**, **Quick Filter Toggle**).
   - **Games Tab**: Custom **Force Enable (Whitelist)** and **Always Ignore (Blacklist)** `.exe` overrides that apply immediately on top of the built-in game engine detector.
   - **Presets Tab**: Portable community `.json` **Preset Export / Import** per profile slot, quick **Data Folder** shortcut, and individual/all-profile factory resets.
   - **Legacy Tab**: Optional shader export to deploy or remove [AIDynamicVibranceEnhance.fx](shaders/AIDynamicVibranceEnhance.fx) for classic DX9/DX10/OpenGL Exclusive Fullscreen titles via ReShade without cluttering the in-game filter sidebar.
   - **About Tab**: Application details (`v1.0.0`, `arthurj119`, MIT license), GitHub repository link, and community thank-you note.

2. **Focused In-Game Filter Sidebar (`Alt + X` / Custom Hotkey)**
   - Non-activating (`WS_EX_NOACTIVATE`), capture-optional (`WDA_EXCLUDEFROMCAPTURE` toggleable from Control Hub), click-through DX11 overlay with an automatic **Exclusive Fullscreen Guard** (`QUNS_RUNNING_D3D_FULL_SCREEN`) so fullscreen games never get Alt-Tab kicked to the desktop.
   - Clean 4 profile slots (`None`, `1`, `2`, `3`) and **12 unified in-place filters**:
     - **`AI Dynamic Vibrance`** — unified 2-slider control (`Intensity %`, `Saturation boost %`) with built-in dark visibility recovery, adaptive filmic exposure, skin-tone protection, noise-cored bilateral clarity, true black-floor lock, and anti-banding dither.
     - **`Sharpen+ (AMD CAS)`**, **`Dynamic HDR`**, **`Dark / Shadow Lighting`**, **`Clarity / Details`**, **`Brightness / Contrast`**, **`Color Temperature & Tint`**, **`Gamma / Exposure`**, **`Night Mode / Blue Light`**, **`Black & White`**, **`Cinematic Vignette`**, and **`Colorblind Assist`** (`Protanopia`, `Deuteranopia`, `Tritanopia`).

3. **Game-Only Auto-Detection**
   - Read-only detection for **Unity**, **Unreal Engine**, **REDengine**, **RE Engine**, **CryEngine**, **Source**, **SDL/GLFW**, Steam/Epic/GOG/Xbox/Riot/HoYoPlay libraries, game DLLs, and borderless/fullscreen games while pausing cleanly on Desktop, File Explorer, browsers, chat/media apps, editors, and launchers.

4. **Windows Setup Installer (`AIDynamicVibranceEnhance_Setup.exe`)**
   - Self-contained frameless Direct2D installer wizard with **Standard** or **Full (+ Legacy Fullscreen Shader Support)** installation types, custom install folder, Desktop & Start Menu shortcuts, optional Windows startup, and clean uninstaller (`--uninstall`) in Windows Apps & Features.

## Build from Source

Requires Windows 10/11 SDK, CMake `3.20+`, and Visual Studio 2022/2026 C++ Desktop toolchain (`x64`).

```bat
cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

This builds both:
- `build/AIDynamicVibranceEnhance.exe` (portable binary & exports synced `shaders/*.hlsl` / `shaders/*.fx`)
- `build/AIDynamicVibranceEnhance_Setup.exe` (Windows Setup Installer)

## Self-Tests

```bat
AIDynamicVibranceEnhance.exe --self-test
AIDynamicVibranceEnhance_Setup.exe --self-test
```
