#include "dx11_overlay.h"
#include "profiles.h"
#include "shader_source.h"

#include <d3dcompiler.h>
#include <dwmapi.h>
#include <roapi.h>
#include <windows.foundation.h>
#include <windows.graphics.capture.interop.h>
#include <winstring.h>
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <vector>

#ifndef WDA_NONE
#define WDA_NONE 0x00000000
#endif

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Wrappers::HStringReference;

namespace {

std::string HrToString(HRESULT hr) {
    char buf[32];
    snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
    return buf;
}

bool CompileShaderEntry(
    const char* entry,
    const char* target,
    ComPtr<ID3DBlob>& outBlob,
    std::string& outErr
) {
    ComPtr<ID3DBlob> errBlob;
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    HRESULT hr = D3DCompile(
        kDynamicVibranceHLSL,
        std::strlen(kDynamicVibranceHLSL),
        "DynamicVibrance.hlsl",
        nullptr,
        nullptr,
        entry,
        target,
        flags,
        0,
        &outBlob,
        &errBlob
    );
    if (FAILED(hr)) {
        outErr = std::string("Compile failed for ") + entry + " (" + HrToString(hr) + ")";
        if (errBlob && errBlob->GetBufferSize() > 0) {
            outErr += ": ";
            outErr.append(static_cast<const char*>(errBlob->GetBufferPointer()), errBlob->GetBufferSize());
        }
        return false;
    }
    return true;
}

EnhanceCBData BuildCB(
    const FilterParams& p,
    bool splitPreview,
    float splitPos,
    int width,
    int height,
    float smoothAlpha
) {
    EnhanceCBData cb{};
    cb.intensity = static_cast<float>(std::clamp(p.intensity, 0, 100)) / 100.0f;
    cb.saturationBoost = static_cast<float>(std::clamp(p.saturationBoost, 0, 100)) / 100.0f;
    cb.darkLighting = static_cast<float>(std::clamp(p.darkLighting, 0, 100)) / 100.0f;
    cb.clarity = static_cast<float>(std::clamp(p.clarity, 0, 100)) / 100.0f;
    cb.dynamicHdr = static_cast<float>(std::clamp(p.dynamicHdr, 0, 100)) / 100.0f;
    cb.brightness = static_cast<float>(std::clamp(p.brightness, 0, 100)) / 100.0f;
    cb.contrast = static_cast<float>(std::clamp(p.contrast, 0, 100)) / 100.0f;
    cb.splitPos = std::clamp(splitPos, 0.05f, 0.95f);

    uint32_t flags = FLAG_SKIN_PROTECTION | FLAG_ADAPTIVE_SCENE;
    if (p.enableDarkLighting) flags |= FLAG_ENABLE_DARK_LIGHT;
    if (p.enableClarity) flags |= FLAG_ENABLE_CLARITY;
    if (p.enableDynamicHdr) flags |= FLAG_ENABLE_DYNAMIC_HDR;
    if (p.enableBrightnessContrast) flags |= FLAG_ENABLE_BC;
    if (p.enableDynamicVibrance) flags |= FLAG_ENABLE_VIBRANCE;
    if (splitPreview) flags |= FLAG_SPLIT_PREVIEW;

    cb.flags = flags;
    cb.width = static_cast<float>(std::max(width, 1));
    cb.height = static_cast<float>(std::max(height, 1));
    cb.smoothAlpha = smoothAlpha;

    cb.sharpen = static_cast<float>(std::clamp(p.sharpen, 0, 100)) / 100.0f;
    cb.temperature = static_cast<float>(std::clamp(p.temperature, 0, 100)) / 100.0f;
    cb.tint = static_cast<float>(std::clamp(p.tint, 0, 100)) / 100.0f;
    cb.exposure = static_cast<float>(std::clamp(p.exposure, 0, 100)) / 100.0f;

    cb.gamma = static_cast<float>(std::clamp(p.gamma, 0, 100)) / 100.0f;
    cb.nightMode = static_cast<float>(std::clamp(p.nightMode, 0, 100)) / 100.0f;
    cb.blackAndWhite = static_cast<float>(std::clamp(p.blackAndWhite, 0, 100)) / 100.0f;
    cb.vignette = static_cast<float>(std::clamp(p.vignette, 0, 100)) / 100.0f;

    cb.colorblindStrength = static_cast<float>(std::clamp(p.colorblindStrength, 0, 100)) / 100.0f;
    cb.colorblindType = static_cast<uint32_t>(std::clamp(p.colorblindType, 0, 2));

    uint32_t flags2 = 0;
    if (p.enableSharpen) flags2 |= FLAG2_ENABLE_SHARPEN;
    if (p.enableColorTemp) flags2 |= FLAG2_ENABLE_COLOR_TEMP;
    if (p.enableGammaExposure) flags2 |= FLAG2_ENABLE_GAMMA_EXP;
    if (p.enableNightMode) flags2 |= FLAG2_ENABLE_NIGHT_MODE;
    if (p.enableBlackAndWhite) flags2 |= FLAG2_ENABLE_BW;
    if (p.enableVignette) flags2 |= FLAG2_ENABLE_VIGNETTE;
    if (p.enableColorblind) flags2 |= FLAG2_ENABLE_COLORBLIND;
    cb.flags2 = flags2;
    cb.pad0 = 0.0f;
    return cb;
}

} // namespace

Dx11Overlay::~Dx11Overlay() {
    Shutdown();
}

