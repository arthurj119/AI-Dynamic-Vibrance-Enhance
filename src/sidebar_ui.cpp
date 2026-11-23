#include "sidebar_ui.h"
#include "profiles.h"

#include <windowsx.h>
#include <algorithm>
#include <cmath>

#ifndef WDA_NONE
#define WDA_NONE 0x00000000
#endif

using Microsoft::WRL::ComPtr;

namespace {

constexpr D2D1_COLOR_F ColHex(uint32_t rgb, float a = 1.0f) {
    return D2D1_COLOR_F{
        ((rgb >> 16) & 0xFF) / 255.0f,
        ((rgb >> 8) & 0xFF) / 255.0f,
        (rgb & 0xFF) / 255.0f,
        a
    };
}

} // namespace

SidebarUI::~SidebarUI() {
    Shutdown();
}

bool SidebarUI::Initialize(HINSTANCE hInst, AppConfig* config, SidebarCallbacks callbacks, std::string& outError) {
    config_ = config;
    callbacks_ = std::move(callbacks);

    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&d2dFactory_));
    if (FAILED(hr)) {
        outError = "D2D1CreateFactory failed";
        return false;
    }

    hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(dwriteFactory_.GetAddressOf())
    );
    if (FAILED(hr)) {
        outError = "DWriteCreateFactory failed";
        return false;
    }

    dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 15.0f, L"en-us", &fmtTitle_);
    dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.5f, L"en-us", &fmtSub_);
    dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"en-us", &fmtSection_);
    dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.5f, L"en-us", &fmtBody_);
    dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 10.5f, L"en-us", &fmtSmall_);
    dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"en-us", &fmtCenter_);
    if (fmtCenter_) {
        fmtCenter_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        fmtCenter_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    screenPhysW_ = std::max(GetSystemMetrics(SM_CXSCREEN), 800);
    screenPhysH_ = std::max(GetSystemMetrics(SM_CYSCREEN), 600);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = SidebarWndProc;
    wc.hInstance = hInst;
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(101));
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"AIDynamicVibranceSidebarWnd";
    RegisterClassExW(&wc);

    // WS_EX_NOACTIVATE prevents Alt+X or clicking sliders from stealing focus / Alt-Tabbing games!
    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        wc.lpszClassName,
        L"AI Dynamic Vibrance Enhance",
        WS_POPUP,
        -400, 0, 384, screenPhysH_,
        nullptr, nullptr, hInst, this
    );
    if (!hwnd_) {
        outError = "CreateWindowExW failed for sidebar";
        return false;
    }

    UINT dpi = GetDpiForWindow(hwnd_);
    dpiScale_ = std::max(static_cast<float>(dpi) / 96.0f, 1.0f);
    sidebarDipW_ = 384.0f;
    sidebarDipH_ = static_cast<float>(screenPhysH_) / dpiScale_;

    SetExcludeFromCapture(config_ ? config_->hideSidebarFromCapture : true);
    CreateRenderResources();
    UpdateWindowPosition();
    ShowWindow(hwnd_, SW_HIDE);
    return true;
}

