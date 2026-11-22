#pragma once

#include "types.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

inline constexpr const char* kDynamicVibranceHLSL = R"HLSL(
cbuffer EnhanceCB : register(b0) {
    float uIntensity;
    float uSaturationBoost;
    float uDarkLighting;
    float uClarity;

    float uDynamicHdr;
    float uBrightness;
    float uContrast;
    float uSplitPos;

    uint  uFlags;
    float uWidth;
    float uHeight;
    float uSmoothAlpha;

    float uSharpen;
    float uTemperature;
    float uTint;
    float uExposure;

    float uGamma;
    float uNightMode;
    float uBlackAndWhite;
    float uVignette;

    float uColorblindStrength;
    uint  uColorblindType;
    uint  uFlags2;
    float uPad0;
};

struct SceneStats {
    float avgLuma;
    float darkRatio;
    float avgChroma;
    float highLuma;
};

Texture2D<float4> srcTex : register(t0);
StructuredBuffer<SceneStats> statsIn : register(t1);
RWStructuredBuffer<SceneStats> statsOut : register(u0);
SamplerState linearSamp : register(s0);

static const uint FLAG_SKIN_PROTECTION    = 1u << 0;
static const uint FLAG_ADAPTIVE_SCENE     = 1u << 1;
static const uint FLAG_ENABLE_DARK_LIGHT  = 1u << 2;
static const uint FLAG_ENABLE_CLARITY     = 1u << 3;
static const uint FLAG_ENABLE_DYNAMIC_HDR = 1u << 4;
static const uint FLAG_ENABLE_BC          = 1u << 5;
static const uint FLAG_SPLIT_PREVIEW      = 1u << 6;
static const uint FLAG_ENABLE_VIBRANCE    = 1u << 7;

static const uint FLAG2_ENABLE_SHARPEN    = 1u << 0;
static const uint FLAG2_ENABLE_COLOR_TEMP = 1u << 1;
static const uint FLAG2_ENABLE_GAMMA_EXP  = 1u << 2;
static const uint FLAG2_ENABLE_NIGHT_MODE = 1u << 3;
static const uint FLAG2_ENABLE_BW         = 1u << 4;
static const uint FLAG2_ENABLE_VIGNETTE   = 1u << 5;
static const uint FLAG2_ENABLE_COLORBLIND = 1u << 6;

float3 SRGBToLinear(float3 c) {
    c = max(c, 0.0f);
    float3 low = c / 12.92f;
    float3 high = pow(max((c + 0.055f) / 1.055f, 0.0f), 2.4f);
    return (c <= 0.04045f) ? low : high;
}

float3 LinearToSRGB(float3 c) {
    c = saturate(c);
    float3 low = c * 12.92f;
    float3 high = 1.055f * pow(max(c, 0.0f), 1.0f / 2.4f) - 0.055f;
    return (c <= 0.0031308f) ? low : high;
}

float CalcLuma(float3 linRgb) {
    return dot(linRgb, float3(0.2126f, 0.7152f, 0.0722f));
}

float FastPerceptualL(float3 srgb) {
    return pow(max(CalcLuma(SRGBToLinear(srgb)), 0.0f), 0.3333333f);
}

float BilateralMean4(float c, float n0, float n1, float n2, float n3, float invSigma2) {
    float d0 = n0 - c; float w0 = exp(-d0 * d0 * invSigma2);
    float d1 = n1 - c; float w1 = exp(-d1 * d1 * invSigma2);
    float d2 = n2 - c; float w2 = exp(-d2 * d2 * invSigma2);
    float d3 = n3 - c; float w3 = exp(-d3 * d3 * invSigma2);
    return (c + n0 * w0 + n1 * w1 + n2 * w2 + n3 * w3) / (1.0f + w0 + w1 + w2 + w3);
}

float3 LinearRGBToOklab(float3 c) {
    float l = 0.4122214708f * c.r + 0.5363325363f * c.g + 0.0514459929f * c.b;
    float m = 0.2119034982f * c.r + 0.6806995451f * c.g + 0.1073969566f * c.b;
    float s = 0.0883024619f * c.r + 0.2817188376f * c.g + 0.6299787005f * c.b;

    float l_ = pow(max(l, 0.0f), 1.0f / 3.0f);
    float m_ = pow(max(m, 0.0f), 1.0f / 3.0f);
    float s_ = pow(max(s, 0.0f), 1.0f / 3.0f);

    return float3(
        0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_,
        1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_,
        0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_
    );
}

float3 OklabToLinearRGB(float3 lab) {
    float l_ = lab.x + 0.3963377774f * lab.y + 0.2158037573f * lab.z;
    float m_ = lab.x - 0.1055613458f * lab.y - 0.0638541728f * lab.z;
    float s_ = lab.x - 0.0894841775f * lab.y - 1.2914855480f * lab.z;

    float l = l_ * l_ * l_;
    float m = m_ * m_ * m_;
    float s = s_ * s_ * s_;

    return float3(
        +4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s,
        -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
        -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s
    );
}

float3 SoftGamutCompress(float3 linRgb) {
    float y = saturate(CalcLuma(linRgb));
    float minC = min(linRgb.r, min(linRgb.g, linRgb.b));
    if (minC < 0.0f) {
        float t = y / max(y - minC, 1e-5f);
        linRgb = lerp(float3(y, y, y), linRgb, saturate(t));
    }
    float maxC = max(linRgb.r, max(linRgb.g, linRgb.b));
    if (maxC > 1.0f) {
        if (y >= 1.0f) {
            return float3(1.0f, 1.0f, 1.0f);
        }
        float t = (1.0f - y) / max(maxC - y, 1e-5f);
        linRgb = lerp(float3(y, y, y), linRgb, saturate(t));
    }
    return saturate(linRgb);
}

groupshared float4 gsStats[256];

