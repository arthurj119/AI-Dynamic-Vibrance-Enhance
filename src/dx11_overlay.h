#pragma once

#include "types.h"

#include <windows.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_6.h>
#include <windows.graphics.capture.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct ScalingFgShaders {
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> csLumaDown;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> csFlowCoarse;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> csFlowRefine;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> csFlowFinalize;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> csFlowStats;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vsFullScreen;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> psFrameGen;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> psFsrEasu;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> psFsrRcas;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> psSpatialScale;
    Microsoft::WRL::ComPtr<ID3D11Buffer> cb;
    Microsoft::WRL::ComPtr<ID3D11Buffer> statsBuf;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> statsSRV;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> statsUAV;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> linearSamp;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rs;
};

struct FlowPyramid {
    UINT srcW = 0;
    UINT srcH = 0;
    int scalePct = 0;
    UINT w[3] = {};
    UINT h[3] = {};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> lumaPrev[3];
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> lumaPrevSRV[3];
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> lumaPrevUAV[3];
    Microsoft::WRL::ComPtr<ID3D11Texture2D> lumaCurr[3];
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> lumaCurrSRV[3];
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> lumaCurrUAV[3];
    Microsoft::WRL::ComPtr<ID3D11Texture2D> flowFwd[3];
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> flowFwdSRV[3];
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> flowFwdUAV[3];
    Microsoft::WRL::ComPtr<ID3D11Texture2D> flowBwd[3];
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> flowBwdSRV[3];
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> flowBwdUAV[3];
    Microsoft::WRL::ComPtr<ID3D11Texture2D> flowFinal;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> flowFinalSRV;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> flowFinalUAV;
};

class Dx11Overlay {
public:
    Dx11Overlay() = default;
    ~Dx11Overlay();

    Dx11Overlay(const Dx11Overlay&) = delete;
    Dx11Overlay& operator=(const Dx11Overlay&) = delete;

    bool Initialize(HINSTANCE hInst, std::string& outError);
    void Shutdown();

    void UpdateRenderState(
        const FilterParams& params,
        bool splitPreview,
        float splitPos,
        bool forceRedraw,
        const AppConfig& engineCfg
    );
    void SetActive(bool active, HWND sidebarHwnd = nullptr, HWND targetHwnd = nullptr);
    bool IsActive() const { return activeAtomic_.load(std::memory_order_acquire); }
    bool IsRecordableWgc() const { return wgcAtomic_.load(std::memory_order_acquire); }

    // Ensures overlay sits topmost (and below sidebar if sidebarHwnd != nullptr and visible)
    void EnsureZOrder(HWND sidebarHwnd);
    void RequestHdrRefresh();
    bool ConsumeDisplayHdrUpdate(DisplayHdrInfo& outInfo);
    void RequestScreenshot(const std::wstring& targetPath);
    bool ConsumeScreenshotResult(bool& outSuccess, std::wstring& outPath, std::wstring& outError);
    static bool SaveBgraPixelsToPng(
        const uint8_t* bgraData,
        UINT width,
        UINT height,
        UINT rowPitch,
        const std::wstring& filePath,
        std::wstring& outError
    );

    std::wstring GetAdapterName() const;
    DisplayHdrInfo GetDisplayHdrInfo() const;
    FrameGenTelemetry GetTelemetry() const;
    int GetMonitorRefreshHz() const { return pubMonitorHz_.load(std::memory_order_acquire); }
    int ScreenWidth() const { return screenWAtomic_.load(std::memory_order_acquire); }
    int ScreenHeight() const { return screenHAtomic_.load(std::memory_order_acquire); }

    // Runs standalone GPU shader + DComp + WGC + True HDR + Scaling + Optical Flow FG verification
    static bool RunSelfTest(std::string& outReport);

private:
    struct EngineRenderConfig {
        int scaleMode = SCALE_OFF;
        int scaleSharpness = 60;
        bool scaleFullscreenStretch = true;
        int fgMode = FG_OFF;
        int fgFlowScale = 50;
        int fgAdaptiveTargetFps = 120;
        bool fgFastMotionFallback = true;
        bool fgStaticHudProtect = true;
        int captureApi = CAPTURE_DXGI;
        bool allowTearing = true;
    };

