#pragma once

#include "types.h"

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <functional>
#include <string>
#include <vector>

struct ControlHubCallbacks {
    std::function<void()> onConfigChanged;
    std::function<void()> onOpenSidebar;
    std::function<void()> onExitRequested;
    std::function<void(bool)> onHotkeyCaptureChanged;
    std::function<void()> onScreenshotRequested;
};

class ControlHub {
public:
    ControlHub() = default;
    ~ControlHub();

    ControlHub(const ControlHub&) = delete;
    ControlHub& operator=(const ControlHub&) = delete;

    bool Initialize(HINSTANCE hInst, AppConfig* config, ControlHubCallbacks callbacks, std::string& outError);
    void Shutdown();

    void Show(int initialTab = -1);
    void Hide();
    bool IsVisible() const { return hwnd_ && IsWindowVisible(hwnd_); }
    HWND Hwnd() const { return hwnd_; }

    void SetAdapterName(const std::wstring& adapterName);
    void SetDisplayHdrInfo(const DisplayHdrInfo& info);
    void SetTelemetry(const FrameGenTelemetry& tel);
    void SetActiveAppInfo(const std::wstring& exeName, const std::wstring& displayTitle, bool isGame, const std::wstring& engineTag);
    void ShowFooterToast(const std::wstring& message, bool isError = false);
    void Invalidate();

private:
    enum HubControlKind {
        HUB_TAB_OVERVIEW,
        HUB_TAB_SCALING,
        HUB_TAB_GAMES,
        HUB_TAB_PRESETS,
        HUB_TAB_LEGACY,
        HUB_TAB_ABOUT,
        HUB_VIEW_GRID,
        HUB_VIEW_LIST,
        HUB_BTN_WIN_MIN,
        HUB_BTN_WIN_CLOSE,
        HUB_TOGGLE_OVERLAY,
        HUB_BTN_HOTKEY_SIDEBAR,
        HUB_TOGGLE_QUICKHOTKEY,
        HUB_BTN_HOTKEY_QUICK,
        HUB_BTN_HOTKEY_SCREENSHOT,
        HUB_BTN_HOTKEY_RESET,
        HUB_TOGGLE_ONLYGAMES,
        HUB_TOGGLE_AUTOSWITCH,
        HUB_TOGGLE_HIDESIDEBAR,
        HUB_TOGGLE_SPLIT,
        HUB_SLIDER_SPLITPOS,
        HUB_TOGGLE_STARTUP,
        HUB_BTN_WL_ADD,
        HUB_BTN_WL_REMOVE,
        HUB_BTN_WL_PREV,
        HUB_BTN_WL_NEXT,
        HUB_BTN_BL_ADD,
        HUB_BTN_BL_REMOVE,
        HUB_BTN_BL_PREV,
        HUB_BTN_BL_NEXT,
        HUB_PRESET_SLOT_1,
        HUB_PRESET_SLOT_2,
        HUB_PRESET_SLOT_3,
        HUB_PRESET_QUICK_0,
        HUB_PRESET_QUICK_1,
        HUB_PRESET_QUICK_2,
        HUB_PRESET_QUICK_3,
        HUB_PRESET_QUICK_4,
        HUB_BTN_PRESET_EXPORT,
        HUB_BTN_PRESET_IMPORT,
        HUB_BTN_SCREENSHOT,
        HUB_BTN_OPEN_DATA_FOLDER,
        HUB_BTN_OPEN_SCREENSHOTS,
        HUB_BTN_RESET_PROFILE,
        HUB_BTN_RESET_ALL,
        HUB_BTN_OPEN_SIDEBAR,
        HUB_BTN_EXIT,
        HUB_BTN_RS_BROWSE,
        HUB_M2_PROF_1,
        HUB_M2_PROF_2,
        HUB_M2_PROF_3,
        HUB_BTN_RS_INSTALL,
        HUB_BTN_RS_REMOVE,
        HUB_BTN_GITHUB,

        // Scaling & Optical Flow Frame Generation controls (v2.0.0)
        HUB_SCALE_OFF,
        HUB_SCALE_FSR1,
        HUB_SCALE_NIS,
        HUB_SCALE_INTEGER,
        HUB_SCALE_ANIME4K,
        HUB_SLIDER_SCALE_SHARP,
        HUB_TOGGLE_SCALE_STRETCH,
        HUB_FG_OFF,
        HUB_FG_2X,
        HUB_FG_3X,
        HUB_FG_4X,
        HUB_FG_ADAPTIVE,
        HUB_SLIDER_FG_FLOWSCALE,
        HUB_SLIDER_FG_TARGETFPS,
        HUB_TOGGLE_FG_FALLBACK,
        HUB_TOGGLE_FG_HUDPROTECT,
        HUB_CAPTURE_DXGI,
        HUB_CAPTURE_WGC,
        HUB_TOGGLE_ALLOW_TEARING,
    };

