# Changelog

All notable changes to **AI Dynamic Vibrance Enhance** are documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), preserving the original `v1.0.0` release history alongside the `v2.0.0` milestone.

---

## [2.0.0] - 2026-11-23

A major platform, rendering pipeline, color calibration, smoothness, and cross-platform release expanding **AI Dynamic Vibrance Enhance** from a 12-filter Windows SDR enhancer (`v1.0.0`) into a 16-filter Visual Suite with Universal Optical Flow Frame Generation, Spatial Upscaling, True HDR10 / scRGB Output, Built-in GPU Screenshots, and Linux Gaming support via `vkBasalt`.

### Major New Features

- **Universal Optical Flow Frame Generation (`Windows DX11`):**
  - Cross-GPU 3-level multi-scale compute shader optical flow pipeline working natively across **AMD Radeon**, **NVIDIA GeForce**, and **Intel Arc / UHD Graphics** without modifying game files.
  - Supports fixed **`2x`**, **`3x`**, **`4x`** frame multipliers plus **`Adaptive`** mode dynamically matching target FPS (`60 to 240 FPS`) and monitor refresh rate.
  - Includes configurable **Optical Flow Scale** (`25% to 100%`), bidirectional forward/backward consistency occlusion blending, AFMF-style **Fast Motion Fallback** on rapid camera pans or scene cuts, and **Static HUD Protection** so crosshairs and UI text remain crisp.
- **Spatial Upscaling Suite (`Windows DX11`):**
  - Added **AMD FSR 1.0** (12-tap EASU + RCAS sharpening), **NVIDIA NIS Style** directional Catmull-Rom scaling, **Integer Scaling** (centered pixel-perfect scaling for retro/2D/emulated games), and **Anime4K** (cel-shaded/animated line thinning and detail restoration), plus **Fullscreen Stretch** for lower-resolution windowed titles.
- **True HDR10 / scRGB 16-Bit Output & Live HDR Switching (`Windows 10/11`):**
  - Automatically detects active Windows HDR (`IDXGIOutput6`) and SDR Content Brightness, seamlessly handling live **`Win + Alt + B`** toggling.
  - Upgrades swapchain & render targets to 16-bit float **scRGB (`R16G16B16A16_FLOAT`)** wide-gamut DCI-P3 / BT.2020 with highlight expansion (**300 to 1500 nits Peak Brightness**) and **HDR Mid Gray / Contrast** slider, while preserving local **Dynamic HDR** tone mapping on SDR displays.
- **Built-in GPU Filtered Lossless PNG Screenshots:**
  - Captures final enhanced frames directly from the GPU swapchain (supporting SDR `BGRA8` and HDR10 scRGB `RGBA16F`) into `Pictures\AI Dynamic Vibrance Enhance\ADV_YYYYMMDD_HHMMSS_mmm.png`, working reliably in Desktop, `DXGI Smooth`, and `WGC` capture paths.
  - Added clickable **Screenshot Hotkey** recorder (default **`Alt + F12`**, persisted in `profiles.json`) and an **Open Screenshots** quick button.
- **5 Curated 1-Click Starter Presets:**
  - Added instant preset cards in the Control Hub (`Presets` tab): **`Balanced RTX`**, **`Cinematic Film`** (`AgX` 30% + Bloom 20% + AO 30% + Deband 35%), **`Competitive`** (Vibrance 60/60 + Dark/Shadow Lift 35% + Sharpen+ 45%), **`Vibrant RPG`** (Vibrance 65/70 + Clarity 25% + Deband 45%), and **`Night Comfort`** (Vibrance 40/35 + Blue Light Warmth 35%).
