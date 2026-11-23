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
    float uHdrPeakNits;

    float uHdrMidGray;
    float uSdrWhiteNits;
    uint  uHdrOutputMode;
    float uAoIntensity;

    float uAoRadius;
    float uBloomIntensity;
    float uBloomThreshold;
    float uFilmicStrength;

    uint  uFilmicMode;
    float uDebandStrength;
    float uPad1;
    float uPad2;
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
static const uint FLAG2_ENABLE_AO         = 1u << 7;
static const uint FLAG2_ENABLE_BLOOM      = 1u << 8;
static const uint FLAG2_ENABLE_FILMIC     = 1u << 9;
static const uint FLAG2_ENABLE_DEBAND     = 1u << 10;

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
    float y = max(dot(saturate(srgb), float3(0.2126f, 0.7152f, 0.0722f)), 0.0f);
    return sqrt(y * sqrt(y));
}

float BilateralMean4(float c, float n0, float n1, float n2, float n3, float invSigma2) {
    float d0 = n0 - c; float w0 = 1.0f / (1.0f + d0 * d0 * invSigma2);
    float d1 = n1 - c; float w1 = 1.0f / (1.0f + d1 * d1 * invSigma2);
    float d2 = n2 - c; float w2 = 1.0f / (1.0f + d2 * d2 * invSigma2);
    float d3 = n3 - c; float w3 = 1.0f / (1.0f + d3 * d3 * invSigma2);
    return (c + n0 * w0 + n1 * w1 + n2 * w2 + n3 * w3) / (1.0f + w0 + w1 + w2 + w3);
}

float AoPairOcclusion(float cL, float aL, float bL) {
    float pairMin = min(aL, bL);
    float pairAvg = 0.5f * (aL + bL);
    float symLift = max(pairMin - cL, 0.0f);
    float cornerGate = smoothstep(0.0f, 0.055f, pairMin - cL + 0.025f);
    float cornerLift = max(pairAvg - cL, 0.0f) * cornerGate;
    float edgeSuppress = 1.0f / (1.0f + abs(aL - bL) * 4.5f);
    return (0.72f * symLift + 0.28f * cornerLift) * edgeSuppress;
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
        float3 satPreserve = linRgb / maxC;
        float t = (1.0f - y) / max(maxC - y, 1e-5f);
        float3 lumaPreserve = lerp(float3(y, y, y), linRgb, saturate(t));
        float whiteRoll = smoothstep(0.55f, 0.92f, y);
        linRgb = lerp(satPreserve, lumaPreserve, whiteRoll);
    }
    return saturate(linRgb);
}

float3 Rec709ToRec2020(float3 c) {
    return float3(
        0.6274040f * c.r + 0.3292820f * c.g + 0.0433136f * c.b,
        0.0690970f * c.r + 0.9195400f * c.g + 0.0113612f * c.b,
        0.0163916f * c.r + 0.0880132f * c.g + 0.8955950f * c.b
    );
}

float3 Rec2020ToRec709(float3 c) {
    return float3(
        +1.6604910f * c.r - 0.5876411f * c.g - 0.0728499f * c.b,
        -0.1245505f * c.r + 1.1328999f * c.g - 0.0083494f * c.b,
        -0.0181508f * c.r - 0.1005789f * c.g + 1.1187297f * c.b
    );
}

float3 ClampToBT2020InRec709(float3 lin709) {
    float3 r2020 = Rec709ToRec2020(lin709);
    float y2020 = max(dot(r2020, float3(0.2627f, 0.6780f, 0.0593f)), 0.0f);
    float minC = min(r2020.r, min(r2020.g, r2020.b));
    if (minC < 0.0f) {
        float t = y2020 / max(y2020 - minC, 1e-5f);
        r2020 = lerp(float3(y2020, y2020, y2020), r2020, saturate(t));
    }
    return Rec2020ToRec709(max(r2020, 0.0f));
}