LRESULT CALLBACK Dx11Overlay::OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCHITTEST) {
        return HTTRANSPARENT;
    }
    if (msg == WM_ERASEBKGND) {
        return 1;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool Dx11Overlay::CreateOverlayWindow(HINSTANCE hInst, std::string& outError) {
    screenW_ = std::max(GetSystemMetrics(SM_CXSCREEN), 640);
    screenH_ = std::max(GetSystemMetrics(SM_CYSCREEN), 480);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"AIDynamicVibranceOverlayWnd";
    RegisterClassExW(&wc);

    DWORD exStyle = WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT |
                    WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP;
    hwndOverlay_ = CreateWindowExW(
        exStyle,
        wc.lpszClassName,
        L"AI Dynamic Vibrance Overlay",
        WS_POPUP,
        0, 0, screenW_, screenH_,
        nullptr, nullptr, hInst, nullptr
    );
    if (!hwndOverlay_) {
        useDComp_ = false;
        exStyle = WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
        hwndOverlay_ = CreateWindowExW(
            exStyle,
            wc.lpszClassName,
            L"AI Dynamic Vibrance Overlay",
            WS_POPUP,
            0, 0, screenW_, screenH_,
            nullptr, nullptr, hInst, nullptr
        );
        if (!hwndOverlay_) {
            outError = "CreateWindowExW failed for overlay (" + std::to_string(GetLastError()) + ")";
            return false;
        }
        SetLayeredWindowAttributes(hwndOverlay_, 0, 255, LWA_ALPHA);
    }

    // Start capture-excluded; WGC per-window capture toggles WDA_NONE safely after InitWgc
    if (!SetWindowDisplayAffinity(hwndOverlay_, WDA_EXCLUDEFROMCAPTURE)) {
        outError = "SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE) failed (" + std::to_string(GetLastError()) + ")";
        return false;
    }
    overlayCapturable_ = false;

    ShowWindow(hwndOverlay_, SW_HIDE);
    return true;
}

bool Dx11Overlay::CreateDeviceAndResources(std::string& outError) {
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL chosen = D3D_FEATURE_LEVEL_11_0;
    UINT devFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        devFlags,
        levels,
        ARRAYSIZE(levels),
        D3D11_SDK_VERSION,
        &device_,
        &chosen,
        &context_
    );
    if (FAILED(hr)) {
        outError = "D3D11CreateDevice failed: " + HrToString(hr);
        return false;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    hr = device_.As(&dxgiDevice);
    if (FAILED(hr)) {
        outError = "Query IDXGIDevice failed: " + HrToString(hr);
        return false;
    }

    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(&adapter);
    if (SUCCEEDED(hr) && adapter) {
        DXGI_ADAPTER_DESC desc{};
        if (SUCCEEDED(adapter->GetDesc(&desc))) {
            adapterName_ = desc.Description;
        }
        ComPtr<IDXGIOutput> output;
        if (SUCCEEDED(adapter->EnumOutputs(0, &output)) && output) {
            output.As(&output1_);
        }
    }

    ComPtr<IDXGIFactory2> factory2;
    if (adapter) {
        adapter->GetParent(IID_PPV_ARGS(&factory2));
    }
    if (!factory2) {
        outError = "Failed to acquire IDXGIFactory2";
        return false;
    }

    // Compile HLSL shaders
    ComPtr<ID3DBlob> csBlob, vsBlob, psBlob;
    if (!CompileShaderEntry("CSAnalyze", "cs_5_0", csBlob, outError)) return false;
    if (!CompileShaderEntry("VSMain", "vs_5_0", vsBlob, outError)) return false;
    if (!CompileShaderEntry("PSEnhance", "ps_5_0", psBlob, outError)) return false;

    hr = device_->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &csAnalyze_);
    if (FAILED(hr)) { outError = "CreateComputeShader failed: " + HrToString(hr); return false; }

    hr = device_->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vsMain_);
    if (FAILED(hr)) { outError = "CreateVertexShader failed: " + HrToString(hr); return false; }

    hr = device_->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &psEnhance_);
    if (FAILED(hr)) { outError = "CreatePixelShader failed: " + HrToString(hr); return false; }

    // Constant buffer
    D3D11_BUFFER_DESC cbDesc{};
    cbDesc.ByteWidth = sizeof(EnhanceCBData);
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = device_->CreateBuffer(&cbDesc, nullptr, &constantBuffer_);
    if (FAILED(hr)) { outError = "CreateBuffer(CB) failed: " + HrToString(hr); return false; }

    // SceneStats structured buffer
    SceneStats initStats{ 0.25f, 0.25f, 0.06f, 0.20f };
    D3D11_BUFFER_DESC sbDesc{};
    sbDesc.ByteWidth = sizeof(SceneStats);
    sbDesc.Usage = D3D11_USAGE_DEFAULT;
    sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    sbDesc.StructureByteStride = sizeof(SceneStats);
    D3D11_SUBRESOURCE_DATA sbInit{};
    sbInit.pSysMem = &initStats;
    hr = device_->CreateBuffer(&sbDesc, &sbInit, &statsBuffer_);
    if (FAILED(hr)) { outError = "CreateBuffer(SceneStats) failed: " + HrToString(hr); return false; }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = 1;
    hr = device_->CreateShaderResourceView(statsBuffer_.Get(), &srvDesc, &statsSRV_);
    if (FAILED(hr)) { outError = "CreateSRV(SceneStats) failed: " + HrToString(hr); return false; }

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = 1;
    hr = device_->CreateUnorderedAccessView(statsBuffer_.Get(), &uavDesc, &statsUAV_);
    if (FAILED(hr)) { outError = "CreateUAV(SceneStats) failed: " + HrToString(hr); return false; }

    // Sampler & Rasterizer
    D3D11_SAMPLER_DESC sampDesc{};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
    hr = device_->CreateSamplerState(&sampDesc, &linearSampler_);
    if (FAILED(hr)) { outError = "CreateSamplerState failed: " + HrToString(hr); return false; }

    D3D11_RASTERIZER_DESC rsDesc{};
    rsDesc.FillMode = D3D11_FILL_SOLID;
    rsDesc.CullMode = D3D11_CULL_NONE;
    rsDesc.DepthClipEnable = FALSE;
    hr = device_->CreateRasterizerState(&rsDesc, &rasterState_);
    if (FAILED(hr)) { outError = "CreateRasterizerState failed: " + HrToString(hr); return false; }

    wgcFrameEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ComPtr<IInspectable> wgcInsp;
    if (SUCCEEDED(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(), &wgcInsp)) && wgcInsp) {
        wgcInsp.As(&wgcDevice_);
    }

    // SwapChain (DirectComposition preferred, HWND fallback if needed)
    DXGI_SWAP_CHAIN_DESC1 scDesc{};
    scDesc.Width = static_cast<UINT>(screenW_);
    scDesc.Height = static_cast<UINT>(screenH_);
    scDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scDesc.SampleDesc.Count = 1;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.BufferCount = 2;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scDesc.Scaling = DXGI_SCALING_STRETCH;
    scDesc.AlphaMode = useDComp_ ? DXGI_ALPHA_MODE_PREMULTIPLIED : DXGI_ALPHA_MODE_IGNORE;

    if (useDComp_) {
        hr = DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(&dcompDevice_));
        if (SUCCEEDED(hr) && dcompDevice_) {
            hr = factory2->CreateSwapChainForComposition(device_.Get(), &scDesc, nullptr, &swapChain_);
            if (SUCCEEDED(hr) && swapChain_) {
                hr = dcompDevice_->CreateTargetForHwnd(hwndOverlay_, TRUE, &dcompTarget_);
                if (SUCCEEDED(hr)) hr = dcompDevice_->CreateVisual(&dcompVisual_);
                if (SUCCEEDED(hr)) hr = dcompVisual_->SetContent(swapChain_.Get());
                if (SUCCEEDED(hr)) hr = dcompTarget_->SetRoot(nullptr);
                if (SUCCEEDED(hr)) hr = dcompDevice_->Commit();
                if (SUCCEEDED(hr)) {
                    visualAttached_ = false;
                }
            }
        }
        if (FAILED(hr) || !swapChain_) {
            dcompVisual_.Reset();
            dcompTarget_.Reset();
            dcompDevice_.Reset();
            swapChain_.Reset();
            useDComp_ = false;
        }
    }

    if (!useDComp_) {
        scDesc.Scaling = DXGI_SCALING_NONE;
        scDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        hr = factory2->CreateSwapChainForHwnd(device_.Get(), hwndOverlay_, &scDesc, nullptr, nullptr, &swapChain_);
        if (FAILED(hr)) {
            outError = "CreateSwapChainForHwnd failed: " + HrToString(hr);
            return false;
        }
    }

    ComPtr<ID3D11Texture2D> backBuf;
    hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuf));
    if (FAILED(hr)) { outError = "GetBuffer(0) failed: " + HrToString(hr); return false; }

    hr = device_->CreateRenderTargetView(backBuf.Get(), nullptr, &rtv_);
    if (FAILED(hr)) { outError = "CreateRenderTargetView failed: " + HrToString(hr); return false; }

    return true;
}