void SidebarUI::Shutdown() {
    DiscardRenderResources();
    fmtCenter_.Reset();
    fmtSmall_.Reset();
    fmtBody_.Reset();
    fmtSection_.Reset();
    fmtSub_.Reset();
    fmtTitle_.Reset();
    dwriteFactory_.Reset();
    d2dFactory_.Reset();
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

bool SidebarUI::CreateRenderResources() {
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

    rt_->CreateSolidColorBrush(ColHex(0x131518), &brBg_);
    rt_->CreateSolidColorBrush(ColHex(0x1D2025), &brCard_);
    rt_->CreateSolidColorBrush(ColHex(0x272B33), &brCardHover_);
    rt_->CreateSolidColorBrush(ColHex(0x323741), &brBorder_);
    rt_->CreateSolidColorBrush(ColHex(0xED1C24), &brAccent_);
    rt_->CreateSolidColorBrush(ColHex(0xED1C24, 0.22f), &brAccentSoft_);
    rt_->CreateSolidColorBrush(ColHex(0x3A3F4B), &brTrackBg_);
    rt_->CreateSolidColorBrush(ColHex(0xF1F5F9), &brText_);
    rt_->CreateSolidColorBrush(ColHex(0x94A3B8), &brTextMuted_);
    rt_->CreateSolidColorBrush(ColHex(0xF87171), &brDanger_);
    return true;
}

void SidebarUI::DiscardRenderResources() {
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
    rt_.Reset();
}

void SidebarUI::SetAdapterName(const std::wstring& adapterName) {
    adapterName_ = adapterName.empty() ? L"Universal GPU" : adapterName;
    Invalidate();
}

void SidebarUI::SetDisplayHdrInfo(const DisplayHdrInfo& info) {
    if (info.hdrActive == displayHdr_.hdrActive &&
        std::abs(info.maxLuminanceNits - displayHdr_.maxLuminanceNits) < 1.0f &&
        std::abs(info.sdrWhiteNits - displayHdr_.sdrWhiteNits) < 1.0f) {
        return;
    }
    displayHdr_ = info;
    Invalidate();
}

void SidebarUI::SetTelemetry(const FrameGenTelemetry& tel) {
    bool changed = (tel.fgRunning != telemetry_.fgRunning) ||
                   (tel.monitorHz != telemetry_.monitorHz) ||
                   (std::abs(tel.sourceFps - telemetry_.sourceFps) >= 1.0f) ||
                   (std::abs(tel.outputFps - telemetry_.outputFps) >= 1.0f);
    if (!changed) return;
    telemetry_ = tel;
    if (IsVisible()) {
        Invalidate();
    }
}

void SidebarUI::SetActiveAppInfo(const std::wstring& exeName, const std::wstring& displayTitle, bool isGame, const std::wstring& engineTag) {
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

void SidebarUI::SetExcludeFromCapture(bool exclude) {
    if (hwnd_) {
        SetWindowDisplayAffinity(hwnd_, exclude ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);
    }
}

void SidebarUI::Toggle() {
    SetOpen(!targetOpen_);
}

void SidebarUI::SetOpen(bool open) {
    if (targetOpen_ == open) return;
    targetOpen_ = open;
    lastTickMs_ = GetTickCount64();

    if (open) {
        // Show without activating so fullscreen/borderless games never minimize or Alt-Tab!
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        UpdateWindowPosition();
        Invalidate();
    } else {
        draggingSlider_ = false;
        if (GetCapture() == hwnd_) ReleaseCapture();
    }
}

void SidebarUI::UpdateWindowPosition() {
    if (!hwnd_) return;
    int physW = static_cast<int>(std::lround(sidebarDipW_ * dpiScale_));
    int physH = screenPhysH_;
    float eased = 1.0f - std::pow(1.0f - animProgress_, 3.0f);
    int x = static_cast<int>(std::lround(-static_cast<float>(physW) * (1.0f - eased)));

    SetWindowPos(
        hwnd_,
        HWND_TOPMOST,
        x, 0, physW, physH,
        SWP_NOACTIVATE
    );
    if (rt_) {
        rt_->Resize(D2D1::SizeU(static_cast<UINT>(physW), static_cast<UINT>(physH)));
    }
}

void SidebarUI::TickAnimation() {
    if (!hwnd_) return;
    ULONGLONG now = GetTickCount64();
    float dt = (lastTickMs_ == 0) ? 0.016f : static_cast<float>(std::min<ULONGLONG>(now - lastTickMs_, 50)) / 1000.0f;
    lastTickMs_ = now;

    float target = targetOpen_ ? 1.0f : 0.0f;
    if (std::abs(animProgress_ - target) > 0.001f) {
        float speed = 6.8f;
        if (animProgress_ < target) {
            animProgress_ = std::min(target, animProgress_ + dt * speed);
        } else {
            animProgress_ = std::max(target, animProgress_ - dt * speed);
        }
        UpdateWindowPosition();
        Render();
        if (!targetOpen_ && animProgress_ <= 0.001f) {
            animProgress_ = 0.0f;
            ShowWindow(hwnd_, SW_HIDE);
        }
    }
}

void SidebarUI::Invalidate() {
    if (hwnd_ && IsWindowVisible(hwnd_)) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void SidebarUI::Render() {
    if (!hwnd_ || !config_) return;
    if (!CreateRenderResources()) return;

    hitRects_.clear();
    rt_->BeginDraw();
    rt_->Clear(ColHex(0x131518));

    const float W = sidebarDipW_;
    const float H = sidebarDipH_;
    const float pad = 16.0f;
    const float cardW = W - pad * 2.0f;
    const float mx = pad + 12.0f;
    const float mw = cardW - 24.0f;

    auto drawText = [&](const std::wstring& txt, IDWriteTextFormat* fmt, D2D1_RECT_F r, ID2D1SolidColorBrush* br) {
        if (!fmt || !br) return;
        rt_->DrawTextW(txt.c_str(), static_cast<UINT32>(txt.size()), fmt, r, br, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };

    // --- Fixed Top Header ---
    const float headerH = 74.0f;
    rt_->FillRectangle(D2D1::RectF(0, 0, W, headerH), brBg_.Get());

    drawText(L"Game Filters", fmtTitle_.Get(), D2D1::RectF(pad, 14, W - 54, 38), brText_.Get());
    std::wstring hkStr = FormatHotkeyString(config_->sidebarHotkeyMods, config_->sidebarHotkeyVk);
    std::wstring appStatusLine = activeAppIsGame_
        ? (L"Playing: " + activeAppTitle_ + L"  \u2022  " + hkStr)
        : (L"Ready  \u2022  Press " + hkStr + L" to toggle");
    drawText(appStatusLine, fmtSub_.Get(), D2D1::RectF(pad, 40, W - 54, 62),
             activeAppIsGame_ ? brAccent_.Get() : brTextMuted_.Get());

    D2D1_RECT_F closeRect = D2D1::RectF(W - 44, 16, W - 16, 44);
    rt_->FillRoundedRectangle(D2D1::RoundedRect(closeRect, 6, 6), brCard_.Get());
    float xPad = 8.5f;
    rt_->DrawLine(D2D1::Point2F(closeRect.left + xPad, closeRect.top + xPad),
                  D2D1::Point2F(closeRect.right - xPad, closeRect.bottom - xPad), brText_.Get(), 1.8f);
    rt_->DrawLine(D2D1::Point2F(closeRect.right - xPad, closeRect.top + xPad),
                  D2D1::Point2F(closeRect.left + xPad, closeRect.bottom - xPad), brText_.Get(), 1.8f);
    hitRects_.push_back({ CTRL_CLOSE_BTN, closeRect, false });

    rt_->DrawLine(D2D1::Point2F(0, headerH), D2D1::Point2F(W, headerH), brBorder_.Get(), 1.0f);

    // --- Scrollable Content Area ---
    const float footerH = 62.0f;
    D2D1_RECT_F scrollClip = D2D1::RectF(0, headerH + 1, W, H - footerH);
    rt_->PushAxisAlignedClip(scrollClip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    float y = headerH + 14.0f - scrollY_;

    auto addHit = [&](ControlKind k, D2D1_RECT_F r, bool isSlider = false) {
        if (r.bottom >= headerH && r.top <= H - footerH) {
            hitRects_.push_back({ k, r, isSlider });
        }
    };

    auto drawTogglePill = [&](ControlKind k, float x, float ty, bool on) {
        D2D1_RECT_F pr = D2D1::RectF(x, ty, x + 36, ty + 18);
        rt_->FillRoundedRectangle(D2D1::RoundedRect(pr, 9, 9), on ? brAccent_.Get() : brTrackBg_.Get());
        float kx = on ? (pr.right - 9.0f) : (pr.left + 9.0f);
        rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kx, (pr.top + pr.bottom) * 0.5f), 6.5f, 6.5f), brText_.Get());
        addHit(k, pr);
    };

    auto drawRangeSlider = [&](ControlKind k, const std::wstring& label, int val, int minVal, int maxVal,
                               float sx, float& sy, float sw, bool showCenterTick = false, float numBoxW = 42.0f) {
        drawText(label, fmtBody_.Get(), D2D1::RectF(sx, sy, sx + sw - numBoxW - 6.0f, sy + 20), brText_.Get());

        D2D1_RECT_F numR = D2D1::RectF(sx + sw - numBoxW, sy, sx + sw, sy + 20);
        rt_->FillRoundedRectangle(D2D1::RoundedRect(numR, 4, 4), brBg_.Get());
        rt_->DrawRoundedRectangle(D2D1::RoundedRect(numR, 4, 4), brBorder_.Get(), 1.0f);
        drawText(std::to_wstring(val), fmtCenter_.Get(), numR, brText_.Get());

        sy += 24.0f;
        D2D1_RECT_F track = D2D1::RectF(sx, sy + 7.0f, sx + sw, sy + 11.0f);
        rt_->FillRoundedRectangle(D2D1::RoundedRect(track, 2, 2), brTrackBg_.Get());

        int span = std::max(maxVal - minVal, 1);
        float norm = static_cast<float>(std::clamp(val, minVal, maxVal) - minVal) / static_cast<float>(span);
        float fillX = track.left + (track.right - track.left) * norm;
        D2D1_RECT_F fillR = D2D1::RectF(track.left, track.top, fillX, track.bottom);
        rt_->FillRoundedRectangle(D2D1::RoundedRect(fillR, 2, 2), brAccent_.Get());

        if (showCenterTick) {
            float midX = (track.left + track.right) * 0.5f;
            rt_->DrawLine(D2D1::Point2F(midX, track.top - 3), D2D1::Point2F(midX, track.bottom + 3), brBorder_.Get(), 1.0f);
        }

        rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(fillX, (track.top + track.bottom) * 0.5f), 7.0f, 7.0f), brText_.Get());
        rt_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(fillX, (track.top + track.bottom) * 0.5f), 7.0f, 7.0f), brAccent_.Get(), 2.0f);

        addHit(k, D2D1::RectF(sx - 6, sy - 2, sx + sw + 6, sy + 20), true);
        sy += 28.0f;
    };

    auto drawSlider = [&](ControlKind k, const std::wstring& label, int val, float sx, float& sy, float sw, bool showCenterTick = false) {
        drawRangeSlider(k, label, val, 0, 100, sx, sy, sw, showCenterTick, 42.0f);
    };

    auto drawFilterCardHeader = [&](const std::wstring& title, const std::wstring& sub, bool enabled,
                                    ControlKind toggleKind, ControlKind resetKind, float cardY, bool accentTitle) {
        if (enabled) {
            drawText(title, fmtSection_.Get(), D2D1::RectF(mx, cardY, mx + mw - 104, cardY + 22),
                     accentTitle ? brAccent_.Get() : brText_.Get());
            D2D1_RECT_F rstR = D2D1::RectF(mx + mw - 98, cardY - 1, mx + mw - 44, cardY + 21);
            rt_->FillRoundedRectangle(D2D1::RoundedRect(rstR, 4, 4), brBg_.Get());
            rt_->DrawRoundedRectangle(D2D1::RoundedRect(rstR, 4, 4), brBorder_.Get(), 1.0f);
            drawText(L"Reset", fmtCenter_.Get(), rstR, brTextMuted_.Get());
            addHit(resetKind, rstR);
        } else {
            drawText(title, fmtBody_.Get(), D2D1::RectF(mx, cardY - 3, mx + mw - 46, cardY + 15), brText_.Get());
            drawText(sub, fmtSmall_.Get(), D2D1::RectF(mx, cardY + 14, mx + mw - 46, cardY + 30), brTextMuted_.Get());
        }
        drawTogglePill(toggleKind, mx + mw - 36, cardY + 1, enabled);
    };

    // --- Section: Profiles (None, 1, 2, 3) ---
    drawText(L"Profiles", fmtSection_.Get(), D2D1::RectF(pad, y, W - pad, y + 20), brText_.Get());
    y += 24.0f;

    const wchar_t* profNames[4] = { L"None", L"1", L"2", L"3" };
    ControlKind profKinds[4] = { CTRL_PROFILE_0, CTRL_PROFILE_1, CTRL_PROFILE_2, CTRL_PROFILE_3 };
    float gap = 8.0f;
    float pCardW = (cardW - gap * 3.0f) / 4.0f;
    for (int i = 0; i < 4; ++i) {
        float px = pad + i * (pCardW + gap);
        D2D1_RECT_F pr = D2D1::RectF(px, y, px + pCardW, y + 46.0f);
        bool sel = (config_->activeProfile == i);
        rt_->FillRoundedRectangle(D2D1::RoundedRect(pr, 6, 6), sel ? brAccentSoft_.Get() : brCard_.Get());
        rt_->DrawRoundedRectangle(D2D1::RoundedRect(pr, 6, 6), sel ? brAccent_.Get() : brBorder_.Get(), sel ? 2.0f : 1.0f);
        drawText(profNames[i], fmtCenter_.Get(), pr, sel ? brText_.Get() : brTextMuted_.Get());
        addHit(profKinds[i], pr);
    }
    y += 56.0f;

    // Compact Quick-Switch Scaling & Frame Gen Card
    {
        float cardH = 112.0f;
        bool activeEng = (config_->scaleMode != SCALE_OFF || config_->fgMode != FG_OFF);
        D2D1_RECT_F sCard = D2D1::RectF(pad, y, pad + cardW, y + cardH);
        rt_->FillRoundedRectangle(D2D1::RoundedRect(sCard, 8, 8), brCard_.Get());
        rt_->DrawRoundedRectangle(D2D1::RoundedRect(sCard, 8, 8), activeEng ? brAccent_.Get() : brBorder_.Get(), 1.0f);

        float cy = y + 10.0f;
        drawText(L"Scaling & Frame Gen", fmtSection_.Get(), D2D1::RectF(mx, cy, mx + 175.0f, cy + 20.0f), brText_.Get());
        std::wstring fpsBadge = (telemetry_.fgRunning && telemetry_.outputFps > 1.0f)
            ? (std::to_wstring(static_cast<int>(std::lround(telemetry_.sourceFps))) + L" -> " + std::to_wstring(static_cast<int>(std::lround(telemetry_.outputFps))) + L" FPS")
            : L"Full settings in Hub";
        drawText(fpsBadge, fmtSmall_.Get(), D2D1::RectF(mx + 180.0f, cy + 2.0f, mx + mw, cy + 18.0f),
                 telemetry_.fgRunning ? brAccent_.Get() : brTextMuted_.Get());
        cy += 24.0f;

        auto drawCompactRow = [&](const wchar_t* rowLabel, const wchar_t* const btnLabels[5], ControlKind baseKind, int curVal) {
            drawText(rowLabel, fmtSmall_.Get(), D2D1::RectF(mx, cy + 5.0f, mx + 64.0f, cy + 23.0f), brTextMuted_.Get());
            float rx = mx + 66.0f;
            float rw = mw - 66.0f;
            float bgap = 4.0f;
            float bw = (rw - bgap * 4.0f) / 5.0f;
            for (int i = 0; i < 5; ++i) {
                float bx = rx + static_cast<float>(i) * (bw + bgap);
                D2D1_RECT_F br = D2D1::RectF(bx, cy, bx + bw, cy + 26.0f);
                bool sel = (curVal == i);
                rt_->FillRoundedRectangle(D2D1::RoundedRect(br, 4, 4), sel ? brAccentSoft_.Get() : brBg_.Get());
                rt_->DrawRoundedRectangle(D2D1::RoundedRect(br, 4, 4), sel ? brAccent_.Get() : brBorder_.Get(), sel ? 1.4f : 1.0f);
                drawText(btnLabels[i], fmtCenter_.Get(), br, sel ? brText_.Get() : brTextMuted_.Get());
                addHit(static_cast<ControlKind>(static_cast<int>(baseKind) + i), br);
            }
            cy += 34.0f;
        };

        const wchar_t* upLabels[5] = { L"Off", L"FSR", L"NIS", L"Int", L"A4K" };
        const wchar_t* fgLabels[5] = { L"Off", L"2x", L"3x", L"4x", L"Auto" };
        drawCompactRow(L"Upscaler", upLabels, CTRL_SB_SCALE_0, config_->scaleMode);
        drawCompactRow(L"Frame Gen", fgLabels, CTRL_SB_FG_0, config_->fgMode);
        y += cardH + 10.0f;
    }

    // Split Preview Card (Live In-Game Before / After Compare + Quick Resize)
    {
        bool splitOn = config_->splitPreview;
        float sCardH = splitOn ? 96.0f : 54.0f;
        D2D1_RECT_F spCard = D2D1::RectF(pad, y, pad + cardW, y + sCardH);
        rt_->FillRoundedRectangle(D2D1::RoundedRect(spCard, 8, 8), brCard_.Get());
        rt_->DrawRoundedRectangle(D2D1::RoundedRect(spCard, 8, 8), splitOn ? brAccent_.Get() : brBorder_.Get(), 1.0f);

        float cy = y + 10.0f;
        if (!splitOn) {
            drawText(L"Split Preview", fmtBody_.Get(), D2D1::RectF(mx, cy - 1.0f, mx + mw - 46.0f, cy + 17.0f), brText_.Get());
            drawText(L"Compare original vs filtered (Alt+[ / ])", fmtSmall_.Get(), D2D1::RectF(mx, cy + 17.0f, mx + mw - 46.0f, cy + 35.0f), brTextMuted_.Get());
            drawTogglePill(CTRL_SB_TOGGLE_SPLIT, mx + mw - 36.0f, y + 18.0f, false);
        } else {
            drawText(L"Split Preview", fmtSection_.Get(), D2D1::RectF(mx, cy, mx + mw - 46.0f, cy + 20.0f), brAccent_.Get());
            drawTogglePill(CTRL_SB_TOGGLE_SPLIT, mx + mw - 36.0f, cy + 2.0f, true);

            float rowY = y + 38.0f;
            D2D1_RECT_F decR = D2D1::RectF(mx, rowY, mx + 26.0f, rowY + 24.0f);
            rt_->FillRoundedRectangle(D2D1::RoundedRect(decR, 4, 4), brBg_.Get());
            rt_->DrawRoundedRectangle(D2D1::RoundedRect(decR, 4, 4), brBorder_.Get(), 1.0f);
            drawText(L"-", fmtCenter_.Get(), decR, brText_.Get());
            addHit(CTRL_SB_SPLIT_DEC, decR);

            float sLeft = mx + 34.0f;
            float sRight = mx + mw - 76.0f;
            D2D1_RECT_F track = D2D1::RectF(sLeft, rowY + 10.0f, sRight, rowY + 14.0f);
            rt_->FillRoundedRectangle(D2D1::RoundedRect(track, 2, 2), brTrackBg_.Get());
            float norm = std::clamp(config_->splitPosition, 0.05f, 0.95f);
            float fillX = track.left + (track.right - track.left) * norm;
            rt_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(track.left, track.top, fillX, track.bottom), 2, 2), brAccent_.Get());
            float midX = (track.left + track.right) * 0.5f;
            rt_->DrawLine(D2D1::Point2F(midX, track.top - 3.0f), D2D1::Point2F(midX, track.bottom + 3.0f), brBorder_.Get(), 1.0f);
            rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(fillX, rowY + 12.0f), 7.0f, 7.0f), brText_.Get());
            rt_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(fillX, rowY + 12.0f), 7.0f, 7.0f), brAccent_.Get(), 2.0f);
            addHit(CTRL_SB_SLIDER_SPLIT, D2D1::RectF(sLeft - 6.0f, rowY, sRight + 6.0f, rowY + 24.0f), true);

            D2D1_RECT_F incR = D2D1::RectF(mx + mw - 68.0f, rowY, mx + mw - 42.0f, rowY + 24.0f);
            rt_->FillRoundedRectangle(D2D1::RoundedRect(incR, 4, 4), brBg_.Get());
            rt_->DrawRoundedRectangle(D2D1::RoundedRect(incR, 4, 4), brBorder_.Get(), 1.0f);
            drawText(L"+", fmtCenter_.Get(), incR, brText_.Get());
            addHit(CTRL_SB_SPLIT_INC, incR);

            int splitPct = static_cast<int>(std::lround(norm * 100.0f));
            D2D1_RECT_F pctR = D2D1::RectF(mx + mw - 38.0f, rowY, mx + mw, rowY + 24.0f);
            rt_->FillRoundedRectangle(D2D1::RoundedRect(pctR, 4, 4), brBg_.Get());
            rt_->DrawRoundedRectangle(D2D1::RoundedRect(pctR, 4, 4), brBorder_.Get(), 1.0f);
            drawText(std::to_wstring(splitPct) + L"%", fmtCenter_.Get(), pctR, brText_.Get());

            drawText(L"Alt+[ / ] resize split  \u2022  Alt+\\ toggle", fmtSmall_.Get(),
                     D2D1::RectF(mx, y + 69.0f, mx + mw, y + 87.0f), brTextMuted_.Get());
        }
        y += sCardH + 10.0f;
    }

    int profIdx = (config_->activeProfile >= 1 && config_->activeProfile <= 3) ? (config_->activeProfile - 1) : 0;
    FilterParams& fp = config_->profiles[profIdx];

    if (config_->activeProfile == 0) {
        D2D1_RECT_F offCard = D2D1::RectF(pad, y, pad + cardW, y + 66.0f);
        rt_->FillRoundedRectangle(D2D1::RoundedRect(offCard, 8, 8), brCard_.Get());
        rt_->DrawRoundedRectangle(D2D1::RoundedRect(offCard, 8, 8), brBorder_.Get(), 1.0f);
        drawText(L"Color filters bypassed.\nSelect Profile 1, 2, or 3 above to enable visual filters.",
                 fmtSub_.Get(), D2D1::RectF(mx, y + 14, mx + mw, y + 56), brTextMuted_.Get());
        y += 76.0f;
    } else {
        // --- Unified Filters List (all 16 modular filters aligned seamlessly) ---
        drawText(L"Filters", fmtSection_.Get(), D2D1::RectF(pad, y, W - pad, y + 20), brText_.Get());
        y += 24.0f;

        {
            float cH = fp.enableDynamicVibrance ? 154.0f : 46.0f;
            D2D1_RECT_F cR = D2D1::RectF(pad, y, pad + cardW, y + cH);
            rt_->FillRoundedRectangle(D2D1::RoundedRect(cR, 8, 8), brCard_.Get());
            rt_->DrawRoundedRectangle(D2D1::RoundedRect(cR, 8, 8),
                                      fp.enableDynamicVibrance ? brAccent_.Get() : brBorder_.Get(), 1.0f);

            float cy = y + 12.0f;
            drawFilterCardHeader(
                L"AI Dynamic Vibrance",
                L"Adaptive perceptual Oklab color and contrast engine",
                fp.enableDynamicVibrance,
                CTRL_TOGGLE_VIBRANCE,
                CTRL_RESET_VIBRANCE,
                cy,
                true
            );
            if (fp.enableDynamicVibrance) {
                cy += 30.0f;
                drawSlider(CTRL_SLIDER_INTENSITY, L"Intensity (%)", fp.intensity, mx, cy, mw);
                drawSlider(CTRL_SLIDER_SATURATION, L"Saturation Boost (%)", fp.saturationBoost, mx, cy, mw);
            }
            y += cH + 8.0f;
        }

        auto drawOptSingle = [&](const std::wstring& title, const std::wstring& sub, const std::wstring& sLabel,
                                 bool enabled, int val, ControlKind toggleKind, ControlKind resetKind, ControlKind sliderKind) {
            float cH = enabled ? 102.0f : 46.0f;
            D2D1_RECT_F cR = D2D1::RectF(pad, y, pad + cardW, y + cH);
            rt_->FillRoundedRectangle(D2D1::RoundedRect(cR, 8, 8), brCard_.Get());
            rt_->DrawRoundedRectangle(D2D1::RoundedRect(cR, 8, 8), brBorder_.Get(), 1.0f);

            float cy = y + 12.0f;
            drawFilterCardHeader(title, sub, enabled, toggleKind, resetKind, cy, false);
            if (enabled) {
                cy += 30.0f;
                drawSlider(sliderKind, sLabel, val, mx, cy, mw);
            }
            y += cH + 8.0f;
        };

        auto drawOptDouble = [&](const std::wstring& title, const std::wstring& sub,
                                 const std::wstring& l1, int v1, ControlKind k1,
                                 const std::wstring& l2, int v2, ControlKind k2,
                                 bool enabled, ControlKind toggleKind, ControlKind resetKind, bool centerTick) {
            float cH = enabled ? 154.0f : 46.0f;
            D2D1_RECT_F cR = D2D1::RectF(pad, y, pad + cardW, y + cH);
            rt_->FillRoundedRectangle(D2D1::RoundedRect(cR, 8, 8), brCard_.Get());
            rt_->DrawRoundedRectangle(D2D1::RoundedRect(cR, 8, 8), brBorder_.Get(), 1.0f);

            float cy = y + 12.0f;
            drawFilterCardHeader(title, sub, enabled, toggleKind, resetKind, cy, false);
            if (enabled) {
                cy += 30.0f;
                drawSlider(k1, l1, v1, mx, cy, mw, centerTick);
                drawSlider(k2, l2, v2, mx, cy, mw, centerTick);
            }
            y += cH + 8.0f;
        };

        drawOptSingle(L"Sharpen+ (AMD CAS)", L"Contrast-adaptive edge sharpening for TAA reconstruction", L"Sharpness (%)",
                      fp.enableSharpen, fp.sharpen, CTRL_TOGGLE_SHARPEN, CTRL_RESET_SHARPEN, CTRL_SLIDER_SHARPEN);

        // Dynamic HDR card (scRGB HDR10 when Windows HDR is active, or Local Tone Mapping in SDR)
        {
            float cH = fp.enableDynamicHdr ? 232.0f : 46.0f;
            D2D1_RECT_F cR = D2D1::RectF(pad, y, pad + cardW, y + cH);
            rt_->FillRoundedRectangle(D2D1::RoundedRect(cR, 8, 8), brCard_.Get());
            rt_->DrawRoundedRectangle(D2D1::RoundedRect(cR, 8, 8),
                                      (fp.enableDynamicHdr && displayHdr_.hdrActive) ? brAccent_.Get() : brBorder_.Get(), 1.0f);

            float cy = y + 12.0f;
            std::wstring hdrSub = displayHdr_.hdrActive
                ? L"Active 16-bit scRGB HDR10 display pipeline"
                : L"Adaptive local tone mapping and highlight expansion";
            drawFilterCardHeader(L"Dynamic HDR", hdrSub, fp.enableDynamicHdr, CTRL_TOGGLE_HDR, CTRL_RESET_HDR, cy, displayHdr_.hdrActive);
            if (fp.enableDynamicHdr) {
                cy += 28.0f;
                D2D1_RECT_F pillR = D2D1::RectF(mx, cy, mx + mw, cy + 22.0f);
                rt_->FillRoundedRectangle(D2D1::RoundedRect(pillR, 4, 4), displayHdr_.hdrActive ? brAccentSoft_.Get() : brBg_.Get());
                rt_->DrawRoundedRectangle(D2D1::RoundedRect(pillR, 4, 4), displayHdr_.hdrActive ? brAccent_.Get() : brBorder_.Get(), 1.0f);
                std::wstring badgeLine = displayHdr_.hdrActive
                    ? (L"scRGB HDR10 Active  \u2022  Display Peak " + std::to_wstring(static_cast<int>(std::lround(displayHdr_.maxLuminanceNits))) + L" nits")
                    : L"Local SDR Tone Mapping  \u2022  Windows HDR Off";
                drawText(badgeLine, fmtSmall_.Get(), D2D1::RectF(pillR.left + 8, pillR.top + 3.5f, pillR.right - 8, pillR.bottom),
                         displayHdr_.hdrActive ? brText_.Get() : brTextMuted_.Get());
                cy += 30.0f;

                drawSlider(CTRL_SLIDER_HDR, L"HDR Intensity (%)", fp.dynamicHdr, mx, cy, mw);
                drawRangeSlider(CTRL_SLIDER_HDR_PEAK, L"Peak Brightness (nits)", fp.hdrPeakNits, 300, 1500, mx, cy, mw, false, 50.0f);
                drawSlider(CTRL_SLIDER_HDR_MIDGRAY, L"Mid Gray / Contrast (%)", fp.hdrMidGray, mx, cy, mw, true);
            }
            y += cH + 8.0f;
        }

        drawOptSingle(L"Dark / Shadow Lighting", L"Perceptual shadow recovery with deep black preservation", L"Shadow Boost (%)",
                      fp.enableDarkLighting, fp.darkLighting, CTRL_TOGGLE_DARK, CTRL_RESET_DARK, CTRL_SLIDER_DARK);
        drawOptDouble(L"Ambient Occlusion", L"Screen-space crevice depth and contact shading",
                      L"Intensity (%)", fp.ambientOcclusion, CTRL_SLIDER_AO_INTENSITY,
                      L"Radius (%)", fp.aoRadius, CTRL_SLIDER_AO_RADIUS,
                      fp.enableAmbientOcclusion, CTRL_TOGGLE_AO, CTRL_RESET_AO, false);
        drawOptSingle(L"Clarity / Details", L"Dual-scale edge-preserving local micro-contrast", L"Clarity (%)",
                      fp.enableClarity, fp.clarity, CTRL_TOGGLE_CLARITY, CTRL_RESET_CLARITY, CTRL_SLIDER_CLARITY);

        drawOptDouble(L"Cinematic Bloom", L"Soft-knee optical glow for neon, fire, and highlights",
                      L"Intensity (%)", fp.bloomIntensity, CTRL_SLIDER_BLOOM_INT,
                      L"Threshold (%)", fp.bloomThreshold, CTRL_SLIDER_BLOOM_THRESH,
                      fp.enableBloom, CTRL_TOGGLE_BLOOM, CTRL_RESET_BLOOM, false);

        // Filmic Grading (3-button selector ACES / AgX / Technicolor + strength slider)
        {
            float cH = fp.enableFilmic ? 136.0f : 46.0f;
            D2D1_RECT_F cR = D2D1::RectF(pad, y, pad + cardW, y + cH);
            rt_->FillRoundedRectangle(D2D1::RoundedRect(cR, 8, 8), brCard_.Get());
            rt_->DrawRoundedRectangle(D2D1::RoundedRect(cR, 8, 8), brBorder_.Get(), 1.0f);

            float cy = y + 12.0f;
            drawFilterCardHeader(L"Filmic Grading", L"Cinema tone mapping with ACES, AgX, and Technicolor",
                                 fp.enableFilmic, CTRL_TOGGLE_FILMIC, CTRL_RESET_FILMIC, cy, false);
            if (fp.enableFilmic) {
                cy += 30.0f;
                const wchar_t* fNames[3] = { L"ACES", L"AgX", L"Technicolor" };
                ControlKind fKinds[3] = { CTRL_FILMIC_MODE_0, CTRL_FILMIC_MODE_1, CTRL_FILMIC_MODE_2 };
                float bGap = 6.0f;
                float bW = (mw - bGap * 2.0f) / 3.0f;
                for (int i = 0; i < 3; ++i) {
                    float bx = mx + i * (bW + bGap);
                    D2D1_RECT_F br = D2D1::RectF(bx, cy, bx + bW, cy + 26.0f);
                    bool sel = (fp.filmicMode == i);
                    rt_->FillRoundedRectangle(D2D1::RoundedRect(br, 4, 4), sel ? brAccentSoft_.Get() : brBg_.Get());
                    rt_->DrawRoundedRectangle(D2D1::RoundedRect(br, 4, 4), sel ? brAccent_.Get() : brBorder_.Get(), sel ? 1.5f : 1.0f);
                    drawText(fNames[i], fmtCenter_.Get(), br, sel ? brText_.Get() : brTextMuted_.Get());
                    addHit(fKinds[i], br);
                }
                cy += 34.0f;
                drawSlider(CTRL_SLIDER_FILMIC_STR, L"Grading Strength (%)", fp.filmicStrength, mx, cy, mw);
            }
            y += cH + 8.0f;
        }

        drawOptSingle(L"Deband", L"Edge-aware gradient smoothing for 8-bit sky and shadows", L"Strength (%)",
                      fp.enableDeband, fp.debandStrength, CTRL_TOGGLE_DEBAND, CTRL_RESET_DEBAND, CTRL_SLIDER_DEBAND);

        drawOptDouble(L"Brightness / Contrast", L"Filmic luminance gain and tonal contrast curve",
                      L"Brightness (%)", fp.brightness, CTRL_SLIDER_BRIGHTNESS,
                      L"Contrast (%)", fp.contrast, CTRL_SLIDER_CONTRAST,
                      fp.enableBrightnessContrast, CTRL_TOGGLE_BC, CTRL_RESET_BC, true);

        drawOptDouble(L"Color Temperature & Tint", L"Luminance-preserved white balance and tint calibration",
                      L"Temperature (%)", fp.temperature, CTRL_SLIDER_TEMPERATURE,
                      L"Tint (%)", fp.tint, CTRL_SLIDER_TINT,
                      fp.enableColorTemp, CTRL_TOGGLE_COLORTEMP, CTRL_RESET_COLORTEMP, true);

        drawOptDouble(L"Gamma / Exposure", L"Midtone transfer curve and photographic EV compensation",
                      L"Exposure (%)", fp.exposure, CTRL_SLIDER_EXPOSURE,
                      L"Gamma (%)", fp.gamma, CTRL_SLIDER_GAMMA,
                      fp.enableGammaExposure, CTRL_TOGGLE_GAMMAEXP, CTRL_RESET_GAMMAEXP, true);

        drawOptSingle(L"Night Mode / Blue Light", L"Spectral blue-light attenuation for low-light viewing", L"Warmth (%)",
                      fp.enableNightMode, fp.nightMode, CTRL_TOGGLE_NIGHT, CTRL_RESET_NIGHT, CTRL_SLIDER_NIGHT);

        drawOptSingle(L"Black & White", L"Luminance-weighted Rec.709 monochrome conversion", L"Intensity (%)",
                      fp.enableBlackAndWhite, fp.blackAndWhite, CTRL_TOGGLE_BW, CTRL_RESET_BW, CTRL_SLIDER_BW);

        drawOptSingle(L"Cinematic Vignette", L"Optical lens peripheral falloff and edge framing", L"Strength (%)",
                      fp.enableVignette, fp.vignette, CTRL_TOGGLE_VIGNETTE, CTRL_RESET_VIGNETTE, CTRL_SLIDER_VIGNETTE);

        // 16. Colorblind Assist (3-button mode selector + strength slider)
        {
            float cH = fp.enableColorblind ? 136.0f : 46.0f;
            D2D1_RECT_F cR = D2D1::RectF(pad, y, pad + cardW, y + cH);
            rt_->FillRoundedRectangle(D2D1::RoundedRect(cR, 8, 8), brCard_.Get());
            rt_->DrawRoundedRectangle(D2D1::RoundedRect(cR, 8, 8), brBorder_.Get(), 1.0f);

            float cy = y + 12.0f;
            drawFilterCardHeader(L"Colorblind Assist", L"LMS cone-space daltonization for color vision support",
                                 fp.enableColorblind, CTRL_TOGGLE_COLORBLIND, CTRL_RESET_COLORBLIND, cy, false);
            if (fp.enableColorblind) {
                cy += 30.0f;
                const wchar_t* cbNames[3] = { L"Protan", L"Deutan", L"Tritan" };
                ControlKind cbKinds[3] = { CTRL_CB_TYPE_0, CTRL_CB_TYPE_1, CTRL_CB_TYPE_2 };
                float bGap = 6.0f;
                float bW = (mw - bGap * 2.0f) / 3.0f;
                for (int i = 0; i < 3; ++i) {
                    float bx = mx + i * (bW + bGap);
                    D2D1_RECT_F br = D2D1::RectF(bx, cy, bx + bW, cy + 26.0f);
                    bool sel = (fp.colorblindType == i);
                    rt_->FillRoundedRectangle(D2D1::RoundedRect(br, 4, 4), sel ? brAccentSoft_.Get() : brBg_.Get());
                    rt_->DrawRoundedRectangle(D2D1::RoundedRect(br, 4, 4), sel ? brAccent_.Get() : brBorder_.Get(), sel ? 1.5f : 1.0f);
                    drawText(cbNames[i], fmtCenter_.Get(), br, sel ? brText_.Get() : brTextMuted_.Get());
                    addHit(cbKinds[i], br);
                }
                cy += 34.0f;
                drawSlider(CTRL_SLIDER_CBSTRENGTH, L"Assist Strength (%)", fp.colorblindStrength, mx, cy, mw);
            }
            y += cH + 12.0f;
        }
    }

    float totalContentH = (y + scrollY_) - (headerH + 14.0f);
    float viewH = (H - footerH) - (headerH + 1.0f);
    maxScrollY_ = std::max(0.0f, totalContentH - viewH);
    scrollY_ = std::clamp(scrollY_, 0.0f, maxScrollY_);

    rt_->PopAxisAlignedClip();

    // --- Fixed Footer ---
    rt_->FillRectangle(D2D1::RectF(0, H - footerH, W, H), brBg_.Get());
    rt_->DrawLine(D2D1::Point2F(0, H - footerH), D2D1::Point2F(W, H - footerH), brBorder_.Get(), 1.0f);

    std::wstring gpuLine = L"GPU: " + adapterName_ + (displayHdr_.hdrActive ? L" \u2022 HDR" : L"");
    drawText(gpuLine, fmtSmall_.Get(), D2D1::RectF(pad, H - footerH + 10, W - 118, H - footerH + 28), brTextMuted_.Get());
    if (activeAppIsGame_) {
        drawText(L"Game: " + activeAppTitle_ + L" (" + activeEngineTag_ + L")", fmtSmall_.Get(),
                 D2D1::RectF(pad, H - footerH + 28, W - 118, H - 8), brAccent_.Get());
    } else {
        drawText(L"Game: Waiting for game...", fmtSmall_.Get(),
                 D2D1::RectF(pad, H - footerH + 28, W - 118, H - 8), brTextMuted_.Get());
    }

    D2D1_RECT_F hubR = D2D1::RectF(W - 108, H - footerH + 15, W - pad, H - 15);
    rt_->FillRoundedRectangle(D2D1::RoundedRect(hubR, 5, 5), brCard_.Get());
    rt_->DrawRoundedRectangle(D2D1::RoundedRect(hubR, 5, 5), brBorder_.Get(), 1.0f);
    drawText(L"Control Hub", fmtCenter_.Get(), hubR, brText_.Get());
    hitRects_.push_back({ CTRL_BTN_OPEN_HUB, hubR, false });

    HRESULT hr = rt_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardRenderResources();
    }
}