float4 FinalizeOutput(float3 lin, float2 posXY, bool alreadyScaledHdr) {
    if (uHdrOutputMode == 0u) {
        float3 outSrgb = LinearToSRGB(SoftGamutCompress(lin));
        if (max(outSrgb.r, max(outSrgb.g, outSrgb.b)) > 0.002f) {
            float ign = frac(52.9829189f * frac(dot(posXY, float2(0.06711056f, 0.00583715f))));
            outSrgb = saturate(outSrgb + (ign - 0.5f) / 255.0f);
        }
        return float4(outSrgb, 1.0f);
    }

    float sdrScale = max(uSdrWhiteNits, 80.0f) / 80.0f;
    float peakScale = max(clamp(uHdrPeakNits, 300.0f, 1500.0f) / 80.0f, sdrScale * 1.25f);
    float3 scRgb = alreadyScaledHdr ? max(lin, 0.0f) : (max(lin, 0.0f) * sdrScale);
    scRgb = ClampToBT2020InRec709(scRgb);
    float maxC = max(scRgb.r, max(scRgb.g, scRgb.b));
    if (maxC > peakScale) {
        scRgb *= (peakScale / maxC);
    }
    return float4(scRgb, 1.0f);
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
            if (uHdrOutputMode == 0u) {
                return float4(0.93f, 0.11f, 0.14f, 1.0f);
            }
            return FinalizeOutput(SRGBToLinear(float3(0.93f, 0.11f, 0.14f)), inp.pos.xy, false);
        }
        if (inp.uv.x < uSplitPos) {
            return FinalizeOutput(SRGBToLinear(rawSrgb), inp.pos.xy, false);
        }
    }

    uint activeMask = FLAG_ENABLE_DARK_LIGHT | FLAG_ENABLE_CLARITY |
                      FLAG_ENABLE_DYNAMIC_HDR | FLAG_ENABLE_BC | FLAG_ENABLE_VIBRANCE;
    uint activeMask2 = FLAG2_ENABLE_SHARPEN | FLAG2_ENABLE_COLOR_TEMP | FLAG2_ENABLE_GAMMA_EXP |
                       FLAG2_ENABLE_NIGHT_MODE | FLAG2_ENABLE_BW | FLAG2_ENABLE_VIGNETTE |
                       FLAG2_ENABLE_COLORBLIND | FLAG2_ENABLE_AO |
                       FLAG2_ENABLE_BLOOM | FLAG2_ENABLE_FILMIC | FLAG2_ENABLE_DEBAND;
    if ((uFlags & activeMask) == 0u && (uFlags2 & activeMask2) == 0u) {
        return FinalizeOutput(SRGBToLinear(rawSrgb), inp.pos.xy, false);
    }

    // 0. Edge-Preserving Stochastic Deband (smooths 8-bit sky & shadow gradients prior to contrast/vibrance)
    if ((uFlags2 & FLAG2_ENABLE_DEBAND) != 0u && uDebandStrength > 0.001f) {
        float dbLvl = saturate(uDebandStrength);
        float2 invRes = 1.0f / float2(max(uWidth, 1.0f), max(uHeight, 1.0f));
        float r1 = frac(52.9829189f * frac(dot(inp.pos.xy, float2(0.06711056f, 0.00583715f))));
        float r2 = frac(52.9829189f * frac(dot(inp.pos.xy + float2(17.0f, 31.0f), float2(0.06711056f, 0.00583715f))));
        float rad = lerp(2.0f, lerp(7.0f, 16.0f, dbLvl), r1);
        float ang = r2 * 6.2831853f;
        float2 d1 = float2(cos(ang), sin(ang)) * rad * invRes;
        float2 d2 = float2(-d1.y, d1.x);

        float3 s0 = srcTex.SampleLevel(linearSamp, inp.uv + d1, 0.0f).rgb;
        float3 s1 = srcTex.SampleLevel(linearSamp, inp.uv - d1, 0.0f).rgb;
        float3 s2 = srcTex.SampleLevel(linearSamp, inp.uv + d2, 0.0f).rgb;
        float3 s3 = srcTex.SampleLevel(linearSamp, inp.uv - d2, 0.0f).rgb;
        float3 avg = 0.25f * (s0 + s1 + s2 + s3);

        float3 diffAvg = abs(avg - rawSrgb);
        float3 diffMax = max(max(abs(s0 - rawSrgb), abs(s1 - rawSrgb)), max(abs(s2 - rawSrgb), abs(s3 - rawSrgb)));
        float metric = max(max(diffAvg.r, max(diffAvg.g, diffAvg.b)),
                           0.55f * max(diffMax.r, max(diffMax.g, diffMax.b)));
        float thresh = lerp(0.010f, 0.036f, dbLvl);
        float wDb = smoothstep(thresh, thresh * 0.28f, metric);
        rawSrgb = lerp(rawSrgb, avg, wDb * lerp(0.45f, 0.92f, dbLvl));
    }

    float3 lin = SRGBToLinear(rawSrgb);
    SceneStats stats = statsIn[0];

    // 1. Standalone Dark / Shadow Lighting Recovery (deep shadow reveal with crevice preservation & cast cleanup)
    if ((uFlags & FLAG_ENABLE_DARK_LIGHT) != 0u && uDarkLighting > 0.001f) {
        float darkLvl = pow(saturate(uDarkLighting), 0.85f);
        float sceneDarkness = 1.0f;
        if ((uFlags & FLAG_ADAPTIVE_SCENE) != 0u) {
            float lumaNeed = saturate((0.26f - stats.avgLuma) / 0.22f);
            float ratioNeed = saturate((stats.darkRatio - 0.15f) / 0.55f);
            sceneDarkness = 0.72f + 0.65f * max(lumaNeed, ratioNeed);
        }

        float2 crvStep = 3.0f / float2(max(uWidth, 1.0f), max(uHeight, 1.0f));
        float crvCenterL = FastPerceptualL(rawSrgb);
        float crv0 = FastPerceptualL(srcTex.SampleLevel(linearSamp, inp.uv + float2( crvStep.x,  crvStep.y), 0.0f).rgb);
        float crv1 = FastPerceptualL(srcTex.SampleLevel(linearSamp, inp.uv + float2(-crvStep.x,  crvStep.y), 0.0f).rgb);
        float crv2 = FastPerceptualL(srcTex.SampleLevel(linearSamp, inp.uv + float2( crvStep.x, -crvStep.y), 0.0f).rgb);
        float crv3 = FastPerceptualL(srcTex.SampleLevel(linearSamp, inp.uv + float2(-crvStep.x, -crvStep.y), 0.0f).rgb);
        float crvAvg = 0.25f * (crv0 + crv1 + crv2 + crv3);
        float creviceKeep = saturate((crvAvg - crvCenterL - 0.012f) / 0.14f);

        float y = max(CalcLuma(lin), 1e-6f);
        float toe = smoothstep(0.0014f, 0.0045f, y);
        float shadowStrength = darkLvl * sceneDarkness * lerp(1.0f, 0.40f, creviceKeep * darkLvl);
        float shadowPow = lerp(1.0f, 0.42f, saturate(shadowStrength * 0.78f));
        float yLifted = pow(saturate(y), shadowPow);
        float shadowBlend = toe * pow(saturate(1.0f - saturate(y)), 1.65f);
        float targetY = lerp(y, max(yLifted, y), shadowBlend * saturate(shadowStrength));
        float highGuard = lerp(1.0f, 0.88f, saturate(stats.highLuma * 1.4f));
        float effGain = lerp(1.0f, targetY / y, highGuard);
        float3 lifted = max(lin * effGain, 0.0f);
        float liftAmt = saturate((effGain - 1.35f) / 3.5f) * smoothstep(0.05f, 0.002f, y);
        float liftY = CalcLuma(lifted);
        lin = lerp(lifted, float3(liftY, liftY, liftY), 0.14f * liftAmt);
    }

    // 2. Brightness / Contrast (50 = exact neutral, filmic luminance power contrast BEFORE Vibrance)
    if ((uFlags & FLAG_ENABLE_BC) != 0u) {
        float yOld = max(CalcLuma(lin), 1e-6f);
        float brightScale = exp2((uBrightness - 0.5f) * 2.2f);
        float contrastGain = (uContrast >= 0.5f) ? (1.0f + (uContrast - 0.5f) * 2.0f) : (0.40f + uContrast * 1.2f);
        float yBright = yOld * brightScale;
        float yNew = max(0.18f * pow(max(yBright / 0.18f, 1e-6f), contrastGain), 0.0f);
        lin = max(lin * (yNew / yOld), 0.0f);
    }

    // 3. Gamma / Exposure (50 = exact neutral, chroma-retaining luminance gamma BEFORE Vibrance)
    if ((uFlags2 & FLAG2_ENABLE_GAMMA_EXP) != 0u) {
        float expEV = (uExposure - 0.5f) * 3.0f;
        lin *= exp2(expEV);
        float gammaExp = exp2((0.5f - uGamma) * 1.5f);
        float yOld = max(CalcLuma(lin), 1e-6f);
        float yNew = pow(yOld, gammaExp);
        float3 chromPres = max(lin * (yNew / yOld), 0.0f);
        float3 perChan = pow(max(lin, 0.0f), gammaExp);
        lin = lerp(perChan, chromPres, 0.82f);
    }

    // 4. Dynamic HDR in SDR Mode (Local Dynamic HDR Tone Mapping + Highlight Saturation)
    if (uHdrOutputMode == 0u && (uFlags & FLAG_ENABLE_DYNAMIC_HDR) != 0u && uDynamicHdr > 0.001f) {
        float hdrLvl = pow(saturate(uDynamicHdr), 0.82f);
        float y = max(CalcLuma(lin), 1e-5f);
        float2 hdrStep = 12.0f / float2(max(uWidth, 1.0f), max(uHeight, 1.0f));
        float ly0 = CalcLuma(SRGBToLinear(srcTex.SampleLevel(linearSamp, inp.uv + float2( hdrStep.x,  0.0f), 0.0f).rgb));
        float ly1 = CalcLuma(SRGBToLinear(srcTex.SampleLevel(linearSamp, inp.uv + float2(-hdrStep.x,  0.0f), 0.0f).rgb));
        float ly2 = CalcLuma(SRGBToLinear(srcTex.SampleLevel(linearSamp, inp.uv + float2( 0.0f,  hdrStep.y), 0.0f).rgb));
        float ly3 = CalcLuma(SRGBToLinear(srcTex.SampleLevel(linearSamp, inp.uv + float2( 0.0f, -hdrStep.y), 0.0f).rgb));
        float localY = 0.25f * (ly0 + ly1 + ly2 + ly3);

        float midGamma = exp2((0.5f - saturate(uHdrMidGray)) * 0.90f);
        float yMid = pow(saturate(y), midGamma);
        float sCurve = yMid * yMid * (3.0f - 2.0f * yMid);
        float localContrast = clamp((y - localY) * 0.48f, -0.14f, 0.18f) * smoothstep(0.02f, 0.15f, y);
        float yTone = lerp(yMid, sCurve, 0.85f * hdrLvl) + localContrast * hdrLvl;
        float shadowAnchor = 1.0f + 0.18f * hdrLvl * smoothstep(0.01f, 0.08f, yMid) * pow(saturate(1.0f - yMid), 2.2f);
        float yTarget = max(yTone * shadowAnchor, 0.0f);
        if (yTarget > 0.78f) {
            float over = (yTarget - 0.78f) / 0.22f;
            yTarget = 0.78f + 0.22f * (over / (1.0f + 0.32f * hdrLvl * over));
        }
        lin *= (yTarget / y);

        float hlSatMask = smoothstep(0.35f, 0.82f, y) * (1.0f - smoothstep(0.92f, 1.0f, y));
        float yNow = CalcLuma(lin);
        lin = max(lerp(float3(yNow, yNow, yNow), lin, 1.0f + 0.20f * hdrLvl * hlSatMask), 0.0f);
    }

    // 5. Standalone Clarity / Details (Dual-Scale Edge-Aware Bilateral Micro-Contrast)
    if ((uFlags & FLAG_ENABLE_CLARITY) != 0u && uClarity > 0.001f) {
        float clarityLvl = pow(saturate(uClarity), 0.85f);
        float2 invRes = 1.0f / float2(max(uWidth, 1.0f), max(uHeight, 1.0f));
        float2 step4 = 4.0f * invRes;
        float2 step9 = 9.0f * invRes;

        float rawL = FastPerceptualL(rawSrgb);
        float c0 = FastPerceptualL(srcTex.SampleLevel(linearSamp, inp.uv + float2( step4.x,  step4.y), 0.0f).rgb);
        float c1 = FastPerceptualL(srcTex.SampleLevel(linearSamp, inp.uv + float2(-step4.x,  step4.y), 0.0f).rgb);
        float c2 = FastPerceptualL(srcTex.SampleLevel(linearSamp, inp.uv + float2( step4.x, -step4.y), 0.0f).rgb);
        float c3 = FastPerceptualL(srcTex.SampleLevel(linearSamp, inp.uv + float2(-step4.x, -step4.y), 0.0f).rgb);
        float mean4 = BilateralMean4(rawL, c0, c1, c2, c3, 95.0f);

        float d0 = FastPerceptualL(srcTex.SampleLevel(linearSamp, inp.uv + float2( step9.x,  0.0f), 0.0f).rgb);
        float d1 = FastPerceptualL(srcTex.SampleLevel(linearSamp, inp.uv + float2(-step9.x,  0.0f), 0.0f).rgb);
        float d2 = FastPerceptualL(srcTex.SampleLevel(linearSamp, inp.uv + float2( 0.0f,  step9.y), 0.0f).rgb);
        float d3 = FastPerceptualL(srcTex.SampleLevel(linearSamp, inp.uv + float2( 0.0f, -step9.y), 0.0f).rgb);
        float mean9 = BilateralMean4(rawL, d0, d1, d2, d3, 75.0f);

        float meanClarityL = 0.45f * mean4 + 0.55f * mean9;
        float detail = clamp(rawL - meanClarityL, -0.28f, 0.28f);
        float clarityCoring = smoothstep(0.010f, 0.028f, abs(detail));
        float rawY = CalcLuma(SRGBToLinear(rawSrgb));
        float midMask = smoothstep(0.015f, 0.09f, rawY) * smoothstep(0.985f, 0.84f, rawY);
        float curY = max(CalcLuma(lin), 1e-6f);
        float curL = pow(curY, 1.0f / 3.0f);
        float targetL = max(curL + detail * clarityCoring * (1.45f * clarityLvl) * midMask, 0.0f);
        float targetY = targetL * targetL * targetL;
        lin *= (targetY / curY);
    }

    // 6. Unified AI Dynamic Vibrance (Haze-Free Black Floor + Adaptive Cave Clarity + Balanced Oklab Color Optimization)
    if ((uFlags & FLAG_ENABLE_VIBRANCE) != 0u && uIntensity > 0.001f) {
        float intLevel = pow(saturate(uIntensity), 0.72f);
        float satLevel = pow(saturate(uSaturationBoost), 0.75f);

        float3 lab = LinearRGBToOklab(max(lin, 0.0f));
        float L = max(lab.x, 0.0f);
        float Lc = saturate(L);
        float C0 = length(lab.yz);

        float3 preSrgb = rawSrgb;
        if ((uFlags & (FLAG_ENABLE_DARK_LIGHT | FLAG_ENABLE_BC | FLAG_ENABLE_DYNAMIC_HDR | FLAG_ENABLE_CLARITY)) != 0u ||
            (uFlags2 & FLAG2_ENABLE_GAMMA_EXP) != 0u) {
            float3 preNorm = max(lin, 0.0f);
            float prePeak = max(preNorm.r, max(preNorm.g, preNorm.b));
            float3 preUnit = (prePeak > 1.0f) ? (preNorm / prePeak) : preNorm;
            preSrgb = LinearToSRGB(preUnit);
        }
        float preMax = max(preSrgb.r, max(preSrgb.g, preSrgb.b));
        float preMin = min(preSrgb.r, min(preSrgb.g, preSrgb.b));
        float preSat = (preMax > 1e-4f) ? saturate((preMax - preMin) / preMax) : 0.0f;

        // Relaxed highlight skin factor so sunlit faces glow naturally without burning out
        float skinMask0 = 1.0f;
        if ((uFlags & FLAG_SKIN_PROTECTION) != 0u && C0 > 1e-4f) {
            float hue0 = atan2(lab.z, lab.y);
            float dHue0 = abs(hue0 - 0.85f);
            dHue0 = min(dHue0, 6.2831853f - dHue0);
            float hueW0 = exp(-(dHue0 * dHue0) / (2.0f * 0.38f * 0.38f));
            float chromaW0 = smoothstep(0.015f, 0.045f, C0) * (1.0f - smoothstep(0.14f, 0.24f, C0));
            skinMask0 = lerp(1.0f, 0.68f, hueW0 * chromaW0);
        }

        // Dual-Scale Neighborhood Lightness (Fine 2px detail + Wide 14px 3D depth pop)
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

        float sceneDark = 0.35f;
        float caveDark = 0.0f;
        if ((uFlags & FLAG_ADAPTIVE_SCENE) != 0u) {
            float lumaDark = saturate((0.18f - stats.avgLuma) / 0.16f);
            float ratioDark = saturate((stats.darkRatio - 0.30f) / 0.55f);
            sceneDark = saturate(max(lumaDark, ratioDark));
            caveDark = saturate((0.032f - stats.avgLuma) / 0.026f);
        }

        // Black floor stays ink-black (zero milky fog), while pitch-dark caves & midtones lift cleanly
        float sCurveL = Lc * Lc * (3.0f - 2.0f * Lc);
        float contrastScale = lerp(1.0f, 0.32f, sceneDark);
        float sCurveMask = smoothstep(0.16f, 0.30f, Lc) * smoothstep(0.98f, 0.80f, Lc);
        float sCurveDelta = (sCurveL - Lc) * (0.10f * intLevel * contrastScale) * sCurveMask;

        // Micro-dehaze strictly below shadow detail so pure blacks stay rich without darkening caves
        float hazeBand = smoothstep(0.10f, 0.15f, Lc) * (1.0f - smoothstep(0.18f, 0.24f, Lc));
        float dehazeDelta = -0.005f * intLevel * hazeBand;

        // Pitch-dark cave clarity recovery (0 lift below Lc=0.118 so void blacks stay pure black)
        float caveBand = smoothstep(0.118f, 0.170f, Lc) * smoothstep(0.62f, 0.28f, Lc);
        float caveLift = (0.135f * caveDark) * intLevel * caveBand;

        // Headroom guard prevents bright saturated reds/blues/cyans near gamut ceiling from washing out pink/white
        float domHeadroom = 1.0f - smoothstep(0.68f, 0.96f, preMax) * smoothstep(0.06f, 0.16f, C0);

        // Adaptive dark-scene mid-shadow clarity lift (0 lift below Lc=0.14 -> zero white fog!)
        float darkLiftBand = smoothstep(0.14f, 0.28f, Lc) * smoothstep(0.76f, 0.44f, Lc);
        float adaptiveDarkLift = (0.018f + 0.064f * sceneDark) * intLevel * darkLiftBand * domHeadroom;

        // Clean midtone luminance lift for vibrant daylight & lit surfaces
        float midLiftBand = smoothstep(0.24f, 0.44f, Lc) * smoothstep(0.95f, 0.70f, Lc);
        float midLift = 0.044f * intLevel * midLiftBand * domHeadroom;

        // Noise-Free Dual-Scale Clarity (higher coring gate suppresses grainy speckles / bintik-bintik)
        float fineDiff = rawL - fineMeanL;
        float fineCoring = smoothstep(0.024f, 0.068f, abs(fineDiff));
        float fineSafe = (fineDiff * fineCoring) / (1.0f + abs(fineDiff) * 12.0f);
        float fineMask = smoothstep(0.16f, 0.30f, Lc) * smoothstep(0.99f, 0.86f, Lc);
        float fineBoost = fineSafe * (0.24f * intLevel) * fineMask;

        float wideDiff = rawL - wideMeanL;
        float wideCoring = smoothstep(0.014f, 0.045f, abs(wideDiff));
        float wideSafe = (wideDiff * wideCoring) / (1.0f + abs(wideDiff) * 7.5f);
        float wideAdj = (wideSafe < 0.0f) ? (wideSafe * lerp(0.30f, 0.75f, smoothstep(0.20f, 0.54f, Lc))) : wideSafe;
        float wideMask = smoothstep(lerp(0.18f, 0.125f, caveDark), lerp(0.34f, 0.22f, caveDark), Lc) * smoothstep(0.99f, 0.84f, Lc);
        float wideBoost = wideAdj * (0.32f * intLevel) * wideMask;

        // Clean Specular & Color Brilliance (vivid glow without noisy pixel sparkle)
        float specBand = smoothstep(0.36f, 0.68f, Lc) * smoothstep(0.99f, 0.86f, Lc);
        float chromaGlow = smoothstep(0.032f, 0.14f, C0);
        float specColorGlow = 0.056f * intLevel * specBand * chromaGlow * (0.45f + 0.55f * satLevel) * skinMask0 * domHeadroom;
        float specEdge = max(fineDiff, 0.0f) * smoothstep(0.045f, 0.11f, fineDiff);
        float specSparkle = (specEdge / (1.0f + specEdge * 14.0f)) * (0.06f * intLevel) * smoothstep(0.56f, 0.80f, Lc) * smoothstep(0.995f, 0.90f, Lc);

        float L_new = max(L + sCurveDelta + dehazeDelta + caveLift + adaptiveDarkLift + midLift + fineBoost + wideBoost + specColorGlow + specSparkle, 0.0f);
        if (L_new > 0.90f && L <= 1.0f) {
            float over = (L_new - 0.90f) / 0.10f;
            L_new = 0.90f + 0.10f * (over / (1.0f + 0.38f * over));
        }

        float liftMinL = lerp(0.14f, 0.125f, caveDark);
        float liftRatio = (L > liftMinL && L_new > L) ? min(L_new / L, 1.42f) : 1.0f;
        float liftChromaScale = 0.70f * (1.0f - 0.75f * smoothstep(0.55f, 0.88f, preSat));
        lab.yz *= lerp(1.0f, liftRatio, liftChromaScale * smoothstep(liftMinL, liftMinL + 0.14f, L));
        lab.x = L_new;

        // Balanced Oklab Dynamic Vibrance + Progressive RTX-Style Warm Golden-Tan Skin Tone
        if (uSaturationBoost > 0.001f) {
            float C = length(lab.yz);
            if (C > 1e-4f) {
                float satDrive = pow(saturate(uSaturationBoost), 0.68f) * lerp(0.68f, 1.22f, intLevel);
                float sceneColorAdapt = 1.0f;
                if ((uFlags & FLAG_ADAPTIVE_SCENE) != 0u) {
                    float rawAdapt = lerp(1.16f, 0.66f, saturate((stats.avgChroma - 0.04f) / 0.10f));
                    float brightRich = smoothstep(0.16f, 0.32f, stats.avgLuma) * smoothstep(0.065f, 0.125f, stats.avgChroma);
                    rawAdapt *= (1.0f - 0.18f * brightRich);
                    sceneColorAdapt = lerp(1.00f, rawAdapt, smoothstep(0.015f, 0.065f, stats.avgLuma));
                }

                float mutedWeight = 1.0f / (1.0f + C * 2.8f);
                float neutralGuard = smoothstep(0.012f, 0.036f, C);
                float lumaProtect = smoothstep(0.14f, 0.34f, lab.x) * smoothstep(0.96f, 0.76f, lab.x);

                float hue = atan2(lab.z, lab.y);
                float skinW = 0.0f;
                float skinMask = 1.0f;
                if ((uFlags & FLAG_SKIN_PROTECTION) != 0u) {
                    float dHue = abs(hue - 0.85f);
                    dHue = min(dHue, 6.2831853f - dHue);
                    float hueWeight = exp(-(dHue * dHue) / (2.0f * 0.38f * 0.38f));
                    float chromaWeight = smoothstep(0.015f, 0.045f, C) * (1.0f - smoothstep(0.14f, 0.24f, C));
                    skinW = hueWeight * chromaWeight;
                    float skinFloor = lerp(0.34f, 0.46f, smoothstep(0.30f, 1.0f, satLevel));
                    skinMask = lerp(1.0f, skinFloor, skinW);
                }

                // Gamut-headroom + warm red/gold & cool cyan-blue roll-off protects sunlit skies, dirt, and interiors
                float gamutHeadroom = 1.0f - 0.82f * smoothstep(0.58f, 0.90f, preSat);
                float dRedHue = abs(hue - 0.62f);
                dRedHue = min(dRedHue, 6.2831853f - dRedHue);
                float warmRedBand = exp(-(dRedHue * dRedHue) / (2.0f * 0.42f * 0.42f));
                float warmRedGuard = 1.0f - 0.68f * warmRedBand * smoothstep(0.075f, 0.145f, C) * smoothstep(0.52f, 0.80f, preSat);

                float dCyanHue = abs(hue - (-2.12f));
                dCyanHue = min(dCyanHue, 6.2831853f - dCyanHue);
                float cyanBlueBand = exp(-(dCyanHue * dCyanHue) / (2.0f * 0.52f * 0.52f));
                float coolCyanBlueGuard = 1.0f - 0.86f * cyanBlueBand * smoothstep(0.038f, 0.088f, C) * smoothstep(0.36f, 0.68f, preSat);

                float dGoldHue = abs(hue - 1.62f);
                dGoldHue = min(dGoldHue, 6.2831853f - dGoldHue);
                float warmGoldBand = exp(-(dGoldHue * dGoldHue) / (2.0f * 0.36f * 0.36f));
                float warmGoldGuard = 1.0f - 0.74f * warmGoldBand * smoothstep(0.055f, 0.115f, C) * smoothstep(0.44f, 0.74f, preSat);

                float highChromaGuard = 1.0f - 0.68f * smoothstep(0.14f, 0.23f, C);
                float highlightPop = 1.0f + 0.12f * smoothstep(0.48f, 0.70f, lab.x) * smoothstep(0.92f, 0.76f, lab.x) * skinMask;
                float boost = 1.68f * satDrive * sceneColorAdapt * mutedWeight * neutralGuard * lumaProtect * skinMask * gamutHeadroom * warmRedGuard * coolCyanBlueGuard * warmGoldGuard * highChromaGuard * highlightPop;
                float maxChromaCap = (uHdrOutputMode == 0u) ? 0.265f : 0.38f;
                if (uHdrOutputMode == 0u) {
                    float cyanCap = 0.088f + 0.024f * smoothstep(0.42f, 0.60f, lab.x) * smoothstep(0.88f, 0.68f, lab.x);
                    maxChromaCap = max(C, lerp(maxChromaCap, cyanCap, cyanBlueBand * smoothstep(0.34f, 0.64f, preSat)));
                    maxChromaCap = max(C, lerp(maxChromaCap, 0.124f, warmGoldBand * smoothstep(0.46f, 0.76f, preSat)));
                }
                float newC = min(C * (1.0f + boost), maxChromaCap);
                lab.yz *= (newC / C);

                // Subtle warm golden-tan refinement on skin tones at medium-high Saturation (matching RTX Vibrance 100%)
                if ((uFlags & FLAG_SKIN_PROTECTION) != 0u && skinW > 0.01f && satLevel > 0.25f) {
                    float targetHue = 1.14f;
                    if (hue < targetHue) {
                        float maxShift = 0.18f * smoothstep(0.25f, 1.0f, satLevel) * skinW * smoothstep(0.26f, 0.82f, lab.x);
                        float newHue = hue + min(targetHue - hue, maxShift);
                        lab.yz = float2(cos(newHue), sin(newHue)) * newC;
                    }
                    lab.x += 0.014f * smoothstep(0.35f, 1.0f, satLevel) * skinW * smoothstep(0.38f, 0.78f, lab.x);
                }
            }
        }

        float3 vibLin = OklabToLinearRGB(lab);
        if (uHdrOutputMode == 0u) {
            uint postMask2 = FLAG2_ENABLE_AO | FLAG2_ENABLE_SHARPEN | FLAG2_ENABLE_BLOOM |
                             FLAG2_ENABLE_FILMIC | FLAG2_ENABLE_COLOR_TEMP |
                             FLAG2_ENABLE_NIGHT_MODE | FLAG2_ENABLE_BW | FLAG2_ENABLE_VIGNETTE | FLAG2_ENABLE_COLORBLIND;
            lin = ((uFlags2 & postMask2) != 0u) ? SoftGamutCompress(vibLin) : vibLin;
        } else {
            lin = ClampToBT2020InRec709(vibLin);
        }
    }

    // 4C. Standalone Filter #13: Ambient Occlusion (Screen-Space Symmetric Crevice & Contact Depth)
    if ((uFlags2 & FLAG2_ENABLE_AO) != 0u && uAoIntensity > 0.001f) {
        float aoLvl = pow(saturate(uAoIntensity), 0.85f);
        float radScale = lerp(0.50f, 1.80f, saturate(uAoRadius));
        float2 invRes = 1.0f / float2(max(uWidth, 1.0f), max(uHeight, 1.0f));
        float cL = FastPerceptualL(rawSrgb);

        float2 r1 = 3.5f * radScale * invRes;
        float f0 = FastPerceptualL(srcTex.SampleLevel(linearSamp, inp.uv + float2( r1.x,  r1.y), 0.0f).rgb);
        float f1 = FastPerceptualL(srcTex.SampleLevel(linearSamp, inp.uv + float2(-r1.x, -r1.y), 0.0f).rgb);
        float f2 = FastPerceptualL(srcTex.SampleLevel(linearSamp, inp.uv + float2(-r1.x,  r1.y), 0.0f).rgb);
        float f3 = FastPerceptualL(srcTex.SampleLevel(linearSamp, inp.uv + float2( r1.x, -r1.y), 0.0f).rgb);
        float occFine = 0.5f * (AoPairOcclusion(cL, f0, f1) + AoPairOcclusion(cL, f2, f3));

        float2 r2 = 12.0f * radScale * invRes;
        float2 r2d = r2 * 0.7071f;
        float bL = FastPerceptualL(srcTex.SampleLevel(linearSamp, inp.uv + float2(-r2.x,  0.0f),  0.0f).rgb);
        float bR = FastPerceptualL(srcTex.SampleLevel(linearSamp, inp.uv + float2( r2.x,  0.0f),  0.0f).rgb);
        float bU = FastPerceptualL(srcTex.SampleLevel(linearSamp, inp.uv + float2( 0.0f, -r2.y),  0.0f).rgb);
        float bD = FastPerceptualL(srcTex.SampleLevel(linearSamp, inp.uv + float2( 0.0f,  r2.y),  0.0f).rgb);
        float bD0 = FastPerceptualL(srcTex.SampleLevel(linearSamp, inp.uv + float2( r2d.x,  r2d.y), 0.0f).rgb);
        float bD1 = FastPerceptualL(srcTex.SampleLevel(linearSamp, inp.uv + float2(-r2d.x, -r2d.y), 0.0f).rgb);
        float bD2 = FastPerceptualL(srcTex.SampleLevel(linearSamp, inp.uv + float2(-r2d.x,  r2d.y), 0.0f).rgb);
        float bD3 = FastPerceptualL(srcTex.SampleLevel(linearSamp, inp.uv + float2( r2d.x, -r2d.y), 0.0f).rgb);
        float occBroad = 0.25f * (
            AoPairOcclusion(cL, bL, bR) +
            AoPairOcclusion(cL, bU, bD) +
            AoPairOcclusion(cL, bD0, bD1) +
            AoPairOcclusion(cL, bD2, bD3)
        );

        float occRaw = (occFine * 0.55f + occBroad * 0.45f) * 3.6f;
        float occ = occRaw / (1.0f + occRaw * 0.45f);

        float highlightGuard = smoothstep(0.92f, 0.56f, cL);
        float blackGuard = smoothstep(0.04f, 0.14f, cL);

        float skinAoGuard = 1.0f;
        float3 rawLab = LinearRGBToOklab(SRGBToLinear(rawSrgb));
        float rawC = length(rawLab.yz);
        if (rawC > 1e-4f) {
            float rawHue = atan2(rawLab.z, rawLab.y);
            float dHueAo = abs(rawHue - 0.85f);
            dHueAo = min(dHueAo, 6.2831853f - dHueAo);
            float hueWAo = exp(-(dHueAo * dHueAo) / (2.0f * 0.38f * 0.38f));
            float chromaWAo = smoothstep(0.015f, 0.045f, rawC) * (1.0f - smoothstep(0.14f, 0.24f, rawC));
            skinAoGuard = lerp(1.0f, 0.48f, hueWAo * chromaWAo);
        }

        float aoDarken = saturate(occ * 0.78f * aoLvl * highlightGuard * blackGuard * skinAoGuard);
        lin *= (1.0f - aoDarken);
    }

    // 5. Sharpen+ (AMD FidelityFX CAS in Perceptual Luma, halo-free across shadows & highlights)
    if ((uFlags2 & FLAG2_ENABLE_SHARPEN) != 0u && uSharpen > 0.001f) {
        float2 px1 = 1.0f / float2(max(uWidth, 1.0f), max(uHeight, 1.0f));
        float3 sc = saturate(rawSrgb);
        float3 sn = saturate(srcTex.SampleLevel(linearSamp, inp.uv + float2( 0.0f, -px1.y), 0.0f).rgb);
        float3 ss = saturate(srcTex.SampleLevel(linearSamp, inp.uv + float2( 0.0f,  px1.y), 0.0f).rgb);
        float3 sw = saturate(srcTex.SampleLevel(linearSamp, inp.uv + float2(-px1.x,  0.0f), 0.0f).rgb);
        float3 se = saturate(srcTex.SampleLevel(linearSamp, inp.uv + float2( px1.x,  0.0f), 0.0f).rgb);

        float3 pW = float3(0.299f, 0.587f, 0.114f);
        float cP = dot(sc, pW);
        float nP = dot(sn, pW);
        float sP = dot(ss, pW);
        float wP = dot(sw, pW);
        float eP = dot(se, pW);

        float mnL = min(cP, min(min(nP, sP), min(wP, eP)));
        float mxL = max(cP, max(max(nP, sP), max(wP, eP)));
        float amp = sqrt(saturate(min(mnL, 2.0f - mxL) / max(mxL, 1e-4f)));
        float sharpNorm = saturate(uSharpen);
        float peak = -1.0f / lerp(8.0f, 4.1f, sharpNorm);
        float wCas = amp * peak;
        float sharpP = (cP + (nP + sP + wP + eP) * wCas) / (1.0f + 4.0f * wCas);
        float diffP = sharpP - cP;
        float coring = smoothstep(0.011f, 0.032f, abs(diffP));
        float deltaP = clamp(diffP * coring * lerp(0.65f, 1.75f, sharpNorm), -0.26f, 0.26f);
        float curY = max(CalcLuma(lin), 1e-6f);
        float curP = pow(curY, 1.0f / 2.2f);
        float targetY = pow(max(curP + deltaP, 0.0f), 2.2f);
        lin *= (targetY / curY);
    }

    // 5B. Cinematic Bloom (Dual-Ring Soft-Knee Optical Highlight Glow)
    if ((uFlags2 & FLAG2_ENABLE_BLOOM) != 0u && uBloomIntensity > 0.001f) {
        float bloomLvl = pow(saturate(uBloomIntensity), 0.90f);
        float thresh = lerp(0.35f, 0.92f, saturate(uBloomThreshold));
        float knee = 0.22f;
        float2 invRes = 1.0f / float2(max(uWidth, 1.0f), max(uHeight, 1.0f));
        float2 r1 = 5.0f * invRes;
        float2 r2 = 13.0f * invRes;

        float3 b0 = SRGBToLinear(srcTex.SampleLevel(linearSamp, inp.uv + float2( r1.x,  r1.y), 0.0f).rgb);
        float3 b1 = SRGBToLinear(srcTex.SampleLevel(linearSamp, inp.uv + float2(-r1.x,  r1.y), 0.0f).rgb);
        float3 b2 = SRGBToLinear(srcTex.SampleLevel(linearSamp, inp.uv + float2( r1.x, -r1.y), 0.0f).rgb);
        float3 b3 = SRGBToLinear(srcTex.SampleLevel(linearSamp, inp.uv + float2(-r1.x, -r1.y), 0.0f).rgb);
        float3 o0 = SRGBToLinear(srcTex.SampleLevel(linearSamp, inp.uv + float2( r2.x,  0.0f), 0.0f).rgb);
        float3 o1 = SRGBToLinear(srcTex.SampleLevel(linearSamp, inp.uv + float2(-r2.x,  0.0f), 0.0f).rgb);
        float3 o2 = SRGBToLinear(srcTex.SampleLevel(linearSamp, inp.uv + float2( 0.0f,  r2.y), 0.0f).rgb);
        float3 o3 = SRGBToLinear(srcTex.SampleLevel(linearSamp, inp.uv + float2( 0.0f, -r2.y), 0.0f).rgb);

        float3 innerSum =
            b0 * smoothstep(thresh - knee, thresh + knee, max(b0.r, max(b0.g, b0.b))) +
            b1 * smoothstep(thresh - knee, thresh + knee, max(b1.r, max(b1.g, b1.b))) +
            b2 * smoothstep(thresh - knee, thresh + knee, max(b2.r, max(b2.g, b2.b))) +
            b3 * smoothstep(thresh - knee, thresh + knee, max(b3.r, max(b3.g, b3.b)));
        float3 outerSum =
            o0 * smoothstep(thresh - knee, thresh + knee, max(o0.r, max(o0.g, o0.b))) +
            o1 * smoothstep(thresh - knee, thresh + knee, max(o1.r, max(o1.g, o1.b))) +
            o2 * smoothstep(thresh - knee, thresh + knee, max(o2.r, max(o2.g, o2.b))) +
            o3 * smoothstep(thresh - knee, thresh + knee, max(o3.r, max(o3.g, o3.b)));

        float3 bloomCol = (innerSum * 0.62f + outerSum * 0.38f) * 0.25f;
        lin = max(lin + bloomCol * (0.85f * bloomLvl) * saturate(1.25f - lin * 0.35f), 0.0f);
    }

    // 5C. Filmic Grading (0 = Academy ACES, 1 = Blender AgX, 2 = 3-Strip Technicolor)
    if ((uFlags2 & FLAG2_ENABLE_FILMIC) != 0u && uFilmicStrength > 0.001f) {
        float fStr = saturate(uFilmicStrength);
        float3 srcLin = max(lin, 0.0f);
        float3 graded = srcLin;
        if (uFilmicMode == 0u) {
            float k = 0.55f;
            float3 x = srcLin * k;
            float wn = (k * (2.51f * k + 0.03f)) / (k * (2.43f * k + 0.59f) + 0.14f);
            graded = saturate(((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f)) / wn);
        } else if (uFilmicMode == 1u) {
            float3 agxIn = float3(
                0.84247906f * srcLin.r + 0.07843360f * srcLin.g + 0.07922375f * srcLin.b,
                0.04232824f * srcLin.r + 0.87846864f * srcLin.g + 0.07916613f * srcLin.b,
                0.04237565f * srcLin.r + 0.07843360f * srcLin.g + 0.87914297f * srcLin.b
            );
            float3 p = pow(max(agxIn, 0.0f), 1.0f / 2.2f);
            float3 s = p * p * (3.0f - 2.0f * p);
            float3 m = saturate(lerp(p, s, 0.55f));
            float3 agxLin = pow(m, 2.2f);
            graded = saturate(float3(
                 1.08f * agxLin.r - 0.04f * agxLin.g - 0.04f * agxLin.b,
                -0.02f * agxLin.r + 1.06f * agxLin.g - 0.04f * agxLin.b,
                -0.02f * agxLin.r - 0.04f * agxLin.g + 1.06f * agxLin.b
            ));
        } else {
            float3 s = LinearToSRGB(saturate(srcLin));
            float3 m = saturate(float3(
                 1.18f * s.r - 0.12f * s.g - 0.06f * s.b,
                -0.06f * s.r + 1.14f * s.g - 0.08f * s.b,
                -0.04f * s.r - 0.08f * s.g + 1.12f * s.b
            ));
            float3 sc = m * m * (3.0f - 2.0f * m);
            graded = SRGBToLinear(lerp(m, sc, 0.32f));
        }
        lin = lerp(srcLin, graded, fStr);
    }

    // 6-8 old post-Vibrance BC & Gamma removed (moved before AI Dynamic Vibrance for true Oklab re-optimization)
    // 9. Color Temperature & Tint (50 = exact neutral, luma-preserving)
    if ((uFlags2 & FLAG2_ENABLE_COLOR_TEMP) != 0u) {
        float temp = (uTemperature - 0.5f) * 2.0f;
        float tintVal = (uTint - 0.5f) * 2.0f;
        float3 wbGain = max(float3(
            1.0f + 0.45f * temp + 0.22f * tintVal,
            1.0f - 0.32f * tintVal,
            1.0f - 0.48f * temp + 0.22f * tintVal
        ), 0.10f);
        float oldY = max(CalcLuma(lin), 1e-5f);
        lin *= wbGain;
        float newY = max(CalcLuma(lin), 1e-5f);
        lin *= (oldY / newY);
    }

    // 9. Night Mode / Blue Light Filter
    if ((uFlags2 & FLAG2_ENABLE_NIGHT_MODE) != 0u && uNightMode > 0.001f) {
        float nm = saturate(uNightMode);
        float3 nightMult = float3(1.0f, lerp(1.0f, 0.68f, nm), lerp(1.0f, 0.20f, nm));
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
            shift = float3(0.0f, 1.0f * errRgb.r, 1.0f * errRgb.r);
        } else if (uColorblindType == 1u) {
            shift = float3(1.0f * errRgb.g, 0.0f, 1.0f * errRgb.g);
        } else {
            shift = float3(1.0f * errRgb.b, 1.0f * errRgb.b, 0.0f);
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
        float r2 = saturate(dot(dUv, dUv) * 2.4f);
        lin *= lerp(1.0f, 1.0f - 0.92f * pow(r2, 1.25f), saturate(uVignette));
    }

    // 2B. True HDR10 / scRGB Inverse Tone Mapping & Highlight Expansion (when Windows HDR is ON)
    if (uHdrOutputMode != 0u) {
        float sdrScale = max(uSdrWhiteNits, 80.0f) / 80.0f;
        float peakScale = max(clamp(uHdrPeakNits, 300.0f, 1500.0f) / 80.0f, sdrScale * 1.25f);
        if ((uFlags & FLAG_ENABLE_DYNAMIC_HDR) != 0u && uDynamicHdr > 0.001f) {
            float hdrLvl = pow(saturate(uDynamicHdr), 0.80f);
            float y = max(CalcLuma(lin), 1e-5f);
            float midGamma = exp2((0.5f - saturate(uHdrMidGray)) * 0.85f);
            float yAdj = pow(saturate(y), midGamma) * (1.0f + 0.14f * hdrLvl * pow(saturate(1.0f - saturate(y)), 2.0f));

            float2 hdrStep = 12.0f / float2(max(uWidth, 1.0f), max(uHeight, 1.0f));
            float ly0 = CalcLuma(SRGBToLinear(srcTex.SampleLevel(linearSamp, inp.uv + float2( hdrStep.x,  0.0f), 0.0f).rgb));
            float ly1 = CalcLuma(SRGBToLinear(srcTex.SampleLevel(linearSamp, inp.uv + float2(-hdrStep.x,  0.0f), 0.0f).rgb));
            float ly2 = CalcLuma(SRGBToLinear(srcTex.SampleLevel(linearSamp, inp.uv + float2( 0.0f,  hdrStep.y), 0.0f).rgb));
            float ly3 = CalcLuma(SRGBToLinear(srcTex.SampleLevel(linearSamp, inp.uv + float2( 0.0f, -hdrStep.y), 0.0f).rgb));
            float localY = 0.25f * (ly0 + ly1 + ly2 + ly3);
            float localPeak = saturate((y - localY * 0.55f) / 0.45f);

            float t = smoothstep(0.28f, 1.0f, saturate(y));
            float headroom = max(peakScale / sdrScale - 1.0f, 0.25f);
            float expand = 1.0f + headroom * pow(t, lerp(2.15f, 1.30f, hdrLvl)) * (0.35f + 0.65f * hdrLvl) * lerp(0.75f, 1.18f, localPeak);
            float yHdr = yAdj * sdrScale * expand;
            if (yHdr > 0.85f * peakScale) {
                float over = (yHdr - 0.85f * peakScale) / max(0.15f * peakScale, 1e-4f);
                yHdr = 0.85f * peakScale + 0.15f * peakScale * (over / (1.0f + over));
            }
            float3 linHdr = max(lin * (yHdr / y), 0.0f);
            float hlChromaBoost = 0.18f * hdrLvl * smoothstep(0.35f, 0.90f, saturate(y));
            linHdr = max(lerp(float3(yHdr, yHdr, yHdr), linHdr, 1.0f + hlChromaBoost), 0.0f);
            return FinalizeOutput(linHdr, inp.pos.xy, true);
        }
        return FinalizeOutput(lin, inp.pos.xy, false);
    }

    return FinalizeOutput(lin, inp.pos.xy, false);
}
)HLSL";

