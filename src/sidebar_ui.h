#pragma once

#include "types.h"

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <functional>
#include <string>
#include <vector>

struct SidebarCallbacks {
    std::function<void()> onConfigChanged;
    std::function<void()> onOpenHub;
    std::function<void()> onExitRequested;
};

class SidebarUI {
public:
    SidebarUI() = default;
    ~SidebarUI();

    SidebarUI(const SidebarUI&) = delete;
    SidebarUI& operator=(const SidebarUI&) = delete;

    bool Initialize(HINSTANCE hInst, AppConfig* config, SidebarCallbacks callbacks, std::string& outError);
    void Shutdown();

    void SetAdapterName(const std::wstring& adapterName);
    void SetDisplayHdrInfo(const DisplayHdrInfo& info);
    void SetTelemetry(const FrameGenTelemetry& tel);
    void SetActiveAppInfo(const std::wstring& exeName, const std::wstring& displayTitle, bool isGame, const std::wstring& engineTag);
    void SetExcludeFromCapture(bool exclude);

    void Toggle();
    void SetOpen(bool open);
    bool IsOpen() const { return targetOpen_; }
    bool IsVisible() const { return hwnd_ && IsWindowVisible(hwnd_); }
    HWND Hwnd() const { return hwnd_; }

    void TickAnimation();
    void Invalidate();

private:
    enum ControlKind {
        CTRL_CLOSE_BTN,
        CTRL_PROFILE_0,
        CTRL_PROFILE_1,
        CTRL_PROFILE_2,
        CTRL_PROFILE_3,

        // Core & optional filters (16 total, stable in-place toggles + sliders + resets)
        CTRL_TOGGLE_VIBRANCE,
        CTRL_SLIDER_INTENSITY,
        CTRL_SLIDER_SATURATION,
        CTRL_RESET_VIBRANCE,

        CTRL_TOGGLE_SHARPEN,
        CTRL_SLIDER_SHARPEN,
        CTRL_RESET_SHARPEN,

        CTRL_TOGGLE_HDR,
        CTRL_SLIDER_HDR,
        CTRL_SLIDER_HDR_PEAK,
        CTRL_SLIDER_HDR_MIDGRAY,
        CTRL_RESET_HDR,

        CTRL_TOGGLE_DARK,
        CTRL_SLIDER_DARK,
        CTRL_RESET_DARK,

        CTRL_TOGGLE_AO,
        CTRL_SLIDER_AO_INTENSITY,
        CTRL_SLIDER_AO_RADIUS,
        CTRL_RESET_AO,

        CTRL_TOGGLE_CLARITY,
        CTRL_SLIDER_CLARITY,
        CTRL_RESET_CLARITY,

        CTRL_TOGGLE_BLOOM,
        CTRL_SLIDER_BLOOM_INT,
        CTRL_SLIDER_BLOOM_THRESH,
        CTRL_RESET_BLOOM,

        CTRL_TOGGLE_FILMIC,
        CTRL_FILMIC_MODE_0,
        CTRL_FILMIC_MODE_1,
        CTRL_FILMIC_MODE_2,
        CTRL_SLIDER_FILMIC_STR,
        CTRL_RESET_FILMIC,

        CTRL_TOGGLE_DEBAND,
        CTRL_SLIDER_DEBAND,
        CTRL_RESET_DEBAND,

        CTRL_TOGGLE_BC,
        CTRL_SLIDER_BRIGHTNESS,
        CTRL_SLIDER_CONTRAST,
        CTRL_RESET_BC,

        CTRL_TOGGLE_COLORTEMP,
        CTRL_SLIDER_TEMPERATURE,
        CTRL_SLIDER_TINT,
        CTRL_RESET_COLORTEMP,

        CTRL_TOGGLE_GAMMAEXP,
        CTRL_SLIDER_EXPOSURE,
        CTRL_SLIDER_GAMMA,
        CTRL_RESET_GAMMAEXP,

        CTRL_TOGGLE_NIGHT,
        CTRL_SLIDER_NIGHT,
        CTRL_RESET_NIGHT,

        CTRL_TOGGLE_BW,
        CTRL_SLIDER_BW,
        CTRL_RESET_BW,

        CTRL_TOGGLE_VIGNETTE,
        CTRL_SLIDER_VIGNETTE,
        CTRL_RESET_VIGNETTE,

        CTRL_TOGGLE_COLORBLIND,
        CTRL_CB_TYPE_0,
        CTRL_CB_TYPE_1,
        CTRL_CB_TYPE_2,
        CTRL_SLIDER_CBSTRENGTH,
        CTRL_RESET_COLORBLIND,

        CTRL_SB_SCALE_0,
        CTRL_SB_SCALE_1,
        CTRL_SB_SCALE_2,
        CTRL_SB_SCALE_3,
        CTRL_SB_SCALE_4,

        CTRL_SB_FG_0,
        CTRL_SB_FG_1,
        CTRL_SB_FG_2,
        CTRL_SB_FG_3,
        CTRL_SB_FG_4,

        CTRL_SB_TOGGLE_SPLIT,
        CTRL_SB_SPLIT_DEC,
        CTRL_SB_SPLIT_INC,
        CTRL_SB_SLIDER_SPLIT,

        CTRL_BTN_OPEN_HUB,
    };

    struct HitRect {
        ControlKind kind;
        D2D1_RECT_F rect;
        bool isSlider = false;
    };

    bool CreateRenderResources();
    void DiscardRenderResources();
    void Render();
    void UpdateWindowPosition();

    void HandleMouseDown(float dipX, float dipY);
    void HandleMouseMove(float dipX, float dipY);
    void HandleMouseUp();
    void HandleWheel(int delta);
    void UpdateSliderFromX(ControlKind kind, float dipX, const D2D1_RECT_F& trackRect);

    static LRESULT CALLBACK SidebarWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND hwnd_ = nullptr;
    AppConfig* config_ = nullptr;
    SidebarCallbacks callbacks_;

    float dpiScale_ = 1.0f;
    float sidebarDipW_ = 384.0f;
    float sidebarDipH_ = 1080.0f;
    int screenPhysW_ = 1920;
    int screenPhysH_ = 1080;

    bool targetOpen_ = false;
    float animProgress_ = 0.0f;
    ULONGLONG lastTickMs_ = 0;

    float scrollY_ = 0.0f;
    float maxScrollY_ = 0.0f;
    bool draggingSlider_ = false;
    ControlKind activeSlider_ = CTRL_SLIDER_INTENSITY;
    D2D1_RECT_F activeTrackRect_ = {};

    std::wstring adapterName_ = L"Universal GPU";
    DisplayHdrInfo displayHdr_{};
    FrameGenTelemetry telemetry_{};
    std::wstring activeExe_ = L"desktop";
    std::wstring activeAppTitle_ = L"Desktop";
    bool activeAppIsGame_ = false;
    std::wstring activeEngineTag_ = L"Desktop";

    std::vector<HitRect> hitRects_;

    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> rt_;

    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtTitle_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtSub_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtSection_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtBody_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtSmall_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtCenter_;

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
};