void SidebarUI::UpdateSliderFromX(ControlKind kind, float dipX, const D2D1_RECT_F& trackRect) {
    if (!config_) return;
    float left = trackRect.left + 6.0f;
    float right = trackRect.right - 6.0f;
    float norm = std::clamp((dipX - left) / std::max(right - left, 1.0f), 0.0f, 1.0f);
    int val = static_cast<int>(std::lround(norm * 100.0f));

    int profIdx = (config_->activeProfile >= 1 && config_->activeProfile <= 3) ? (config_->activeProfile - 1) : 0;
    FilterParams& fp = config_->profiles[profIdx];
    switch (kind) {
        case CTRL_SLIDER_INTENSITY:    fp.intensity = val; break;
        case CTRL_SLIDER_SATURATION:   fp.saturationBoost = val; break;
        case CTRL_SLIDER_SHARPEN:      fp.sharpen = val; break;
        case CTRL_SLIDER_DARK:         fp.darkLighting = val; break;
        case CTRL_SLIDER_AO_INTENSITY: fp.ambientOcclusion = val; break;
        case CTRL_SLIDER_AO_RADIUS:    fp.aoRadius = val; break;
        case CTRL_SLIDER_CLARITY:      fp.clarity = val; break;
        case CTRL_SLIDER_BLOOM_INT:    fp.bloomIntensity = val; break;
        case CTRL_SLIDER_BLOOM_THRESH: fp.bloomThreshold = val; break;
        case CTRL_SLIDER_FILMIC_STR:   fp.filmicStrength = val; break;
        case CTRL_SLIDER_DEBAND:       fp.debandStrength = val; break;
        case CTRL_SLIDER_HDR:          fp.dynamicHdr = val; break;
        case CTRL_SLIDER_HDR_PEAK: {
            int nits = 300 + static_cast<int>(std::lround(norm * 120.0f)) * 10;
            fp.hdrPeakNits = std::clamp(nits, 300, 1500);
            break;
        }
        case CTRL_SLIDER_HDR_MIDGRAY:  fp.hdrMidGray = val; break;
        case CTRL_SLIDER_BRIGHTNESS:   fp.brightness = val; break;
        case CTRL_SLIDER_CONTRAST:     fp.contrast = val; break;
        case CTRL_SLIDER_TEMPERATURE:  fp.temperature = val; break;
        case CTRL_SLIDER_TINT:         fp.tint = val; break;
        case CTRL_SLIDER_EXPOSURE:     fp.exposure = val; break;
        case CTRL_SLIDER_GAMMA:        fp.gamma = val; break;
        case CTRL_SLIDER_NIGHT:        fp.nightMode = val; break;
        case CTRL_SLIDER_BW:           fp.blackAndWhite = val; break;
        case CTRL_SLIDER_VIGNETTE:     fp.vignette = val; break;
        case CTRL_SLIDER_CBSTRENGTH:   fp.colorblindStrength = val; break;
        case CTRL_SB_SLIDER_SPLIT:
            config_->splitPosition = std::clamp(norm, 0.05f, 0.95f);
            Render();
            if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
            return;
        default: break;
    }
    Render();
    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
}