- **Linux Gaming Support (`vkBasalt` Edition):**
  - Added native **Linux Control Hub (GUI & CLI)** ([`linux/adv_control_hub.py`](linux/adv_control_hub.py)) with dark AMD-Crimson interface, 16-filter tuning, 5 starter presets, Split Preview, per-game config generator (`VKBASALT_CONFIG_FILE`), Flatpak permission fixer (`org.freedesktop.Platform.VulkanLayer.vkBasalt`), automatic legacy config backup (`vkBasalt.conf.adv-backup`), and Windows-compatible `.json` preset import/export.
  - Shipped synced ReShade `.fx` shader + bundled MIT-licensed [`ReShade.fxh`](linux/shaders/ReShade.fxh) header targeting major Linux gaming distributions:
    - **Arch Linux / CachyOS / EndeavourOS:** [`PKGBUILD`](linux/packaging/arch/PKGBUILD) + [`install.sh`](linux/install.sh)
    - **Fedora / Nobara / Bazzite:** RPM [`ai-dynamic-vibrance-enhance.spec`](linux/packaging/fedora/ai-dynamic-vibrance-enhance.spec) + immutable user-space install support
    - **Ubuntu / Pop!_OS / Linux Mint:** Prebuilt Debian package [`ai-dynamic-vibrance-enhance_2.0.0_all.deb`](linux/dist/ai-dynamic-vibrance-enhance_2.0.0_all.deb)
    - **SteamOS / Steam Deck & Portable Tarball:** [`AIDynamicVibranceEnhance-Linux-v2.0.0.tar.gz`](linux/dist/AIDynamicVibranceEnhance-Linux-v2.0.0.tar.gz)
  - Added automated multi-distro package build workflow [`.github/workflows/linux-packages.yml`](.github/workflows/linux-packages.yml).

### New Visual Filters (Expanded from 12 to 16 Filters)

Every new filter runs identically across both the live DX11 overlay HLSL pipeline and the exported ReShade / `vkBasalt` `.fx` shader:
- **Ambient Occlusion (`AO Intensity %`, `AO Radius %`):** Screen-space crevice and contact-shadow depth darkening with skin-tone & highlight protection in live overlay, plus automatic 3D depth buffer SSAO fallback in ReShade `.fx`.
- **Cinematic Bloom (`Bloom Intensity %`, `Bloom Threshold %`):** Soft bright-pass optical highlight halo extraction with anti-blowout attenuation.
- **Filmic Color Grading (`ACES`, `AgX`, `Technicolor`, `Strength %`):** Selectable cinema tone curves preserving saturated highlights and foliage realism.
- **Deband (`Deband Strength %`):** Edge-preserving multi-tap dithering smoothing 8-bit sky and dark-cave color banding without blurring crisp geometry edges.

### Performance, Smoothness & VRR Frame Pacing Fixes

- **Fixed Stutter / Frame-Pacing Lag in `DXGI Smooth` Mode:**
  - Switched default capture back to zero-copy **DXGI Desktop Duplication** paired with a **`VRR / Allow Tearing`** flip-model waitable swapchain (`DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING`, `MaxFrameLatency = 1`) so FreeSync and G-Sync monitors maintain smooth variable refresh pacing without VSync stall.
- **Unthrottled Windows 11 Graphics Capture (`WGC`) for OBS & ReLive:**
  - Added selectable **`Capture API` (`DXGI Smooth` vs `WGC`)** so creators recording with OBS Display Capture or Radeon ReLive can record clean enhanced footage without feedback loops, utilizing Windows 11 `IsCursorCaptureEnabled` / unthrottled capture intervals.
- **Smooth Temporal Adaptation:**
  - Frame-to-frame scene statistics (`avgLuma`, `darkRatio`, `avgChroma`, `highLuma`) adapt smoothly without sudden brightness pumping on fast camera turns or HUD popups.

### Color Science & Visual Calibration Improvements

- **Haze-Free Dark Cave & Shadow Recovery:**
  - Replaced aggressive global shadow lifting with toe-anchored dark shadow recovery that keeps true voids black (`RGB(4,4,4)` untouched) and adds **Crevice Preservation** in `Dark / Shadow Lighting` so dark cave depth stays contrasty rather than washed out.
- **Cool Cyan-Blue Sky & Bright Daylight Calibration:**
  - Added `coolCyanBlueGuard` and bell-shaped SDR cyan headroom capping alongside `warmGoldGuard` and bright-scene chroma backoff so bright sunlit scenes and azure skies remain crisp and deep without clipping to neon cyan or blown-out yellows.
- **Warm Candlelit & Vivid Red UI Protection:**
  - Added `warmRedGuard` and dominance-headroom limiting so bright candlelit reds/oranges stay natural and vivid red HUD elements (health bars, quest text, icons) never clip harshly.

### UI, Sidebar & Workflow Upgrades

- **Control Hub Expanded to 6 Tabs:**
  - Added dedicated **`Scaling`** tab unifying Spatial Upscaling, sharpness, Fullscreen Stretch, Capture API (`DXGI Smooth` / `WGC`), `VRR / Allow Tearing`, Optical Flow Frame Gen controls, live source/output FPS telemetry, and honest live overlay status (`Active: Desktop (DXGI)`, `Active: ... (WGC OBS)`, `Live: DXGI Fallback`, `Standby`).
