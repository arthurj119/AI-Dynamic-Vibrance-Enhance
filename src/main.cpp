#include "control_hub.h"
#include "dx11_overlay.h"
#include "profiles.h"
#include "shader_source.h"
#include "sidebar_ui.h"
#include "types.h"

#include <windows.h>
#include <roapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr UINT WM_TRAYICON = WM_APP + 1;
constexpr UINT WM_APP_SHOW_HUB = WM_APP + 2;
constexpr int HOTKEY_TOGGLE_ID = 1;
constexpr int HOTKEY_QUICK_TOGGLE_ID = 2;
constexpr int HOTKEY_SCREENSHOT_ID = 3;
constexpr int HOTKEY_SPLIT_TOGGLE_ID = 4;
constexpr int HOTKEY_SPLIT_DEC_ID = 5;
constexpr int HOTKEY_SPLIT_INC_ID = 6;
constexpr UINT TRAY_ICON_ID = 1001;

enum TrayMenuCmd {
    CMD_SHOW_HUB = 2000,
    CMD_TOGGLE_SIDEBAR = 2001,
    CMD_TOGGLE_OVERLAY_MASTER = 2002,
    CMD_SCREENSHOT = 2003,
    CMD_PROFILE_0 = 2010,
    CMD_PROFILE_1 = 2011,
    CMD_PROFILE_2 = 2012,
    CMD_PROFILE_3 = 2013,
    CMD_EXIT = 2099,
};

void WriteStdout(const std::string& msg) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!hOut || hOut == INVALID_HANDLE_VALUE || GetFileType(hOut) == FILE_TYPE_UNKNOWN) {
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        }
    }
    if (hOut && hOut != INVALID_HANDLE_VALUE && GetFileType(hOut) != FILE_TYPE_UNKNOWN) {
        DWORD written = 0;
        WriteFile(hOut, msg.data(), static_cast<DWORD>(msg.size()), &written, nullptr);
        FlushFileBuffers(hOut);
    }
}

HICON CreateAppTrayIcon(HINSTANCE hInst) {
    HICON hEmbedded = static_cast<HICON>(
        LoadImageW(hInst, MAKEINTRESOURCEW(101), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR)
    );
    if (hEmbedded) {
        return hEmbedded;
    }

    // Full AMD Crimson Red fallback badge with pure-white radiant diamond emblem
    constexpr int S = 32;
    std::vector<uint32_t> pixels(S * S, 0x00000000);
    for (int y = 0; y < S; ++y) {
        float ny = ((y + 0.5f) / S) * 2.0f - 1.0f;
        for (int x = 0; x < S; ++x) {
            float nx = ((x + 0.5f) / S) * 2.0f - 1.0f;
            float bx = std::abs(nx) - 0.60f;
            float by = std::abs(ny) - 0.60f;
            float sdBox = std::hypot(std::max(bx, 0.0f), std::max(by, 0.0f)) + std::min(std::max(bx, by), 0.0f) - 0.28f;
            if (sdBox <= 0.0f) {
                float ty = std::clamp((ny + 0.88f) / 1.76f, 0.0f, 1.0f);
                uint32_t r = static_cast<uint32_t>(237.0f * (1.0f - ty) + 181.0f * ty);
                uint32_t g = static_cast<uint32_t>(28.0f * (1.0f - ty) + 18.0f * ty);
                uint32_t b = static_cast<uint32_t>(36.0f * (1.0f - ty) + 27.0f * ty);
                float ax = std::abs(nx);
                float ay = std::abs(ny);
                float starD = std::pow(ax / 0.62f, 0.72f) + std::pow(ay / 0.62f, 0.72f);
                bool inStar = (starD <= 1.0f) && ((ax + ay) >= 0.16f);
                pixels[y * S + x] = inStar ? 0xFFFFFFFFu : (0xFF000000u | (r << 16) | (g << 8) | b);
            }
        }
    }

    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = S;
    bi.bV5Height = -S;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    HDC hdc = GetDC(nullptr);
    void* bits = nullptr;
    HBITMAP hColor = CreateDIBSection(hdc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &bits, nullptr, 0);
    if (hColor && bits) {
        std::memcpy(bits, pixels.data(), pixels.size() * sizeof(uint32_t));
    }
    HBITMAP hMask = CreateBitmap(S, S, 1, 1, nullptr);
    ReleaseDC(nullptr, hdc);

    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmColor = hColor;
    ii.hbmMask = hMask;
    HICON hIcon = CreateIconIndirect(&ii);

    if (hColor) DeleteObject(hColor);
    if (hMask) DeleteObject(hMask);
    return hIcon ? hIcon : LoadIconW(nullptr, IDI_APPLICATION);
}