inline std::string GenerateReShadeFx(const FilterParams& p) {
    std::ostringstream ss;
    ss << "// AI Dynamic Vibrance Enhance - ReShade Shader (Windows ReShade & Linux vkBasalt)\n";
    ss << "// Safely removable anytime via the 'Remove .fx' button or uninstall script.\n\n";
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
    ss << "uniform bool UI_EnableAO <\n"
          "    ui_label = \"Enable Ambient Occlusion\";\n"
          "> = " << (p.enableAmbientOcclusion ? "true" : "false") << ";\n\n";
    ss << "uniform float UI_AOIntensity <\n"
          "    ui_type = \"slider\";\n"
          "    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;\n"
          "    ui_label = \"AO Intensity (%)\";\n"
          "> = " << p.ambientOcclusion << ".0;\n\n";
    ss << "uniform float UI_AORadius <\n"
          "    ui_type = \"slider\";\n"
          "    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;\n"
          "    ui_label = \"AO Radius (%)\";\n"
          "> = " << p.aoRadius << ".0;\n\n";
    ss << "uniform bool UI_AOUseDepth <\n"
          "    ui_label = \"Use 3D Depth Buffer SSAO (auto fallback if unavailable)\";\n"
          "> = true;\n\n";
    ss << "uniform bool UI_EnableDynamicHdr <\n"
          "    ui_label = \"Enable Dynamic HDR Tone\";\n"
          "> = " << (p.enableDynamicHdr ? "true" : "false") << ";\n\n";
    ss << "uniform float UI_DynamicHdr <\n"
          "    ui_type = \"slider\";\n"
          "    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;\n"
          "    ui_label = \"Dynamic HDR tone (%)\";\n"
          "> = " << p.dynamicHdr << ".0;\n\n";
    ss << "uniform float UI_HdrMidGray <\n"
          "    ui_type = \"slider\";\n"
          "    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;\n"
          "    ui_label = \"HDR Mid Gray / Contrast (%)\";\n"
          "> = " << p.hdrMidGray << ".0;\n\n";
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
    ss << "uniform bool UI_EnableBloom <\n"
          "    ui_label = \"Enable Cinematic Bloom\";\n"
          "> = " << (p.enableBloom ? "true" : "false") << ";\n\n";
    ss << "uniform float UI_BloomIntensity <\n"
          "    ui_type = \"slider\";\n"
          "    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;\n"
          "    ui_label = \"Bloom Intensity (%)\";\n"
          "> = " << p.bloomIntensity << ".0;\n\n";
    ss << "uniform float UI_BloomThreshold <\n"
          "    ui_type = \"slider\";\n"
          "    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;\n"
          "    ui_label = \"Bloom Threshold (%)\";\n"
          "> = " << p.bloomThreshold << ".0;\n\n";
    ss << "uniform bool UI_EnableFilmic <\n"
          "    ui_label = \"Enable Filmic Grading\";\n"
          "> = " << (p.enableFilmic ? "true" : "false") << ";\n\n";
    ss << "uniform int UI_FilmicMode <\n"
          "    ui_type = \"combo\";\n"
          "    ui_items = \"ACES\\0AgX\\0Technicolor\\0\";\n"
          "    ui_label = \"Filmic Curve Mode\";\n"
          "> = " << p.filmicMode << ";\n\n";
    ss << "uniform float UI_FilmicStrength <\n"
          "    ui_type = \"slider\";\n"
          "    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;\n"
          "    ui_label = \"Filmic Grading Strength (%)\";\n"
          "> = " << p.filmicStrength << ".0;\n\n";
    ss << "uniform bool UI_EnableDeband <\n"
          "    ui_label = \"Enable Deband\";\n"
          "> = " << (p.enableDeband ? "true" : "false") << ";\n\n";
    ss << "uniform float UI_DebandStrength <\n"
          "    ui_type = \"slider\";\n"
          "    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;\n"
          "    ui_label = \"Deband Strength (%)\";\n"
          "> = " << p.debandStrength << ".0;\n\n";
    ss << "uniform bool UI_SplitPreview <\n"
          "    ui_label = \"Enable Split Preview (Before / After)\";\n"
          "> = false;\n\n";
    ss << "uniform float UI_SplitPosition <\n"
          "    ui_type = \"slider\";\n"
          "    ui_min = 5.0; ui_max = 95.0; ui_step = 1.0;\n"
          "    ui_label = \"Split Preview Position (%)\";\n"
          "> = 50.0;\n\n";

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
    float y = max(dot(saturate(srgb), float3(0.2126, 0.7152, 0.0722)), 0.0);
    return sqrt(y * sqrt(y));
}

