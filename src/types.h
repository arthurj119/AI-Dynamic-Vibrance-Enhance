#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct FilterParams {
    // Primary sliders on a 0..100 scale (50 = calibrated balanced/neutral default unless noted)
    int intensity = 50;
    int saturationBoost = 50;
    int darkLighting = 50;
    int clarity = 50;
    int dynamicHdr = 50;
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

    bool skinProtection = true;  // Always enabled under the hood
    bool adaptiveScene = true;   // Always enabled under the hood
    bool enableDynamicVibrance = true;
    bool enableDarkLighting = false;
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
};

struct AppConfig {
    // Schema v3: Control Hub + 12-filter Alt+X Sidebar + Windows Setup Installer
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
    int mode2ExportProfile = 1;
    std::wstring reshadeFolder;

    FilterParams profiles[3];
    std::map<std::wstring, int> perAppProfile;
    std::vector<std::wstring> customGameWhitelist;
    std::vector<std::wstring> customGameBlacklist;

    static AppConfig CreateDefault() {
        AppConfig cfg;
        cfg.schemaVersion = 3;
        cfg.activeProfile = 1;
        cfg.overlayEnabled = true;
        cfg.autoSwitchPerApp = true;
        cfg.onlyInGames = true;
        cfg.launchAtStartup = false;
        cfg.mode2Enabled = false;
        cfg.splitPreview = false;
        cfg.splitPosition = 0.5f;
        cfg.hubListView = false;
        cfg.hideSidebarFromCapture = true;
        cfg.sidebarHotkeyMods = 0x0001u;
        cfg.sidebarHotkeyVk = 0x58u;
        cfg.quickToggleEnabled = true;
        cfg.quickToggleMods = 0x0001u;
        cfg.quickToggleVk = 0x56u;
        cfg.mode2ExportProfile = 1;

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

    // Extended v3 parameters (96 bytes total)
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
    float pad0;
};
static_assert(sizeof(EnhanceCBData) == 96, "EnhanceCBData must be 96 bytes");

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
};

struct SceneStats {
    float avgLuma;
    float darkRatio;
    float avgChroma;
    float highLuma;
};
static_assert(sizeof(SceneStats) == 16, "SceneStats must be 16 bytes");