    struct HitRect {
        HubControlKind kind;
        D2D1_RECT_F rect;
        bool isSlider = false;
        int payloadIdx = -1;
    };

    bool CreateRenderResources();
    void DiscardRenderResources();
    void Render();
    void ResizeRenderTarget();
    void UpdateWindowSizeForDpi(UINT dpi);

    void DrawTopBarAndFooter();
    void DrawSettingIcon(int iconIdx, float x, float y);
    void DrawTogglePill(HubControlKind k, float x, float ty, bool on, bool large = false);
    void DrawFlatButton(HubControlKind k, const std::wstring& label, D2D1_RECT_F r, bool primary, bool danger = false, int payload = -1);
    void DrawHubSlider(HubControlKind k, const std::wstring& label, const std::wstring& valueText, int val, int minVal, int maxVal, float sx, float sy, float sw);

    void RenderOverviewTab();
    void RenderScalingTab();
    void RenderGamesTab();
    void RenderPresetsTab();
    void RenderLegacyTab();
    void RenderAboutTab();

    void SetRecordingHotkey(int target);
    void HandleHotkeyKeyDown(UINT vk);
    void HandleMouseDown(float dipX, float dipY);
    void HandleMouseMove(float dipX, float dipY);
    void HandleMouseUp();
    void UpdateHubSliderFromX(HubControlKind kind, float dipX, const D2D1_RECT_F& trackRect);
    void PickReShadeFolder();
    void PickExecutableForList(bool whitelist);
    void ExportPresetWithDialog();
    void ImportPresetWithDialog();
    void OpenDataFolder();
    void OpenScreenshotsFolder();

    static LRESULT CALLBACK HubWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND hwnd_ = nullptr;
    AppConfig* config_ = nullptr;
    ControlHubCallbacks callbacks_;

    float dpiScale_ = 1.0f;
    static constexpr float kHubDipW = 920.0f;
    static constexpr float kHubDipH = 590.0f;
    static constexpr float kTopBarH = 54.0f;
    static constexpr float kFooterH = 48.0f;

    int currentTab_ = 0; // 0 = Overview, 1 = Scaling, 2 = Games, 3 = Presets, 4 = Legacy, 5 = About
    bool draggingSlider_ = false;
    HubControlKind activeSliderKind_ = HUB_SLIDER_SPLITPOS;
    D2D1_RECT_F activeTrackRect_ = {};

    int recordingHotkey_ = 0; // 0 = none, 1 = sidebar, 2 = quick toggle, 3 = screenshot
    int wlPage_ = 0;
    int blPage_ = 0;
    int presetSlot_ = 1; // 1..3

    std::wstring adapterName_ = L"Universal GPU";
    DisplayHdrInfo displayHdr_{};
    FrameGenTelemetry telemetry_{};
    std::wstring activeExe_ = L"desktop";
    std::wstring activeAppTitle_ = L"Desktop";
    bool activeAppIsGame_ = false;
    std::wstring activeEngineTag_ = L"Desktop";
    std::wstring hotkeyStatus_ = L"Click any hotkey badge to rebind. Shortcuts suspend during capture.";
    bool hotkeyStatusError_ = false;
    std::wstring gameListStatus_ = L"Overrides take effect immediately by .exe filename without restarting games.";
    bool gameListStatusError_ = false;
    std::wstring presetStatus_ = L"Export or import community .json presets, or manage saved profiles.";
    bool presetStatusError_ = false;
    std::wstring reshadeStatus_ = L"Ready to export AIDynamicVibranceEnhance.fx to a legacy game's Shaders folder.";
    bool reshadeStatusError_ = false;
    std::wstring footerToast_;
    bool footerToastError_ = false;
    ULONGLONG footerToastUntilMs_ = 0;

    std::vector<HitRect> hitRects_;

    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> rt_;

    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtBrand_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtTitle_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtSection_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtBody_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtSmall_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtCenter_;

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brTopBar_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brBg_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brCard_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brCardHover_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brBorder_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brAccent_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brAccentSoft_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brTrackBg_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brText_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brTextMuted_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brDanger_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brSuccess_;
};
