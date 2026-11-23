#include "dx11_overlay.h"
#include "profiles.h"
#include "scaling_fg_shaders.h"
#include "shader_source.h"

#include <d3dcompiler.h>
#include <dwmapi.h>
#include <mmsystem.h>
#include <roapi.h>
#include <wincodec.h>
#include <windows.foundation.h>
#include <windows.graphics.capture.interop.h>
#include <winstring.h>
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
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

uint8_t LinearToSrgbU8(float c) {
    float clamped = std::isfinite(c) ? std::clamp(c, 0.0f, 1.0f) : 0.0f;
    float srgb = (clamped <= 0.0031308f)
        ? (clamped * 12.92f)
        : (1.055f * std::pow(clamped, 1.0f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(srgb * 255.0f)), 0, 255));
}

bool CompileShaderFromSource(
    const char* source,
    const char* sourceName,
    const char* entry,
    const char* target,
    ComPtr<ID3DBlob>& outBlob,
    std::string& outErr
) {
    ComPtr<ID3DBlob> errBlob;
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    HRESULT hr = D3DCompile(
        source,
        std::strlen(source),
        sourceName,
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

bool CompileShaderEntry(
    const char* entry,
    const char* target,
    ComPtr<ID3DBlob>& outBlob,
    std::string& outErr
) {
    return CompileShaderFromSource(kDynamicVibranceHLSL, "DynamicVibrance.hlsl", entry, target, outBlob, outErr);
}

bool InitScalingFgShaders(ID3D11Device* dev, ScalingFgShaders& s, std::string& outErr) {
    auto compileCS = [&](const char* entry, ComPtr<ID3D11ComputeShader>& dst) -> bool {
        ComPtr<ID3DBlob> b;
        if (!CompileShaderFromSource(kScalingFgHLSL, "ScalingFg.hlsl", entry, "cs_5_0", b, outErr)) return false;
        return SUCCEEDED(dev->CreateComputeShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &dst));
    };
    auto compilePS = [&](const char* entry, ComPtr<ID3D11PixelShader>& dst) -> bool {
        ComPtr<ID3DBlob> b;
        if (!CompileShaderFromSource(kScalingFgHLSL, "ScalingFg.hlsl", entry, "ps_5_0", b, outErr)) return false;
        return SUCCEEDED(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &dst));
    };

    ComPtr<ID3DBlob> vsB;
    if (!CompileShaderFromSource(kScalingFgHLSL, "ScalingFg.hlsl", "VSFullScreen", "vs_5_0", vsB, outErr)) return false;
    if (FAILED(dev->CreateVertexShader(vsB->GetBufferPointer(), vsB->GetBufferSize(), nullptr, &s.vsFullScreen))) {
        outErr = "Failed creating VSFullScreen.";
        return false;
    }

    if (!compileCS("CSLumaDownsample", s.csLumaDown) ||
        !compileCS("CSOpticalFlowCoarse", s.csFlowCoarse) ||
        !compileCS("CSOpticalFlowRefine", s.csFlowRefine) ||
        !compileCS("CSFlowFinalize", s.csFlowFinalize) ||
        !compileCS("CSFlowStats", s.csFlowStats) ||
        !compilePS("PSFrameGen", s.psFrameGen) ||
        !compilePS("PSFsrEasu", s.psFsrEasu) ||
        !compilePS("PSFsrRcas", s.psFsrRcas) ||
        !compilePS("PSSpatialScale", s.psSpatialScale)) {
        if (outErr.empty()) outErr = "Failed creating Scaling/FG D3D11 shader objects.";
        return false;
    }

    D3D11_BUFFER_DESC cbd{};
    cbd.ByteWidth = sizeof(FgCBData);
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(dev->CreateBuffer(&cbd, nullptr, &s.cb))) {
        outErr = "Failed creating FgCBData constant buffer.";
        return false;
    }

    FgStats initSt{ 0.0f, 0.0f, 1.0f, 0.0f };
    D3D11_BUFFER_DESC sbd{};
    sbd.ByteWidth = sizeof(FgStats);
    sbd.Usage = D3D11_USAGE_DEFAULT;
    sbd.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    sbd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    sbd.StructureByteStride = sizeof(FgStats);
    D3D11_SUBRESOURCE_DATA sbInit{ &initSt, 0, 0 };
    if (FAILED(dev->CreateBuffer(&sbd, &sbInit, &s.statsBuf))) {
        outErr = "Failed creating FgStats buffer.";
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
    srvd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvd.Buffer.NumElements = 1;
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavd{};
    uavd.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavd.Buffer.NumElements = 1;
    if (FAILED(dev->CreateShaderResourceView(s.statsBuf.Get(), &srvd, &s.statsSRV)) ||
        FAILED(dev->CreateUnorderedAccessView(s.statsBuf.Get(), &uavd, &s.statsUAV))) {
        outErr = "Failed creating FgStats views.";
        return false;
    }

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    dev->CreateSamplerState(&sd, &s.linearSamp);

    D3D11_RASTERIZER_DESC rsd{};
    rsd.FillMode = D3D11_FILL_SOLID;
    rsd.CullMode = D3D11_CULL_NONE;
    rsd.DepthClipEnable = FALSE;
    dev->CreateRasterizerState(&rsd, &s.rs);
    return true;
}

bool EnsureFlowPyramid(ID3D11Device* dev, FlowPyramid& pyr, UINT srcW, UINT srcH, int flowScalePct) {
    int pct = std::clamp(flowScalePct, 25, 100);
    if (pyr.flowFinalSRV && pyr.srcW == srcW && pyr.srcH == srcH && pyr.scalePct == pct) {
        return true;
    }

    UINT l0w = std::clamp((srcW * static_cast<UINT>(pct)) / 200u, 160u, 960u);
    l0w = (l0w + 3u) & ~3u;
    UINT rawH = static_cast<UINT>(std::lround(static_cast<double>(l0w) * srcH / std::max(srcW, 1u)));
    UINT l0h = std::clamp((rawH + 3u) & ~3u, 90u, 540u);

    pyr.srcW = srcW;
    pyr.srcH = srcH;
    pyr.scalePct = pct;
    pyr.w[0] = l0w;
    pyr.h[0] = l0h;
    pyr.w[1] = std::max(l0w / 2u, 40u);
    pyr.h[1] = std::max(l0h / 2u, 24u);
    pyr.w[2] = std::max(pyr.w[1] / 2u, 20u);
    pyr.h[2] = std::max(pyr.h[1] / 2u, 12u);

    auto makeTex = [&](UINT w, UINT h, DXGI_FORMAT fmt,
                       ComPtr<ID3D11Texture2D>& tex,
                       ComPtr<ID3D11ShaderResourceView>& srv,
                       ComPtr<ID3D11UnorderedAccessView>& uav) -> bool {
        tex.Reset(); srv.Reset(); uav.Reset();
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = fmt;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        if (FAILED(dev->CreateTexture2D(&td, nullptr, &tex))) return false;
        if (FAILED(dev->CreateShaderResourceView(tex.Get(), nullptr, &srv))) return false;
        if (FAILED(dev->CreateUnorderedAccessView(tex.Get(), nullptr, &uav))) return false;
        return true;
    };

    for (int i = 0; i < 3; ++i) {
        if (!makeTex(pyr.w[i], pyr.h[i], DXGI_FORMAT_R16_FLOAT, pyr.lumaPrev[i], pyr.lumaPrevSRV[i], pyr.lumaPrevUAV[i]) ||
            !makeTex(pyr.w[i], pyr.h[i], DXGI_FORMAT_R16_FLOAT, pyr.lumaCurr[i], pyr.lumaCurrSRV[i], pyr.lumaCurrUAV[i]) ||
            !makeTex(pyr.w[i], pyr.h[i], DXGI_FORMAT_R16G16_FLOAT, pyr.flowFwd[i], pyr.flowFwdSRV[i], pyr.flowFwdUAV[i]) ||
            !makeTex(pyr.w[i], pyr.h[i], DXGI_FORMAT_R16G16_FLOAT, pyr.flowBwd[i], pyr.flowBwdSRV[i], pyr.flowBwdUAV[i])) {
            return false;
        }
    }
    return makeTex(pyr.w[0], pyr.h[0], DXGI_FORMAT_R16G16B16A16_FLOAT, pyr.flowFinal, pyr.flowFinalSRV, pyr.flowFinalUAV);
}

void UpdateFgCB(ID3D11DeviceContext* ctx, ID3D11Buffer* cb, const FgCBData& data) {
    D3D11_MAPPED_SUBRESOURCE m{};
    if (SUCCEEDED(ctx->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        std::memcpy(m.pData, &data, sizeof(data));
        ctx->Unmap(cb, 0);
    }
}

void ExecuteOpticalFlow(
    ID3D11DeviceContext* ctx,
    const ScalingFgShaders& s,
    FlowPyramid& pyr,
    ID3D11ShaderResourceView* prevSRV,
    ID3D11ShaderResourceView* currSRV,
    float fallbackStrength,
    float hudProtect
) {
    ID3D11ShaderResourceView* nullSRVs[5] = {};
    ID3D11UnorderedAccessView* nullUAVs[4] = {};
    ctx->PSSetShaderResources(0, 5, nullSRVs);
    ctx->CSSetSamplers(0, 1, s.linearSamp.GetAddressOf());
    ctx->CSSetConstantBuffers(0, 1, s.cb.GetAddressOf());

    auto unbindCS = [&]() {
        ctx->CSSetUnorderedAccessViews(0, 4, nullUAVs, nullptr);
        ctx->CSSetShaderResources(0, 5, nullSRVs);
    };

    // 1. Luma Pyramid L0, L1, L2 for Prev & Curr
    for (int lvl = 0; lvl < 3; ++lvl) {
        FgCBData cb{};
        cb.srcW = (lvl == 0) ? pyr.srcW : pyr.w[lvl - 1];
        cb.srcH = (lvl == 0) ? pyr.srcH : pyr.h[lvl - 1];
        cb.dstW = pyr.w[lvl];
        cb.dstH = pyr.h[lvl];
        cb.level = static_cast<uint32_t>(lvl);
        UpdateFgCB(ctx, s.cb.Get(), cb);

        UINT gx = (pyr.w[lvl] + 15u) / 16u;
        UINT gy = (pyr.h[lvl] + 15u) / 16u;

        // Prev luma
        ID3D11ShaderResourceView* pIn = (lvl == 0) ? prevSRV : pyr.lumaPrevSRV[lvl - 1].Get();
        ctx->CSSetShader(s.csLumaDown.Get(), nullptr, 0);
        ctx->CSSetShaderResources(0, 1, &pIn);
        ctx->CSSetUnorderedAccessViews(0, 1, pyr.lumaPrevUAV[lvl].GetAddressOf(), nullptr);
        ctx->Dispatch(gx, gy, 1);
        unbindCS();

        // Curr luma
        ID3D11ShaderResourceView* cIn = (lvl == 0) ? currSRV : pyr.lumaCurrSRV[lvl - 1].Get();
        ctx->CSSetShader(s.csLumaDown.Get(), nullptr, 0);
        ctx->CSSetShaderResources(0, 1, &cIn);
        ctx->CSSetUnorderedAccessViews(0, 1, pyr.lumaCurrUAV[lvl].GetAddressOf(), nullptr);
        ctx->Dispatch(gx, gy, 1);
        unbindCS();
    }

    auto runDirectionPyramid = [&](bool backward) {
        auto* texPrevArr = backward ? pyr.lumaCurrSRV : pyr.lumaPrevSRV;
        auto* texCurrArr = backward ? pyr.lumaPrevSRV : pyr.lumaCurrSRV;
        auto* flowSRVArr = backward ? pyr.flowBwdSRV : pyr.flowFwdSRV;
        auto* flowUAVArr = backward ? pyr.flowBwdUAV : pyr.flowFwdUAV;

        // Coarsest L2
        {
            FgCBData cb{};
            cb.srcW = pyr.w[2]; cb.srcH = pyr.h[2];
            cb.dstW = pyr.w[2]; cb.dstH = pyr.h[2];
            UpdateFgCB(ctx, s.cb.Get(), cb);
            ID3D11ShaderResourceView* srvs[2] = { texPrevArr[2].Get(), texCurrArr[2].Get() };
            ctx->CSSetShader(s.csFlowCoarse.Get(), nullptr, 0);
            ctx->CSSetShaderResources(0, 2, srvs);
            ctx->CSSetUnorderedAccessViews(1, 1, flowUAVArr[2].GetAddressOf(), nullptr);
            ctx->Dispatch((pyr.w[2] + 15u) / 16u, (pyr.h[2] + 15u) / 16u, 1);
            unbindCS();
        }
        // Refine L1 -> L0
        for (int lvl = 1; lvl >= 0; --lvl) {
            FgCBData cb{};
            cb.srcW = pyr.w[lvl + 1]; cb.srcH = pyr.h[lvl + 1];
            cb.dstW = pyr.w[lvl];     cb.dstH = pyr.h[lvl];
            cb.searchDir = 1.0f;
            cb.level = static_cast<uint32_t>(lvl);
            UpdateFgCB(ctx, s.cb.Get(), cb);
            ID3D11ShaderResourceView* srvs[3] = { texPrevArr[lvl].Get(), texCurrArr[lvl].Get(), flowSRVArr[lvl + 1].Get() };
            ctx->CSSetShader(s.csFlowRefine.Get(), nullptr, 0);
            ctx->CSSetShaderResources(0, 3, srvs);
            ctx->CSSetUnorderedAccessViews(1, 1, flowUAVArr[lvl].GetAddressOf(), nullptr);
            ctx->Dispatch((pyr.w[lvl] + 15u) / 16u, (pyr.h[lvl] + 15u) / 16u, 1);
            unbindCS();
        }
    };

    // 2. Forward (Prev -> Curr) and Backward (Curr -> Prev) Pyramids
    runDirectionPyramid(false);
    runDirectionPyramid(true);

    // 3. Bilateral Smooth + Forward-Backward Consistency + Static HUD Mask
    {
        FgCBData cb{};
        cb.srcW = pyr.w[0]; cb.srcH = pyr.h[0];
        cb.dstW = pyr.w[0]; cb.dstH = pyr.h[0];
        cb.hudProtect = hudProtect;
        UpdateFgCB(ctx, s.cb.Get(), cb);
        ID3D11ShaderResourceView* srvs[4] = {
            pyr.lumaPrevSRV[0].Get(), pyr.lumaCurrSRV[0].Get(),
            pyr.flowFwdSRV[0].Get(), pyr.flowBwdSRV[0].Get()
        };
        ctx->CSSetShader(s.csFlowFinalize.Get(), nullptr, 0);
        ctx->CSSetShaderResources(0, 4, srvs);
        ctx->CSSetUnorderedAccessViews(2, 1, pyr.flowFinalUAV.GetAddressOf(), nullptr);
        ctx->Dispatch((pyr.w[0] + 15u) / 16u, (pyr.h[0] + 15u) / 16u, 1);
        unbindCS();
    }

    // 4. AFMF Scene-Cut / Fast-Motion Fallback Statistics
    {
        FgCBData cb{};
        cb.srcW = pyr.w[0]; cb.srcH = pyr.h[0];
        cb.dstW = pyr.w[0]; cb.dstH = pyr.h[0];
        cb.fallbackStrength = fallbackStrength;
        UpdateFgCB(ctx, s.cb.Get(), cb);
        ID3D11ShaderResourceView* srvs[3] = {
            pyr.lumaPrevSRV[0].Get(), pyr.lumaCurrSRV[0].Get(), pyr.flowFinalSRV.Get()
        };
        ctx->CSSetShader(s.csFlowStats.Get(), nullptr, 0);
        ctx->CSSetShaderResources(0, 3, srvs);
        ctx->CSSetUnorderedAccessViews(3, 1, s.statsUAV.GetAddressOf(), nullptr);
        ctx->Dispatch(1, 1, 1);
        unbindCS();
    }
}

void DrawFrameGenPass(
    ID3D11DeviceContext* ctx,
    const ScalingFgShaders& s,
    const FlowPyramid& pyr,
    ID3D11ShaderResourceView* prevFrameSRV,
    ID3D11ShaderResourceView* currFrameSRV,
    ID3D11RenderTargetView* outRTV,
    UINT outW,
    UINT outH,
    float interpT,
    float hudProtect
) {
    FgCBData cb{};
    cb.srcW = outW; cb.srcH = outH;
    cb.dstW = outW; cb.dstH = outH;
    cb.interpT = interpT;
    cb.hudProtect = hudProtect;
    UpdateFgCB(ctx, s.cb.Get(), cb);

    D3D11_VIEWPORT vp{ 0.0f, 0.0f, static_cast<float>(outW), static_cast<float>(outH), 0.0f, 1.0f };
    ID3D11ShaderResourceView* srvs[5] = {
        prevFrameSRV, currFrameSRV, pyr.flowFinalSRV.Get(), pyr.flowBwdSRV[0].Get(), s.statsSRV.Get()
    };
    ctx->OMSetRenderTargets(1, &outRTV, nullptr);
    ctx->RSSetViewports(1, &vp);
    ctx->RSSetState(s.rs.Get());
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(s.vsFullScreen.Get(), nullptr, 0);
    ctx->PSSetShader(s.psFrameGen.Get(), nullptr, 0);
    ctx->PSSetConstantBuffers(0, 1, s.cb.GetAddressOf());
    ctx->PSSetShaderResources(0, 5, srvs);
    ctx->PSSetSamplers(0, 1, s.linearSamp.GetAddressOf());
    ctx->Draw(3, 0);

    ID3D11ShaderResourceView* nullSRVs[5] = {};
    ctx->PSSetShaderResources(0, 5, nullSRVs);
}

void DrawSpatialScalePass(
    ID3D11DeviceContext* ctx,
    const ScalingFgShaders& s,
    ID3D11ShaderResourceView* inputSRV,
    UINT srcW,
    UINT srcH,
    ID3D11RenderTargetView* tempRTV,
    ID3D11ShaderResourceView* tempSRV,
    ID3D11RenderTargetView* outRTV,
    UINT dstW,
    UINT dstH,
    int scaleMode,
    int sharpPct
) {
    D3D11_VIEWPORT vp{ 0.0f, 0.0f, static_cast<float>(dstW), static_cast<float>(dstH), 0.0f, 1.0f };
    ctx->RSSetViewports(1, &vp);
    ctx->RSSetState(s.rs.Get());
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(s.vsFullScreen.Get(), nullptr, 0);
    ctx->PSSetConstantBuffers(0, 1, s.cb.GetAddressOf());
    ctx->PSSetSamplers(0, 1, s.linearSamp.GetAddressOf());

    float sharpNorm = static_cast<float>(std::clamp(sharpPct, 0, 100)) / 100.0f;
    ID3D11ShaderResourceView* nullSRVs[1] = { nullptr };

    if (scaleMode == SCALE_FSR1 && tempRTV && tempSRV) {
        // Pass 1: EASU
        FgCBData cb1{};
        cb1.srcW = srcW; cb1.srcH = srcH;
        cb1.dstW = dstW; cb1.dstH = dstH;
        cb1.scaleMode = static_cast<uint32_t>(scaleMode);
        cb1.scaleSharpness = sharpNorm;
        UpdateFgCB(ctx, s.cb.Get(), cb1);

        ctx->OMSetRenderTargets(1, &tempRTV, nullptr);
        ctx->PSSetShader(s.psFsrEasu.Get(), nullptr, 0);
        ctx->PSSetShaderResources(0, 1, &inputSRV);
        ctx->Draw(3, 0);
        ctx->PSSetShaderResources(0, 1, nullSRVs);

        // Pass 2: RCAS
        FgCBData cb2{};
        cb2.srcW = dstW; cb2.srcH = dstH;
        cb2.dstW = dstW; cb2.dstH = dstH;
        cb2.scaleMode = static_cast<uint32_t>(scaleMode);
        cb2.scaleSharpness = sharpNorm;
        UpdateFgCB(ctx, s.cb.Get(), cb2);

        ctx->OMSetRenderTargets(1, &outRTV, nullptr);
        ctx->PSSetShader(s.psFsrRcas.Get(), nullptr, 0);
        ctx->PSSetShaderResources(0, 1, &tempSRV);
        ctx->Draw(3, 0);
        ctx->PSSetShaderResources(0, 1, nullSRVs);
    } else {
        FgCBData cb{};
        cb.srcW = srcW; cb.srcH = srcH;
        cb.dstW = dstW; cb.dstH = dstH;
        cb.scaleMode = static_cast<uint32_t>(scaleMode);
        cb.scaleSharpness = sharpNorm;
        UpdateFgCB(ctx, s.cb.Get(), cb);

        ctx->OMSetRenderTargets(1, &outRTV, nullptr);
        ctx->PSSetShader(s.psSpatialScale.Get(), nullptr, 0);
        ctx->PSSetShaderResources(0, 1, &inputSRV);
        ctx->Draw(3, 0);
        ctx->PSSetShaderResources(0, 1, nullSRVs);
    }
}

EnhanceCBData BuildCB(
    const FilterParams& p,
    bool splitPreview,
    float splitPos,
    int width,
    int height,
    float smoothAlpha,
    float sdrWhiteNits,
    bool hdrOutputMode
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
    if (p.enableAmbientOcclusion) flags2 |= FLAG2_ENABLE_AO;
    if (p.enableBloom) flags2 |= FLAG2_ENABLE_BLOOM;
    if (p.enableFilmic) flags2 |= FLAG2_ENABLE_FILMIC;
    if (p.enableDeband) flags2 |= FLAG2_ENABLE_DEBAND;
    cb.flags2 = flags2;
    cb.hdrPeakNits = static_cast<float>(std::clamp(p.hdrPeakNits, 300, 1500));

    cb.hdrMidGray = static_cast<float>(std::clamp(p.hdrMidGray, 0, 100)) / 100.0f;
    cb.sdrWhiteNits = std::clamp(sdrWhiteNits, 80.0f, 480.0f);
    cb.hdrOutputMode = hdrOutputMode ? 1u : 0u;
    cb.aoIntensity = static_cast<float>(std::clamp(p.ambientOcclusion, 0, 100)) / 100.0f;
    cb.aoRadius = static_cast<float>(std::clamp(p.aoRadius, 0, 100)) / 100.0f;
    cb.bloomIntensity = static_cast<float>(std::clamp(p.bloomIntensity, 0, 100)) / 100.0f;
    cb.bloomThreshold = static_cast<float>(std::clamp(p.bloomThreshold, 0, 100)) / 100.0f;
    cb.filmicStrength = static_cast<float>(std::clamp(p.filmicStrength, 0, 100)) / 100.0f;
    cb.filmicMode = static_cast<uint32_t>(std::clamp(p.filmicMode, 0, 2));
    cb.debandStrength = static_cast<float>(std::clamp(p.debandStrength, 0, 100)) / 100.0f;
    cb.pad1 = 0.0f;
    cb.pad2 = 0.0f;
    return cb;
}

float HalfToFloat(uint16_t h) {
    uint32_t sign = static_cast<uint32_t>(h & 0x8000u) << 16;
    uint32_t exp = (h >> 10) & 0x1Fu;
    uint32_t mant = h & 0x03FFu;
    uint32_t fBits = 0;
    if (exp == 0u) {
        if (mant == 0u) {
            fBits = sign;
        } else {
            float val = std::ldexp(static_cast<float>(mant), -24);
            return (sign != 0u) ? -val : val;
        }
    } else if (exp == 31u) {
        fBits = sign | 0x7F800000u | (mant << 13);
    } else {
        fBits = sign | ((exp + 112u) << 23) | (mant << 13);
    }
    float out = 0.0f;
    std::memcpy(&out, &fBits, sizeof(out));
    return out;
}

} // namespace