float ADV_BilateralMean4(float c, float n0, float n1, float n2, float n3, float invSigma2) {
    float d0 = n0 - c; float w0 = 1.0 / (1.0 + d0 * d0 * invSigma2);
    float d1 = n1 - c; float w1 = 1.0 / (1.0 + d1 * d1 * invSigma2);
    float d2 = n2 - c; float w2 = 1.0 / (1.0 + d2 * d2 * invSigma2);
    float d3 = n3 - c; float w3 = 1.0 / (1.0 + d3 * d3 * invSigma2);
    return (c + n0 * w0 + n1 * w1 + n2 * w2 + n3 * w3) / (1.0 + w0 + w1 + w2 + w3);
}

float ADV_AoPairOcclusion(float cL, float aL, float bL) {
    float pairMin = min(aL, bL);
    float pairAvg = 0.5 * (aL + bL);
    float symLift = max(pairMin - cL, 0.0);
    float cornerGate = smoothstep(0.0, 0.055, pairMin - cL + 0.025);
    float cornerLift = max(pairAvg - cL, 0.0) * cornerGate;
    float edgeSuppress = 1.0 / (1.0 + abs(aL - bL) * 4.5);
    return (0.72 * symLift + 0.28 * cornerLift) * edgeSuppress;
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
        float3 satPreserve = c / maxC;
        float3 lumaPreserve = lerp(float3(y, y, y), c, saturate((1.0 - y) / max(maxC - y, 1e-5)));
        float whiteRoll = smoothstep(0.55, 0.92, y);
        c = lerp(satPreserve, lumaPreserve, whiteRoll);
    }
    return saturate(c);
}