bool Dx11Overlay::Initialize(HINSTANCE hInst, std::string& outError) {
    if (!CreateOverlayWindow(hInst, outError)) {
        return false;
    }
    if (!CreateDeviceAndResources(outError)) {
        Shutdown();
        return false;
    }
    return true;
}

void Dx11Overlay::Shutdown() {
    ReleaseWgc();
    ReleaseDuplication();
    wgcDevice_.Reset();
    if (wgcFrameEvent_) {
        CloseHandle(wgcFrameEvent_);
        wgcFrameEvent_ = nullptr;
    }
    capturedSRV_.Reset();
    capturedTex_.Reset();
    rtv_.Reset();
    swapChain_.Reset();
    dcompVisual_.Reset();
    dcompTarget_.Reset();
    dcompDevice_.Reset();
    rasterState_.Reset();
    linearSampler_.Reset();
    statsUAV_.Reset();
    statsSRV_.Reset();
    statsBuffer_.Reset();
    constantBuffer_.Reset();
    psEnhance_.Reset();
    vsMain_.Reset();
    csAnalyze_.Reset();
    output1_.Reset();
    context_.Reset();
    device_.Reset();

    if (hwndOverlay_) {
        DestroyWindow(hwndOverlay_);
        hwndOverlay_ = nullptr;
    }
    targetHwnd_ = nullptr;
    active_ = false;
    hasCapturedFrame_ = false;
    overlayCapturable_ = false;
}

bool Dx11Overlay::SetOverlayCaptureVisibility(bool allowCapture) {
    if (overlayCapturable_ == allowCapture) return true;
    if (!hwndOverlay_) {
        overlayCapturable_ = false;
        return false;
    }
    if (SetWindowDisplayAffinity(hwndOverlay_, allowCapture ? WDA_NONE : WDA_EXCLUDEFROMCAPTURE)) {
        overlayCapturable_ = allowCapture;
        return true;
    }
    return false;
}

bool Dx11Overlay::InitDuplication() {
    // Critical feedback-loop invariant: exclude overlay from capture BEFORE DuplicateOutput
    ReleaseWgc();
    SetOverlayCaptureVisibility(false);
    if (duplication_) return true;
    if (!output1_ || !device_) return false;

    ULONGLONG now = GetTickCount64();
    if (lastDuplRetryMs_ != 0 && (now - lastDuplRetryMs_) < 200) {
        return false;
    }
    lastDuplRetryMs_ = now;

    HRESULT hr = output1_->DuplicateOutput(device_.Get(), &duplication_);
    return SUCCEEDED(hr) && duplication_;
}

void Dx11Overlay::ReleaseDuplication() {
    duplication_.Reset();
    hasCapturedFrame_ = false;
}