int RunFullSelfTest() {
    namespace fs = std::filesystem;
    std::ostringstream out;
    out << "=== AI Dynamic Vibrance Enhance Self-Test ===\n";

    // 1. GPU / HLSL / DirectComposition / WDA_EXCLUDEFROMCAPTURE
    std::string gpuReport;
    if (!Dx11Overlay::RunSelfTest(gpuReport)) {
        out << "[FAIL] GPU/Shader test: " << gpuReport << "\n";
        WriteStdout(out.str());
        return 1;
    }
    out << gpuReport;

    // 2. profiles.json round-trip
    fs::path tempBase = fs::temp_directory_path() / L"adv_selftest_tmp";
    std::error_code ec;
    fs::remove_all(tempBase, ec);
    fs::create_directories(tempBase, ec);

    fs::path cfgPath = tempBase / L"profiles.json";
    AppConfig c1 = AppConfig::CreateDefault();
    c1.activeProfile = 2;
    c1.overlayEnabled = false;
    c1.autoSwitchPerApp = false;
    c1.onlyInGames = false;
    c1.mode2Enabled = true;
    c1.splitPreview = true;
    c1.splitPosition = 0.65f;
    c1.hubListView = true;
    c1.hideSidebarFromCapture = false;
    c1.sidebarHotkeyMods = MOD_CONTROL | MOD_SHIFT;
    c1.sidebarHotkeyVk = VK_F9;
    c1.quickToggleEnabled = true;
    c1.quickToggleMods = MOD_ALT;
    c1.quickToggleVk = 'Q';
    c1.screenshotHotkeyMods = MOD_CONTROL | MOD_ALT;
    c1.screenshotHotkeyVk = VK_F11;
    c1.mode2ExportProfile = 3;
    c1.reshadeFolder = L"F:\\Games\\OldTitle\\reshade-shaders\\Shaders";
    c1.scaleMode = SCALE_FSR1;
    c1.scaleSharpness = 72;
    c1.scaleFullscreenStretch = false;
    c1.fgMode = FG_ADAPTIVE;
    c1.fgFlowScale = 75;
    c1.fgAdaptiveTargetFps = 165;
    c1.fgFastMotionFallback = false;
    c1.fgStaticHudProtect = true;
    c1.captureApi = CAPTURE_WGC;
    c1.allowTearing = false;
    c1.customGameWhitelist = { L"pcsx2-qt.exe", L"myindie.exe" };
    c1.customGameBlacklist = { L"blender.exe" };
    c1.profiles[1].darkLighting = 82;
    c1.profiles[1].ambientOcclusion = 68;
    c1.profiles[1].aoRadius = 64;
    c1.profiles[1].enableAmbientOcclusion = true;
    c1.profiles[1].bloomIntensity = 65;
    c1.profiles[1].bloomThreshold = 70;
    c1.profiles[1].enableBloom = true;
    c1.profiles[1].filmicMode = 1;
    c1.profiles[1].filmicStrength = 60;
    c1.profiles[1].enableFilmic = true;
    c1.profiles[1].debandStrength = 55;
    c1.profiles[1].enableDeband = true;
    c1.profiles[1].dynamicHdr = 75;
    c1.profiles[1].hdrPeakNits = 1100;
    c1.profiles[1].hdrMidGray = 62;
    c1.profiles[1].brightness = 47;
    c1.profiles[1].sharpen = 74;
    c1.profiles[1].enableSharpen = true;
    c1.profiles[1].temperature = 63;
    c1.profiles[1].colorblindType = 2;
    c1.profiles[1].enableColorblind = true;
    c1.perAppProfile[L"monsterhunterwilds.exe"] = 1;

    if (!SaveConfig(c1, cfgPath)) {
        out << "[FAIL] SaveConfig failed.\n";
        WriteStdout(out.str());
        fs::remove_all(tempBase, ec);
        return 1;
    }

    AppConfig c2 = AppConfig::CreateDefault();
    if (!LoadConfig(c2, cfgPath) ||
        c2.activeProfile != 2 ||
        c2.overlayEnabled != false ||
        c2.autoSwitchPerApp != false ||
        c2.onlyInGames != false ||
        c2.mode2Enabled != true ||
        c2.splitPreview != true ||
        std::abs(c2.splitPosition - 0.65f) > 0.01f ||
        c2.hubListView != true ||
        c2.hideSidebarFromCapture != false ||
        c2.sidebarHotkeyMods != (MOD_CONTROL | MOD_SHIFT) ||
        c2.sidebarHotkeyVk != VK_F9 ||
        c2.quickToggleEnabled != true ||
        c2.quickToggleMods != MOD_ALT ||
        c2.quickToggleVk != 'Q' ||
        c2.screenshotHotkeyMods != (MOD_CONTROL | MOD_ALT) ||
        c2.screenshotHotkeyVk != VK_F11 ||
        c2.mode2ExportProfile != 3 ||
        c2.reshadeFolder != c1.reshadeFolder ||
        c2.scaleMode != SCALE_FSR1 ||
        c2.scaleSharpness != 72 ||
        c2.scaleFullscreenStretch != false ||
        c2.fgMode != FG_ADAPTIVE ||
        c2.fgFlowScale != 75 ||
        c2.fgAdaptiveTargetFps != 165 ||
        c2.fgFastMotionFallback != false ||
        c2.fgStaticHudProtect != true ||
        c2.captureApi != CAPTURE_WGC ||
        c2.allowTearing != false ||
        c2.customGameWhitelist.size() != 2 ||
        c2.customGameWhitelist[0] != L"pcsx2-qt.exe" ||
        c2.customGameBlacklist.size() != 1 ||
        c2.customGameBlacklist[0] != L"blender.exe" ||
        c2.profiles[1].darkLighting != 82 ||
        c2.profiles[1].ambientOcclusion != 68 ||
        c2.profiles[1].aoRadius != 64 ||
        c2.profiles[1].enableAmbientOcclusion != true ||
        c2.profiles[1].bloomIntensity != 65 ||
        c2.profiles[1].bloomThreshold != 70 ||
        c2.profiles[1].enableBloom != true ||
        c2.profiles[1].filmicMode != 1 ||
        c2.profiles[1].filmicStrength != 60 ||
        c2.profiles[1].enableFilmic != true ||
        c2.profiles[1].debandStrength != 55 ||
        c2.profiles[1].enableDeband != true ||
        c2.profiles[1].dynamicHdr != 75 ||
        c2.profiles[1].hdrPeakNits != 1100 ||
        c2.profiles[1].hdrMidGray != 62 ||
        c2.profiles[1].brightness != 47 ||
        c2.profiles[1].sharpen != 74 ||
        c2.profiles[1].enableSharpen != true ||
        c2.profiles[1].temperature != 63 ||
        c2.profiles[1].colorblindType != 2 ||
        c2.profiles[1].enableColorblind != true ||
        c2.perAppProfile[L"monsterhunterwilds.exe"] != 1) {
        out << "[FAIL] LoadConfig round-trip mismatch.\n";
        WriteStdout(out.str());
        fs::remove_all(tempBase, ec);
        return 1;
    }
    out << "[PASS] profiles.json v3 round-trip verified\n";

    // 2B. Hotkey formatter + Community preset JSON export/import round-trip
    if (FormatHotkeyString(MOD_ALT, 'X') != L"Alt + X" ||
        FormatHotkeyString(MOD_CONTROL | MOD_SHIFT, VK_F9) != L"Ctrl + Shift + F9") {
        out << "[FAIL] FormatHotkeyString assertion failed.\n";
        WriteStdout(out.str());
        fs::remove_all(tempBase, ec);
        return 1;
    }
    fs::path presetPath = tempBase / L"community_preset.json";
    FilterParams impFp{};
    std::wstring impName;
    if (!ExportPresetFile(c1.profiles[1], L"Cinematic Test", presetPath) ||
        !ImportPresetFile(impFp, impName, presetPath) ||
        impName != L"Cinematic Test" ||
        impFp.sharpen != 74 || !impFp.enableSharpen || impFp.colorblindType != 2 ||
        impFp.hdrPeakNits != 1100 || impFp.hdrMidGray != 62) {
        out << "[FAIL] Community preset export/import failed.\n";
        WriteStdout(out.str());
        fs::remove_all(tempBase, ec);
        return 1;
    }
    {
        FilterParams p0 = BuildCuratedPreset(0);
        FilterParams p1 = BuildCuratedPreset(1);
        FilterParams p2 = BuildCuratedPreset(2);
        FilterParams p3 = BuildCuratedPreset(3);
        FilterParams p4 = BuildCuratedPreset(4);
        if (std::wstring(GetCuratedPresetName(1)) != L"Cinematic Film" ||
            p0.intensity != 50 || p0.enableFilmic ||
            !p1.enableFilmic || p1.filmicMode != 1 || !p1.enableBloom || !p1.enableAmbientOcclusion || !p1.enableDeband ||
            !p2.enableDarkLighting || !p2.enableSharpen ||
            !p3.enableClarity || !p3.enableDeband ||
            !p4.enableNightMode) {
            out << "[FAIL] Built-in curated starter presets assertion failed.\n";
            WriteStdout(out.str());
            fs::remove_all(tempBase, ec);
            return 1;
        }
    }
    out << "[PASS] Custom hotkey formatter, 5 starter presets & .json round-trip verified\n";

    // 2C. Game-Only auto-detector + custom whitelist/blacklist override check
    std::wstring tagBrowser, tagWitcher, tagUnreal, tagCustomWl, tagCustomBl;
    bool detBrowser  = ClassifyProcessAsGame(L"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe", L"Chrome_WidgetWin_1", false, tagBrowser);
    bool detWitcher  = ClassifyProcessAsGame(L"D:\\SteamLibrary\\steamapps\\common\\The Witcher 3\\bin\\x64_dx12\\witcher3.exe", L"W2ViewportClass", true, tagWitcher);
    bool detUnreal   = ClassifyProcessAsGame(L"C:\\Games\\MyGame\\Game-Win64-Shipping.exe", L"UnrealWindow", true, tagUnreal);
    bool detCustomWl = ClassifyProcessAsGame(L"D:\\Emulators\\pcsx2-qt.exe", L"Qt6QWindowIcon", false, tagCustomWl, &c2.customGameWhitelist, &c2.customGameBlacklist);
    bool detCustomBl = ClassifyProcessAsGame(L"C:\\Games\\blender.exe", L"GHOST_WindowClass", true, tagCustomBl, &c2.customGameWhitelist, &c2.customGameBlacklist);
    if (detBrowser || !detWitcher || !detUnreal || !detCustomWl || detCustomBl ||
        tagCustomWl != L"Custom Game" || tagCustomBl != L"Ignored (User)" ||
        !IsTransientCaptureProcess(L"screenclippinghost.exe") ||
        !IsTransientCaptureProcess(L"SnippingTool.exe") ||
        IsTransientCaptureProcess(L"witcher3.exe")) {
        out << "[FAIL] Game-Only / override classifier assertion failed.\n";
        WriteStdout(out.str());
        fs::remove_all(tempBase, ec);
        return 1;
    }
    out << "[PASS] Game-Only detector + whitelist/blacklist verified (Chrome=Non-Game, Witcher3="
        << WideToUtf8(tagWitcher) << ", UE=" << WideToUtf8(tagUnreal) << ", CustomWL=" << WideToUtf8(tagCustomWl) << ")\n";

    // 3. ReShade .fx install & uninstall
    fs::path rsDir = tempBase / L"game_dir";
    fs::path rsShaders = rsDir / L"reshade-shaders" / L"Shaders";
    fs::create_directories(rsShaders, ec);

    std::wstring installedPath, errMsg, remMsg;
    if (!InstallReShadeShader(rsDir.wstring(), c1.profiles[1], installedPath, errMsg)) {
        out << "[FAIL] InstallReShadeShader failed: " << WideToUtf8(errMsg) << "\n";
        WriteStdout(out.str());
        fs::remove_all(tempBase, ec);
        return 1;
    }
    if (!fs::exists(installedPath, ec) || fs::file_size(installedPath, ec) < 500) {
        out << "[FAIL] Installed .fx missing or too small.\n";
        WriteStdout(out.str());
        fs::remove_all(tempBase, ec);
        return 1;
    }
    if (!UninstallReShadeShader(rsDir.wstring(), remMsg) || fs::exists(installedPath, ec)) {
        out << "[FAIL] UninstallReShadeShader failed.\n";
        WriteStdout(out.str());
        fs::remove_all(tempBase, ec);
        return 1;
    }
    out << "[PASS] ReShade .fx install & clean uninstall verified\n";

    fs::remove_all(tempBase, ec);
    out << "ALL SELF-TESTS PASSED!\n";
    WriteStdout(out.str());
    return 0;
}