[numthreads(16, 16, 1)]
void CSAnalyze(uint3 gtid : SV_GroupThreadID) {
    uint idx = gtid.y * 16u + gtid.x;
    float2 uv = (float2(gtid.xy) + 0.5f) / 16.0f;
    int maxX = max((int)uWidth - 1, 0);
    int maxY = max((int)uHeight - 1, 0);
    int2 px = clamp(int2(uv * float2(uWidth, uHeight)), int2(0, 0), int2(maxX, maxY));

    float3 srgb = srcTex.Load(int3(px, 0)).rgb;
    float3 lin = SRGBToLinear(srgb);
    float luma = CalcLuma(lin);
    float isDark = (luma < 0.14f) ? 1.0f : 0.0f;
    float isHigh = (luma > 0.75f) ? 1.0f : 0.0f;
    float3 lab = LinearRGBToOklab(lin);
    float chroma = length(lab.yz);

    gsStats[idx] = float4(luma, isDark, chroma, isHigh);
    GroupMemoryBarrierWithGroupSync();

    [unroll]
    for (uint stride = 128u; stride > 0u; stride >>= 1u) {
        if (idx < stride) {
            gsStats[idx] += gsStats[idx + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (idx == 0u) {
        float4 avg = gsStats[0] / 256.0f;
        SceneStats prev = statsOut[0];
        float alpha = saturate(uSmoothAlpha);
        SceneStats next;
        next.avgLuma   = lerp(prev.avgLuma,   avg.x, alpha);
        next.darkRatio = lerp(prev.darkRatio, avg.y, alpha);
        next.avgChroma = lerp(prev.avgChroma, avg.z, alpha);
        next.highLuma  = lerp(prev.highLuma,  avg.w, alpha);
        statsOut[0] = next;
    }
}

struct VSOut {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

VSOut VSMain(uint id : SV_VertexID) {
    VSOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.uv = uv;
    o.pos = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, 0.0f, 1.0f);
    return o;
}

float4 PSEnhance(VSOut inp) : SV_TARGET {
    float3 rawSrgb = srcTex.SampleLevel(linearSamp, inp.uv, 0.0f).rgb;

    if ((uFlags & FLAG_SPLIT_PREVIEW) != 0u) {
        float distPx = abs(inp.uv.x - uSplitPos) * max(uWidth, 1.0f);
        if (distPx < 1.5f) {
            return float4(0.93f, 0.11f, 0.14f, 1.0f);
        }
        if (inp.uv.x < uSplitPos) {
            return float4(saturate(rawSrgb), 1.0f);
        }
    }

    uint activeMask = FLAG_ENABLE_DARK_LIGHT | FLAG_ENABLE_CLARITY |
                      FLAG_ENABLE_DYNAMIC_HDR | FLAG_ENABLE_BC | FLAG_ENABLE_VIBRANCE;
    uint activeMask2 = FLAG2_ENABLE_SHARPEN | FLAG2_ENABLE_COLOR_TEMP | FLAG2_ENABLE_GAMMA_EXP |
                       FLAG2_ENABLE_NIGHT_MODE | FLAG2_ENABLE_BW | FLAG2_ENABLE_VIGNETTE | FLAG2_ENABLE_COLORBLIND;
    if ((uFlags & activeMask) == 0u && (uFlags2 & activeMask2) == 0u) {
        return float4(saturate(rawSrgb), 1.0f);
    }

    float3 lin = SRGBToLinear(rawSrgb);
    SceneStats stats = statsIn[0];

    // 1. Standalone Dark / Shadow Lighting Recovery (independent of Intensity)
    if ((uFlags & FLAG_ENABLE_DARK_LIGHT) != 0u && uDarkLighting > 0.001f) {
        float darkUser = uDarkLighting * 2.0f;
        float sceneDarkness = 1.0f;
        if ((uFlags & FLAG_ADAPTIVE_SCENE) != 0u) {
            float lumaNeed = saturate((0.26f - stats.avgLuma) / 0.22f);
            float ratioNeed = saturate((stats.darkRatio - 0.15f) / 0.55f);
            sceneDarkness = 0.65f + 0.70f * max(lumaNeed, ratioNeed);
        }

        float y = CalcLuma(lin);
        float shadowStrength = 0.24f * darkUser * sceneDarkness;
        float toe = smoothstep(0.002f, 0.025f, y);
        float shadowMask = toe * pow(saturate(1.0f - y), 2.2f);
        float gain = 1.0f + shadowStrength * shadowMask * (1.20f / (y + 0.18f));
        float highGuard = lerp(1.0f, 0.85f, saturate(stats.highLuma * 1.4f));
        lin *= lerp(1.0f, gain, highGuard);
        lin = max(lin, 0.0f);
    }

    // 2. Standalone Dynamic HDR Tone (independent of Intensity)
    if ((uFlags & FLAG_ENABLE_DYNAMIC_HDR) != 0u && uDynamicHdr > 0.001f) {
        float hdrStrength = 0.20f * (uDynamicHdr * 2.0f);
        float y = max(CalcLuma(lin), 1e-5f);
        float yPow = pow(saturate(y), 1.0f + hdrStrength * 0.40f);
        float sCurve = y * y * (3.0f - 2.0f * y);
        float yTarget = lerp(y, lerp(sCurve, yPow, 0.35f), saturate(hdrStrength));
        if (yTarget > 0.82f) {
            float over = (yTarget - 0.82f) / 0.18f;
            yTarget = 0.82f + 0.18f * (over / (1.0f + 0.22f * hdrStrength * over));
        }
        lin *= (yTarget / y);
    }

    // 3. Standalone Clarity / Micro-Contrast (independent of Intensity)
    if ((uFlags & FLAG_ENABLE_CLARITY) != 0u && uClarity > 0.001f) {
        float clarityStrength = 0.28f * (uClarity * 2.0f);
        float2 pxStep = 1.5f / float2(max(uWidth, 1.0f), max(uHeight, 1.0f));
        float3 n0 = SRGBToLinear(srcTex.SampleLevel(linearSamp, inp.uv + float2( pxStep.x,  pxStep.y), 0.0f).rgb);
        float3 n1 = SRGBToLinear(srcTex.SampleLevel(linearSamp, inp.uv + float2(-pxStep.x,  pxStep.y), 0.0f).rgb);
        float3 n2 = SRGBToLinear(srcTex.SampleLevel(linearSamp, inp.uv + float2( pxStep.x, -pxStep.y), 0.0f).rgb);
        float3 n3 = SRGBToLinear(srcTex.SampleLevel(linearSamp, inp.uv + float2(-pxStep.x, -pxStep.y), 0.0f).rgb);

        float cLuma = CalcLuma(SRGBToLinear(rawSrgb));
        float bLuma = 0.25f * (CalcLuma(n0) + CalcLuma(n1) + CalcLuma(n2) + CalcLuma(n3));
        float detail = clamp(cLuma - bLuma, -0.12f, 0.12f);
        float midMask = smoothstep(0.02f, 0.12f, cLuma) * smoothstep(0.98f, 0.82f, cLuma);
        float curLuma = max(CalcLuma(lin), 1e-5f);
        float targetLuma = max(curLuma + detail * clarityStrength * midMask, 0.0f);
        lin *= (targetLuma / curLuma);
    }

    // 4. Unified AI Dynamic Vibrance (AMD Dark Visibility + Dual-Scale 3D Pop + Specular Color Glow + Oklab Vibrance)
    if ((uFlags & FLAG_ENABLE_VIBRANCE) != 0u && uIntensity > 0.001f) {
        // Concave progressive 0 -> 50 -> 100 curves:
        // 0% = off, 50% = ~60% of safe max (immediately vivid & clear), 100% = strong without blowing out
        float intLevel = pow(saturate(uIntensity), 0.72f);
        float satLevel = pow(saturate(uSaturationBoost), 0.75f);

        float3 lab = LinearRGBToOklab(max(lin, 0.0f));
        float L = max(lab.x, 0.0f);
        float Lc = saturate(L);
        float C0 = length(lab.yz);

        // Precompute skin protection mask so sunlit faces never get over-glowed or orange
        float skinMask0 = 1.0f;
        if ((uFlags & FLAG_SKIN_PROTECTION) != 0u && C0 > 1e-4f) {
            float hue0 = atan2(lab.z, lab.y);
            float dHue0 = abs(hue0 - 0.85f);
            dHue0 = min(dHue0, 6.2831853f - dHue0);
            float hueW0 = exp(-(dHue0 * dHue0) / (2.0f * 0.42f * 0.42f));
            float chromaW0 = smoothstep(0.015f, 0.045f, C0) * (1.0f - smoothstep(0.13f, 0.22f, C0));
            skinMask0 = lerp(1.0f, 0.28f, hueW0 * chromaW0);
        }

        // 4A. Dual-Scale Neighborhood Lightness (Fine 2px detail + Wide 14px 3D depth pop, 8 rotated taps)
        float2 invRes = 1.0f / float2(max(uWidth, 1.0f), max(uHeight, 1.0f));
        float2 fineStep = 2.0f * invRes;
        float2 wideStep = 14.0f * invRes;

        float rawL = FastPerceptualL(rawSrgb);
        float f0 = FastPerceptualL(srcTex.SampleLevel(linearSamp, inp.uv + float2( fineStep.x,  fineStep.y), 0.0f).rgb);
        float f1 = FastPerceptualL(srcTex.SampleLevel(linearSamp, inp.uv + float2(-fineStep.x,  fineStep.y), 0.0f).rgb);
        float f2 = FastPerceptualL(srcTex.SampleLevel(linearSamp, inp.uv + float2( fineStep.x, -fineStep.y), 0.0f).rgb);
        float f3 = FastPerceptualL(srcTex.SampleLevel(linearSamp, inp.uv + float2(-fineStep.x, -fineStep.y), 0.0f).rgb);
        float fineMeanL = BilateralMean4(rawL, f0, f1, f2, f3, 120.0f);

        float w0 = FastPerceptualL(srcTex.SampleLevel(linearSamp, inp.uv + float2( wideStep.x,  0.0f), 0.0f).rgb);
        float w1 = FastPerceptualL(srcTex.SampleLevel(linearSamp, inp.uv + float2(-wideStep.x,  0.0f), 0.0f).rgb);
        float w2 = FastPerceptualL(srcTex.SampleLevel(linearSamp, inp.uv + float2( 0.0f,  wideStep.y), 0.0f).rgb);
        float w3 = FastPerceptualL(srcTex.SampleLevel(linearSamp, inp.uv + float2( 0.0f, -wideStep.y), 0.0f).rgb);
        float wideMeanL = BilateralMean4(rawL, w0, w1, w2, w3, 90.0f);
        float localEnvL = 0.45f * (0.20f * (rawL + f0 + f1 + f2 + f3)) + 0.55f * (0.20f * (rawL + w0 + w1 + w2 + w3));

        // True Black-Floor Anchor (unlocks dark detail sRGB 14..45 while keeping near-black crisp)
        float blackFloor = smoothstep(0.105f, 0.20f, Lc);
        float darkZone = pow(saturate(1.0f - Lc / 0.80f), 1.55f);
        float localDark = saturate((0.58f - localEnvL) / 0.46f);
        float sceneFactor = 1.0f;
        if ((uFlags & FLAG_ADAPTIVE_SCENE) != 0u) {
            float nightBoost = saturate((0.24f - stats.avgLuma) / 0.22f);
            float brightGuard = saturate((stats.highLuma - 0.30f) / 0.45f);
            sceneFactor = lerp(1.0f, 1.30f, nightBoost) * lerp(1.0f, 0.82f, brightGuard);
        }
        float shadowLift = 0.185f * intLevel * sceneFactor * blackFloor * darkZone * (0.55f + 0.45f * localDark);

        // Adaptive Filmic Exposure lift so dimmed interiors & midtones feel alive without raising black floor
        float expAdapt = 1.0f;
        if ((uFlags & FLAG_ADAPTIVE_SCENE) != 0u) {
            expAdapt = lerp(1.22f, 0.78f, saturate((stats.avgLuma - 0.14f) / 0.28f));
        }
        float midGlow = 0.048f * intLevel * expAdapt * sin(3.14159265f * Lc) *
                        smoothstep(0.11f, 0.23f, Lc) * smoothstep(0.96f, 0.72f, Lc);

        // Component #2 (Noise-Cored & Edge-Aware): removes speckles ("bintik-bintik") & dark border halo
        float fineDiff = rawL - fineMeanL;
        float fineCoring = smoothstep(0.014f, 0.042f, abs(fineDiff));
        float fineSafe = (fineDiff * fineCoring) / (1.0f + abs(fineDiff) * 18.0f);
        float fineMask = smoothstep(0.14f, 0.28f, Lc) * smoothstep(0.995f, 0.88f, Lc);
        float fineBoost = fineSafe * (0.24f * intLevel) * fineMask;

        float wideDiff = rawL - wideMeanL;
        float wideCoring = smoothstep(0.008f, 0.028f, abs(wideDiff));
        float wideSafe = (wideDiff * wideCoring) / (1.0f + abs(wideDiff) * 10.0f);
        float wideAdj = (wideSafe < 0.0f) ? (wideSafe * lerp(0.25f, 0.80f, smoothstep(0.20f, 0.58f, Lc))) : wideSafe;
        float wideMask = smoothstep(0.18f, 0.36f, Lc) * smoothstep(0.995f, 0.84f, Lc);
        float wideBoost = wideAdj * (0.26f * intLevel) * wideMask;

        // Component #3 (Noise-Safe Specular Glow): clean HDR-like glow on real light/color without grain
        float specBand = smoothstep(0.40f, 0.72f, Lc) * smoothstep(0.995f, 0.86f, Lc);
        float chromaGlow = smoothstep(0.03f, 0.16f, C0);
        float specColorGlow = 0.042f * intLevel * specBand * chromaGlow * (0.45f + 0.55f * satLevel) * skinMask0;
        float specEdge = max(fineDiff, 0.0f) * smoothstep(0.025f, 0.065f, fineDiff);
        float specSparkle = (specEdge / (1.0f + specEdge * 14.0f)) * (0.16f * intLevel) * smoothstep(0.54f, 0.80f, Lc) * smoothstep(0.995f, 0.90f, Lc);

        float L_new = max(L + shadowLift + midGlow + fineBoost + wideBoost + specColorGlow + specSparkle, 0.0f);
        if (L_new > 0.90f && L <= 1.0f) {
            float over = (L_new - 0.90f) / 0.10f;
            L_new = 0.90f + 0.10f * (over / (1.0f + 0.42f * over));
        }

        // Preserve natural chroma proportionally when dark shadows are lifted (anchored safely above true black)
        float liftRatio = (L > 0.12f && L_new > L) ? min(L_new / L, 1.40f) : 1.0f;
        lab.yz *= lerp(1.0f, liftRatio, 0.68f * smoothstep(0.12f, 0.24f, L));
        lab.x = L_new;

        // 4B. Perceptual Oklab Dynamic Vibrance + Component #3 (Part B): Highlight Chroma Retention
        if (uSaturationBoost > 0.001f) {
            float C = length(lab.yz);
            if (C > 1e-4f) {
                float satDrive = satLevel * lerp(0.50f, 1.15f, intLevel);
                float sceneColorAdapt = 1.0f;
                if ((uFlags & FLAG_ADAPTIVE_SCENE) != 0u) {
                    sceneColorAdapt = lerp(1.16f, 0.86f, saturate((stats.avgChroma - 0.04f) / 0.10f));
                }

                float mutedWeight = 1.0f / (1.0f + C * 6.8f);
                float lumaProtect = smoothstep(0.125f, 0.23f, lab.x) * smoothstep(0.995f, 0.91f, lab.x);

                float skinMask = 1.0f;
                if ((uFlags & FLAG_SKIN_PROTECTION) != 0u) {
                    float hue = atan2(lab.z, lab.y);
                    float dHue = abs(hue - 0.85f);
                    dHue = min(dHue, 6.2831853f - dHue);
                    float hueWeight = exp(-(dHue * dHue) / (2.0f * 0.42f * 0.42f));
                    float chromaWeight = smoothstep(0.015f, 0.045f, C) * (1.0f - smoothstep(0.13f, 0.22f, C));
                    skinMask = lerp(1.0f, 0.28f, hueWeight * chromaWeight);
                }

                // Retain rich color in bright fire/sky/magic highlights instead of fading to chalky white
                float highlightPop = 1.0f + 0.32f * smoothstep(0.58f, 0.84f, lab.x) * smoothstep(0.985f, 0.90f, lab.x) * skinMask;

                float boost = 0.82f * satDrive * sceneColorAdapt * mutedWeight * lumaProtect * skinMask * highlightPop;
                float newC = min(C * (1.0f + boost), 0.34f);
                lab.yz *= (newC / C);
            }
        }

        lin = SoftGamutCompress(OklabToLinearRGB(lab));
    }

    // 5. Sharpen+ (AMD FidelityFX CAS style contrast-adaptive sharpening, halo-free)
    if ((uFlags2 & FLAG2_ENABLE_SHARPEN) != 0u && uSharpen > 0.001f) {
        float2 px1 = 1.0f / float2(max(uWidth, 1.0f), max(uHeight, 1.0f));
        float3 c = SRGBToLinear(rawSrgb);
        float3 n = SRGBToLinear(srcTex.SampleLevel(linearSamp, inp.uv + float2( 0.0f, -px1.y), 0.0f).rgb);
        float3 s = SRGBToLinear(srcTex.SampleLevel(linearSamp, inp.uv + float2( 0.0f,  px1.y), 0.0f).rgb);
        float3 w = SRGBToLinear(srcTex.SampleLevel(linearSamp, inp.uv + float2(-px1.x,  0.0f), 0.0f).rgb);
        float3 e = SRGBToLinear(srcTex.SampleLevel(linearSamp, inp.uv + float2( px1.x,  0.0f), 0.0f).rgb);

        float cL = CalcLuma(c);
        float nL = CalcLuma(n);
        float sL = CalcLuma(s);
        float wL = CalcLuma(w);
        float eL = CalcLuma(e);

        float mnL = min(cL, min(min(nL, sL), min(wL, eL)));
        float mxL = max(cL, max(max(nL, sL), max(wL, eL)));
        float amp = sqrt(saturate(min(mnL, 2.0f - mxL) / max(mxL, 1e-4f)));
        float peak = -1.0f / lerp(12.0f, 5.2f, saturate(uSharpen));
        float wCas = amp * peak;
        float sharpL = (cL + (nL + sL + wL + eL) * wCas) / (1.0f + 4.0f * wCas);
        float diffL = sharpL - cL;
        float coring = smoothstep(0.004f, 0.018f, abs(diffL));
        float casDelta = clamp(diffL * coring * (0.65f + 0.55f * saturate(uSharpen)), -0.18f, 0.18f);
        float curL = max(CalcLuma(lin), 1e-5f);
        lin *= max(curL + casDelta, 0.0f) / curL;
    }

    // 6. Standalone Brightness / Contrast (50 = exact neutral)
    if ((uFlags & FLAG_ENABLE_BC) != 0u) {
        float brightShift = (uBrightness - 0.5f) * 1.4f;
        float contrastGain = 1.0f + (uContrast - 0.5f) * 1.0f;
        lin *= exp2(brightShift);
        lin = max((lin - 0.18f) * contrastGain + 0.18f, 0.0f);
    }

    // 7. Gamma / Exposure (50 = exact neutral)
    if ((uFlags2 & FLAG2_ENABLE_GAMMA_EXP) != 0u) {
        float expEV = (uExposure - 0.5f) * 2.0f;
        lin *= exp2(expEV);
        float gammaExp = exp2((0.5f - uGamma) * 1.1f);
        lin = pow(max(lin, 0.0f), gammaExp);
    }

    // 8. Color Temperature & Tint (50 = exact neutral, luma-preserving)
    if ((uFlags2 & FLAG2_ENABLE_COLOR_TEMP) != 0u) {
        float temp = (uTemperature - 0.5f) * 2.0f;
        float tintVal = (uTint - 0.5f) * 2.0f;
        float3 wbGain = max(float3(
            1.0f + 0.22f * temp + 0.10f * tintVal,
            1.0f - 0.14f * tintVal,
            1.0f - 0.24f * temp + 0.10f * tintVal
        ), 0.10f);
        float oldY = max(CalcLuma(lin), 1e-5f);
        lin *= wbGain;
        float newY = max(CalcLuma(lin), 1e-5f);
        lin *= (oldY / newY);
    }

    // 9. Night Mode / Blue Light Filter
    if ((uFlags2 & FLAG2_ENABLE_NIGHT_MODE) != 0u && uNightMode > 0.001f) {
        float nm = saturate(uNightMode);
        float3 nightMult = float3(1.0f, lerp(1.0f, 0.80f, nm), lerp(1.0f, 0.44f, nm));
        lin *= nightMult;
    }

    // 10. Colorblind Assist (LMS Daltonization: 0=Protanopia, 1=Deuteranopia, 2=Tritanopia)
    if ((uFlags2 & FLAG2_ENABLE_COLORBLIND) != 0u && uColorblindStrength > 0.001f) {
        float3 lms = float3(
            17.8824f * lin.r + 43.5161f * lin.g + 4.11935f * lin.b,
            3.45565f * lin.r + 27.1554f * lin.g + 3.86714f * lin.b,
            0.0299566f * lin.r + 0.184309f * lin.g + 1.46709f * lin.b
        );
        float3 lmsSim = lms;
        if (uColorblindType == 0u) {
            lmsSim.x = 2.02344f * lms.y - 2.52581f * lms.z;
        } else if (uColorblindType == 1u) {
            lmsSim.y = 0.494207f * lms.x + 1.24827f * lms.z;
        } else {
            lmsSim.z = -0.395913f * lms.x + 0.801109f * lms.y;
        }
        float3 simRgb = float3(
            0.0809444479f * lmsSim.x - 0.130504409f * lmsSim.y + 0.116721066f * lmsSim.z,
           -0.0102485335f * lmsSim.x + 0.0540193266f * lmsSim.y - 0.113614708f * lmsSim.z,
           -0.000365296938f * lmsSim.x - 0.00412161469f * lmsSim.y + 0.693511405f * lmsSim.z
        );
        float3 errRgb = lin - simRgb;
        float3 shift = float3(0.0f, 0.0f, 0.0f);
        if (uColorblindType == 0u) {
            shift = float3(0.0f, 0.7f * errRgb.r, 0.7f * errRgb.r);
        } else if (uColorblindType == 1u) {
            shift = float3(0.7f * errRgb.g, 0.0f, 0.7f * errRgb.g);
        } else {
            shift = float3(0.7f * errRgb.b, 0.7f * errRgb.b, 0.0f);
        }
        lin = max(lin + shift * saturate(uColorblindStrength), 0.0f);
    }

    // 11. Black & White (Rec.709 Monochrome Mix)
    if ((uFlags2 & FLAG2_ENABLE_BW) != 0u && uBlackAndWhite > 0.001f) {
        float bwY = CalcLuma(lin);
        lin = lerp(lin, float3(bwY, bwY, bwY), saturate(uBlackAndWhite));
    }

    // 12. Cinematic Vignette
    if ((uFlags2 & FLAG2_ENABLE_VIGNETTE) != 0u && uVignette > 0.001f) {
        float2 dUv = inp.uv - 0.5f;
        float r2 = saturate(dot(dUv, dUv) * 2.0f);
        lin *= lerp(1.0f, 1.0f - 0.65f * r2, saturate(uVignette));
    }

    float3 outSrgb = LinearToSRGB(SoftGamutCompress(lin));
    if (max(outSrgb.r, max(outSrgb.g, outSrgb.b)) > 0.002f) {
        float ign = frac(52.9829189f * frac(dot(inp.pos.xy, float2(0.06711056f, 0.00583715f))));
        outSrgb = saturate(outSrgb + (ign - 0.5f) / 255.0f);
    }
    return float4(outSrgb, 1.0f);
}
)HLSL";

inline std::string GenerateReShadeFx(const FilterParams& p) {
    std::ostringstream ss;
    ss << "// AI Dynamic Vibrance Enhance - ReShade Shader (Legacy Exclusive Fullscreen Support)\n";
    ss << "// Safely removable anytime via the 'Remove .fx' button in AI Dynamic Vibrance Enhance.\n\n";
    ss << "#include \"ReShade.fxh\"\n\n";
    ss << "uniform bool UI_EnableDynamicVibrance <\n"
          "    ui_label = \"Enable AI Dynamic Vibrance\";\n"
          "> = " << (p.enableDynamicVibrance ? "true" : "false") << ";\n\n";
    ss << "uniform float UI_Intensity <\n"
          "    ui_type = \"slider\";\n"
          "    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;\n"
          "    ui_label = \"Intensity (%)\";\n"
          "> = " << p.intensity << ".0;\n\n";
    ss << "uniform float UI_SaturationBoost <\n"
          "    ui_type = \"slider\";\n"
          "    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;\n"
          "    ui_label = \"Saturation boost (%)\";\n"
          "> = " << p.saturationBoost << ".0;\n\n";
    ss << "uniform bool UI_EnableDarkLighting <\n"
          "    ui_label = \"Enable Dark / Shadow Lighting\";\n"
          "> = " << (p.enableDarkLighting ? "true" : "false") << ";\n\n";
    ss << "uniform float UI_DarkLighting <\n"
          "    ui_type = \"slider\";\n"
          "    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;\n"
          "    ui_label = \"Dark / Shadow lighting (%)\";\n"
          "> = " << p.darkLighting << ".0;\n\n";
    ss << "uniform bool UI_EnableDynamicHdr <\n"
          "    ui_label = \"Enable Dynamic HDR Tone\";\n"
          "> = " << (p.enableDynamicHdr ? "true" : "false") << ";\n\n";
    ss << "uniform float UI_DynamicHdr <\n"
          "    ui_type = \"slider\";\n"
          "    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;\n"
          "    ui_label = \"Dynamic HDR tone (%)\";\n"
          "> = " << p.dynamicHdr << ".0;\n\n";
    ss << "uniform bool UI_EnableClarity <\n"
          "    ui_label = \"Enable Clarity / Details\";\n"
          "> = " << (p.enableClarity ? "true" : "false") << ";\n\n";
    ss << "uniform float UI_Clarity <\n"
          "    ui_type = \"slider\";\n"
          "    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;\n"
          "    ui_label = \"Clarity (%)\";\n"
          "> = " << p.clarity << ".0;\n\n";
    ss << "uniform bool UI_EnableBC <\n"
          "    ui_label = \"Enable Brightness / Contrast\";\n"
          "> = " << (p.enableBrightnessContrast ? "true" : "false") << ";\n\n";
    ss << "uniform float UI_Brightness <\n"
          "    ui_type = \"slider\";\n"
          "    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;\n"
          "    ui_label = \"Brightness (%)\";\n"
          "> = " << p.brightness << ".0;\n\n";
    ss << "uniform float UI_Contrast <\n"
          "    ui_type = \"slider\";\n"
          "    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;\n"
          "    ui_label = \"Contrast (%)\";\n"
          "> = " << p.contrast << ".0;\n\n";
    ss << "uniform bool UI_EnableSharpen <\n"
          "    ui_label = \"Enable Sharpen+ (AMD CAS)\";\n"
          "> = " << (p.enableSharpen ? "true" : "false") << ";\n\n";
    ss << "uniform float UI_Sharpen <\n"
          "    ui_type = \"slider\";\n"
          "    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;\n"
          "    ui_label = \"Sharpness (%)\";\n"
          "> = " << p.sharpen << ".0;\n\n";
    ss << "uniform bool UI_EnableGammaExp <\n"
          "    ui_label = \"Enable Gamma / Exposure\";\n"
          "> = " << (p.enableGammaExposure ? "true" : "false") << ";\n\n";
    ss << "uniform float UI_Exposure <\n"
          "    ui_type = \"slider\";\n"
          "    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;\n"
          "    ui_label = \"Exposure (%)\";\n"
          "> = " << p.exposure << ".0;\n\n";
    ss << "uniform float UI_Gamma <\n"
          "    ui_type = \"slider\";\n"
          "    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;\n"
          "    ui_label = \"Gamma (%)\";\n"
          "> = " << p.gamma << ".0;\n\n";
    ss << "uniform bool UI_EnableColorTemp <\n"
          "    ui_label = \"Enable Color Temperature & Tint\";\n"
          "> = " << (p.enableColorTemp ? "true" : "false") << ";\n\n";
    ss << "uniform float UI_Temperature <\n"
          "    ui_type = \"slider\";\n"
          "    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;\n"
          "    ui_label = \"Temperature (%)\";\n"
          "> = " << p.temperature << ".0;\n\n";
    ss << "uniform float UI_Tint <\n"
          "    ui_type = \"slider\";\n"
          "    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;\n"
          "    ui_label = \"Tint (%)\";\n"
          "> = " << p.tint << ".0;\n\n";
    ss << "uniform bool UI_EnableNightMode <\n"
          "    ui_label = \"Enable Night Mode / Blue Light\";\n"
          "> = " << (p.enableNightMode ? "true" : "false") << ";\n\n";
    ss << "uniform float UI_NightMode <\n"
          "    ui_type = \"slider\";\n"
          "    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;\n"
          "    ui_label = \"Warmth (%)\";\n"
          "> = " << p.nightMode << ".0;\n\n";
    ss << "uniform bool UI_EnableColorblind <\n"
          "    ui_label = \"Enable Colorblind Assist\";\n"
          "> = " << (p.enableColorblind ? "true" : "false") << ";\n\n";
    ss << "uniform int UI_ColorblindType <\n"
          "    ui_type = \"combo\";\n"
          "    ui_items = \"Protanopia (Red)\\0Deuteranopia (Green)\\0Tritanopia (Blue-Yellow)\\0\";\n"
          "    ui_label = \"Colorblind mode\";\n"
          "> = " << p.colorblindType << ";\n\n";
    ss << "uniform float UI_ColorblindStrength <\n"
          "    ui_type = \"slider\";\n"
          "    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;\n"
          "    ui_label = \"Assist Strength (%)\";\n"
          "> = " << p.colorblindStrength << ".0;\n\n";
    ss << "uniform bool UI_EnableBW <\n"
          "    ui_label = \"Enable Black & White\";\n"
          "> = " << (p.enableBlackAndWhite ? "true" : "false") << ";\n\n";
    ss << "uniform float UI_BW <\n"
          "    ui_type = \"slider\";\n"
          "    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;\n"
          "    ui_label = \"Black & White (%)\";\n"
          "> = " << p.blackAndWhite << ".0;\n\n";
    ss << "uniform bool UI_EnableVignette <\n"
          "    ui_label = \"Enable Cinematic Vignette\";\n"
          "> = " << (p.enableVignette ? "true" : "false") << ";\n\n";
    ss << "uniform float UI_Vignette <\n"
          "    ui_type = \"slider\";\n"
          "    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;\n"
          "    ui_label = \"Vignette Strength (%)\";\n"
          "> = " << p.vignette << ".0;\n\n";

    ss << R"FX(
float3 ADV_SRGBToLinear(float3 c) {
    c = max(c, 0.0);
    return (c <= 0.04045) ? (c / 12.92) : pow(max((c + 0.055) / 1.055, 0.0), 2.4);
}

float3 ADV_LinearToSRGB(float3 c) {
    c = saturate(c);
    return (c <= 0.0031308) ? (c * 12.92) : (1.055 * pow(max(c, 0.0), 1.0 / 2.4) - 0.055);
}

float ADV_Luma(float3 c) {
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

float ADV_FastPerceptualL(float3 srgb) {
    return pow(max(ADV_Luma(ADV_SRGBToLinear(srgb)), 0.0), 0.3333333);
}

float ADV_BilateralMean4(float c, float n0, float n1, float n2, float n3, float invSigma2) {
    float d0 = n0 - c; float w0 = exp(-d0 * d0 * invSigma2);
    float d1 = n1 - c; float w1 = exp(-d1 * d1 * invSigma2);
    float d2 = n2 - c; float w2 = exp(-d2 * d2 * invSigma2);
    float d3 = n3 - c; float w3 = exp(-d3 * d3 * invSigma2);
    return (c + n0 * w0 + n1 * w1 + n2 * w2 + n3 * w3) / (1.0 + w0 + w1 + w2 + w3);
}

float3 ADV_RGBToOklab(float3 c) {
    float l = 0.4122214708 * c.r + 0.5363325363 * c.g + 0.0514459929 * c.b;
    float m = 0.2119034982 * c.r + 0.6806995451 * c.g + 0.1073969566 * c.b;
    float s = 0.0883024619 * c.r + 0.2817188376 * c.g + 0.6299787005 * c.b;
    float l_ = pow(max(l, 0.0), 1.0 / 3.0);
    float m_ = pow(max(m, 0.0), 1.0 / 3.0);
    float s_ = pow(max(s, 0.0), 1.0 / 3.0);
    return float3(
        0.2104542553 * l_ + 0.7936177850 * m_ - 0.0040720468 * s_,
        1.9779984951 * l_ - 2.4285922050 * m_ + 0.4505937099 * s_,
        0.0259040371 * l_ + 0.7827717662 * m_ - 0.8086757660 * s_
    );
}

float3 ADV_OklabToRGB(float3 lab) {
    float l_ = lab.x + 0.3963377774 * lab.y + 0.2158037573 * lab.z;
    float m_ = lab.x - 0.1055613458 * lab.y - 0.0638541728 * lab.z;
    float s_ = lab.x - 0.0894841775 * lab.y - 1.2914855480 * lab.z;
    float l = l_ * l_ * l_;
    float m = m_ * m_ * m_;
    float s = s_ * s_ * s_;
    return float3(
        +4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
        -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
        -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s
    );
}

float3 ADV_SoftGamut(float3 c) {
    float y = saturate(ADV_Luma(c));
    float minC = min(c.r, min(c.g, c.b));
    if (minC < 0.0) {
        c = lerp(float3(y, y, y), c, saturate(y / max(y - minC, 1e-5)));
    }
    float maxC = max(c.r, max(c.g, c.b));
    if (maxC > 1.0) {
        if (y >= 1.0) return float3(1.0, 1.0, 1.0);
        c = lerp(float3(y, y, y), c, saturate((1.0 - y) / max(maxC - y, 1e-5)));
    }
    return saturate(c);
}

float3 PS_AIDynamicVibrance(float4 vpos : SV_Position, float2 texcoord : TEXCOORD) : SV_Target {
    float3 rawSrgb = tex2D(ReShade::BackBuffer, texcoord).rgb;
    if (!UI_EnableDynamicVibrance && !UI_EnableDarkLighting &&
        !UI_EnableDynamicHdr && !UI_EnableClarity && !UI_EnableBC &&
        !UI_EnableSharpen && !UI_EnableGammaExp && !UI_EnableColorTemp &&
        !UI_EnableNightMode && !UI_EnableColorblind && !UI_EnableBW && !UI_EnableVignette) {
        return rawSrgb;
    }

    float3 lin = ADV_SRGBToLinear(rawSrgb);

    if (UI_EnableDarkLighting && UI_DarkLighting > 0.01) {
        float darkUser = (UI_DarkLighting / 100.0) * 2.0;
        float y = ADV_Luma(lin);
        float shadowStrength = 0.24 * darkUser;
        float toe = smoothstep(0.002, 0.025, y);
        float shadowMask = toe * pow(saturate(1.0 - y), 2.2);
        float gain = 1.0 + shadowStrength * shadowMask * (1.20 / (y + 0.18));
        lin = max(lin * gain, 0.0);
    }

    if (UI_EnableDynamicHdr && UI_DynamicHdr > 0.01) {
        float hdrStrength = 0.20 * (UI_DynamicHdr / 100.0) * 2.0;
        float y = max(ADV_Luma(lin), 1e-5);
        float yPow = pow(saturate(y), 1.0 + hdrStrength * 0.40);
        float sCurve = y * y * (3.0 - 2.0 * y);
        float yTarget = lerp(y, lerp(sCurve, yPow, 0.35), saturate(hdrStrength));
        if (yTarget > 0.82) {
            float over = (yTarget - 0.82) / 0.18;
            yTarget = 0.82 + 0.18 * (over / (1.0 + 0.22 * hdrStrength * over));
        }
        lin *= (yTarget / y);
    }

    if (UI_EnableClarity && UI_Clarity > 0.01) {
        float clarityStrength = 0.28 * (UI_Clarity / 100.0) * 2.0;
        float2 pxStep = BUFFER_PIXEL_SIZE * 1.5;
        float3 n0 = ADV_SRGBToLinear(tex2D(ReShade::BackBuffer, texcoord + float2( pxStep.x,  pxStep.y)).rgb);
        float3 n1 = ADV_SRGBToLinear(tex2D(ReShade::BackBuffer, texcoord + float2(-pxStep.x,  pxStep.y)).rgb);
        float3 n2 = ADV_SRGBToLinear(tex2D(ReShade::BackBuffer, texcoord + float2( pxStep.x, -pxStep.y)).rgb);
        float3 n3 = ADV_SRGBToLinear(tex2D(ReShade::BackBuffer, texcoord + float2(-pxStep.x, -pxStep.y)).rgb);
        float cLuma = ADV_Luma(ADV_SRGBToLinear(rawSrgb));
        float bLuma = 0.25 * (ADV_Luma(n0) + ADV_Luma(n1) + ADV_Luma(n2) + ADV_Luma(n3));
        float detail = clamp(cLuma - bLuma, -0.12, 0.12);
        float midMask = smoothstep(0.02, 0.12, cLuma) * smoothstep(0.98, 0.82, cLuma);
        float curLuma = max(ADV_Luma(lin), 1e-5);
        float targetLuma = max(curLuma + detail * clarityStrength * midMask, 0.0);
        lin *= (targetLuma / curLuma);
    }

    if (UI_EnableDynamicVibrance && UI_Intensity > 0.01) {
        float intLevel = pow(saturate(UI_Intensity / 100.0), 0.72);
        float satLevel = pow(saturate(UI_SaturationBoost / 100.0), 0.75);

        float3 lab = ADV_RGBToOklab(max(lin, 0.0));
        float L = max(lab.x, 0.0);
        float Lc = saturate(L);
        float C0 = length(lab.yz);

        float skinMask0 = 1.0;
        if (C0 > 1e-4) {
            float hue0 = atan2(lab.z, lab.y);
            float dHue0 = abs(hue0 - 0.85);
            dHue0 = min(dHue0, 6.2831853 - dHue0);
            float hueW0 = exp(-(dHue0 * dHue0) / (2.0 * 0.42 * 0.42));
            float chromaW0 = smoothstep(0.015, 0.045, C0) * (1.0 - smoothstep(0.13, 0.22, C0));
            skinMask0 = lerp(1.0, 0.28, hueW0 * chromaW0);
        }

        float2 fineStep = BUFFER_PIXEL_SIZE * 2.0;
        float2 wideStep = BUFFER_PIXEL_SIZE * 14.0;

        float rawL = ADV_FastPerceptualL(rawSrgb);
        float f0 = ADV_FastPerceptualL(tex2D(ReShade::BackBuffer, texcoord + float2( fineStep.x,  fineStep.y)).rgb);
        float f1 = ADV_FastPerceptualL(tex2D(ReShade::BackBuffer, texcoord + float2(-fineStep.x,  fineStep.y)).rgb);
        float f2 = ADV_FastPerceptualL(tex2D(ReShade::BackBuffer, texcoord + float2( fineStep.x, -fineStep.y)).rgb);
        float f3 = ADV_FastPerceptualL(tex2D(ReShade::BackBuffer, texcoord + float2(-fineStep.x, -fineStep.y)).rgb);
        float fineMeanL = ADV_BilateralMean4(rawL, f0, f1, f2, f3, 120.0);

        float w0 = ADV_FastPerceptualL(tex2D(ReShade::BackBuffer, texcoord + float2( wideStep.x,  0.0)).rgb);
        float w1 = ADV_FastPerceptualL(tex2D(ReShade::BackBuffer, texcoord + float2(-wideStep.x,  0.0)).rgb);
        float w2 = ADV_FastPerceptualL(tex2D(ReShade::BackBuffer, texcoord + float2( 0.0,  wideStep.y)).rgb);
        float w3 = ADV_FastPerceptualL(tex2D(ReShade::BackBuffer, texcoord + float2( 0.0, -wideStep.y)).rgb);
        float wideMeanL = ADV_BilateralMean4(rawL, w0, w1, w2, w3, 90.0);
        float localEnvL = 0.45 * (0.20 * (rawL + f0 + f1 + f2 + f3)) + 0.55 * (0.20 * (rawL + w0 + w1 + w2 + w3));

        float blackFloor = smoothstep(0.105, 0.20, Lc);
        float darkZone = pow(saturate(1.0 - Lc / 0.80), 1.55);
        float localDark = saturate((0.58 - localEnvL) / 0.46);
        float sceneFactor = 1.0;
        float shadowLift = 0.185 * intLevel * sceneFactor * blackFloor * darkZone * (0.55 + 0.45 * localDark);

        float midGlow = 0.048 * intLevel * sin(3.14159265 * Lc) * smoothstep(0.11, 0.23, Lc) * smoothstep(0.96, 0.72, Lc);

        float fineDiff = rawL - fineMeanL;
        float fineCoring = smoothstep(0.014, 0.042, abs(fineDiff));
        float fineSafe = (fineDiff * fineCoring) / (1.0 + abs(fineDiff) * 18.0);
        float fineMask = smoothstep(0.14, 0.28, Lc) * smoothstep(0.995, 0.88, Lc);
        float fineBoost = fineSafe * (0.24 * intLevel) * fineMask;

        float wideDiff = rawL - wideMeanL;
        float wideCoring = smoothstep(0.008, 0.028, abs(wideDiff));
        float wideSafe = (wideDiff * wideCoring) / (1.0 + abs(wideDiff) * 10.0);
        float wideAdj = (wideSafe < 0.0) ? (wideSafe * lerp(0.25, 0.80, smoothstep(0.20, 0.58, Lc))) : wideSafe;
        float wideMask = smoothstep(0.18, 0.36, Lc) * smoothstep(0.995, 0.84, Lc);
        float wideBoost = wideAdj * (0.26 * intLevel) * wideMask;

        float specBand = smoothstep(0.40, 0.72, Lc) * smoothstep(0.995, 0.86, Lc);
        float chromaGlow = smoothstep(0.03, 0.16, C0);
        float specColorGlow = 0.042 * intLevel * specBand * chromaGlow * (0.45 + 0.55 * satLevel) * skinMask0;
        float specEdge = max(fineDiff, 0.0) * smoothstep(0.025, 0.065, fineDiff);
        float specSparkle = (specEdge / (1.0 + specEdge * 14.0)) * (0.16 * intLevel) * smoothstep(0.54, 0.80, Lc) * smoothstep(0.995, 0.90, Lc);

        float L_new = max(L + shadowLift + midGlow + fineBoost + wideBoost + specColorGlow + specSparkle, 0.0);
        if (L_new > 0.90 && L <= 1.0) {
            float over = (L_new - 0.90) / 0.10;
            L_new = 0.90 + 0.10 * (over / (1.0 + 0.42 * over));
        }

        float liftRatio = (L > 0.12 && L_new > L) ? min(L_new / L, 1.40) : 1.0;
        lab.yz *= lerp(1.0, liftRatio, 0.68 * smoothstep(0.12, 0.24, L));
        lab.x = L_new;

        if (UI_SaturationBoost > 0.01) {
            float C = length(lab.yz);
            if (C > 1e-4) {
                float satDrive = satLevel * lerp(0.50, 1.15, intLevel);
                float mutedWeight = 1.0 / (1.0 + C * 6.8);
                float lumaProtect = smoothstep(0.125, 0.23, lab.x) * smoothstep(0.995, 0.91, lab.x);
                float hue = atan2(lab.z, lab.y);
                float dHue = abs(hue - 0.85);
                dHue = min(dHue, 6.2831853 - dHue);
                float hueWeight = exp(-(dHue * dHue) / (2.0 * 0.42 * 0.42));
                float chromaWeight = smoothstep(0.015, 0.045, C) * (1.0 - smoothstep(0.13, 0.22, C));
                float skinMask = lerp(1.0, 0.28, hueWeight * chromaWeight);
                float highlightPop = 1.0 + 0.32 * smoothstep(0.58, 0.84, lab.x) * smoothstep(0.985, 0.90, lab.x) * skinMask;
                float boost = 0.82 * satDrive * mutedWeight * lumaProtect * skinMask * highlightPop;
                float newC = min(C * (1.0 + boost), 0.34);
                lab.yz *= (newC / C);
            }
        }

        lin = ADV_SoftGamut(ADV_OklabToRGB(lab));
    }

    if (UI_EnableSharpen && UI_Sharpen > 0.01) {
        float sharpNorm = saturate(UI_Sharpen / 100.0);
        float2 px1 = BUFFER_PIXEL_SIZE;
        float cL = ADV_Luma(ADV_SRGBToLinear(rawSrgb));
        float nL = ADV_Luma(ADV_SRGBToLinear(tex2D(ReShade::BackBuffer, texcoord + float2( 0.0, -px1.y)).rgb));
        float sL = ADV_Luma(ADV_SRGBToLinear(tex2D(ReShade::BackBuffer, texcoord + float2( 0.0,  px1.y)).rgb));
        float wL = ADV_Luma(ADV_SRGBToLinear(tex2D(ReShade::BackBuffer, texcoord + float2(-px1.x,  0.0)).rgb));
        float eL = ADV_Luma(ADV_SRGBToLinear(tex2D(ReShade::BackBuffer, texcoord + float2( px1.x,  0.0)).rgb));

        float mnL = min(cL, min(min(nL, sL), min(wL, eL)));
        float mxL = max(cL, max(max(nL, sL), max(wL, eL)));
        float amp = sqrt(saturate(min(mnL, 2.0 - mxL) / max(mxL, 1e-4)));
        float peak = -1.0 / lerp(12.0, 5.2, sharpNorm);
        float wCas = amp * peak;
        float sharpL = (cL + (nL + sL + wL + eL) * wCas) / (1.0 + 4.0 * wCas);
        float diffL = sharpL - cL;
        float coring = smoothstep(0.004, 0.018, abs(diffL));
        float casDelta = clamp(diffL * coring * (0.65 + 0.55 * sharpNorm), -0.18, 0.18);
        float curL = max(ADV_Luma(lin), 1e-5);
        lin *= max(curL + casDelta, 0.0) / curL;
    }

    if (UI_EnableBC) {
        float brightShift = ((UI_Brightness / 100.0) - 0.5) * 1.4;
        float contrastGain = 1.0 + ((UI_Contrast / 100.0) - 0.5) * 1.0;
        lin *= exp2(brightShift);
        lin = max((lin - 0.18) * contrastGain + 0.18, 0.0);
    }

    if (UI_EnableGammaExp) {
        float expEV = ((UI_Exposure / 100.0) - 0.5) * 2.0;
        lin *= exp2(expEV);
        float gammaExp = exp2((0.5 - (UI_Gamma / 100.0)) * 1.1);
        lin = pow(max(lin, 0.0), gammaExp);
    }

    if (UI_EnableColorTemp) {
        float temp = ((UI_Temperature / 100.0) - 0.5) * 2.0;
        float tintVal = ((UI_Tint / 100.0) - 0.5) * 2.0;
        float3 wbGain = max(float3(
            1.0 + 0.22 * temp + 0.10 * tintVal,
            1.0 - 0.14 * tintVal,
            1.0 - 0.24 * temp + 0.10 * tintVal
        ), 0.10);
        float oldY = max(ADV_Luma(lin), 1e-5);
        lin *= wbGain;
        float newY = max(ADV_Luma(lin), 1e-5);
        lin *= (oldY / newY);
    }

    if (UI_EnableNightMode && UI_NightMode > 0.01) {
        float nm = saturate(UI_NightMode / 100.0);
        float3 nightMult = float3(1.0, lerp(1.0, 0.80, nm), lerp(1.0, 0.44, nm));
        lin *= nightMult;
    }

    if (UI_EnableColorblind && UI_ColorblindStrength > 0.01) {
        float3 lms = float3(
            17.8824 * lin.r + 43.5161 * lin.g + 4.11935 * lin.b,
            3.45565 * lin.r + 27.1554 * lin.g + 3.86714 * lin.b,
            0.0299566 * lin.r + 0.184309 * lin.g + 1.46709 * lin.b
        );
        float3 lmsSim = lms;
        if (UI_ColorblindType == 0) {
            lmsSim.x = 2.02344 * lms.y - 2.52581 * lms.z;
        } else if (UI_ColorblindType == 1) {
            lmsSim.y = 0.494207 * lms.x + 1.24827 * lms.z;
        } else {
            lmsSim.z = -0.395913 * lms.x + 0.801109 * lms.y;
        }
        float3 simRgb = float3(
            0.0809444479 * lmsSim.x - 0.130504409 * lmsSim.y + 0.116721066 * lmsSim.z,
           -0.0102485335 * lmsSim.x + 0.0540193266 * lmsSim.y - 0.113614708 * lmsSim.z,
           -0.000365296938 * lmsSim.x - 0.00412161469 * lmsSim.y + 0.693511405 * lmsSim.z
        );
        float3 errRgb = lin - simRgb;
        float3 shift = float3(0.0, 0.0, 0.0);
        if (UI_ColorblindType == 0) {
            shift = float3(0.0, 0.7 * errRgb.r, 0.7 * errRgb.r);
        } else if (UI_ColorblindType == 1) {
            shift = float3(0.7 * errRgb.g, 0.0, 0.7 * errRgb.g);
        } else {
            shift = float3(0.7 * errRgb.b, 0.7 * errRgb.b, 0.0);
        }
        lin = max(lin + shift * saturate(UI_ColorblindStrength / 100.0), 0.0);
    }

    if (UI_EnableBW && UI_BW > 0.01) {
        float bwY = ADV_Luma(lin);
        lin = lerp(lin, float3(bwY, bwY, bwY), saturate(UI_BW / 100.0));
    }

    if (UI_EnableVignette && UI_Vignette > 0.01) {
        float2 dUv = texcoord - 0.5;
        float r2 = saturate(dot(dUv, dUv) * 2.0);
        lin *= lerp(1.0, 1.0 - 0.65 * r2, saturate(UI_Vignette / 100.0));
    }

    float3 outSrgb = ADV_LinearToSRGB(ADV_SoftGamut(lin));
    if (max(outSrgb.r, max(outSrgb.g, outSrgb.b)) > 0.002) {
        float ign = frac(52.9829189 * frac(dot(vpos.xy, float2(0.06711056, 0.00583715))));
        outSrgb = saturate(outSrgb + (ign - 0.5) / 255.0);
    }
    return outSrgb;
}

technique AIDynamicVibranceEnhance {
    pass {
        VertexShader = PostProcessVS;
        PixelShader = PS_AIDynamicVibrance;
    }
}
)FX";
    return ss.str();
}

inline std::filesystem::path ResolveReShadeTargetFile(const std::wstring& folder) {
    namespace fs = std::filesystem;
    fs::path base(folder);
    fs::path subShaders = base / L"reshade-shaders" / L"Shaders";
    if (fs::exists(subShaders) && fs::is_directory(subShaders)) {
        return subShaders / L"AIDynamicVibranceEnhance.fx";
    }
    return base / L"AIDynamicVibranceEnhance.fx";
}

inline bool InstallReShadeShader(
    const std::wstring& folder,
    const FilterParams& params,
    std::wstring& outInstalledPath,
    std::wstring& outError
) {
    namespace fs = std::filesystem;
    if (folder.empty()) {
        outError = L"Please select a game or reshade-shaders folder first.";
        return false;
    }
    std::error_code ec;
    fs::path dir(folder);
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) {
        outError = L"Target folder was not found.";
        return false;
    }
    fs::path target = ResolveReShadeTargetFile(folder);
    std::ofstream ofs(target, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        outError = L"Failed to write .fx file (check folder permissions).";
        return false;
    }
    std::string content = GenerateReShadeFx(params);
    ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
    ofs.close();
    if (!ofs) {
        outError = L"Error while saving .fx file.";
        return false;
    }
    outInstalledPath = target.wstring();
    return true;
}

inline bool UninstallReShadeShader(const std::wstring& folder, std::wstring& outMessage) {
    namespace fs = std::filesystem;
    if (folder.empty()) {
        outMessage = L"Please select a game or reshade-shaders folder first.";
        return false;
    }
    std::error_code ec;
    fs::path p1 = ResolveReShadeTargetFile(folder);
    fs::path p2 = fs::path(folder) / L"AIDynamicVibranceEnhance.fx";
    bool removed = false;
    if (fs::exists(p1, ec)) {
        removed |= fs::remove(p1, ec);
    }
    if (p2 != p1 && fs::exists(p2, ec)) {
        removed |= fs::remove(p2, ec);
    }
    if (removed) {
        outMessage = L"AIDynamicVibranceEnhance.fx was cleanly removed.";
        return true;
    }
    outMessage = L"AIDynamicVibranceEnhance.fx was not found in that folder.";
    return false;
}
