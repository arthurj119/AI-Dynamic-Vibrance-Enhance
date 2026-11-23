#include "control_hub.h"
#include "profiles.h"
#include "shader_source.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <filesystem>

using Microsoft::WRL::ComPtr;

namespace {

constexpr D2D1_COLOR_F HubColHex(uint32_t rgb, float a = 1.0f) {
    return D2D1_COLOR_F{
        ((rgb >> 16) & 0xFF) / 255.0f,
        ((rgb >> 8) & 0xFF) / 255.0f,
        (rgb & 0xFF) / 255.0f,
        a
    };
}

constexpr DWORD kHubWindowStyle = WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU;

void ConfigureFramelessDarkWindow(HWND hwnd) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    DWORD cornerPref = 2; /* DWMWCP_ROUND */
    DwmSetWindowAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &cornerPref, sizeof(cornerPref));
    COLORREF borderCol = RGB(35, 39, 50);
    DwmSetWindowAttribute(hwnd, 34 /* DWMWA_BORDER_COLOR */, &borderCol, sizeof(borderCol));
}

void DrawBrandStarBadge(
    ID2D1Factory* factory,
    ID2D1HwndRenderTarget* rt,
    D2D1_RECT_F r,
    ID2D1SolidColorBrush* bgBr,
    ID2D1SolidColorBrush* fgBr
) {
    float w = r.right - r.left;
    float corner = w * 0.24f;
    rt->FillRoundedRectangle(D2D1::RoundedRect(r, corner, corner), bgBr);
    if (!factory) return;

    float cx = (r.left + r.right) * 0.5f;
    float cy = (r.top + r.bottom) * 0.5f;
    float R = w * 0.36f;
    float c = w * 0.075f;

    ComPtr<ID2D1PathGeometry> starGeo;
    if (SUCCEEDED(factory->CreatePathGeometry(&starGeo)) && starGeo) {
        ComPtr<ID2D1GeometrySink> sink;
        if (SUCCEEDED(starGeo->Open(&sink)) && sink) {
            sink->BeginFigure(D2D1::Point2F(cx, cy - R), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(D2D1::Point2F(cx + c, cy - c), D2D1::Point2F(cx + R, cy)));
            sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(D2D1::Point2F(cx + c, cy + c), D2D1::Point2F(cx, cy + R)));
            sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(D2D1::Point2F(cx - c, cy + c), D2D1::Point2F(cx - R, cy)));
            sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(D2D1::Point2F(cx - c, cy - c), D2D1::Point2F(cx, cy - R)));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            rt->FillGeometry(starGeo.Get(), fgBr);
        }
    }

    float cut = w * 0.095f;
    ComPtr<ID2D1PathGeometry> holeGeo;
    if (SUCCEEDED(factory->CreatePathGeometry(&holeGeo)) && holeGeo) {
        ComPtr<ID2D1GeometrySink> sink;
        if (SUCCEEDED(holeGeo->Open(&sink)) && sink) {
            sink->BeginFigure(D2D1::Point2F(cx, cy - cut), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(D2D1::Point2F(cx + cut, cy));
            sink->AddLine(D2D1::Point2F(cx, cy + cut));
            sink->AddLine(D2D1::Point2F(cx - cut, cy));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            rt->FillGeometry(holeGeo.Get(), bgBr);
        }
    }
}

void DrawTxt(ID2D1HwndRenderTarget* rt, const std::wstring& s, IDWriteTextFormat* fmt, D2D1_RECT_F r, ID2D1SolidColorBrush* br) {
    if (!rt || !fmt || !br) return;
    rt->DrawTextW(s.c_str(), static_cast<UINT32>(s.size()), fmt, r, br, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

bool IsModifierOnlyKey(UINT vk) {
    switch (vk) {
        case VK_SHIFT:
        case VK_CONTROL:
        case VK_MENU:
        case VK_LWIN:
        case VK_RWIN:
        case VK_LSHIFT:
        case VK_RSHIFT:
        case VK_LCONTROL:
        case VK_RCONTROL:
        case VK_LMENU:
        case VK_RMENU:
            return true;
        default:
            return false;
    }
}

bool IsStandaloneAllowedKey(UINT vk) {
    return (vk >= VK_F1 && vk <= VK_F12) ||
           vk == VK_INSERT || vk == VK_HOME || vk == VK_END ||
           vk == VK_PAUSE || vk == VK_SCROLL;
}

bool IsAllowedBindingKey(UINT vk) {
    if (vk >= 'A' && vk <= 'Z') return true;
    if (vk >= '0' && vk <= '9') return true;
    if (vk >= VK_F1 && vk <= VK_F12) return true;
    switch (vk) {
        case VK_INSERT:
        case VK_HOME:
        case VK_END:
        case VK_PRIOR:
        case VK_NEXT:
        case VK_OEM_3:
        case VK_PAUSE:
        case VK_SCROLL:
            return true;
        default:
            return false;
    }
}

std::wstring BuildProfileSummary(const FilterParams& fp) {
    std::vector<std::wstring> active;
    if (fp.enableDynamicVibrance) {
        active.push_back(L"AI Vibrance " + std::to_wstring(fp.intensity) + L"/" + std::to_wstring(fp.saturationBoost) + L"%");
    }
    if (fp.enableSharpen) active.push_back(L"Sharpen+");
    if (fp.enableDynamicHdr) active.push_back(L"Dynamic HDR");
    if (fp.enableDarkLighting) active.push_back(L"Dark Light");
    if (fp.enableAmbientOcclusion) active.push_back(L"AO");
    if (fp.enableClarity) active.push_back(L"Clarity");
    if (fp.enableBloom) active.push_back(L"Bloom");
    if (fp.enableFilmic) active.push_back(L"Filmic");
    if (fp.enableDeband) active.push_back(L"Deband");
    if (fp.enableBrightnessContrast) active.push_back(L"B/C");
    if (fp.enableColorTemp) active.push_back(L"Temp/Tint");
    if (fp.enableGammaExposure) active.push_back(L"Gamma/Exp");
    if (fp.enableNightMode) active.push_back(L"Night");
    if (fp.enableBlackAndWhite) active.push_back(L"B&W");
    if (fp.enableVignette) active.push_back(L"Vignette");
    if (fp.enableColorblind) active.push_back(L"Colorblind");
    if (active.empty()) return L"All 16 filter modules off";

    std::wstring line;
    for (size_t i = 0; i < active.size(); ++i) {
        if (i > 0) line += L"  \u2022  ";
        line += active[i];
    }
    return line;
}

bool DetectReShadeInFolder(const std::wstring& folder) {
    if (folder.empty()) return false;
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path dir(folder);
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return false;
    if (fs::exists(dir / L"reshade-shaders" / L"Shaders", ec)) return true;
    if (_wcsicmp(dir.filename().c_str(), L"Shaders") == 0 &&
        _wcsicmp(dir.parent_path().filename().c_str(), L"reshade-shaders") == 0) {
        return true;
    }
    if (fs::exists(dir / L"ReShade.ini", ec)) return true;
    return false;
}

} // namespace

ControlHub::~ControlHub() {
    Shutdown();
}

bool ControlHub::Initialize(HINSTANCE hInst, AppConfig* config, ControlHubCallbacks callbacks, std::string& outError) {
    config_ = config;
    callbacks_ = std::move(callbacks);

    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&d2dFactory_));
    if (FAILED(hr)) {
        outError = "D2D1CreateFactory failed for ControlHub";
        return false;
    }

    hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(dwriteFactory_.GetAddressOf())
    );
    if (FAILED(hr)) {
        outError = "DWriteCreateFactory failed for ControlHub";
        return false;
    }

    dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"en-us", &fmtBrand_);
    dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 16.5f, L"en-us", &fmtTitle_);
    dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13.5f, L"en-us", &fmtSection_);
    dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"en-us", &fmtBody_);
    dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"en-us", &fmtSmall_);
    dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.5f, L"en-us", &fmtCenter_);
    if (fmtCenter_) {
        fmtCenter_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        fmtCenter_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = HubWndProc;
    wc.hInstance = hInst;
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(101));
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(14, 16, 20));
    wc.lpszClassName = L"AIDynamicVibranceControlHubWnd";
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(
        0,
        wc.lpszClassName,
        L"AI Dynamic Vibrance Enhance",
        kHubWindowStyle,
        CW_USEDEFAULT, CW_USEDEFAULT, 920, 590,
        nullptr, nullptr, hInst, this
    );
    if (!hwnd_) {
        outError = "CreateWindowExW failed for ControlHub";
        return false;
    }

    ConfigureFramelessDarkWindow(hwnd_);

    UINT dpi = GetDpiForWindow(hwnd_);
    UpdateWindowSizeForDpi(dpi);

    CreateRenderResources();
    ShowWindow(hwnd_, SW_HIDE);
    return true;
}

void ControlHub::UpdateWindowSizeForDpi(UINT dpi) {
    if (!hwnd_) return;
    dpiScale_ = std::max(static_cast<float>(dpi) / 96.0f, 1.0f);
    RECT rc{ 0, 0, static_cast<LONG>(std::lround(kHubDipW * dpiScale_)), static_cast<LONG>(std::lround(kHubDipH * dpiScale_)) };
    AdjustWindowRectExForDpi(&rc, kHubWindowStyle, FALSE, 0, dpi);
    int winW = rc.right - rc.left;
    int winH = rc.bottom - rc.top;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = std::max(0, (screenW - winW) / 2);
    int y = std::max(0, (screenH - winH) / 2);
    SetWindowPos(hwnd_, nullptr, x, y, winW, winH, SWP_NOZORDER | SWP_NOACTIVATE);
    ResizeRenderTarget();
}

void ControlHub::Shutdown() {
    DiscardRenderResources();
    fmtCenter_.Reset();
    fmtSmall_.Reset();
    fmtBody_.Reset();
    fmtSection_.Reset();
    fmtTitle_.Reset();
    fmtBrand_.Reset();
    dwriteFactory_.Reset();
    d2dFactory_.Reset();
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void ControlHub::Show(int initialTab) {
    if (!hwnd_) return;
    if (initialTab >= 0 && initialTab <= 5) {
        currentTab_ = initialTab;
    }
    if (IsIconic(hwnd_)) {
        ShowWindow(hwnd_, SW_RESTORE);
    } else {
        ShowWindow(hwnd_, SW_SHOW);
    }
    SetForegroundWindow(hwnd_);
    Invalidate();
}

void ControlHub::Hide() {
    if (!hwnd_) return;
    SetRecordingHotkey(0);
    draggingSlider_ = false;
    if (GetCapture() == hwnd_) ReleaseCapture();
    ShowWindow(hwnd_, SW_HIDE);
}

void ControlHub::SetAdapterName(const std::wstring& adapterName) {
    adapterName_ = adapterName.empty() ? L"Universal GPU" : adapterName;
    Invalidate();
}

void ControlHub::SetDisplayHdrInfo(const DisplayHdrInfo& info) {
    if (info.hdrActive == displayHdr_.hdrActive &&
        std::abs(info.maxLuminanceNits - displayHdr_.maxLuminanceNits) < 1.0f &&
        std::abs(info.sdrWhiteNits - displayHdr_.sdrWhiteNits) < 1.0f) {
        return;
    }
    displayHdr_ = info;
    Invalidate();
}

void ControlHub::SetTelemetry(const FrameGenTelemetry& tel) {
    ULONGLONG nowMs = GetTickCount64();
    bool toastExpired = (footerToastUntilMs_ != 0 && nowMs >= footerToastUntilMs_);
    if (toastExpired) {
        footerToastUntilMs_ = 0;
        footerToast_.clear();
    }
    bool changed = (tel.fgRunning != telemetry_.fgRunning) ||
                   (tel.overlayActive != telemetry_.overlayActive) ||
                   (tel.usingWgc != telemetry_.usingWgc) ||
                   (tel.monitorHz != telemetry_.monitorHz) ||
                   (std::abs(tel.sourceFps - telemetry_.sourceFps) >= 1.0f) ||
                   (std::abs(tel.outputFps - telemetry_.outputFps) >= 1.0f);
    if (!changed && !toastExpired) return;
    telemetry_ = tel;
    if (hwnd_ && IsWindowVisible(hwnd_) && !IsIconic(hwnd_)) {
        Invalidate();
    }
}

void ControlHub::ShowFooterToast(const std::wstring& message, bool isError) {
    footerToast_ = message;
    footerToastError_ = isError;
    footerToastUntilMs_ = GetTickCount64() + 3500;
    Invalidate();
}

void ControlHub::SetActiveAppInfo(const std::wstring& exeName, const std::wstring& displayTitle, bool isGame, const std::wstring& engineTag) {
    std::wstring nextExe = exeName.empty() ? L"desktop" : exeName;
    std::wstring nextTitle = displayTitle.empty() ? L"Desktop" : displayTitle;
    std::wstring nextTag = engineTag.empty() ? (isGame ? L"Game" : L"Non-Game") : engineTag;
    if (activeExe_ == nextExe && activeAppTitle_ == nextTitle &&
        activeAppIsGame_ == isGame && activeEngineTag_ == nextTag) {
        return;
    }
    activeExe_ = std::move(nextExe);
    activeAppTitle_ = std::move(nextTitle);
    activeAppIsGame_ = isGame;
    activeEngineTag_ = std::move(nextTag);
    Invalidate();
}

void ControlHub::Invalidate() {
    if (hwnd_ && IsWindowVisible(hwnd_)) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

bool ControlHub::CreateRenderResources() {
    if (rt_ || !hwnd_ || !d2dFactory_) return rt_ != nullptr;

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    D2D1_SIZE_U sz = D2D1::SizeU(std::max<UINT>(rc.right - rc.left, 1), std::max<UINT>(rc.bottom - rc.top, 1));

    D2D1_RENDER_TARGET_PROPERTIES rtp = D2D1::RenderTargetProperties();
    rtp.dpiX = 96.0f * dpiScale_;
    rtp.dpiY = 96.0f * dpiScale_;

    HRESULT hr = d2dFactory_->CreateHwndRenderTarget(
        rtp,
        D2D1::HwndRenderTargetProperties(hwnd_, sz, D2D1_PRESENT_OPTIONS_IMMEDIATELY),
        &rt_
    );
    if (FAILED(hr)) return false;

    rt_->CreateSolidColorBrush(HubColHex(0x0B0D11), &brTopBar_);
    rt_->CreateSolidColorBrush(HubColHex(0x0E1014), &brBg_);
    rt_->CreateSolidColorBrush(HubColHex(0x151820), &brCard_);
    rt_->CreateSolidColorBrush(HubColHex(0x1C202B), &brCardHover_);
    rt_->CreateSolidColorBrush(HubColHex(0x232732), &brBorder_);
    rt_->CreateSolidColorBrush(HubColHex(0xED1C24), &brAccent_);
    rt_->CreateSolidColorBrush(HubColHex(0xED1C24, 0.16f), &brAccentSoft_);
    rt_->CreateSolidColorBrush(HubColHex(0x2A2F3D), &brTrackBg_);
    rt_->CreateSolidColorBrush(HubColHex(0xF3F5F8), &brText_);
    rt_->CreateSolidColorBrush(HubColHex(0x8B95A5), &brTextMuted_);
    rt_->CreateSolidColorBrush(HubColHex(0xF87171), &brDanger_);
    rt_->CreateSolidColorBrush(HubColHex(0x22C55E), &brSuccess_);
    return true;
}

void ControlHub::DiscardRenderResources() {
    brSuccess_.Reset();
    brDanger_.Reset();
    brTextMuted_.Reset();
    brText_.Reset();
    brTrackBg_.Reset();
    brAccentSoft_.Reset();
    brAccent_.Reset();
    brBorder_.Reset();
    brCardHover_.Reset();
    brCard_.Reset();
    brBg_.Reset();
    brTopBar_.Reset();
    rt_.Reset();
}

void ControlHub::ResizeRenderTarget() {
    if (!rt_ || !hwnd_) return;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    D2D1_SIZE_U sz = D2D1::SizeU(std::max<UINT>(rc.right - rc.left, 1), std::max<UINT>(rc.bottom - rc.top, 1));
    rt_->Resize(sz);
    rt_->SetDpi(96.0f * dpiScale_, 96.0f * dpiScale_);
}

void ControlHub::DrawTogglePill(HubControlKind k, float x, float ty, bool on, bool large) {
    float w = large ? 44.0f : 38.0f;
    float h = large ? 22.0f : 20.0f;
    float r = h * 0.5f;
    D2D1_RECT_F pr = D2D1::RectF(x, ty, x + w, ty + h);
    rt_->FillRoundedRectangle(D2D1::RoundedRect(pr, r, r), on ? brAccent_.Get() : brTrackBg_.Get());
    float kx = on ? (pr.right - r) : (pr.left + r);
    rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kx, (pr.top + pr.bottom) * 0.5f), r - 2.5f, r - 2.5f), brText_.Get());
    hitRects_.push_back({ k, pr, false, -1 });
}