Dx11Overlay::~Dx11Overlay() {
    Shutdown();
}

LRESULT CALLBACK Dx11Overlay::OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Dx11Overlay* self = reinterpret_cast<Dx11Overlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<Dx11Overlay*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (msg == WM_DISPLAYCHANGE) {
        if (self) self->displayDirty_ = true;
        return 0;
    }
    if (msg == WM_NCHITTEST) {
        return HTTRANSPARENT;
    }
    if (msg == WM_ERASEBKGND) {
        return 1;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool Dx11Overlay::CreateOverlayWindow(HINSTANCE hInst, bool wantHwndTearing, std::string& outError) {
    if (hwndOverlay_) {
        DestroyWindow(hwndOverlay_);
        hwndOverlay_ = nullptr;
        overlayCapturable_ = false;
    }

    screenW_ = std::max(GetSystemMetrics(SM_CXSCREEN), 640);
    screenH_ = std::max(GetSystemMetrics(SM_CYSCREEN), 480);
    screenWAtomic_.store(screenW_, std::memory_order_release);
    screenHAtomic_.store(screenH_, std::memory_order_release);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"AIDynamicVibranceOverlayWnd";
    RegisterClassExW(&wc);

    hwndTearingMode_ = wantHwndTearing;
    useDComp_ = !wantHwndTearing;

    DWORD exStyle = WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT |
                    WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    if (!wantHwndTearing) {
        exStyle |= WS_EX_NOREDIRECTIONBITMAP;
    }
    hwndOverlay_ = CreateWindowExW(
        exStyle,
        wc.lpszClassName,
        L"AI Dynamic Vibrance Overlay",
        WS_POPUP,
        0, 0, screenW_, screenH_,
        nullptr, nullptr, hInst, this
    );
    if (!hwndOverlay_ && !wantHwndTearing) {
        useDComp_ = false;
        exStyle = WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
        hwndOverlay_ = CreateWindowExW(
            exStyle,
            wc.lpszClassName,
            L"AI Dynamic Vibrance Overlay",
            WS_POPUP,
            0, 0, screenW_, screenH_,
            nullptr, nullptr, hInst, this
        );
    }
    if (!hwndOverlay_) {
        outError = "CreateWindowExW failed for overlay (" + std::to_string(GetLastError()) + ")";
        return false;
    }
    if (!useDComp_) {
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
    dxgiDevice->SetGPUThreadPriority(7);
    {
        ComPtr<IDXGIDevice1> dxgiDev1;
        if (SUCCEEDED(dxgiDevice.As(&dxgiDev1)) && dxgiDev1) {
            dxgiDev1->SetMaximumFrameLatency(1);
        }
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

    if (adapter) {
        adapter->GetParent(IID_PPV_ARGS(&factory2_));
    }
    if (!factory2_) {
        outError = "Failed to acquire IDXGIFactory2";
        return false;
    }

    tearingSupported_ = false;
    {
        ComPtr<IDXGIFactory5> factory5;
        if (SUCCEEDED(factory2_.As(&factory5)) && factory5) {
            BOOL allowTear = FALSE;
            if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTear, sizeof(allowTear)))) {
                tearingSupported_ = (allowTear != FALSE);
            }
        }
    }
    if (hwndTearingMode_ && !tearingSupported_) {
        hwndTearingMode_ = false;
        useDComp_ = true;
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

    if (useDComp_) {
        hr = DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(&dcompDevice_));
        if (SUCCEEDED(hr) && dcompDevice_) {
            hr = dcompDevice_->CreateTargetForHwnd(hwndOverlay_, TRUE, &dcompTarget_);
            if (SUCCEEDED(hr)) hr = dcompDevice_->CreateVisual(&dcompVisual_);
            if (SUCCEEDED(hr)) hr = dcompTarget_->SetRoot(nullptr);
            if (SUCCEEDED(hr)) hr = dcompDevice_->Commit();
        }
        if (FAILED(hr) || !dcompTarget_ || !dcompVisual_) {
            dcompVisual_.Reset();
            dcompTarget_.Reset();
            dcompDevice_.Reset();
            useDComp_ = false;
        }
    }

    displayHdr_ = QueryPrimaryDisplayHdrInfo();
    QueryPerformanceFrequency(&qpcFreq_);
    monitorHz_ = QueryMonitorRefreshHz();
    telemetry_.monitorHz = monitorHz_;
    {
        std::lock_guard<std::mutex> lk(pubMutex_);
        pubAdapterName_ = adapterName_;
        pubDisplayHdr_ = displayHdr_;
        pubTelemetry_ = telemetry_;
    }
    pubMonitorHz_.store(monitorHz_, std::memory_order_release);

    if (!InitScalingFgShaders(device_.Get(), sfShaders_, outError)) {
        return false;
    }

    return CreateOrRecreateSwapChain(displayHdr_.hdrActive, outError);
}

int Dx11Overlay::QueryMonitorRefreshHz() {
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm) &&
        dm.dmDisplayFrequency >= 30 && dm.dmDisplayFrequency <= 360) {
        return static_cast<int>(dm.dmDisplayFrequency);
    }
    return 60;
}

DisplayHdrInfo Dx11Overlay::QueryPrimaryDisplayHdrInfo() {
    DisplayHdrInfo info{};
    ComPtr<IDXGIFactory1> freshFactory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&freshFactory))) || !freshFactory) {
        return info;
    }

    ComPtr<IDXGIAdapter1> adapter1;
    if (FAILED(freshFactory->EnumAdapters1(0, &adapter1)) || !adapter1) {
        return info;
    }

    ComPtr<IDXGIOutput> output;
    if (FAILED(adapter1->EnumOutputs(0, &output)) || !output) {
        return info;
    }

    ComPtr<IDXGIOutput6> output6;
    DXGI_OUTPUT_DESC1 desc1{};
    if (FAILED(output.As(&output6)) || !output6 || FAILED(output6->GetDesc1(&desc1))) {
        return info;
    }

    info.hdrActive = (desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
    if (info.hdrActive) {
        info.maxLuminanceNits = (desc1.MaxLuminance >= 250.0f && desc1.MaxLuminance <= 4000.0f)
            ? desc1.MaxLuminance
            : 800.0f;
        info.minLuminanceNits = std::clamp(desc1.MinLuminance, 0.0f, 0.5f);
    }

    UINT32 pathCount = 0, modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) == ERROR_SUCCESS && pathCount > 0) {
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
        if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) == ERROR_SUCCESS) {
            for (const auto& path : paths) {
                DISPLAYCONFIG_SOURCE_DEVICE_NAME srcName{};
                srcName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
                srcName.header.size = sizeof(srcName);
                srcName.header.adapterId = path.sourceInfo.adapterId;
                srcName.header.id = path.sourceInfo.id;
                if (DisplayConfigGetDeviceInfo(&srcName.header) == ERROR_SUCCESS &&
                    _wcsicmp(srcName.viewGdiDeviceName, desc1.DeviceName) == 0) {
                    DISPLAYCONFIG_SDR_WHITE_LEVEL sdrWhite{};
                    sdrWhite.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
                    sdrWhite.header.size = sizeof(sdrWhite);
                    sdrWhite.header.adapterId = path.targetInfo.adapterId;
                    sdrWhite.header.id = path.targetInfo.id;
                    if (DisplayConfigGetDeviceInfo(&sdrWhite.header) == ERROR_SUCCESS && sdrWhite.SDRWhiteLevel > 0) {
                        info.sdrWhiteNits = std::clamp((static_cast<float>(sdrWhite.SDRWhiteLevel) / 1000.0f) * 80.0f, 80.0f, 480.0f);
                    }
                    break;
                }
            }
        }
    }
    return info;
}

bool Dx11Overlay::RecreateWindowAndSwapChain(bool wantHwndTearing, bool wantHdr, std::string& outError) {
    if (!device_ || !factory2_ || !hInst_) {
        outError = "RecreateWindowAndSwapChain prerequisites not ready.";
        return false;
    }

    rtv_.Reset();
    if (dcompTarget_ && dcompDevice_) {
        dcompTarget_->SetRoot(nullptr);
    }
    if (dcompVisual_) {
        dcompVisual_->SetContent(nullptr);
    }
    if (dcompDevice_) {
        dcompDevice_->Commit();
    }
    dcompVisual_.Reset();
    dcompTarget_.Reset();
    dcompDevice_.Reset();
    visualAttached_ = false;
    if (frameLatencyWaitable_) {
        CloseHandle(frameLatencyWaitable_);
        frameLatencyWaitable_ = nullptr;
    }
    swapChain_.Reset();
    swapChainAllowsTearing_ = false;

    bool effectiveTearing = wantHwndTearing && tearingSupported_;
    if (!CreateOverlayWindow(hInst_, effectiveTearing, outError)) {
        return false;
    }
    SetOverlayCaptureVisibility(usingWgc_);

    if (useDComp_) {
        ComPtr<IDXGIDevice> dxgiDevice;
        HRESULT hr = device_.As(&dxgiDevice);
        if (SUCCEEDED(hr) && dxgiDevice) {
            hr = DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(&dcompDevice_));
            if (SUCCEEDED(hr) && dcompDevice_) {
                hr = dcompDevice_->CreateTargetForHwnd(hwndOverlay_, TRUE, &dcompTarget_);
                if (SUCCEEDED(hr)) hr = dcompDevice_->CreateVisual(&dcompVisual_);
                if (SUCCEEDED(hr)) hr = dcompTarget_->SetRoot(nullptr);
                if (SUCCEEDED(hr)) hr = dcompDevice_->Commit();
            }
        }
        if (FAILED(hr) || !dcompTarget_ || !dcompVisual_) {
            dcompVisual_.Reset();
            dcompTarget_.Reset();
            dcompDevice_.Reset();
            useDComp_ = false;
            hwndTearingMode_ = tearingSupported_;
        }
    }

    return CreateOrRecreateSwapChain(wantHdr, outError);
}

bool Dx11Overlay::CreateOrRecreateSwapChain(bool hdrMode, std::string& outError) {
    if (!device_ || !factory2_ || !hwndOverlay_) {
        outError = "SwapChain prerequisites not ready.";
        return false;
    }

    rtv_.Reset();
    if (useDComp_ && dcompTarget_ && dcompVisual_ && dcompDevice_) {
        dcompTarget_->SetRoot(nullptr);
        dcompVisual_->SetContent(nullptr);
        dcompDevice_->Commit();
        visualAttached_ = false;
    }
    if (frameLatencyWaitable_) {
        CloseHandle(frameLatencyWaitable_);
        frameLatencyWaitable_ = nullptr;
    }
    swapChain_.Reset();
    swapChainAllowsTearing_ = false;

    auto tryCreate = [&](bool wantHdr, bool wantWaitable) -> bool {
        DXGI_SWAP_CHAIN_DESC1 scDesc{};
        scDesc.Width = static_cast<UINT>(screenW_);
        scDesc.Height = static_cast<UINT>(screenH_);
        scDesc.Format = wantHdr ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_B8G8R8A8_UNORM;
        scDesc.SampleDesc.Count = 1;
        scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scDesc.BufferCount = 3;
        scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        bool wantTear = !useDComp_ && hwndTearingMode_ && tearingSupported_;
        scDesc.Scaling = DXGI_SCALING_STRETCH;
        scDesc.AlphaMode = useDComp_ ? DXGI_ALPHA_MODE_PREMULTIPLIED : DXGI_ALPHA_MODE_IGNORE;
        scDesc.Flags = (wantTear ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u) |
                       (wantWaitable ? DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT : 0u);

        ComPtr<IDXGISwapChain1> sc;
        HRESULT hr = useDComp_
            ? factory2_->CreateSwapChainForComposition(device_.Get(), &scDesc, nullptr, &sc)
            : factory2_->CreateSwapChainForHwnd(device_.Get(), hwndOverlay_, &scDesc, nullptr, nullptr, &sc);
        if (FAILED(hr) || !sc) return false;
        if (!useDComp_) {
            factory2_->MakeWindowAssociation(hwndOverlay_, DXGI_MWA_NO_ALT_ENTER);
        }

        if (wantHdr) {
            ComPtr<IDXGISwapChain3> sc3;
            UINT sup = 0;
            if (FAILED(sc.As(&sc3)) || !sc3 ||
                FAILED(sc3->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, &sup)) ||
                (sup & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) == 0 ||
                FAILED(sc3->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709))) {
                return false;
            }
        }

        if (wantWaitable) {
            ComPtr<IDXGISwapChain2> sc2;
            if (SUCCEEDED(sc.As(&sc2)) && sc2) {
                sc2->SetMaximumFrameLatency(1);
                frameLatencyWaitable_ = sc2->GetFrameLatencyWaitableObject();
            }
        }

        swapChain_ = std::move(sc);
        swapChainAllowsTearing_ = wantTear;
        return true;
    };

    auto tryCreateBest = [&](bool wantHdr) -> bool {
        return tryCreate(wantHdr, true) || tryCreate(wantHdr, false);
    };

    bool actualHdr = hdrMode;
    bool created = tryCreateBest(actualHdr);
    if (!created && actualHdr) {
        actualHdr = false;
        created = tryCreateBest(false);
    }
    if (!created && !useDComp_ && hwndTearingMode_) {
        hwndTearingMode_ = false;
        created = tryCreateBest(actualHdr);
        if (!created && actualHdr) {
            actualHdr = false;
            created = tryCreateBest(false);
        }
    }
    if (!created && useDComp_) {
        dcompVisual_.Reset();
        dcompTarget_.Reset();
        dcompDevice_.Reset();
        useDComp_ = false;
        created = tryCreateBest(actualHdr);
        if (!created && actualHdr) {
            actualHdr = false;
            created = tryCreateBest(false);
        }
    }
    if (!created) {
        outError = useDComp_ ? "CreateSwapChainForComposition failed." : "CreateSwapChainForHwnd failed.";
        swapChainAllowsTearing_ = false;
        return false;
    }

    if (useDComp_ && dcompVisual_ && dcompTarget_ && dcompDevice_) {
        dcompVisual_->SetContent(swapChain_.Get());
        dcompTarget_->SetRoot(nullptr);
        dcompDevice_->Commit();
        visualAttached_ = false;
    }

    ComPtr<ID3D11Texture2D> backBuf;
    HRESULT hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuf));
    if (FAILED(hr)) { outError = "GetBuffer(0) failed: " + HrToString(hr); return false; }

    hr = device_->CreateRenderTargetView(backBuf.Get(), nullptr, &rtv_);
    if (FAILED(hr)) { outError = "CreateRenderTargetView failed: " + HrToString(hr); return false; }

    swapChainIsHdr_ = actualHdr;
    return true;
}

bool Dx11Overlay::RefreshDisplayHdrStateInternal() {
    int nextW = std::max(GetSystemMetrics(SM_CXSCREEN), 640);
    int nextH = std::max(GetSystemMetrics(SM_CYSCREEN), 480);
    bool dimChanged = (nextW != screenW_ || nextH != screenH_);
    if (dimChanged) {
        screenW_ = nextW;
        screenH_ = nextH;
        screenWAtomic_.store(nextW, std::memory_order_release);
        screenHAtomic_.store(nextH, std::memory_order_release);
        if (hwndOverlay_) {
            SetWindowPos(hwndOverlay_, nullptr, 0, 0, screenW_, screenH_, SWP_NOACTIVATE | SWP_NOZORDER);
        }
    }

    DisplayHdrInfo next = QueryPrimaryDisplayHdrInfo();
    int nextHz = QueryMonitorRefreshHz();
    monitorHz_ = nextHz;
    pubMonitorHz_.store(nextHz, std::memory_order_release);
    bool changed = dimChanged ||
                   (next.hdrActive != displayHdr_.hdrActive) ||
                   (std::abs(next.maxLuminanceNits - displayHdr_.maxLuminanceNits) > 5.0f) ||
                   (std::abs(next.sdrWhiteNits - displayHdr_.sdrWhiteNits) > 2.0f);
    if ((dimChanged || next.hdrActive != swapChainIsHdr_) && device_ && factory2_) {
        std::string dummyErr;
        CreateOrRecreateSwapChain(next.hdrActive, dummyErr);
        changed = true;
    }
    if (changed) {
        displayHdr_ = next;
        {
            std::lock_guard<std::mutex> lk(pubMutex_);
            pubDisplayHdr_ = next;
            telemetry_.monitorHz = nextHz;
            pubTelemetry_.monitorHz = nextHz;
        }
        hdrChangedAtomic_.store(true, std::memory_order_release);
    } else {
        std::lock_guard<std::mutex> lk(pubMutex_);
        telemetry_.monitorHz = nextHz;
        pubTelemetry_.monitorHz = nextHz;
    }
    return changed;
}

