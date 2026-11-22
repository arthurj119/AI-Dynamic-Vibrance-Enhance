# AI Dynamic Vibrance Enhance (`v1.0.0`)

**Developer:** `arthurj119` | **License:** [MIT](LICENSE) | **Repository:** https://github.com/arthurj119/AI-Dynamic-Vibrance-Enhance

An open source, cross GPU real time game visual enhancer for **AMD Radeon**, **NVIDIA GeForce**, and **Intel Arc / UHD Graphics** on Windows 10 and 11. It combines adaptive dark area visibility (Dynamic Contrast), vivid perceptual Oklab color boost (Vivid Mode), adaptive filmic exposure, dual scale bilateral clarity, specular highlight glow, automatic skin tone protection, and 11 additional game filters inside a dedicated **`Alt + X`** slide out filter sidebar and a flat dark desktop **Control Hub**.

## Features & Architecture

1. **Main Control Hub (Desktop Window)**
   * Frameless Windows 11 dark flat UI with centered horizontal navigation (`Overview`, `Games`, `Presets`, `Legacy`, `About`), live **`Grid | List`** layout switcher saved in `profiles.json`, full screenshot and recording capturability, and clean center screen startup (or `--minimized` to system tray).
   * **Overview Tab:** Master toggle for the overlay engine, clickable **Sidebar Hotkey** (`Alt + X` default) and **Quick Filter Toggle** (`Alt + V` default) recorder, plus 6 compact tiles or list rows (`Game Only Mode`, `Per Game Profiles`, `Capture Privacy`, `Split Preview`, `Start with Windows`, and `Quick Filter Toggle`).
   * **Games Tab:** Custom **Force Enable (Whitelist)** and **Always Ignore (Blacklist)** `.exe` overrides that apply immediately alongside the built in game engine detector.
   * **Presets Tab:** Portable community `.json` **Preset Export / Import** per profile slot, quick **Data Folder** shortcut, and single or full profile factory resets.
   * **Legacy Tab:** Optional shader export to deploy or remove [AIDynamicVibranceEnhance.fx](shaders/AIDynamicVibranceEnhance.fx) for classic DX9, DX10, and OpenGL Exclusive Fullscreen games via ReShade without cluttering the overlay sidebar.
   * **About Tab:** Application details (`v1.0.0`, `arthurj119`, MIT license), GitHub repository link, and community note.

2. **Overlay Filter Sidebar (`Alt + X` / Custom Hotkey)**
   * Focus safe (`WS_EX_NOACTIVATE`), capture optional (`WDA_EXCLUDEFROMCAPTURE` toggleable from Control Hub), and click through DX11 overlay with an automatic **Exclusive Fullscreen Guard** (`QUNS_RUNNING_D3D_FULL_SCREEN`) so fullscreen games never get Alt Tabbed back to the desktop.
   * 4 clean profile slots (`None`, `1`, `2`, `3`) and 12 unified modular filters:
     * **`AI Dynamic Vibrance`**: unified two slider control (`Intensity %`, `Saturation boost %`) with dark visibility recovery, adaptive filmic exposure, skin tone protection, noise cored bilateral clarity, true black floor anchor, and anti banding dither.
     * **`Sharpen+ (AMD CAS)`**, **`Dynamic HDR`**, **`Dark / Shadow Lighting`**, **`Clarity / Details`**, **`Brightness / Contrast`**, **`Color Temperature & Tint`**, **`Gamma / Exposure`**, **`Night Mode / Blue Light`**, **`Black & White`**, **`Cinematic Vignette`**, and **`Colorblind Assist`** (`Protanopia`, `Deuteranopia`, `Tritanopia`).

3. **Automatic Game Detection**
   * Detects **Unity**, **Unreal Engine**, **REDengine**, **RE Engine**, **CryEngine**, **Source**, **SDL/GLFW**, Steam, Epic, GOG, Xbox, Riot, and HoYoPlay game libraries as well as borderless and fullscreen games, while pausing cleanly on Desktop, File Explorer, browsers, chat or media apps, code editors, and launchers.

4. **Windows Setup Installer (`AIDynamicVibranceEnhance_Setup.exe`)**
   * Native Direct2D installer wizard with **Standard** or **Full (with Legacy Fullscreen Shader Support)** installation options, custom install directory, Desktop and Start Menu shortcuts, optional Windows startup, and clean uninstall support (`--uninstall`) registered in Windows Apps & Features.

## Build from Source

Requires Windows 10/11 SDK, CMake 3.20+, and Visual Studio 2022/2026 C++ Desktop toolchain (`x64`).

```bat
cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

This builds both binaries:
* `build/AIDynamicVibranceEnhance.exe` (main application binary; also exports synced `shaders/*.hlsl` and `shaders/*.fx`)
* `build/AIDynamicVibranceEnhance_Setup.exe` (Windows Setup Installer)

## Verification Tests

```bat
AIDynamicVibranceEnhance.exe --self-test
AIDynamicVibranceEnhance_Setup.exe --self-test
```