int ExportShadersToDir(const std::filesystem::path& outDir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(outDir, ec);

    {
        std::ofstream f(outDir / L"DynamicVibrance.hlsl", std::ios::binary | std::ios::trunc);
        if (!f) {
            WriteStdout("Failed writing DynamicVibrance.hlsl\n");
            return 1;
        }
        f << kDynamicVibranceHLSL;
    }
    {
        FilterParams fp{};
        std::ofstream f(outDir / L"AIDynamicVibranceEnhance.fx", std::ios::binary | std::ios::trunc);
        if (!f) {
            WriteStdout("Failed writing AIDynamicVibranceEnhance.fx\n");
            return 2;
        }
        f << GenerateReShadeFx(fp);
    }
    WriteStdout("Exported shaders cleanly.\n");
    return 0;
}

int RunUninstall() {
    namespace fs = std::filesystem;
    int choice = MessageBoxW(
        nullptr,
        L"Uninstall AI Dynamic Vibrance Enhance and remove its shortcuts and configuration?",
        L"Uninstall AI Dynamic Vibrance Enhance",
        MB_YESNO | MB_ICONQUESTION
    );
    if (choice != IDYES) {
        return 0;
    }

    // Stop any running background instance gracefully
    HWND runningTray = FindWindowExW(HWND_MESSAGE, nullptr, L"AIDynamicVibranceTrayWnd", nullptr);
    if (runningTray) {
        PostMessageW(runningTray, WM_COMMAND, CMD_EXIT, 0);
        Sleep(400);
    }

    SetLaunchAtStartup(false);
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\AIDynamicVibranceEnhance");

    auto removeLink = [](KNOWNFOLDERID fid, const std::wstring& subPath) {
        PWSTR rawPath = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(fid, 0, nullptr, &rawPath)) && rawPath) {
            std::error_code ec;
            fs::path target = fs::path(rawPath) / subPath;
            fs::remove_all(target, ec);
            CoTaskMemFree(rawPath);
        }
    };
    removeLink(FOLDERID_Desktop, L"AI Dynamic Vibrance Enhance.lnk");
    removeLink(FOLDERID_Programs, L"AI Dynamic Vibrance Enhance");

    fs::path exeDir = GetExeDirectory();
    std::error_code ec;
    fs::remove_all(exeDir / L"shaders", ec);
    fs::remove(exeDir / L"README.md", ec);
    fs::remove(exeDir / L"profiles.json", ec);

    std::wstring cmdArgs = L"/c ping 127.0.0.1 -n 3 > nul & del /f /q \"" +
        (exeDir / L"AIDynamicVibranceEnhance.exe").wstring() + L"\" & rd /q \"" + exeDir.wstring() + L"\"";
    ShellExecuteW(nullptr, L"open", L"cmd.exe", cmdArgs.c_str(), nullptr, SW_HIDE);

    MessageBoxW(
        nullptr,
        L"AI Dynamic Vibrance Enhance has been removed.",
        L"Uninstall Complete",
        MB_OK | MB_ICONINFORMATION
    );
    return 0;
}