    struct RenderStateSnapshot {
        FilterParams params{};
        EngineRenderConfig engineCfg{};
        bool splitPreview = false;
        float splitPos = 0.5f;
        bool wantActive = false;
        HWND wantSidebarHwnd = nullptr;
        HWND wantTargetHwnd = nullptr;
        bool forceRedraw = false;
        bool zOrderDirty = false;
        bool hdrRefreshRequested = false;
        bool screenshotRequested = false;
        std::wstring screenshotPath;
    };

    struct WgcSrvCacheEntry {
        ID3D11Texture2D* tex = nullptr;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    };

    void RenderThreadMain(HINSTANCE hInst);
    void ShutdownInternal();
    bool CreateOverlayWindow(HINSTANCE hInst, bool wantHwndTearing, std::string& outError);
    bool CreateDeviceAndResources(std::string& outError);
    bool RecreateWindowAndSwapChain(bool wantHwndTearing, bool wantHdr, std::string& outError);
    bool CreateOrRecreateSwapChain(bool hdrMode, std::string& outError);
    static DisplayHdrInfo QueryPrimaryDisplayHdrInfo();
    static int QueryMonitorRefreshHz();
    void ApplyActiveStateInternal(bool active, HWND sidebarHwnd, HWND targetHwnd, int desiredCaptureApi);
    void ApplyZOrderInternal(HWND sidebarHwnd);
    bool RefreshDisplayHdrStateInternal();
    bool TickAndRenderInternal(
        const FilterParams& params,
        bool splitPreview,
        float splitPos,
        bool forceRedraw,
        const EngineRenderConfig& engineCfg
    );
    bool EnsureCaptureSource(int desiredApi);
    bool InitDuplication();
    void ReleaseDuplication();
    bool InitWgc(HWND targetHwnd);
    void ReleaseWgc();
    ID3D11ShaderResourceView* GetOrCreateWgcSrv(ID3D11Texture2D* wgcTex);
    bool SetOverlayCaptureVisibility(bool allowCapture, bool forceSync = false);
    bool EnsureCapturedTexture(const D3D11_TEXTURE2D_DESC& srcDesc);
    bool EnsureFrameHistoryAndInterpTextures(UINT srcW, UINT srcH, DXGI_FORMAT fmt);
    bool EnsureScaledTextures(UINT dstW, UINT dstH, DXGI_FORMAT fmt, bool needTemp);
    void ResetFrameHistory();
    double NowSeconds() const;
    void UpdateTelemetryWindow(double nowSec, bool isNewSource, bool fgActive);
    bool CaptureBackbufferScreenshot(
        int vpX,
        int vpY,
        UINT vpW,
        UINT vpH,
        bool fullScreenVp,
        const std::wstring& targetPath,
        std::wstring& outError
    );
    bool ProcessAndPresentFrame(
        const FilterParams& params,
        bool splitPreview,
        float splitPos,
        ID3D11ShaderResourceView* activeInputSRV,
        UINT inputW,
        UINT inputH,
        DXGI_FORMAT inputFmt,
        const EngineRenderConfig& engineCfg
    );
    bool RenderInternal(
        const FilterParams& params,
        bool splitPreview,
        float splitPos,
        ID3D11ShaderResourceView* inputSRV,
        UINT inputW,
        UINT inputH,
        int vpX,
        int vpY,
        UINT vpW,
        UINT vpH,
        bool opaqueLetterbox,
        bool fastPresent
    );

    static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Cross-thread coordination
    std::thread renderThread_;
    HANDLE wakeEvent_ = nullptr;
    HANDLE initDoneEvent_ = nullptr;
    std::atomic<bool> stopThread_{ false };
    bool initOk_ = false;
    std::string initErr_;

    mutable std::mutex stateMutex_;
    RenderStateSnapshot state_{};