void SidebarUI::HandleMouseDown(float dipX, float dipY) {
    if (!config_) return;

    int profIdx = (config_->activeProfile >= 1 && config_->activeProfile <= 3) ? (config_->activeProfile - 1) : 0;
    FilterParams& fp = config_->profiles[profIdx];

    for (const auto& hr : hitRects_) {
        if (dipX >= hr.rect.left && dipX <= hr.rect.right && dipY >= hr.rect.top && dipY <= hr.rect.bottom) {
            if (hr.isSlider) {
                draggingSlider_ = true;
                activeSlider_ = hr.kind;
                activeTrackRect_ = hr.rect;
                SetCapture(hwnd_);
                UpdateSliderFromX(hr.kind, dipX, hr.rect);
                return;
            }
            switch (hr.kind) {
                case CTRL_CLOSE_BTN:
                    SetOpen(false);
                    return;
                case CTRL_PROFILE_0:
                case CTRL_PROFILE_1:
                case CTRL_PROFILE_2:
                case CTRL_PROFILE_3: {
                    int p = static_cast<int>(hr.kind - CTRL_PROFILE_0);
                    config_->activeProfile = p;
                    if (config_->autoSwitchPerApp && !activeExe_.empty() && activeExe_ != L"desktop" &&
                        (!config_->onlyInGames || activeAppIsGame_)) {
                        config_->perAppProfile[activeExe_] = p;
                    }
                    if (p == 0) {
                        scrollY_ = 0.0f;
                    }
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;
                }
                case CTRL_TOGGLE_SHARPEN:
                    fp.enableSharpen = !fp.enableSharpen;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;
                case CTRL_RESET_SHARPEN:
                    fp.sharpen = 50;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;

                case CTRL_TOGGLE_VIBRANCE:
                    fp.enableDynamicVibrance = !fp.enableDynamicVibrance;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;
                case CTRL_RESET_VIBRANCE:
                    fp.intensity = 50;
                    fp.saturationBoost = 50;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;

                case CTRL_TOGGLE_HDR:
                    fp.enableDynamicHdr = !fp.enableDynamicHdr;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;
                case CTRL_RESET_HDR:
                    fp.dynamicHdr = 50;
                    fp.hdrPeakNits = displayHdr_.hdrActive
                        ? std::clamp(static_cast<int>(std::lround(displayHdr_.maxLuminanceNits / 10.0f)) * 10, 300, 1500)
                        : 800;
                    fp.hdrMidGray = 50;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;

                case CTRL_TOGGLE_DARK:
                    fp.enableDarkLighting = !fp.enableDarkLighting;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;
                case CTRL_RESET_DARK:
                    fp.darkLighting = 50;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;

                case CTRL_TOGGLE_AO:
                    fp.enableAmbientOcclusion = !fp.enableAmbientOcclusion;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;
                case CTRL_RESET_AO:
                    fp.ambientOcclusion = 50;
                    fp.aoRadius = 50;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;

                case CTRL_TOGGLE_CLARITY:
                    fp.enableClarity = !fp.enableClarity;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;
                case CTRL_RESET_CLARITY:
                    fp.clarity = 50;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;

                case CTRL_TOGGLE_BLOOM:
                    fp.enableBloom = !fp.enableBloom;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;
                case CTRL_RESET_BLOOM:
                    fp.bloomIntensity = 50;
                    fp.bloomThreshold = 60;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;

                case CTRL_TOGGLE_FILMIC:
                    fp.enableFilmic = !fp.enableFilmic;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;
                case CTRL_FILMIC_MODE_0:
                case CTRL_FILMIC_MODE_1:
                case CTRL_FILMIC_MODE_2:
                    fp.filmicMode = static_cast<int>(hr.kind - CTRL_FILMIC_MODE_0);
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;
                case CTRL_RESET_FILMIC:
                    fp.filmicMode = 0;
                    fp.filmicStrength = 50;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;

                case CTRL_TOGGLE_DEBAND:
                    fp.enableDeband = !fp.enableDeband;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;
                case CTRL_RESET_DEBAND:
                    fp.debandStrength = 50;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;

                case CTRL_TOGGLE_BC:
                    fp.enableBrightnessContrast = !fp.enableBrightnessContrast;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;
                case CTRL_RESET_BC:
                    fp.brightness = 50;
                    fp.contrast = 50;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;

                case CTRL_TOGGLE_COLORTEMP:
                    fp.enableColorTemp = !fp.enableColorTemp;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;
                case CTRL_RESET_COLORTEMP:
                    fp.temperature = 50;
                    fp.tint = 50;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;

                case CTRL_TOGGLE_GAMMAEXP:
                    fp.enableGammaExposure = !fp.enableGammaExposure;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;
                case CTRL_RESET_GAMMAEXP:
                    fp.exposure = 50;
                    fp.gamma = 50;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;

                case CTRL_TOGGLE_NIGHT:
                    fp.enableNightMode = !fp.enableNightMode;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;
                case CTRL_RESET_NIGHT:
                    fp.nightMode = 50;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;

                case CTRL_TOGGLE_BW:
                    fp.enableBlackAndWhite = !fp.enableBlackAndWhite;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;
                case CTRL_RESET_BW:
                    fp.blackAndWhite = 100;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;

                case CTRL_TOGGLE_VIGNETTE:
                    fp.enableVignette = !fp.enableVignette;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;
                case CTRL_RESET_VIGNETTE:
                    fp.vignette = 50;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;

                case CTRL_TOGGLE_COLORBLIND:
                    fp.enableColorblind = !fp.enableColorblind;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;
                case CTRL_CB_TYPE_0:
                case CTRL_CB_TYPE_1:
                case CTRL_CB_TYPE_2:
                    fp.colorblindType = static_cast<int>(hr.kind - CTRL_CB_TYPE_0);
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;
                case CTRL_RESET_COLORBLIND:
                    fp.colorblindType = 0;
                    fp.colorblindStrength = 50;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;

                case CTRL_SB_SCALE_0:
                case CTRL_SB_SCALE_1:
                case CTRL_SB_SCALE_2:
                case CTRL_SB_SCALE_3:
                case CTRL_SB_SCALE_4:
                    config_->scaleMode = static_cast<int>(hr.kind - CTRL_SB_SCALE_0);
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;

                case CTRL_SB_FG_0:
                case CTRL_SB_FG_1:
                case CTRL_SB_FG_2:
                case CTRL_SB_FG_3:
                case CTRL_SB_FG_4:
                    config_->fgMode = static_cast<int>(hr.kind - CTRL_SB_FG_0);
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;

                case CTRL_SB_TOGGLE_SPLIT:
                    config_->splitPreview = !config_->splitPreview;
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;
                case CTRL_SB_SPLIT_DEC:
                    config_->splitPosition = std::clamp(config_->splitPosition - 0.05f, 0.05f, 0.95f);
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;
                case CTRL_SB_SPLIT_INC:
                    config_->splitPosition = std::clamp(config_->splitPosition + 0.05f, 0.05f, 0.95f);
                    Render();
                    if (callbacks_.onConfigChanged) callbacks_.onConfigChanged();
                    return;

                case CTRL_BTN_OPEN_HUB:
                    SetOpen(false);
                    if (callbacks_.onOpenHub) callbacks_.onOpenHub();
                    return;
                default:
                    break;
            }
        }
    }
}

