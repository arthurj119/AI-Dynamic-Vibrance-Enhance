#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <windowsx.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

namespace {

constexpr int kResExe    = 201;
constexpr int kResHlsl   = 202;
constexpr int kResFx     = 203;
constexpr int kResReadme = 204;

constexpr DWORD kSetupStyle = WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU;
constexpr float kSetupDipW = 640.0f;
constexpr float kSetupDipH = 560.0f;
constexpr float kSetupTopBarH = 44.0f;

void ConfigureFramelessDarkSetup(HWND hwnd) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    DWORD cornerPref = 2; /* DWMWCP_ROUND */
    DwmSetWindowAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &cornerPref, sizeof(cornerPref));
    COLORREF borderCol = RGB(35, 39, 50);
    DwmSetWindowAttribute(hwnd, 34 /* DWMWA_BORDER_COLOR */, &borderCol, sizeof(borderCol));
}

void DrawSetupStarBadge(
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

constexpr D2D1_COLOR_F SColHex(uint32_t rgb, float a = 1.0f) {
    return D2D1_COLOR_F{
        ((rgb >> 16) & 0xFF) / 255.0f,
        ((rgb >> 8) & 0xFF) / 255.0f,
        (rgb & 0xFF) / 255.0f,
        a
    };
}

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

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), len, nullptr, nullptr);
    return out;
}

bool LoadRcData(HINSTANCE hInst, int resId, const void*& outPtr, DWORD& outSize) {
    HRSRC hrsrc = FindResourceW(hInst, MAKEINTRESOURCEW(resId), RT_RCDATA);
    if (!hrsrc) return false;
    DWORD sz = SizeofResource(hInst, hrsrc);
    HGLOBAL hg = LoadResource(hInst, hrsrc);
    if (!hg || sz == 0) return false;
    const void* ptr = LockResource(hg);
    if (!ptr) return false;
    outPtr = ptr;
    outSize = sz;
    return true;
}

bool ExtractResourceToFile(HINSTANCE hInst, int resId, const fs::path& dstPath, DWORD& outBytesWritten) {
    const void* ptr = nullptr;
    DWORD sz = 0;
    if (!LoadRcData(hInst, resId, ptr, sz)) return false;

    std::error_code ec;
    fs::create_directories(dstPath.parent_path(), ec);

    std::ofstream ofs(dstPath, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) return false;
    ofs.write(static_cast<const char*>(ptr), static_cast<std::streamsize>(sz));
    ofs.close();
    if (!ofs) return false;
    outBytesWritten = sz;
    return true;
}

bool CreateShortcut(const fs::path& lnkPath, const fs::path& targetExe, const std::wstring& args, const std::wstring& desc) {
    std::error_code ec;
    fs::create_directories(lnkPath.parent_path(), ec);

    ComPtr<IShellLinkW> psl;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&psl));
    if (FAILED(hr) || !psl) return false;

    psl->SetPath(targetExe.c_str());
    psl->SetWorkingDirectory(targetExe.parent_path().c_str());
    if (!args.empty()) {
        psl->SetArguments(args.c_str());
    }
    if (!desc.empty()) {
        psl->SetDescription(desc.c_str());
    }
    psl->SetIconLocation(targetExe.c_str(), 0);

    ComPtr<IPersistFile> ppf;
    hr = psl.As(&ppf);
    if (FAILED(hr) || !ppf) return false;

    hr = ppf->Save(lnkPath.c_str(), TRUE);
    return SUCCEEDED(hr);
}

fs::path GetDefaultInstallDir() {
    PWSTR rawPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &rawPath)) && rawPath) {
        fs::path p = fs::path(rawPath) / L"Programs" / L"AIDynamicVibranceEnhance";
        CoTaskMemFree(rawPath);
        return p;
    }
    return L"C:\\AIDynamicVibranceEnhance";
}

