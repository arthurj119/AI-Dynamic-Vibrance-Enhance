#pragma once

#include "types.h"

#include <windows.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <windows.graphics.capture.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <wrl/client.h>

#include <string>

class Dx11Overlay {
public:
    Dx11Overlay() = default;
    ~Dx11Overlay();

    Dx11Overlay(const Dx11Overlay&) = delete;
    Dx11Overlay& operator=(const Dx11Overlay&) = delete;

    bool Initialize(HINSTANCE hInst, std::string& outError);
    void Shutdown();

    void SetActive(bool active, HWND sidebarHwnd = nullptr, HWND targetHwnd = nullptr);
    bool IsActive() const { return active_; }
    bool IsRecordableWgc() const { return usingWgc_; }

    // Ensures overlay sits topmost (and below sidebar if sidebarHwnd != nullptr and visible)
    void EnsureZOrder(HWND sidebarHwnd);

    // Captures target window (WGC) or desktop (DD) & renders enhanced frame.
    bool TickAndRender(const FilterParams& params, bool splitPreview, float splitPos, bool forceRedraw);

    std::wstring GetAdapterName() const { return adapterName_; }
    int ScreenWidth() const { return screenW_; }
    int ScreenHeight() const { return screenH_; }

    // Runs standalone GPU shader + DComp + WGC capture verification
    static bool RunSelfTest(std::string& outReport);

private:
    bool CreateOverlayWindow(HINSTANCE hInst, std::string& outError);
    bool CreateDeviceAndResources(std::string& outError);
    bool InitDuplication();
    void ReleaseDuplication();
    bool InitWgc(HWND targetHwnd);
    void ReleaseWgc();
    bool SetOverlayCaptureVisibility(bool allowCapture);
    bool EnsureCapturedTexture(const D3D11_TEXTURE2D_DESC& srcDesc);
    bool RenderInternal(const FilterParams& params, bool splitPreview, float splitPos);

    static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND hwndOverlay_ = nullptr;
    HWND targetHwnd_ = nullptr;
    int screenW_ = 1920;
    int screenH_ = 1080;
    bool active_ = false;
    bool useDComp_ = true;
    bool visualAttached_ = false;
    bool hasCapturedFrame_ = false;
    bool usingWgc_ = false;
    bool overlayCapturable_ = false;
    ULONGLONG lastDuplRetryMs_ = 0;
    ULONGLONG lastWgcRetryMs_ = 0;
    HWND lastSidebarHwnd_ = nullptr;

    std::wstring adapterName_ = L"Unknown GPU";

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGIOutput1> output1_;
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication_;

    HANDLE wgcFrameEvent_ = nullptr;
    ABI::Windows::Graphics::SizeInt32 wgcPoolSize_{ 0, 0 };
    EventRegistrationToken wgcFrameToken_{};
    bool wgcTokenRegistered_ = false;
    Microsoft::WRL::ComPtr<ABI::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice> wgcDevice_;
    Microsoft::WRL::ComPtr<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem> wgcItem_;
    Microsoft::WRL::ComPtr<ABI::Windows::Graphics::Capture::IDirect3D11CaptureFramePool> wgcFramePool_;
    Microsoft::WRL::ComPtr<ABI::Windows::Graphics::Capture::IGraphicsCaptureSession> wgcSession_;

    Microsoft::WRL::ComPtr<IDCompositionDevice> dcompDevice_;
    Microsoft::WRL::ComPtr<IDCompositionTarget> dcompTarget_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> dcompVisual_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv_;

    Microsoft::WRL::ComPtr<ID3D11ComputeShader> csAnalyze_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vsMain_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> psEnhance_;

    Microsoft::WRL::ComPtr<ID3D11Buffer> constantBuffer_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> statsBuffer_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> statsSRV_;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> statsUAV_;

    Microsoft::WRL::ComPtr<ID3D11SamplerState> linearSampler_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterState_;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> capturedTex_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> capturedSRV_;
    D3D11_TEXTURE2D_DESC capturedDesc_ = {};
};