void SidebarUI::HandleMouseMove(float dipX, float dipY) {
    (void)dipY;
    if (draggingSlider_) {
        UpdateSliderFromX(activeSlider_, dipX, activeTrackRect_);
    }
}

void SidebarUI::HandleMouseUp() {
    if (draggingSlider_) {
        draggingSlider_ = false;
        if (GetCapture() == hwnd_) ReleaseCapture();
    }
}

void SidebarUI::HandleWheel(int delta) {
    scrollY_ = std::clamp(scrollY_ - static_cast<float>(delta) * 0.35f, 0.0f, maxScrollY_);
    Render();
}

LRESULT CALLBACK SidebarUI::SidebarWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    SidebarUI* self = reinterpret_cast<SidebarUI*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<SidebarUI*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
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
        case WM_MOUSEWHEEL:
            self->HandleWheel(GET_WHEEL_DELTA_WPARAM(wParam));
            return 0;
        case WM_DPICHANGED: {
            UINT newDpi = HIWORD(wParam);
            self->dpiScale_ = std::max(static_cast<float>(newDpi) / 96.0f, 1.0f);
            self->sidebarDipH_ = static_cast<float>(self->screenPhysH_) / self->dpiScale_;
            self->DiscardRenderResources();
            self->UpdateWindowPosition();
            self->Render();
            return 0;
        }
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}