bool WriteInitialProfileIfMissing(const fs::path& cfgPath, bool mode2Enabled, bool launchAtStartup) {
    std::error_code ec;
    if (fs::exists(cfgPath, ec)) {
        return true; // Preserve user's existing profiles on upgrade/reinstall
    }

    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"schemaVersion\": 3,\n";
    ss << "  \"activeProfile\": 1,\n";
    ss << "  \"overlayEnabled\": true,\n";
    ss << "  \"autoSwitchPerApp\": true,\n";
    ss << "  \"onlyInGames\": true,\n";
    ss << "  \"launchAtStartup\": " << (launchAtStartup ? "true" : "false") << ",\n";
    ss << "  \"mode2Enabled\": " << (mode2Enabled ? "true" : "false") << ",\n";
    ss << "  \"splitPreview\": false,\n";
    ss << "  \"splitPosition\": 0.5,\n";
    ss << "  \"reshadeFolder\": \"\",\n";
    ss << "  \"profiles\": [\n";
    for (int i = 0; i < 3; ++i) {
        ss << "    {\n";
        ss << "      \"intensity\": 50,\n";
        ss << "      \"saturationBoost\": 50,\n";
        ss << "      \"enableDynamicVibrance\": true\n";
        ss << "    }" << (i < 2 ? ",\n" : "\n");
    }
    ss << "  ],\n";
    ss << "  \"perAppProfile\": {}\n";
    ss << "}\n";

    std::ofstream ofs(cfgPath, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) return false;
    std::string s = ss.str();
    ofs.write(s.data(), static_cast<std::streamsize>(s.size()));
    return static_cast<bool>(ofs);
}

bool RegisterUninstallEntry(const fs::path& installDir, const fs::path& exePath, DWORD totalBytes) {
    HKEY hKey = nullptr;
    LONG rc = RegCreateKeyExW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\AIDynamicVibranceEnhance",
        0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr
    );
    if (rc != ERROR_SUCCESS || !hKey) return false;

    auto setStr = [&](const wchar_t* name, const std::wstring& val) {
        DWORD bytes = static_cast<DWORD>((val.size() + 1) * sizeof(wchar_t));
        RegSetValueExW(hKey, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(val.c_str()), bytes);
    };
    auto setDword = [&](const wchar_t* name, DWORD val) {
        RegSetValueExW(hKey, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&val), sizeof(DWORD));
    };

    std::wstring iconLoc = exePath.wstring() + L",0";
    std::wstring uninstCmd = L"\"" + exePath.wstring() + L"\" --uninstall";

    setStr(L"DisplayName", L"AI Dynamic Vibrance Enhance");
    setStr(L"DisplayVersion", L"1.0.0");
    setStr(L"Publisher", L"arthurj119");
    setStr(L"InstallLocation", installDir.wstring());
    setStr(L"DisplayIcon", iconLoc);
    setStr(L"UninstallString", uninstCmd);
    setDword(L"NoModify", 1);
    setDword(L"NoRepair", 1);
    setDword(L"EstimatedSize", std::max<DWORD>(totalBytes / 1024, 1));

    RegCloseKey(hKey);
    return true;
}

bool RegisterStartupKey(const fs::path& exePath, bool enable) {
    HKEY hKey = nullptr;
    LONG rc = RegCreateKeyExW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr
    );
    if (rc != ERROR_SUCCESS || !hKey) return false;

    bool ok = false;
    if (enable) {
        std::wstring cmd = L"\"" + exePath.wstring() + L"\" --minimized";
        DWORD bytes = static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t));
        ok = (RegSetValueExW(hKey, L"AIDynamicVibranceEnhance", 0, REG_SZ,
                             reinterpret_cast<const BYTE*>(cmd.c_str()), bytes) == ERROR_SUCCESS);
    } else {
        LSTATUS del = RegDeleteValueW(hKey, L"AIDynamicVibranceEnhance");
        ok = (del == ERROR_SUCCESS || del == ERROR_FILE_NOT_FOUND);
    }
    RegCloseKey(hKey);
    return ok;
}

int RunSetupSelfTest(HINSTANCE hInst) {
    std::ostringstream out;
    out << "=== AI Dynamic Vibrance Enhance Setup Self-Test ===\n";

    fs::path tmpDir = fs::temp_directory_path() / L"adv_setup_selftest";
    std::error_code ec;
    fs::remove_all(tmpDir, ec);
    fs::create_directories(tmpDir, ec);

    struct TestItem {
        int resId;
        const wchar_t* relName;
        const char* label;
    } items[] = {
        { kResExe,    L"AIDynamicVibranceEnhance.exe",      "Main Executable RCDATA" },
        { kResHlsl,   L"shaders\\DynamicVibrance.hlsl",     "HLSL Shader RCDATA" },
        { kResFx,     L"shaders\\AIDynamicVibranceEnhance.fx", "ReShade .fx RCDATA" },
        { kResReadme, L"README.md",                         "README RCDATA" },
    };

    for (const auto& item : items) {
        const void* ptr = nullptr;
        DWORD expectedSize = 0;
        if (!LoadRcData(hInst, item.resId, ptr, expectedSize) || expectedSize < 100) {
            out << "[FAIL] Missing or invalid resource: " << item.label << "\n";
            WriteStdout(out.str());
            fs::remove_all(tmpDir, ec);
            return 1;
        }
        DWORD written = 0;
        fs::path dst = tmpDir / item.relName;
        if (!ExtractResourceToFile(hInst, item.resId, dst, written) || written != expectedSize) {
            out << "[FAIL] Failed extracting resource: " << item.label << "\n";
            WriteStdout(out.str());
            fs::remove_all(tmpDir, ec);
            return 1;
        }
        out << "[PASS] " << item.label << " verified (" << written << " bytes)\n";
    }

    fs::remove_all(tmpDir, ec);
    out << "SETUP SELF-TEST PASSED!\n";
    WriteStdout(out.str());
    return 0;
}