bool Dx11Overlay::InitWgc(HWND targetHwnd) {
    if (usingWgc_ && wgcSession_) return true;
    if (!targetHwnd || !IsWindow(targetHwnd) || IsIconic(targetHwnd) || !wgcDevice_) {
        return false;
    }

    ULONGLONG now = GetTickCount64();
    if (lastWgcRetryMs_ != 0 && (now - lastWgcRetryMs_) < 250) {
        return false;
    }
    lastWgcRetryMs_ = now;

    ReleaseDuplication();

    ComPtr<IGraphicsCaptureItemInterop> interop;
    HRESULT hr = RoGetActivationFactory(
        HStringReference(RuntimeClass_Windows_Graphics_Capture_GraphicsCaptureItem).Get(),
        IID_PPV_ARGS(&interop)
    );
    if (FAILED(hr) || !interop) return false;

    ComPtr<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem> item;
    hr = interop->CreateForWindow(targetHwnd, IID_PPV_ARGS(&item));
    if (FAILED(hr) || !item) return false;

    ABI::Windows::Graphics::SizeInt32 itemSize{};
    hr = item->get_Size(&itemSize);
    if (FAILED(hr) || itemSize.Width <= 0 || itemSize.Height <= 0) return false;

    ComPtr<ABI::Windows::Graphics::Capture::IDirect3D11CaptureFramePoolStatics2> poolStatics;
    hr = RoGetActivationFactory(
        HStringReference(RuntimeClass_Windows_Graphics_Capture_Direct3D11CaptureFramePool).Get(),
        IID_PPV_ARGS(&poolStatics)
    );
    if (FAILED(hr) || !poolStatics) return false;

    ComPtr<ABI::Windows::Graphics::Capture::IDirect3D11CaptureFramePool> pool;
    hr = poolStatics->CreateFreeThreaded(
        wgcDevice_.Get(),
        ABI::Windows::Graphics::DirectX::DirectXPixelFormat_B8G8R8A8UIntNormalized,
        2,
        itemSize,
        &pool
    );
    if (FAILED(hr) || !pool) return false;

    auto closePool = [&]() {
        ComPtr<ABI::Windows::Foundation::IClosable> cl;
        if (SUCCEEDED(pool.As(&cl)) && cl) cl->Close();
    };

    EventRegistrationToken tok{};
    auto cb = Microsoft::WRL::Callback<
        Microsoft::WRL::Implements<
            Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
            __FITypedEventHandler_2_Windows__CGraphics__CCapture__CDirect3D11CaptureFramePool_IInspectable,
            Microsoft::WRL::FtmBase>
    >([ev = wgcFrameEvent_](
        ABI::Windows::Graphics::Capture::IDirect3D11CaptureFramePool*,
        IInspectable*
    ) -> HRESULT {
        if (ev) SetEvent(ev);
        return S_OK;
    });
    if (!cb || FAILED(pool->add_FrameArrived(cb.Get(), &tok))) {
        closePool();
        return false;
    }

    ComPtr<ABI::Windows::Graphics::Capture::IGraphicsCaptureSession> session;
    hr = pool->CreateCaptureSession(item.Get(), &session);
    if (FAILED(hr) || !session) {
        pool->remove_FrameArrived(tok);
        closePool();
        return false;
    }

    ComPtr<ABI::Windows::Graphics::Capture::IGraphicsCaptureSession2> session2;
    if (SUCCEEDED(session.As(&session2)) && session2) {
        session2->put_IsCursorCaptureEnabled(FALSE);
    }
    ComPtr<ABI::Windows::Graphics::Capture::IGraphicsCaptureSession3> session3;
    if (SUCCEEDED(session.As(&session3)) && session3) {
        session3->put_IsBorderRequired(FALSE);
    }

    if (wgcFrameEvent_) ResetEvent(wgcFrameEvent_);
    hr = session->StartCapture();
    if (FAILED(hr)) {
        ComPtr<ABI::Windows::Foundation::IClosable> sCl;
        if (SUCCEEDED(session.As(&sCl)) && sCl) sCl->Close();
        pool->remove_FrameArrived(tok);
        closePool();
        return false;
    }

    wgcItem_ = std::move(item);
    wgcFramePool_ = std::move(pool);
    wgcSession_ = std::move(session);
    wgcFrameToken_ = tok;
    wgcTokenRegistered_ = true;
    wgcPoolSize_ = itemSize;
    usingWgc_ = true;
    hasCapturedFrame_ = false;

    // Safe to allow OBS / Windows Screenshots to capture our overlay now that we capture ONLY targetHwnd
    SetOverlayCaptureVisibility(true);
    return true;
}

void Dx11Overlay::ReleaseWgc() {
    SetOverlayCaptureVisibility(false);
    if (wgcFramePool_ && wgcTokenRegistered_) {
        wgcFramePool_->remove_FrameArrived(wgcFrameToken_);
        wgcTokenRegistered_ = false;
    }
    if (wgcSession_) {
        ComPtr<ABI::Windows::Foundation::IClosable> cl;
        if (SUCCEEDED(wgcSession_.As(&cl)) && cl) cl->Close();
        wgcSession_.Reset();
    }
    if (wgcFramePool_) {
        ComPtr<ABI::Windows::Foundation::IClosable> cl;
        if (SUCCEEDED(wgcFramePool_.As(&cl)) && cl) cl->Close();
        wgcFramePool_.Reset();
    }
    wgcItem_.Reset();
    wgcPoolSize_ = { 0, 0 };
    usingWgc_ = false;
    hasCapturedFrame_ = false;
}

void Dx11Overlay::SetActive(bool active, HWND sidebarHwnd, HWND targetHwnd) {
    lastSidebarHwnd_ = sidebarHwnd;
    if (active && IsExclusiveFullscreenActive()) {
        active = false;
    }
    bool targetChanged = (targetHwnd != targetHwnd_);
    targetHwnd_ = targetHwnd;

    if (active_ == active) {
        if (active_ && targetChanged) {
            ReleaseWgc();
            ReleaseDuplication();
            lastWgcRetryMs_ = 0;
            if (!(targetHwnd_ && IsWindow(targetHwnd_) && !IsIconic(targetHwnd_) && InitWgc(targetHwnd_))) {
                InitDuplication();
            }
        } else if (!active_ && hwndOverlay_ && IsWindowVisible(hwndOverlay_) &&
                   (!useDComp_ || !sidebarHwnd || !IsWindowVisible(sidebarHwnd) || IsExclusiveFullscreenActive())) {
            ShowWindow(hwndOverlay_, SW_HIDE);
        }
        return;
    }
    active_ = active;
    if (!hwndOverlay_) return;

    if (active_) {
        visualAttached_ = false;
        lastWgcRetryMs_ = 0;
        if (!(targetHwnd_ && IsWindow(targetHwnd_) && !IsIconic(targetHwnd_) && InitWgc(targetHwnd_))) {
            InitDuplication();
        }
    } else {
        if (useDComp_ && dcompTarget_ && dcompDevice_) {
            if (visualAttached_) {
                dcompTarget_->SetRoot(nullptr);
                dcompDevice_->Commit();
                visualAttached_ = false;
            }
        }
        if (!useDComp_ || !sidebarHwnd || !IsWindowVisible(sidebarHwnd) || IsExclusiveFullscreenActive()) {
            ShowWindow(hwndOverlay_, SW_HIDE);
        }
        ReleaseWgc();
        ReleaseDuplication();
    }
}