struct AppState {
    AppConfig config = AppConfig::CreateDefault();
    ControlHub controlHub;
    Dx11Overlay overlay;
    SidebarUI sidebar;
    HWND trayHwnd = nullptr;
    NOTIFYICONDATAW nid{};
    bool trayAdded = false;
    std::wstring lastTrayTip;
    HICON appIcon = nullptr;
    bool running = true;
    bool sidebarHotkeyRegistered = false;
    uint32_t regSidebarMods = 0;
    uint32_t regSidebarVk = 0;
    bool quickHotkeyRegistered = false;
    uint32_t regQuickMods = 0;
    uint32_t regQuickVk = 0;
    bool screenshotHotkeyRegistered = false;
    uint32_t regShotMods = 0;
    uint32_t regShotVk = 0;
    bool splitHotkeysRegistered = false;
    bool hotkeysSuspended = false;
    bool paramsDirty = true;
    bool configDirty = false;
    ULONGLONG lastConfigEditMs = 0;
    ULONGLONG lastFgCheckMs = 0;
    std::wstring currentAppExe;
    HWND currentAppHwnd = nullptr;
    bool currentAppIsGame = false;
    bool exclusiveFullscreen = false;
    ULONGLONG gameEnteredMs = 0;
    int lastNonZeroProfile = 1;
    bool lastTelActive = false;
    bool lastTelWgc = false;
};

std::wstring BuildScreenshotFilePath() {
    std::filesystem::path baseDir = GetScreenshotsDirectory();
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t nameBuf[64] = {};
    swprintf_s(
        nameBuf,
        L"ADV_%04u%02u%02u_%02u%02u%02u_%03u.png",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds
    );
    return (baseDir / nameBuf).wstring();
}

void TriggerFilteredScreenshot(AppState& state) {
    state.overlay.RequestScreenshot(BuildScreenshotFilePath());
}

void SyncSingleHotkey(
    HWND trayHwnd,
    int id,
    bool want,
    uint32_t desiredMods,
    uint32_t desiredVk,
    bool& isRegistered,
    uint32_t& regMods,
    uint32_t& regVk
) {
    if (!trayHwnd) return;
    bool mismatch = isRegistered && (!want || regMods != desiredMods || regVk != desiredVk);
    if (mismatch) {
        UnregisterHotKey(trayHwnd, id);
        isRegistered = false;
        regMods = 0;
        regVk = 0;
    }
    if (want && !isRegistered && desiredVk != 0) {
        if (RegisterHotKey(trayHwnd, id, desiredMods | MOD_NOREPEAT, desiredVk)) {
            isRegistered = true;
            regMods = desiredMods;
            regVk = desiredVk;
        }
    }
}