void ControlHub::DrawFlatButton(HubControlKind k, const std::wstring& label, D2D1_RECT_F r, bool primary, bool danger, int payload) {
    rt_->FillRoundedRectangle(D2D1::RoundedRect(r, 6, 6), primary ? brAccent_.Get() : brCardHover_.Get());
    if (!primary) {
        rt_->DrawRoundedRectangle(D2D1::RoundedRect(r, 6, 6), danger ? brDanger_.Get() : brBorder_.Get(), 1.0f);
    }
    rt_->DrawTextW(label.c_str(), static_cast<UINT32>(label.size()), fmtCenter_.Get(), r,
                   danger ? brDanger_.Get() : brText_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    hitRects_.push_back({ k, r, false, payload });
}

void ControlHub::DrawHubSlider(HubControlKind k, const std::wstring& label, const std::wstring& valueText, int val, int minVal, int maxVal, float sx, float sy, float sw) {
    DrawTxt(rt_.Get(), label, fmtBody_.Get(), D2D1::RectF(sx, sy, sx + sw - 96.0f, sy + 20.0f), brText_.Get());
    DrawTxt(rt_.Get(), valueText, fmtSmall_.Get(), D2D1::RectF(sx + sw - 96.0f, sy + 1.0f, sx + sw, sy + 20.0f), brTextMuted_.Get());

    D2D1_RECT_F track = D2D1::RectF(sx, sy + 27.0f, sx + sw, sy + 31.0f);
    rt_->FillRoundedRectangle(D2D1::RoundedRect(track, 2, 2), brTrackBg_.Get());
    int span = std::max(maxVal - minVal, 1);
    float norm = static_cast<float>(std::clamp(val, minVal, maxVal) - minVal) / static_cast<float>(span);
    float fillX = track.left + (track.right - track.left) * norm;
    rt_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(track.left, track.top, fillX, track.bottom), 2, 2), brAccent_.Get());
    rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(fillX, sy + 29.0f), 6.5f, 6.5f), brText_.Get());
    rt_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(fillX, sy + 29.0f), 6.5f, 6.5f), brAccent_.Get(), 1.8f);
    hitRects_.push_back({ k, D2D1::RectF(track.left - 4.0f, sy + 18.0f, track.right + 4.0f, sy + 42.0f), true, -1 });
}

void ControlHub::DrawSettingIcon(int iconIdx, float x, float y) {
    // 20x20 crisp vector crimson icons
    ID2D1SolidColorBrush* br = brAccent_.Get();
    switch (iconIdx) {
        case 0: { // Game-Only Mode (gamepad)
            D2D1_ROUNDED_RECT body = D2D1::RoundedRect(D2D1::RectF(x + 1, y + 4, x + 19, y + 16), 5, 5);
            rt_->DrawRoundedRectangle(body, br, 1.5f);
            rt_->DrawLine(D2D1::Point2F(x + 6, y + 7.5f), D2D1::Point2F(x + 6, y + 12.5f), br, 1.5f);
            rt_->DrawLine(D2D1::Point2F(x + 3.5f, y + 10), D2D1::Point2F(x + 8.5f, y + 10), br, 1.5f);
            rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x + 13.5f, y + 8.5f), 1.2f, 1.2f), br);
            rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x + 15.5f, y + 11.2f), 1.2f, 1.2f), br);
            break;
        }
        case 1: { // Per-Game Profiles (folder / stack)
            rt_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x + 2, y + 5, x + 18, y + 16), 2.5f, 2.5f), br, 1.5f);
            rt_->DrawLine(D2D1::Point2F(x + 4, y + 3), D2D1::Point2F(x + 9, y + 3), br, 1.5f);
            rt_->DrawLine(D2D1::Point2F(x + 5, y + 10.5f), D2D1::Point2F(x + 15, y + 10.5f), br, 1.4f);
            break;
        }
        case 2: { // Capture Privacy (eye-off)
            rt_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x + 10, y + 10), 7.5f, 4.5f), br, 1.4f);
            rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x + 10, y + 10), 2.0f, 2.0f), br);
            rt_->DrawLine(D2D1::Point2F(x + 3, y + 16), D2D1::Point2F(x + 17, y + 4), br, 1.6f);
            break;
        }
        case 3: { // Split Preview (vertical split compare)
            rt_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x + 2, y + 3, x + 18, y + 17), 2.5f, 2.5f), br, 1.5f);
            rt_->DrawLine(D2D1::Point2F(x + 10, y + 1), D2D1::Point2F(x + 10, y + 19), br, 1.8f);
            break;
        }
        case 4: { // Start with Windows (4 panes)
            rt_->DrawRectangle(D2D1::RectF(x + 2, y + 3, x + 9, y + 9), br, 1.4f);
            rt_->DrawRectangle(D2D1::RectF(x + 11, y + 3, x + 18, y + 9), br, 1.4f);
            rt_->DrawRectangle(D2D1::RectF(x + 2, y + 11, x + 9, y + 17), br, 1.4f);
            rt_->DrawRectangle(D2D1::RectF(x + 11, y + 11, x + 18, y + 17), br, 1.4f);
            break;
        }
        default: { // Quick Filter Toggle (keyboard)
            rt_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x + 1.5f, y + 4.5f, x + 18.5f, y + 15.5f), 2.5f, 2.5f), br, 1.5f);
            rt_->DrawLine(D2D1::Point2F(x + 5, y + 8), D2D1::Point2F(x + 7, y + 8), br, 1.5f);
            rt_->DrawLine(D2D1::Point2F(x + 9, y + 8), D2D1::Point2F(x + 11, y + 8), br, 1.5f);
            rt_->DrawLine(D2D1::Point2F(x + 13, y + 8), D2D1::Point2F(x + 15, y + 8), br, 1.5f);
            rt_->DrawLine(D2D1::Point2F(x + 5.5f, y + 12), D2D1::Point2F(x + 14.5f, y + 12), br, 1.6f);
            break;
        }
    }
}