void Dx11Overlay::EnsureZOrder(HWND sidebarHwnd) {
    lastSidebarHwnd_ = sidebarHwnd;
    if (IsExclusiveFullscreenActive()) {
        SetActive(false, nullptr);
        if (hwndOverlay_ && IsWindowVisible(hwndOverlay_)) {
            ShowWindow(hwndOverlay_, SW_HIDE);
        }
        return;
    }
    bool sidebarVisible = (sidebarHwnd && IsWindowVisible(sidebarHwnd));
    if (!active_ && !sidebarVisible) {
        if (hwndOverlay_ && IsWindowVisible(hwndOverlay_)) {
            ShowWindow(hwndOverlay_, SW_HIDE);
        }
        return;
    }
    if (sidebarVisible) {
        SetWindowPos(
            sidebarHwnd,
            HWND_TOPMOST,
            0, 0, 0, 0,
            SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE
        );
        if (active_ && hwndOverlay_ && IsWindowVisible(hwndOverlay_) &&
            GetWindow(hwndOverlay_, GW_HWNDPREV) != sidebarHwnd) {
            SetWindowPos(
                hwndOverlay_,
                sidebarHwnd,
                0, 0, screenW_, screenH_,
                SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE
            );
        }
    }
}

bool Dx11Overlay::EnsureCapturedTexture(const D3D11_TEXTURE2D_DESC& srcDesc) {
    if (capturedTex_ &&
        capturedDesc_.Width == srcDesc.Width &&
        capturedDesc_.Height == srcDesc.Height &&
        capturedDesc_.Format == srcDesc.Format) {
        return true;
    }

    capturedSRV_.Reset();
    capturedTex_.Reset();

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = srcDesc.Width;
    td.Height = srcDesc.Height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = srcDesc.Format;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = device_->CreateTexture2D(&td, nullptr, &capturedTex_);
    if (FAILED(hr)) return false;

    hr = device_->CreateShaderResourceView(capturedTex_.Get(), nullptr, &capturedSRV_);
    if (FAILED(hr)) {
        capturedTex_.Reset();
        return false;
    }
    capturedDesc_ = td;
    return true;
}

bool Dx11Overlay::RenderInternal(const FilterParams& params, bool splitPreview, float splitPos) {
    if (!capturedSRV_ || !rtv_ || !swapChain_) return false;

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context_->Map(constantBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        EnhanceCBData cb = BuildCB(
            params,
            splitPreview,
            splitPos,
            static_cast<int>(capturedDesc_.Width),
            static_cast<int>(capturedDesc_.Height),
            0.12f
        );
        std::memcpy(mapped.pData, &cb, sizeof(cb));
        context_->Unmap(constantBuffer_.Get(), 0);
    }

    // Pass 1: Compute Shader scene luminance / darkness / chroma analysis
    ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
    ID3D11UnorderedAccessView* nullUAVs[1] = { nullptr };
    context_->PSSetShaderResources(0, 2, nullSRVs);

    ID3D11Buffer* cbArr[1] = { constantBuffer_.Get() };
    ID3D11ShaderResourceView* csSRVs[1] = { capturedSRV_.Get() };
    ID3D11UnorderedAccessView* csUAVs[1] = { statsUAV_.Get() };
    context_->CSSetShader(csAnalyze_.Get(), nullptr, 0);
    context_->CSSetConstantBuffers(0, 1, cbArr);
    context_->CSSetShaderResources(0, 1, csSRVs);
    context_->CSSetUnorderedAccessViews(0, 1, csUAVs, nullptr);
    context_->Dispatch(1, 1, 1);

    context_->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
    context_->CSSetShaderResources(0, 1, nullSRVs);

    // Pass 2: Pixel Shader enhancement (windowed rect under WGC, fullscreen under Desktop Duplication)
    D3D11_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = static_cast<float>(screenW_);
    vp.Height = static_cast<float>(screenH_);
    vp.MaxDepth = 1.0f;

    if (usingWgc_ && targetHwnd_ && IsWindow(targetHwnd_)) {
        RECT winRc{};
        if (FAILED(DwmGetWindowAttribute(targetHwnd_, DWMWA_EXTENDED_FRAME_BOUNDS, &winRc, sizeof(winRc)))) {
            GetWindowRect(targetHwnd_, &winRc);
        }
        int w = winRc.right - winRc.left;
        int h = winRc.bottom - winRc.top;
        if (w <= 0 || h <= 0 || winRc.right <= 0 || winRc.bottom <= 0 ||
            winRc.left >= screenW_ || winRc.top >= screenH_) {
            return false;
        }
        vp.TopLeftX = static_cast<float>(winRc.left);
        vp.TopLeftY = static_cast<float>(winRc.top);
        vp.Width = static_cast<float>(w);
        vp.Height = static_cast<float>(h);
    }

    if (useDComp_) {
        const float clearZero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        context_->ClearRenderTargetView(rtv_.Get(), clearZero);
    }

    ID3D11RenderTargetView* rtvArr[1] = { rtv_.Get() };
    ID3D11ShaderResourceView* psSRVs[2] = { capturedSRV_.Get(), statsSRV_.Get() };
    ID3D11SamplerState* psSamps[1] = { linearSampler_.Get() };

    context_->OMSetRenderTargets(1, rtvArr, nullptr);
    context_->RSSetViewports(1, &vp);
    context_->RSSetState(rasterState_.Get());
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    context_->VSSetShader(vsMain_.Get(), nullptr, 0);
    context_->PSSetShader(psEnhance_.Get(), nullptr, 0);
    context_->PSSetConstantBuffers(0, 1, cbArr);
    context_->PSSetShaderResources(0, 2, psSRVs);
    context_->PSSetSamplers(0, 1, psSamps);

    context_->Draw(3, 0);

    context_->PSSetShaderResources(0, 2, nullSRVs);

    HRESULT hr = swapChain_->Present(1, 0);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        return false;
    }
    if (IsExclusiveFullscreenActive()) {
        return false;
    }
    if (hwndOverlay_ && !IsWindowVisible(hwndOverlay_)) {
        HWND insertAfter = (lastSidebarHwnd_ && IsWindowVisible(lastSidebarHwnd_)) ? lastSidebarHwnd_ : HWND_TOPMOST;
        SetWindowPos(
            hwndOverlay_,
            insertAfter,
            0, 0, screenW_, screenH_,
            SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW
        );
    }
    if (useDComp_ && !visualAttached_ && dcompTarget_ && dcompVisual_ && dcompDevice_) {
        dcompTarget_->SetRoot(dcompVisual_.Get());
        dcompDevice_->Commit();
        visualAttached_ = true;
    }
    return true;
}

