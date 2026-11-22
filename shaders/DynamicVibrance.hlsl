
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
