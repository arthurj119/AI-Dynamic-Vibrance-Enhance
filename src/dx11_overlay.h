#pragma once

#include "types.h"

#include <windows.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_2.h>
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

    void SetActive(bool active, HWND sidebarHwnd = nullptr);
    bool IsActive() const { return active_; }

    // Ensures overlay sits topmost (and below sidebar if sidebarHwnd != nullptr and visible)
    void EnsureZOrder(HWND sidebarHwnd);

    // Captures desktop & renders enhanced frame. Returns true if Present was called.
    bool TickAndRender(const FilterParams& params, bool splitPreview, float splitPos, bool forceRedraw);

    std::wstring GetAdapterName() const { return adapterName_; }
    int ScreenWidth() const { return screenW_; }
    int ScreenHeight() const { return screenH_; }

    // Runs standalone GPU shader + DComp + WDA_EXCLUDEFROMCAPTURE verification
    static bool RunSelfTest(std::string& outReport);

private:
    bool CreateOverlayWindow(HINSTANCE hInst, std::string& outError);
    bool CreateDeviceAndResources(std::string& outError);
    bool InitDuplication();
    void ReleaseDuplication();
    bool EnsureCapturedTexture(const D3D11_TEXTURE2D_DESC& srcDesc);
    bool RenderInternal(const FilterParams& params, bool splitPreview, float splitPos);

    static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND hwndOverlay_ = nullptr;
    int screenW_ = 1920;
    int screenH_ = 1080;
    bool active_ = false;
    bool useDComp_ = true;
    bool visualAttached_ = false;
    bool hasCapturedFrame_ = false;
    ULONGLONG lastDuplRetryMs_ = 0;
    HWND lastSidebarHwnd_ = nullptr;

    std::wstring adapterName_ = L"Unknown GPU";

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGIOutput1> output1_;
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication_;

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