bool Dx11Overlay::TickAndRender(const FilterParams& params, bool splitPreview, float splitPos, bool forceRedraw) {
    if (!active_) return false;
    if (!usingWgc_ && !duplication_) {
        if (!(targetHwnd_ && IsWindow(targetHwnd_) && !IsIconic(targetHwnd_) && InitWgc(targetHwnd_))) {
            if (!InitDuplication()) {
                return false;
            }
        }
    }

    if (usingWgc_) {
        if (!targetHwnd_ || !IsWindow(targetHwnd_) || IsIconic(targetHwnd_) || !wgcFramePool_) {
            ReleaseWgc();
            return false;
        }

        ComPtr<ABI::Windows::Graphics::Capture::IDirect3D11CaptureFrame> latestFrame;
        auto drainFrames = [&]() {
            while (true) {
                ComPtr<ABI::Windows::Graphics::Capture::IDirect3D11CaptureFrame> next;
                if (FAILED(wgcFramePool_->TryGetNextFrame(&next)) || !next) {
                    break;
                }
                if (latestFrame) {
                    ComPtr<ABI::Windows::Foundation::IClosable> prevCl;
                    if (SUCCEEDED(latestFrame.As(&prevCl)) && prevCl) prevCl->Close();
                }
                latestFrame = std::move(next);
            }
        };

        drainFrames();
        if (!latestFrame && wgcFrameEvent_) {
            WaitForSingleObject(wgcFrameEvent_, 8);
            drainFrames();
        }

        if (!latestFrame) {
            if (forceRedraw && hasCapturedFrame_) {
                return RenderInternal(params, splitPreview, splitPos);
            }
            return false;
        }

        ABI::Windows::Graphics::SizeInt32 contentSize{};
        if (SUCCEEDED(latestFrame->get_ContentSize(&contentSize)) &&
            contentSize.Width > 0 && contentSize.Height > 0 &&
            (contentSize.Width != wgcPoolSize_.Width || contentSize.Height != wgcPoolSize_.Height)) {
            wgcPoolSize_ = contentSize;
            wgcFramePool_->Recreate(
                wgcDevice_.Get(),
                ABI::Windows::Graphics::DirectX::DirectXPixelFormat_B8G8R8A8UIntNormalized,
                2,
                wgcPoolSize_
            );
        }

        bool copied = false;
        ComPtr<ABI::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface> surf;
        if (SUCCEEDED(latestFrame->get_Surface(&surf)) && surf) {
            ComPtr<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> access;
            if (SUCCEEDED(surf.As(&access)) && access) {
                ComPtr<ID3D11Texture2D> wgcTex;
                if (SUCCEEDED(access->GetInterface(IID_PPV_ARGS(&wgcTex))) && wgcTex) {
                    D3D11_TEXTURE2D_DESC desc{};
                    wgcTex->GetDesc(&desc);
                    if (EnsureCapturedTexture(desc)) {
                        context_->CopyResource(capturedTex_.Get(), wgcTex.Get());
                        hasCapturedFrame_ = true;
                        copied = true;
                    }
                }
            }
        }

        ComPtr<ABI::Windows::Foundation::IClosable> frameCl;
        if (SUCCEEDED(latestFrame.As(&frameCl)) && frameCl) {
            frameCl->Close();
        }

        if (copied) {
            return RenderInternal(params, splitPreview, splitPos);
        }
        return false;
    }

    DXGI_OUTDUPL_FRAME_INFO frameInfo{};
    ComPtr<IDXGIResource> desktopRes;
    HRESULT hr = duplication_->AcquireNextFrame(8, &frameInfo, &desktopRes);

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        if (forceRedraw && hasCapturedFrame_) {
            return RenderInternal(params, splitPreview, splitPos);
        }
        return false;
    }

    if (hr == DXGI_ERROR_ACCESS_LOST || hr == DXGI_ERROR_INVALID_CALL) {
        ReleaseDuplication();
        return false;
    }

    if (FAILED(hr) || !desktopRes) {
        return false;
    }

    ComPtr<ID3D11Texture2D> desktopTex;
    bool copied = false;
    if (SUCCEEDED(desktopRes.As(&desktopTex)) && desktopTex) {
        D3D11_TEXTURE2D_DESC desc{};
        desktopTex->GetDesc(&desc);
        if (EnsureCapturedTexture(desc)) {
            context_->CopyResource(capturedTex_.Get(), desktopTex.Get());
            hasCapturedFrame_ = true;
            copied = true;
        }
    }
    duplication_->ReleaseFrame();

    if (copied) {
        return RenderInternal(params, splitPreview, splitPos);
    }
    return false;
}