class SetupWizard {
public:
    bool Initialize(HINSTANCE hInst, std::string& outError) {
        hInst_ = hInst;
        installDir_ = GetDefaultInstallDir();

        HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&d2dFactory_));
        if (FAILED(hr)) { outError = "D2D1CreateFactory failed"; return false; }

        hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                 reinterpret_cast<IUnknown**>(dwriteFactory_.GetAddressOf()));
        if (FAILED(hr)) { outError = "DWriteCreateFactory failed"; return false; }

        dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 16.0f, L"en-us", &fmtTitle_);
        dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"en-us", &fmtSection_);
        dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.5f, L"en-us", &fmtBody_);
        dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"en-us", &fmtSmall_);
        dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.5f, L"en-us", &fmtCenter_);
        if (fmtCenter_) {
            fmtCenter_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            fmtCenter_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = SetupWndProc;
        wc.hInstance = hInst;
        wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(101));
        wc.hIconSm = wc.hIcon;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(RGB(14, 16, 20));
        wc.lpszClassName = L"AIDynamicVibranceSetupWnd";
        RegisterClassExW(&wc);

        hwnd_ = CreateWindowExW(
            0, wc.lpszClassName, L"AI Dynamic Vibrance Enhance Setup",
            kSetupStyle, CW_USEDEFAULT, CW_USEDEFAULT, 640, 560,
            nullptr, nullptr, hInst, this
        );
        if (!hwnd_) { outError = "CreateWindowExW failed"; return false; }

        ConfigureFramelessDarkSetup(hwnd_);

        UINT dpi = GetDpiForWindow(hwnd_);
        dpiScale_ = std::max(static_cast<float>(dpi) / 96.0f, 1.0f);
        RECT rc{ 0, 0, static_cast<LONG>(std::lround(kSetupDipW * dpiScale_)), static_cast<LONG>(std::lround(kSetupDipH * dpiScale_)) };
        AdjustWindowRectExForDpi(&rc, kSetupStyle, FALSE, 0, dpi);
        int winW = rc.right - rc.left;
        int winH = rc.bottom - rc.top;
        int x = std::max(0, (GetSystemMetrics(SM_CXSCREEN) - winW) / 2);
        int y = std::max(0, (GetSystemMetrics(SM_CYSCREEN) - winH) / 2);
        SetWindowPos(hwnd_, nullptr, x, y, winW, winH, SWP_NOZORDER);

        CreateResources();
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
        return true;
    }

    int RunLoop() {
        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return static_cast<int>(msg.wParam);
    }