float4 PS_AIDynamicVibrance(float4 vpos : SV_Position, float2 texcoord : TEXCOORD) : SV_Target {
    float3 origSrgb = tex2D(ReShade::BackBuffer, texcoord).rgb;
    float3 rawSrgb = origSrgb;
    if (!UI_EnableDynamicVibrance && !UI_EnableDarkLighting && !UI_EnableAO &&
        !UI_EnableDynamicHdr && !UI_EnableClarity && !UI_EnableBC &&
        !UI_EnableSharpen && !UI_EnableGammaExp && !UI_EnableColorTemp &&
        !UI_EnableNightMode && !UI_EnableColorblind && !UI_EnableBW && !UI_EnableVignette &&
        !UI_EnableBloom && !UI_EnableFilmic && !UI_EnableDeband) {
        return float4(origSrgb, 1.0);
    }

    if (UI_EnableDeband && UI_DebandStrength > 0.01) {
        float dbLvl = saturate(UI_DebandStrength / 100.0);
        float r1 = frac(52.9829189 * frac(dot(vpos.xy, float2(0.06711056, 0.00583715))));
        float r2 = frac(52.9829189 * frac(dot(vpos.xy + float2(17.0, 31.0), float2(0.06711056, 0.00583715))));
        float rad = lerp(2.0, lerp(7.0, 16.0, dbLvl), r1);
        float ang = r2 * 6.2831853;
        float2 d1 = float2(cos(ang), sin(ang)) * rad * BUFFER_PIXEL_SIZE;
        float2 d2 = float2(-d1.y, d1.x);

        float3 s0 = tex2D(ReShade::BackBuffer, texcoord + d1).rgb;
        float3 s1 = tex2D(ReShade::BackBuffer, texcoord - d1).rgb;
        float3 s2 = tex2D(ReShade::BackBuffer, texcoord + d2).rgb;
        float3 s3 = tex2D(ReShade::BackBuffer, texcoord - d2).rgb;
        float3 avg = 0.25 * (s0 + s1 + s2 + s3);

        float3 diffAvg = abs(avg - rawSrgb);
        float3 diffMax = max(max(abs(s0 - rawSrgb), abs(s1 - rawSrgb)), max(abs(s2 - rawSrgb), abs(s3 - rawSrgb)));
        float metric = max(max(diffAvg.r, max(diffAvg.g, diffAvg.b)),
                           0.55 * max(diffMax.r, max(diffMax.g, diffMax.b)));
        float thresh = lerp(0.010, 0.036, dbLvl);
        float wDb = smoothstep(thresh, thresh * 0.28, metric);
        rawSrgb = lerp(rawSrgb, avg, wDb * lerp(0.45, 0.92, dbLvl));
    }

    float3 lin = ADV_SRGBToLinear(rawSrgb);

    if (UI_EnableDarkLighting && UI_DarkLighting > 0.01) {
        float darkLvl = pow(saturate(UI_DarkLighting / 100.0), 0.85);
        float2 crvStep = BUFFER_PIXEL_SIZE * 3.0;
        float crvCenterL = ADV_FastPerceptualL(rawSrgb);
        float crv0 = ADV_FastPerceptualL(tex2D(ReShade::BackBuffer, texcoord + float2( crvStep.x,  crvStep.y)).rgb);
        float crv1 = ADV_FastPerceptualL(tex2D(ReShade::BackBuffer, texcoord + float2(-crvStep.x,  crvStep.y)).rgb);
        float crv2 = ADV_FastPerceptualL(tex2D(ReShade::BackBuffer, texcoord + float2( crvStep.x, -crvStep.y)).rgb);
        float crv3 = ADV_FastPerceptualL(tex2D(ReShade::BackBuffer, texcoord + float2(-crvStep.x, -crvStep.y)).rgb);
        float crvAvg = 0.25 * (crv0 + crv1 + crv2 + crv3);
        float creviceKeep = saturate((crvAvg - crvCenterL - 0.012) / 0.14);

        float y = max(ADV_Luma(lin), 1e-6);
        float toe = smoothstep(0.0014, 0.0045, y);
        float shadowStrength = darkLvl * lerp(1.0, 0.40, creviceKeep * darkLvl);
        float shadowPow = lerp(1.0, 0.42, saturate(shadowStrength * 0.78));
        float yLifted = pow(saturate(y), shadowPow);
        float shadowBlend = toe * pow(saturate(1.0 - saturate(y)), 1.65);
        float targetY = lerp(y, max(yLifted, y), shadowBlend * saturate(shadowStrength));
        float effGain = targetY / y;
        float3 lifted = max(lin * effGain, 0.0);
        float liftAmt = saturate((effGain - 1.35) / 3.5) * smoothstep(0.05, 0.002, y);
        float liftY = ADV_Luma(lifted);
        lin = lerp(lifted, float3(liftY, liftY, liftY), 0.14 * liftAmt);
    }

    if (UI_EnableBC) {
        float yOld = max(ADV_Luma(lin), 1e-6);
        float brightScale = exp2(((UI_Brightness / 100.0) - 0.5) * 2.2);
        float cNorm = saturate(UI_Contrast / 100.0);
        float contrastGain = (cNorm >= 0.5) ? (1.0 + (cNorm - 0.5) * 2.0) : (0.40 + cNorm * 1.2);
        float yBright = yOld * brightScale;
        float yNew = max(0.18 * pow(max(yBright / 0.18, 1e-6), contrastGain), 0.0);
        lin = max(lin * (yNew / yOld), 0.0);
    }

    if (UI_EnableGammaExp) {
        float expEV = ((UI_Exposure / 100.0) - 0.5) * 3.0;
        lin *= exp2(expEV);
        float gammaExp = exp2((0.5 - (UI_Gamma / 100.0)) * 1.5);
        float yOld = max(ADV_Luma(lin), 1e-6);
        float yNew = pow(yOld, gammaExp);
        float3 chromPres = max(lin * (yNew / yOld), 0.0);
        float3 perChan = pow(max(lin, 0.0), gammaExp);
        lin = lerp(perChan, chromPres, 0.82);
    }

    if (UI_EnableDynamicHdr && UI_DynamicHdr > 0.01) {
        float hdrLvl = pow(saturate(UI_DynamicHdr / 100.0), 0.82);
        float y = max(ADV_Luma(lin), 1e-5);
        float2 hdrStep = BUFFER_PIXEL_SIZE * 12.0;
        float ly0 = ADV_Luma(ADV_SRGBToLinear(tex2D(ReShade::BackBuffer, texcoord + float2( hdrStep.x,  0.0)).rgb));
        float ly1 = ADV_Luma(ADV_SRGBToLinear(tex2D(ReShade::BackBuffer, texcoord + float2(-hdrStep.x,  0.0)).rgb));
        float ly2 = ADV_Luma(ADV_SRGBToLinear(tex2D(ReShade::BackBuffer, texcoord + float2( 0.0,  hdrStep.y)).rgb));
        float ly3 = ADV_Luma(ADV_SRGBToLinear(tex2D(ReShade::BackBuffer, texcoord + float2( 0.0, -hdrStep.y)).rgb));
        float localY = 0.25 * (ly0 + ly1 + ly2 + ly3);

        float midGamma = exp2((0.5 - saturate(UI_HdrMidGray / 100.0)) * 0.90);
        float yMid = pow(saturate(y), midGamma);
        float sCurve = yMid * yMid * (3.0 - 2.0 * yMid);
        float localContrast = clamp((y - localY) * 0.48, -0.14, 0.18) * smoothstep(0.02, 0.15, y);
        float yTone = lerp(yMid, sCurve, 0.85 * hdrLvl) + localContrast * hdrLvl;
        float shadowAnchor = 1.0 + 0.18 * hdrLvl * smoothstep(0.01, 0.08, yMid) * pow(saturate(1.0 - yMid), 2.2);
        float yTarget = max(yTone * shadowAnchor, 0.0);
        if (yTarget > 0.78) {
            float over = (yTarget - 0.78) / 0.22;
            yTarget = 0.78 + 0.22 * (over / (1.0 + 0.32 * hdrLvl * over));
        }
        lin *= (yTarget / y);

        float hlSatMask = smoothstep(0.35, 0.82, y) * (1.0 - smoothstep(0.92, 1.0, y));
        float yNow = ADV_Luma(lin);
        lin = max(lerp(float3(yNow, yNow, yNow), lin, 1.0 + 0.20 * hdrLvl * hlSatMask), 0.0);
    }

    if (UI_EnableClarity && UI_Clarity > 0.01) {
        float clarityLvl = pow(saturate(UI_Clarity / 100.0), 0.85);
        float2 step4 = BUFFER_PIXEL_SIZE * 4.0;
        float2 step9 = BUFFER_PIXEL_SIZE * 9.0;

        float rawL = ADV_FastPerceptualL(rawSrgb);
        float c0 = ADV_FastPerceptualL(tex2D(ReShade::BackBuffer, texcoord + float2( step4.x,  step4.y)).rgb);
        float c1 = ADV_FastPerceptualL(tex2D(ReShade::BackBuffer, texcoord + float2(-step4.x,  step4.y)).rgb);
        float c2 = ADV_FastPerceptualL(tex2D(ReShade::BackBuffer, texcoord + float2( step4.x, -step4.y)).rgb);
        float c3 = ADV_FastPerceptualL(tex2D(ReShade::BackBuffer, texcoord + float2(-step4.x, -step4.y)).rgb);
        float mean4 = ADV_BilateralMean4(rawL, c0, c1, c2, c3, 95.0);

        float d0 = ADV_FastPerceptualL(tex2D(ReShade::BackBuffer, texcoord + float2( step9.x,  0.0)).rgb);
        float d1 = ADV_FastPerceptualL(tex2D(ReShade::BackBuffer, texcoord + float2(-step9.x,  0.0)).rgb);
        float d2 = ADV_FastPerceptualL(tex2D(ReShade::BackBuffer, texcoord + float2( 0.0,  step9.y)).rgb);
        float d3 = ADV_FastPerceptualL(tex2D(ReShade::BackBuffer, texcoord + float2( 0.0, -step9.y)).rgb);
        float mean9 = ADV_BilateralMean4(rawL, d0, d1, d2, d3, 75.0);

        float meanClarityL = 0.45 * mean4 + 0.55 * mean9;
        float detail = clamp(rawL - meanClarityL, -0.28, 0.28);
        float clarityCoring = smoothstep(0.010, 0.028, abs(detail));
        float rawY = ADV_Luma(ADV_SRGBToLinear(rawSrgb));
        float midMask = smoothstep(0.015, 0.09, rawY) * smoothstep(0.985, 0.84, rawY);
        float curY = max(ADV_Luma(lin), 1e-6);
        float curL = pow(curY, 1.0 / 3.0);
        float targetL = max(curL + detail * clarityCoring * (1.45 * clarityLvl) * midMask, 0.0);
        float targetY = targetL * targetL * targetL;
        lin *= (targetY / curY);
    }

    if (UI_EnableDynamicVibrance && UI_Intensity > 0.01) {
        float intLevel = pow(saturate(UI_Intensity / 100.0), 0.72);
        float satLevel = pow(saturate(UI_SaturationBoost / 100.0), 0.75);

        float3 lab = ADV_RGBToOklab(max(lin, 0.0));
        float L = max(lab.x, 0.0);
        float Lc = saturate(L);
        float C0 = length(lab.yz);

        float3 preSrgb = rawSrgb;
        if (UI_EnableDarkLighting || UI_EnableBC || UI_EnableDynamicHdr || UI_EnableClarity || UI_EnableGammaExp) {
            preSrgb = ADV_LinearToSRGB(saturate(lin));
        }
        float preMax = max(preSrgb.r, max(preSrgb.g, preSrgb.b));
        float preMin = min(preSrgb.r, min(preSrgb.g, preSrgb.b));
        float preSat = (preMax > 1e-4) ? saturate((preMax - preMin) / preMax) : 0.0;

        float skinMask0 = 1.0;
        if (C0 > 1e-4) {
            float hue0 = atan2(lab.z, lab.y);
            float dHue0 = abs(hue0 - 0.85);
            dHue0 = min(dHue0, 6.2831853 - dHue0);
            float hueW0 = exp(-(dHue0 * dHue0) / (2.0 * 0.38 * 0.38));
            float chromaW0 = smoothstep(0.015, 0.045, C0) * (1.0 - smoothstep(0.14, 0.24, C0));
            skinMask0 = lerp(1.0, 0.68, hueW0 * chromaW0);
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

        float wideAvgL = 0.25 * (w0 + w1 + w2 + w3);
        float sceneDark = saturate((0.42 - wideAvgL) / 0.24);
        float caveDark = saturate((0.26 - wideAvgL) / 0.12);

        float sCurveL = Lc * Lc * (3.0 - 2.0 * Lc);
        float contrastScale = lerp(1.0, 0.32, sceneDark);
        float sCurveMask = smoothstep(0.16, 0.30, Lc) * smoothstep(0.98, 0.80, Lc);
        float sCurveDelta = (sCurveL - Lc) * (0.10 * intLevel * contrastScale) * sCurveMask;

        float hazeBand = smoothstep(0.10, 0.15, Lc) * (1.0 - smoothstep(0.18, 0.24, Lc));
        float dehazeDelta = -0.005 * intLevel * hazeBand;

        float caveBand = smoothstep(0.118, 0.170, Lc) * smoothstep(0.62, 0.28, Lc);
        float caveLift = (0.135 * caveDark) * intLevel * caveBand;

        float domHeadroom = 1.0 - smoothstep(0.68, 0.96, preMax) * smoothstep(0.06, 0.16, C0);

        float darkLiftBand = smoothstep(0.14, 0.28, Lc) * smoothstep(0.76, 0.44, Lc);
        float adaptiveDarkLift = (0.018 + 0.064 * sceneDark) * intLevel * darkLiftBand * domHeadroom;

        float midLiftBand = smoothstep(0.24, 0.44, Lc) * smoothstep(0.95, 0.70, Lc);
        float midLift = 0.044 * intLevel * midLiftBand * domHeadroom;

        float fineDiff = rawL - fineMeanL;
        float fineCoring = smoothstep(0.024, 0.068, abs(fineDiff));
        float fineSafe = (fineDiff * fineCoring) / (1.0 + abs(fineDiff) * 12.0);
        float fineMask = smoothstep(0.16, 0.30, Lc) * smoothstep(0.99, 0.86, Lc);
        float fineBoost = fineSafe * (0.24 * intLevel) * fineMask;

        float wideDiff = rawL - wideMeanL;
        float wideCoring = smoothstep(0.014, 0.045, abs(wideDiff));
        float wideSafe = (wideDiff * wideCoring) / (1.0 + abs(wideDiff) * 7.5);
        float wideAdj = (wideSafe < 0.0) ? (wideSafe * lerp(0.30, 0.75, smoothstep(0.20, 0.54, Lc))) : wideSafe;
        float wideMask = smoothstep(lerp(0.18, 0.125, caveDark), lerp(0.34, 0.22, caveDark), Lc) * smoothstep(0.99, 0.84, Lc);
        float wideBoost = wideAdj * (0.32 * intLevel) * wideMask;

        float specBand = smoothstep(0.36, 0.68, Lc) * smoothstep(0.99, 0.86, Lc);
        float chromaGlow = smoothstep(0.032, 0.14, C0);
        float specColorGlow = 0.056 * intLevel * specBand * chromaGlow * (0.45 + 0.55 * satLevel) * skinMask0 * domHeadroom;
        float specEdge = max(fineDiff, 0.0) * smoothstep(0.045, 0.11, fineDiff);
        float specSparkle = (specEdge / (1.0 + specEdge * 14.0)) * (0.06 * intLevel) * smoothstep(0.56, 0.80, Lc) * smoothstep(0.995, 0.90, Lc);

        float L_new = max(L + sCurveDelta + dehazeDelta + caveLift + adaptiveDarkLift + midLift + fineBoost + wideBoost + specColorGlow + specSparkle, 0.0);
        if (L_new > 0.90 && L <= 1.0) {
            float over = (L_new - 0.90) / 0.10;
            L_new = 0.90 + 0.10 * (over / (1.0 + 0.38 * over));
        }

        float liftMinL = lerp(0.14, 0.125, caveDark);
        float liftRatio = (L > liftMinL && L_new > L) ? min(L_new / L, 1.42) : 1.0;
        float liftChromaScale = 0.70 * (1.0 - 0.75 * smoothstep(0.55, 0.88, preSat));
        lab.yz *= lerp(1.0, liftRatio, liftChromaScale * smoothstep(liftMinL, liftMinL + 0.14, L));
        lab.x = L_new;

        if (UI_SaturationBoost > 0.01) {
            float C = length(lab.yz);
            if (C > 1e-4) {
                float satDrive = pow(saturate(UI_SaturationBoost / 100.0), 0.68) * lerp(0.68, 1.22, intLevel);
                float mutedWeight = 1.0 / (1.0 + C * 2.8);
                float neutralGuard = smoothstep(0.012, 0.036, C);
                float lumaProtect = smoothstep(0.14, 0.34, lab.x) * smoothstep(0.96, 0.76, lab.x);
                float hue = atan2(lab.z, lab.y);
                float dHue = abs(hue - 0.85);
                dHue = min(dHue, 6.2831853 - dHue);
                float hueWeight = exp(-(dHue * dHue) / (2.0 * 0.38 * 0.38));
                float chromaWeight = smoothstep(0.015, 0.045, C) * (1.0 - smoothstep(0.14, 0.24, C));
                float skinW = hueWeight * chromaWeight;
                float skinFloor = lerp(0.34, 0.46, smoothstep(0.30, 1.0, satLevel));
                float skinMask = lerp(1.0, skinFloor, skinW);
                float gamutHeadroom = 1.0 - 0.82 * smoothstep(0.58, 0.90, preSat);
                float dRedHue = abs(hue - 0.62);
                dRedHue = min(dRedHue, 6.2831853 - dRedHue);
                float warmRedBand = exp(-(dRedHue * dRedHue) / (2.0 * 0.42 * 0.42));
                float warmRedGuard = 1.0 - 0.68 * warmRedBand * smoothstep(0.075, 0.145, C) * smoothstep(0.52, 0.80, preSat);
                float dCyanHue = abs(hue - (-2.12));
                dCyanHue = min(dCyanHue, 6.2831853 - dCyanHue);
                float cyanBlueBand = exp(-(dCyanHue * dCyanHue) / (2.0 * 0.52 * 0.52));
                float coolCyanBlueGuard = 1.0 - 0.86 * cyanBlueBand * smoothstep(0.038, 0.088, C) * smoothstep(0.36, 0.68, preSat);
                float dGoldHue = abs(hue - 1.62);
                dGoldHue = min(dGoldHue, 6.2831853 - dGoldHue);
                float warmGoldBand = exp(-(dGoldHue * dGoldHue) / (2.0 * 0.36 * 0.36));
                float warmGoldGuard = 1.0 - 0.74 * warmGoldBand * smoothstep(0.055, 0.115, C) * smoothstep(0.44, 0.74, preSat);
                float highChromaGuard = 1.0 - 0.68 * smoothstep(0.14, 0.23, C);
                float highlightPop = 1.0 + 0.12 * smoothstep(0.48, 0.70, lab.x) * smoothstep(0.92, 0.76, lab.x) * skinMask;
                float boost = 1.68 * satDrive * mutedWeight * neutralGuard * lumaProtect * skinMask * gamutHeadroom * warmRedGuard * coolCyanBlueGuard * warmGoldGuard * highChromaGuard * highlightPop;
                float cyanCap = 0.088 + 0.024 * smoothstep(0.42, 0.60, lab.x) * smoothstep(0.88, 0.68, lab.x);
                float maxChromaCap = max(C, lerp(0.265, cyanCap, cyanBlueBand * smoothstep(0.34, 0.64, preSat)));
                maxChromaCap = max(C, lerp(maxChromaCap, 0.124, warmGoldBand * smoothstep(0.46, 0.76, preSat)));
                float newC = min(C * (1.0 + boost), maxChromaCap);
                lab.yz *= (newC / C);

                if (skinW > 0.01 && satLevel > 0.25) {
                    float targetHue = 1.14;
                    if (hue < targetHue) {
                        float maxShift = 0.18 * smoothstep(0.25, 1.0, satLevel) * skinW * smoothstep(0.26, 0.82, lab.x);
                        float newHue = hue + min(targetHue - hue, maxShift);
                        lab.yz = float2(cos(newHue), sin(newHue)) * newC;
                    }
                    lab.x += 0.014 * smoothstep(0.35, 1.0, satLevel) * skinW * smoothstep(0.38, 0.78, lab.x);
                }
            }
        }

        lin = ADV_SoftGamut(ADV_OklabToRGB(lab));
    }

    if (UI_EnableAO && UI_AOIntensity > 0.01) {
        float aoLvl = pow(saturate(UI_AOIntensity / 100.0), 0.85);
        float radScale = lerp(0.50, 1.80, saturate(UI_AORadius / 100.0));
        float cL = ADV_FastPerceptualL(rawSrgb);

        float2 r1 = BUFFER_PIXEL_SIZE * (3.5 * radScale);
        float f0 = ADV_FastPerceptualL(tex2D(ReShade::BackBuffer, texcoord + float2( r1.x,  r1.y)).rgb);
        float f1 = ADV_FastPerceptualL(tex2D(ReShade::BackBuffer, texcoord + float2(-r1.x, -r1.y)).rgb);
        float f2 = ADV_FastPerceptualL(tex2D(ReShade::BackBuffer, texcoord + float2(-r1.x,  r1.y)).rgb);
        float f3 = ADV_FastPerceptualL(tex2D(ReShade::BackBuffer, texcoord + float2( r1.x, -r1.y)).rgb);
        float occFine = 0.5 * (ADV_AoPairOcclusion(cL, f0, f1) + ADV_AoPairOcclusion(cL, f2, f3));

        float2 r2 = BUFFER_PIXEL_SIZE * (12.0 * radScale);
        float2 r2d = r2 * 0.7071;
        float bL = ADV_FastPerceptualL(tex2D(ReShade::BackBuffer, texcoord + float2(-r2.x,  0.0)).rgb);
        float bR = ADV_FastPerceptualL(tex2D(ReShade::BackBuffer, texcoord + float2( r2.x,  0.0)).rgb);
        float bU = ADV_FastPerceptualL(tex2D(ReShade::BackBuffer, texcoord + float2( 0.0, -r2.y)).rgb);
        float bD = ADV_FastPerceptualL(tex2D(ReShade::BackBuffer, texcoord + float2( 0.0,  r2.y)).rgb);
        float bD0 = ADV_FastPerceptualL(tex2D(ReShade::BackBuffer, texcoord + float2( r2d.x,  r2d.y)).rgb);
        float bD1 = ADV_FastPerceptualL(tex2D(ReShade::BackBuffer, texcoord + float2(-r2d.x, -r2d.y)).rgb);
        float bD2 = ADV_FastPerceptualL(tex2D(ReShade::BackBuffer, texcoord + float2(-r2d.x,  r2d.y)).rgb);
        float bD3 = ADV_FastPerceptualL(tex2D(ReShade::BackBuffer, texcoord + float2( r2d.x, -r2d.y)).rgb);
        float occBroad = 0.25 * (
            ADV_AoPairOcclusion(cL, bL, bR) +
            ADV_AoPairOcclusion(cL, bU, bD) +
            ADV_AoPairOcclusion(cL, bD0, bD1) +
            ADV_AoPairOcclusion(cL, bD2, bD3)
        );

        float occRaw = (occFine * 0.55 + occBroad * 0.45) * 3.6;
        float occ2D = occRaw / (1.0 + occRaw * 0.45);
        float occ = occ2D;

        if (UI_AOUseDepth) {
            float dC = ReShade::GetLinearizedDepth(texcoord);
            float dSamples[8] = {
                ReShade::GetLinearizedDepth(texcoord + float2(-r2.x,  0.0)),
                ReShade::GetLinearizedDepth(texcoord + float2( r2.x,  0.0)),
                ReShade::GetLinearizedDepth(texcoord + float2( 0.0, -r2.y)),
                ReShade::GetLinearizedDepth(texcoord + float2( 0.0,  r2.y)),
                ReShade::GetLinearizedDepth(texcoord + float2( r2d.x,  r2d.y)),
                ReShade::GetLinearizedDepth(texcoord + float2(-r2d.x, -r2d.y)),
                ReShade::GetLinearizedDepth(texcoord + float2(-r2d.x,  r2d.y)),
                ReShade::GetLinearizedDepth(texcoord + float2( r2d.x, -r2d.y))
            };
            float dMin = dC;
            float dMax = dC;
            float occDepthSum = 0.0;
            [unroll]
            for (int di = 0; di < 8; ++di) {
                float dS = dSamples[di];
                dMin = min(dMin, dS);
                dMax = max(dMax, dS);
                float dz = dC - dS;
                if (dz > 0.0005 && dz < 0.045) {
                    occDepthSum += saturate((dz - 0.0005) / 0.015) * (1.0 - smoothstep(0.018, 0.045, dz));
                }
            }
            if ((dMax - dMin) > 1e-4 && dC < 0.995) {
                float occDepth = saturate(occDepthSum / 6.0);
                occ = lerp(occ2D, max(occDepth, occ2D * 0.65), 0.78);
            }
        }

        float highlightGuard = smoothstep(0.92, 0.56, cL);
        float blackGuard = smoothstep(0.04, 0.14, cL);

        float skinAoGuard = 1.0;
        float3 rawLab = ADV_RGBToOklab(ADV_SRGBToLinear(rawSrgb));
        float rawC = length(rawLab.yz);
        if (rawC > 1e-4) {
            float rawHue = atan2(rawLab.z, rawLab.y);
            float dHueAo = abs(rawHue - 0.85);
            dHueAo = min(dHueAo, 6.2831853 - dHueAo);
            float hueWAo = exp(-(dHueAo * dHueAo) / (2.0 * 0.38 * 0.38));
            float chromaWAo = smoothstep(0.015, 0.045, rawC) * (1.0 - smoothstep(0.14, 0.24, rawC));
            skinAoGuard = lerp(1.0, 0.48, hueWAo * chromaWAo);
        }

        float aoDarken = saturate(occ * 0.78 * aoLvl * highlightGuard * blackGuard * skinAoGuard);
        lin *= (1.0 - aoDarken);
    }

    if (UI_EnableSharpen && UI_Sharpen > 0.01) {
        float sharpNorm = saturate(UI_Sharpen / 100.0);
        float2 px1 = BUFFER_PIXEL_SIZE;
        float3 pW = float3(0.299, 0.587, 0.114);
        float cP = dot(saturate(rawSrgb), pW);
        float nP = dot(saturate(tex2D(ReShade::BackBuffer, texcoord + float2( 0.0, -px1.y)).rgb), pW);
        float sP = dot(saturate(tex2D(ReShade::BackBuffer, texcoord + float2( 0.0,  px1.y)).rgb), pW);
        float wP = dot(saturate(tex2D(ReShade::BackBuffer, texcoord + float2(-px1.x,  0.0)).rgb), pW);
        float eP = dot(saturate(tex2D(ReShade::BackBuffer, texcoord + float2( px1.x,  0.0)).rgb), pW);

        float mnL = min(cP, min(min(nP, sP), min(wP, eP)));
        float mxL = max(cP, max(max(nP, sP), max(wP, eP)));
        float amp = sqrt(saturate(min(mnL, 2.0 - mxL) / max(mxL, 1e-4)));
        float peak = -1.0 / lerp(8.0, 4.1, sharpNorm);
        float wCas = amp * peak;
        float sharpP = (cP + (nP + sP + wP + eP) * wCas) / (1.0 + 4.0 * wCas);
        float diffP = sharpP - cP;
        float coring = smoothstep(0.011, 0.032, abs(diffP));
        float deltaP = clamp(diffP * coring * lerp(0.65, 1.75, sharpNorm), -0.26, 0.26);
        float curY = max(ADV_Luma(lin), 1e-6);
        float curP = pow(curY, 1.0 / 2.2);
        float targetY = pow(max(curP + deltaP, 0.0), 2.2);
        lin *= (targetY / curY);
    }

    if (UI_EnableBloom && UI_BloomIntensity > 0.01) {
        float bloomLvl = pow(saturate(UI_BloomIntensity / 100.0), 0.90);
        float thresh = lerp(0.35, 0.92, saturate(UI_BloomThreshold / 100.0));
        float knee = 0.22;
        float2 r1 = BUFFER_PIXEL_SIZE * 5.0;
        float2 r2 = BUFFER_PIXEL_SIZE * 13.0;

        float3 b0 = ADV_SRGBToLinear(tex2D(ReShade::BackBuffer, texcoord + float2( r1.x,  r1.y)).rgb);
        float3 b1 = ADV_SRGBToLinear(tex2D(ReShade::BackBuffer, texcoord + float2(-r1.x,  r1.y)).rgb);
        float3 b2 = ADV_SRGBToLinear(tex2D(ReShade::BackBuffer, texcoord + float2( r1.x, -r1.y)).rgb);
        float3 b3 = ADV_SRGBToLinear(tex2D(ReShade::BackBuffer, texcoord + float2(-r1.x, -r1.y)).rgb);
        float3 o0 = ADV_SRGBToLinear(tex2D(ReShade::BackBuffer, texcoord + float2( r2.x,  0.0)).rgb);
        float3 o1 = ADV_SRGBToLinear(tex2D(ReShade::BackBuffer, texcoord + float2(-r2.x,  0.0)).rgb);
        float3 o2 = ADV_SRGBToLinear(tex2D(ReShade::BackBuffer, texcoord + float2( 0.0,  r2.y)).rgb);
        float3 o3 = ADV_SRGBToLinear(tex2D(ReShade::BackBuffer, texcoord + float2( 0.0, -r2.y)).rgb);

        float3 innerSum =
            b0 * smoothstep(thresh - knee, thresh + knee, max(b0.r, max(b0.g, b0.b))) +
            b1 * smoothstep(thresh - knee, thresh + knee, max(b1.r, max(b1.g, b1.b))) +
            b2 * smoothstep(thresh - knee, thresh + knee, max(b2.r, max(b2.g, b2.b))) +
            b3 * smoothstep(thresh - knee, thresh + knee, max(b3.r, max(b3.g, b3.b)));
        float3 outerSum =
            o0 * smoothstep(thresh - knee, thresh + knee, max(o0.r, max(o0.g, o0.b))) +
            o1 * smoothstep(thresh - knee, thresh + knee, max(o1.r, max(o1.g, o1.b))) +
            o2 * smoothstep(thresh - knee, thresh + knee, max(o2.r, max(o2.g, o2.b))) +
            o3 * smoothstep(thresh - knee, thresh + knee, max(o3.r, max(o3.g, o3.b)));

        float3 bloomCol = (innerSum * 0.62 + outerSum * 0.38) * 0.25;
        lin = max(lin + bloomCol * (0.85 * bloomLvl) * saturate(1.25 - lin * 0.35), 0.0);
    }

    if (UI_EnableFilmic && UI_FilmicStrength > 0.01) {
        float fStr = saturate(UI_FilmicStrength / 100.0);
        float3 srcLin = max(lin, 0.0);
        float3 graded = srcLin;
        if (UI_FilmicMode == 0) {
            float k = 0.55;
            float3 x = srcLin * k;
            float wn = (k * (2.51 * k + 0.03)) / (k * (2.43 * k + 0.59) + 0.14);
            graded = saturate(((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14)) / wn);
        } else if (UI_FilmicMode == 1) {
            float3 agxIn = float3(
                0.84247906 * srcLin.r + 0.07843360 * srcLin.g + 0.07922375 * srcLin.b,
                0.04232824 * srcLin.r + 0.87846864 * srcLin.g + 0.07916613 * srcLin.b,
                0.04237565 * srcLin.r + 0.07843360 * srcLin.g + 0.87914297 * srcLin.b
            );
            float3 p = pow(max(agxIn, 0.0), 1.0 / 2.2);
            float3 s = p * p * (3.0 - 2.0 * p);
            float3 m = saturate(lerp(p, s, 0.55));
            float3 agxLin = pow(m, 2.2);
            graded = saturate(float3(
                 1.08 * agxLin.r - 0.04 * agxLin.g - 0.04 * agxLin.b,
                -0.02 * agxLin.r + 1.06 * agxLin.g - 0.04 * agxLin.b,
                -0.02 * agxLin.r - 0.04 * agxLin.g + 1.06 * agxLin.b
            ));
        } else {
            float3 s = ADV_LinearToSRGB(saturate(srcLin));
            float3 m = saturate(float3(
                 1.18 * s.r - 0.12 * s.g - 0.06 * s.b,
                -0.06 * s.r + 1.14 * s.g - 0.08 * s.b,
                -0.04 * s.r - 0.08 * s.g + 1.12 * s.b
            ));
            float3 sc = m * m * (3.0 - 2.0 * m);
            graded = ADV_SRGBToLinear(lerp(m, sc, 0.32));
        }
        lin = lerp(srcLin, graded, fStr);
    }

    if (UI_EnableColorTemp) {
        float temp = ((UI_Temperature / 100.0) - 0.5) * 2.0;
        float tintVal = ((UI_Tint / 100.0) - 0.5) * 2.0;
        float3 wbGain = max(float3(
            1.0 + 0.45 * temp + 0.22 * tintVal,
            1.0 - 0.32 * tintVal,
            1.0 - 0.48 * temp + 0.22 * tintVal
        ), 0.10);
        float oldY = max(ADV_Luma(lin), 1e-5);
        lin *= wbGain;
        float newY = max(ADV_Luma(lin), 1e-5);
        lin *= (oldY / newY);
    }

    if (UI_EnableNightMode && UI_NightMode > 0.01) {
        float nm = saturate(UI_NightMode / 100.0);
        float3 nightMult = float3(1.0, lerp(1.0, 0.68, nm), lerp(1.0, 0.20, nm));
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
            shift = float3(0.0, 1.0 * errRgb.r, 1.0 * errRgb.r);
        } else if (UI_ColorblindType == 1) {
            shift = float3(1.0 * errRgb.g, 0.0, 1.0 * errRgb.g);
        } else {
            shift = float3(1.0 * errRgb.b, 1.0 * errRgb.b, 0.0);
        }
        lin = max(lin + shift * saturate(UI_ColorblindStrength / 100.0), 0.0);
    }

    if (UI_EnableBW && UI_BW > 0.01) {
        float bwY = ADV_Luma(lin);
        lin = lerp(lin, float3(bwY, bwY, bwY), saturate(UI_BW / 100.0));
    }

    if (UI_EnableVignette && UI_Vignette > 0.01) {
        float2 dUv = texcoord - 0.5;
        float r2 = saturate(dot(dUv, dUv) * 2.4);
        lin *= lerp(1.0, 1.0 - 0.92 * pow(r2, 1.25), saturate(UI_Vignette / 100.0));
    }

    float3 outSrgb = ADV_LinearToSRGB(ADV_SoftGamut(lin));
    if (max(outSrgb.r, max(outSrgb.g, outSrgb.b)) > 0.002) {
        float ign = frac(52.9829189 * frac(dot(vpos.xy, float2(0.06711056, 0.00583715))));
        outSrgb = saturate(outSrgb + (ign - 0.5) / 255.0);
    }
    if (UI_SplitPreview) {
        float splitX = clamp(UI_SplitPosition / 100.0, 0.05, 0.95);
        if (abs(texcoord.x - splitX) <= BUFFER_PIXEL_SIZE.x * 1.25) {
            return float4(0.92, 0.92, 0.94, 1.0);
        }
        if (texcoord.x < splitX) {
            return float4(origSrgb, 1.0);
        }
    }
    return float4(outSrgb, 1.0);
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
