# AI Dynamic Vibrance Enhance (`v2.0.0`)

**Developer:** `arthurj119` | **License:** [MIT](LICENSE) | **Repository:** https://github.com/arthurj119/AI-Dynamic-Vibrance-Enhance

An open source, cross GPU real time game visual enhancer, spatial upscaler, optical flow frame generator, and True HDR10 output suite for **AMD Radeon**, **NVIDIA GeForce**, and **Intel Arc / UHD Graphics** on Windows 10 and 11. Built around a modular three stage DirectX 11 compute and pixel shader pipeline with DirectComposition, Windows Graphics Capture for clean OBS Display Capture recording, an **`Alt + X`** slide out overlay sidebar, and a flat dark desktop **Control Hub**.

## Features & Architecture

1. **Universal Optical Flow Frame Generation (`v2.0.0`)**
   * External multi scale three level compute shader optical flow pipeline running natively across AMD, NVIDIA, and Intel GPUs on windowed and borderless games without modifying game files.
   * Supports fixed **`2x`**, **`3x`**, **`4x`** multipliers and **`Adaptive`** mode matching a target FPS (`60 to 240 FPS`) and monitor refresh rate.
   * Includes adjustable **Optical Flow Scale** (`25% to 100%`), bidirectional forward and backward consistency occlusion blending, AFMF style **Fast Motion Fallback** on rapid camera pans and scene cuts, and **Static HUD Protection** to keep crosshairs and UI elements crisp.

2. **Spatial Upscaling Suite (`v2.0.0`)**
   * **AMD FSR 1.0**: Two pass 12 tap Edge Adaptive Spatial Upsampling (`EASU`) and Robust Contrast Adaptive Sharpening (`RCAS`).
   * **NVIDIA NIS Style**: Directional sharpness adaptive Catmull Rom spatial scaling for every GPU vendor.
   * **Integer Scaling**: Sharp pixel perfect integer multiplication centered on screen for retro, emulation, and 2D games.
   * **Anime4K**: Gradient based dark line thinning and detail restoration tuned for cel shaded and animated visuals.
   * Supports **Fullscreen Stretch** for lower resolution windowed games and composes directly ahead of visual filters and True HDR output.

3. **True HDR10 / scRGB Output & Dynamic HDR**
   * Automatically detects Windows HDR state (`IDXGIOutput6`) and Windows SDR Content Brightness (`Win + Alt + B` live switching supported).
   * When Windows HDR is active, the overlay upgrades automatically to a 16 bit float **scRGB** (`DXGI_FORMAT_R16G16B16A16_FLOAT`) wide gamut DCI P3 / BT.2020 pipeline with highlight expansion (**300 to 1500 nits** Peak Brightness) and **Mid Gray / Contrast** control.
   * When Windows HDR is off, **Dynamic HDR** switches seamlessly to a local HDR tone mapping curve for SDR monitors.

4. **Main Control Hub (Desktop Window)**
   * Frameless Windows 11 dark flat UI with six centered navigation tabs (`Overview`, `Scaling`, `Games`, `Presets`, `Legacy`, `About`), live **`Grid | List`** layout switcher persisted in `profiles.json`, full screenshot and OBS capturability, and clean center screen startup (or `--minimized` to system tray).
   * **Overview Tab:** Master toggle for the overlay engine, clickable **Sidebar Hotkey** (`Alt + X` default) and **Quick Filter Toggle** (`Alt + V` default) recorder, plus six compact tiles or list rows (`Game Only Mode`, `Per Game Profiles`, `Capture Privacy`, `Split Preview`, `Start with Windows`, and `Quick Filter Toggle`).
   * **Scaling Tab:** Unified controls for Spatial Upscaling, sharpness, fullscreen stretch, Capture API (`DXGI Smooth` default for FreeSync and G Sync variable refresh rate pacing, `WGC` for OBS Display Capture), `VRR / Allow Tearing`, Optical Flow Frame Generation, flow scale, adaptive target FPS, fallback protection, and live FPS telemetry.
   * **Games Tab:** Custom **Force Enable (Whitelist)** and **Always Ignore (Blacklist)** `.exe` overrides that apply immediately alongside the built in game engine detector.
   * **Presets Tab:** Portable community `.json` **Preset Export / Import** per profile slot, quick **Data Folder** shortcut, and single or full profile factory resets.
   * **Legacy Tab:** Optional shader export to deploy or remove [AIDynamicVibranceEnhance.fx](shaders/AIDynamicVibranceEnhance.fx) for classic DX9, DX10, and OpenGL Exclusive Fullscreen games via ReShade, including automatic warning if ReShade is not yet installed in the chosen folder.
   * **About Tab:** Application details (`v2.0.0`, `arthurj119`, MIT license), GitHub repository link, and community note.

