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
    void SetActiveAppInfo(const std::wstring& exeName, const std::wstring& displayTitle, bool isGame, const std::wstring& engineTag);
    void Invalidate();

private:
    enum HubControlKind {
        HUB_TAB_OVERVIEW,
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
        HUB_BTN_PRESET_EXPORT,
        HUB_BTN_PRESET_IMPORT,
        HUB_BTN_OPEN_DATA_FOLDER,
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

    void RenderOverviewTab();
    void RenderGamesTab();
    void RenderPresetsTab();
    void RenderLegacyTab();
    void RenderAboutTab();

    void SetRecordingHotkey(int target);
    void HandleHotkeyKeyDown(UINT vk);
    void HandleMouseDown(float dipX, float dipY);
    void HandleMouseMove(float dipX, float dipY);
    void HandleMouseUp();
    void UpdateSplitSliderFromX(float dipX, const D2D1_RECT_F& trackRect);
    void PickReShadeFolder();
    void PickExecutableForList(bool whitelist);
    void ExportPresetWithDialog();
    void ImportPresetWithDialog();
    void OpenDataFolder();

    static LRESULT CALLBACK HubWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND hwnd_ = nullptr;
    AppConfig* config_ = nullptr;
    ControlHubCallbacks callbacks_;

    float dpiScale_ = 1.0f;
    static constexpr float kHubDipW = 920.0f;
    static constexpr float kHubDipH = 590.0f;
    static constexpr float kTopBarH = 54.0f;
    static constexpr float kFooterH = 48.0f;

    int currentTab_ = 0; // 0 = Overview, 1 = Games, 2 = Presets, 3 = Legacy, 4 = About
    bool draggingSlider_ = false;
    D2D1_RECT_F activeTrackRect_ = {};

    int recordingHotkey_ = 0; // 0 = none, 1 = sidebar hotkey, 2 = quick toggle hotkey
    int wlPage_ = 0;
    int blPage_ = 0;
    int presetSlot_ = 1; // 1..3

    std::wstring adapterName_ = L"Universal GPU";
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