    mutable std::mutex pubMutex_;
    DisplayHdrInfo pubDisplayHdr_{};
    FrameGenTelemetry pubTelemetry_{};
    std::wstring pubAdapterName_ = L"Unknown GPU";
    std::atomic<int> pubMonitorHz_{ 60 };
    std::atomic<bool> activeAtomic_{ false };
    std::atomic<bool> wgcAtomic_{ false };
    std::atomic<bool> hdrChangedAtomic_{ false };
    std::atomic<bool> screenshotResultReady_{ false };
    bool pubScreenshotOk_ = false;
    std::wstring pubScreenshotPath_;
    std::wstring pubScreenshotErr_;
    std::atomic<int> screenWAtomic_{ 1920 };
    std::atomic<int> screenHAtomic_{ 1080 };
    bool displayDirty_ = false;
    bool pendingScreenshot_ = false;
    std::wstring pendingScreenshotPath_;

    std::vector<WgcSrvCacheEntry> wgcSrvCache_;

    HINSTANCE hInst_ = nullptr;
    HWND hwndOverlay_ = nullptr;
    HWND targetHwnd_ = nullptr;
    int screenW_ = 1920;
    int screenH_ = 1080;
    bool active_ = false;
    bool useDComp_ = false;
    bool tearingSupported_ = false;
    bool hwndTearingMode_ = false;
    bool swapChainAllowsTearing_ = false;
    int activeCaptureApi_ = CAPTURE_DXGI;
    uint32_t analyzeFrameTick_ = 0u;
    bool visualAttached_ = false;
    bool hasCapturedFrame_ = false;
    bool usingWgc_ = false;
    bool overlayCapturable_ = false;
    bool swapChainIsHdr_ = false;
    DisplayHdrInfo displayHdr_{};
    ULONGLONG lastDuplRetryMs_ = 0;
    ULONGLONG lastWgcRetryMs_ = 0;
    HWND lastSidebarHwnd_ = nullptr;

    std::wstring adapterName_ = L"Unknown GPU";

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGIFactory2> factory2_;
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
    HANDLE frameLatencyWaitable_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv_;

    Microsoft::WRL::ComPtr<ID3D11ComputeShader> csAnalyze_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vsMain_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> psEnhance_;

    ScalingFgShaders sfShaders_{};
    FlowPyramid flowPyr_{};

    Microsoft::WRL::ComPtr<ID3D11Buffer> constantBuffer_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> statsBuffer_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> statsSRV_;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> statsUAV_;

    Microsoft::WRL::ComPtr<ID3D11SamplerState> linearSampler_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterState_;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> capturedTex_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> capturedSRV_;
    D3D11_TEXTURE2D_DESC capturedDesc_ = {};

    Microsoft::WRL::ComPtr<ID3D11Texture2D> prevFrameTex_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> prevFrameSRV_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> interpTex_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> interpSRV_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> interpRTV_;
    D3D11_TEXTURE2D_DESC historyDesc_ = {};

    Microsoft::WRL::ComPtr<ID3D11Texture2D> upscaleTempTex_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> upscaleTempSRV_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> upscaleTempRTV_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> scaledTex_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> scaledSRV_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> scaledRTV_;
    UINT scaledTexW_ = 0;
    UINT scaledTexH_ = 0;
    DXGI_FORMAT scaledTexFmt_ = DXGI_FORMAT_UNKNOWN;

    // Frame pacing & live telemetry
    LARGE_INTEGER qpcFreq_{};
    int monitorHz_ = 60;
    bool hasPrevFrame_ = false;
    bool flowValid_ = false;
    bool fgFlushedCurr_ = true;
    double prevFrameTimeSec_ = 0.0;
    double currFrameTimeSec_ = 0.0;
    double avgSourceIntervalSec_ = 1.0 / 60.0;
    double nextPresentSec_ = 0.0;
    int fgGenCountSinceReal_ = 0;
    double fpsWindowStartSec_ = 0.0;
    int srcFrameCountWin_ = 0;
    int outFrameCountWin_ = 0;
    FrameGenTelemetry telemetry_{};
};