void Dx11Overlay::UpdateRenderState(
    const FilterParams& params,
    bool splitPreview,
    float splitPos,
    bool forceRedraw,
    const AppConfig& engineCfg
) {
    {
        std::lock_guard<std::mutex> lk(stateMutex_);
        state_.params = params;
        state_.splitPreview = splitPreview;
        state_.splitPos = splitPos;
        state_.engineCfg.scaleMode = engineCfg.scaleMode;
        state_.engineCfg.scaleSharpness = engineCfg.scaleSharpness;
        state_.engineCfg.scaleFullscreenStretch = engineCfg.scaleFullscreenStretch;
        state_.engineCfg.fgMode = engineCfg.fgMode;
        state_.engineCfg.fgFlowScale = engineCfg.fgFlowScale;
        state_.engineCfg.fgAdaptiveTargetFps = engineCfg.fgAdaptiveTargetFps;
        state_.engineCfg.fgFastMotionFallback = engineCfg.fgFastMotionFallback;
        state_.engineCfg.fgStaticHudProtect = engineCfg.fgStaticHudProtect;
        state_.engineCfg.captureApi = engineCfg.captureApi;
        state_.engineCfg.allowTearing = engineCfg.allowTearing;
        state_.forceRedraw = state_.forceRedraw || forceRedraw;
    }
    if (wakeEvent_) SetEvent(wakeEvent_);
}

void Dx11Overlay::SetActive(bool active, HWND sidebarHwnd, HWND targetHwnd) {
    {
        std::lock_guard<std::mutex> lk(stateMutex_);
        state_.wantActive = active;
        state_.wantSidebarHwnd = sidebarHwnd;
        state_.wantTargetHwnd = targetHwnd;
        if (active) state_.forceRedraw = true;
    }
    if (wakeEvent_) SetEvent(wakeEvent_);
}

void Dx11Overlay::EnsureZOrder(HWND sidebarHwnd) {
    {
        std::lock_guard<std::mutex> lk(stateMutex_);
        state_.wantSidebarHwnd = sidebarHwnd;
        state_.zOrderDirty = true;
    }
    if (wakeEvent_) SetEvent(wakeEvent_);
}

void Dx11Overlay::RequestHdrRefresh() {
    {
        std::lock_guard<std::mutex> lk(stateMutex_);
        state_.hdrRefreshRequested = true;
    }
    if (wakeEvent_) SetEvent(wakeEvent_);
}

void Dx11Overlay::RequestScreenshot(const std::wstring& targetPath) {
    if (targetPath.empty()) return;
    {
        std::lock_guard<std::mutex> lk(stateMutex_);
        state_.screenshotRequested = true;
        state_.screenshotPath = targetPath;
        state_.forceRedraw = true;
    }
    if (wakeEvent_) SetEvent(wakeEvent_);
}

bool Dx11Overlay::ConsumeScreenshotResult(bool& outSuccess, std::wstring& outPath, std::wstring& outError) {
    if (!screenshotResultReady_.exchange(false, std::memory_order_acq_rel)) {
        return false;
    }
    std::lock_guard<std::mutex> lk(pubMutex_);
    outSuccess = pubScreenshotOk_;
    outPath = pubScreenshotPath_;
    outError = pubScreenshotErr_;
    return true;
}

bool Dx11Overlay::SaveBgraPixelsToPng(
    const uint8_t* bgraData,
    UINT width,
    UINT height,
    UINT rowPitch,
    const std::wstring& filePath,
    std::wstring& outError
) {
    if (!bgraData || width == 0 || height == 0 || rowPitch < width * 4u || filePath.empty()) {
        outError = L"Invalid screenshot pixel buffer or output path.";
        return false;
    }

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path target(filePath);
    if (!target.parent_path().empty()) {
        fs::create_directories(target.parent_path(), ec);
        if (ec) {
            outError = L"Failed creating Screenshots directory.";
            return false;
        }
    }

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)
    );
    if (FAILED(hr) || !factory) {
        outError = L"Failed creating WIC imaging factory (" + Utf8ToWide(HrToString(hr)) + L").";
        return false;
    }

    ComPtr<IWICStream> stream;
    hr = factory->CreateStream(&stream);
    if (FAILED(hr) || !stream || FAILED(hr = stream->InitializeFromFilename(filePath.c_str(), GENERIC_WRITE))) {
        outError = L"Failed opening PNG output file (" + Utf8ToWide(HrToString(hr)) + L").";
        return false;
    }

    ComPtr<IWICBitmapEncoder> encoder;
    hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (FAILED(hr) || !encoder || FAILED(hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) {
        outError = L"Failed initializing PNG encoder (" + Utf8ToWide(HrToString(hr)) + L").";
        return false;
    }

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props;
    hr = encoder->CreateNewFrame(&frame, &props);
    if (FAILED(hr) || !frame || FAILED(hr = frame->Initialize(props.Get()))) {
        outError = L"Failed creating PNG frame (" + Utf8ToWide(HrToString(hr)) + L").";
        return false;
    }

    hr = frame->SetSize(width, height);
    if (FAILED(hr)) {
        outError = L"Failed setting PNG size (" + Utf8ToWide(HrToString(hr)) + L").";
        return false;
    }

    WICPixelFormatGUID pixFmt = GUID_WICPixelFormat32bppBGRA;
    hr = frame->SetPixelFormat(&pixFmt);
    if (FAILED(hr) || pixFmt != GUID_WICPixelFormat32bppBGRA) {
        outError = L"PNG encoder rejected 32bpp BGRA pixel format.";
        return false;
    }

    const UINT rowBytes = width * 4u;
    std::vector<uint8_t> rowBuf(rowBytes);
    for (UINT y = 0; y < height; ++y) {
        const uint8_t* srcRow = bgraData + static_cast<size_t>(y) * rowPitch;
        std::memcpy(rowBuf.data(), srcRow, rowBytes);
        for (UINT x = 0; x < width; ++x) {
            rowBuf[static_cast<size_t>(x) * 4u + 3u] = 0xFFu;
        }
        hr = frame->WritePixels(1, rowBytes, rowBytes, rowBuf.data());
        if (FAILED(hr)) {
            outError = L"Failed writing PNG pixels (" + Utf8ToWide(HrToString(hr)) + L").";
            return false;
        }
    }

    if (FAILED(hr = frame->Commit()) || FAILED(hr = encoder->Commit())) {
        outError = L"Failed committing PNG stream (" + Utf8ToWide(HrToString(hr)) + L").";
        return false;
    }
    return true;
}

bool Dx11Overlay::ConsumeDisplayHdrUpdate(DisplayHdrInfo& outInfo) {
    if (!hdrChangedAtomic_.exchange(false, std::memory_order_acq_rel)) {
        return false;
    }
    std::lock_guard<std::mutex> lk(pubMutex_);
    outInfo = pubDisplayHdr_;
    return true;
}

std::wstring Dx11Overlay::GetAdapterName() const {
    std::lock_guard<std::mutex> lk(pubMutex_);
    return pubAdapterName_;
}

DisplayHdrInfo Dx11Overlay::GetDisplayHdrInfo() const {
    std::lock_guard<std::mutex> lk(pubMutex_);
    return pubDisplayHdr_;
}

FrameGenTelemetry Dx11Overlay::GetTelemetry() const {
    FrameGenTelemetry copy{};
    {
        std::lock_guard<std::mutex> lk(pubMutex_);
        copy = pubTelemetry_;
    }
    copy.overlayActive = activeAtomic_.load(std::memory_order_acquire);
    copy.usingWgc = wgcAtomic_.load(std::memory_order_acquire);
    return copy;
}

bool Dx11Overlay::Initialize(HINSTANCE hInst, std::string& outError) {
    Shutdown();
    wakeEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    initDoneEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!wakeEvent_ || !initDoneEvent_) {
        outError = "Failed creating overlay synchronization events.";
        Shutdown();
        return false;
    }
    stopThread_.store(false, std::memory_order_release);
    initOk_ = false;
    initErr_.clear();
    renderThread_ = std::thread(&Dx11Overlay::RenderThreadMain, this, hInst);

    WaitForSingleObject(initDoneEvent_, INFINITE);
    if (!initOk_) {
        outError = initErr_.empty() ? "Render thread initialization failed." : initErr_;
        Shutdown();
        return false;
    }
    return true;
}

void Dx11Overlay::Shutdown() {
    stopThread_.store(true, std::memory_order_release);
    if (wakeEvent_) {
        SetEvent(wakeEvent_);
    }
    if (renderThread_.joinable()) {
        renderThread_.join();
    }
    if (wakeEvent_) {
        CloseHandle(wakeEvent_);
        wakeEvent_ = nullptr;
    }
    if (initDoneEvent_) {
        CloseHandle(initDoneEvent_);
        initDoneEvent_ = nullptr;
    }
    activeAtomic_.store(false, std::memory_order_release);
    wgcAtomic_.store(false, std::memory_order_release);
    hdrChangedAtomic_.store(false, std::memory_order_release);
}

void Dx11Overlay::RenderThreadMain(HINSTANCE hInst) {
    hInst_ = hInst;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    timeBeginPeriod(1);
    HRESULT roHr = RoInitialize(RO_INIT_MULTITHREADED);

    if (!CreateOverlayWindow(hInst_, true, initErr_) || !CreateDeviceAndResources(initErr_)) {
        initOk_ = false;
        ShutdownInternal();
        SetEvent(initDoneEvent_);
        if (SUCCEEDED(roHr)) RoUninitialize();
        timeEndPeriod(1);
        return;
    }

    initOk_ = true;
    SetEvent(initDoneEvent_);

    while (!stopThread_.load(std::memory_order_acquire)) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        RenderStateSnapshot snap{};
        {
            std::lock_guard<std::mutex> lk(stateMutex_);
            snap = state_;
            state_.forceRedraw = false;
            state_.zOrderDirty = false;
            state_.hdrRefreshRequested = false;
            state_.screenshotRequested = false;
            state_.screenshotPath.clear();
        }

        if (displayDirty_ || snap.hdrRefreshRequested) {
            displayDirty_ = false;
            RefreshDisplayHdrStateInternal();
        }

        bool isTargetFullscreen = true;
        if (snap.engineCfg.captureApi == CAPTURE_WGC && snap.wantTargetHwnd && IsWindow(snap.wantTargetHwnd)) {
            RECT wr{};
            if (FAILED(DwmGetWindowAttribute(snap.wantTargetHwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &wr, sizeof(wr)))) {
                GetWindowRect(snap.wantTargetHwnd, &wr);
            }
            isTargetFullscreen = ((wr.right - wr.left) >= screenW_ - 4 && (wr.bottom - wr.top) >= screenH_ - 4);
        }
        bool needsTransparency = (snap.engineCfg.captureApi == CAPTURE_WGC) &&
                                 !isTargetFullscreen &&
                                 !(snap.engineCfg.scaleMode != SCALE_OFF && snap.engineCfg.scaleFullscreenStretch);
        bool wantHwndTearing = snap.engineCfg.allowTearing && tearingSupported_ && !needsTransparency;
        if (snap.wantActive && (wantHwndTearing != hwndTearingMode_)) {
            std::string dummyErr;
            RecreateWindowAndSwapChain(wantHwndTearing, displayHdr_.hdrActive, dummyErr);
            snap.forceRedraw = true;
        }

        ApplyActiveStateInternal(snap.wantActive, snap.wantSidebarHwnd, snap.wantTargetHwnd, snap.engineCfg.captureApi);
        if (snap.zOrderDirty) {
            ApplyZOrderInternal(snap.wantSidebarHwnd);
        }

        if (snap.screenshotRequested) {
            if (active_) {
                pendingScreenshot_ = true;
                pendingScreenshotPath_ = snap.screenshotPath;
                snap.forceRedraw = true;
            } else {
                std::lock_guard<std::mutex> lk(pubMutex_);
                pubScreenshotOk_ = false;
                pubScreenshotPath_ = snap.screenshotPath;
                pubScreenshotErr_ = L"Overlay is in Standby \u2014 enter a game or turn off Game-Only Mode first.";
                screenshotResultReady_.store(true, std::memory_order_release);
            }
        }

        if (!active_) {
            MsgWaitForMultipleObjects(1, &wakeEvent_, FALSE, 100, QS_ALLINPUT);
            continue;
        }

        TickAndRenderInternal(snap.params, snap.splitPreview, snap.splitPos, snap.forceRedraw, snap.engineCfg);
    }

    ShutdownInternal();
    if (SUCCEEDED(roHr)) RoUninitialize();
    timeEndPeriod(1);
}

void Dx11Overlay::ShutdownInternal() {
    ReleaseWgc();
    ReleaseDuplication();
    wgcDevice_.Reset();
    if (wgcFrameEvent_) {
        CloseHandle(wgcFrameEvent_);
        wgcFrameEvent_ = nullptr;
    }
    capturedSRV_.Reset();
    capturedTex_.Reset();
    prevFrameSRV_.Reset();
    prevFrameTex_.Reset();
    interpSRV_.Reset();
    interpRTV_.Reset();
    interpTex_.Reset();
    upscaleTempSRV_.Reset();
    upscaleTempRTV_.Reset();
    upscaleTempTex_.Reset();
    scaledSRV_.Reset();
    scaledRTV_.Reset();
    scaledTex_.Reset();
    scaledTexW_ = 0;
    scaledTexH_ = 0;
    scaledTexFmt_ = DXGI_FORMAT_UNKNOWN;
    flowPyr_ = {};
    sfShaders_ = {};
    rtv_.Reset();
    if (frameLatencyWaitable_) {
        CloseHandle(frameLatencyWaitable_);
        frameLatencyWaitable_ = nullptr;
    }
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
    factory2_.Reset();
    context_.Reset();
    device_.Reset();

    if (hwndOverlay_) {
        DestroyWindow(hwndOverlay_);
        hwndOverlay_ = nullptr;
    }
    targetHwnd_ = nullptr;
    active_ = false;
    activeCaptureApi_ = CAPTURE_DXGI;
    analyzeFrameTick_ = 0u;
    hwndTearingMode_ = false;
    swapChainAllowsTearing_ = false;
    hasCapturedFrame_ = false;
    overlayCapturable_ = false;
    swapChainIsHdr_ = false;
    pendingScreenshot_ = false;
    pendingScreenshotPath_.clear();
    telemetry_.overlayActive = false;
    telemetry_.usingWgc = false;
}