bool Dx11Overlay::RunSelfTest(std::string& outReport) {
    std::ostringstream rep;

    // 1. Test D3D11 hardware device & adapter
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL chosen = D3D_FEATURE_LEVEL_11_0;
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels),
        D3D11_SDK_VERSION, &dev, &chosen, &ctx
    );
    if (FAILED(hr)) {
        outReport = "D3D11CreateDevice failed: " + HrToString(hr);
        return false;
    }

    ComPtr<IDXGIDevice> dxgiDev;
    dev.As(&dxgiDev);
    ComPtr<IDXGIAdapter> adapter;
    dxgiDev->GetAdapter(&adapter);
    DXGI_ADAPTER_DESC adesc{};
    adapter->GetDesc(&adesc);
    char adapterBuf[256] = {};
    WideCharToMultiByte(CP_UTF8, 0, adesc.Description, -1, adapterBuf, sizeof(adapterBuf), nullptr, nullptr);
    rep << "[PASS] D3D11 Adapter: " << adapterBuf << "\n";

    // 2. Compile all 3 HLSL entry points
    std::string err;
    ComPtr<ID3DBlob> csBlob, vsBlob, psBlob;
    if (!CompileShaderEntry("CSAnalyze", "cs_5_0", csBlob, err) ||
        !CompileShaderEntry("VSMain", "vs_5_0", vsBlob, err) ||
        !CompileShaderEntry("PSEnhance", "ps_5_0", psBlob, err)) {
        outReport = err;
        return false;
    }
    ComPtr<ID3D11ComputeShader> cs;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    if (FAILED(dev->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &cs)) ||
        FAILED(dev->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs)) ||
        FAILED(dev->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps))) {
        outReport = "Failed creating D3D11 shader objects.";
        return false;
    }
    rep << "[PASS] HLSL Shaders compiled cleanly (CSAnalyze, VSMain, PSEnhance)\n";

    // 3. Test DirectComposition + WDA_EXCLUDEFROMCAPTURE on a hidden window
    ComPtr<IDCompositionDevice> dcomp;
    hr = DCompositionCreateDevice(dxgiDev.Get(), IID_PPV_ARGS(&dcomp));
    if (FAILED(hr) || !dcomp) {
        outReport = "DCompositionCreateDevice failed: " + HrToString(hr);
        return false;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"ADV_SelfTestWnd";
    RegisterClassExW(&wc);
    HWND testWnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP,
        wc.lpszClassName, L"Test", WS_POPUP, 0, 0, 64, 64,
        nullptr, nullptr, wc.hInstance, nullptr
    );
    BOOL affOk = testWnd ? SetWindowDisplayAffinity(testWnd, WDA_EXCLUDEFROMCAPTURE) : FALSE;

    // 3B. Verify WinRT Windows Graphics Capture (OBS-Recordable Mode)
    HWND wgcWnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
        wc.lpszClassName, L"WGC Test", WS_POPUP, -200, -200, 64, 64,
        nullptr, nullptr, wc.hInstance, nullptr
    );
    bool wgcOk = false;
    std::string wgcErr = "WGC window creation failed.";
    if (wgcWnd) {
        SetLayeredWindowAttributes(wgcWnd, 0, 1, LWA_ALPHA);
        ShowWindow(wgcWnd, SW_SHOWNOACTIVATE);
        UpdateWindow(wgcWnd);

        ComPtr<IInspectable> wgcInsp;
        ComPtr<ABI::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice> wgcDev;
        ComPtr<IGraphicsCaptureItemInterop> interop;
        ComPtr<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem> item;
        ComPtr<ABI::Windows::Graphics::Capture::IDirect3D11CaptureFramePoolStatics2> poolStatics;
        ComPtr<ABI::Windows::Graphics::Capture::IDirect3D11CaptureFramePool> pool;
        ComPtr<ABI::Windows::Graphics::Capture::IGraphicsCaptureSession> sess;

        if (FAILED(hr = CreateDirect3D11DeviceFromDXGIDevice(dxgiDev.Get(), &wgcInsp)) ||
            FAILED(hr = wgcInsp.As(&wgcDev))) {
            wgcErr = "CreateDirect3D11DeviceFromDXGIDevice failed: " + HrToString(hr);
        } else if (FAILED(hr = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_Graphics_Capture_GraphicsCaptureItem).Get(), IID_PPV_ARGS(&interop)))) {
            wgcErr = "RoGetActivationFactory(GraphicsCaptureItem) failed: " + HrToString(hr);
        } else if (FAILED(hr = interop->CreateForWindow(wgcWnd, IID_PPV_ARGS(&item)))) {
            wgcErr = "IGraphicsCaptureItemInterop::CreateForWindow failed: " + HrToString(hr);
        } else if (FAILED(hr = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_Graphics_Capture_Direct3D11CaptureFramePool).Get(), IID_PPV_ARGS(&poolStatics)))) {
            wgcErr = "RoGetActivationFactory(Direct3D11CaptureFramePool) failed: " + HrToString(hr);
        } else if (FAILED(hr = poolStatics->CreateFreeThreaded(wgcDev.Get(), ABI::Windows::Graphics::DirectX::DirectXPixelFormat_B8G8R8A8UIntNormalized, 2, { 64, 64 }, &pool))) {
            wgcErr = "CreateFreeThreaded failed: " + HrToString(hr);
        } else if (FAILED(hr = pool->CreateCaptureSession(item.Get(), &sess)) || !sess) {
            wgcErr = "CreateCaptureSession failed: " + HrToString(hr);
        } else {
            ComPtr<ABI::Windows::Graphics::Capture::IGraphicsCaptureSession3> sess3;
            if (SUCCEEDED(sess.As(&sess3)) && sess3) {
                sess3->put_IsBorderRequired(FALSE);
            }
            wgcOk = true;
        }
        if (sess) {
            ComPtr<ABI::Windows::Foundation::IClosable> cl;
            if (SUCCEEDED(sess.As(&cl)) && cl) cl->Close();
        }
        if (pool) {
            ComPtr<ABI::Windows::Foundation::IClosable> cl;
            if (SUCCEEDED(pool.As(&cl)) && cl) cl->Close();
        }
        DestroyWindow(wgcWnd);
    }

    if (testWnd) DestroyWindow(testWnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    if (!affOk) {
        outReport = "SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE) failed.";
        return false;
    }
    if (!wgcOk) {
        outReport = wgcErr;
        return false;
    }
    rep << "[PASS] DirectComposition + WDA_EXCLUDEFROMCAPTURE verified\n";
    rep << "[PASS] Windows Graphics Capture (OBS-Recordable Mode) verified\n";

    // 4. Execute dummy 256x256 GPU Compute + Pixel enhancement pass
    constexpr int W = 256, H = 256;
    std::vector<uint32_t> srcPixels(W * H, (0xFFu << 24) | (60u << 16) | (45u << 8) | 30u); // B=30, G=45, R=60

    D3D11_TEXTURE2D_DESC td{};
    td.Width = W; td.Height = H; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initData{ srcPixels.data(), W * 4, 0 };
    ComPtr<ID3D11Texture2D> srcTex, dstTex, stagingTex;
    if (FAILED(dev->CreateTexture2D(&td, &initData, &srcTex))) {
        outReport = "Failed creating dummy input texture.";
        return false;
    }
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (FAILED(dev->CreateTexture2D(&td, nullptr, &dstTex))) {
        outReport = "Failed creating dummy render target texture.";
        return false;
    }
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(dev->CreateTexture2D(&td, nullptr, &stagingTex))) {
        outReport = "Failed creating staging texture.";
        return false;
    }

    ComPtr<ID3D11ShaderResourceView> srcSRV;
    ComPtr<ID3D11RenderTargetView> dstRTV;
    dev->CreateShaderResourceView(srcTex.Get(), nullptr, &srcSRV);
    dev->CreateRenderTargetView(dstTex.Get(), nullptr, &dstRTV);

    // CB + SceneStats + Sampler + RS
    FilterParams fp{};
    fp.enableDynamicVibrance = true;
    fp.intensity = 50;
    fp.saturationBoost = 50;
    EnhanceCBData cb = BuildCB(fp, false, 0.5f, W, H, 1.0f);
    D3D11_BUFFER_DESC cbd{ sizeof(EnhanceCBData), D3D11_USAGE_IMMUTABLE, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };
    D3D11_SUBRESOURCE_DATA cbInit{ &cb, 0, 0 };
    ComPtr<ID3D11Buffer> cbuf;
    dev->CreateBuffer(&cbd, &cbInit, &cbuf);

    SceneStats stInit{ 0.05f, 0.9f, 0.03f, 0.0f };
    D3D11_BUFFER_DESC sbd{ sizeof(SceneStats), D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, sizeof(SceneStats) };
    D3D11_SUBRESOURCE_DATA sbInit{ &stInit, 0, 0 };
    ComPtr<ID3D11Buffer> sbuf, sStage;
    dev->CreateBuffer(&sbd, &sbInit, &sbuf);
    D3D11_BUFFER_DESC sStageDesc{ sizeof(SceneStats), D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, 0 };
    dev->CreateBuffer(&sStageDesc, nullptr, &sStage);

    D3D11_SHADER_RESOURCE_VIEW_DESC srvd{}; srvd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; srvd.Buffer.NumElements = 1;
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavd{}; uavd.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; uavd.Buffer.NumElements = 1;
    ComPtr<ID3D11ShaderResourceView> stSRV;
    ComPtr<ID3D11UnorderedAccessView> stUAV;
    dev->CreateShaderResourceView(sbuf.Get(), &srvd, &stSRV);
    dev->CreateUnorderedAccessView(sbuf.Get(), &uavd, &stUAV);

    D3D11_SAMPLER_DESC sd{}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    ComPtr<ID3D11SamplerState> samp;
    dev->CreateSamplerState(&sd, &samp);

    D3D11_RASTERIZER_DESC rsd{}; rsd.FillMode = D3D11_FILL_SOLID; rsd.CullMode = D3D11_CULL_NONE;
    ComPtr<ID3D11RasterizerState> rs;
    dev->CreateRasterizerState(&rsd, &rs);

    ID3D11Buffer* cbs[1] = { cbuf.Get() };
    ID3D11ShaderResourceView* csIn[1] = { srcSRV.Get() };
    ID3D11UnorderedAccessView* csOut[1] = { stUAV.Get() };
    ctx->CSSetShader(cs.Get(), nullptr, 0);
    ctx->CSSetConstantBuffers(0, 1, cbs);
    ctx->CSSetShaderResources(0, 1, csIn);
    ctx->CSSetUnorderedAccessViews(0, 1, csOut, nullptr);
    ctx->Dispatch(1, 1, 1);

    ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
    ID3D11ShaderResourceView* nullSRV[2] = { nullptr, nullptr };
    ctx->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
    ctx->CSSetShaderResources(0, 1, nullSRV);

    D3D11_VIEWPORT vp{ 0.0f, 0.0f, static_cast<float>(W), static_cast<float>(H), 0.0f, 1.0f };
    ID3D11RenderTargetView* rtvs[1] = { dstRTV.Get() };
    ID3D11ShaderResourceView* psIn[2] = { srcSRV.Get(), stSRV.Get() };
    ID3D11SamplerState* samps[1] = { samp.Get() };
    ctx->OMSetRenderTargets(1, rtvs, nullptr);
    ctx->RSSetViewports(1, &vp);
    ctx->RSSetState(rs.Get());
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vs.Get(), nullptr, 0);
    ctx->PSSetShader(ps.Get(), nullptr, 0);
    ctx->PSSetConstantBuffers(0, 1, cbs);
    ctx->PSSetShaderResources(0, 2, psIn);
    ctx->PSSetSamplers(0, 1, samps);
    ctx->Draw(3, 0);
    ctx->PSSetShaderResources(0, 2, nullSRV);

    ctx->CopyResource(stagingTex.Get(), dstTex.Get());
    ctx->CopyResource(sStage.Get(), sbuf.Get());

    D3D11_MAPPED_SUBRESOURCE mapPix{}, mapStat{};
    if (FAILED(ctx->Map(stagingTex.Get(), 0, D3D11_MAP_READ, 0, &mapPix)) ||
        FAILED(ctx->Map(sStage.Get(), 0, D3D11_MAP_READ, 0, &mapStat))) {
        outReport = "Failed mapping self-test GPU readback.";
        return false;
    }

    const uint8_t* px = static_cast<const uint8_t*>(mapPix.pData); // BGRA
    int outB = px[0], outG = px[1], outR = px[2], outA = px[3];
    const SceneStats* stOut = static_cast<const SceneStats*>(mapStat.pData);
    SceneStats statsCopy = *stOut;
    ctx->Unmap(stagingTex.Get(), 0);
    ctx->Unmap(sStage.Get(), 0);

    if (outA != 255 || (outR <= 60 && outG <= 45)) {
        outReport = "Shader output validation failed (expected brightened/vibrant pixel).";
        return false;
    }

    rep << "[PASS] GPU Compute + Pixel 256x256 pass: Dark Input RGB(60,45,30) -> Enhanced RGB("
        << outR << "," << outG << "," << outB << "), SceneStats(avgLuma=" << statsCopy.avgLuma
        << ", darkRatio=" << statsCopy.darkRatio << ")\n";

    outReport = rep.str();
    return true;
}