void SyncOverlayAndHotkey(AppState& state) {
    state.sidebar.SetExcludeFromCapture(state.config.hideSidebarFromCapture);
    if (state.config.activeProfile >= 1 && state.config.activeProfile <= 3) {
        state.lastNonZeroProfile = state.config.activeProfile;
    }

    bool wantSidebar = state.config.overlayEnabled && !state.hotkeysSuspended && state.config.sidebarHotkeyVk != 0;
    SyncSingleHotkey(
        state.trayHwnd, HOTKEY_TOGGLE_ID, wantSidebar,
        state.config.sidebarHotkeyMods, state.config.sidebarHotkeyVk,
        state.sidebarHotkeyRegistered, state.regSidebarMods, state.regSidebarVk
    );

    bool wantQuick = state.config.overlayEnabled && state.config.quickToggleEnabled &&
                     !state.hotkeysSuspended && state.config.quickToggleVk != 0;
    SyncSingleHotkey(
        state.trayHwnd, HOTKEY_QUICK_TOGGLE_ID, wantQuick,
        state.config.quickToggleMods, state.config.quickToggleVk,
        state.quickHotkeyRegistered, state.regQuickMods, state.regQuickVk
    );

    bool wantShot = state.config.overlayEnabled && !state.hotkeysSuspended && state.config.screenshotHotkeyVk != 0;
    SyncSingleHotkey(
        state.trayHwnd, HOTKEY_SCREENSHOT_ID, wantShot,
        state.config.screenshotHotkeyMods, state.config.screenshotHotkeyVk,
        state.screenshotHotkeyRegistered, state.regShotMods, state.regShotVk
    );

    bool wantSplitHotkeys = state.config.overlayEnabled && !state.hotkeysSuspended;
    if (state.splitHotkeysRegistered && !wantSplitHotkeys && state.trayHwnd) {
        UnregisterHotKey(state.trayHwnd, HOTKEY_SPLIT_TOGGLE_ID);
        UnregisterHotKey(state.trayHwnd, HOTKEY_SPLIT_DEC_ID);
        UnregisterHotKey(state.trayHwnd, HOTKEY_SPLIT_INC_ID);
        state.splitHotkeysRegistered = false;
    } else if (!state.splitHotkeysRegistered && wantSplitHotkeys && state.trayHwnd) {
        RegisterHotKey(state.trayHwnd, HOTKEY_SPLIT_TOGGLE_ID, MOD_ALT | MOD_NOREPEAT, VK_OEM_5);
        RegisterHotKey(state.trayHwnd, HOTKEY_SPLIT_DEC_ID, MOD_ALT, VK_OEM_4);
        RegisterHotKey(state.trayHwnd, HOTKEY_SPLIT_INC_ID, MOD_ALT, VK_OEM_6);
        state.splitHotkeysRegistered = true;
    }

    if (state.trayAdded && state.trayHwnd) {
        std::wstring tip = L"AI Dynamic Vibrance Enhance (" +
                           FormatHotkeyString(state.config.sidebarHotkeyMods, state.config.sidebarHotkeyVk) + L")";
        if (tip.size() >= std::size(state.nid.szTip)) {
            tip.resize(std::size(state.nid.szTip) - 1);
        }
        if (tip != state.lastTrayTip) {
            state.lastTrayTip = tip;
            wcscpy_s(state.nid.szTip, tip.c_str());
            Shell_NotifyIconW(NIM_MODIFY, &state.nid);
        }
    }

    bool isFse = IsExclusiveFullscreenActive();
    state.exclusiveFullscreen = isFse;
    if (state.config.overlayEnabled) {
        if (isFse && state.sidebar.IsOpen()) {
            state.sidebar.SetOpen(false);
        }
        bool allow = (!state.config.onlyInGames || state.currentAppIsGame) && !isFse;
        bool anyEngineFeature = (state.config.activeProfile != 0) ||
                                (state.config.scaleMode != SCALE_OFF) ||
                                (state.config.fgMode != FG_OFF);
        state.overlay.SetActive(anyEngineFeature && allow,
                               isFse ? nullptr : state.sidebar.Hwnd(),
                               state.currentAppHwnd);
    } else {
        if (state.sidebar.IsOpen()) {
            state.sidebar.SetOpen(false);
        }
        state.overlay.SetActive(false, isFse ? nullptr : state.sidebar.Hwnd(), nullptr);
    }
}

void PushOverlayRenderState(AppState& state, bool forceRedraw) {
    FilterParams bypassFp{};
    bypassFp.enableDynamicVibrance = false;
    const FilterParams& activeFp = (state.config.activeProfile >= 1 && state.config.activeProfile <= 3)
        ? state.config.profiles[state.config.activeProfile - 1]
        : bypassFp;
    state.overlay.UpdateRenderState(
        activeFp,
        state.config.splitPreview,
        state.config.splitPosition,
        forceRedraw,
        state.config
    );
}

