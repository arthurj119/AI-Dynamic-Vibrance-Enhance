#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct DisplayHdrInfo {
    bool hdrActive = false;
    float maxLuminanceNits = 800.0f;
    float minLuminanceNits = 0.005f;
    float sdrWhiteNits = 200.0f;
};

struct FilterParams {
    // Primary sliders on a 0..100 scale (50 = calibrated balanced/neutral default unless noted)
    int intensity = 50;
    int saturationBoost = 50;
    int darkLighting = 50;
    int clarity = 50;
    int dynamicHdr = 50;
    int hdrPeakNits = 800;       // 300..1500 peak brightness for True HDR10 / scRGB output
    int hdrMidGray = 50;         // 0..100 (50 = neutral Windows SDR white midtone)
    int brightness = 50;
    int contrast = 50;

    // Extended modular filters (v3)
    int sharpen = 50;
    int temperature = 50;        // 50 = neutral (0 = cool blue, 100 = warm amber)
    int tint = 50;               // 50 = neutral (0 = green, 100 = magenta)
    int exposure = 50;           // 50 = neutral
    int gamma = 50;              // 50 = neutral
    int nightMode = 50;          // 0..100 blue-light warmth
    int blackAndWhite = 100;     // 0..100 desaturation mix
    int vignette = 50;           // 0..100 edge darkening
    int colorblindStrength = 50; // 0..100 daltonization mix
    int colorblindType = 0;      // 0 = Protanopia, 1 = Deuteranopia, 2 = Tritanopia
    int ambientOcclusion = 50;   // 0..100 (Crevice & contact shadow intensity)
    int aoRadius = 50;           // 0..100 (Tight fine crevices -> wide corners)
    int bloomIntensity = 50;     // 0..100 optical highlight bloom strength
    int bloomThreshold = 60;     // 0..100 bright-pass extraction threshold
    int filmicStrength = 50;     // 0..100 filmic grading blend strength
    int filmicMode = 0;          // 0 = ACES, 1 = AgX, 2 = Technicolor
    int debandStrength = 50;     // 0..100 edge-preserving gradient deband strength

    bool skinProtection = true;  // Always enabled under the hood
    bool adaptiveScene = true;   // Always enabled under the hood
    bool enableDynamicVibrance = true;
    bool enableDarkLighting = false;
    bool enableAmbientOcclusion = false;
    bool enableClarity = false;
    bool enableDynamicHdr = false;
    bool enableBrightnessContrast = false;
    bool enableSharpen = false;
    bool enableColorTemp = false;
    bool enableGammaExposure = false;
    bool enableNightMode = false;
    bool enableBlackAndWhite = false;
    bool enableVignette = false;
    bool enableColorblind = false;
    bool enableBloom = false;
    bool enableFilmic = false;
    bool enableDeband = false;
};

enum ScaleMode : int {
    SCALE_OFF = 0,
    SCALE_FSR1 = 1,
    SCALE_NIS = 2,
    SCALE_INTEGER = 3,
    SCALE_ANIME4K = 4,
};

enum FrameGenMode : int {
    FG_OFF = 0,
    FG_2X = 1,
    FG_3X = 2,
    FG_4X = 3,
    FG_ADAPTIVE = 4,
};

enum CaptureApi : int {
    CAPTURE_DXGI = 0,
    CAPTURE_WGC = 1,
};

struct FrameGenTelemetry {
    float sourceFps = 0.0f;
    float outputFps = 0.0f;
    int monitorHz = 60;
    bool fgRunning = false;
    bool fallbackActive = false;
    bool overlayActive = false;
    bool usingWgc = false;
};

struct AppConfig {
    // Schema v3: Control Hub + 13-filter Alt+X Sidebar + Spatial Scaling + Optical Flow Frame Gen
    int schemaVersion = 3;

    // 0 = None (Bypass), 1 = Profile 1, 2 = Profile 2, 3 = Profile 3
    int activeProfile = 1;
    bool overlayEnabled = true;
    bool autoSwitchPerApp = true;
    bool onlyInGames = true;
    bool launchAtStartup = false;
    bool mode2Enabled = false;
    bool splitPreview = false;
    float splitPosition = 0.5f;
    bool hubListView = false;
    bool hideSidebarFromCapture = true;
    uint32_t sidebarHotkeyMods = 0x0001u; // MOD_ALT
    uint32_t sidebarHotkeyVk = 0x58u;   // 'X'
    bool quickToggleEnabled = true;
    uint32_t quickToggleMods = 0x0001u;   // MOD_ALT
    uint32_t quickToggleVk = 0x56u;     // 'V'
    uint32_t screenshotHotkeyMods = 0x0001u; // MOD_ALT
    uint32_t screenshotHotkeyVk = 0x7Bu;     // VK_F12
    int mode2ExportProfile = 1;
    std::wstring reshadeFolder;