void ControlHub::DrawTopBarAndFooter() {
    const float W = kHubDipW;
    const float H = kHubDipH;

    // Top bar background & hairline divider
    rt_->FillRectangle(D2D1::RectF(0, 0, W, kTopBarH), brTopBar_.Get());
    rt_->DrawLine(D2D1::Point2F(0, kTopBarH), D2D1::Point2F(W, kTopBarH), brBorder_.Get(), 1.0f);

    // Brand star emblem + Full Title (always AI Dynamic Vibrance Enhance)
    D2D1_RECT_F badgeR = D2D1::RectF(18, 15, 42, 39);
    DrawBrandStarBadge(d2dFactory_.Get(), rt_.Get(), badgeR, brAccent_.Get(), brText_.Get());

    std::wstring brandTitle = L"AI Dynamic Vibrance Enhance";
    rt_->DrawTextW(brandTitle.c_str(), static_cast<UINT32>(brandTitle.size()), fmtBrand_.Get(),
                   D2D1::RectF(50, 17, 270, 39), brText_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

    // Centered 6 horizontal tabs: Overview | Scaling | Games | Presets | Legacy | About
    const wchar_t* tabLabels[6] = { L"Overview", L"Scaling", L"Games", L"Presets", L"Legacy", L"About" };
    const HubControlKind tabKinds[6] = {
        HUB_TAB_OVERVIEW, HUB_TAB_SCALING, HUB_TAB_GAMES, HUB_TAB_PRESETS, HUB_TAB_LEGACY, HUB_TAB_ABOUT
    };
    const float tabStartX = 276.0f;
    const float tabW = 69.0f;
    for (int i = 0; i < 6; ++i) {
        float tx = tabStartX + static_cast<float>(i) * tabW;
        D2D1_RECT_F tr = D2D1::RectF(tx, 6.0f, tx + tabW, kTopBarH);
        bool active = (currentTab_ == i);
        rt_->DrawTextW(tabLabels[i], static_cast<UINT32>(wcslen(tabLabels[i])), fmtCenter_.Get(), tr,
                       active ? brText_.Get() : brTextMuted_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        if (active) {
            rt_->FillRectangle(D2D1::RectF(tx + 10.0f, kTopBarH - 2.5f, tx + tabW - 10.0f, kTopBarH), brAccent_.Get());
        }
        hitRects_.push_back({ tabKinds[i], tr, false, -1 });
    }

    // Right: Segmented Grid | List Switcher
    D2D1_RECT_F segBox = D2D1::RectF(700, 13, 814, 41);
    rt_->FillRoundedRectangle(D2D1::RoundedRect(segBox, 6, 6), brCard_.Get());
    rt_->DrawRoundedRectangle(D2D1::RoundedRect(segBox, 6, 6), brBorder_.Get(), 1.0f);

    bool isList = config_->hubListView;
    D2D1_RECT_F gridBtn = D2D1::RectF(702, 15, 756, 39);
    if (!isList) rt_->FillRoundedRectangle(D2D1::RoundedRect(gridBtn, 4, 4), brAccent_.Get());
    // Mini 2x2 grid icon
    ID2D1SolidColorBrush* gIconBr = !isList ? brText_.Get() : brTextMuted_.Get();
    rt_->FillRectangle(D2D1::RectF(710, 22, 714, 26), gIconBr);
    rt_->FillRectangle(D2D1::RectF(716, 22, 720, 26), gIconBr);
    rt_->FillRectangle(D2D1::RectF(710, 28, 714, 32), gIconBr);
    rt_->FillRectangle(D2D1::RectF(716, 28, 720, 32), gIconBr);
    rt_->DrawTextW(L"Grid", 4, fmtSmall_.Get(), D2D1::RectF(724, 19, 755, 36), gIconBr, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    hitRects_.push_back({ HUB_VIEW_GRID, gridBtn, false, -1 });

    D2D1_RECT_F listBtn = D2D1::RectF(758, 15, 812, 39);
    if (isList) rt_->FillRoundedRectangle(D2D1::RoundedRect(listBtn, 4, 4), brAccent_.Get());
    ID2D1SolidColorBrush* lIconBr = isList ? brText_.Get() : brTextMuted_.Get();
    rt_->FillRectangle(D2D1::RectF(765, 22, 775, 24), lIconBr);
    rt_->FillRectangle(D2D1::RectF(765, 26, 775, 28), lIconBr);
    rt_->FillRectangle(D2D1::RectF(765, 30, 775, 32), lIconBr);
    rt_->DrawTextW(L"List", 4, fmtSmall_.Get(), D2D1::RectF(779, 19, 811, 36), lIconBr, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    hitRects_.push_back({ HUB_VIEW_LIST, listBtn, false, -1 });

    // Divider & flat window controls
    rt_->DrawLine(D2D1::Point2F(824, 16), D2D1::Point2F(824, 38), brBorder_.Get(), 1.0f);

    D2D1_RECT_F minR = D2D1::RectF(834, 14, 864, 40);
    rt_->DrawLine(D2D1::Point2F(844, 27), D2D1::Point2F(854, 27), brTextMuted_.Get(), 1.6f);
    hitRects_.push_back({ HUB_BTN_WIN_MIN, minR, false, -1 });

    D2D1_RECT_F closeR = D2D1::RectF(870, 14, 900, 40);
    rt_->DrawLine(D2D1::Point2F(880, 22), D2D1::Point2F(890, 32), brTextMuted_.Get(), 1.6f);
    rt_->DrawLine(D2D1::Point2F(890, 22), D2D1::Point2F(880, 32), brTextMuted_.Get(), 1.6f);
    hitRects_.push_back({ HUB_BTN_WIN_CLOSE, closeR, false, -1 });

    // Footer Bar
    float fy = H - kFooterH;
    rt_->FillRectangle(D2D1::RectF(0, fy, W, H), brTopBar_.Get());
    rt_->DrawLine(D2D1::Point2F(0, fy), D2D1::Point2F(W, fy), brBorder_.Get(), 1.0f);

    bool toastActive = (footerToastUntilMs_ != 0 && GetTickCount64() < footerToastUntilMs_ && !footerToast_.empty());
    ID2D1SolidColorBrush* dotBr = toastActive
        ? (footerToastError_ ? brDanger_.Get() : brSuccess_.Get())
        : (config_->overlayEnabled ? brSuccess_.Get() : brTextMuted_.Get());
    rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(28.0f, fy + 24.0f), 4.0f, 4.0f), dotBr);

    std::wstring hdrBadge = displayHdr_.hdrActive
        ? (L"HDR (" + std::to_wstring(static_cast<int>(std::lround(displayHdr_.maxLuminanceNits))) + L" nits)")
        : L"SDR";
    std::wstring footTxt;
    ID2D1SolidColorBrush* txtBr = brTextMuted_.Get();
    if (toastActive) {
        footTxt = footerToast_;
        txtBr = footerToastError_ ? brDanger_.Get() : brSuccess_.Get();
    } else {
        std::wstring stateLabel;
        if (!config_->overlayEnabled) {
            stateLabel = L"Paused";
        } else if (activeEngineTag_ == L"Ignored (User)") {
            stateLabel = L"Ignored: " + activeAppTitle_;
        } else if (activeAppIsGame_ && telemetry_.overlayActive) {
            stateLabel = L"Playing: " + activeAppTitle_ + (telemetry_.usingWgc ? L" (WGC OBS)" : L" (DXGI)");
        } else if (activeAppIsGame_) {
            stateLabel = L"Playing: " + activeAppTitle_ + L" (" + activeEngineTag_ + L")";
        } else if (telemetry_.overlayActive) {
            std::wstring targetName = activeAppTitle_.empty() ? L"Desktop" : activeAppTitle_;
            stateLabel = L"Active: " + targetName + (telemetry_.usingWgc ? L" (WGC OBS)" : L" (DXGI)");
        } else {
            stateLabel = L"Standby";
        }
        footTxt = adapterName_ + L" (" + hdrBadge + L")  \u2022  " + stateLabel;
        if (telemetry_.fgRunning && telemetry_.outputFps > 1.0f) {
            footTxt += L"  \u2022  " + std::to_wstring(static_cast<int>(std::lround(telemetry_.sourceFps))) +
                       L" -> " + std::to_wstring(static_cast<int>(std::lround(telemetry_.outputFps))) + L" FPS";
        }
        txtBr = telemetry_.overlayActive ? brAccent_.Get() : brTextMuted_.Get();
    }
    rt_->DrawTextW(footTxt.c_str(), static_cast<UINT32>(footTxt.size()), fmtBody_.Get(),
                   D2D1::RectF(40, fy + 15, 548, fy + 35),
                   txtBr, D2D1_DRAW_TEXT_OPTIONS_CLIP);

    std::wstring shotHkStr = FormatHotkeyString(config_->screenshotHotkeyMods, config_->screenshotHotkeyVk);
    std::wstring shotBtnLabel = (shotHkStr.size() <= 9) ? (L"Shot (" + shotHkStr + L")") : L"Screenshot";
    DrawFlatButton(HUB_BTN_SCREENSHOT, shotBtnLabel, D2D1::RectF(558, fy + 10, 686, fy + 38), false);
    DrawFlatButton(HUB_BTN_OPEN_DATA_FOLDER, L"Data Folder", D2D1::RectF(696, fy + 10, 802, fy + 38), false);
    DrawFlatButton(HUB_BTN_EXIT, L"Exit", D2D1::RectF(812, fy + 10, 896, fy + 38), false, true);
}

void ControlHub::RenderOverviewTab() {
    const float cx0 = 24.0f;
    const float cw = kHubDipW - 48.0f; // 872
    const std::wstring sidebarHkStr = FormatHotkeyString(config_->sidebarHotkeyMods, config_->sidebarHotkeyVk);
    const std::wstring quickHkStr = FormatHotkeyString(config_->quickToggleMods, config_->quickToggleVk);
    const std::wstring shotHkStr = FormatHotkeyString(config_->screenshotHotkeyMods, config_->screenshotHotkeyVk);

    // Hero Engine Bar (y = 70..130, h = 60)
    D2D1_RECT_F heroR = D2D1::RectF(cx0, 70.0f, cx0 + cw, 130.0f);
    rt_->FillRoundedRectangle(D2D1::RoundedRect(heroR, 10, 10), brCard_.Get());
    rt_->DrawRoundedRectangle(D2D1::RoundedRect(heroR, 10, 10), brBorder_.Get(), 1.0f);

    DrawTogglePill(HUB_TOGGLE_OVERLAY, cx0 + 18, 89.0f, config_->overlayEnabled, true);
    rt_->DrawTextW(L"In-Game Overlay", 15, fmtSection_.Get(),
                   D2D1::RectF(cx0 + 74, 88, cx0 + 210, 112), brText_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

    D2D1_RECT_F badge = D2D1::RectF(cx0 + 204, 90, cx0 + 266, 110);
    rt_->FillRoundedRectangle(D2D1::RoundedRect(badge, 4, 4),
                              config_->overlayEnabled ? brAccent_.Get() : brTrackBg_.Get());
    std::wstring badgeTxt = config_->overlayEnabled ? L"ACTIVE" : L"PAUSED";
    rt_->DrawTextW(badgeTxt.c_str(), static_cast<UINT32>(badgeTxt.size()), fmtCenter_.Get(),
                   badge, brText_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

    // Clickable Screenshot Hotkey Chip
    bool recShot = (recordingHotkey_ == 3);
    D2D1_RECT_F shotChip = D2D1::RectF(cx0 + cw - 468, 84, cx0 + cw - 314, 116);
    rt_->FillRoundedRectangle(D2D1::RoundedRect(shotChip, 6, 6), recShot ? brAccentSoft_.Get() : brBg_.Get());
    rt_->DrawRoundedRectangle(D2D1::RoundedRect(shotChip, 6, 6), recShot ? brAccent_.Get() : brBorder_.Get(), recShot ? 1.5f : 1.0f);
    std::wstring shotChipTxt = recShot ? L"Press keys... (Esc)" : (L"Shot: " + shotHkStr);
    rt_->DrawTextW(shotChipTxt.c_str(), static_cast<UINT32>(shotChipTxt.size()), fmtCenter_.Get(),
                   shotChip, brText_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    hitRects_.push_back({ HUB_BTN_HOTKEY_SCREENSHOT, shotChip, false, -1 });

    // Clickable Sidebar Hotkey Chip
    bool recSide = (recordingHotkey_ == 1);
    D2D1_RECT_F sideChip = D2D1::RectF(cx0 + cw - 304, 84, cx0 + cw - 134, 116);
    rt_->FillRoundedRectangle(D2D1::RoundedRect(sideChip, 6, 6), recSide ? brAccentSoft_.Get() : brBg_.Get());
    rt_->DrawRoundedRectangle(D2D1::RoundedRect(sideChip, 6, 6), recSide ? brAccent_.Get() : brBorder_.Get(), recSide ? 1.5f : 1.0f);
    std::wstring sideChipTxt = recSide ? L"Press keys... (Esc)" : (L"Sidebar: " + sidebarHkStr);
    rt_->DrawTextW(sideChipTxt.c_str(), static_cast<UINT32>(sideChipTxt.size()), fmtCenter_.Get(),
                   sideChip, brText_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    hitRects_.push_back({ HUB_BTN_HOTKEY_SIDEBAR, sideChip, false, -1 });

    DrawFlatButton(HUB_BTN_OPEN_SIDEBAR, L"Open Filters",
                   D2D1::RectF(cx0 + cw - 122, 84, cx0 + cw - 14, 116), true);

    struct SettingDef {
        int iconIdx;
        const wchar_t* title;
        const wchar_t* desc;
        HubControlKind toggleKind;
        bool val;
    };
    const SettingDef defs[6] = {
        { 0, L"Game-Only Mode",      L"Activate filters only in detected games", HUB_TOGGLE_ONLYGAMES,   config_->onlyInGames },
        { 1, L"Per-Game Profiles",   L"Remember last profile for each title",    HUB_TOGGLE_AUTOSWITCH,  config_->autoSwitchPerApp },
        { 2, L"Capture Privacy",     L"Hide sidebar in screenshots & OBS",       HUB_TOGGLE_HIDESIDEBAR, config_->hideSidebarFromCapture },
        { 3, L"Split Preview",       L"Compare original vs enhanced live",       HUB_TOGGLE_SPLIT,       config_->splitPreview },
        { 4, L"Start with Windows",  L"Launch minimized in system tray",         HUB_TOGGLE_STARTUP,     config_->launchAtStartup },
        { 5, L"Quick Filter Toggle", L"Switch active filter On/Off in-game",     HUB_TOGGLE_QUICKHOTKEY, config_->quickToggleEnabled },
    };

    if (!config_->hubListView) {
        // Concept A: Symmetrical 3 x 2 Bento Grid
        const float gap = 14.0f;
        const float tileW = (cw - gap * 2.0f) / 3.0f;
        const float tileH = 166.0f;
        const float startY = 146.0f;

        for (int i = 0; i < 6; ++i) {
            int col = i % 3;
            int row = i / 3;
            float tx = cx0 + static_cast<float>(col) * (tileW + gap);
            float ty = startY + static_cast<float>(row) * (tileH + gap);

            D2D1_RECT_F tr = D2D1::RectF(tx, ty, tx + tileW, ty + tileH);
            rt_->FillRoundedRectangle(D2D1::RoundedRect(tr, 10, 10), brCard_.Get());
            rt_->DrawRoundedRectangle(D2D1::RoundedRect(tr, 10, 10), brBorder_.Get(), 1.0f);

            // Top row inside card: Icon Box left + Toggle Pill right
            D2D1_RECT_F iconBox = D2D1::RectF(tx + 16, ty + 16, tx + 48, ty + 48);
            rt_->FillRoundedRectangle(D2D1::RoundedRect(iconBox, 7, 7), brCardHover_.Get());
            DrawSettingIcon(defs[i].iconIdx, tx + 22, ty + 22);
            DrawTogglePill(defs[i].toggleKind, tx + tileW - 54, ty + 22, defs[i].val, false);

            rt_->DrawTextW(defs[i].title, static_cast<UINT32>(wcslen(defs[i].title)), fmtSection_.Get(),
                           D2D1::RectF(tx + 16, ty + 62, tx + tileW - 16, ty + 84), brText_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            rt_->DrawTextW(defs[i].desc, static_cast<UINT32>(wcslen(defs[i].desc)), fmtBody_.Get(),
                           D2D1::RectF(tx + 16, ty + 86, tx + tileW - 16, ty + 122), brTextMuted_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

            if (i == 3 && config_->splitPreview) {
                int splitPct = static_cast<int>(std::lround(config_->splitPosition * 100.0f));
                std::wstring pctStr = std::to_wstring(splitPct) + L"%";
                D2D1_RECT_F track = D2D1::RectF(tx + 16, ty + 136, tx + tileW - 58, ty + 140);
                rt_->FillRoundedRectangle(D2D1::RoundedRect(track, 2, 2), brTrackBg_.Get());
                float norm = std::clamp(config_->splitPosition, 0.05f, 0.95f);
                float fillX = track.left + (track.right - track.left) * norm;
                rt_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(track.left, track.top, fillX, track.bottom), 2, 2), brAccent_.Get());
                rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(fillX, ty + 138), 6.0f, 6.0f), brText_.Get());
                rt_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(fillX, ty + 138), 6.0f, 6.0f), brAccent_.Get(), 1.8f);
                hitRects_.push_back({ HUB_SLIDER_SPLITPOS, D2D1::RectF(track.left - 4, ty + 126, track.right + 4, ty + 150), true, -1 });

                rt_->DrawTextW(pctStr.c_str(), static_cast<UINT32>(pctStr.size()), fmtSmall_.Get(),
                               D2D1::RectF(tx + tileW - 50, ty + 129, tx + tileW - 14, ty + 148), brText_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            } else if (i == 5) {
                bool recQuick = (recordingHotkey_ == 2);
                D2D1_RECT_F qChip = D2D1::RectF(tx + 16, ty + 124, tx + tileW - 82, ty + 150);
                rt_->FillRoundedRectangle(D2D1::RoundedRect(qChip, 5, 5), recQuick ? brAccentSoft_.Get() : brBg_.Get());
                rt_->DrawRoundedRectangle(D2D1::RoundedRect(qChip, 5, 5), recQuick ? brAccent_.Get() : brBorder_.Get(), recQuick ? 1.5f : 1.0f);
                std::wstring qDisp = recQuick ? L"Press keys... (Esc)" : quickHkStr;
                rt_->DrawTextW(qDisp.c_str(), static_cast<UINT32>(qDisp.size()), fmtCenter_.Get(),
                               qChip, (recQuick || config_->quickToggleEnabled) ? brText_.Get() : brTextMuted_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                hitRects_.push_back({ HUB_BTN_HOTKEY_QUICK, qChip, false, -1 });

                DrawFlatButton(HUB_BTN_HOTKEY_RESET, L"Reset",
                               D2D1::RectF(tx + tileW - 74, ty + 124, tx + tileW - 16, ty + 150), false);
            }
        }
    } else {
        // Concept C: Minimal Single-Panel List View
        const float listY = 146.0f;
        const float rowH = 54.0f;
        const float splitExtra = config_->splitPreview ? 30.0f : 0.0f;
        const float panelH = rowH * 6.0f + splitExtra;

        D2D1_RECT_F panelR = D2D1::RectF(cx0, listY, cx0 + cw, listY + panelH);
        rt_->FillRoundedRectangle(D2D1::RoundedRect(panelR, 10, 10), brCard_.Get());
        rt_->DrawRoundedRectangle(D2D1::RoundedRect(panelR, 10, 10), brBorder_.Get(), 1.0f);

        float ry = listY;
        for (int i = 0; i < 6; ++i) {
            float curH = rowH + ((i == 3 && config_->splitPreview) ? splitExtra : 0.0f);
            if (i > 0) {
                rt_->DrawLine(D2D1::Point2F(cx0 + 16, ry), D2D1::Point2F(cx0 + cw - 16, ry), brBorder_.Get(), 1.0f);
            }

            DrawSettingIcon(defs[i].iconIdx, cx0 + 20, ry + 17);
            rt_->DrawTextW(defs[i].title, static_cast<UINT32>(wcslen(defs[i].title)), fmtSection_.Get(),
                           D2D1::RectF(cx0 + 54, ry + 9, cx0 + 540, ry + 28), brText_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            rt_->DrawTextW(defs[i].desc, static_cast<UINT32>(wcslen(defs[i].desc)), fmtSmall_.Get(),
                           D2D1::RectF(cx0 + 54, ry + 29, cx0 + 560, ry + 47), brTextMuted_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

            if (i < 5) {
                std::wstring st = defs[i].val ? L"ON" : L"OFF";
                rt_->DrawTextW(st.c_str(), static_cast<UINT32>(st.size()), fmtSmall_.Get(),
                               D2D1::RectF(cx0 + cw - 92, ry + 19, cx0 + cw - 62, ry + 36),
                               defs[i].val ? brText_.Get() : brTextMuted_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                DrawTogglePill(defs[i].toggleKind, cx0 + cw - 56, ry + 17, defs[i].val, false);
            } else {
                DrawTogglePill(defs[i].toggleKind, cx0 + cw - 280, ry + 17, defs[i].val, false);
                bool recQuick = (recordingHotkey_ == 2);
                D2D1_RECT_F qChip = D2D1::RectF(cx0 + cw - 228, ry + 14, cx0 + cw - 84, ry + 40);
                rt_->FillRoundedRectangle(D2D1::RoundedRect(qChip, 5, 5), recQuick ? brAccentSoft_.Get() : brBg_.Get());
                rt_->DrawRoundedRectangle(D2D1::RoundedRect(qChip, 5, 5), recQuick ? brAccent_.Get() : brBorder_.Get(), recQuick ? 1.5f : 1.0f);
                std::wstring qDisp = recQuick ? L"Press keys... (Esc)" : quickHkStr;
                rt_->DrawTextW(qDisp.c_str(), static_cast<UINT32>(qDisp.size()), fmtCenter_.Get(),
                               qChip, (recQuick || config_->quickToggleEnabled) ? brText_.Get() : brTextMuted_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                hitRects_.push_back({ HUB_BTN_HOTKEY_QUICK, qChip, false, -1 });

                DrawFlatButton(HUB_BTN_HOTKEY_RESET, L"Reset",
                               D2D1::RectF(cx0 + cw - 76, ry + 14, cx0 + cw - 18, ry + 40), false);
            }

            if (i == 3 && config_->splitPreview) {
                int splitPct = static_cast<int>(std::lround(config_->splitPosition * 100.0f));
                std::wstring pctStr = std::to_wstring(splitPct) + L"%";
                D2D1_RECT_F track = D2D1::RectF(cx0 + 54, ry + 61, cx0 + cw - 68, ry + 65);
                rt_->FillRoundedRectangle(D2D1::RoundedRect(track, 2, 2), brTrackBg_.Get());
                float norm = std::clamp(config_->splitPosition, 0.05f, 0.95f);
                float fillX = track.left + (track.right - track.left) * norm;
                rt_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(track.left, track.top, fillX, track.bottom), 2, 2), brAccent_.Get());
                rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(fillX, ry + 63), 6.5f, 6.5f), brText_.Get());
                rt_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(fillX, ry + 63), 6.5f, 6.5f), brAccent_.Get(), 1.8f);
                hitRects_.push_back({ HUB_SLIDER_SPLITPOS, D2D1::RectF(track.left - 4, ry + 50, track.right + 4, ry + 76), true, -1 });

                rt_->DrawTextW(pctStr.c_str(), static_cast<UINT32>(pctStr.size()), fmtSmall_.Get(),
                               D2D1::RectF(cx0 + cw - 56, ry + 54, cx0 + cw - 16, ry + 72), brText_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
            ry += curH;
        }
    }

    rt_->DrawTextW(hotkeyStatus_.c_str(), static_cast<UINT32>(hotkeyStatus_.size()), fmtSmall_.Get(),
                   D2D1::RectF(cx0, 506, cx0 + cw, 526),
                   hotkeyStatusError_ ? brDanger_.Get() : brTextMuted_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void ControlHub::RenderScalingTab() {
    const float cx0 = 24.0f;
    const float cw = kHubDipW - 48.0f;

    DrawTxt(rt_.Get(), L"Spatial Scaling & Frame Generation", fmtTitle_.Get(),
            D2D1::RectF(cx0, 70, cx0 + cw, 94), brText_.Get());
    DrawTxt(rt_.Get(), L"Upscale windowed games with sharp edge reconstruction and synthesize smoother motion across all GPUs.",
            fmtBody_.Get(), D2D1::RectF(cx0, 96, cx0 + cw, 114), brTextMuted_.Get());

    const wchar_t* scaleLabels[5] = { L"Off", L"FSR 1.0", L"NIS", L"Integer", L"Anime4K" };
    const HubControlKind scaleKinds[5] = {
        HUB_SCALE_OFF, HUB_SCALE_FSR1, HUB_SCALE_NIS, HUB_SCALE_INTEGER, HUB_SCALE_ANIME4K
    };
    const wchar_t* fgLabels[5] = { L"Off", L"2x", L"3x", L"4x", L"Adaptive" };
    const HubControlKind fgKinds[5] = {
        HUB_FG_OFF, HUB_FG_2X, HUB_FG_3X, HUB_FG_4X, HUB_FG_ADAPTIVE
    };

    const wchar_t* scaleDescText = L"Bypassed (0% GPU cost). Native game resolution passes directly to color filters.";
    switch (config_->scaleMode) {
        case SCALE_FSR1:    scaleDescText = L"AMD FSR 1.0 (12-tap EASU edge reconstruction + RCAS crisp sharpening). Best for 3D games."; break;
        case SCALE_NIS:     scaleDescText = L"NVIDIA NIS style Catmull-Rom + directional adaptive unsharp mask across all GPUs."; break;
        case SCALE_INTEGER: scaleDescText = L"Pixel-perfect integer scaling with crisp pixels and centered aspect framing. Ideal for retro & 2D."; break;
        case SCALE_ANIME4K: scaleDescText = L"Anime4K Sobel dark-line thinning & cel-shaded detail restoration for anime/stylized titles."; break;
        default: break;
    }

    auto drawSelectorRow = [&](float sx, float sy, float sw, const wchar_t* const labels[5], const HubControlKind kinds[5], int activeVal) {
        float gap = 6.0f;
        float bw = (sw - gap * 4.0f) / 5.0f;
        for (int i = 0; i < 5; ++i) {
            float bx = sx + static_cast<float>(i) * (bw + gap);
            D2D1_RECT_F br = D2D1::RectF(bx, sy, bx + bw, sy + 32.0f);
            bool sel = (activeVal == i);
            rt_->FillRoundedRectangle(D2D1::RoundedRect(br, 6, 6), sel ? brAccent_.Get() : brBg_.Get());
            if (!sel) rt_->DrawRoundedRectangle(D2D1::RoundedRect(br, 6, 6), brBorder_.Get(), 1.0f);
            DrawTxt(rt_.Get(), labels[i], fmtCenter_.Get(), br, sel ? brText_.Get() : brTextMuted_.Get());
            hitRects_.push_back({ kinds[i], br, false, -1 });
        }
    };

    std::wstring targetDesc = std::to_wstring(config_->fgAdaptiveTargetFps) + L" FPS / " + std::to_wstring(telemetry_.monitorHz) + L" Hz";

    if (!config_->hubListView) {
        const float colW = (cw - 16.0f) * 0.5f;
        const float cardY = 124.0f;
        const float cardH = 338.0f;

        // Left Card: Spatial Upscaling
        D2D1_RECT_F lCard = D2D1::RectF(cx0, cardY, cx0 + colW, cardY + cardH);
        rt_->FillRoundedRectangle(D2D1::RoundedRect(lCard, 10, 10), brCard_.Get());
        rt_->DrawRoundedRectangle(D2D1::RoundedRect(lCard, 10, 10), (config_->scaleMode != SCALE_OFF) ? brAccent_.Get() : brBorder_.Get(), 1.0f);

        float lx = cx0 + 18.0f;
        float lw = colW - 36.0f;
        DrawTxt(rt_.Get(), L"Spatial Upscaling (Windowed -> Fullscreen)", fmtSection_.Get(),
                D2D1::RectF(lx, 138, lx + lw, 160), brText_.Get());
        rt_->DrawLine(D2D1::Point2F(lx, 166), D2D1::Point2F(lx + lw, 166), brBorder_.Get(), 1.0f);

        drawSelectorRow(lx, 176.0f, lw, scaleLabels, scaleKinds, config_->scaleMode);

        DrawHubSlider(HUB_SLIDER_SCALE_SHARP, L"Upscale Sharpness", std::to_wstring(config_->scaleSharpness) + L"%",
                      config_->scaleSharpness, 0, 100, lx, 224.0f, lw);

        DrawTxt(rt_.Get(), L"Fullscreen Stretch (Windowed Games)", fmtBody_.Get(),
                D2D1::RectF(lx, 286, lx + lw - 46, 306), brText_.Get());
        DrawTxt(rt_.Get(), L"Scales lower-res windowed games cleanly to full screen", fmtSmall_.Get(),
                D2D1::RectF(lx, 306, lx + lw - 46, 324), brTextMuted_.Get());
        DrawTogglePill(HUB_TOGGLE_SCALE_STRETCH, lx + lw - 38, 294.0f, config_->scaleFullscreenStretch, false);

        rt_->DrawLine(D2D1::Point2F(lx, 334.0f), D2D1::Point2F(lx + lw, 334.0f), brBorder_.Get(), 1.0f);
        DrawTxt(rt_.Get(), L"Capture & Display Sync", fmtSmall_.Get(),
                D2D1::RectF(lx, 342.0f, lx + lw, 360.0f), brText_.Get());

        float capGap = 8.0f;
        float capBw = (lw - capGap) * 0.5f;
        D2D1_RECT_F dxgiBtn = D2D1::RectF(lx, 364.0f, lx + capBw, 394.0f);
        D2D1_RECT_F wgcBtn = D2D1::RectF(lx + capBw + capGap, 364.0f, lx + lw, 394.0f);
        bool dxgiSel = (config_->captureApi == CAPTURE_DXGI);
        bool wgcSel = (config_->captureApi == CAPTURE_WGC);
        rt_->FillRoundedRectangle(D2D1::RoundedRect(dxgiBtn, 6, 6), dxgiSel ? brAccent_.Get() : brBg_.Get());
        if (!dxgiSel) rt_->DrawRoundedRectangle(D2D1::RoundedRect(dxgiBtn, 6, 6), brBorder_.Get(), 1.0f);
        DrawTxt(rt_.Get(), L"DXGI Smooth (Default)", fmtCenter_.Get(), dxgiBtn, dxgiSel ? brText_.Get() : brTextMuted_.Get());
        hitRects_.push_back({ HUB_CAPTURE_DXGI, dxgiBtn, false, -1 });

        rt_->FillRoundedRectangle(D2D1::RoundedRect(wgcBtn, 6, 6), wgcSel ? brAccent_.Get() : brBg_.Get());
        if (!wgcSel) rt_->DrawRoundedRectangle(D2D1::RoundedRect(wgcBtn, 6, 6), brBorder_.Get(), 1.0f);
        DrawTxt(rt_.Get(), L"WGC (OBS Capture)", fmtCenter_.Get(), wgcBtn, wgcSel ? brText_.Get() : brTextMuted_.Get());
        hitRects_.push_back({ HUB_CAPTURE_WGC, wgcBtn, false, -1 });

        DrawTxt(rt_.Get(), L"VRR / Allow Tearing (FreeSync)", fmtBody_.Get(),
                D2D1::RectF(lx, 404.0f, lx + lw - 46, 424.0f), brText_.Get());
        DrawTxt(rt_.Get(), L"Bypasses DWM VSync lock for smooth FreeSync / G-Sync pacing", fmtSmall_.Get(),
                D2D1::RectF(lx, 424.0f, lx + lw - 46, 444.0f), brTextMuted_.Get());
        DrawTogglePill(HUB_TOGGLE_ALLOW_TEARING, lx + lw - 38, 412.0f, config_->allowTearing, false);

        // Right Card: Optical Flow Frame Generation
        float rx0 = cx0 + colW + 16.0f;
        D2D1_RECT_F rCard = D2D1::RectF(rx0, cardY, rx0 + colW, cardY + cardH);
        rt_->FillRoundedRectangle(D2D1::RoundedRect(rCard, 10, 10), brCard_.Get());
        rt_->DrawRoundedRectangle(D2D1::RoundedRect(rCard, 10, 10), (config_->fgMode != FG_OFF) ? brAccent_.Get() : brBorder_.Get(), 1.0f);

        float rx = rx0 + 18.0f;
        DrawTxt(rt_.Get(), L"Optical Flow Frame Generation (All GPUs)", fmtSection_.Get(),
                D2D1::RectF(rx, 138, rx + lw, 160), brText_.Get());
        rt_->DrawLine(D2D1::Point2F(rx, 166), D2D1::Point2F(rx + lw, 166), brBorder_.Get(), 1.0f);

        drawSelectorRow(rx, 176.0f, lw, fgLabels, fgKinds, config_->fgMode);

        DrawHubSlider(HUB_SLIDER_FG_FLOWSCALE, L"Optical Flow Scale", std::to_wstring(config_->fgFlowScale) + L"%",
                      config_->fgFlowScale, 25, 100, rx, 224.0f, lw);

        DrawHubSlider(HUB_SLIDER_FG_TARGETFPS, L"Adaptive Target FPS", targetDesc,
                      config_->fgAdaptiveTargetFps, 60, 240, rx, 276.0f, lw);

        DrawTxt(rt_.Get(), L"AFMF Fast-Motion Fallback", fmtBody_.Get(),
                D2D1::RectF(rx, 338, rx + lw - 46, 358), brText_.Get());
        DrawTxt(rt_.Get(), L"Reduces ghosting during whip camera pans & scene cuts", fmtSmall_.Get(),
                D2D1::RectF(rx, 358, rx + lw - 46, 376), brTextMuted_.Get());
        DrawTogglePill(HUB_TOGGLE_FG_FALLBACK, rx + lw - 38, 344.0f, config_->fgFastMotionFallback, false);

        DrawTxt(rt_.Get(), L"Static HUD Protection", fmtBody_.Get(),
                D2D1::RectF(rx, 392, rx + lw - 46, 412), brText_.Get());
        DrawTxt(rt_.Get(), L"Preserves sharp crosshairs & stationary game UI overlays", fmtSmall_.Get(),
                D2D1::RectF(rx, 412, rx + lw - 46, 430), brTextMuted_.Get());
        DrawTogglePill(HUB_TOGGLE_FG_HUDPROTECT, rx + lw - 38, 398.0f, config_->fgStaticHudProtect, false);
    } else {
        // List View: Top Horizontal Spatial Upscaling Panel + Bottom Optical Flow Panel
        D2D1_RECT_F topPanel = D2D1::RectF(cx0, 124.0f, cx0 + cw, 282.0f);
        rt_->FillRoundedRectangle(D2D1::RoundedRect(topPanel, 10, 10), brCard_.Get());
        rt_->DrawRoundedRectangle(D2D1::RoundedRect(topPanel, 10, 10), brBorder_.Get(), 1.0f);

        float mx = cx0 + 18.0f;
        float mw = cw - 36.0f;
        DrawTxt(rt_.Get(), L"Spatial Upscaling", fmtSection_.Get(), D2D1::RectF(mx, 138, mx + 220, 162), brText_.Get());
        drawSelectorRow(mx + 230.0f, 133.0f, mw - 230.0f, scaleLabels, scaleKinds, config_->scaleMode);

        float halfW = (mw - 24.0f) * 0.5f;
        DrawHubSlider(HUB_SLIDER_SCALE_SHARP, L"Upscale Sharpness", std::to_wstring(config_->scaleSharpness) + L"%",
                      config_->scaleSharpness, 0, 100, mx, 184.0f, halfW);
        DrawTxt(rt_.Get(), L"Fullscreen Stretch (Windowed)", fmtBody_.Get(),
                D2D1::RectF(mx + halfW + 24.0f, 184.0f, mx + mw - 46.0f, 206.0f), brText_.Get());
        DrawTogglePill(HUB_TOGGLE_SCALE_STRETCH, mx + mw - 38.0f, 186.0f, config_->scaleFullscreenStretch, false);

        DrawTxt(rt_.Get(), L"Capture API:", fmtBody_.Get(), D2D1::RectF(mx, 243.0f, mx + 92.0f, 265.0f), brText_.Get());
        bool dxgiSel = (config_->captureApi == CAPTURE_DXGI);
        bool wgcSel = (config_->captureApi == CAPTURE_WGC);
        D2D1_RECT_F dxgiBtn = D2D1::RectF(mx + 98.0f, 240.0f, mx + 248.0f, 268.0f);
        D2D1_RECT_F wgcBtn = D2D1::RectF(mx + 256.0f, 240.0f, mx + halfW, 268.0f);
        rt_->FillRoundedRectangle(D2D1::RoundedRect(dxgiBtn, 6, 6), dxgiSel ? brAccent_.Get() : brBg_.Get());
        if (!dxgiSel) rt_->DrawRoundedRectangle(D2D1::RoundedRect(dxgiBtn, 6, 6), brBorder_.Get(), 1.0f);
        DrawTxt(rt_.Get(), L"DXGI Smooth", fmtCenter_.Get(), dxgiBtn, dxgiSel ? brText_.Get() : brTextMuted_.Get());
        hitRects_.push_back({ HUB_CAPTURE_DXGI, dxgiBtn, false, -1 });

        rt_->FillRoundedRectangle(D2D1::RoundedRect(wgcBtn, 6, 6), wgcSel ? brAccent_.Get() : brBg_.Get());
        if (!wgcSel) rt_->DrawRoundedRectangle(D2D1::RoundedRect(wgcBtn, 6, 6), brBorder_.Get(), 1.0f);
        DrawTxt(rt_.Get(), L"WGC OBS", fmtCenter_.Get(), wgcBtn, wgcSel ? brText_.Get() : brTextMuted_.Get());
        hitRects_.push_back({ HUB_CAPTURE_WGC, wgcBtn, false, -1 });

        DrawTxt(rt_.Get(), L"VRR / Allow Tearing (FreeSync)", fmtBody_.Get(),
                D2D1::RectF(mx + halfW + 24.0f, 243.0f, mx + mw - 46.0f, 265.0f), brText_.Get());
        DrawTogglePill(HUB_TOGGLE_ALLOW_TEARING, mx + mw - 38.0f, 244.0f, config_->allowTearing, false);

        D2D1_RECT_F botPanel = D2D1::RectF(cx0, 294.0f, cx0 + cw, 462.0f);
        rt_->FillRoundedRectangle(D2D1::RoundedRect(botPanel, 10, 10), brCard_.Get());
        rt_->DrawRoundedRectangle(D2D1::RoundedRect(botPanel, 10, 10), brBorder_.Get(), 1.0f);

        DrawTxt(rt_.Get(), L"Optical Flow Frame Generation", fmtSection_.Get(), D2D1::RectF(mx, 308, mx + 240, 332), brText_.Get());
        drawSelectorRow(mx + 250.0f, 303.0f, mw - 250.0f, fgLabels, fgKinds, config_->fgMode);

        DrawHubSlider(HUB_SLIDER_FG_FLOWSCALE, L"Optical Flow Scale", std::to_wstring(config_->fgFlowScale) + L"%",
                      config_->fgFlowScale, 25, 100, mx, 352.0f, halfW);
        DrawHubSlider(HUB_SLIDER_FG_TARGETFPS, L"Adaptive Target FPS", targetDesc,
                      config_->fgAdaptiveTargetFps, 60, 240, mx + halfW + 24.0f, 352.0f, halfW);

        DrawTxt(rt_.Get(), L"AFMF Fast-Motion Fallback", fmtBody_.Get(),
                D2D1::RectF(mx, 414.0f, mx + halfW - 46.0f, 436.0f), brText_.Get());
        DrawTogglePill(HUB_TOGGLE_FG_FALLBACK, mx + halfW - 38.0f, 414.0f, config_->fgFastMotionFallback, false);

        DrawTxt(rt_.Get(), L"Static HUD Protection", fmtBody_.Get(),
                D2D1::RectF(mx + halfW + 24.0f, 414.0f, mx + mw - 46.0f, 436.0f), brText_.Get());
        DrawTogglePill(HUB_TOGGLE_FG_HUDPROTECT, mx + mw - 38.0f, 414.0f, config_->fgStaticHudProtect, false);
    }

    // Telemetry & Best Practices Footer Strip (y = 474..520)
    D2D1_RECT_F telR = D2D1::RectF(cx0, 474.0f, cx0 + cw, 520.0f);
    rt_->FillRoundedRectangle(D2D1::RoundedRect(telR, 8, 8), brCard_.Get());
    rt_->DrawRoundedRectangle(D2D1::RoundedRect(telR, 8, 8),
                              (telemetry_.fgRunning || telemetry_.overlayActive) ? brAccent_.Get() : brBorder_.Get(), 1.0f);

    std::wstring vrrTag = config_->allowTearing ? L"ON" : L"OFF";
    std::wstring hzStr = std::to_wstring(telemetry_.monitorHz) + L" Hz";
    std::wstring shotHk = FormatHotkeyString(config_->screenshotHotkeyMods, config_->screenshotHotkeyVk);
    std::wstring telTxt;
    if (telemetry_.fgRunning && telemetry_.outputFps > 1.0f) {
        std::wstring liveCap = telemetry_.usingWgc ? L"WGC OBS" : L"DXGI";
        telTxt = L"Live Frame Gen: " + std::to_wstring(static_cast<int>(std::lround(telemetry_.sourceFps))) +
                 L" -> " + std::to_wstring(static_cast<int>(std::lround(telemetry_.outputFps))) +
                 L" FPS  \u2022  Monitor: " + hzStr +
                 L"  \u2022  Live: " + liveCap + L"  \u2022  VRR: " + vrrTag + L"  \u2022  Filters apply after scaling";
    } else if (telemetry_.overlayActive) {
        if (telemetry_.usingWgc) {
            telTxt = L"Active (Monitor: " + hzStr + L"  \u2022  Live: WGC OBS  \u2022  VRR: " + vrrTag +
                     L")  \u2022  OBS & Windows Screenshot Ready";
        } else if (config_->captureApi == CAPTURE_WGC) {
            telTxt = L"Active (Monitor: " + hzStr + L"  \u2022  Live: DXGI Fallback)  \u2022  Focus a window for WGC OBS or press " + shotHk;
        } else {
            telTxt = L"Active (Monitor: " + hzStr + L"  \u2022  Live: DXGI Smooth  \u2022  VRR: " + vrrTag +
                     L")  \u2022  Press " + shotHk + L" for filtered PNG";
        }
    } else {
        std::wstring modeTag = (config_->captureApi == CAPTURE_WGC) ? L"WGC OBS" : L"DXGI Smooth";
        telTxt = L"Standby (Monitor: " + hzStr + L"  \u2022  Mode: " + modeTag +
                 L"  \u2022  VRR: " + vrrTag + L")  \u2022  " + shotHk + L" saves filtered PNG";
    }
    DrawTxt(rt_.Get(), telTxt, fmtSmall_.Get(), D2D1::RectF(cx0 + 16.0f, 489.0f, cx0 + cw - 16.0f, 510.0f),
            (telemetry_.fgRunning || telemetry_.overlayActive) ? brText_.Get() : brTextMuted_.Get());
}

void ControlHub::RenderGamesTab() {
    const float cx0 = 24.0f;
    const float cw = kHubDipW - 48.0f;

    rt_->DrawTextW(L"Game Detection Overrides", 24, fmtTitle_.Get(),
                   D2D1::RectF(cx0, 70, cx0 + cw, 94), brText_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    std::wstring sub = L"Force unrecognized titles or emulators as games, or ignore specific background apps by .exe filename.";
    rt_->DrawTextW(sub.c_str(), static_cast<UINT32>(sub.size()), fmtBody_.Get(),
                   D2D1::RectF(cx0, 96, cx0 + cw, 114), brTextMuted_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

    auto drawColumn = [&](float colX, float colY, float colW, float colH, int maxRows,
                          const std::wstring& title, const std::vector<std::wstring>& items,
                          int& page, HubControlKind addKind, const std::wstring& addText,
                          HubControlKind rmKind, HubControlKind prevKind, HubControlKind nextKind, bool primaryBtn) {
        D2D1_RECT_F cardR = D2D1::RectF(colX, colY, colX + colW, colY + colH);
        rt_->FillRoundedRectangle(D2D1::RoundedRect(cardR, 10, 10), brCard_.Get());
        rt_->DrawRoundedRectangle(D2D1::RoundedRect(cardR, 10, 10), brBorder_.Get(), 1.0f);

        float lx = colX + 16.0f;
        float lw = colW - 32.0f;
        std::wstring hdr = title + L" (" + std::to_wstring(items.size()) + L")";
        rt_->DrawTextW(hdr.c_str(), static_cast<UINT32>(hdr.size()), fmtSection_.Get(),
                       D2D1::RectF(lx, colY + 14, lx + lw - 108, colY + 36), brText_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        DrawFlatButton(addKind, addText, D2D1::RectF(lx + lw - 100, colY + 11, lx + lw, colY + 37), primaryBtn);

        rt_->DrawLine(D2D1::Point2F(lx, colY + 46), D2D1::Point2F(lx + lw, colY + 46), brBorder_.Get(), 1.0f);

        int total = static_cast<int>(items.size());
        int totalPages = std::max(1, (total + maxRows - 1) / maxRows);
        page = std::clamp(page, 0, totalPages - 1);

        if (items.empty()) {
            std::wstring emptyMsg = L"No custom entries added.";
            rt_->DrawTextW(emptyMsg.c_str(), static_cast<UINT32>(emptyMsg.size()), fmtCenter_.Get(),
                           D2D1::RectF(lx, colY + 52, lx + lw, colY + colH - 12), brTextMuted_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        } else {
            int startIdx = page * maxRows;
            int endIdx = std::min(startIdx + maxRows, total);
            for (int i = startIdx; i < endIdx; ++i) {
                float ry = colY + 55.0f + static_cast<float>(i - startIdx) * 32.0f;
                D2D1_RECT_F rowR = D2D1::RectF(lx, ry, lx + lw, ry + 26.0f);
                rt_->FillRoundedRectangle(D2D1::RoundedRect(rowR, 5, 5), brBg_.Get());
                rt_->DrawRoundedRectangle(D2D1::RoundedRect(rowR, 5, 5), brBorder_.Get(), 0.9f);
                rt_->DrawTextW(items[i].c_str(), static_cast<UINT32>(items[i].size()), fmtSmall_.Get(),
                               D2D1::RectF(lx + 10, ry + 5, lx + lw - 32, ry + 22), brText_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

                D2D1_RECT_F rmR = D2D1::RectF(lx + lw - 26, ry + 3, lx + lw - 4, ry + 23);
                rt_->FillRoundedRectangle(D2D1::RoundedRect(rmR, 4, 4), brCardHover_.Get());
                rt_->DrawTextW(L"x", 1, fmtCenter_.Get(), rmR, brDanger_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                hitRects_.push_back({ rmKind, rmR, false, i });
            }
        }

        if (totalPages > 1) {
            float py = colY + colH - 34.0f;
            if (page > 0) DrawFlatButton(prevKind, L"< Prev", D2D1::RectF(lx, py, lx + 58, py + 24), false);
            std::wstring pStr = L"Page " + std::to_wstring(page + 1) + L"/" + std::to_wstring(totalPages);
            rt_->DrawTextW(pStr.c_str(), static_cast<UINT32>(pStr.size()), fmtCenter_.Get(),
                           D2D1::RectF(lx + 62, py, lx + lw - 62, py + 24), brTextMuted_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            if (page + 1 < totalPages) DrawFlatButton(nextKind, L"Next >", D2D1::RectF(lx + lw - 58, py, lx + lw, py + 24), false);
        }
    };

    if (!config_->hubListView) {
        float colW = (cw - 16.0f) * 0.5f;
        drawColumn(cx0, 124, colW, 366, 8, L"Force Enable (Whitelist)", config_->customGameWhitelist, wlPage_,
                   HUB_BTN_WL_ADD, L"+ Add .exe", HUB_BTN_WL_REMOVE, HUB_BTN_WL_PREV, HUB_BTN_WL_NEXT, true);
        drawColumn(cx0 + colW + 16.0f, 124, colW, 366, 8, L"Always Ignore (Blacklist)", config_->customGameBlacklist, blPage_,
                   HUB_BTN_BL_ADD, L"+ Ignore .exe", HUB_BTN_BL_REMOVE, HUB_BTN_BL_PREV, HUB_BTN_BL_NEXT, false);
    } else {
        drawColumn(cx0, 124, cw, 176, 3, L"Force Enable (Whitelist)", config_->customGameWhitelist, wlPage_,
                   HUB_BTN_WL_ADD, L"+ Add .exe", HUB_BTN_WL_REMOVE, HUB_BTN_WL_PREV, HUB_BTN_WL_NEXT, true);
        drawColumn(cx0, 312, cw, 176, 3, L"Always Ignore (Blacklist)", config_->customGameBlacklist, blPage_,
                   HUB_BTN_BL_ADD, L"+ Ignore .exe", HUB_BTN_BL_REMOVE, HUB_BTN_BL_PREV, HUB_BTN_BL_NEXT, false);
    }

    rt_->DrawTextW(gameListStatus_.c_str(), static_cast<UINT32>(gameListStatus_.size()), fmtSmall_.Get(),
                   D2D1::RectF(cx0, 504, cx0 + cw, 526),
                   gameListStatusError_ ? brDanger_.Get() : brTextMuted_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void ControlHub::RenderPresetsTab() {
    const float cx0 = 24.0f;
    const float cw = kHubDipW - 48.0f;

    rt_->DrawTextW(L"Presets & Configuration", 23, fmtTitle_.Get(),
                   D2D1::RectF(cx0, 70, cx0 + cw, 94), brText_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    std::wstring sub = L"Share portable .json filter presets or reset saved profile slots.";
    rt_->DrawTextW(sub.c_str(), static_cast<UINT32>(sub.size()), fmtBody_.Get(),
                   D2D1::RectF(cx0, 96, cx0 + cw, 114), brTextMuted_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

    presetSlot_ = std::clamp(presetSlot_, 1, 3);
    const HubControlKind slotKinds[3] = { HUB_PRESET_SLOT_1, HUB_PRESET_SLOT_2, HUB_PRESET_SLOT_3 };

    if (!config_->hubListView) {
        const float gap = 14.0f;
        const float cardW = (cw - gap * 2.0f) / 3.0f;
        for (int i = 0; i < 3; ++i) {
            float sx = cx0 + static_cast<float>(i) * (cardW + gap);
            bool sel = (presetSlot_ == (i + 1));
            D2D1_RECT_F sr = D2D1::RectF(sx, 122, sx + cardW, 234);
            rt_->FillRoundedRectangle(D2D1::RoundedRect(sr, 10, 10), sel ? brAccentSoft_.Get() : brCard_.Get());
            rt_->DrawRoundedRectangle(D2D1::RoundedRect(sr, 10, 10), sel ? brAccent_.Get() : brBorder_.Get(), sel ? 1.6f : 1.0f);

            std::wstring pTitle = L"Profile " + std::to_wstring(i + 1);
            rt_->DrawTextW(pTitle.c_str(), static_cast<UINT32>(pTitle.size()), fmtSection_.Get(),
                           D2D1::RectF(sx + 16, 132, sx + cardW - 92, 154), brText_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            if (sel) {
                D2D1_RECT_F pill = D2D1::RectF(sx + cardW - 84, 132, sx + cardW - 14, 152);
                rt_->FillRoundedRectangle(D2D1::RoundedRect(pill, 4, 4), brAccent_.Get());
                rt_->DrawTextW(L"Selected", 8, fmtCenter_.Get(), pill, brText_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
            std::wstring sum = BuildProfileSummary(config_->profiles[i]);
            rt_->DrawTextW(sum.c_str(), static_cast<UINT32>(sum.size()), fmtSmall_.Get(),
                           D2D1::RectF(sx + 16, 158, sx + cardW - 16, 226), brTextMuted_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            hitRects_.push_back({ slotKinds[i], sr, false, -1 });
        }
    } else {
        D2D1_RECT_F panel = D2D1::RectF(cx0, 122, cx0 + cw, 236);
        rt_->FillRoundedRectangle(D2D1::RoundedRect(panel, 10, 10), brCard_.Get());
        rt_->DrawRoundedRectangle(D2D1::RoundedRect(panel, 10, 10), brBorder_.Get(), 1.0f);
        for (int i = 0; i < 3; ++i) {
            float ry = 122.0f + static_cast<float>(i) * 38.0f;
            if (i > 0) rt_->DrawLine(D2D1::Point2F(cx0 + 16, ry), D2D1::Point2F(cx0 + cw - 16, ry), brBorder_.Get(), 1.0f);
            bool sel = (presetSlot_ == (i + 1));
            D2D1_RECT_F rr = D2D1::RectF(cx0, ry, cx0 + cw, ry + 38.0f);
            if (sel) rt_->FillRectangle(D2D1::RectF(cx0 + 1, ry + 5, cx0 + 4.5f, ry + 33), brAccent_.Get());
            std::wstring pTitle = L"Profile " + std::to_wstring(i + 1) + (sel ? L"  (Selected)" : L"");
            rt_->DrawTextW(pTitle.c_str(), static_cast<UINT32>(pTitle.size()), fmtSection_.Get(),
                           D2D1::RectF(cx0 + 18, ry + 9, cx0 + 210, ry + 29), sel ? brText_.Get() : brTextMuted_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            std::wstring sum = BuildProfileSummary(config_->profiles[i]);
            rt_->DrawTextW(sum.c_str(), static_cast<UINT32>(sum.size()), fmtSmall_.Get(),
                           D2D1::RectF(cx0 + 220, ry + 11, cx0 + cw - 18, ry + 29), brTextMuted_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            hitRects_.push_back({ slotKinds[i], rr, false, -1 });
        }
    }

    // One-Click Starter Presets (y = 244..314)
    {
        std::wstring qTitle = L"One-Click Starter Presets (Apply to Profile " + std::to_wstring(presetSlot_) + L")";
        rt_->DrawTextW(qTitle.c_str(), static_cast<UINT32>(qTitle.size()), fmtSection_.Get(),
                       D2D1::RectF(cx0, 244, cx0 + cw, 264), brText_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

        const HubControlKind qKinds[5] = {
            HUB_PRESET_QUICK_0, HUB_PRESET_QUICK_1, HUB_PRESET_QUICK_2, HUB_PRESET_QUICK_3, HUB_PRESET_QUICK_4
        };
        const wchar_t* qSubs[5] = { L"Balanced", L"AgX + Bloom", L"FPS Clarity", L"Vivid Color", L"Warm Night" };
        const float qGap = 10.0f;
        const float qW = (cw - qGap * 4.0f) / 5.0f;
        if (fmtSmall_) fmtSmall_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        for (int i = 0; i < 5; ++i) {
            float qx = cx0 + static_cast<float>(i) * (qW + qGap);
            D2D1_RECT_F qr = D2D1::RectF(qx, 268, qx + qW, 314);
            rt_->FillRoundedRectangle(D2D1::RoundedRect(qr, 7, 7), brCard_.Get());
            rt_->DrawRoundedRectangle(D2D1::RoundedRect(qr, 7, 7), brBorder_.Get(), 1.0f);

            const wchar_t* qName = GetCuratedPresetName(i);
            rt_->DrawTextW(qName, static_cast<UINT32>(wcslen(qName)), fmtCenter_.Get(),
                           D2D1::RectF(qx + 4, 272, qx + qW - 4, 293), brText_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            rt_->DrawTextW(qSubs[i], static_cast<UINT32>(wcslen(qSubs[i])), fmtSmall_.Get(),
                           D2D1::RectF(qx + 4, 293, qx + qW - 4, 310), brTextMuted_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            hitRects_.push_back({ qKinds[i], qr, false, -1 });
        }
        if (fmtSmall_) fmtSmall_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }

    // Actions & Storage Card (y = 326..518)
    D2D1_RECT_F actR = D2D1::RectF(cx0, 326, cx0 + cw, 518);
    rt_->FillRoundedRectangle(D2D1::RoundedRect(actR, 10, 10), brCard_.Get());
    rt_->DrawRoundedRectangle(D2D1::RoundedRect(actR, 10, 10), brBorder_.Get(), 1.0f);

    std::wstring mTitle = L"Manage Profile " + std::to_wstring(presetSlot_);
    rt_->DrawTextW(mTitle.c_str(), static_cast<UINT32>(mTitle.size()), fmtSection_.Get(),
                   D2D1::RectF(cx0 + 20, 338, cx0 + cw - 20, 358), brText_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

    std::wstring dataPath = L"Config Folder: " + GetExeDirectory().wstring();
    rt_->DrawTextW(dataPath.c_str(), static_cast<UINT32>(dataPath.size()), fmtSmall_.Get(),
                   D2D1::RectF(cx0 + 20, 360, cx0 + cw - 20, 378), brTextMuted_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

    float mw = cw - 40.0f;
    float w2 = (mw - 12.0f) * 0.5f;
    DrawFlatButton(HUB_BTN_PRESET_EXPORT, L"Export Profile " + std::to_wstring(presetSlot_) + L" (.json)...",
                   D2D1::RectF(cx0 + 20, 388, cx0 + 20 + w2, 424), true);
    DrawFlatButton(HUB_BTN_PRESET_IMPORT, L"Import into Profile " + std::to_wstring(presetSlot_) + L" (.json)...",
                   D2D1::RectF(cx0 + 20 + w2 + 12.0f, 388, cx0 + cw - 20, 424), false);

    float g4 = 10.0f;
    float w4 = (mw - g4 * 3.0f) / 4.0f;
    DrawFlatButton(HUB_BTN_OPEN_DATA_FOLDER, L"Open Data Folder",
                   D2D1::RectF(cx0 + 20, 434, cx0 + 20 + w4, 468), false);
    DrawFlatButton(HUB_BTN_OPEN_SCREENSHOTS, L"Open Screenshots",
                   D2D1::RectF(cx0 + 20 + (w4 + g4), 434, cx0 + 20 + (w4 + g4) + w4, 468), false);
    DrawFlatButton(HUB_BTN_RESET_PROFILE, L"Reset Profile " + std::to_wstring(presetSlot_),
                   D2D1::RectF(cx0 + 20 + (w4 + g4) * 2.0f, 434, cx0 + 20 + (w4 + g4) * 2.0f + w4, 468), false);
    DrawFlatButton(HUB_BTN_RESET_ALL, L"Reset All",
                   D2D1::RectF(cx0 + 20 + (w4 + g4) * 3.0f, 434, cx0 + cw - 20, 468), false, true);

    rt_->DrawTextW(presetStatus_.c_str(), static_cast<UINT32>(presetStatus_.size()), fmtSmall_.Get(),
                   D2D1::RectF(cx0 + 20, 480, cx0 + cw - 20, 506),
                   presetStatusError_ ? brDanger_.Get() : brTextMuted_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void ControlHub::RenderLegacyTab() {
    const float cx0 = 24.0f;
    const float cw = kHubDipW - 48.0f;

    DrawTxt(rt_.Get(), L"Legacy Fullscreen Support (Optional ReShade .fx)", fmtTitle_.Get(),
            D2D1::RectF(cx0, 70, cx0 + cw, 94), brText_.Get());
    DrawTxt(rt_.Get(), L"Export AIDynamicVibranceEnhance.fx for classic DX9, DX10, and OpenGL Exclusive Fullscreen games.", fmtBody_.Get(),
            D2D1::RectF(cx0, 96, cx0 + cw, 114), brTextMuted_.Get());

    D2D1_RECT_F cardR = D2D1::RectF(cx0, 126, cx0 + cw, 392);
    rt_->FillRoundedRectangle(D2D1::RoundedRect(cardR, 10, 10), brCard_.Get());
    rt_->DrawRoundedRectangle(D2D1::RoundedRect(cardR, 10, 10), brBorder_.Get(), 1.0f);

    float mx = cx0 + 20.0f;
    float mw = cw - 40.0f;
    DrawTxt(rt_.Get(), L"Target Game Folder or reshade-shaders\\Shaders", fmtSection_.Get(),
            D2D1::RectF(mx, 144, mx + mw, 166), brText_.Get());

    std::wstring folderDisp = config_->reshadeFolder.empty()
        ? L"(Not selected \u2014 pick your legacy game's Shaders folder)"
        : config_->reshadeFolder;
    if (folderDisp.size() > 86) {
        folderDisp = L"..." + folderDisp.substr(folderDisp.size() - 83);
    }
    D2D1_RECT_F folderBox = D2D1::RectF(mx, 174, mx + mw - 116, 208);
    rt_->FillRoundedRectangle(D2D1::RoundedRect(folderBox, 6, 6), brBg_.Get());
    rt_->DrawRoundedRectangle(D2D1::RoundedRect(folderBox, 6, 6), brBorder_.Get(), 1.0f);
    DrawTxt(rt_.Get(), folderDisp, fmtSmall_.Get(),
            D2D1::RectF(folderBox.left + 12, folderBox.top + 9, folderBox.right - 10, folderBox.bottom),
            brTextMuted_.Get());
    DrawFlatButton(HUB_BTN_RS_BROWSE, L"Browse...", D2D1::RectF(mx + mw - 104, 174, mx + mw, 208), false);

    DrawTxt(rt_.Get(), L"Export Preset Source (16 filters synced)", fmtBody_.Get(),
            D2D1::RectF(mx, 224, mx + mw, 244), brTextMuted_.Get());

    int m2Prof = std::clamp(config_->mode2ExportProfile, 1, 3);
    float m2Gap = 12.0f;
    float m2W = (mw - m2Gap * 2.0f) / 3.0f;
    const HubControlKind m2Kinds[3] = { HUB_M2_PROF_1, HUB_M2_PROF_2, HUB_M2_PROF_3 };
    for (int i = 0; i < 3; ++i) {
        float sx = mx + static_cast<float>(i) * (m2W + m2Gap);
        bool sel = (m2Prof == (i + 1));
        D2D1_RECT_F sr = D2D1::RectF(sx, 250, sx + m2W, 284);
        rt_->FillRoundedRectangle(D2D1::RoundedRect(sr, 6, 6), sel ? brAccentSoft_.Get() : brBg_.Get());
        rt_->DrawRoundedRectangle(D2D1::RoundedRect(sr, 6, 6), sel ? brAccent_.Get() : brBorder_.Get(), sel ? 1.5f : 1.0f);
        DrawTxt(rt_.Get(), L"Profile " + std::to_wstring(i + 1), fmtCenter_.Get(), sr,
                sel ? brText_.Get() : brTextMuted_.Get());
        hitRects_.push_back({ m2Kinds[i], sr, false, -1 });
    }

    float bw = (mw - 12.0f) * 0.5f;
    DrawFlatButton(HUB_BTN_RS_INSTALL, L"Install / Update .fx (Profile " + std::to_wstring(m2Prof) + L")",
                   D2D1::RectF(mx, 302, mx + bw, 340), true);
    DrawFlatButton(HUB_BTN_RS_REMOVE, L"Remove .fx",
                   D2D1::RectF(mx + bw + 12.0f, 302, mx + mw, 340), false, true);

    DrawTxt(rt_.Get(), reshadeStatus_, fmtSmall_.Get(),
            D2D1::RectF(mx, 352, mx + mw, 384),
            reshadeStatusError_ ? brDanger_.Get() : brTextMuted_.Get());

    // Info Bento Strip (y = 408..492)
    float iw = (cw - 16.0f) * 0.5f;
    D2D1_RECT_F i1 = D2D1::RectF(cx0, 408, cx0 + iw, 492);
    D2D1_RECT_F i2 = D2D1::RectF(cx0 + iw + 16.0f, 408, cx0 + cw, 492);
    rt_->FillRoundedRectangle(D2D1::RoundedRect(i1, 10, 10), brCard_.Get());
    rt_->DrawRoundedRectangle(D2D1::RoundedRect(i1, 10, 10), brBorder_.Get(), 1.0f);
    rt_->FillRoundedRectangle(D2D1::RoundedRect(i2, 10, 10), brCard_.Get());
    rt_->DrawRoundedRectangle(D2D1::RoundedRect(i2, 10, 10), brBorder_.Get(), 1.0f);

    DrawTxt(rt_.Get(), L"Real-Time Overlay (Default)", fmtSection_.Get(),
            D2D1::RectF(i1.left + 16, i1.top + 14, i1.right - 16, i1.top + 36), brText_.Get());
    DrawTxt(rt_.Get(), L"Works automatically on modern DX12, Vulkan, and DX11 Borderless Fullscreen games with DirectComposition.", fmtSmall_.Get(),
            D2D1::RectF(i1.left + 16, i1.top + 40, i1.right - 16, i1.bottom - 12), brTextMuted_.Get());

    DrawTxt(rt_.Get(), L"Legacy Fullscreen Shader (Optional)", fmtSection_.Get(),
            D2D1::RectF(i2.left + 16, i2.top + 14, i2.right - 16, i2.top + 36), brText_.Get());
    DrawTxt(rt_.Get(), L"Exports all 16 calibrated shader modules directly into ReShade for classic DX9, DX10, and OpenGL titles.", fmtSmall_.Get(),
            D2D1::RectF(i2.left + 16, i2.top + 40, i2.right - 16, i2.bottom - 12), brTextMuted_.Get());
}

void ControlHub::RenderAboutTab() {
    const float cx0 = 24.0f;
    const float cw = kHubDipW - 48.0f;

    // Hero Identity Card (y = 70..162)
    D2D1_RECT_F hero = D2D1::RectF(cx0, 70, cx0 + cw, 162);
    rt_->FillRoundedRectangle(D2D1::RoundedRect(hero, 10, 10), brCard_.Get());
    rt_->DrawRoundedRectangle(D2D1::RoundedRect(hero, 10, 10), brBorder_.Get(), 1.0f);

    D2D1_RECT_F logo = D2D1::RectF(cx0 + 20, 94, cx0 + 64, 138);
    DrawBrandStarBadge(d2dFactory_.Get(), rt_.Get(), logo, brAccent_.Get(), brText_.Get());

    DrawTxt(rt_.Get(), L"AI Dynamic Vibrance Enhance", fmtTitle_.Get(),
            D2D1::RectF(cx0 + 82, 88, cx0 + 360, 114), brText_.Get());

    D2D1_RECT_F verBadge = D2D1::RectF(cx0 + 364, 90, cx0 + 426, 112);
    rt_->FillRoundedRectangle(D2D1::RoundedRect(verBadge, 5, 5), brAccentSoft_.Get());
    rt_->DrawRoundedRectangle(D2D1::RoundedRect(verBadge, 5, 5), brAccent_.Get(), 1.0f);
    DrawTxt(rt_.Get(), L"v2.0.0", fmtCenter_.Get(), verBadge, brText_.Get());

    DrawTxt(rt_.Get(), L"Developed by arthurj119  \u2022  MIT Open Source License  \u2022  Cross GPU Game Visual, Scaling & FG Suite",
            fmtBody_.Get(), D2D1::RectF(cx0 + 82, 120, cx0 + cw - 196, 144), brTextMuted_.Get());

    DrawFlatButton(HUB_BTN_GITHUB, L"GitHub Repository",
                   D2D1::RectF(cx0 + cw - 182, 98, cx0 + cw - 20, 134), true);

    // Two Equal Bento Cards Below (y = 178..492)
    float colW = (cw - 16.0f) * 0.5f;
    D2D1_RECT_F leftCard = D2D1::RectF(cx0, 178, cx0 + colW, 492);
    D2D1_RECT_F rightCard = D2D1::RectF(cx0 + colW + 16.0f, 178, cx0 + cw, 492);
    rt_->FillRoundedRectangle(D2D1::RoundedRect(leftCard, 10, 10), brCard_.Get());
    rt_->DrawRoundedRectangle(D2D1::RoundedRect(leftCard, 10, 10), brBorder_.Get(), 1.0f);
    rt_->FillRoundedRectangle(D2D1::RoundedRect(rightCard, 10, 10), brCard_.Get());
    rt_->DrawRoundedRectangle(D2D1::RoundedRect(rightCard, 10, 10), brBorder_.Get(), 1.0f);

    DrawTxt(rt_.Get(), L"Application Details", fmtSection_.Get(),
            D2D1::RectF(leftCard.left + 20, 196, leftCard.right - 20, 220), brText_.Get());

    struct DetailRow {
        const wchar_t* k;
        const wchar_t* v;
    };
    const DetailRow details[7] = {
        { L"Application",    L"AI Dynamic Vibrance Enhance" },
        { L"Version",        L"v2.0.0" },
        { L"Developer",      L"arthurj119" },
        { L"License",        L"MIT Open Source" },
        { L"Render Engine",  L"DX11 HLSL + DirectComposition (SDR, scRGB HDR & Optical Flow)" },
        { L"Shader Modules", L"16 Visual Filters + 4 Spatial Scalers + Multi-Scale FG" },
        { L"GPU Support",    L"AMD Radeon / NVIDIA GeForce / Intel" },
    };
    for (int i = 0; i < 7; ++i) {
        float ry = 232.0f + static_cast<float>(i) * 35.0f;
        if (i > 0) {
            rt_->DrawLine(D2D1::Point2F(leftCard.left + 20, ry), D2D1::Point2F(leftCard.right - 20, ry), brBorder_.Get(), 1.0f);
        }
        DrawTxt(rt_.Get(), details[i].k, fmtSmall_.Get(),
                D2D1::RectF(leftCard.left + 20, ry + 10, leftCard.left + 140, ry + 28), brTextMuted_.Get());
        DrawTxt(rt_.Get(), details[i].v, fmtSmall_.Get(),
                D2D1::RectF(leftCard.left + 144, ry + 10, leftCard.right - 20, ry + 28), brText_.Get());
    }

    DrawTxt(rt_.Get(), L"Thank You", fmtSection_.Get(),
            D2D1::RectF(rightCard.left + 20, 196, rightCard.right - 20, 220), brText_.Get());
    rt_->DrawLine(D2D1::Point2F(rightCard.left + 20, 232), D2D1::Point2F(rightCard.right - 20, 232), brBorder_.Get(), 1.0f);

    std::wstring thankYouBody =
        L"Thank you for using AI Dynamic Vibrance Enhance.\n\n"
        L"This project was created to bring open source, hardware agnostic color vibrancy, dark area visibility, "
        L"True HDR10 highlight expansion, and perceptual clarity to every PC gamer without proprietary driver lock in or heavy GPU overhead.\n\n"
        L"We truly appreciate your support, feedback, custom presets, and community testing across AMD, NVIDIA, and Intel GPUs.";
    DrawTxt(rt_.Get(), thankYouBody, fmtBody_.Get(),
            D2D1::RectF(rightCard.left + 20, 248, rightCard.right - 20, 434), brTextMuted_.Get());

    DrawTxt(rt_.Get(), L"by arthurj119", fmtSection_.Get(),
            D2D1::RectF(rightCard.left + 20, 444, rightCard.right - 20, 472), brAccent_.Get());
}

void ControlHub::Render() {
    if (!hwnd_ || !config_) return;
    if (!CreateRenderResources()) return;

    hitRects_.clear();
    rt_->BeginDraw();
    rt_->Clear(HubColHex(0x0E1014));

    DrawTopBarAndFooter();

    switch (currentTab_) {
        case 0: RenderOverviewTab(); break;
        case 1: RenderScalingTab();  break;
        case 2: RenderGamesTab();    break;
        case 3: RenderPresetsTab();  break;
        case 4: RenderLegacyTab();   break;
        case 5: RenderAboutTab();    break;
        default: RenderOverviewTab(); break;
    }

    // Crisp outer border around frameless window
    rt_->DrawRectangle(D2D1::RectF(0.5f, 0.5f, kHubDipW - 0.5f, kHubDipH - 0.5f), brBorder_.Get(), 1.0f);

    HRESULT hr = rt_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardRenderResources();
    }
}

void ControlHub::UpdateHubSliderFromX(HubControlKind kind, float dipX, const D2D1_RECT_F& trackRect) {
    if (!config_) return;
    float left = trackRect.left + 4.0f;
    float right = trackRect.right - 4.0f;
    float norm = std::clamp((dipX - left) / std::max(right - left, 1.0f), 0.0f, 1.0f);
    switch (kind) {
        case HUB_SLIDER_SPLITPOS:
            config_->splitPosition = std::clamp(norm, 0.05f, 0.95f);
            break;
        case HUB_SLIDER_SCALE_SHARP:
            config_->scaleSharpness = std::clamp(static_cast<int>(std::lround(norm * 100.0f)), 0, 100);
            break;
        case HUB_SLIDER_FG_FLOWSCALE: {
            int val = 25 + static_cast<int>(std::lround(norm * 15.0f)) * 5;
            config_->fgFlowScale = std::clamp(val, 25, 100);
            break;
        }
        case HUB_SLIDER_FG_TARGETFPS: {
            int val = 60 + static_cast<int>(std::lround(norm * 36.0f)) * 5;
            config_->fgAdaptiveTargetFps = std::clamp(val, 60, 240);
            break;
        }
        default:
            break;
    }
    Render();
    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
}

void ControlHub::SetRecordingHotkey(int target) {
    bool wasRecording = (recordingHotkey_ != 0);
    bool nowRecording = (target != 0);
    if (recordingHotkey_ == target) return;
    recordingHotkey_ = target;
    if (nowRecording) {
        if (target == 1) {
            hotkeyStatus_ = L"Press new key combo for Filter Sidebar (e.g. Alt + F3, F9). Esc cancels.";
        } else if (target == 2) {
            hotkeyStatus_ = L"Press new key combo for Quick Filter Toggle (e.g. Alt + V, F10). Esc cancels.";
        } else {
            hotkeyStatus_ = L"Press new key combo for Filtered Screenshot (e.g. Alt + F12, F11). Esc cancels.";
        }
        hotkeyStatusError_ = false;
    }
    if (wasRecording != nowRecording && callbacks_.onHotkeyCaptureChanged) {
        callbacks_.onHotkeyCaptureChanged(nowRecording);
    }
    Render();
}

void ControlHub::HandleHotkeyKeyDown(UINT vk) {
    if (recordingHotkey_ == 0 || !config_) return;

    if (vk == VK_ESCAPE) {
        hotkeyStatus_ = L"Hotkey change cancelled.";
        hotkeyStatusError_ = false;
        SetRecordingHotkey(0);
        return;
    }
    if (IsModifierOnlyKey(vk)) {
        return;
    }

    uint32_t mods = 0;
    if (GetKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
    if (GetKeyState(VK_MENU) & 0x8000)    mods |= MOD_ALT;
    if (GetKeyState(VK_SHIFT) & 0x8000)   mods |= MOD_SHIFT;

    if (!IsAllowedBindingKey(vk)) {
        hotkeyStatus_ = L"Unsupported key. Use A-Z, 0-9, F1-F12, Home/End/Ins/PgUp/PgDn.";
        hotkeyStatusError_ = true;
        Render();
        return;
    }
    if (mods == 0 && !IsStandaloneAllowedKey(vk)) {
        hotkeyStatus_ = L"Include Ctrl, Alt, or Shift for letter or number keys.";
        hotkeyStatusError_ = true;
        Render();
        return;
    }

    bool collision = false;
    if (recordingHotkey_ != 1 && mods == config_->sidebarHotkeyMods && vk == config_->sidebarHotkeyVk) collision = true;
    if (recordingHotkey_ != 2 && mods == config_->quickToggleMods && vk == config_->quickToggleVk) collision = true;
    if (recordingHotkey_ != 3 && mods == config_->screenshotHotkeyMods && vk == config_->screenshotHotkeyVk) collision = true;
    if (collision) {
        hotkeyStatus_ = L"That shortcut is already assigned to another action.";
        hotkeyStatusError_ = true;
        Render();
        return;
    }

    if (recordingHotkey_ == 1) {
        config_->sidebarHotkeyMods = mods;
        config_->sidebarHotkeyVk = vk;
        hotkeyStatus_ = L"Updated Filter Sidebar hotkey to " + FormatHotkeyString(mods, vk) + L".";
    } else if (recordingHotkey_ == 2) {
        config_->quickToggleMods = mods;
        config_->quickToggleVk = vk;
        config_->quickToggleEnabled = true;
        hotkeyStatus_ = L"Updated Quick Filter hotkey to " + FormatHotkeyString(mods, vk) + L".";
    } else {
        config_->screenshotHotkeyMods = mods;
        config_->screenshotHotkeyVk = vk;
        hotkeyStatus_ = L"Updated Filtered Screenshot hotkey to " + FormatHotkeyString(mods, vk) + L".";
    }
    hotkeyStatusError_ = false;
    SetRecordingHotkey(0);
    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
}

void ControlHub::OpenDataFolder() {
    std::wstring dir = GetExeDirectory().wstring();
    ShellExecuteW(hwnd_, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void ControlHub::OpenScreenshotsFolder() {
    std::filesystem::path dir = GetScreenshotsDirectory();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    ShellExecuteW(hwnd_, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void ControlHub::PickExecutableForList(bool whitelist) {
    if (!config_) return;
    ComPtr<IFileOpenDialog> dlg;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg));
    if (FAILED(hr) || !dlg) return;

    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR);
    static const COMDLG_FILTERSPEC specs[] = {
        { L"Executables (*.exe)", L"*.exe" },
        { L"All Files (*.*)",     L"*.*"   }
    };
    dlg->SetFileTypes(2, specs);
    dlg->SetTitle(whitelist ? L"Select Game Executable to Force Enable (Whitelist)"
                            : L"Select App Executable to Always Ignore (Blacklist)");

    if (SUCCEEDED(dlg->Show(hwnd_))) {
        ComPtr<IShellItem> item;
        if (SUCCEEDED(dlg->GetResult(&item)) && item) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                std::wstring exe = NormalizeExeFilename(path);
                CoTaskMemFree(path);
                if (exe.empty() || exe == L"aidynamicvibranceenhance.exe") {
                    gameListStatus_ = L"Cannot override this application's own executable.";
                    gameListStatusError_ = true;
                    Render();
                    return;
                }
                auto removeFrom = [&](std::vector<std::wstring>& vec) {
                    vec.erase(std::remove(vec.begin(), vec.end(), exe), vec.end());
                };
                removeFrom(config_->customGameWhitelist);
                removeFrom(config_->customGameBlacklist);
                if (whitelist) {
                    config_->customGameWhitelist.insert(config_->customGameWhitelist.begin(), exe);
                    wlPage_ = 0;
                    gameListStatus_ = L"Added " + exe + L" to Force-Enable Whitelist.";
                } else {
                    config_->customGameBlacklist.insert(config_->customGameBlacklist.begin(), exe);
                    blPage_ = 0;
                    gameListStatus_ = L"Added " + exe + L" to Always-Ignore Blacklist.";
                }
                gameListStatusError_ = false;
                Render();
                if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                return;
            }
        }
    }
    Render();
}

void ControlHub::ExportPresetWithDialog() {
    if (!config_) return;
    int slot = std::clamp(presetSlot_, 1, 3);

    ComPtr<IFileSaveDialog> dlg;
    HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg));
    if (FAILED(hr) || !dlg) return;

    static const COMDLG_FILTERSPEC specs[] = {
        { L"Preset JSON (*.json)", L"*.json" }
    };
    dlg->SetFileTypes(1, specs);
    dlg->SetDefaultExtension(L"json");
    std::wstring defName = L"AIDynamicVibrance_Profile" + std::to_wstring(slot) + L".json";
    dlg->SetFileName(defName.c_str());
    dlg->SetTitle(L"Export Profile Preset (.json)");

    if (SUCCEEDED(dlg->Show(hwnd_))) {
        ComPtr<IShellItem> item;
        if (SUCCEEDED(dlg->GetResult(&item)) && item) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                std::filesystem::path outPath(path);
                CoTaskMemFree(path);
                std::wstring profName = L"Profile " + std::to_wstring(slot);
                if (ExportPresetFile(config_->profiles[slot - 1], profName, outPath)) {
                    presetStatus_ = L"Exported " + profName + L" to " + outPath.filename().wstring();
                    presetStatusError_ = false;
                } else {
                    presetStatus_ = L"Failed to write preset file.";
                    presetStatusError_ = true;
                }
                Render();
                return;
            }
        }
    }
    Render();
}

void ControlHub::ImportPresetWithDialog() {
    if (!config_) return;
    int slot = std::clamp(presetSlot_, 1, 3);

    ComPtr<IFileOpenDialog> dlg;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg));
    if (FAILED(hr) || !dlg) return;

    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR);
    static const COMDLG_FILTERSPEC specs[] = {
        { L"Preset JSON (*.json)", L"*.json" },
        { L"All Files (*.*)",     L"*.*"    }
    };
    dlg->SetFileTypes(2, specs);
    dlg->SetTitle(L"Import Community Preset (.json)");

    if (SUCCEEDED(dlg->Show(hwnd_))) {
        ComPtr<IShellItem> item;
        if (SUCCEEDED(dlg->GetResult(&item)) && item) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                std::filesystem::path inPath(path);
                CoTaskMemFree(path);
                FilterParams imported{};
                std::wstring importedName;
                if (ImportPresetFile(imported, importedName, inPath)) {
                    config_->profiles[slot - 1] = imported;
                    presetStatus_ = L"Imported " + (importedName.empty() ? inPath.filename().wstring() : importedName) +
                                    L" into Profile " + std::to_wstring(slot) + L".";
                    presetStatusError_ = false;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                } else {
                    presetStatus_ = L"Invalid preset JSON file.";
                    presetStatusError_ = true;
                    Render();
                }
                return;
            }
        }
    }
    Render();
}

void ControlHub::PickReShadeFolder() {
    ComPtr<IFileOpenDialog> dlg;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg));
    if (SUCCEEDED(hr) && dlg) {
        DWORD opts = 0;
        dlg->GetOptions(&opts);
        dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        dlg->SetTitle(L"Select Game Folder or reshade-shaders\\Shaders");
        if (SUCCEEDED(dlg->Show(hwnd_))) {
            ComPtr<IShellItem> item;
            if (SUCCEEDED(dlg->GetResult(&item)) && item) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                    config_->reshadeFolder = path;
                    CoTaskMemFree(path);
                    if (DetectReShadeInFolder(config_->reshadeFolder)) {
                        reshadeStatus_ = L"ReShade detected! Click 'Install / Update .fx' to deploy shader.";
                        reshadeStatusError_ = false;
                    } else {
                        reshadeStatus_ = L"Warning: ReShade not detected in this folder. Install ReShade (reshade.me) to this game first.";
                        reshadeStatusError_ = true;
                    }
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;
                }
            }
        }
    }
    Render();
}

void ControlHub::HandleMouseDown(float dipX, float dipY) {
    if (!config_) return;
    for (const auto& hr : hitRects_) {
        if (dipX >= hr.rect.left && dipX <= hr.rect.right && dipY >= hr.rect.top && dipY <= hr.rect.bottom) {
            if (hr.isSlider) {
                SetRecordingHotkey(0);
                draggingSlider_ = true;
                activeSliderKind_ = hr.kind;
                activeTrackRect_ = hr.rect;
                SetCapture(hwnd_);
                UpdateHubSliderFromX(hr.kind, dipX, hr.rect);
                return;
            }
            if (hr.kind != HUB_BTN_HOTKEY_SIDEBAR && hr.kind != HUB_BTN_HOTKEY_QUICK &&
                hr.kind != HUB_BTN_HOTKEY_SCREENSHOT && recordingHotkey_ != 0) {
                SetRecordingHotkey(0);
            }
            switch (hr.kind) {
                case HUB_TAB_OVERVIEW: currentTab_ = 0; Render(); return;
                case HUB_TAB_SCALING:  currentTab_ = 1; Render(); return;
                case HUB_TAB_GAMES:    currentTab_ = 2; Render(); return;
                case HUB_TAB_PRESETS:  currentTab_ = 3; Render(); return;
                case HUB_TAB_LEGACY:   currentTab_ = 4; Render(); return;
                case HUB_TAB_ABOUT:    currentTab_ = 5; Render(); return;

                case HUB_VIEW_GRID:
                    if (config_->hubListView) {
                        config_->hubListView = false;
                        Render();
                        if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    }
                    return;
                case HUB_VIEW_LIST:
                    if (!config_->hubListView) {
                        config_->hubListView = true;
                        Render();
                        if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    }
                    return;
                case HUB_BTN_WIN_MIN:
                    ShowWindow(hwnd_, SW_MINIMIZE);
                    return;
                case HUB_BTN_WIN_CLOSE:
                    Hide();
                    return;

                case HUB_TOGGLE_OVERLAY:
                    config_->overlayEnabled = !config_->overlayEnabled;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;
                case HUB_BTN_HOTKEY_SIDEBAR:
                    SetRecordingHotkey(recordingHotkey_ == 1 ? 0 : 1);
                    return;
                case HUB_TOGGLE_QUICKHOTKEY:
                    config_->quickToggleEnabled = !config_->quickToggleEnabled;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;
                case HUB_BTN_HOTKEY_QUICK:
                    SetRecordingHotkey(recordingHotkey_ == 2 ? 0 : 2);
                    return;
                case HUB_BTN_HOTKEY_SCREENSHOT:
                    SetRecordingHotkey(recordingHotkey_ == 3 ? 0 : 3);
                    return;
                case HUB_BTN_HOTKEY_RESET:
                    SetRecordingHotkey(0);
                    config_->sidebarHotkeyMods = MOD_ALT;
                    config_->sidebarHotkeyVk = 'X';
                    config_->quickToggleMods = MOD_ALT;
                    config_->quickToggleVk = 'V';
                    config_->quickToggleEnabled = true;
                    config_->screenshotHotkeyMods = MOD_ALT;
                    config_->screenshotHotkeyVk = VK_F12;
                    hotkeyStatus_ = L"Reset hotkeys to defaults (Alt + X, Alt + V, and Alt + F12).";
                    hotkeyStatusError_ = false;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;

                case HUB_TOGGLE_ONLYGAMES:
                    config_->onlyInGames = !config_->onlyInGames;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;
                case HUB_TOGGLE_AUTOSWITCH:
                    config_->autoSwitchPerApp = !config_->autoSwitchPerApp;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;
                case HUB_TOGGLE_HIDESIDEBAR:
                    config_->hideSidebarFromCapture = !config_->hideSidebarFromCapture;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;
                case HUB_TOGGLE_SPLIT:
                    config_->splitPreview = !config_->splitPreview;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;
                case HUB_TOGGLE_STARTUP:
                    config_->launchAtStartup = !config_->launchAtStartup;
                    SetLaunchAtStartup(config_->launchAtStartup);
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;

                case HUB_BTN_WL_ADD:
                    PickExecutableForList(true);
                    return;
                case HUB_BTN_WL_REMOVE:
                    if (hr.payloadIdx >= 0 && hr.payloadIdx < static_cast<int>(config_->customGameWhitelist.size())) {
                        std::wstring removed = config_->customGameWhitelist[hr.payloadIdx];
                        config_->customGameWhitelist.erase(config_->customGameWhitelist.begin() + hr.payloadIdx);
                        gameListStatus_ = L"Removed " + removed + L" from Force-Enable Whitelist.";
                        gameListStatusError_ = false;
                        Render();
                        if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    }
                    return;
                case HUB_BTN_WL_PREV:
                    if (wlPage_ > 0) { --wlPage_; Render(); }
                    return;
                case HUB_BTN_WL_NEXT:
                    ++wlPage_;
                    Render();
                    return;
                case HUB_BTN_BL_ADD:
                    PickExecutableForList(false);
                    return;
                case HUB_BTN_BL_REMOVE:
                    if (hr.payloadIdx >= 0 && hr.payloadIdx < static_cast<int>(config_->customGameBlacklist.size())) {
                        std::wstring removed = config_->customGameBlacklist[hr.payloadIdx];
                        config_->customGameBlacklist.erase(config_->customGameBlacklist.begin() + hr.payloadIdx);
                        gameListStatus_ = L"Removed " + removed + L" from Always-Ignore Blacklist.";
                        gameListStatusError_ = false;
                        Render();
                        if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    }
                    return;
                case HUB_BTN_BL_PREV:
                    if (blPage_ > 0) { --blPage_; Render(); }
                    return;
                case HUB_BTN_BL_NEXT:
                    ++blPage_;
                    Render();
                    return;

                case HUB_PRESET_SLOT_1:
                case HUB_PRESET_SLOT_2:
                case HUB_PRESET_SLOT_3:
                    presetSlot_ = static_cast<int>(hr.kind - HUB_PRESET_SLOT_1) + 1;
                    Render();
                    return;
                case HUB_PRESET_QUICK_0:
                case HUB_PRESET_QUICK_1:
                case HUB_PRESET_QUICK_2:
                case HUB_PRESET_QUICK_3:
                case HUB_PRESET_QUICK_4: {
                    int idx = static_cast<int>(hr.kind - HUB_PRESET_QUICK_0);
                    int slot = std::clamp(presetSlot_, 1, 3);
                    config_->profiles[slot - 1] = BuildCuratedPreset(idx);
                    presetStatus_ = L"Applied '" + std::wstring(GetCuratedPresetName(idx)) +
                                    L"' preset to Profile " + std::to_wstring(slot) + L".";
                    presetStatusError_ = false;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;
                }
                case HUB_BTN_PRESET_EXPORT:
                    ExportPresetWithDialog();
                    return;
                case HUB_BTN_PRESET_IMPORT:
                    ImportPresetWithDialog();
                    return;
                case HUB_BTN_SCREENSHOT:
                    if (callbacks_.onScreenshotRequested) callbacks_.onScreenshotRequested();
                    return;
                case HUB_BTN_OPEN_DATA_FOLDER:
                    OpenDataFolder();
                    return;
                case HUB_BTN_OPEN_SCREENSHOTS:
                    OpenScreenshotsFolder();
                    return;
                case HUB_BTN_RESET_PROFILE: {
                    int slot = std::clamp(presetSlot_, 1, 3);
                    config_->profiles[slot - 1] = FilterParams{};
                    presetStatus_ = L"Reset Profile " + std::to_wstring(slot) + L" to balanced defaults.";
                    presetStatusError_ = false;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;
                }
                case HUB_BTN_RESET_ALL: {
                    int ans = MessageBoxW(
                        hwnd_,
                        L"Reset all 3 filter profiles back to their balanced factory defaults?",
                        L"Reset All Profiles",
                        MB_YESNO | MB_ICONQUESTION
                    );
                    if (ans == IDYES) {
                        for (int i = 0; i < 3; ++i) {
                            config_->profiles[i] = FilterParams{};
                        }
                        presetStatus_ = L"All 3 filter profiles reset to factory defaults.";
                        presetStatusError_ = false;
                        Render();
                        if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    }
                    return;
                }

                case HUB_BTN_OPEN_SIDEBAR:
                    if (!config_->overlayEnabled) {
                        config_->overlayEnabled = true;
                        Render();
                        if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    }
                    if (callbacks_.onOpenSidebar) callbacks_.onOpenSidebar();
                    return;
                case HUB_BTN_EXIT:
                    if (callbacks_.onExitRequested) callbacks_.onExitRequested();
                    return;

                case HUB_BTN_RS_BROWSE:
                    PickReShadeFolder();
                    return;
                case HUB_M2_PROF_1:
                case HUB_M2_PROF_2:
                case HUB_M2_PROF_3:
                    config_->mode2ExportProfile = static_cast<int>(hr.kind - HUB_M2_PROF_1) + 1;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;
                case HUB_BTN_RS_INSTALL: {
                    int profIdx = std::clamp(config_->mode2ExportProfile, 1, 3) - 1;
                    std::wstring installedPath, err;
                    if (InstallReShadeShader(config_->reshadeFolder, config_->profiles[profIdx], installedPath, err)) {
                        config_->mode2Enabled = true;
                        if (DetectReShadeInFolder(config_->reshadeFolder)) {
                            reshadeStatus_ = L"Installed Profile " + std::to_wstring(profIdx + 1) + L" into ReShade: " + installedPath;
                            reshadeStatusError_ = false;
                        } else {
                            reshadeStatus_ = L"Exported .fx, but ReShade was NOT detected here! Install ReShade to this game first, then re-click Install.";
                            reshadeStatusError_ = true;
                        }
                        Render();
                        if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    } else {
                        reshadeStatus_ = err;
                        reshadeStatusError_ = true;
                        Render();
                    }
                    return;
                }
                case HUB_BTN_RS_REMOVE: {
                    std::wstring msg;
                    bool ok = UninstallReShadeShader(config_->reshadeFolder, msg);
                    reshadeStatus_ = msg;
                    reshadeStatusError_ = !ok;
                    Render();
                    return;
                }
                case HUB_BTN_GITHUB:
                    ShellExecuteW(hwnd_, L"open", L"https://github.com/arthurj119/AI-Dynamic-Vibrance-Enhance",
                                  nullptr, nullptr, SW_SHOWNORMAL);
                    return;

                case HUB_SCALE_OFF:
                case HUB_SCALE_FSR1:
                case HUB_SCALE_NIS:
                case HUB_SCALE_INTEGER:
                case HUB_SCALE_ANIME4K:
                    config_->scaleMode = static_cast<int>(hr.kind - HUB_SCALE_OFF);
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;
                case HUB_TOGGLE_SCALE_STRETCH:
                    config_->scaleFullscreenStretch = !config_->scaleFullscreenStretch;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;

                case HUB_FG_OFF:
                case HUB_FG_2X:
                case HUB_FG_3X:
                case HUB_FG_4X:
                case HUB_FG_ADAPTIVE:
                    config_->fgMode = static_cast<int>(hr.kind - HUB_FG_OFF);
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;
                case HUB_TOGGLE_FG_FALLBACK:
                    config_->fgFastMotionFallback = !config_->fgFastMotionFallback;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;
                case HUB_TOGGLE_FG_HUDPROTECT:
                    config_->fgStaticHudProtect = !config_->fgStaticHudProtect;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;

                case HUB_CAPTURE_DXGI:
                    if (config_->captureApi != CAPTURE_DXGI) {
                        config_->captureApi = CAPTURE_DXGI;
                        Render();
                        if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    }
                    return;
                case HUB_CAPTURE_WGC:
                    if (config_->captureApi != CAPTURE_WGC) {
                        config_->captureApi = CAPTURE_WGC;
                        Render();
                        if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    }
                    return;
                case HUB_TOGGLE_ALLOW_TEARING:
                    config_->allowTearing = !config_->allowTearing;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;
                default:
                    break;
            }
        }
    }
    if (recordingHotkey_ != 0) {
        SetRecordingHotkey(0);
    }
}

void ControlHub::HandleMouseMove(float dipX, float dipY) {
    (void)dipY;
    if (draggingSlider_) {
        UpdateHubSliderFromX(activeSliderKind_, dipX, activeTrackRect_);
    }
}

void ControlHub::HandleMouseUp() {
    if (draggingSlider_) {
        draggingSlider_ = false;
        if (GetCapture() == hwnd_) ReleaseCapture();
    }
}

LRESULT CALLBACK ControlHub::HubWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ControlHub* self = reinterpret_cast<ControlHub*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<ControlHub*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
        case WM_CLOSE:
            self->Hide();
            return 0;
        case WM_NCHITTEST: {
            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &pt);
            float dipX = static_cast<float>(pt.x) / self->dpiScale_;
            float dipY = static_cast<float>(pt.y) / self->dpiScale_;
            if (dipY >= 0.0f && dipY <= kTopBarH) {
                for (const auto& hr : self->hitRects_) {
                    if (dipX >= hr.rect.left && dipX <= hr.rect.right &&
                        dipY >= hr.rect.top && dipY <= hr.rect.bottom) {
                        return HTCLIENT;
                    }
                }
                return HTCAPTION;
            }
            return HTCLIENT;
        }
        case WM_NCLBUTTONDBLCLK:
            return 0;
        case WM_KILLFOCUS:
            if (self->recordingHotkey_ != 0) {
                self->SetRecordingHotkey(0);
            }
            return 0;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (self->recordingHotkey_ != 0) {
                self->HandleHotkeyKeyDown(static_cast<UINT>(wParam));
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            BeginPaint(hwnd, &ps);
            self->Render();
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_LBUTTONDOWN: {
            SetFocus(hwnd);
            float x = static_cast<float>(GET_X_LPARAM(lParam)) / self->dpiScale_;
            float y = static_cast<float>(GET_Y_LPARAM(lParam)) / self->dpiScale_;
            self->HandleMouseDown(x, y);
            return 0;
        }
        case WM_MOUSEMOVE: {
            float x = static_cast<float>(GET_X_LPARAM(lParam)) / self->dpiScale_;
            float y = static_cast<float>(GET_Y_LPARAM(lParam)) / self->dpiScale_;
            self->HandleMouseMove(x, y);
            return 0;
        }
        case WM_LBUTTONUP:
            self->HandleMouseUp();
            return 0;
        case WM_DPICHANGED: {
            UINT newDpi = HIWORD(wParam);
            self->dpiScale_ = std::max(static_cast<float>(newDpi) / 96.0f, 1.0f);
            self->DiscardRenderResources();
            RECT* suggested = reinterpret_cast<RECT*>(lParam);
            if (suggested) {
                SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left, suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            self->Render();
            return 0;
        }
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}