bool Dx11Overlay::SetOverlayCaptureVisibility(bool allowCapture, bool forceSync) {
    if (!forceSync && overlayCapturable_ == allowCapture) return true;
    if (!hwndOverlay_) {
        overlayCapturable_ = false;
        return false;
    }
    if (SetWindowDisplayAffinity(hwndOverlay_, allowCapture ? WDA_NONE : WDA_EXCLUDEFROMCAPTURE)) {
        overlayCapturable_ = allowCapture;
        SetWindowPos(
            hwndOverlay_,
            nullptr,
            0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED
        );
        if (useDComp_ && dcompDevice_) {
            dcompDevice_->Commit();
        }
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
    ResetFrameHistory();
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
    ComPtr<ABI::Windows::Graphics::Capture::IGraphicsCaptureSession5> session5;
    if (SUCCEEDED(session.As(&session5)) && session5) {
        ABI::Windows::Foundation::TimeSpan minInterval{ 10000 };
        session5->put_MinUpdateInterval(minInterval);
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
    wgcAtomic_.store(true, std::memory_order_release);
    hasCapturedFrame_ = false;
    wgcSrvCache_.clear();
    {
        std::lock_guard<std::mutex> lk(pubMutex_);
        telemetry_.overlayActive = active_;
        telemetry_.usingWgc = true;
        pubTelemetry_.overlayActive = active_;
        pubTelemetry_.usingWgc = true;
    }

    // Safe to allow OBS / Windows Screenshots to capture our overlay now that we capture ONLY targetHwnd
    SetOverlayCaptureVisibility(true, true);
    return true;
}

void Dx11Overlay::ReleaseWgc() {
    SetOverlayCaptureVisibility(false);
    wgcSrvCache_.clear();
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
    wgcAtomic_.store(false, std::memory_order_release);
    hasCapturedFrame_ = false;
    ResetFrameHistory();
}

ID3D11ShaderResourceView* Dx11Overlay::GetOrCreateWgcSrv(ID3D11Texture2D* wgcTex) {
    if (!wgcTex || !device_) return nullptr;
    for (const auto& entry : wgcSrvCache_) {
        if (entry.tex == wgcTex) {
            return entry.srv.Get();
        }
    }
    ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(device_->CreateShaderResourceView(wgcTex, nullptr, &srv)) || !srv) {
        return nullptr;
    }
    if (wgcSrvCache_.size() >= 8) {
        wgcSrvCache_.erase(wgcSrvCache_.begin());
    }
    wgcSrvCache_.push_back({ wgcTex, srv });
    return wgcSrvCache_.back().srv.Get();
}

bool Dx11Overlay::EnsureCaptureSource(int desiredApi) {
    if (desiredApi == CAPTURE_WGC) {
        if (targetHwnd_ && IsWindow(targetHwnd_) && !IsIconic(targetHwnd_) && InitWgc(targetHwnd_)) {
            return true;
        }
        return InitDuplication();
    }
    if (usingWgc_) {
        ReleaseWgc();
    }
    return InitDuplication();
}

void Dx11Overlay::ApplyActiveStateInternal(bool active, HWND sidebarHwnd, HWND targetHwnd, int desiredCaptureApi) {
    lastSidebarHwnd_ = sidebarHwnd;
    bool targetChanged = (targetHwnd != targetHwnd_);
    bool apiChanged = (desiredCaptureApi != activeCaptureApi_);
    targetHwnd_ = targetHwnd;

    if (active_ == active) {
        if (active_ && (apiChanged || (desiredCaptureApi == CAPTURE_WGC && targetChanged))) {
            ReleaseWgc();
            ReleaseDuplication();
            lastWgcRetryMs_ = 0;
            lastDuplRetryMs_ = 0;
            activeCaptureApi_ = desiredCaptureApi;
            EnsureCaptureSource(desiredCaptureApi);
        } else if (!active_ && hwndOverlay_ && IsWindowVisible(hwndOverlay_) &&
                   (!useDComp_ || !sidebarHwnd || !IsWindowVisible(sidebarHwnd))) {
            ShowWindow(hwndOverlay_, SW_HIDE);
        }
        return;
    }
    active_ = active;
    activeAtomic_.store(active, std::memory_order_release);
    if (!hwndOverlay_) return;

    if (active_) {
        visualAttached_ = false;
        lastWgcRetryMs_ = 0;
        lastDuplRetryMs_ = 0;
        activeCaptureApi_ = desiredCaptureApi;
        EnsureCaptureSource(desiredCaptureApi);
        {
            std::lock_guard<std::mutex> lk(pubMutex_);
            telemetry_.overlayActive = active_;
            telemetry_.usingWgc = usingWgc_;
            pubTelemetry_.overlayActive = active_;
            pubTelemetry_.usingWgc = usingWgc_;
        }
    } else {
        if (pendingScreenshot_) {
            pendingScreenshot_ = false;
            std::lock_guard<std::mutex> lk(pubMutex_);
            pubScreenshotOk_ = false;
            pubScreenshotPath_ = pendingScreenshotPath_;
            pendingScreenshotPath_.clear();
            pubScreenshotErr_ = L"Overlay entered Standby before screenshot completed.";
            screenshotResultReady_.store(true, std::memory_order_release);
        }
        if (useDComp_ && dcompTarget_ && dcompDevice_) {
            if (visualAttached_) {
                dcompTarget_->SetRoot(nullptr);
                dcompDevice_->Commit();
                visualAttached_ = false;
            }
        }
        if (!useDComp_ || !sidebarHwnd || !IsWindowVisible(sidebarHwnd)) {
            ShowWindow(hwndOverlay_, SW_HIDE);
        }
        ReleaseWgc();
        ReleaseDuplication();
        ResetFrameHistory();
    }
}

void Dx11Overlay::ApplyZOrderInternal(HWND sidebarHwnd) {
    lastSidebarHwnd_ = sidebarHwnd;
    if (active_ && hwndOverlay_ && IsWindowVisible(hwndOverlay_)) {
        if (sidebarHwnd && IsWindowVisible(sidebarHwnd)) {
            if (GetWindow(hwndOverlay_, GW_HWNDPREV) != sidebarHwnd) {
                SetWindowPos(
                    hwndOverlay_,
                    sidebarHwnd,
                    0, 0, screenW_, screenH_,
                    SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_ASYNCWINDOWPOS
                );
            }
        } else if ((GetWindowLongW(hwndOverlay_, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0) {
            SetWindowPos(
                hwndOverlay_,
                HWND_TOPMOST,
                0, 0, screenW_, screenH_,
                SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_ASYNCWINDOWPOS
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

void Dx11Overlay::ResetFrameHistory() {
    analyzeFrameTick_ = 0u;
    hasPrevFrame_ = false;
    flowValid_ = false;
    fgFlushedCurr_ = true;
    prevFrameTimeSec_ = 0.0;
    currFrameTimeSec_ = 0.0;
    nextPresentSec_ = 0.0;
    fgGenCountSinceReal_ = 0;
    fpsWindowStartSec_ = 0.0;
    srcFrameCountWin_ = 0;
    outFrameCountWin_ = 0;
    telemetry_.sourceFps = 0.0f;
    telemetry_.outputFps = 0.0f;
    telemetry_.fgRunning = false;
    telemetry_.fallbackActive = false;
    telemetry_.overlayActive = active_;
    telemetry_.usingWgc = usingWgc_;
    {
        std::lock_guard<std::mutex> lk(pubMutex_);
        pubTelemetry_ = telemetry_;
    }
}

double Dx11Overlay::NowSeconds() const {
    LARGE_INTEGER cnt{};
    QueryPerformanceCounter(&cnt);
    return (qpcFreq_.QuadPart > 0) ? (static_cast<double>(cnt.QuadPart) / static_cast<double>(qpcFreq_.QuadPart)) : 0.0;
}

void Dx11Overlay::UpdateTelemetryWindow(double nowSec, bool isNewSource, bool fgActive) {
    telemetry_.fgRunning = fgActive;
    telemetry_.monitorHz = monitorHz_;
    telemetry_.overlayActive = active_;
    telemetry_.usingWgc = usingWgc_;
    if (fpsWindowStartSec_ <= 0.0) {
        fpsWindowStartSec_ = nowSec;
    }
    if (isNewSource) ++srcFrameCountWin_;
    ++outFrameCountWin_;
    double elapsed = nowSec - fpsWindowStartSec_;
    if (elapsed >= 0.5) {
        telemetry_.sourceFps = static_cast<float>(srcFrameCountWin_ / elapsed);
        telemetry_.outputFps = static_cast<float>(outFrameCountWin_ / elapsed);
        fpsWindowStartSec_ = nowSec;
        srcFrameCountWin_ = 0;
        outFrameCountWin_ = 0;
        std::lock_guard<std::mutex> lk(pubMutex_);
        pubTelemetry_ = telemetry_;
    } else {
        std::lock_guard<std::mutex> lk(pubMutex_);
        pubTelemetry_.fgRunning = fgActive;
        pubTelemetry_.overlayActive = active_;
        pubTelemetry_.usingWgc = usingWgc_;
    }
}

bool Dx11Overlay::EnsureFrameHistoryAndInterpTextures(UINT srcW, UINT srcH, DXGI_FORMAT fmt) {
    if (prevFrameSRV_ && interpSRV_ && interpRTV_ &&
        historyDesc_.Width == srcW && historyDesc_.Height == srcH && historyDesc_.Format == fmt) {
        return true;
    }

    prevFrameSRV_.Reset();
    prevFrameTex_.Reset();
    interpSRV_.Reset();
    interpRTV_.Reset();
    interpTex_.Reset();
    hasPrevFrame_ = false;
    flowValid_ = false;

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = srcW;
    td.Height = srcH;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = fmt;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device_->CreateTexture2D(&td, nullptr, &prevFrameTex_)) ||
        FAILED(device_->CreateShaderResourceView(prevFrameTex_.Get(), nullptr, &prevFrameSRV_))) {
        prevFrameTex_.Reset();
        prevFrameSRV_.Reset();
        return false;
    }

    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    if (FAILED(device_->CreateTexture2D(&td, nullptr, &interpTex_)) ||
        FAILED(device_->CreateShaderResourceView(interpTex_.Get(), nullptr, &interpSRV_)) ||
        FAILED(device_->CreateRenderTargetView(interpTex_.Get(), nullptr, &interpRTV_))) {
        interpTex_.Reset();
        interpSRV_.Reset();
        interpRTV_.Reset();
        prevFrameTex_.Reset();
        prevFrameSRV_.Reset();
        return false;
    }

    historyDesc_ = td;
    return true;
}

bool Dx11Overlay::EnsureScaledTextures(UINT dstW, UINT dstH, DXGI_FORMAT fmt, bool needTemp) {
    if (scaledSRV_ && scaledRTV_ && scaledTexW_ == dstW && scaledTexH_ == dstH && scaledTexFmt_ == fmt &&
        (!needTemp || (upscaleTempSRV_ && upscaleTempRTV_))) {
        return true;
    }

    if (scaledTexW_ != dstW || scaledTexH_ != dstH || scaledTexFmt_ != fmt || !scaledSRV_ || !scaledRTV_) {
        scaledSRV_.Reset();
        scaledRTV_.Reset();
        scaledTex_.Reset();
        upscaleTempSRV_.Reset();
        upscaleTempRTV_.Reset();
        upscaleTempTex_.Reset();

        D3D11_TEXTURE2D_DESC td = {};
        td.Width = dstW;
        td.Height = dstH;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = fmt;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        if (FAILED(device_->CreateTexture2D(&td, nullptr, &scaledTex_)) ||
            FAILED(device_->CreateShaderResourceView(scaledTex_.Get(), nullptr, &scaledSRV_)) ||
            FAILED(device_->CreateRenderTargetView(scaledTex_.Get(), nullptr, &scaledRTV_))) {
            scaledTex_.Reset();
            scaledSRV_.Reset();
            scaledRTV_.Reset();
            return false;
        }
    }

    if (needTemp && (scaledTexW_ != dstW || scaledTexH_ != dstH || scaledTexFmt_ != fmt || !upscaleTempSRV_ || !upscaleTempRTV_)) {
        upscaleTempSRV_.Reset();
        upscaleTempRTV_.Reset();
        upscaleTempTex_.Reset();

        D3D11_TEXTURE2D_DESC td = {};
        td.Width = dstW;
        td.Height = dstH;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = fmt;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        if (FAILED(device_->CreateTexture2D(&td, nullptr, &upscaleTempTex_)) ||
            FAILED(device_->CreateShaderResourceView(upscaleTempTex_.Get(), nullptr, &upscaleTempSRV_)) ||
            FAILED(device_->CreateRenderTargetView(upscaleTempTex_.Get(), nullptr, &upscaleTempRTV_))) {
            upscaleTempTex_.Reset();
            upscaleTempSRV_.Reset();
            upscaleTempRTV_.Reset();
            return false;
        }
    }

    scaledTexW_ = dstW;
    scaledTexH_ = dstH;
    scaledTexFmt_ = fmt;
    return true;
}

bool Dx11Overlay::ProcessAndPresentFrame(
    const FilterParams& params,
    bool splitPreview,
    float splitPos,
    ID3D11ShaderResourceView* activeInputSRV,
    UINT inputW,
    UINT inputH,
    DXGI_FORMAT inputFmt,
    const EngineRenderConfig& engineCfg
) {
    if (!activeInputSRV || inputW == 0 || inputH == 0) return false;

    int vpX = 0, vpY = 0;
    UINT vpW = static_cast<UINT>(screenW_);
    UINT vpH = static_cast<UINT>(screenH_);
    bool opaqueLetterbox = false;

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
        if (engineCfg.scaleMode != SCALE_OFF && engineCfg.scaleFullscreenStretch) {
            opaqueLetterbox = true;
            if (engineCfg.scaleMode == SCALE_INTEGER) {
                UINT k = std::max<UINT>(1u, std::min(static_cast<UINT>(screenW_) / inputW, static_cast<UINT>(screenH_) / inputH));
                vpW = std::min<UINT>(inputW * k, static_cast<UINT>(screenW_));
                vpH = std::min<UINT>(inputH * k, static_cast<UINT>(screenH_));
            } else {
                double ratio = std::min(static_cast<double>(screenW_) / inputW, static_cast<double>(screenH_) / inputH);
                vpW = std::clamp(static_cast<UINT>(std::lround(inputW * ratio)), 1u, static_cast<UINT>(screenW_));
                vpH = std::clamp(static_cast<UINT>(std::lround(inputH * ratio)), 1u, static_cast<UINT>(screenH_));
            }
            vpX = (screenW_ > static_cast<int>(vpW)) ? ((screenW_ - static_cast<int>(vpW)) / 2) : 0;
            vpY = (screenH_ > static_cast<int>(vpH)) ? ((screenH_ - static_cast<int>(vpH)) / 2) : 0;
        } else {
            vpX = static_cast<int>(winRc.left);
            vpY = static_cast<int>(winRc.top);
            vpW = static_cast<UINT>(w);
            vpH = static_cast<UINT>(h);
        }
    } else if (engineCfg.scaleMode != SCALE_OFF && engineCfg.scaleMode == SCALE_INTEGER && engineCfg.scaleFullscreenStretch) {
        opaqueLetterbox = true;
    }

    ID3D11ShaderResourceView* stageCInput = activeInputSRV;
    UINT stageCW = inputW;
    UINT stageCH = inputH;

    if (engineCfg.scaleMode != SCALE_OFF) {
        UINT dstW = vpW;
        UINT dstH = vpH;
        bool needTemp = (engineCfg.scaleMode == SCALE_FSR1);
        if (EnsureScaledTextures(dstW, dstH, inputFmt, needTemp)) {
            DrawSpatialScalePass(
                context_.Get(),
                sfShaders_,
                activeInputSRV,
                inputW,
                inputH,
                upscaleTempRTV_.Get(),
                upscaleTempSRV_.Get(),
                scaledRTV_.Get(),
                dstW,
                dstH,
                engineCfg.scaleMode,
                engineCfg.scaleSharpness
            );
            stageCInput = scaledSRV_.Get();
            stageCW = dstW;
            stageCH = dstH;
        }
    }

    return RenderInternal(
        params,
        splitPreview,
        splitPos,
        stageCInput,
        stageCW,
        stageCH,
        vpX,
        vpY,
        vpW,
        vpH,
        opaqueLetterbox,
        engineCfg.fgMode != FG_OFF
    );
}

bool Dx11Overlay::CaptureBackbufferScreenshot(
    int vpX,
    int vpY,
    UINT vpW,
    UINT vpH,
    bool fullScreenVp,
    const std::wstring& targetPath,
    std::wstring& outError
) {
    if (!device_ || !context_ || !swapChain_) {
        outError = L"DirectX overlay device not ready for screenshot.";
        return false;
    }

    ComPtr<ID3D11Texture2D> backBuf;
    HRESULT hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuf));
    if (FAILED(hr) || !backBuf) {
        outError = L"Failed acquiring backbuffer (" + Utf8ToWide(HrToString(hr)) + L").";
        return false;
    }

    D3D11_TEXTURE2D_DESC bbDesc{};
    backBuf->GetDesc(&bbDesc);

    D3D11_TEXTURE2D_DESC stDesc = bbDesc;
    stDesc.MipLevels = 1;
    stDesc.ArraySize = 1;
    stDesc.Usage = D3D11_USAGE_STAGING;
    stDesc.BindFlags = 0;
    stDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stDesc.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> staging;
    hr = device_->CreateTexture2D(&stDesc, nullptr, &staging);
    if (FAILED(hr) || !staging) {
        outError = L"Failed allocating staging texture (" + Utf8ToWide(HrToString(hr)) + L").";
        return false;
    }

    context_->CopyResource(staging.Get(), backBuf.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr) || !mapped.pData) {
        outError = L"Failed mapping GPU backbuffer (" + Utf8ToWide(HrToString(hr)) + L").";
        return false;
    }

    UINT cropX = 0;
    UINT cropY = 0;
    UINT cropW = bbDesc.Width;
    UINT cropH = bbDesc.Height;
    if (usingWgc_ && !fullScreenVp) {
        int x0 = std::max(0, vpX);
        int y0 = std::max(0, vpY);
        int x1 = std::min(static_cast<int>(bbDesc.Width), vpX + static_cast<int>(vpW));
        int y1 = std::min(static_cast<int>(bbDesc.Height), vpY + static_cast<int>(vpH));
        if (x1 > x0 && y1 > y0) {
            cropX = static_cast<UINT>(x0);
            cropY = static_cast<UINT>(y0);
            cropW = static_cast<UINT>(x1 - x0);
            cropH = static_cast<UINT>(y1 - y0);
        }
    }

    bool ok = false;
    if (bbDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM) {
        const uint8_t* startPtr = static_cast<const uint8_t*>(mapped.pData) +
            static_cast<size_t>(cropY) * mapped.RowPitch +
            static_cast<size_t>(cropX) * 4u;
        ok = SaveBgraPixelsToPng(startPtr, cropW, cropH, mapped.RowPitch, targetPath, outError);
    } else if (bbDesc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
        const float invSdrScale = 80.0f / std::max(displayHdr_.sdrWhiteNits, 80.0f);
        const UINT dstPitch = cropW * 4u;
        std::vector<uint8_t> bgraBuf(static_cast<size_t>(cropH) * dstPitch);
        for (UINT y = 0; y < cropH; ++y) {
            const uint16_t* srcRow = reinterpret_cast<const uint16_t*>(
                static_cast<const uint8_t*>(mapped.pData) +
                static_cast<size_t>(cropY + y) * mapped.RowPitch +
                static_cast<size_t>(cropX) * 8u
            );
            uint8_t* dstRow = bgraBuf.data() + static_cast<size_t>(y) * dstPitch;
            for (UINT x = 0; x < cropW; ++x) {
                float r = HalfToFloat(srcRow[x * 4u + 0u]) * invSdrScale;
                float g = HalfToFloat(srcRow[x * 4u + 1u]) * invSdrScale;
                float b = HalfToFloat(srcRow[x * 4u + 2u]) * invSdrScale;
                dstRow[x * 4u + 0u] = LinearToSrgbU8(b);
                dstRow[x * 4u + 1u] = LinearToSrgbU8(g);
                dstRow[x * 4u + 2u] = LinearToSrgbU8(r);
                dstRow[x * 4u + 3u] = 0xFFu;
            }
        }
        ok = SaveBgraPixelsToPng(bgraBuf.data(), cropW, cropH, dstPitch, targetPath, outError);
    } else {
        outError = L"Unsupported swapchain format for screenshot.";
    }

    context_->Unmap(staging.Get(), 0);
    return ok;
}

bool Dx11Overlay::RenderInternal(
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
) {
    if (!inputSRV || !rtv_ || !swapChain_) return false;
    (void)fastPresent;

    if (frameLatencyWaitable_) {
        WaitForSingleObjectEx(frameLatencyWaitable_, 16, FALSE);
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context_->Map(constantBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        EnhanceCBData cb = BuildCB(
            params,
            splitPreview,
            splitPos,
            static_cast<int>(inputW),
            static_cast<int>(inputH),
            0.25f,
            displayHdr_.sdrWhiteNits,
            swapChainIsHdr_
        );
        std::memcpy(mapped.pData, &cb, sizeof(cb));
        context_->Unmap(constantBuffer_.Get(), 0);
    }

    // Pass 1: Compute Shader scene luminance / darkness / chroma analysis (throttled to every 4th frame)
    ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
    ID3D11Buffer* cbArr[1] = { constantBuffer_.Get() };
    bool runAnalyze = ((analyzeFrameTick_++ & 3u) == 0u);
    if (runAnalyze) {
        ID3D11UnorderedAccessView* nullUAVs[1] = { nullptr };
        context_->PSSetShaderResources(0, 2, nullSRVs);

        ID3D11ShaderResourceView* csSRVs[1] = { inputSRV };
        ID3D11UnorderedAccessView* csUAVs[1] = { statsUAV_.Get() };
        context_->CSSetShader(csAnalyze_.Get(), nullptr, 0);
        context_->CSSetConstantBuffers(0, 1, cbArr);
        context_->CSSetShaderResources(0, 1, csSRVs);
        context_->CSSetUnorderedAccessViews(0, 1, csUAVs, nullptr);
        context_->Dispatch(1, 1, 1);

        context_->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
        context_->CSSetShaderResources(0, 1, nullSRVs);
    }

    D3D11_VIEWPORT vp{};
    vp.TopLeftX = static_cast<float>(vpX);
    vp.TopLeftY = static_cast<float>(vpY);
    vp.Width = static_cast<float>(vpW);
    vp.Height = static_cast<float>(vpH);
    vp.MaxDepth = 1.0f;

    bool fullScreenVp = (vpX == 0 && vpY == 0 && vpW == static_cast<UINT>(screenW_) && vpH == static_cast<UINT>(screenH_));
    if (opaqueLetterbox || !fullScreenVp) {
        const float clearCol[4] = { 0.0f, 0.0f, 0.0f, opaqueLetterbox ? 1.0f : 0.0f };
        context_->ClearRenderTargetView(rtv_.Get(), clearCol);
    }

    ID3D11RenderTargetView* rtvArr[1] = { rtv_.Get() };
    ID3D11ShaderResourceView* psSRVs[2] = { inputSRV, statsSRV_.Get() };
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

    if (pendingScreenshot_) {
        std::wstring ssPath = pendingScreenshotPath_;
        pendingScreenshot_ = false;
        pendingScreenshotPath_.clear();
        std::wstring ssErr;
        bool ssOk = CaptureBackbufferScreenshot(vpX, vpY, vpW, vpH, fullScreenVp, ssPath, ssErr);
        {
            std::lock_guard<std::mutex> lk(pubMutex_);
            pubScreenshotOk_ = ssOk;
            pubScreenshotPath_ = ssPath;
            pubScreenshotErr_ = ssErr;
        }
        screenshotResultReady_.store(true, std::memory_order_release);
    }

    UINT presentFlags = (!useDComp_ && swapChainAllowsTearing_) ? DXGI_PRESENT_ALLOW_TEARING : 0u;
    HRESULT hr = swapChain_->Present(0u, presentFlags);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        return false;
    }
    if (hwndOverlay_ && !IsWindowVisible(hwndOverlay_)) {
        HWND insertAfter = (lastSidebarHwnd_ && IsWindowVisible(lastSidebarHwnd_)) ? lastSidebarHwnd_ : HWND_TOPMOST;
        SetWindowPos(
            hwndOverlay_,
            insertAfter,
            0, 0, screenW_, screenH_,
            SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_ASYNCWINDOWPOS
        );
        SetOverlayCaptureVisibility(overlayCapturable_, true);
    }
    if (useDComp_ && !visualAttached_ && dcompTarget_ && dcompVisual_ && dcompDevice_) {
        dcompTarget_->SetRoot(dcompVisual_.Get());
        dcompDevice_->Commit();
        visualAttached_ = true;
    }
    return true;
}

bool Dx11Overlay::TickAndRenderInternal(
    const FilterParams& params,
    bool splitPreview,
    float splitPos,
    bool forceRedraw,
    const EngineRenderConfig& engineCfg
) {
    if (!active_) return false;
    if (!usingWgc_ && !duplication_) {
        if (!EnsureCaptureSource(engineCfg.captureApi)) {
            return false;
        }
    }

    bool fgEnabled = (engineCfg.fgMode != FG_OFF);
    double baseInterval = std::clamp(avgSourceIntervalSec_, 1.0 / 240.0, 1.0 / 15.0);
    int maxGen = 1;
    double targetOutInterval = baseInterval * 0.5;
    if (engineCfg.fgMode == FG_3X) {
        maxGen = 2;
        targetOutInterval = baseInterval / 3.0;
    } else if (engineCfg.fgMode == FG_4X) {
        maxGen = 3;
        targetOutInterval = baseInterval * 0.25;
    } else if (engineCfg.fgMode == FG_ADAPTIVE) {
        double targetFps = static_cast<double>(std::clamp(engineCfg.fgAdaptiveTargetFps, 60, 240));
        double baseFps = 1.0 / baseInterval;
        maxGen = std::clamp(static_cast<int>(std::ceil(targetFps / std::max(baseFps, 1.0))) - 1, 1, 5);
        targetOutInterval = 1.0 / targetFps;
    }

    auto copySourceToCaptured = [&](ID3D11Texture2D* srcTex, const D3D11_TEXTURE2D_DESC& desc) -> bool {
        bool sameDims = hasCapturedFrame_ &&
            capturedDesc_.Width == desc.Width &&
            capturedDesc_.Height == desc.Height &&
            capturedDesc_.Format == desc.Format;
        if (!EnsureCapturedTexture(desc)) return false;

        if (fgEnabled) {
            if (EnsureFrameHistoryAndInterpTextures(desc.Width, desc.Height, desc.Format) && sameDims) {
                context_->CopyResource(prevFrameTex_.Get(), capturedTex_.Get());
                hasPrevFrame_ = true;
            } else {
                hasPrevFrame_ = false;
                flowValid_ = false;
            }
        } else {
            hasPrevFrame_ = false;
            flowValid_ = false;
        }

        context_->CopyResource(capturedTex_.Get(), srcTex);
        hasCapturedFrame_ = true;
        return true;
    };

    auto finalizeCapturedSourceFrame = [&](UINT srcW, UINT srcH, DXGI_FORMAT srcFmt, ID3D11ShaderResourceView* directSRV) -> bool {
        double nowSec = NowSeconds();
        if (currFrameTimeSec_ > 0.0 && nowSec > currFrameTimeSec_) {
            double dt = nowSec - currFrameTimeSec_;
            if (dt >= (1.0 / 240.0) && dt <= 0.20) {
                avgSourceIntervalSec_ = 0.80 * avgSourceIntervalSec_ + 0.20 * dt;
            }
        }
        prevFrameTimeSec_ = currFrameTimeSec_;
        currFrameTimeSec_ = nowSec;

        if (!directSRV && fgEnabled && hasPrevFrame_ &&
            EnsureFlowPyramid(device_.Get(), flowPyr_, srcW, srcH, engineCfg.fgFlowScale)) {
            ExecuteOpticalFlow(
                context_.Get(),
                sfShaders_,
                flowPyr_,
                prevFrameSRV_.Get(),
                capturedSRV_.Get(),
                engineCfg.fgFastMotionFallback ? 1.0f : 0.0f,
                engineCfg.fgStaticHudProtect ? 1.0f : 0.0f
            );
            flowValid_ = true;
        }

        fgGenCountSinceReal_ = 0;
        bool deferCurrForInterp = (!directSRV && fgEnabled && flowValid_ && hasPrevFrame_);
        fgFlushedCurr_ = !deferCurrForInterp;
        nextPresentSec_ = nowSec + targetOutInterval;
        UpdateTelemetryWindow(nowSec, true, fgEnabled);
        ID3D11ShaderResourceView* presentSRV = directSRV ? directSRV : (deferCurrForInterp ? prevFrameSRV_.Get() : capturedSRV_.Get());
        return ProcessAndPresentFrame(params, splitPreview, splitPos, presentSRV, srcW, srcH, srcFmt, engineCfg);
    };

    auto processNewSourceFrame = [&](ID3D11Texture2D* srcTex, const D3D11_TEXTURE2D_DESC& desc) -> bool {
        if (!copySourceToCaptured(srcTex, desc)) return false;
        return finalizeCapturedSourceFrame(desc.Width, desc.Height, desc.Format, nullptr);
    };

    auto tryGenerateIntermediate = [&]() -> bool {
        if (!fgEnabled || !flowValid_ || !hasPrevFrame_ || !hasCapturedFrame_) return false;
        double nowSec = NowSeconds();
        if (fgGenCountSinceReal_ >= maxGen) {
            if (!fgFlushedCurr_ && nowSec >= currFrameTimeSec_ + std::max(baseInterval * 1.65, targetOutInterval * (maxGen + 1) + 0.010)) {
                fgFlushedCurr_ = true;
                return ProcessAndPresentFrame(
                    params, splitPreview, splitPos,
                    capturedSRV_.Get(), capturedDesc_.Width, capturedDesc_.Height, capturedDesc_.Format,
                    engineCfg
                );
            }
            return false;
        }
        if (nowSec < (nextPresentSec_ - 0.0005)) return false;

        float interpT = 0.5f;
        if (engineCfg.fgMode == FG_ADAPTIVE) {
            interpT = std::clamp(
                static_cast<float>((nowSec - currFrameTimeSec_) / std::max(avgSourceIntervalSec_, 1e-4)),
                0.08f, 0.92f
            );
        } else {
            interpT = static_cast<float>(fgGenCountSinceReal_ + 1) / static_cast<float>(maxGen + 1);
        }

        DrawFrameGenPass(
            context_.Get(),
            sfShaders_,
            flowPyr_,
            prevFrameSRV_.Get(),
            capturedSRV_.Get(),
            interpRTV_.Get(),
            historyDesc_.Width,
            historyDesc_.Height,
            interpT,
            engineCfg.fgStaticHudProtect ? 1.0f : 0.0f
        );

        ++fgGenCountSinceReal_;
        nextPresentSec_ = nowSec + targetOutInterval;
        UpdateTelemetryWindow(nowSec, false, true);
        return ProcessAndPresentFrame(
            params, splitPreview, splitPos,
            interpSRV_.Get(), historyDesc_.Width, historyDesc_.Height, historyDesc_.Format,
            engineCfg
        );
    };

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
            DWORD waitMs = 12;
            if (fgEnabled && flowValid_ && fgGenCountSinceReal_ < maxGen) {
                double remMs = (nextPresentSec_ - NowSeconds()) * 1000.0 - 0.5;
                waitMs = (remMs > 1.0) ? static_cast<DWORD>(std::min(remMs, 3.0)) : 0u;
            }
            if (waitMs > 0) {
                HANDLE waitHandles[2] = { wgcFrameEvent_, wakeEvent_ };
                DWORD handleCount = wakeEvent_ ? 2u : 1u;
                DWORD wr = MsgWaitForMultipleObjects(handleCount, waitHandles, FALSE, waitMs, QS_ALLINPUT);
                drainFrames();
                if (!latestFrame && (wr == WAIT_OBJECT_0 + 1 || wr == WAIT_OBJECT_0 + handleCount)) {
                    return false;
                }
            }
        }

        if (!latestFrame) {
            if (tryGenerateIntermediate()) {
                return true;
            }
            if (forceRedraw && hasCapturedFrame_) {
                return ProcessAndPresentFrame(
                    params, splitPreview, splitPos,
                    capturedSRV_.Get(), capturedDesc_.Width, capturedDesc_.Height, capturedDesc_.Format,
                    engineCfg
                );
            }
            return false;
        }

        ABI::Windows::Graphics::SizeInt32 contentSize{};
        if (SUCCEEDED(latestFrame->get_ContentSize(&contentSize)) &&
            contentSize.Width > 0 && contentSize.Height > 0 &&
            (contentSize.Width != wgcPoolSize_.Width || contentSize.Height != wgcPoolSize_.Height)) {
            wgcPoolSize_ = contentSize;
            wgcSrvCache_.clear();
            wgcFramePool_->Recreate(
                wgcDevice_.Get(),
                ABI::Windows::Graphics::DirectX::DirectXPixelFormat_B8G8R8A8UIntNormalized,
                2,
                wgcPoolSize_
            );
        }

        bool ok = false;
        ComPtr<ABI::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface> surf;
        if (SUCCEEDED(latestFrame->get_Surface(&surf)) && surf) {
            ComPtr<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> access;
            if (SUCCEEDED(surf.As(&access)) && access) {
                ComPtr<ID3D11Texture2D> wgcTex;
                if (SUCCEEDED(access->GetInterface(IID_PPV_ARGS(&wgcTex))) && wgcTex) {
                    D3D11_TEXTURE2D_DESC desc{};
                    wgcTex->GetDesc(&desc);
                    bool sidebarVis = (lastSidebarHwnd_ && IsWindowVisible(lastSidebarHwnd_));
                    bool canZeroCopy = !fgEnabled && hasCapturedFrame_ && !sidebarVis &&
                        capturedDesc_.Width == desc.Width && capturedDesc_.Height == desc.Height &&
                        capturedDesc_.Format == desc.Format &&
                        (desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) != 0;
                    if (canZeroCopy) {
                        if (ID3D11ShaderResourceView* directSRV = GetOrCreateWgcSrv(wgcTex.Get())) {
                            ok = finalizeCapturedSourceFrame(desc.Width, desc.Height, desc.Format, directSRV);
                        } else {
                            ok = processNewSourceFrame(wgcTex.Get(), desc);
                        }
                    } else {
                        ok = processNewSourceFrame(wgcTex.Get(), desc);
                    }
                }
            }
        }

        ComPtr<ABI::Windows::Foundation::IClosable> frameCl;
        if (SUCCEEDED(latestFrame.As(&frameCl)) && frameCl) {
            frameCl->Close();
        }
        return ok;
    }

    DXGI_OUTDUPL_FRAME_INFO frameInfo{};
    ComPtr<IDXGIResource> desktopRes;
    UINT ddTimeout = (fgEnabled && flowValid_) ? 1u : 8u;
    HRESULT hr = duplication_->AcquireNextFrame(ddTimeout, &frameInfo, &desktopRes);

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        if (tryGenerateIntermediate()) {
            return true;
        }
        if (forceRedraw && hasCapturedFrame_) {
            return ProcessAndPresentFrame(
                params, splitPreview, splitPos,
                capturedSRV_.Get(), capturedDesc_.Width, capturedDesc_.Height, capturedDesc_.Format,
                engineCfg
            );
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

    if (hasCapturedFrame_ && frameInfo.LastPresentTime.QuadPart == 0 && frameInfo.AccumulatedFrames == 0) {
        desktopRes.Reset();
        duplication_->ReleaseFrame();
        if (tryGenerateIntermediate()) {
            return true;
        }
        if (forceRedraw && hasCapturedFrame_) {
            return ProcessAndPresentFrame(
                params, splitPreview, splitPos,
                capturedSRV_.Get(), capturedDesc_.Width, capturedDesc_.Height, capturedDesc_.Format,
                engineCfg
            );
        }
        return false;
    }

    bool copiedNewFrame = false;
    D3D11_TEXTURE2D_DESC srcDesc{};
    {
        ComPtr<ID3D11Texture2D> desktopTex;
        if (SUCCEEDED(desktopRes.As(&desktopTex)) && desktopTex) {
            desktopTex->GetDesc(&srcDesc);
            copiedNewFrame = copySourceToCaptured(desktopTex.Get(), srcDesc);
        }
    }
    // Release DXGI desktop frame lock BEFORE optical flow, waitable sync, and Present()
    desktopRes.Reset();
    duplication_->ReleaseFrame();

    if (!copiedNewFrame) {
        return false;
    }
    return finalizeCapturedSourceFrame(srcDesc.Width, srcDesc.Height, srcDesc.Format, nullptr);
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
    bool wgcUnthrottled = false;
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
            ComPtr<ABI::Windows::Graphics::Capture::IGraphicsCaptureSession5> sess5;
            if (SUCCEEDED(sess.As(&sess5)) && sess5) {
                ABI::Windows::Foundation::TimeSpan minInterval{ 10000 };
                wgcUnthrottled = SUCCEEDED(sess5->put_MinUpdateInterval(minInterval));
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
    if (!affOk) {
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        outReport = "SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE) failed.";
        return false;
    }
    if (!wgcOk) {
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        outReport = wgcErr;
        return false;
    }
    rep << "[PASS] DirectComposition + WDA_EXCLUDEFROMCAPTURE verified\n";
    rep << "[PASS] Windows Graphics Capture (OBS-Recordable Mode) verified (win11Unthrottled="
        << (wgcUnthrottled ? "true" : "false") << ")\n";

    // 3C. Empirical VRR / Allow Tearing SwapChain + Click-Through + Pixel Visibility Test
    {
        ComPtr<IDXGIFactory2> f2;
        ComPtr<IDXGIFactory5> f5;
        if (FAILED(adapter->GetParent(IID_PPV_ARGS(&f2))) || !f2) {
            UnregisterClassW(wc.lpszClassName, wc.hInstance);
            outReport = "Failed acquiring IDXGIFactory2 for VRR self-test.";
            return false;
        }
        BOOL tearingSup = FALSE;
        if (SUCCEEDED(f2.As(&f5)) && f5) {
            f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearingSup, sizeof(tearingSup));
        }

        HWND vrrWnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT |
            WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            wc.lpszClassName, L"VRR SelfTest", WS_POPUP,
            120, 120, 128, 128,
            nullptr, nullptr, wc.hInstance, nullptr
        );
        if (!vrrWnd) {
            UnregisterClassW(wc.lpszClassName, wc.hInstance);
            outReport = "Failed creating HWND for VRR Allow Tearing self-test.";
            return false;
        }
        BOOL lwaOk = SetLayeredWindowAttributes(vrrWnd, 0, 255, LWA_ALPHA);

        DXGI_SWAP_CHAIN_DESC1 vscDesc{};
        vscDesc.Width = 128;
        vscDesc.Height = 128;
        vscDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        vscDesc.SampleDesc.Count = 1;
        vscDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        vscDesc.BufferCount = 3;
        vscDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        vscDesc.Scaling = DXGI_SCALING_STRETCH;
        vscDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        vscDesc.Flags = (tearingSup ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u) |
                        DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

        ComPtr<IDXGISwapChain1> vrrSc;
        hr = f2->CreateSwapChainForHwnd(dev.Get(), vrrWnd, &vscDesc, nullptr, nullptr, &vrrSc);
        if (FAILED(hr) || !vrrSc) {
            DestroyWindow(vrrWnd);
            UnregisterClassW(wc.lpszClassName, wc.hInstance);
            outReport = "CreateSwapChainForHwnd (VRR Allow Tearing) failed: " + HrToString(hr);
            return false;
        }
        f2->MakeWindowAssociation(vrrWnd, DXGI_MWA_NO_ALT_ENTER);

        ComPtr<ID3D11Texture2D> vrrBuf, vrrStage;
        ComPtr<ID3D11RenderTargetView> vrrRtv;
        if (FAILED(hr = vrrSc->GetBuffer(0, IID_PPV_ARGS(&vrrBuf))) ||
            FAILED(hr = dev->CreateRenderTargetView(vrrBuf.Get(), nullptr, &vrrRtv))) {
            DestroyWindow(vrrWnd);
            UnregisterClassW(wc.lpszClassName, wc.hInstance);
            outReport = "Failed creating RTV for VRR self-test: " + HrToString(hr);
            return false;
        }

        D3D11_TEXTURE2D_DESC vStDesc{};
        vStDesc.Width = 128;
        vStDesc.Height = 128;
        vStDesc.MipLevels = 1;
        vStDesc.ArraySize = 1;
        vStDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        vStDesc.SampleDesc.Count = 1;
        vStDesc.Usage = D3D11_USAGE_STAGING;
        vStDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(hr = dev->CreateTexture2D(&vStDesc, nullptr, &vrrStage)) || !vrrStage) {
            vrrRtv.Reset();
            vrrBuf.Reset();
            vrrSc.Reset();
            DestroyWindow(vrrWnd);
            UnregisterClassW(wc.lpszClassName, wc.hInstance);
            outReport = "Failed creating staging texture for VRR self-test: " + HrToString(hr);
            return false;
        }

        const float magenta[4] = { 1.0f, 0.0f, 1.0f, 1.0f };
        ctx->ClearRenderTargetView(vrrRtv.Get(), magenta);
        ctx->CopyResource(vrrStage.Get(), vrrBuf.Get());

        int pxR = 0, pxG = 0, pxB = 0;
        D3D11_MAPPED_SUBRESOURCE vMap{};
        bool mappedOk = SUCCEEDED(ctx->Map(vrrStage.Get(), 0, D3D11_MAP_READ, 0, &vMap));
        if (mappedOk) {
            const uint8_t* p = static_cast<const uint8_t*>(vMap.pData) + 64 * vMap.RowPitch + 64 * 4;
            pxB = p[0];
            pxG = p[1];
            pxR = p[2];
            ctx->Unmap(vrrStage.Get(), 0);
        }

        HANDLE waitObj = nullptr;
        bool waitableOk = false;
        {
            ComPtr<IDXGISwapChain2> vrrSc2;
            if (SUCCEEDED(vrrSc.As(&vrrSc2)) && vrrSc2 &&
                SUCCEEDED(vrrSc2->SetMaximumFrameLatency(1))) {
                waitObj = vrrSc2->GetFrameLatencyWaitableObject();
                waitableOk = (waitObj != nullptr);
            }
        }
        ShowWindow(vrrWnd, SW_SHOWNOACTIVATE);
        UINT presFlags = tearingSup ? DXGI_PRESENT_ALLOW_TEARING : 0u;
        DWORD w1 = waitObj ? WaitForSingleObjectEx(waitObj, 200, FALSE) : WAIT_FAILED;
        HRESULT hrP1 = vrrSc->Present(0u, presFlags);
        ctx->ClearRenderTargetView(vrrRtv.Get(), magenta);
        DWORD w2 = waitObj ? WaitForSingleObjectEx(waitObj, 200, FALSE) : WAIT_FAILED;
        HRESULT hrP2 = vrrSc->Present(0u, presFlags);

        HWND hitWnd = WindowFromPoint(POINT{ 184, 184 });
        BOOL vrrAffOk = SetWindowDisplayAffinity(vrrWnd, WDA_EXCLUDEFROMCAPTURE);

        if (waitObj) CloseHandle(waitObj);
        vrrStage.Reset();
        vrrRtv.Reset();
        vrrBuf.Reset();
        vrrSc.Reset();
        DestroyWindow(vrrWnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);

        if (!lwaOk) {
            outReport = "SetLayeredWindowAttributes(255, LWA_ALPHA) failed on VRR HWND.";
            return false;
        }
        if (!waitableOk || w1 != WAIT_OBJECT_0 || w2 != WAIT_OBJECT_0) {
            std::ostringstream errStr;
            errStr << "Waitable frame latency (max=1) failed: waitableOk=" << (waitableOk ? "true" : "false")
                   << ", w1=" << w1 << ", w2=" << w2;
            outReport = errStr.str();
            return false;
        }
        if (FAILED(hrP1) || FAILED(hrP2)) {
            outReport = "Present(0, ALLOW_TEARING) failed (" + HrToString(FAILED(hrP1) ? hrP1 : hrP2) + ").";
            return false;
        }
        if (!mappedOk || pxR != 255 || pxG != 0 || pxB != 255) {
            std::ostringstream errStr;
            errStr << "VRR Allow Tearing backbuffer readback failed at (64,64): RGB("
                   << pxR << "," << pxG << "," << pxB << ")";
            outReport = errStr.str();
            return false;
        }
        if (hitWnd == vrrWnd) {
            outReport = "VRR overlay click-through failed (WindowFromPoint hit overlay HWND).";
            return false;
        }
        if (!vrrAffOk) {
            outReport = "VRR overlay SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE) failed.";
            return false;
        }
        rep << "[PASS] VRR / Allow Tearing SwapChain verified (tearingSupported="
            << (tearingSup ? "true" : "false") << ", waitableLatency1=true, clickThrough=true, WDA_EXCLUDE=true, backbuf=RGB("
            << pxR << "," << pxG << "," << pxB << "))\n";
    }

    // 3D. Verify DisplayAffinity WDA_EXCLUDEFROMCAPTURE -> WDA_NONE transition + SWP_FRAMECHANGED
    {
        WNDCLASSEXW awc{};
        awc.cbSize = sizeof(awc);
        awc.lpfnWndProc = DefWindowProcW;
        awc.hInstance = GetModuleHandleW(nullptr);
        awc.lpszClassName = L"ADV_AffinitySelfTestWnd";
        RegisterClassExW(&awc);
        HWND affWnd = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP,
            awc.lpszClassName, L"Affinity Test", WS_POPUP,
            -200, -200, 64, 64,
            nullptr, nullptr, awc.hInstance, nullptr
        );
        DWORD affExcl = 0xFFFFFFFFu, affNone = 0xFFFFFFFFu;
        BOOL setExclOk = affWnd ? SetWindowDisplayAffinity(affWnd, WDA_EXCLUDEFROMCAPTURE) : FALSE;
        BOOL getExclOk = affWnd ? GetWindowDisplayAffinity(affWnd, &affExcl) : FALSE;
        BOOL setNoneOk = affWnd ? SetWindowDisplayAffinity(affWnd, WDA_NONE) : FALSE;
        if (affWnd) {
            SetWindowPos(affWnd, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
        BOOL getNoneOk = affWnd ? GetWindowDisplayAffinity(affWnd, &affNone) : FALSE;
        if (affWnd) DestroyWindow(affWnd);
        UnregisterClassW(awc.lpszClassName, awc.hInstance);

        if (!setExclOk || !getExclOk || affExcl != WDA_EXCLUDEFROMCAPTURE ||
            !setNoneOk || !getNoneOk || affNone != WDA_NONE) {
            std::ostringstream errStr;
            errStr << "DisplayAffinity transition failed: setExcl=" << setExclOk
                   << " getExcl=" << affExcl << " setNone=" << setNoneOk << " getNone=" << affNone;
            outReport = errStr.str();
            return false;
        }
        rep << "[PASS] DisplayAffinity WDA_EXCLUDE -> WDA_NONE transition verified\n";
    }

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
    EnhanceCBData cb = BuildCB(fp, false, 0.5f, W, H, 1.0f, 200.0f, false);
    D3D11_BUFFER_DESC cbd{ sizeof(EnhanceCBData), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
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

    if (outA != 255 || outR <= 60 || (outR - outB) <= ((60 - 30) + 10)) {
        outReport = "Shader output validation failed (expected vibrant color enrichment on warm dark pixel).";
        return false;
    }

    rep << "[PASS] GPU Compute + Pixel 256x256 pass: Dark Input RGB(60,45,30) -> Enhanced RGB("
        << outR << "," << outG << "," << outB << "), SceneStats(avgLuma=" << statsCopy.avgLuma
        << ", darkRatio=" << statsCopy.darkRatio << ")\n";

    // 4B. Skin Tone 100% Saturation GPU pass (warm golden-tan instead of muted/washed out)
    {
        std::vector<uint32_t> skinPixels(W * H, (0xFFu << 24) | (190u << 16) | (145u << 8) | 118u); // RGB(190,145,118)
        ctx->UpdateSubresource(srcTex.Get(), 0, nullptr, skinPixels.data(), W * 4, 0);

        FilterParams fpSkin{};
        fpSkin.enableDynamicVibrance = true;
        fpSkin.intensity = 100;
        fpSkin.saturationBoost = 100;
        EnhanceCBData cbSkin = BuildCB(fpSkin, false, 0.5f, W, H, 1.0f, 200.0f, false);
        D3D11_MAPPED_SUBRESOURCE mapCb{};
        if (SUCCEEDED(ctx->Map(cbuf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapCb))) {
            std::memcpy(mapCb.pData, &cbSkin, sizeof(cbSkin));
            ctx->Unmap(cbuf.Get(), 0);
        }

        ctx->CSSetShader(cs.Get(), nullptr, 0);
        ctx->CSSetConstantBuffers(0, 1, cbs);
        ctx->CSSetShaderResources(0, 1, csIn);
        ctx->CSSetUnorderedAccessViews(0, 1, csOut, nullptr);
        ctx->Dispatch(1, 1, 1);
        ctx->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
        ctx->CSSetShaderResources(0, 1, nullSRV);

        ctx->OMSetRenderTargets(1, rtvs, nullptr);
        ctx->RSSetViewports(1, &vp);
        ctx->VSSetShader(vs.Get(), nullptr, 0);
        ctx->PSSetShader(ps.Get(), nullptr, 0);
        ctx->PSSetConstantBuffers(0, 1, cbs);
        ctx->PSSetShaderResources(0, 2, psIn);
        ctx->PSSetSamplers(0, 1, samps);
        ctx->Draw(3, 0);
        ctx->PSSetShaderResources(0, 2, nullSRV);

        ctx->CopyResource(stagingTex.Get(), dstTex.Get());
        D3D11_MAPPED_SUBRESOURCE mapSkin{};
        if (FAILED(ctx->Map(stagingTex.Get(), 0, D3D11_MAP_READ, 0, &mapSkin))) {
            outReport = "Failed mapping Skin Tone self-test GPU readback.";
            return false;
        }
        const uint8_t* spx = static_cast<const uint8_t*>(mapSkin.pData);
        int sB = spx[0], sG = spx[1], sR = spx[2];
        ctx->Unmap(stagingTex.Get(), 0);

        if (sR <= 190 || sG <= 140 || (sR - sB) <= ((190 - 118) + 12)) {
            std::ostringstream errStr;
            errStr << "Skin Tone 100% validation failed: RGB(190,145,118) -> RGB("
                   << sR << "," << sG << "," << sB << ")";
            outReport = errStr.str();
            return false;
        }
        rep << "[PASS] Skin tone 100% pass: RGB(190,145,118) -> RGB("
            << sR << "," << sG << "," << sB << ")\n";
    }

    // 4C. True HDR10 scRGB (DXGI_FORMAT_R16G16B16A16_FLOAT) pass
    {
        std::vector<uint32_t> hlPixels(W * H, (0xFFu << 24) | (245u << 16) | (225u << 8) | 190u); // Highlight RGB(245,225,190)
        ctx->UpdateSubresource(srcTex.Get(), 0, nullptr, hlPixels.data(), W * 4, 0);

        D3D11_TEXTURE2D_DESC hdrDesc{};
        hdrDesc.Width = W; hdrDesc.Height = H; hdrDesc.MipLevels = 1; hdrDesc.ArraySize = 1;
        hdrDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        hdrDesc.SampleDesc.Count = 1;
        hdrDesc.Usage = D3D11_USAGE_DEFAULT;
        hdrDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> dstHdrTex, hdrStageTex;
        if (FAILED(dev->CreateTexture2D(&hdrDesc, nullptr, &dstHdrTex))) {
            outReport = "Failed creating True HDR R16G16B16A16_FLOAT render target.";
            return false;
        }
        hdrDesc.Usage = D3D11_USAGE_STAGING;
        hdrDesc.BindFlags = 0;
        hdrDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(dev->CreateTexture2D(&hdrDesc, nullptr, &hdrStageTex))) {
            outReport = "Failed creating True HDR staging texture.";
            return false;
        }
        ComPtr<ID3D11RenderTargetView> hdrRTV;
        if (FAILED(dev->CreateRenderTargetView(dstHdrTex.Get(), nullptr, &hdrRTV))) {
            outReport = "Failed creating True HDR RTV.";
            return false;
        }

        FilterParams fpHdr{};
        fpHdr.enableDynamicVibrance = false;
        fpHdr.enableDynamicHdr = true;
        fpHdr.dynamicHdr = 80;
        fpHdr.hdrPeakNits = 1000;
        fpHdr.hdrMidGray = 50;
        EnhanceCBData cbHdr = BuildCB(fpHdr, false, 0.5f, W, H, 1.0f, 200.0f, true);
        D3D11_MAPPED_SUBRESOURCE mapCb{};
        if (SUCCEEDED(ctx->Map(cbuf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapCb))) {
            std::memcpy(mapCb.pData, &cbHdr, sizeof(cbHdr));
            ctx->Unmap(cbuf.Get(), 0);
        }

        ctx->CSSetShader(cs.Get(), nullptr, 0);
        ctx->CSSetConstantBuffers(0, 1, cbs);
        ctx->CSSetShaderResources(0, 1, csIn);
        ctx->CSSetUnorderedAccessViews(0, 1, csOut, nullptr);
        ctx->Dispatch(1, 1, 1);
        ctx->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
        ctx->CSSetShaderResources(0, 1, nullSRV);

        ID3D11RenderTargetView* hdrRtvs[1] = { hdrRTV.Get() };
        ctx->OMSetRenderTargets(1, hdrRtvs, nullptr);
        ctx->RSSetViewports(1, &vp);
        ctx->VSSetShader(vs.Get(), nullptr, 0);
        ctx->PSSetShader(ps.Get(), nullptr, 0);
        ctx->PSSetConstantBuffers(0, 1, cbs);
        ctx->PSSetShaderResources(0, 2, psIn);
        ctx->PSSetSamplers(0, 1, samps);
        ctx->Draw(3, 0);
        ctx->PSSetShaderResources(0, 2, nullSRV);

        ctx->CopyResource(hdrStageTex.Get(), dstHdrTex.Get());
        D3D11_MAPPED_SUBRESOURCE mapHdr{};
        if (FAILED(ctx->Map(hdrStageTex.Get(), 0, D3D11_MAP_READ, 0, &mapHdr))) {
            outReport = "Failed mapping True HDR self-test GPU readback.";
            return false;
        }
        const uint16_t* hpx = static_cast<const uint16_t*>(mapHdr.pData); // RGBA16F
        float hR = HalfToFloat(hpx[0]);
        float hG = HalfToFloat(hpx[1]);
        float hB = HalfToFloat(hpx[2]);
        ctx->Unmap(hdrStageTex.Get(), 0);

        float outNits = (0.2126f * hR + 0.7152f * hG + 0.0722f * hB) * 80.0f;
        if (!(outNits > 320.0f && outNits <= 1050.0f)) {
            std::ostringstream errStr;
            errStr << "True HDR scRGB validation failed: Highlight RGB(245,225,190) -> "
                   << outNits << " nits (expected > 320 and <= 1050)";
            outReport = errStr.str();
            return false;
        }
        rep << "[PASS] True HDR scRGB (R16G16B16A16_FLOAT) pass: Highlight RGB(245,225,190) -> "
            << static_cast<int>(std::lround(outNits)) << " nits (Peak 1000 nits, SDR White 200 nits)\n";
    }

    // 4D. Scaling & Multi-Scale Optical Flow compilation + all 4 spatial upscalers
    ScalingFgShaders sfg{};
    if (!InitScalingFgShaders(dev.Get(), sfg, err)) {
        outReport = err;
        return false;
    }
    rep << "[PASS] Scaling & Optical Flow HLSL shaders compiled cleanly (10 entry points)\n";

    {
        constexpr UINT SW = 128, SH = 128;
        std::vector<uint32_t> upPx(SW * SH);
        for (UINT y = 0; y < SH; ++y) {
            for (UINT x = 0; x < SW; ++x) {
                bool white = (y < SH / 2u) ? (x < 96u) : (x >= 96u);
                uint32_t c = white ? 240u : 20u;
                upPx[y * SW + x] = (0xFFu << 24) | (c << 16) | (c << 8) | c;
            }
        }
        D3D11_TEXTURE2D_DESC sd128{};
        sd128.Width = SW; sd128.Height = SH; sd128.MipLevels = 1; sd128.ArraySize = 1;
        sd128.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd128.SampleDesc.Count = 1;
        sd128.Usage = D3D11_USAGE_DEFAULT;
        sd128.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init128{ upPx.data(), SW * 4, 0 };
        ComPtr<ID3D11Texture2D> src128Tex, tmp256Tex;
        ComPtr<ID3D11ShaderResourceView> src128SRV, tmp256SRV;
        ComPtr<ID3D11RenderTargetView> tmp256RTV;
        D3D11_TEXTURE2D_DESC td256 = td;
        td256.Usage = D3D11_USAGE_DEFAULT;
        td256.CPUAccessFlags = 0;
        td256.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        if (FAILED(dev->CreateTexture2D(&sd128, &init128, &src128Tex)) ||
            FAILED(dev->CreateShaderResourceView(src128Tex.Get(), nullptr, &src128SRV)) ||
            FAILED(dev->CreateTexture2D(&td256, nullptr, &tmp256Tex)) ||
            FAILED(dev->CreateShaderResourceView(tmp256Tex.Get(), nullptr, &tmp256SRV)) ||
            FAILED(dev->CreateRenderTargetView(tmp256Tex.Get(), nullptr, &tmp256RTV))) {
            outReport = "Failed creating 4D upscaler self-test textures.";
            return false;
        }

        const int modes[4] = { SCALE_FSR1, SCALE_NIS, SCALE_INTEGER, SCALE_ANIME4K };
        for (int mode : modes) {
            DrawSpatialScalePass(ctx.Get(), sfg, src128SRV.Get(), SW, SH, tmp256RTV.Get(), tmp256SRV.Get(), dstRTV.Get(), W, H, mode, 65);
            ctx->CopyResource(stagingTex.Get(), dstTex.Get());
            D3D11_MAPPED_SUBRESOURCE mUp{};
            if (FAILED(ctx->Map(stagingTex.Get(), 0, D3D11_MAP_READ, 0, &mUp))) {
                outReport = "Failed mapping upscaler self-test output.";
                return false;
            }
            const uint8_t* pTop = static_cast<const uint8_t*>(mUp.pData) + 64 * mUp.RowPitch + 64 * 4;
            const uint8_t* pBot = static_cast<const uint8_t*>(mUp.pData) + 192 * mUp.RowPitch + 64 * 4;
            int topR = pTop[2], botR = pBot[2];
            ctx->Unmap(stagingTex.Get(), 0);
            if (topR <= 210 || botR >= 40) {
                std::ostringstream errStr;
                errStr << "Spatial upscaler mode " << mode << " validation failed: topR=" << topR << ", botR=" << botR;
                outReport = errStr.str();
                return false;
            }
        }
        rep << "[PASS] Spatial Upscalers verified (128x128 -> 256x256: FSR 1.0, NIS, Integer, Anime4K)\n";
    }

    // 4E. Optical Flow Frame Generation Centroid Proof (x=100 -> x=116 => midpoint ~108)
    FlowPyramid pyr{};
    if (!EnsureFlowPyramid(dev.Get(), pyr, W, H, 50)) {
        outReport = "Failed allocating Optical Flow pyramid.";
        return false;
    }

    D3D11_TEXTURE2D_DESC ftd = td;
    ftd.Usage = D3D11_USAGE_DEFAULT;
    ftd.CPUAccessFlags = 0;
    ftd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> fgATex, fgBTex;
    ComPtr<ID3D11ShaderResourceView> fgASRV, fgBSRV;
    if (FAILED(dev->CreateTexture2D(&ftd, nullptr, &fgATex)) ||
        FAILED(dev->CreateTexture2D(&ftd, nullptr, &fgBTex)) ||
        FAILED(dev->CreateShaderResourceView(fgATex.Get(), nullptr, &fgASRV)) ||
        FAILED(dev->CreateShaderResourceView(fgBTex.Get(), nullptr, &fgBSRV))) {
        outReport = "Failed creating Frame Gen self-test input textures.";
        return false;
    }

    {
        auto makeSquareFrame = [&](int cx, int cy) {
            std::vector<uint32_t> pxData(W * H, (0xFFu << 24) | (30u << 16) | (30u << 8) | 30u);
            for (int y = cy - 24; y < cy + 24; ++y) {
                for (int x = cx - 24; x < cx + 24; ++x) {
                    if (x >= 0 && x < W && y >= 0 && y < H) {
                        pxData[y * W + x] = (0xFFu << 24) | (240u << 16) | (240u << 8) | 240u;
                    }
                }
            }
            return pxData;
        };
        auto frameA = makeSquareFrame(100, 128);
        auto frameB = makeSquareFrame(116, 128);
        ctx->UpdateSubresource(fgATex.Get(), 0, nullptr, frameA.data(), W * 4, 0);
        ctx->UpdateSubresource(fgBTex.Get(), 0, nullptr, frameB.data(), W * 4, 0);

        ExecuteOpticalFlow(ctx.Get(), sfg, pyr, fgASRV.Get(), fgBSRV.Get(), 1.0f, 1.0f);
        DrawFrameGenPass(ctx.Get(), sfg, pyr, fgASRV.Get(), fgBSRV.Get(), dstRTV.Get(), W, H, 0.5f, 1.0f);
        ctx->CopyResource(stagingTex.Get(), dstTex.Get());

        D3D11_MAPPED_SUBRESOURCE mFg{};
        if (FAILED(ctx->Map(stagingTex.Get(), 0, D3D11_MAP_READ, 0, &mFg))) {
            outReport = "Failed mapping Frame Gen self-test output.";
            return false;
        }
        double sumX = 0.0;
        int brightCount = 0;
        for (int y = 0; y < H; ++y) {
            const uint8_t* row = static_cast<const uint8_t*>(mFg.pData) + y * mFg.RowPitch;
            for (int x = 0; x < W; ++x) {
                if (row[x * 4 + 2] > 160) {
                    sumX += x;
                    ++brightCount;
                }
            }
        }
        ctx->Unmap(stagingTex.Get(), 0);

        double avgX = (brightCount > 0) ? (sumX / brightCount) : 0.0;
        if (brightCount < 500 || avgX < 104.0 || avgX > 112.0) {
            std::ostringstream errStr;
            errStr << "Optical Flow Frame Gen centroid check failed: avgX=" << avgX
                   << ", count=" << brightCount << " (expected avgX in 104..112)";
            outReport = errStr.str();
            return false;
        }
        rep << "[PASS] Optical Flow Frame Gen midpoint pass: x=100 -> x=116 synthesized at avgX="
            << avgX << " (brightPx=" << brightCount << ")\n";
    }

    // 4F. AFMF Scene-Cut Fallback Proof (extreme scene cut => fallbackFactor < 0.35)
    {
        std::vector<uint32_t> darkCut(W * H, (0xFFu << 24) | (20u << 16) | (20u << 8) | 20u);
        std::vector<uint32_t> brightCut(W * H);
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                bool stripe = ((x / 16) % 2) == 0;
                brightCut[y * W + x] = stripe
                    ? ((0xFFu << 24) | (250u << 16) | (250u << 8) | 250u)
                    : ((0xFFu << 24) | (250u << 8) | 250u);
            }
        }
        ctx->UpdateSubresource(fgATex.Get(), 0, nullptr, darkCut.data(), W * 4, 0);
        ctx->UpdateSubresource(fgBTex.Get(), 0, nullptr, brightCut.data(), W * 4, 0);

        ExecuteOpticalFlow(ctx.Get(), sfg, pyr, fgASRV.Get(), fgBSRV.Get(), 1.0f, 1.0f);

        D3D11_BUFFER_DESC fgsDesc{ sizeof(FgStats), D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, 0 };
        ComPtr<ID3D11Buffer> fgStage;
        if (FAILED(dev->CreateBuffer(&fgsDesc, nullptr, &fgStage))) {
            outReport = "Failed creating FgStats staging buffer.";
            return false;
        }
        ctx->CopyResource(fgStage.Get(), sfg.statsBuf.Get());
        D3D11_MAPPED_SUBRESOURCE mSt{};
        if (FAILED(ctx->Map(fgStage.Get(), 0, D3D11_MAP_READ, 0, &mSt))) {
            outReport = "Failed mapping FgStats staging buffer.";
            return false;
        }
        FgStats st = *static_cast<const FgStats*>(mSt.pData);
        ctx->Unmap(fgStage.Get(), 0);

        if (st.fallbackFactor >= 0.35f) {
            std::ostringstream errStr;
            errStr << "AFMF Scene-Cut Fallback check failed: fallbackFactor=" << st.fallbackFactor
                   << " (warpErr=" << st.avgWarpErr << ", expected < 0.35)";
            outReport = errStr.str();
            return false;
        }
        rep << "[PASS] AFMF Scene-Cut Fallback verified: fallbackFactor=" << st.fallbackFactor
            << " (warpErr=" << st.avgWarpErr << ")\n";
    }

    // 4G. Ambient Occlusion Crevice Depth + Flat / Step-Edge Safety Proof
    {
        std::vector<uint32_t> aoPixels(W * H);
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                uint32_t c = 75u;
                if (x < 128) {
                    c = (x >= 62 && x <= 65) ? 75u : 180u;
                }
                aoPixels[y * W + x] = (0xFFu << 24) | (c << 16) | (c << 8) | c;
            }
        }
        ctx->UpdateSubresource(srcTex.Get(), 0, nullptr, aoPixels.data(), W * 4, 0);

        FilterParams fpAo{};
        fpAo.enableDynamicVibrance = false;
        fpAo.enableAmbientOcclusion = true;
        fpAo.ambientOcclusion = 85;
        fpAo.aoRadius = 50;
        EnhanceCBData cbAo = BuildCB(fpAo, false, 0.5f, W, H, 1.0f, 200.0f, false);
        D3D11_MAPPED_SUBRESOURCE mapCb{};
        if (SUCCEEDED(ctx->Map(cbuf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapCb))) {
            std::memcpy(mapCb.pData, &cbAo, sizeof(cbAo));
            ctx->Unmap(cbuf.Get(), 0);
        }

        ctx->CSSetShader(cs.Get(), nullptr, 0);
        ctx->CSSetConstantBuffers(0, 1, cbs);
        ctx->CSSetShaderResources(0, 1, csIn);
        ctx->CSSetUnorderedAccessViews(0, 1, csOut, nullptr);
        ctx->Dispatch(1, 1, 1);
        ctx->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
        ctx->CSSetShaderResources(0, 1, nullSRV);

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
        D3D11_MAPPED_SUBRESOURCE mapAo{};
        if (FAILED(ctx->Map(stagingTex.Get(), 0, D3D11_MAP_READ, 0, &mapAo))) {
            outReport = "Failed mapping Ambient Occlusion self-test GPU readback.";
            return false;
        }
        const uint8_t* row = static_cast<const uint8_t*>(mapAo.pData) + 128 * mapAo.RowPitch;
        int crevR  = row[64 * 4 + 2];
        int stoneR = row[40 * 4 + 2];
        int edgeR  = row[130 * 4 + 2];
        int flatR  = row[192 * 4 + 2];
        ctx->Unmap(stagingTex.Get(), 0);

        if (crevR >= 64 || std::abs(flatR - 75) > 2 || std::abs(stoneR - 180) > 2 || edgeR < 65) {
            std::ostringstream errStr;
            errStr << "Ambient Occlusion validation failed: crevR=" << crevR
                   << ", flatR=" << flatR << ", stoneR=" << stoneR << ", edgeR=" << edgeR;
            outReport = errStr.str();
            return false;
        }
        rep << "[PASS] Ambient Occlusion pass: Crevice 75 -> " << crevR
            << ", Flat 75 -> " << flatR << ", Step-Edge 75 -> " << edgeR << "\n";
    }

    // 4H. Haze-Free Void Black Guard + Real Cave Wall (50/50 & 100/100) + Dark Lighting + Grass Proof
    {
        // 4H-1a: Pure void black RGB(4,4,4) at Vibrance 100/100 -> ZERO milky white fog
        std::vector<uint32_t> blackPixels(W * H, (0xFFu << 24) | (4u << 16) | (4u << 8) | 4u);
        ctx->UpdateSubresource(srcTex.Get(), 0, nullptr, blackPixels.data(), W * 4, 0);

        FilterParams fpBlack{};
        fpBlack.enableDynamicVibrance = true;
        fpBlack.intensity = 100;
        fpBlack.saturationBoost = 100;
        fpBlack.enableDarkLighting = false;
        EnhanceCBData cbBlack = BuildCB(fpBlack, false, 0.5f, W, H, 1.0f, 200.0f, false);
        D3D11_MAPPED_SUBRESOURCE mapCb{};
        if (SUCCEEDED(ctx->Map(cbuf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapCb))) {
            std::memcpy(mapCb.pData, &cbBlack, sizeof(cbBlack));
            ctx->Unmap(cbuf.Get(), 0);
        }

        ctx->CSSetShader(cs.Get(), nullptr, 0);
        ctx->CSSetConstantBuffers(0, 1, cbs);
        ctx->CSSetShaderResources(0, 1, csIn);
        ctx->CSSetUnorderedAccessViews(0, 1, csOut, nullptr);
        ctx->Dispatch(1, 1, 1);
        ctx->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
        ctx->CSSetShaderResources(0, 1, nullSRV);

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
        D3D11_MAPPED_SUBRESOURCE mapBlack{};
        if (FAILED(ctx->Map(stagingTex.Get(), 0, D3D11_MAP_READ, 0, &mapBlack))) {
            outReport = "Failed mapping Void Black self-test (4H-1a) GPU readback.";
            return false;
        }
        const uint8_t* bpx = static_cast<const uint8_t*>(mapBlack.pData);
        int bB = bpx[0], bG = bpx[1], bR = bpx[2];
        ctx->Unmap(stagingTex.Get(), 0);

        if (bR > 8 || bG > 8 || bB > 8) {
            std::ostringstream errStr;
            errStr << "Haze-Free Void Black 4H-1a failed: RGB(4,4,4) -> RGB("
                   << bR << "," << bG << "," << bB << ")";
            outReport = errStr.str();
            return false;
        }

        // 4H-1b: Real Cave Wall RGB(14,15,17) at Vibrance 50/50 and 100/100 (Dark Lighting OFF)
        std::vector<uint32_t> cavePixels(W * H, (0xFFu << 24) | (14u << 16) | (15u << 8) | 17u);
        ctx->UpdateSubresource(srcTex.Get(), 0, nullptr, cavePixels.data(), W * 4, 0);

        FilterParams fpCave50{};
        fpCave50.enableDynamicVibrance = true;
        fpCave50.intensity = 50;
        fpCave50.saturationBoost = 50;
        fpCave50.enableDarkLighting = false;
        EnhanceCBData cbCave50 = BuildCB(fpCave50, false, 0.5f, W, H, 1.0f, 200.0f, false);
        if (SUCCEEDED(ctx->Map(cbuf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapCb))) {
            std::memcpy(mapCb.pData, &cbCave50, sizeof(cbCave50));
            ctx->Unmap(cbuf.Get(), 0);
        }

        ctx->CSSetShader(cs.Get(), nullptr, 0);
        ctx->CSSetConstantBuffers(0, 1, cbs);
        ctx->CSSetShaderResources(0, 1, csIn);
        ctx->CSSetUnorderedAccessViews(0, 1, csOut, nullptr);
        ctx->Dispatch(1, 1, 1);
        ctx->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
        ctx->CSSetShaderResources(0, 1, nullSRV);

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
        D3D11_MAPPED_SUBRESOURCE mapCave50{};
        if (FAILED(ctx->Map(stagingTex.Get(), 0, D3D11_MAP_READ, 0, &mapCave50))) {
            outReport = "Failed mapping Cave Wall 50/50 self-test (4H-1b) GPU readback.";
            return false;
        }
        const uint8_t* c50px = static_cast<const uint8_t*>(mapCave50.pData);
        int c50B = c50px[0], c50G = c50px[1], c50R = c50px[2];
        ctx->Unmap(stagingTex.Get(), 0);

        if (c50G < 26 || c50G > 48 || std::abs(c50B - c50R) > 10) {
            std::ostringstream errStr;
            errStr << "Cave Wall 50/50 4H-1b failed: RGB(14,15,17) -> RGB("
                   << c50R << "," << c50G << "," << c50B << ")";
            outReport = errStr.str();
            return false;
        }

        if (SUCCEEDED(ctx->Map(cbuf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapCb))) {
            std::memcpy(mapCb.pData, &cbBlack, sizeof(cbBlack));
            ctx->Unmap(cbuf.Get(), 0);
        }
        ctx->CSSetShader(cs.Get(), nullptr, 0);
        ctx->CSSetConstantBuffers(0, 1, cbs);
        ctx->CSSetShaderResources(0, 1, csIn);
        ctx->CSSetUnorderedAccessViews(0, 1, csOut, nullptr);
        ctx->Dispatch(1, 1, 1);
        ctx->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
        ctx->CSSetShaderResources(0, 1, nullSRV);

        ctx->OMSetRenderTargets(1, rtvs, nullptr);
        ctx->RSSetViewports(1, &vp);
        ctx->VSSetShader(vs.Get(), nullptr, 0);
        ctx->PSSetShader(ps.Get(), nullptr, 0);
        ctx->PSSetConstantBuffers(0, 1, cbs);
        ctx->PSSetShaderResources(0, 2, psIn);
        ctx->PSSetSamplers(0, 1, samps);
        ctx->Draw(3, 0);
        ctx->PSSetShaderResources(0, 2, nullSRV);

        ctx->CopyResource(stagingTex.Get(), dstTex.Get());
        D3D11_MAPPED_SUBRESOURCE mapCave100{};
        if (FAILED(ctx->Map(stagingTex.Get(), 0, D3D11_MAP_READ, 0, &mapCave100))) {
            outReport = "Failed mapping Cave Wall 100/100 self-test (4H-1b) GPU readback.";
            return false;
        }
        const uint8_t* c100px = static_cast<const uint8_t*>(mapCave100.pData);
        int c100B = c100px[0], c100G = c100px[1], c100R = c100px[2];
        ctx->Unmap(stagingTex.Get(), 0);

        if (c100G < 38 || c100G > 64 || std::abs(c100B - c100R) > 12) {
            std::ostringstream errStr;
            errStr << "Cave Wall 100/100 4H-1b failed: RGB(14,15,17) -> RGB("
                   << c100R << "," << c100G << "," << c100B << ")";
            outReport = errStr.str();
            return false;
        }

        // 4H-2: Standalone Dark / Shadow Lighting 80% on real cave wall RGB(14,15,17)
        FilterParams fpDark{};
        fpDark.enableDynamicVibrance = false;
        fpDark.enableDarkLighting = true;
        fpDark.darkLighting = 80;
        EnhanceCBData cbDark = BuildCB(fpDark, false, 0.5f, W, H, 1.0f, 200.0f, false);
        if (SUCCEEDED(ctx->Map(cbuf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapCb))) {
            std::memcpy(mapCb.pData, &cbDark, sizeof(cbDark));
            ctx->Unmap(cbuf.Get(), 0);
        }

        ctx->CSSetShader(cs.Get(), nullptr, 0);
        ctx->CSSetConstantBuffers(0, 1, cbs);
        ctx->CSSetShaderResources(0, 1, csIn);
        ctx->CSSetUnorderedAccessViews(0, 1, csOut, nullptr);
        ctx->Dispatch(1, 1, 1);
        ctx->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
        ctx->CSSetShaderResources(0, 1, nullSRV);

        ctx->OMSetRenderTargets(1, rtvs, nullptr);
        ctx->RSSetViewports(1, &vp);
        ctx->VSSetShader(vs.Get(), nullptr, 0);
        ctx->PSSetShader(ps.Get(), nullptr, 0);
        ctx->PSSetConstantBuffers(0, 1, cbs);
        ctx->PSSetShaderResources(0, 2, psIn);
        ctx->PSSetSamplers(0, 1, samps);
        ctx->Draw(3, 0);
        ctx->PSSetShaderResources(0, 2, nullSRV);

        ctx->CopyResource(stagingTex.Get(), dstTex.Get());
        D3D11_MAPPED_SUBRESOURCE mapDark{};
        if (FAILED(ctx->Map(stagingTex.Get(), 0, D3D11_MAP_READ, 0, &mapDark))) {
            outReport = "Failed mapping Dark Lighting self-test (4H-2) GPU readback.";
            return false;
        }
        const uint8_t* dpx = static_cast<const uint8_t*>(mapDark.pData);
        int dB = dpx[0], dG = dpx[1], dR = dpx[2];
        ctx->Unmap(stagingTex.Get(), 0);

        if (dG < 45) {
            std::ostringstream errStr;
            errStr << "Cave Dark Lighting 4H-2 (80%) failed: RGB(14,15,17) -> RGB("
                   << dR << "," << dG << "," << dB << ")";
            outReport = errStr.str();
            return false;
        }

        // 4H-3: Midtone Vivid Grass RGB(85,110,75) -> crisp bright green pop at 100/100
        std::vector<uint32_t> grassPixels(W * H, (0xFFu << 24) | (85u << 16) | (110u << 8) | 75u);
        ctx->UpdateSubresource(srcTex.Get(), 0, nullptr, grassPixels.data(), W * 4, 0);

        if (SUCCEEDED(ctx->Map(cbuf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapCb))) {
            std::memcpy(mapCb.pData, &cbBlack, sizeof(cbBlack));
            ctx->Unmap(cbuf.Get(), 0);
        }
        ctx->CSSetShader(cs.Get(), nullptr, 0);
        ctx->CSSetConstantBuffers(0, 1, cbs);
        ctx->CSSetShaderResources(0, 1, csIn);
        ctx->CSSetUnorderedAccessViews(0, 1, csOut, nullptr);
        ctx->Dispatch(1, 1, 1);
        ctx->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
        ctx->CSSetShaderResources(0, 1, nullSRV);

        ctx->OMSetRenderTargets(1, rtvs, nullptr);
        ctx->RSSetViewports(1, &vp);
        ctx->VSSetShader(vs.Get(), nullptr, 0);
        ctx->PSSetShader(ps.Get(), nullptr, 0);
        ctx->PSSetConstantBuffers(0, 1, cbs);
        ctx->PSSetShaderResources(0, 2, psIn);
        ctx->PSSetSamplers(0, 1, samps);
        ctx->Draw(3, 0);
        ctx->PSSetShaderResources(0, 2, nullSRV);

        ctx->CopyResource(stagingTex.Get(), dstTex.Get());
        D3D11_MAPPED_SUBRESOURCE mapGrass{};
        if (FAILED(ctx->Map(stagingTex.Get(), 0, D3D11_MAP_READ, 0, &mapGrass))) {
            outReport = "Failed mapping Vivid Grass self-test (4H-3) GPU readback.";
            return false;
        }
        const uint8_t* gpx = static_cast<const uint8_t*>(mapGrass.pData);
        int gB = gpx[0], gG = gpx[1], gR = gpx[2];
        ctx->Unmap(stagingTex.Get(), 0);

        if (gG <= 115 || (gG - gR) <= ((110 - 85) + 20)) {
            std::ostringstream errStr;
            errStr << "Haze-Free Vivid Grass 4H-3 failed: RGB(85,110,75) -> RGB("
                   << gR << "," << gG << "," << gB << ")";
            outReport = errStr.str();
            return false;
        }

        rep << "[PASS] Haze-Free Cave & Grass pass: Void RGB(4,4,4)->RGB("
            << bR << "," << bG << "," << bB << "), Cave 50/50 RGB(14,15,17)->RGB("
            << c50R << "," << c50G << "," << c50B << "), Cave 100/100->RGB("
            << c100R << "," << c100G << "," << c100B << "), DarkLight 80%->RGB("
            << dR << "," << dG << "," << dB << "), Grass->RGB("
            << gR << "," << gG << "," << gB << ")\n";
    }

    // 4I. Brightened Gamma + Brightness/Contrast -> Oklab Vibrance Re-Optimization Proof
    {
        std::vector<uint32_t> forestPixels(W * H, (0xFFu << 24) | (55u << 16) | (78u << 8) | 48u); // RGB(55,78,48)
        ctx->UpdateSubresource(srcTex.Get(), 0, nullptr, forestPixels.data(), W * 4, 0);

        FilterParams fpBright{};
        fpBright.enableDynamicVibrance = true;
        fpBright.intensity = 50;
        fpBright.saturationBoost = 50;
        fpBright.enableGammaExposure = true;
        fpBright.gamma = 70;
        fpBright.exposure = 50;
        fpBright.enableBrightnessContrast = true;
        fpBright.brightness = 58;
        fpBright.contrast = 55;
        EnhanceCBData cbBright = BuildCB(fpBright, false, 0.5f, W, H, 1.0f, 200.0f, false);
        D3D11_MAPPED_SUBRESOURCE mapCb{};
        if (SUCCEEDED(ctx->Map(cbuf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapCb))) {
            std::memcpy(mapCb.pData, &cbBright, sizeof(cbBright));
            ctx->Unmap(cbuf.Get(), 0);
        }

        ctx->CSSetShader(cs.Get(), nullptr, 0);
        ctx->CSSetConstantBuffers(0, 1, cbs);
        ctx->CSSetShaderResources(0, 1, csIn);
        ctx->CSSetUnorderedAccessViews(0, 1, csOut, nullptr);
        ctx->Dispatch(1, 1, 1);
        ctx->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
        ctx->CSSetShaderResources(0, 1, nullSRV);

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
        D3D11_MAPPED_SUBRESOURCE mapBright{};
        if (FAILED(ctx->Map(stagingTex.Get(), 0, D3D11_MAP_READ, 0, &mapBright))) {
            outReport = "Failed mapping Brightened Tone -> Oklab Vibrance self-test (4I) readback.";
            return false;
        }
        const uint8_t* ppx = static_cast<const uint8_t*>(mapBright.pData);
        int pB = ppx[0], pG = ppx[1], pR = ppx[2];
        ctx->Unmap(stagingTex.Get(), 0);

        if (pG < 115 || (pG - pR) < 30 || (pG - pB) < 35) {
            std::ostringstream errStr;
            errStr << "Brightened Tone -> Oklab Vibrance 4I failed: RGB(55,78,48) -> RGB("
                   << pR << "," << pG << "," << pB << ")";
            outReport = errStr.str();
            return false;
        }
        rep << "[PASS] Brightened Tone -> Oklab Vibrance Re-Optimization pass: RGB(55,78,48) -> RGB("
            << pR << "," << pG << "," << pB << ")\n";
    }

    // 4J. Warm Candlelit Red-Orange High-Chroma Roll-Off & Gamut Headroom Proof
    {
        std::vector<uint32_t> roomPixels(W * H, (0xFFu << 24) | (22u << 16) | (12u << 8) | 8u);
        for (int y = 0; y < 128; ++y) {
            for (int x = 0; x < 128; ++x) {
                roomPixels[y * W + x] = (0xFFu << 24) | (165u << 16) | (55u << 8) | 20u;
            }
        }
        ctx->UpdateSubresource(srcTex.Get(), 0, nullptr, roomPixels.data(), W * 4, 0);

        FilterParams fpQ50{};
        fpQ50.enableDynamicVibrance = true;
        fpQ50.intensity = 50;
        fpQ50.saturationBoost = 50;
        EnhanceCBData cbQ50 = BuildCB(fpQ50, false, 0.5f, W, H, 1.0f, 200.0f, false);
        D3D11_MAPPED_SUBRESOURCE mapCb{};
        if (SUCCEEDED(ctx->Map(cbuf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapCb))) {
            std::memcpy(mapCb.pData, &cbQ50, sizeof(cbQ50));
            ctx->Unmap(cbuf.Get(), 0);
        }

        ctx->CSSetShader(cs.Get(), nullptr, 0);
        ctx->CSSetConstantBuffers(0, 1, cbs);
        ctx->CSSetShaderResources(0, 1, csIn);
        ctx->CSSetUnorderedAccessViews(0, 1, csOut, nullptr);
        ctx->Dispatch(1, 1, 1);
        ctx->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
        ctx->CSSetShaderResources(0, 1, nullSRV);

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
        D3D11_MAPPED_SUBRESOURCE mapQ50{};
        if (FAILED(ctx->Map(stagingTex.Get(), 0, D3D11_MAP_READ, 0, &mapQ50))) {
            outReport = "Failed mapping Warm Candlelit Quilt 50/50 self-test (4J) readback.";
            return false;
        }
        const uint8_t* q50Row = static_cast<const uint8_t*>(mapQ50.pData) + 32 * mapQ50.RowPitch;
        int q50B = q50Row[32 * 4 + 0], q50G = q50Row[32 * 4 + 1], q50R = q50Row[32 * 4 + 2];
        ctx->Unmap(stagingTex.Get(), 0);

        FilterParams fpQ100 = fpQ50;
        fpQ100.intensity = 100;
        fpQ100.saturationBoost = 100;
        EnhanceCBData cbQ100 = BuildCB(fpQ100, false, 0.5f, W, H, 1.0f, 200.0f, false);
        if (SUCCEEDED(ctx->Map(cbuf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapCb))) {
            std::memcpy(mapCb.pData, &cbQ100, sizeof(cbQ100));
            ctx->Unmap(cbuf.Get(), 0);
        }

        ctx->CSSetShader(cs.Get(), nullptr, 0);
        ctx->CSSetConstantBuffers(0, 1, cbs);
        ctx->CSSetShaderResources(0, 1, csIn);
        ctx->CSSetUnorderedAccessViews(0, 1, csOut, nullptr);
        ctx->Dispatch(1, 1, 1);
        ctx->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
        ctx->CSSetShaderResources(0, 1, nullSRV);

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
        D3D11_MAPPED_SUBRESOURCE mapQ100{};
        if (FAILED(ctx->Map(stagingTex.Get(), 0, D3D11_MAP_READ, 0, &mapQ100))) {
            outReport = "Failed mapping Warm Candlelit Quilt 100/100 self-test (4J) readback.";
            return false;
        }
        const uint8_t* q100Row = static_cast<const uint8_t*>(mapQ100.pData) + 32 * mapQ100.RowPitch;
        int q100B = q100Row[32 * 4 + 0], q100G = q100Row[32 * 4 + 1], q100R = q100Row[32 * 4 + 2];
        ctx->Unmap(stagingTex.Get(), 0);

        if (q50B < 20 || q50G < 60 || q50R < 170 || q50R > 205 ||
            q100B < 15 || q100G < 65 || q100R < 180 || q100R > 220) {
            std::ostringstream errStr;
            errStr << "Warm Candlelit Red-Orange Guard 4J failed: Quilt RGB(165,55,20) -> 50/50 RGB("
                   << q50R << "," << q50G << "," << q50B << ") / 100/100 RGB("
                   << q100R << "," << q100G << "," << q100B << ")";
            outReport = errStr.str();
            return false;
        }
        rep << "[PASS] Warm Candlelit Red-Orange Guard pass: Quilt RGB(165,55,20) -> 50/50 RGB("
            << q50R << "," << q50G << "," << q50B << ") / 100/100 RGB("
            << q100R << "," << q100G << "," << q100B << ")\n";
    }

    // 4K. Vivid Red UI Text Guard Proof (e.g. Cyberpunk 2077 red dialog text stays rich red, not washed-out pink)
    {
        std::vector<uint32_t> cpPixels(W * H, (0xFFu << 24) | (65u << 16) | (58u << 8) | 45u);
        for (int y = 0; y < 128; ++y) {
            for (int x = 0; x < 128; ++x) {
                cpPixels[y * W + x] = (0xFFu << 24) | (235u << 16) | (85u << 8) | 80u;
            }
        }
        ctx->UpdateSubresource(srcTex.Get(), 0, nullptr, cpPixels.data(), W * 4, 0);

        FilterParams fpRed{};
        fpRed.enableDynamicVibrance = true;
        fpRed.intensity = 50;
        fpRed.saturationBoost = 50;
        EnhanceCBData cbRed = BuildCB(fpRed, false, 0.5f, W, H, 0.5f, 200.0f, false);
        D3D11_MAPPED_SUBRESOURCE mapCb{};
        if (SUCCEEDED(ctx->Map(cbuf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapCb))) {
            std::memcpy(mapCb.pData, &cbRed, sizeof(cbRed));
            ctx->Unmap(cbuf.Get(), 0);
        }

        ctx->CSSetShader(cs.Get(), nullptr, 0);
        ctx->CSSetConstantBuffers(0, 1, cbs);
        ctx->CSSetShaderResources(0, 1, csIn);
        ctx->CSSetUnorderedAccessViews(0, 1, csOut, nullptr);
        ctx->Dispatch(1, 1, 1);
        ctx->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
        ctx->CSSetShaderResources(0, 1, nullSRV);

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
        D3D11_MAPPED_SUBRESOURCE mapRed{};
        if (FAILED(ctx->Map(stagingTex.Get(), 0, D3D11_MAP_READ, 0, &mapRed))) {
            outReport = "Failed mapping Vivid Red UI Text self-test (4K) readback.";
            return false;
        }
        const uint8_t* rRow = static_cast<const uint8_t*>(mapRed.pData) + 32 * mapRed.RowPitch;
        int b50 = rRow[32 * 4 + 0], g50 = rRow[32 * 4 + 1], r50 = rRow[32 * 4 + 2];
        ctx->Unmap(stagingTex.Get(), 0);

        if (r50 < 245 || g50 < 30 || g50 > 72 || b50 < 30 || b50 > 72 || std::abs(g50 - b50) > 15) {
            std::ostringstream errStr;
            errStr << "Vivid Red UI Text Guard 4K failed: Red UI RGB(235,85,80) -> 50/50 RGB("
                   << r50 << "," << g50 << "," << b50 << ")";
            outReport = errStr.str();
            return false;
        }
        rep << "[PASS] Vivid Red UI Text Guard pass: Red UI RGB(235,85,80) -> 50/50 RGB("
            << r50 << "," << g50 << "," << b50 << ")\n";
    }

    // 4L. Edge-Preserving Stochastic Deband Proof
    {
        std::vector<uint32_t> dbPixels(W * H);
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                if (y >= 192) {
                    dbPixels[y * W + x] = (0xFFu << 24) | (220u << 16) | (220u << 8) | 220u;
                } else if (x < 128) {
                    dbPixels[y * W + x] = (0xFFu << 24) | (30u << 16) | (45u << 8) | 70u;
                } else {
                    dbPixels[y * W + x] = (0xFFu << 24) | (34u << 16) | (49u << 8) | 74u;
                }
            }
        }
        ctx->UpdateSubresource(srcTex.Get(), 0, nullptr, dbPixels.data(), W * 4, 0);

        FilterParams fpDb{};
        fpDb.enableDynamicVibrance = false;
        fpDb.enableDeband = true;
        fpDb.debandStrength = 80;
        EnhanceCBData cbDb = BuildCB(fpDb, false, 0.5f, W, H, 0.5f, 200.0f, false);
        D3D11_MAPPED_SUBRESOURCE mapCb{};
        if (SUCCEEDED(ctx->Map(cbuf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapCb))) {
            std::memcpy(mapCb.pData, &cbDb, sizeof(cbDb));
            ctx->Unmap(cbuf.Get(), 0);
        }

        ctx->CSSetShader(cs.Get(), nullptr, 0);
        ctx->CSSetConstantBuffers(0, 1, cbs);
        ctx->CSSetShaderResources(0, 1, csIn);
        ctx->CSSetUnorderedAccessViews(0, 1, csOut, nullptr);
        ctx->Dispatch(1, 1, 1);
        ctx->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
        ctx->CSSetShaderResources(0, 1, nullSRV);

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
        D3D11_MAPPED_SUBRESOURCE mapDb{};
        if (FAILED(ctx->Map(stagingTex.Get(), 0, D3D11_MAP_READ, 0, &mapDb))) {
            outReport = "Failed mapping Deband self-test (4L) readback.";
            return false;
        }
        int seamBlended = 0;
        int sampleB = 0;
        for (int y = 20; y < 120; ++y) {
            const uint8_t* row = static_cast<const uint8_t*>(mapDb.pData) + y * mapDb.RowPitch;
            int b127 = row[127 * 4 + 0];
            if (b127 > 70 && b127 < 74) {
                ++seamBlended;
                sampleB = b127;
            }
        }
        const uint8_t* edgeRow = static_cast<const uint8_t*>(mapDb.pData) + 196 * mapDb.RowPitch;
        int edgeR = edgeRow[64 * 4 + 2];
        ctx->Unmap(stagingTex.Get(), 0);

        if (seamBlended == 0 || edgeR < 218) {
            std::ostringstream errStr;
            errStr << "Deband 4L failed: seamBlended=" << seamBlended << " (sampleB=" << sampleB << "), edgeR=" << edgeR;
            outReport = errStr.str();
            return false;
        }
        rep << "[PASS] Deband 8-bit gradient pass: seamBlended=" << seamBlended
            << "/100 px (B=70..74 -> " << sampleB << "), hardEdge=" << edgeR << "\n";
    }

    // 4M. Cinematic Bloom Soft-Knee Optical Glow Proof
    {
        std::vector<uint32_t> blPixels(W * H, (0xFFu << 24) | (18u << 16) | (18u << 8) | 22u);
        for (int y = 100; y <= 156; ++y) {
            for (int x = 120; x <= 136; ++x) {
                blPixels[y * W + x] = (0xFFu << 24) | (250u << 16) | (235u << 8) | 190u;
            }
        }
        ctx->UpdateSubresource(srcTex.Get(), 0, nullptr, blPixels.data(), W * 4, 0);

        FilterParams fpBl{};
        fpBl.enableDynamicVibrance = false;
        fpBl.enableBloom = true;
        fpBl.bloomIntensity = 70;
        fpBl.bloomThreshold = 50;
        EnhanceCBData cbBl = BuildCB(fpBl, false, 0.5f, W, H, 0.5f, 200.0f, false);
        D3D11_MAPPED_SUBRESOURCE mapCb{};
        if (SUCCEEDED(ctx->Map(cbuf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapCb))) {
            std::memcpy(mapCb.pData, &cbBl, sizeof(cbBl));
            ctx->Unmap(cbuf.Get(), 0);
        }

        ctx->CSSetShader(cs.Get(), nullptr, 0);
        ctx->CSSetConstantBuffers(0, 1, cbs);
        ctx->CSSetShaderResources(0, 1, csIn);
        ctx->CSSetUnorderedAccessViews(0, 1, csOut, nullptr);
        ctx->Dispatch(1, 1, 1);
        ctx->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
        ctx->CSSetShaderResources(0, 1, nullSRV);

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
        D3D11_MAPPED_SUBRESOURCE mapBl{};
        if (FAILED(ctx->Map(stagingTex.Get(), 0, D3D11_MAP_READ, 0, &mapBl))) {
            outReport = "Failed mapping Bloom self-test (4M) readback.";
            return false;
        }
        const uint8_t* hRow = static_cast<const uint8_t*>(mapBl.pData) + 128 * mapBl.RowPitch;
        int haloR = hRow[115 * 4 + 2], haloG = hRow[115 * 4 + 1], haloB = hRow[115 * 4 + 0];
        int bgR   = hRow[20 * 4 + 2];
        ctx->Unmap(stagingTex.Get(), 0);

        if (haloR < 40 || bgR > 22) {
            std::ostringstream errStr;
            errStr << "Cinematic Bloom 4M failed: halo(115,128)=RGB(" << haloR << "," << haloG << "," << haloB
                   << "), bgR=" << bgR;
            outReport = errStr.str();
            return false;
        }
        rep << "[PASS] Cinematic Bloom halo pass: darkRGB(18,18,22)->halo RGB("
            << haloR << "," << haloG << "," << haloB << "), farBgR=" << bgR << "\n";
    }

    // 4N. Filmic Grading Proof (ACES, AgX, Technicolor)
    {
        auto runFilmic = [&](uint32_t rIn, uint32_t gIn, uint32_t bIn, int mode, int& rOut, int& gOut, int& bOut) -> bool {
            std::vector<uint32_t> px(W * H, (0xFFu << 24) | (rIn << 16) | (gIn << 8) | bIn);
            ctx->UpdateSubresource(srcTex.Get(), 0, nullptr, px.data(), W * 4, 0);

            FilterParams fpF{};
            fpF.enableDynamicVibrance = false;
            fpF.enableFilmic = true;
            fpF.filmicMode = mode;
            fpF.filmicStrength = 100;
            EnhanceCBData cbF = BuildCB(fpF, false, 0.5f, W, H, 0.5f, 200.0f, false);
            D3D11_MAPPED_SUBRESOURCE mapCb{};
            if (SUCCEEDED(ctx->Map(cbuf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapCb))) {
                std::memcpy(mapCb.pData, &cbF, sizeof(cbF));
                ctx->Unmap(cbuf.Get(), 0);
            }

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
            D3D11_MAPPED_SUBRESOURCE mapF{};
            if (FAILED(ctx->Map(stagingTex.Get(), 0, D3D11_MAP_READ, 0, &mapF))) {
                return false;
            }
            const uint8_t* p = static_cast<const uint8_t*>(mapF.pData);
            bOut = p[0]; gOut = p[1]; rOut = p[2];
            ctx->Unmap(stagingTex.Get(), 0);
            return true;
        };

        int ar, ag, ab, xr, xg, xb, tr, tg, tb;
        if (!runFilmic(85, 110, 75, 0, ar, ag, ab) ||
            !runFilmic(250, 220, 20, 1, xr, xg, xb) ||
            !runFilmic(85, 110, 75, 2, tr, tg, tb)) {
            outReport = "Failed mapping Filmic Grading self-test (4N) GPU readback.";
            return false;
        }
        if (ab >= 68 || xb < 40 || (tg - tr) < 30) {
            std::ostringstream errStr;
            errStr << "Filmic Grading 4N failed: ACES(85,110,75)->RGB(" << ar << "," << ag << "," << ab
                   << "), AgX(250,220,20)->RGB(" << xr << "," << xg << "," << xb
                   << "), Tech(85,110,75)->RGB(" << tr << "," << tg << "," << tb << ")";
            outReport = errStr.str();
            return false;
        }
        rep << "[PASS] Filmic Grading pass: ACES Grass->RGB(" << ar << "," << ag << "," << ab
            << "), AgX NeonYellow->RGB(" << xr << "," << xg << "," << xb
            << "), Tech Grass->RGB(" << tr << "," << tg << "," << tb << ")\n";
    }

    // 4O. Built-in WIC PNG Filtered Screenshot Encoder Proof
    {
        constexpr UINT pw = 32, ph = 32;
        std::vector<uint8_t> bgra(pw * ph * 4u, 0);
        for (UINT y = 0; y < ph; ++y) {
            for (UINT x = 0; x < pw; ++x) {
                size_t idx = (static_cast<size_t>(y) * pw + x) * 4u;
                bgra[idx + 0] = static_cast<uint8_t>(x * 8u);
                bgra[idx + 1] = static_cast<uint8_t>(y * 8u);
                bgra[idx + 2] = 220u;
                bgra[idx + 3] = 0u;
            }
        }
        std::filesystem::path pngPath = std::filesystem::temp_directory_path() / L"adv_selftest_png.png";
        std::error_code ec;
        std::filesystem::remove(pngPath, ec);
        std::wstring pngErr;
        bool saved = SaveBgraPixelsToPng(bgra.data(), pw, ph, pw * 4u, pngPath.wstring(), pngErr);
        bool validSig = false;
        uintmax_t pngSize = 0;
        if (saved && std::filesystem::exists(pngPath, ec)) {
            pngSize = std::filesystem::file_size(pngPath, ec);
            std::ifstream pf(pngPath, std::ios::binary);
            uint8_t hdr[8] = {};
            static const uint8_t kPngSig[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
            if (pf && pf.read(reinterpret_cast<char*>(hdr), 8) && std::memcmp(hdr, kPngSig, 8) == 0) {
                validSig = true;
            }
        }
        std::filesystem::remove(pngPath, ec);
        if (!saved || !validSig || pngSize <= 64) {
            outReport = "Built-in WIC PNG encoder failed: " + WideToUtf8(pngErr);
            return false;
        }
        rep << "[PASS] Built-in WIC PNG filtered screenshot encoder verified (" << pngSize << " bytes)\n";
    }

    // 4P. Cool Cyan-Blue Sky / Horizon Guard Proof (misty twilight sky keeps Red intact without electric-cyan clip)
    {
        auto runHorizon = [&](int intensity, int saturation, int& rOut, int& gOut, int& bOut) -> bool {
            std::vector<uint32_t> px(W * H, (0xFFu << 24) | (42u << 16) | (90u << 8) | 124u); // RGB(42,90,124)
            ctx->UpdateSubresource(srcTex.Get(), 0, nullptr, px.data(), W * 4, 0);

            FilterParams fpSky{};
            fpSky.enableDynamicVibrance = true;
            fpSky.intensity = intensity;
            fpSky.saturationBoost = saturation;
            EnhanceCBData cbSky = BuildCB(fpSky, false, 0.5f, W, H, 1.0f, 200.0f, false);
            D3D11_MAPPED_SUBRESOURCE mapCb{};
            if (SUCCEEDED(ctx->Map(cbuf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapCb))) {
                std::memcpy(mapCb.pData, &cbSky, sizeof(cbSky));
                ctx->Unmap(cbuf.Get(), 0);
            }

            ctx->CSSetShader(cs.Get(), nullptr, 0);
            ctx->CSSetConstantBuffers(0, 1, cbs);
            ctx->CSSetShaderResources(0, 1, csIn);
            ctx->CSSetUnorderedAccessViews(0, 1, csOut, nullptr);
            ctx->Dispatch(1, 1, 1);
            ctx->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
            ctx->CSSetShaderResources(0, 1, nullSRV);

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
            D3D11_MAPPED_SUBRESOURCE mapSky{};
            if (FAILED(ctx->Map(stagingTex.Get(), 0, D3D11_MAP_READ, 0, &mapSky))) {
                return false;
            }
            const uint8_t* p = static_cast<const uint8_t*>(mapSky.pData);
            bOut = p[0]; gOut = p[1]; rOut = p[2];
            ctx->Unmap(stagingTex.Get(), 0);
            return true;
        };

        int r50 = 0, g50 = 0, b50 = 0, r100 = 0, g100 = 0, b100 = 0;
        if (!runHorizon(50, 50, r50, g50, b50) || !runHorizon(100, 100, r100, g100, b100)) {
            outReport = "Failed mapping Cool Cyan-Blue Sky self-test (4P) GPU readback.";
            return false;
        }
        if (r50 < 24 || b50 < 138 || (b50 - r50) <= (124 - 42) ||
            r100 < 20 || b100 < 148 || (b100 - r100) <= (124 - 42)) {
            std::ostringstream errStr;
            errStr << "Cool Cyan-Blue Sky 4P failed: Horizon(42,90,124) -> 50/50 RGB("
                   << r50 << "," << g50 << "," << b50 << ") / 100/100 RGB("
                   << r100 << "," << g100 << "," << b100 << ")";
            outReport = errStr.str();
            return false;
        }
        rep << "[PASS] Cool Cyan-Blue Sky Guard pass: Horizon RGB(42,90,124) -> 50/50 RGB("
            << r50 << "," << g50 << "," << b50 << ") / 100/100 RGB("
            << r100 << "," << g100 << "," << b100 << ")\n";
    }

    // 4Q. Bright Daylight Scene Balance Proof (Toussaint Sky + Sunlit Gold Dirt)
    {
        std::vector<uint32_t> dayPixels(W * H);
        for (int y = 0; y < H; ++y) {
            uint32_t col = (y < 128)
                ? ((0xFFu << 24) | (72u << 16) | (170u << 8) | 218u)
                : ((0xFFu << 24) | (168u << 16) | (136u << 8) | 62u);
            for (int x = 0; x < W; ++x) {
                dayPixels[y * W + x] = col;
            }
        }
        ctx->UpdateSubresource(srcTex.Get(), 0, nullptr, dayPixels.data(), W * 4, 0);

        FilterParams fpDay{};
        fpDay.enableDynamicVibrance = true;
        fpDay.intensity = 50;
        fpDay.saturationBoost = 50;
        EnhanceCBData cbDay = BuildCB(fpDay, false, 0.5f, W, H, 1.0f, 200.0f, false);
        D3D11_MAPPED_SUBRESOURCE mapCb{};
        if (SUCCEEDED(ctx->Map(cbuf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapCb))) {
            std::memcpy(mapCb.pData, &cbDay, sizeof(cbDay));
            ctx->Unmap(cbuf.Get(), 0);
        }

        ctx->CSSetShader(cs.Get(), nullptr, 0);
        ctx->CSSetConstantBuffers(0, 1, cbs);
        ctx->CSSetShaderResources(0, 1, csIn);
        ctx->CSSetUnorderedAccessViews(0, 1, csOut, nullptr);
        ctx->Dispatch(1, 1, 1);
        ctx->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
        ctx->CSSetShaderResources(0, 1, nullSRV);

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
        D3D11_MAPPED_SUBRESOURCE mapDay{};
        if (FAILED(ctx->Map(stagingTex.Get(), 0, D3D11_MAP_READ, 0, &mapDay))) {
            outReport = "Failed mapping Bright Daylight Balance self-test (4Q) GPU readback.";
            return false;
        }
        const uint8_t* skyRow = static_cast<const uint8_t*>(mapDay.pData) + 32 * mapDay.RowPitch;
        int skyB = skyRow[64 * 4 + 0], skyG = skyRow[64 * 4 + 1], skyR = skyRow[64 * 4 + 2];
        const uint8_t* dirtRow = static_cast<const uint8_t*>(mapDay.pData) + 192 * mapDay.RowPitch;
        int dirtB = dirtRow[64 * 4 + 0], dirtG = dirtRow[64 * 4 + 1], dirtR = dirtRow[64 * 4 + 2];
        ctx->Unmap(stagingTex.Get(), 0);

        if (skyR < 60 || skyB < 220 || dirtB < 20 || dirtR < 180) {
            std::ostringstream errStr;
            errStr << "Bright Daylight Balance 4Q failed: Sky->RGB(" << skyR << "," << skyG << "," << skyB
                   << ") | Dirt->RGB(" << dirtR << "," << dirtG << "," << dirtB << ")";
            outReport = errStr.str();
            return false;
        }
        rep << "[PASS] Bright Daylight Balance pass: Sky RGB(72,170,218)->RGB(" << skyR << "," << skyG << "," << skyB
            << ") | Dirt RGB(168,136,62)->RGB(" << dirtR << "," << dirtG << "," << dirtB << ")\n";
    }

    outReport = rep.str();
    return true;
}