    // Spatial Scaling & Optical Flow Frame Generation (v2.0.0)
    int scaleMode = SCALE_OFF;
    int scaleSharpness = 60;
    bool scaleFullscreenStretch = true;
    int fgMode = FG_OFF;
    int fgFlowScale = 50;
    int fgAdaptiveTargetFps = 120;
    bool fgFastMotionFallback = true;
    bool fgStaticHudProtect = true;
    int captureApi = CAPTURE_DXGI; // 0 = DXGI Smooth (VRR / low latency), 1 = WGC (OBS Display Capture)
    bool allowTearing = true;      // DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING for FreeSync / G-Sync VRR

    FilterParams profiles[3];
    std::map<std::wstring, int> perAppProfile;
    std::vector<std::wstring> customGameWhitelist;
    std::vector<std::wstring> customGameBlacklist;

    static AppConfig CreateDefault() {
        AppConfig cfg;
        for (int i = 0; i < 3; ++i) {
            cfg.profiles[i] = FilterParams{};
        }
        return cfg;
    }
};

struct alignas(16) EnhanceCBData {
    float intensity;
    float saturationBoost;
    float darkLighting;
    float clarity;

    float dynamicHdr;
    float brightness;
    float contrast;
    float splitPos;

    uint32_t flags;
    float width;
    float height;
    float smoothAlpha;

    // Extended v3 parameters (128 bytes total)
    float sharpen;
    float temperature;
    float tint;
    float exposure;

    float gamma;
    float nightMode;
    float blackAndWhite;
    float vignette;

    float colorblindStrength;
    uint32_t colorblindType;
    uint32_t flags2;
    float hdrPeakNits;

    float hdrMidGray;
    float sdrWhiteNits;
    uint32_t hdrOutputMode; // 0 = 8-bit SDR sRGB, 1 = 16-bit Linear scRGB True HDR (1.0 = 80 nits)
    float aoIntensity;

    float aoRadius;
    float bloomIntensity;
    float bloomThreshold;
    float filmicStrength;

    uint32_t filmicMode; // 0 = ACES, 1 = AgX, 2 = Technicolor
    float debandStrength;
    float pad1;
    float pad2;
};
static_assert(sizeof(EnhanceCBData) == 144, "EnhanceCBData must be 144 bytes");

enum EnhanceFlags : uint32_t {
    FLAG_SKIN_PROTECTION    = 1u << 0,
    FLAG_ADAPTIVE_SCENE     = 1u << 1,
    FLAG_ENABLE_DARK_LIGHT  = 1u << 2,
    FLAG_ENABLE_CLARITY     = 1u << 3,
    FLAG_ENABLE_DYNAMIC_HDR = 1u << 4,
    FLAG_ENABLE_BC          = 1u << 5,
    FLAG_SPLIT_PREVIEW      = 1u << 6,
    FLAG_ENABLE_VIBRANCE    = 1u << 7,
};

enum EnhanceFlags2 : uint32_t {
    FLAG2_ENABLE_SHARPEN    = 1u << 0,
    FLAG2_ENABLE_COLOR_TEMP = 1u << 1,
    FLAG2_ENABLE_GAMMA_EXP  = 1u << 2,
    FLAG2_ENABLE_NIGHT_MODE = 1u << 3,
    FLAG2_ENABLE_BW         = 1u << 4,
    FLAG2_ENABLE_VIGNETTE   = 1u << 5,
    FLAG2_ENABLE_COLORBLIND = 1u << 6,
    FLAG2_ENABLE_AO         = 1u << 7,
    FLAG2_ENABLE_BLOOM      = 1u << 8,
    FLAG2_ENABLE_FILMIC     = 1u << 9,
    FLAG2_ENABLE_DEBAND     = 1u << 10,
};

struct SceneStats {
    float avgLuma;
    float darkRatio;
    float avgChroma;
    float highLuma;
};
static_assert(sizeof(SceneStats) == 16, "SceneStats must be 16 bytes");

struct alignas(16) FgCBData {
    uint32_t srcW;
    uint32_t srcH;
    uint32_t dstW;
    uint32_t dstH;

    float interpT;
    float fallbackStrength;
    float hudProtect;
    float searchDir;

    uint32_t level;
    uint32_t scaleMode;
    float scaleSharpness;
    float pad0;

    float vpLeft;
    float vpTop;
    float vpW;
    float vpH;
};
static_assert(sizeof(FgCBData) == 64, "FgCBData must be 64 bytes");

struct FgStats {
    float avgWarpErr;
    float avgFlowMag;
    float fallbackFactor;
    float pad0;
};
static_assert(sizeof(FgStats) == 16, "FgStats must be 16 bytes");