void ShowTrayMenu(AppState* state) {
    HMENU menu = CreatePopupMenu();
    std::wstring hkStr = FormatHotkeyString(state->config.sidebarHotkeyMods, state->config.sidebarHotkeyVk);
    AppendMenuW(menu, MF_STRING, CMD_SHOW_HUB, L"Open Control Hub");
    UINT sbFlags = MF_STRING | (state->config.overlayEnabled ? 0 : MF_GRAYED);
    std::wstring toggleLabel = (state->sidebar.IsOpen() ? L"Close Filter Sidebar (" : L"Open Filter Sidebar (") + hkStr + L")";
    AppendMenuW(menu, sbFlags, CMD_TOGGLE_SIDEBAR, toggleLabel.c_str());
    std::wstring shotLabel = L"Save Filtered Screenshot (" +
        FormatHotkeyString(state->config.screenshotHotkeyMods, state->config.screenshotHotkeyVk) + L")";
    AppendMenuW(menu, MF_STRING, CMD_SCREENSHOT, shotLabel.c_str());
    std::wstring masterLabel = L"In-Game Overlay (" + hkStr + L")";
    AppendMenuW(menu, MF_STRING | (state->config.overlayEnabled ? MF_CHECKED : 0),
                CMD_TOGGLE_OVERLAY_MASTER, masterLabel.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    auto addProf = [&](UINT cmd, const wchar_t* label, int idx) {
        UINT flags = MF_STRING | (state->config.activeProfile == idx ? MF_CHECKED : 0);
        AppendMenuW(menu, flags, cmd, label);
    };
    addProf(CMD_PROFILE_0, L"Profile: None (Overlay Off)", 0);
    addProf(CMD_PROFILE_1, L"Profile 1", 1);
    addProf(CMD_PROFILE_2, L"Profile 2", 2);
    addProf(CMD_PROFILE_3, L"Profile 3", 3);

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, CMD_EXIT, L"Exit");

    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(state->trayHwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, state->trayHwnd, nullptr);
    PostMessageW(state->trayHwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AppState* state = reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = reinterpret_cast<AppState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (!state) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
        case WM_HOTKEY:
            if (wParam == HOTKEY_TOGGLE_ID && state->config.overlayEnabled) {
                if (IsExclusiveFullscreenActive()) {
                    SyncOverlayAndHotkey(*state);
                    return 0;
                }
                if (!state->sidebar.IsOpen()) {
                    state->overlay.RequestHdrRefresh();
                }
                state->sidebar.Toggle();
            } else if (wParam == HOTKEY_QUICK_TOGGLE_ID &&
                       state->config.overlayEnabled && state->config.quickToggleEnabled) {
                if (IsExclusiveFullscreenActive()) {
                    SyncOverlayAndHotkey(*state);
                    return 0;
                }
                if (state->config.activeProfile == 0) {
                    state->config.activeProfile = std::clamp(state->lastNonZeroProfile, 1, 3);
                } else {
                    state->lastNonZeroProfile = state->config.activeProfile;
                    state->config.activeProfile = 0;
                }
                if (state->config.autoSwitchPerApp && !state->currentAppExe.empty() &&
                    state->currentAppExe != L"desktop" && (!state->config.onlyInGames || state->currentAppIsGame)) {
                    state->config.perAppProfile[state->currentAppExe] = state->config.activeProfile;
                }
                SyncOverlayAndHotkey(*state);
                PushOverlayRenderState(*state, true);
                state->configDirty = true;
                state->lastConfigEditMs = GetTickCount64();
                state->controlHub.Invalidate();
                state->sidebar.Invalidate();
            } else if (wParam == HOTKEY_SCREENSHOT_ID && state->config.overlayEnabled) {
                TriggerFilteredScreenshot(*state);
            } else if (wParam == HOTKEY_SPLIT_TOGGLE_ID && state->config.overlayEnabled) {
                state->config.splitPreview = !state->config.splitPreview;
                PushOverlayRenderState(*state, true);
                state->configDirty = true;
                state->lastConfigEditMs = GetTickCount64();
                state->controlHub.Invalidate();
                state->sidebar.Invalidate();
            } else if ((wParam == HOTKEY_SPLIT_DEC_ID || wParam == HOTKEY_SPLIT_INC_ID) && state->config.overlayEnabled) {
                float step = (wParam == HOTKEY_SPLIT_INC_ID) ? 0.05f : -0.05f;
                state->config.splitPreview = true;
                state->config.splitPosition = std::clamp(state->config.splitPosition + step, 0.05f, 0.95f);
                PushOverlayRenderState(*state, true);
                state->configDirty = true;
                state->lastConfigEditMs = GetTickCount64();
                state->controlHub.Invalidate();
                state->sidebar.Invalidate();
            }
            return 0;
        case WM_APP_SHOW_HUB:
            state->overlay.RequestHdrRefresh();
            state->controlHub.Show(0);
            return 0;
        case WM_TRAYICON:
            if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
                state->overlay.RequestHdrRefresh();
                state->controlHub.Show(0);
            } else if (lParam == WM_RBUTTONUP) {
                ShowTrayMenu(state);
            }
            return 0;
        case WM_COMMAND: {
            UINT cmd = LOWORD(wParam);
            switch (cmd) {
                case CMD_SHOW_HUB:
                    state->overlay.RequestHdrRefresh();
                    state->controlHub.Show(0);
                    break;
                case CMD_TOGGLE_SIDEBAR:
                    if (state->config.overlayEnabled) {
                        if (!state->sidebar.IsOpen()) {
                            state->overlay.RequestHdrRefresh();
                        }
                        state->sidebar.Toggle();
                    }
                    break;
                case CMD_TOGGLE_OVERLAY_MASTER:
                    state->config.overlayEnabled = !state->config.overlayEnabled;
                    SyncOverlayAndHotkey(*state);
                    PushOverlayRenderState(*state, true);
                    state->configDirty = true;
                    state->lastConfigEditMs = GetTickCount64();
                    state->controlHub.Invalidate();
                    state->sidebar.Invalidate();
                    break;
                case CMD_SCREENSHOT:
                    TriggerFilteredScreenshot(*state);
                    break;
                case CMD_PROFILE_0:
                case CMD_PROFILE_1:
                case CMD_PROFILE_2:
                case CMD_PROFILE_3: {
                    int p = static_cast<int>(cmd - CMD_PROFILE_0);
                    state->config.activeProfile = p;
                    if (state->config.autoSwitchPerApp && !state->currentAppExe.empty() &&
                        state->currentAppExe != L"desktop" && (!state->config.onlyInGames || state->currentAppIsGame)) {
                        state->config.perAppProfile[state->currentAppExe] = p;
                    }
                    SyncOverlayAndHotkey(*state);
                    PushOverlayRenderState(*state, true);
                    state->configDirty = true;
                    state->lastConfigEditMs = GetTickCount64();
                    state->controlHub.Invalidate();
                    state->sidebar.Invalidate();
                    break;
                }
                case CMD_EXIT:
                    state->running = false;
                    PostQuitMessage(0);
                    break;
                default:
                    break;
            }
            return 0;
        }
        case WM_DESTROY:
            state->running = false;
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    RoInitialize(RO_INIT_SINGLETHREADED);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool startMinimized = false;
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--self-test") == 0) {
            LocalFree(argv);
            int rc = RunFullSelfTest();
            RoUninitialize();
            return rc;
        }
        if (wcscmp(argv[i], L"--export-shaders") == 0 && (i + 1) < argc) {
            std::filesystem::path outDir(argv[i + 1]);
            LocalFree(argv);
            int rc = ExportShadersToDir(outDir);
            RoUninitialize();
            return rc;
        }
        if (wcscmp(argv[i], L"--uninstall") == 0) {
            LocalFree(argv);
            int rc = RunUninstall();
            RoUninitialize();
            return rc;
        }
        if (wcscmp(argv[i], L"--minimized") == 0) {
            startMinimized = true;
        }
    }
    if (argv) LocalFree(argv);

    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"Local\\AIDynamicVibranceEnhance_SingleInstance");
    if (hMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowExW(HWND_MESSAGE, nullptr, L"AIDynamicVibranceTrayWnd", nullptr);
        if (existing) {
            PostMessageW(existing, WM_APP_SHOW_HUB, 0, 0);
        } else {
            MessageBoxW(
                nullptr,
                L"AI Dynamic Vibrance Enhance is already running in the background.\nCheck the System Tray icon to open Control Hub or press Alt + X.",
                L"AI Dynamic Vibrance Enhance",
                MB_OK | MB_ICONINFORMATION
            );
        }
        CloseHandle(hMutex);
        RoUninitialize();
        return 0;
    }

    AppState state;
    std::filesystem::path cfgPath = GetDefaultConfigPath();
    LoadConfig(state.config, cfgPath);
    if (IsLaunchAtStartupEnabled()) {
        state.config.launchAtStartup = true;
    }

    // Create Tray / Hotkey message window
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = TrayWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"AIDynamicVibranceTrayWnd";
    RegisterClassExW(&wc);

    state.trayHwnd = CreateWindowExW(
        0, wc.lpszClassName, L"AI Dynamic Vibrance Enhance Tray",
        0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInst, &state
    );
    if (!state.trayHwnd) {
        MessageBoxW(nullptr, L"Failed to create application window.", L"Error", MB_OK | MB_ICONERROR);
        if (hMutex) CloseHandle(hMutex);
        RoUninitialize();
        return 1;
    }

    std::string err;
    if (!state.overlay.Initialize(hInst, err)) {
        MessageBoxA(nullptr, err.c_str(), "DirectX 11 Initialization Failed", MB_OK | MB_ICONERROR);
        DestroyWindow(state.trayHwnd);
        if (hMutex) CloseHandle(hMutex);
        RoUninitialize();
        return 1;
    }

    SidebarCallbacks scbs;
    scbs.onConfigChanged = [&state]() {
        SyncOverlayAndHotkey(state);
        PushOverlayRenderState(state, true);
        state.configDirty = true;
        state.lastConfigEditMs = GetTickCount64();
        state.controlHub.Invalidate();
    };
    scbs.onOpenHub = [&state]() {
        state.overlay.RequestHdrRefresh();
        state.controlHub.Show(0);
    };
    scbs.onExitRequested = [&state]() {
        state.running = false;
        PostQuitMessage(0);
    };

    if (!state.sidebar.Initialize(hInst, &state.config, scbs, err)) {
        MessageBoxA(nullptr, err.c_str(), "Sidebar UI Initialization Failed", MB_OK | MB_ICONERROR);
        state.overlay.Shutdown();
        DestroyWindow(state.trayHwnd);
        if (hMutex) CloseHandle(hMutex);
        RoUninitialize();
        return 1;
    }

    ControlHubCallbacks hcbs;
    hcbs.onConfigChanged = [&state]() {
        SyncOverlayAndHotkey(state);
        PushOverlayRenderState(state, true);
        state.configDirty = true;
        state.lastConfigEditMs = GetTickCount64();
        state.lastFgCheckMs = 0; // Re-evaluate active game overrides cleanly
        state.sidebar.Invalidate();
    };
    hcbs.onOpenSidebar = [&state]() {
        state.overlay.RequestHdrRefresh();
        state.sidebar.SetOpen(true);
    };
    hcbs.onExitRequested = [&state]() {
        state.running = false;
        PostQuitMessage(0);
    };
    hcbs.onHotkeyCaptureChanged = [&state](bool capturing) {
        state.hotkeysSuspended = capturing;
        SyncOverlayAndHotkey(state);
    };
    hcbs.onScreenshotRequested = [&state]() {
        TriggerFilteredScreenshot(state);
    };

    if (!state.controlHub.Initialize(hInst, &state.config, hcbs, err)) {
        MessageBoxA(nullptr, err.c_str(), "Control Hub Initialization Failed", MB_OK | MB_ICONERROR);
        state.sidebar.Shutdown();
        state.overlay.Shutdown();
        DestroyWindow(state.trayHwnd);
        if (hMutex) CloseHandle(hMutex);
        RoUninitialize();
        return 1;
    }

    std::wstring adapter = state.overlay.GetAdapterName();
    state.sidebar.SetAdapterName(adapter);
    state.controlHub.SetAdapterName(adapter);
    DisplayHdrInfo initHdr = state.overlay.GetDisplayHdrInfo();
    state.sidebar.SetDisplayHdrInfo(initHdr);
    state.controlHub.SetDisplayHdrInfo(initHdr);
    FrameGenTelemetry initTel = state.overlay.GetTelemetry();
    state.sidebar.SetTelemetry(initTel);
    state.controlHub.SetTelemetry(initTel);

    state.appIcon = CreateAppTrayIcon(hInst);
    state.nid.cbSize = sizeof(state.nid);
    state.nid.hWnd = state.trayHwnd;
    state.nid.uID = TRAY_ICON_ID;
    state.nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    state.nid.uCallbackMessage = WM_TRAYICON;
    state.nid.hIcon = state.appIcon;
    std::wstring initTip = L"AI Dynamic Vibrance Enhance (" +
                           FormatHotkeyString(state.config.sidebarHotkeyMods, state.config.sidebarHotkeyVk) + L")";
    if (initTip.size() >= std::size(state.nid.szTip)) {
        initTip.resize(std::size(state.nid.szTip) - 1);
    }
    state.lastTrayTip = initTip;
    wcscpy_s(state.nid.szTip, initTip.c_str());
    state.trayAdded = (Shell_NotifyIconW(NIM_ADD, &state.nid) != FALSE);

    DWORD selfPid = GetCurrentProcessId();
    ForegroundAppInfo initFg;
    if (DetectActiveForegroundApp(selfPid, initFg, &state.config.customGameWhitelist, &state.config.customGameBlacklist)) {
        state.currentAppExe = initFg.exeName;
        state.currentAppHwnd = initFg.hwnd;
        state.currentAppIsGame = initFg.isGame;
        state.sidebar.SetActiveAppInfo(initFg.exeName, initFg.displayName, initFg.isGame, initFg.engineTag);
        state.controlHub.SetActiveAppInfo(initFg.exeName, initFg.displayName, initFg.isGame, initFg.engineTag);
    }

    // Activate hotkey/overlay per config and open desktop Control Hub (unless --minimized)
    SyncOverlayAndHotkey(state);
    PushOverlayRenderState(state, true);
    if (!startMinimized) {
        state.controlHub.Show(0);
    }

    ULONGLONG lastZCheckMs = 0;
    ULONGLONG lastTelMs = 0;

    while (state.running) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                state.running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!state.running) break;

        ULONGLONG now = GetTickCount64();

        // Foreground app polling every 250ms (Game-Only Auto-Detect + Exclusive Fullscreen Guard)
        if (state.lastFgCheckMs == 0 || now - state.lastFgCheckMs >= 250) {
            state.lastFgCheckMs = now;
            ForegroundAppInfo fg;
            bool wasGame = state.currentAppIsGame;
            bool isFse = IsExclusiveFullscreenActive();
            bool changedFse = (isFse != state.exclusiveFullscreen);
            if (DetectActiveForegroundApp(selfPid, fg, &state.config.customGameWhitelist, &state.config.customGameBlacklist)) {
                bool changedApp = (fg.exeName != state.currentAppExe);
                bool changedHwnd = (fg.hwnd != state.currentAppHwnd);
                bool changedGameState = (fg.isGame != state.currentAppIsGame);
                state.currentAppExe = fg.exeName;
                state.currentAppHwnd = fg.hwnd;
                state.currentAppIsGame = fg.isGame;
                state.exclusiveFullscreen = isFse;

                std::wstring displayEngineTag = fg.engineTag;
                if (fg.isGame && isFse) {
                    displayEngineTag = L"Exclusive Fullscreen \u2014 Use Borderless or Legacy .fx";
                }
                state.sidebar.SetActiveAppInfo(fg.exeName, fg.displayName, fg.isGame, displayEngineTag);
                state.controlHub.SetActiveAppInfo(fg.exeName, fg.displayName, fg.isGame, displayEngineTag);
                if (changedApp && state.config.autoSwitchPerApp && fg.exeName != L"desktop" &&
                    (!state.config.onlyInGames || fg.isGame)) {
                    auto it = state.config.perAppProfile.find(fg.exeName);
                    if (it != state.config.perAppProfile.end() && it->second != state.config.activeProfile) {
                        state.config.activeProfile = it->second;
                        PushOverlayRenderState(state, true);
                        state.sidebar.Invalidate();
                        state.controlHub.Invalidate();
                    }
                }
                if (!fg.isGame || isFse) {
                    state.gameEnteredMs = 0;
                    if (changedApp || changedGameState || changedFse || changedHwnd) {
                        SyncOverlayAndHotkey(state);
                    }
                } else if (fg.isGame && !wasGame) {
                    state.gameEnteredMs = now;
                } else if (changedApp || changedGameState || changedFse || changedHwnd) {
                    if (state.gameEnteredMs == 0) {
                        SyncOverlayAndHotkey(state);
                    }
                }
            } else if (changedFse) {
                state.exclusiveFullscreen = isFse;
                if (isFse) {
                    state.gameEnteredMs = 0;
                }
                SyncOverlayAndHotkey(state);
            }
        }
        if (state.currentAppIsGame && state.gameEnteredMs != 0 && (now - state.gameEnteredMs >= 600)) {
            state.gameEnteredMs = 0;
            SyncOverlayAndHotkey(state);
        }

        // Maintain sidebar + overlay topmost z-order on owning threads cleanly
        if (now - lastZCheckMs >= 1000) {
            lastZCheckMs = now;
            if (state.sidebar.IsVisible()) {
                SetWindowPos(state.sidebar.Hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
                if (state.overlay.IsActive()) {
                    state.overlay.EnsureZOrder(state.sidebar.Hwnd());
                }
            }
        }

        // Event-driven HDR / display update notifications from render thread (WM_DISPLAYCHANGE)
        DisplayHdrInfo nextHdr{};
        if (state.overlay.ConsumeDisplayHdrUpdate(nextHdr)) {
            state.sidebar.SetDisplayHdrInfo(nextHdr);
            state.controlHub.SetDisplayHdrInfo(nextHdr);
            PushOverlayRenderState(state, true);
        }

        // Push live Frame Gen & capture telemetry (immediately on status change, or every 500ms)
        bool curOverlayActive = state.overlay.IsActive();
        bool curUsingWgc = state.overlay.IsRecordableWgc();
        bool statusFlipped = (curOverlayActive != state.lastTelActive || curUsingWgc != state.lastTelWgc);
        if (lastTelMs == 0 || statusFlipped || (now - lastTelMs >= 500)) {
            lastTelMs = now;
            state.lastTelActive = curOverlayActive;
            state.lastTelWgc = curUsingWgc;
            if (state.sidebar.IsVisible() || state.controlHub.IsVisible()) {
                FrameGenTelemetry tel = state.overlay.GetTelemetry();
                if (state.sidebar.IsVisible()) state.sidebar.SetTelemetry(tel);
                if (state.controlHub.IsVisible()) state.controlHub.SetTelemetry(tel);
            }
        }

        // Poll completed built-in PNG screenshot result
        {
            bool ssOk = false;
            std::wstring ssPath, ssErr;
            if (state.overlay.ConsumeScreenshotResult(ssOk, ssPath, ssErr)) {
                std::wstring fname = std::filesystem::path(ssPath).filename().wstring();
                std::wstring toast = ssOk ? (L"Saved PNG: " + fname) : ssErr;
                state.controlHub.ShowFooterToast(toast, !ssOk);
                if (!state.controlHub.IsVisible() && state.trayAdded && state.trayHwnd) {
                    NOTIFYICONDATAW tipNid = state.nid;
                    tipNid.uFlags = NIF_INFO;
                    wcscpy_s(tipNid.szInfoTitle, ssOk ? L"Filtered Screenshot Saved" : L"Screenshot Unavailable");
                    std::wstring body = ssOk ? ssPath : ssErr;
                    if (body.size() >= std::size(tipNid.szInfo)) {
                        body.resize(std::size(tipNid.szInfo) - 1);
                    }
                    wcscpy_s(tipNid.szInfo, body.c_str());
                    tipNid.dwInfoFlags = ssOk ? NIIF_INFO : NIIF_WARNING;
                    Shell_NotifyIconW(NIM_MODIFY, &tipNid);
                }
            }
        }

        // Debounced config save (300ms after last edit)
        if (state.configDirty && (now - state.lastConfigEditMs >= 300)) {
            SaveConfig(state.config, cfgPath);
            state.configDirty = false;
        }

        state.sidebar.TickAnimation();

        bool uiAnimating = state.sidebar.IsVisible() || state.controlHub.IsVisible();
        MsgWaitForMultipleObjects(0, nullptr, FALSE, uiAnimating ? 16 : 200, QS_ALLINPUT);
    }

    if (state.configDirty) {
        SaveConfig(state.config, cfgPath);
    }

    if (state.sidebarHotkeyRegistered) {
        UnregisterHotKey(state.trayHwnd, HOTKEY_TOGGLE_ID);
    }
    if (state.quickHotkeyRegistered) {
        UnregisterHotKey(state.trayHwnd, HOTKEY_QUICK_TOGGLE_ID);
    }
    if (state.screenshotHotkeyRegistered) {
        UnregisterHotKey(state.trayHwnd, HOTKEY_SCREENSHOT_ID);
    }
    if (state.splitHotkeysRegistered) {
        UnregisterHotKey(state.trayHwnd, HOTKEY_SPLIT_TOGGLE_ID);
        UnregisterHotKey(state.trayHwnd, HOTKEY_SPLIT_DEC_ID);
        UnregisterHotKey(state.trayHwnd, HOTKEY_SPLIT_INC_ID);
    }
    Shell_NotifyIconW(NIM_DELETE, &state.nid);
    if (state.appIcon) DestroyIcon(state.appIcon);

    state.controlHub.Shutdown();
    state.sidebar.Shutdown();
    state.overlay.Shutdown();
    DestroyWindow(state.trayHwnd);

    if (hMutex) CloseHandle(hMutex);
    RoUninitialize();
    return 0;
}