- **In-Sidebar `Split Preview` & Live Shortcuts:**
  - Added a **`Split Preview`** card (`Toggle`, `-`, `+`, live slider `5% to 95%`) directly inside the **`Alt + X`** Overlay Sidebar alongside global in-game shortcuts: **`Alt + \`** (toggle Split Preview), **`Alt + [`** (`-5%`), and **`Alt + ]`** (`+5%`).
- **Legacy Shader & Preset Workflow Polish:**
  - Added automatic check warning users when deploying `AIDynamicVibranceEnhance.fx` into a folder without an existing ReShade installation, plus ReShade / `vkBasalt` Split Preview uniform support (`UI_SplitPreview`, `UI_SplitPosition`).

### Bug Fixes & Stability

- **Fixed Screenshot Tool Focus Hijacking:** Filtered transient capture overlays (`screenclippinghost.exe`, `snippingtool.exe`, `sharex.exe`, `lightshot.exe`, `greenshot.exe`, `gamebar.exe`, `amddvr.exe`, `nvidia share.exe`) so pressing `PrintScreen` or `Win + Shift + S` no longer steals focus or tears down WGC/overlay state.
- **Fixed `SetWindowDisplayAffinity` / OBS Visibility Desync:** `SetOverlayCaptureVisibility()` now flushes DWM with `SetWindowPos(..., SWP_FRAMECHANGED)` and `dcompDevice_->Commit()`, and re-syncs affinity after `SWP_SHOWWINDOW` transitions.
- **Fixed Multi-Monitor Viewport Coordinates:** Changed overlay `vpX` / `vpY` types in render paths from unsigned to signed `int` so negative virtual-desktop monitor coordinates work accurately.
- **Expanded Automated Self-Tests:**
  - Windows app `--self-test` expanded to **28 GPU/shader/pipeline checks** covering SDR/HDR10, spatial upscalers, optical flow, AO, cave/sky/skin/red guards, deband, bloom, filmic curves, WIC PNG encoding, WGC/DXGI/VRR, capture affinity transitions, hotkeys, presets, game detection, and `.fx` install/uninstall.
  - Setup installer `--self-test` verifies all **4 RCDATA payloads**; Linux Control Hub `--self-test` verifies **8 offline checks**; package builder verifies `.deb` (`ar`/`md5sums`/permissions) and `.tar.gz` integrity.

---

## [1.0.0] - 2026-11-22

Initial public release of **AI Dynamic Vibrance Enhance** for Windows 10 and Windows 11.

### Highlights
- **12 Unified Visual Filters:** `AI Dynamic Vibrance` (`Intensity %`, `Saturation boost %` in Oklab space with skin protection, adaptive dark visibility, bilateral clarity, black-floor lock, and anti-banding dither), `Sharpen+ (AMD CAS)`, `Dynamic HDR`, `Dark / Shadow Lighting`, `Clarity / Details`, `Brightness / Contrast`, `Color Temperature & Tint`, `Gamma / Exposure`, `Night Mode / Blue Light`, `Black & White`, `Cinematic Vignette`, and `Colorblind Assist` (`Protanopia`, `Deuteranopia`, `Tritanopia`).
- **Control Hub & `Alt + X` Overlay Sidebar:** Frameless Windows 11 dark flat UI with 5 tabs (`Overview`, `Games`, `Presets`, `Legacy`, `About`), Grid/List switcher, custom hotkeys (`Alt + X` sidebar, `Alt + V` quick toggle), `Split Preview`, per-app profile switching, community `.json` preset Export/Import, and click-through `WS_EX_NOACTIVATE` overlay with `WDA_EXCLUDEFROMCAPTURE` & Exclusive Fullscreen Guard (`QUNS_RUNNING_D3D_FULL_SCREEN`).
- **Game-Only Auto-Detection & Installer:** Automatic engine detection (`Unity`, `Unreal Engine`, `REDengine`, `RE Engine`, `CryEngine`, `Source`, `SDL/GLFW`) with custom Whitelist/Blacklist `.exe` overrides, Legacy ReShade `.fx` export, and Direct2D Windows Setup Installer (`AIDynamicVibranceEnhance_Setup.exe`).