private:
    enum SetupCtrl {
        CTRL_WIN_MIN,
        CTRL_WIN_CLOSE,
        CTRL_MODE_1,
        CTRL_MODE_BOTH,
        CTRL_BROWSE,
        CTRL_CHK_DESKTOP,
        CTRL_CHK_STARTMENU,
        CTRL_CHK_STARTUP,
        CTRL_BTN_INSTALL,
        CTRL_BTN_CANCEL,
        CTRL_BTN_LAUNCH,
        CTRL_BTN_CLOSE,
    };

    struct HitRect {
        SetupCtrl kind;
        D2D1_RECT_F rect;
    };

    bool CreateResources() {
        if (rt_ || !hwnd_) return rt_ != nullptr;
        RECT rc{};
        GetClientRect(hwnd_, &rc);
        D2D1_SIZE_U sz = D2D1::SizeU(std::max<UINT>(rc.right - rc.left, 1), std::max<UINT>(rc.bottom - rc.top, 1));
        D2D1_RENDER_TARGET_PROPERTIES rtp = D2D1::RenderTargetProperties();
        rtp.dpiX = 96.0f * dpiScale_;
        rtp.dpiY = 96.0f * dpiScale_;
        if (FAILED(d2dFactory_->CreateHwndRenderTarget(
                rtp, D2D1::HwndRenderTargetProperties(hwnd_, sz, D2D1_PRESENT_OPTIONS_NONE), &rt_))) {
            return false;
        }
        rt_->CreateSolidColorBrush(SColHex(0x0B0D11), &brTopBar_);
        rt_->CreateSolidColorBrush(SColHex(0x0E1014), &brBg_);
        rt_->CreateSolidColorBrush(SColHex(0x151820), &brCard_);
        rt_->CreateSolidColorBrush(SColHex(0x1C202B), &brCardHover_);
        rt_->CreateSolidColorBrush(SColHex(0x232732), &brBorder_);
        rt_->CreateSolidColorBrush(SColHex(0xED1C24), &brAccent_);
        rt_->CreateSolidColorBrush(SColHex(0xED1C24, 0.16f), &brAccentSoft_);
        rt_->CreateSolidColorBrush(SColHex(0xF3F5F8), &brText_);
        rt_->CreateSolidColorBrush(SColHex(0x8B95A5), &brTextMuted_);
        rt_->CreateSolidColorBrush(SColHex(0xF87171), &brDanger_);
        return true;
    }

    void DiscardResources() {
        brDanger_.Reset();
        brTextMuted_.Reset();
        brText_.Reset();
        brAccentSoft_.Reset();
        brAccent_.Reset();
        brBorder_.Reset();
        brCardHover_.Reset();
        brCard_.Reset();
        brBg_.Reset();
        brTopBar_.Reset();
        rt_.Reset();
    }

    void Render() {
        if (!CreateResources()) return;
        hitRects_.clear();
        rt_->BeginDraw();
        rt_->Clear(SColHex(0x0E1014));

        const float W = kSetupDipW;
        const float H = kSetupDipH;
        const float pad = 24.0f;
        const float cw = W - pad * 2.0f;

        auto drawText = [&](const std::wstring& txt, IDWriteTextFormat* fmt, D2D1_RECT_F r, ID2D1SolidColorBrush* br) {
            rt_->DrawTextW(txt.c_str(), static_cast<UINT32>(txt.size()), fmt, r, br, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        };

        // Custom Frameless Dark Titlebar (matching Control Hub cleanly)
        rt_->FillRectangle(D2D1::RectF(0, 0, W, kSetupTopBarH), brTopBar_.Get());
        rt_->DrawLine(D2D1::Point2F(0, kSetupTopBarH), D2D1::Point2F(W, kSetupTopBarH), brBorder_.Get(), 1.0f);
        DrawSetupStarBadge(d2dFactory_.Get(), rt_.Get(), D2D1::RectF(16, 12, 36, 32), brAccent_.Get(), brText_.Get());
        drawText(L"AI Dynamic Vibrance Enhance Setup", fmtSmall_.Get(), D2D1::RectF(44, 14, 460, 32), brText_.Get());

        D2D1_RECT_F winMinR = D2D1::RectF(W - 76, 8, W - 46, 36);
        rt_->DrawLine(D2D1::Point2F(W - 66, 22), D2D1::Point2F(W - 56, 22), brTextMuted_.Get(), 1.6f);
        hitRects_.push_back({ CTRL_WIN_MIN, winMinR });

        D2D1_RECT_F winCloseR = D2D1::RectF(W - 40, 8, W - 10, 36);
        rt_->DrawLine(D2D1::Point2F(W - 30, 17), D2D1::Point2F(W - 20, 27), brTextMuted_.Get(), 1.6f);
        rt_->DrawLine(D2D1::Point2F(W - 20, 17), D2D1::Point2F(W - 30, 27), brTextMuted_.Get(), 1.6f);
        hitRects_.push_back({ CTRL_WIN_CLOSE, winCloseR });

        // Top Brand Header Band
        float hdrY = kSetupTopBarH;
        rt_->FillRectangle(D2D1::RectF(0, hdrY, W, hdrY + 70.0f), brCard_.Get());
        rt_->DrawLine(D2D1::Point2F(0, hdrY + 70.0f), D2D1::Point2F(W, hdrY + 70.0f), brBorder_.Get(), 1.0f);

        D2D1_RECT_F badgeR = D2D1::RectF(pad, hdrY + 17.0f, pad + 36.0f, hdrY + 53.0f);
        DrawSetupStarBadge(d2dFactory_.Get(), rt_.Get(), badgeR, brAccent_.Get(), brText_.Get());

        drawText(L"AI Dynamic Vibrance Enhance", fmtTitle_.Get(), D2D1::RectF(pad + 50, hdrY + 14, W - pad, hdrY + 38), brText_.Get());
        drawText(L"v1.0.0  \u2022  by arthurj119  \u2022  Open-Source Cross-GPU Color & Visibility Enhancer",
                 fmtSmall_.Get(), D2D1::RectF(pad + 50, hdrY + 39, W - pad, hdrY + 58), brTextMuted_.Get());

        float y = hdrY + 86.0f;
        if (page_ == 0) {
            drawText(L"1. Installation Type", fmtSection_.Get(), D2D1::RectF(pad, y, W - pad, y + 20), brText_.Get());
            y += 26.0f;

            auto drawModeCard = [&](SetupCtrl k, bool sel, const std::wstring& title, const std::wstring& desc) {
                float h = 56.0f;
                D2D1_RECT_F mr = D2D1::RectF(pad, y, pad + cw, y + h);
                rt_->FillRoundedRectangle(D2D1::RoundedRect(mr, 8, 8), sel ? brAccentSoft_.Get() : brCard_.Get());
                rt_->DrawRoundedRectangle(D2D1::RoundedRect(mr, 8, 8), sel ? brAccent_.Get() : brBorder_.Get(), sel ? 1.6f : 1.0f);

                float rx = pad + 20.0f, ry = y + 28.0f;
                rt_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(rx, ry), 8.0f, 8.0f), sel ? brAccent_.Get() : brBorder_.Get(), 1.8f);
                if (sel) {
                    rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(rx, ry), 4.5f, 4.5f), brAccent_.Get());
                }
                drawText(title, fmtSection_.Get(), D2D1::RectF(pad + 40, y + 9, pad + cw - 14, y + 29), brText_.Get());
                drawText(desc, fmtSmall_.Get(), D2D1::RectF(pad + 40, y + 30, pad + cw - 14, y + 50), brTextMuted_.Get());
                hitRects_.push_back({ k, mr });
                y += h + 8.0f;
            };

            drawModeCard(CTRL_MODE_1, !mode2Enabled_,
                         L"Standard Installation (Recommended)",
                         L"Installs the real-time DirectX 11 overlay and desktop Control Hub for modern games.");
            drawModeCard(CTRL_MODE_BOTH, mode2Enabled_,
                         L"Full Installation (+ Legacy Fullscreen Shader Support)",
                         L"Includes optional ReShade .fx shader export for classic DX9, DX10, and OpenGL games.");
            y += 8.0f;

            drawText(L"2. Installation Folder", fmtSection_.Get(), D2D1::RectF(pad, y, W - pad, y + 20), brText_.Get());
            y += 26.0f;

            std::wstring dirDisp = installDir_.wstring();
            if (dirDisp.size() > 64) {
                dirDisp = L"..." + dirDisp.substr(dirDisp.size() - 61);
            }
            D2D1_RECT_F dirBox = D2D1::RectF(pad, y, pad + cw - 104, y + 32);
            rt_->FillRoundedRectangle(D2D1::RoundedRect(dirBox, 5, 5), brCard_.Get());
            rt_->DrawRoundedRectangle(D2D1::RoundedRect(dirBox, 5, 5), brBorder_.Get(), 1.0f);
            drawText(dirDisp, fmtSmall_.Get(), D2D1::RectF(dirBox.left + 10, dirBox.top + 8, dirBox.right - 8, dirBox.bottom), brText_.Get());

            D2D1_RECT_F brwR = D2D1::RectF(pad + cw - 94, y, pad + cw, y + 32);
            rt_->FillRoundedRectangle(D2D1::RoundedRect(brwR, 5, 5), brCardHover_.Get());
            rt_->DrawRoundedRectangle(D2D1::RoundedRect(brwR, 5, 5), brBorder_.Get(), 1.0f);
            drawText(L"Browse...", fmtCenter_.Get(), brwR, brText_.Get());
            hitRects_.push_back({ CTRL_BROWSE, brwR });
            y += 46.0f;

            drawText(L"3. Shortcuts & Startup", fmtSection_.Get(), D2D1::RectF(pad, y, W - pad, y + 20), brText_.Get());
            y += 26.0f;

            auto drawChk = [&](SetupCtrl k, const std::wstring& label, bool checked) {
                D2D1_RECT_F hr = D2D1::RectF(pad, y, pad + cw, y + 24);
                D2D1_RECT_F bx = D2D1::RectF(pad, y + 3, pad + 17, y + 20);
                rt_->FillRoundedRectangle(D2D1::RoundedRect(bx, 3, 3), checked ? brAccent_.Get() : brCard_.Get());
                rt_->DrawRoundedRectangle(D2D1::RoundedRect(bx, 3, 3), checked ? brAccent_.Get() : brBorder_.Get(), 1.2f);
                if (checked) {
                    rt_->DrawLine(D2D1::Point2F(bx.left + 4, bx.top + 9), D2D1::Point2F(bx.left + 7.5f, bx.top + 12.5f), brText_.Get(), 1.8f);
                    rt_->DrawLine(D2D1::Point2F(bx.left + 7.5f, bx.top + 12.5f), D2D1::Point2F(bx.left + 13.5f, bx.top + 5.5f), brText_.Get(), 1.8f);
                }
                drawText(label, fmtBody_.Get(), D2D1::RectF(pad + 26, y + 1, pad + cw, y + 23), brText_.Get());
                hitRects_.push_back({ k, hr });
                y += 28.0f;
            };

            drawChk(CTRL_CHK_DESKTOP,   L"Create Desktop shortcut", optDesktop_);
            drawChk(CTRL_CHK_STARTMENU, L"Create Start Menu shortcuts", optStartMenu_);
            drawChk(CTRL_CHK_STARTUP,   L"Launch on Windows startup (minimized to tray)", optStartup_);

            if (!errorMsg_.empty()) {
                drawText(errorMsg_, fmtSmall_.Get(), D2D1::RectF(pad, H - 84, W - pad, H - 62), brDanger_.Get());
            }

            // Bottom Footer Bar
            rt_->DrawLine(D2D1::Point2F(0, H - 60), D2D1::Point2F(W, H - 60), brBorder_.Get(), 1.0f);
            D2D1_RECT_F cancelR = D2D1::RectF(W - pad - 230, H - 46, W - pad - 130, H - 12);
            rt_->FillRoundedRectangle(D2D1::RoundedRect(cancelR, 6, 6), brCard_.Get());
            rt_->DrawRoundedRectangle(D2D1::RoundedRect(cancelR, 6, 6), brBorder_.Get(), 1.0f);
            drawText(L"Cancel", fmtCenter_.Get(), cancelR, brTextMuted_.Get());
            hitRects_.push_back({ CTRL_BTN_CANCEL, cancelR });

            D2D1_RECT_F instR = D2D1::RectF(W - pad - 118, H - 46, W - pad, H - 12);
            rt_->FillRoundedRectangle(D2D1::RoundedRect(instR, 6, 6), brAccent_.Get());
            drawText(L"Install", fmtCenter_.Get(), instR, brText_.Get());
            hitRects_.push_back({ CTRL_BTN_INSTALL, instR });
        } else {
            // Page 1: Installation Complete
            D2D1_RECT_F cardR = D2D1::RectF(pad, y + 8, pad + cw, y + 250);
            rt_->FillRoundedRectangle(D2D1::RoundedRect(cardR, 10, 10), brCard_.Get());
            rt_->DrawRoundedRectangle(D2D1::RoundedRect(cardR, 10, 10), brAccent_.Get(), 1.2f);

            float cyPos = y + 54.0f;
            rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(W * 0.5f, cyPos), 24.0f, 24.0f), brAccent_.Get());
            rt_->DrawLine(D2D1::Point2F(W * 0.5f - 9, cyPos + 1), D2D1::Point2F(W * 0.5f - 2, cyPos + 8), brText_.Get(), 2.8f);
            rt_->DrawLine(D2D1::Point2F(W * 0.5f - 2, cyPos + 8), D2D1::Point2F(W * 0.5f + 10, cyPos - 7), brText_.Get(), 2.8f);

            drawText(L"Installation Complete!", fmtTitle_.Get(), D2D1::RectF(pad + 20, y + 92, pad + cw - 20, y + 120), brText_.Get());
            std::wstring summary =
                L"Installed to: " + installDir_.wstring() + L"\n" +
                L"Package: " + (mode2Enabled_
                    ? L"Full Installation (Overlay + Legacy Fullscreen Shaders)"
                    : L"Standard Installation (Real-Time Overlay & Control Hub)") + L"\n\n" +
                L"Click 'Launch Control Hub' below to customize your filters and hotkeys anytime.";
            drawText(summary, fmtBody_.Get(), D2D1::RectF(pad + 24, y + 128, pad + cw - 24, y + 236), brTextMuted_.Get());

            rt_->DrawLine(D2D1::Point2F(0, H - 60), D2D1::Point2F(W, H - 60), brBorder_.Get(), 1.0f);
            D2D1_RECT_F closeR = D2D1::RectF(W - pad - 294, H - 46, W - pad - 196, H - 12);
            rt_->FillRoundedRectangle(D2D1::RoundedRect(closeR, 6, 6), brCard_.Get());
            rt_->DrawRoundedRectangle(D2D1::RoundedRect(closeR, 6, 6), brBorder_.Get(), 1.0f);
            drawText(L"Close", fmtCenter_.Get(), closeR, brTextMuted_.Get());
            hitRects_.push_back({ CTRL_BTN_CLOSE, closeR });

            D2D1_RECT_F launchR = D2D1::RectF(W - pad - 184, H - 46, W - pad, H - 12);
            rt_->FillRoundedRectangle(D2D1::RoundedRect(launchR, 6, 6), brAccent_.Get());
            drawText(L"Launch Control Hub", fmtCenter_.Get(), launchR, brText_.Get());
            hitRects_.push_back({ CTRL_BTN_LAUNCH, launchR });
        }

        // Crisp 1px matte frame around frameless setup window
        rt_->DrawRectangle(D2D1::RectF(0.5f, 0.5f, W - 0.5f, H - 0.5f), brBorder_.Get(), 1.0f);

        if (rt_->EndDraw() == D2DERR_RECREATE_TARGET) {
            DiscardResources();
        }
    }

    void BrowseFolder() {
        ComPtr<IFileOpenDialog> dlg;
        if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))) && dlg) {
            DWORD opts = 0;
            dlg->GetOptions(&opts);
            dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
            dlg->SetTitle(L"Choose Installation Folder");
            if (SUCCEEDED(dlg->Show(hwnd_))) {
                ComPtr<IShellItem> item;
                if (SUCCEEDED(dlg->GetResult(&item)) && item) {
                    PWSTR p = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p)) && p) {
                        fs::path picked(p);
                        CoTaskMemFree(p);
                        if (picked.filename() != L"AIDynamicVibranceEnhance") {
                            picked /= L"AIDynamicVibranceEnhance";
                        }
                        installDir_ = picked;
                    }
                }
            }
        }
        Render();
    }

    bool PerformInstall() {
        errorMsg_.clear();

        // Gracefully exit any already-running instance before overwriting files
        HWND runningTray = FindWindowExW(HWND_MESSAGE, nullptr, L"AIDynamicVibranceTrayWnd", nullptr);
        if (runningTray) {
            PostMessageW(runningTray, WM_COMMAND, 2099 /* CMD_EXIT */, 0);
            Sleep(450);
        }

        DWORD b1 = 0, b2 = 0, b3 = 0, b4 = 0;
        fs::path exePath = installDir_ / L"AIDynamicVibranceEnhance.exe";
        if (!ExtractResourceToFile(hInst_, kResExe, exePath, b1) ||
            !ExtractResourceToFile(hInst_, kResHlsl, installDir_ / L"shaders" / L"DynamicVibrance.hlsl", b2) ||
            !ExtractResourceToFile(hInst_, kResFx, installDir_ / L"shaders" / L"AIDynamicVibranceEnhance.fx", b3) ||
            !ExtractResourceToFile(hInst_, kResReadme, installDir_ / L"README.md", b4)) {
            errorMsg_ = L"Failed writing files to the selected folder (try another folder or check permissions).";
            return false;
        }

        WriteInitialProfileIfMissing(installDir_ / L"profiles.json", mode2Enabled_, optStartup_);
        RegisterUninstallEntry(installDir_, exePath, b1 + b2 + b3 + b4);
        RegisterStartupKey(exePath, optStartup_);

        if (optDesktop_) {
            PWSTR desk = nullptr;
            if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desk)) && desk) {
                CreateShortcut(fs::path(desk) / L"AI Dynamic Vibrance Enhance.lnk", exePath, L"",
                               L"AI Dynamic Vibrance Enhance — Control Hub");
                CoTaskMemFree(desk);
            }
        }
        if (optStartMenu_) {
            PWSTR sm = nullptr;
            if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Programs, 0, nullptr, &sm)) && sm) {
                fs::path smDir = fs::path(sm) / L"AI Dynamic Vibrance Enhance";
                CreateShortcut(smDir / L"AI Dynamic Vibrance Enhance.lnk", exePath, L"",
                               L"Open AI Dynamic Vibrance Enhance Control Hub");
                CreateShortcut(smDir / L"Uninstall AI Dynamic Vibrance Enhance.lnk", exePath, L"--uninstall",
                               L"Uninstall AI Dynamic Vibrance Enhance");
                CoTaskMemFree(sm);
            }
        }

        installedExe_ = exePath;
        return true;
    }

    void HandleClick(float dipX, float dipY) {
        for (const auto& hr : hitRects_) {
            if (dipX >= hr.rect.left && dipX <= hr.rect.right && dipY >= hr.rect.top && dipY <= hr.rect.bottom) {
                switch (hr.kind) {
                    case CTRL_WIN_MIN:     ShowWindow(hwnd_, SW_MINIMIZE); return;
                    case CTRL_WIN_CLOSE:   DestroyWindow(hwnd_); return;
                    case CTRL_MODE_1:      mode2Enabled_ = false; Render(); return;
                    case CTRL_MODE_BOTH:   mode2Enabled_ = true; Render(); return;
                    case CTRL_BROWSE:      BrowseFolder(); return;
                    case CTRL_CHK_DESKTOP: optDesktop_ = !optDesktop_; Render(); return;
                    case CTRL_CHK_STARTMENU: optStartMenu_ = !optStartMenu_; Render(); return;
                    case CTRL_CHK_STARTUP: optStartup_ = !optStartup_; Render(); return;
                    case CTRL_BTN_CANCEL:
                    case CTRL_BTN_CLOSE:
                        DestroyWindow(hwnd_);
                        return;
                    case CTRL_BTN_INSTALL:
                        if (PerformInstall()) {
                            page_ = 1;
                        }
                        Render();
                        return;
                    case CTRL_BTN_LAUNCH:
                        ShellExecuteW(nullptr, L"open", installedExe_.c_str(), nullptr, installDir_.c_str(), SW_SHOWNORMAL);
                        DestroyWindow(hwnd_);
                        return;
                }
            }
        }
    }

    static LRESULT CALLBACK SetupWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        SetupWizard* self = reinterpret_cast<SetupWizard*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = reinterpret_cast<SetupWizard*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);

        switch (msg) {
            case WM_NCHITTEST: {
                POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ScreenToClient(hwnd, &pt);
                float dipX = static_cast<float>(pt.x) / self->dpiScale_;
                float dipY = static_cast<float>(pt.y) / self->dpiScale_;
                if (dipY >= 0.0f && dipY <= kSetupTopBarH) {
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
                self->HandleClick(x, y);
                return 0;
            }
            case WM_DESTROY:
                self->DiscardResources();
                PostQuitMessage(0);
                return 0;
            default:
                return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
    }

    HINSTANCE hInst_ = nullptr;
    HWND hwnd_ = nullptr;
    float dpiScale_ = 1.0f;

    int page_ = 0;
    bool mode2Enabled_ = false;
    bool optDesktop_ = true;
    bool optStartMenu_ = true;
    bool optStartup_ = false;
    fs::path installDir_;
    fs::path installedExe_;
    std::wstring errorMsg_;
    std::vector<HitRect> hitRects_;

    ComPtr<ID2D1Factory> d2dFactory_;
    ComPtr<IDWriteFactory> dwriteFactory_;
    ComPtr<ID2D1HwndRenderTarget> rt_;
    ComPtr<IDWriteTextFormat> fmtTitle_, fmtSection_, fmtBody_, fmtSmall_, fmtCenter_;
    ComPtr<ID2D1SolidColorBrush> brTopBar_, brBg_, brCard_, brCardHover_, brBorder_, brAccent_, brAccentSoft_, brText_, brTextMuted_, brDanger_;
};

} // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR lpCmdLine, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    if (lpCmdLine && wcsstr(lpCmdLine, L"--self-test") != nullptr) {
        int rc = RunSetupSelfTest(hInst);
        CoUninitialize();
        return rc;
    }

    SetupWizard wiz;
    std::string err;
    int rc = 1;
    if (wiz.Initialize(hInst, err)) {
        rc = wiz.RunLoop();
    } else {
        MessageBoxA(nullptr, err.c_str(), "Setup Initialization Failed", MB_OK | MB_ICONERROR);
    }

    CoUninitialize();
    return rc;
}