5. **Overlay Filter Sidebar (`Alt + X` / Custom Hotkey)**
   * Focus safe (`WS_EX_NOACTIVATE`), capture optional (`WDA_EXCLUDEFROMCAPTURE` toggleable from Control Hub), and click through DX11 overlay with an automatic **Exclusive Fullscreen Guard** (`QUNS_RUNNING_D3D_FULL_SCREEN`) so fullscreen games never get Alt Tabbed back to the desktop.
   * Compact **Scaling & Frame Gen** quick switch card plus 4 profile slots (`None`, `1`, `2`, `3`) and 16 calibrated visual filters synced identically between live overlay HLSL and ReShade `.fx`:
     * **`AI Dynamic Vibrance`**, **`Sharpen+ (AMD CAS)`**, **`Dynamic HDR`**, **`Dark / Shadow Lighting`** (with Crevice Preservation), **`Ambient Occlusion`** (Screen Space Crevice & Contact Depth in live overlay, plus automatic 3D Depth Buffer SSAO in ReShade `.fx`), **`Clarity / Details`**, **`Cinematic Bloom`**, **`Filmic Grading`** (`ACES`, `AgX`, `Technicolor`), **`Deband`**, **`Brightness / Contrast`**, **`Color Temperature & Tint`**, **`Gamma / Exposure`**, **`Night Mode / Blue Light`**, **`Black & White`**, **`Cinematic Vignette`**, and **`Colorblind Assist`** (`Protanopia`, `Deuteranopia`, `Tritanopia`).

6. **Automatic Game Detection, Low Latency VRR Sync & OBS Capture**
   * Detects **Unity**, **Unreal Engine**, **REDengine**, **RE Engine**, **CryEngine**, **Source**, **SDL / GLFW**, Steam, Epic, GOG, Xbox, Riot, and HoYoPlay game libraries as well as borderless and fullscreen games, while pausing cleanly on Desktop, File Explorer, browsers, chat or media apps, code editors, and launchers.
   * Defaults to zero copy **DXGI Desktop Duplication** paired with a **VRR / Allow Tearing** flip model swapchain (`DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING`) to preserve FreeSync and G Sync frame pacing, with one click switching to **Windows Graphics Capture (`WGC`)** when recording through OBS Display Capture or Radeon ReLive without feedback loops.

7. **Windows Setup Installer (`AIDynamicVibranceEnhance_Setup.exe`)**
   * Native Direct2D installer wizard with **Standard** or **Full (with Legacy Fullscreen Shader Support)** installation options, custom install directory, Desktop and Start Menu shortcuts, optional Windows startup, and clean uninstall support (`--uninstall`) registered in Windows Apps & Features.

## Linux Gaming Support (`vkBasalt` Edition)

AI Dynamic Vibrance Enhance supports major Linux gaming distributions through **vkBasalt** (Vulkan post processing layer across Wayland, X11, Gamescope, Proton DXVK, VKD3D Proton, and native Vulkan games) together with a native **Linux Control Hub (GUI & CLI)** in [`linux/adv_control_hub.py`](linux/adv_control_hub.py) and a self contained [`AIDynamicVibranceEnhance.fx`](shaders/AIDynamicVibranceEnhance.fx) + [`ReShade.fxh`](linux/shaders/ReShade.fxh) shader bundle.

### Supported Linux Gaming Distributions
* **Arch Linux / CachyOS / EndeavourOS:** Native [`PKGBUILD`](linux/packaging/arch/PKGBUILD) or `./linux/install.sh`
* **Fedora / Nobara / Bazzite:** Native RPM [`ai-dynamic-vibrance-enhance.spec`](linux/packaging/fedora/ai-dynamic-vibrance-enhance.spec) or user space `./linux/install.sh` (automatic Flatpak VulkanLayer permission setup for Bazzite)
* **Ubuntu / Pop!_OS / Linux Mint:** Prebuilt [`linux/dist/ai-dynamic-vibrance-enhance_2.0.0_all.deb`](linux/dist/ai-dynamic-vibrance-enhance_2.0.0_all.deb) (`sudo apt install ./linux/dist/ai-dynamic-vibrance-enhance_2.0.0_all.deb`) or `./linux/install.sh`
* **SteamOS (Steam Deck):** Immutable user space installation via `./linux/install.sh --user` with Flatpak `org.freedesktop.Platform.VulkanLayer.vkBasalt` support

### Linux Quick Start & Features
* Includes all **16 calibrated filters**, all **5 curated starter presets** (`Balanced RTX`, `Cinematic Film`, `Competitive`, `Vibrant RPG`, `Night Comfort`), **Split Preview**, custom toggle keys (`Home` default), **per game config generation**, and **Windows compatible `.json` presets**.
* Quick install from source or portable bundle:

```bash
chmod +x linux/install.sh
./linux/install.sh
```

* Set your Steam **Launch Options** (also works in Lutris, Heroic, and Bottles):

```text
ENABLE_VKBASALT=1 %command%
```

*Note: DirectX 11 Optical Flow Frame Generation, Windows Spatial Upscaling, and True HDR10 scRGB output apply to Windows 10 and 11 only. On SteamOS, Bazzite, or Gamescope sessions, pair vkBasalt directly with Gamescope FSR (`gamescope -U -- %command%`).*

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
python3 linux/adv_control_hub.py --self-test
python3 linux/packaging/build-deb.py --verify
```
