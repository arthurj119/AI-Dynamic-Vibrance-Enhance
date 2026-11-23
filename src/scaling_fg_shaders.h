#pragma once

inline constexpr const char* kScalingFgHLSL = R"HLSL(
cbuffer FgCB : register(b0) {
    uint uSrcW;
    uint uSrcH;
    uint uDstW;
    uint uDstH;

    float uInterpT;
    float uFallbackStrength;
    float uHudProtect;
    float uSearchDir;

    uint uLevel;
    uint uScaleMode;
    float uScaleSharpness;
    float uPad0;

    float uVpLeft;
    float uVpTop;
    float uVpW;
    float uVpH;
};

struct FgStats {
    float avgWarpErr;
    float avgFlowMag;
    float fallbackFactor;
    float pad0;
};

Texture2D<float4> texA : register(t0);
Texture2D<float4> texB : register(t1);
Texture2D<float4> texC : register(t2);
Texture2D<float4> texD : register(t3);
StructuredBuffer<FgStats> fgStatsIn : register(t4);
SamplerState linearSamp : register(s0);

RWTexture2D<float> uLumaOut : register(u0);
RWTexture2D<float2> uFlowOut : register(u1);
RWTexture2D<float4> uFlowFinal : register(u2);
RWStructuredBuffer<FgStats> uStatsOut : register(u3);

struct VSOut {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

VSOut VSFullScreen(uint id : SV_VertexID) {
    VSOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.uv = uv;
    o.pos = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, 0.0f, 1.0f);
    return o;
}

float PerceptualLuma(float3 rgb) {
    return dot(saturate(rgb), float3(0.299f, 0.587f, 0.114f));
}

// =========================================================================
// 1. MULTI-SCALE OPTICAL FLOW COMPUTE PIPELINE
// =========================================================================

[numthreads(16, 16, 1)]
void CSLumaDownsample(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= uDstW || dtid.y >= uDstH) return;

    float2 invDst = 1.0f / float2(max(uDstW, 1u), max(uDstH, 1u));
    float2 uv = (float2(dtid.xy) + 0.5f) * invDst;
    float2 off = 0.25f * invDst;

    if (uLevel == 0u) {
        float l0 = PerceptualLuma(texA.SampleLevel(linearSamp, uv + float2(-off.x, -off.y), 0.0f).rgb);
        float l1 = PerceptualLuma(texA.SampleLevel(linearSamp, uv + float2( off.x, -off.y), 0.0f).rgb);
        float l2 = PerceptualLuma(texA.SampleLevel(linearSamp, uv + float2(-off.x,  off.y), 0.0f).rgb);
        float l3 = PerceptualLuma(texA.SampleLevel(linearSamp, uv + float2( off.x,  off.y), 0.0f).rgb);
        uLumaOut[dtid.xy] = 0.25f * (l0 + l1 + l2 + l3);
    } else {
        float l0 = texA.SampleLevel(linearSamp, uv + float2(-off.x, -off.y), 0.0f).r;
        float l1 = texA.SampleLevel(linearSamp, uv + float2( off.x, -off.y), 0.0f).r;
        float l2 = texA.SampleLevel(linearSamp, uv + float2(-off.x,  off.y), 0.0f).r;
        float l3 = texA.SampleLevel(linearSamp, uv + float2( off.x,  off.y), 0.0f).r;
        uLumaOut[dtid.xy] = 0.25f * (l0 + l1 + l2 + l3);
    }
}

float PatchMatchCost(Texture2D<float4> srcPrev, Texture2D<float4> srcCurr, float2 uv, float2 uvShift, float2 invTexSize) {
    float sad = 0.0f;
    [unroll]
    for (int py = -1; py <= 1; ++py) {
        [unroll]
        for (int px = -1; px <= 1; ++px) {
            float2 d = float2(px, py) * invTexSize;
            float2 uvC = uv + d;
            float2 uvP = saturate(uv - uvShift + d);
            float cL = srcCurr.SampleLevel(linearSamp, uvC, 0.0f).r;
            float pL = srcPrev.SampleLevel(linearSamp, uvP, 0.0f).r;
            sad += abs(cL - pL);
        }
    }
    return sad / 9.0f;
}

float PatchMatchGradCost(Texture2D<float4> srcPrev, Texture2D<float4> srcCurr, float2 uv, float2 uvShift, float2 invTexSize) {
    float sad = 0.0f;
    float gradSad = 0.0f;
    [unroll]
    for (int py = -1; py <= 1; ++py) {
        [unroll]
        for (int px = -1; px <= 1; ++px) {
            float2 d = float2(px, py) * invTexSize;
            float2 uvC = uv + d;
            float2 uvP = saturate(uv - uvShift + d);
            float cL = srcCurr.SampleLevel(linearSamp, uvC, 0.0f).r;
            float pL = srcPrev.SampleLevel(linearSamp, uvP, 0.0f).r;
            sad += abs(cL - pL);

            if (px == 0 && py == 0) {
                float cR = srcCurr.SampleLevel(linearSamp, uvC + float2(invTexSize.x, 0.0f), 0.0f).r;
                float cD = srcCurr.SampleLevel(linearSamp, uvC + float2(0.0f, invTexSize.y), 0.0f).r;
                float pR = srcPrev.SampleLevel(linearSamp, uvP + float2(invTexSize.x, 0.0f), 0.0f).r;
                float pD = srcPrev.SampleLevel(linearSamp, uvP + float2(0.0f, invTexSize.y), 0.0f).r;
                gradSad = abs((cR - cL) - (pR - pL)) + abs((cD - cL) - (pD - pL));
            }
        }
    }
    return (sad / 9.0f) + 0.30f * gradSad;
}

[numthreads(16, 16, 1)]
void CSOpticalFlowCoarse(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= uDstW || dtid.y >= uDstH) return;

    float2 invRes = 1.0f / float2(max(uDstW, 1u), max(uDstH, 1u));
    float2 uv = (float2(dtid.xy) + 0.5f) * invRes;

    float scores[9][9];
    float bestCost = 1e6f;
    int bestIx = 4;
    int bestIy = 4;

    for (int iy = 0; iy < 9; ++iy) {
        int dy = iy - 4;
        for (int ix = 0; ix < 9; ++ix) {
            int dx = ix - 4;
            float2 uvShift = float2(dx, dy) * invRes;
            float reg = 0.0018f * sqrt(float(dx * dx + dy * dy));
            float cost = PatchMatchCost(texA, texB, uv, uvShift, invRes) + reg;
            scores[iy][ix] = cost;
            if (cost < bestCost) {
                bestCost = cost;
                bestIx = ix;
                bestIy = iy;
            }
        }
    }

    float subX = float(bestIx - 4);
    if (bestIx > 0 && bestIx < 8) {
        float cL = scores[bestIy][bestIx - 1];
        float cC = scores[bestIy][bestIx];
        float cR = scores[bestIy][bestIx + 1];
        float denom = cL - 2.0f * cC + cR;
        if (denom > 1e-5f) {
            subX += clamp(0.5f * (cL - cR) / denom, -0.5f, 0.5f);
        }
    }

    float subY = float(bestIy - 4);
    if (bestIy > 0 && bestIy < 8) {
        float cU = scores[bestIy - 1][bestIx];
        float cC = scores[bestIy][bestIx];
        float cD = scores[bestIy + 1][bestIx];
        float denom = cU - 2.0f * cC + cD;
        if (denom > 1e-5f) {
            subY += clamp(0.5f * (cU - cD) / denom, -0.5f, 0.5f);
        }
    }

    uFlowOut[dtid.xy] = float2(subX, subY) * invRes;
}

[numthreads(16, 16, 1)]
void CSOpticalFlowRefine(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= uDstW || dtid.y >= uDstH) return;

    float2 res = float2(max(uDstW, 1u), max(uDstH, 1u));
    float2 invRes = 1.0f / res;
    float2 uv = (float2(dtid.xy) + 0.5f) * invRes;

    float2 seedUv = texC.SampleLevel(linearSamp, uv, 0.0f).xy * uSearchDir;
    float2 seedPx = seedUv * res;

    float scores[3][3];
    float bestCost = 1e6f;
    int bestIx = 1;
    int bestIy = 1;

    [unroll]
    for (int iy = 0; iy < 3; ++iy) {
        float dy = float(iy - 1);
        [unroll]
        for (int ix = 0; ix < 3; ++ix) {
            float dx = float(ix - 1);
            float2 candPx = seedPx + float2(dx, dy);
            float2 candUv = candPx * invRes;
            float reg = 0.0005f * length(candPx);
            float cost = PatchMatchGradCost(texA, texB, uv, candUv, invRes) + reg;
            scores[iy][ix] = cost;
            if (cost < bestCost) {
                bestCost = cost;
                bestIx = ix;
                bestIy = iy;
            }
        }
    }

    float subX = float(bestIx - 1);
    if (bestIx == 1) {
        float cL = scores[bestIy][0];
        float cC = scores[bestIy][1];
        float cR = scores[bestIy][2];
        float denom = cL - 2.0f * cC + cR;
        if (denom > 1e-5f) {
            subX = clamp(0.5f * (cL - cR) / denom, -0.5f, 0.5f);
        }
    }

    float subY = float(bestIy - 1);
    if (bestIy == 1) {
        float cU = scores[0][bestIx];
        float cC = scores[1][bestIx];
        float cD = scores[2][bestIx];
        float denom = cU - 2.0f * cC + cD;
        if (denom > 1e-5f) {
            subY = clamp(0.5f * (cU - cD) / denom, -0.5f, 0.5f);
        }
    }

    float2 finalUv = (seedPx + float2(subX, subY)) * invRes;
    uFlowOut[dtid.xy] = finalUv;
}

[numthreads(16, 16, 1)]
void CSFlowFinalize(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= uDstW || dtid.y >= uDstH) return;

    float2 res = float2(max(uDstW, 1u), max(uDstH, 1u));
    float2 invRes = 1.0f / res;
    float2 uv = (float2(dtid.xy) + 0.5f) * invRes;

    // texA = lumaPrevL0, texB = lumaCurrL0, texC = flowFwdL0, texD = flowBwdL0
    float cCurr = texB.SampleLevel(linearSamp, uv, 0.0f).r;
    float cPrev = texA.SampleLevel(linearSamp, uv, 0.0f).r;
    float2 centerFlow = texC.SampleLevel(linearSamp, uv, 0.0f).xy;

    // Edge-aware 3x3 bilateral flow smoothing to sharpen object boundaries cleanly
    float2 sumFlow = float2(0.0f, 0.0f);
    float sumW = 0.0f;
    float2 avgNeighbor = float2(0.0f, 0.0f);

    [unroll]
    for (int py = -1; py <= 1; ++py) {
        [unroll]
        for (int px = -1; px <= 1; ++px) {
            float2 nUv = saturate(uv + float2(px, py) * invRes);
            float nL = texB.SampleLevel(linearSamp, nUv, 0.0f).r;
            float2 nF = texC.SampleLevel(linearSamp, nUv, 0.0f).xy;
            float dL = nL - cCurr;
            float w = exp(-dL * dL * 65.0f);
            sumFlow += nF * w;
            sumW += w;
            avgNeighbor += nF;
        }
    }
    float2 vSmooth = (sumW > 1e-5f) ? (sumFlow / sumW) : centerFlow;
    avgNeighbor /= 9.0f;

    // Symmetrical Forward-Backward consistency check at uv - vSmooth in Prev frame
    float2 vBwd = texD.SampleLevel(linearSamp, saturate(uv - vSmooth), 0.0f).xy;
    float fbErrPx = length((vSmooth + vBwd) * res);
    float confidence = 1.0f - smoothstep(1.2f, 4.5f, fbErrPx);

    // Static HUD / Crosshair Guard: high edge contrast stationary across Prev/Curr while neighbors move
    float cR = texB.SampleLevel(linearSamp, saturate(uv + float2(invRes.x, 0.0f)), 0.0f).r;
    float cD = texB.SampleLevel(linearSamp, saturate(uv + float2(0.0f, invRes.y)), 0.0f).r;
    float temporalDiff = abs(cCurr - cPrev);
    float edgeMag = abs(cCurr - cR) + abs(cCurr - cD);
    float avgMagPx = length(avgNeighbor * res);

    float staticCue = (1.0f - smoothstep(0.008f, 0.022f, temporalDiff)) * smoothstep(0.04f, 0.085f, edgeMag);
    float neighborMoveCue = smoothstep(0.45f, 1.4f, avgMagPx);
    float hudMask = saturate(staticCue * neighborMoveCue);

    vSmooth *= (1.0f - 0.85f * hudMask * saturate(uHudProtect));
    uFlowFinal[dtid.xy] = float4(vSmooth.x, vSmooth.y, confidence, hudMask);
}

groupshared float4 gsFgStats[256];

[numthreads(16, 16, 1)]
void CSFlowStats(uint3 gtid : SV_GroupThreadID) {
    uint idx = gtid.y * 16u + gtid.x;
    float2 uv = (float2(gtid.xy) + 0.5f) / 16.0f;

    // texA = lumaPrevL0, texB = lumaCurrL0, texC = flowFinal
    float2 flow = texC.SampleLevel(linearSamp, uv, 0.0f).xy;
    float cCurr = texB.SampleLevel(linearSamp, uv, 0.0f).r;
    float cWarp = texA.SampleLevel(linearSamp, saturate(uv - flow), 0.0f).r;
    float warpErr = abs(cCurr - cWarp);
    float flowMag = length(flow);

    gsFgStats[idx] = float4(warpErr, flowMag, 0.0f, 0.0f);
    GroupMemoryBarrierWithGroupSync();

    [unroll]
    for (uint stride = 128u; stride > 0u; stride >>= 1u) {
        if (idx < stride) {
            gsFgStats[idx] += gsFgStats[idx + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (idx == 0u) {
        float avgWarpErr = gsFgStats[0].x / 256.0f;
        float avgFlowMag = gsFgStats[0].y / 256.0f;
        float cutGuard = 1.0f - smoothstep(0.09f, 0.20f, avgWarpErr);
        float panGuard = 1.0f - smoothstep(0.065f, 0.14f, avgFlowMag);
        float fb = lerp(1.0f, min(cutGuard, panGuard), saturate(uFallbackStrength));
        FgStats st;
        st.avgWarpErr = avgWarpErr;
        st.avgFlowMag = avgFlowMag;
        st.fallbackFactor = fb;
        st.pad0 = 0.0f;
        uStatsOut[0] = st;
    }
}

// =========================================================================
// 2. BIDIRECTIONAL DISOCCLUSION-AWARE FRAME SYNTHESIS PIXEL SHADER
// =========================================================================

float4 PSFrameGen(VSOut inp) : SV_TARGET {
    // texA = prevFrame, texB = currFrame, texC = flowFinal, texD = flowBwdL0, fgStatsIn[0]
    float3 rawCurr = texB.SampleLevel(linearSamp, inp.uv, 0.0f).rgb;
    float effScale = fgStatsIn[0].fallbackFactor;
    if (uInterpT <= 0.001f || uInterpT >= 0.999f || effScale < 0.05f) {
        return float4(rawCurr, 1.0f);
    }

    float4 ff = texC.SampleLevel(linearSamp, inp.uv, 0.0f);
    float2 vFwd = ff.xy;
    float conf = ff.z;
    float hudMask = ff.w;

    float2 vBwdAtUv = texD.SampleLevel(linearSamp, inp.uv, 0.0f).xy;
    float2 v = (length(vBwdAtUv) > length(vFwd) * 1.35f) ? -vBwdAtUv : vFwd;

    float hudBlock = hudMask * saturate(uHudProtect);
    float2 vEff = v * (1.0f - hudBlock) * effScale;

    float2 uvPrev = saturate(inp.uv - vEff * uInterpT);
    float2 uvCurr = saturate(inp.uv + vEff * (1.0f - uInterpT));
    float3 cPrev = texA.SampleLevel(linearSamp, uvPrev, 0.0f).rgb;
    float3 cCurr = texB.SampleLevel(linearSamp, uvCurr, 0.0f).rgb;

    float photoMatch = 1.0f - smoothstep(0.18f, 0.52f, length(cPrev - cCurr));
    float blendTrust = saturate(conf * 0.45f + photoMatch * 0.55f);

    float3 warpedBlend = lerp(cPrev, cCurr, uInterpT);
    float3 rawPrev = texA.SampleLevel(linearSamp, inp.uv, 0.0f).rgb;
    float3 nearestRaw = (uInterpT < 0.5f) ? rawPrev : rawCurr;

    float3 outRgb = lerp(nearestRaw, warpedBlend, blendTrust);
    outRgb = lerp(outRgb, rawCurr, hudBlock);
    outRgb = lerp(rawCurr, outRgb, effScale);
    return float4(saturate(outRgb), 1.0f);
}

// =========================================================================
// 3. SPATIAL UPSCALING SUITE (FSR 1.0 EASU/RCAS, NIS, INTEGER, ANIME4K)
// =========================================================================

float FsrLanczos2Weight(float d2) {
    float a = 0.4f * d2 - 1.0f;
    float b = 0.25f * d2 - 1.0f;
    float wBase = (25.0f / 16.0f) * (a * a) - (9.0f / 16.0f);
    float wWindow = b * b;
    return wBase * wWindow * step(d2, 4.0f);
}

void FsrTapAccum(
    inout float3 accColor,
    inout float accWeight,
    float2 off,
    float3 tapColor,
    float2 dir,
    float2 scale
) {
    float2 rot;
    rot.x = off.x * dir.x + off.y * dir.y;
    rot.y = off.x * (-dir.y) + off.y * dir.x;
    rot *= scale;
    float d2 = dot(rot, rot);
    float w = FsrLanczos2Weight(d2);
    accColor += tapColor * w;
    accWeight += w;
}

float4 PSFsrEasu(VSOut inp) : SV_TARGET {
    float2 srcSize = float2(max(uSrcW, 1u), max(uSrcH, 1u));
    float2 invSrc = 1.0f / srcSize;

    float2 pos = inp.uv * srcSize - 0.5f;
    float2 fp = floor(pos);
    float2 f = pos - fp;
    float2 p0 = (fp + 0.5f) * invSrc;

    // 12-tap EASU neighborhood (4 inner corners + 8 outer neighbors)
    float3 b = texA.SampleLevel(linearSamp, p0 + float2( 0.0f, -1.0f) * invSrc, 0.0f).rgb;
    float3 c = texA.SampleLevel(linearSamp, p0 + float2( 1.0f, -1.0f) * invSrc, 0.0f).rgb;
    float3 e = texA.SampleLevel(linearSamp, p0 + float2(-1.0f,  0.0f) * invSrc, 0.0f).rgb;
    float3 fC = texA.SampleLevel(linearSamp, p0, 0.0f).rgb;
    float3 g = texA.SampleLevel(linearSamp, p0 + float2( 1.0f,  0.0f) * invSrc, 0.0f).rgb;
    float3 h = texA.SampleLevel(linearSamp, p0 + float2( 2.0f,  0.0f) * invSrc, 0.0f).rgb;
    float3 i = texA.SampleLevel(linearSamp, p0 + float2(-1.0f,  1.0f) * invSrc, 0.0f).rgb;
    float3 j = texA.SampleLevel(linearSamp, p0 + float2( 0.0f,  1.0f) * invSrc, 0.0f).rgb;
    float3 k = texA.SampleLevel(linearSamp, p0 + float2( 1.0f,  1.0f) * invSrc, 0.0f).rgb;
    float3 l = texA.SampleLevel(linearSamp, p0 + float2( 2.0f,  1.0f) * invSrc, 0.0f).rgb;
    float3 n = texA.SampleLevel(linearSamp, p0 + float2( 0.0f,  2.0f) * invSrc, 0.0f).rgb;
    float3 o = texA.SampleLevel(linearSamp, p0 + float2( 1.0f,  2.0f) * invSrc, 0.0f).rgb;

    float lb = PerceptualLuma(b), lc = PerceptualLuma(c);
    float le = PerceptualLuma(e), lf = PerceptualLuma(fC), lg = PerceptualLuma(g), lh = PerceptualLuma(h);
    float li = PerceptualLuma(i), lj = PerceptualLuma(j), lk = PerceptualLuma(k), ll = PerceptualLuma(l);
    float ln = PerceptualLuma(n), lo = PerceptualLuma(o);

    // Directional gradient anisotropy across the 4 inner bilinear pixels
    float dx = (lg - le) * (1.0f - f.x) * (1.0f - f.y) + (lh - lf) * f.x * (1.0f - f.y) +
               (lk - li) * (1.0f - f.x) * f.y + (ll - lj) * f.x * f.y;
    float dy = (lj - lb) * (1.0f - f.x) * (1.0f - f.y) + (lk - lc) * f.x * (1.0f - f.y) +
               (ln - lf) * (1.0f - f.x) * f.y + (lo - lg) * f.x * f.y;

    float len2 = dx * dx + dy * dy;
    float2 dir = (len2 > 1e-6f) ? (float2(dx, dy) * rsqrt(len2)) : float2(1.0f, 0.0f);
    float edgeStrength = saturate(sqrt(len2) * 3.2f);
    float2 anisoScale = float2(lerp(1.0f, 1.35f, edgeStrength), lerp(1.0f, 0.72f, edgeStrength));

    float3 accColor = float3(0.0f, 0.0f, 0.0f);
    float accWeight = 0.0f;

    FsrTapAccum(accColor, accWeight, float2( 0.0f, -1.0f) - f, b, dir, anisoScale);
    FsrTapAccum(accColor, accWeight, float2( 1.0f, -1.0f) - f, c, dir, anisoScale);
    FsrTapAccum(accColor, accWeight, float2(-1.0f,  0.0f) - f, e, dir, anisoScale);
    FsrTapAccum(accColor, accWeight, float2( 0.0f,  0.0f) - f, fC, dir, anisoScale);
    FsrTapAccum(accColor, accWeight, float2( 1.0f,  0.0f) - f, g, dir, anisoScale);
    FsrTapAccum(accColor, accWeight, float2( 2.0f,  0.0f) - f, h, dir, anisoScale);
    FsrTapAccum(accColor, accWeight, float2(-1.0f,  1.0f) - f, i, dir, anisoScale);
    FsrTapAccum(accColor, accWeight, float2( 0.0f,  1.0f) - f, j, dir, anisoScale);
    FsrTapAccum(accColor, accWeight, float2( 1.0f,  1.0f) - f, k, dir, anisoScale);
    FsrTapAccum(accColor, accWeight, float2( 2.0f,  1.0f) - f, l, dir, anisoScale);
    FsrTapAccum(accColor, accWeight, float2( 0.0f,  2.0f) - f, n, dir, anisoScale);
    FsrTapAccum(accColor, accWeight, float2( 1.0f,  2.0f) - f, o, dir, anisoScale);

    float3 easu = (accWeight > 1e-5f) ? (accColor / accWeight) : fC;
    float3 min4 = min(min(fC, g), min(j, k));
    float3 max4 = max(max(fC, g), max(j, k));
    easu = clamp(easu, min4, max4);
    return float4(saturate(easu), 1.0f);
}

float4 PSFsrRcas(VSOut inp) : SV_TARGET {
    float2 invDst = 1.0f / float2(max(uDstW, 1u), max(uDstH, 1u));
    float3 c = texA.SampleLevel(linearSamp, inp.uv, 0.0f).rgb;
    if (uScaleSharpness <= 0.001f) {
        return float4(c, 1.0f);
    }

    float3 n = texA.SampleLevel(linearSamp, inp.uv + float2( 0.0f, -invDst.y), 0.0f).rgb;
    float3 s = texA.SampleLevel(linearSamp, inp.uv + float2( 0.0f,  invDst.y), 0.0f).rgb;
    float3 w = texA.SampleLevel(linearSamp, inp.uv + float2(-invDst.x,  0.0f), 0.0f).rgb;
    float3 e = texA.SampleLevel(linearSamp, inp.uv + float2( invDst.x,  0.0f), 0.0f).rgb;

    float lc = PerceptualLuma(c);
    float ln = PerceptualLuma(n), ls = PerceptualLuma(s), lw = PerceptualLuma(w), le = PerceptualLuma(e);

    float mn4 = min(min(ln, ls), min(lw, le));
    float mx4 = max(max(ln, ls), max(lw, le));
    float mnL = min(mn4, lc);
    float mxL = max(mx4, lc);

    float hitMin = mnL / max(4.0f * mx4, 1e-4f);
    float hitMax = (1.0f - mxL) / min(4.0f * mn4 - 4.0f, -1e-4f);
    float lobe = max(-hitMin, hitMax);
    float sharp = saturate(uScaleSharpness);
    float wRcas = clamp(lobe, -0.1875f * sharp, 0.0f);

    float3 outRgb = (c + (n + s + w + e) * wRcas) / (1.0f + 4.0f * wRcas);
    float3 minC = min(c, min(min(n, s), min(w, e)));
    float3 maxC = max(c, max(max(n, s), max(w, e)));
    outRgb = clamp(outRgb, minC, maxC);
    return float4(saturate(outRgb), 1.0f);
}

float3 SampleCatmullRom(Texture2D<float4> srcTex, float2 uv, float2 srcSize) {
    float2 invSrc = 1.0f / srcSize;
    float2 pos = uv * srcSize - 0.5f;
    float2 fp = floor(pos);
    float2 f = pos - fp;

    float2 w0 = f * (-0.5f + f * (1.0f - 0.5f * f));
    float2 w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
    float2 w2 = f * (0.5f + f * (2.0f - 1.5f * f));
    float2 w3 = f * f * (-0.5f + 0.5f * f);

    float2 w12 = w1 + w2;
    float2 off12 = w2 / max(w12, 1e-5f);

    float2 uv0 = (fp - 0.5f) * invSrc;
    float2 uv3 = (fp + 2.5f) * invSrc;
    float2 uv12 = (fp + 0.5f + off12) * invSrc;

    float3 c = float3(0.0f, 0.0f, 0.0f);
    c += srcTex.SampleLevel(linearSamp, float2(uv0.x,  uv0.y),  0.0f).rgb * (w0.x  * w0.y);
    c += srcTex.SampleLevel(linearSamp, float2(uv12.x, uv0.y),  0.0f).rgb * (w12.x * w0.y);
    c += srcTex.SampleLevel(linearSamp, float2(uv3.x,  uv0.y),  0.0f).rgb * (w3.x  * w0.y);

    c += srcTex.SampleLevel(linearSamp, float2(uv0.x,  uv12.y), 0.0f).rgb * (w0.x  * w12.y);
    c += srcTex.SampleLevel(linearSamp, float2(uv12.x, uv12.y), 0.0f).rgb * (w12.x * w12.y);
    c += srcTex.SampleLevel(linearSamp, float2(uv3.x,  uv12.y), 0.0f).rgb * (w3.x  * w12.y);

    c += srcTex.SampleLevel(linearSamp, float2(uv0.x,  uv3.y),  0.0f).rgb * (w0.x  * w3.y);
    c += srcTex.SampleLevel(linearSamp, float2(uv12.x, uv3.y),  0.0f).rgb * (w12.x * w3.y);
    c += srcTex.SampleLevel(linearSamp, float2(uv3.x,  uv3.y),  0.0f).rgb * (w3.x  * w3.y);
    return saturate(c);
}

float4 PSSpatialScale(VSOut inp) : SV_TARGET {
    float2 srcSize = float2(max(uSrcW, 1u), max(uSrcH, 1u));
    float2 invSrc = 1.0f / srcSize;

    // Mode 3: Exact Pixel-Perfect Integer Scaling
    if (uScaleMode == 3u) {
        int2 px = clamp(int2(inp.uv * srcSize), int2(0, 0), int2(int(uSrcW) - 1, int(uSrcH) - 1));
        return float4(texA.Load(int3(px, 0)).rgb, 1.0f);
    }

    // Mode 4: Anime4K Line-Thinning & Crisp Edge Sharpening
    if (uScaleMode == 4u) {
        float3 tl = texA.SampleLevel(linearSamp, inp.uv + float2(-invSrc.x, -invSrc.y), 0.0f).rgb;
        float3 t  = texA.SampleLevel(linearSamp, inp.uv + float2( 0.0f,     -invSrc.y), 0.0f).rgb;
        float3 tr = texA.SampleLevel(linearSamp, inp.uv + float2( invSrc.x, -invSrc.y), 0.0f).rgb;
        float3 l  = texA.SampleLevel(linearSamp, inp.uv + float2(-invSrc.x,  0.0f),     0.0f).rgb;
        float3 r  = texA.SampleLevel(linearSamp, inp.uv + float2( invSrc.x,  0.0f),     0.0f).rgb;
        float3 bl = texA.SampleLevel(linearSamp, inp.uv + float2(-invSrc.x,  invSrc.y), 0.0f).rgb;
        float3 b  = texA.SampleLevel(linearSamp, inp.uv + float2( 0.0f,      invSrc.y), 0.0f).rgb;
        float3 br = texA.SampleLevel(linearSamp, inp.uv + float2( invSrc.x,  invSrc.y), 0.0f).rgb;

        float ltl = PerceptualLuma(tl), lt = PerceptualLuma(t), ltr = PerceptualLuma(tr);
        float ll  = PerceptualLuma(l),                          lr  = PerceptualLuma(r);
        float lbl = PerceptualLuma(bl), lb = PerceptualLuma(b), lbr = PerceptualLuma(br);

        float gx = (ltr + 2.0f * lr + lbr) - (ltl + 2.0f * ll + lbl);
        float gy = (lbl + 2.0f * lb + lbr) - (ltl + 2.0f * lt + ltr);
        float edgeMag = saturate(sqrt(gx * gx + gy * gy) * 1.4f);
        float2 gradDir = (edgeMag > 0.02f) ? normalize(float2(gx, gy)) : float2(0.0f, 0.0f);

        float pushScale = (0.35f + 0.55f * saturate(uScaleSharpness)) * edgeMag;
        float2 sampleA = saturate(inp.uv + gradDir * invSrc * (0.45f * pushScale));
        float2 sampleB = saturate(inp.uv - gradDir * invSrc * (0.45f * pushScale));
        float3 cA = SampleCatmullRom(texA, sampleA, srcSize);
        float3 cB = SampleCatmullRom(texA, sampleB, srcSize);
        float3 cBase = SampleCatmullRom(texA, inp.uv, srcSize);
        float3 cDarkPush = (PerceptualLuma(cA) < PerceptualLuma(cB)) ? cA : cB;
        float3 lineCrisp = lerp(cBase, cDarkPush, 0.48f * edgeMag * (0.4f + 0.6f * saturate(uScaleSharpness)));

        float3 blur4 = 0.25f * (t + b + l + r);
        float3 outRgb = lineCrisp + (lineCrisp - blur4) * (0.45f * saturate(uScaleSharpness));
        return float4(saturate(outRgb), 1.0f);
    }

    // Mode 2 (and default fallback): NVIDIA NIS style Catmull-Rom + directional edge sharpening
    float3 base = SampleCatmullRom(texA, inp.uv, srcSize);
    float3 n = texA.SampleLevel(linearSamp, inp.uv + float2( 0.0f, -invSrc.y), 0.0f).rgb;
    float3 s = texA.SampleLevel(linearSamp, inp.uv + float2( 0.0f,  invSrc.y), 0.0f).rgb;
    float3 w = texA.SampleLevel(linearSamp, inp.uv + float2(-invSrc.x,  0.0f), 0.0f).rgb;
    float3 e = texA.SampleLevel(linearSamp, inp.uv + float2( invSrc.x,  0.0f), 0.0f).rgb;

    float ln = PerceptualLuma(n), ls = PerceptualLuma(s), lw = PerceptualLuma(w), le = PerceptualLuma(e);
    float lc = PerceptualLuma(base);
    float meanCross = 0.25f * (ln + ls + lw + le);
    float detail = clamp(lc - meanCross, -0.22f, 0.22f);
    float coring = smoothstep(0.005f, 0.025f, abs(detail));
    float sharp = saturate(uScaleSharpness);
    float targetL = max(lc + detail * coring * (1.25f * sharp), 0.0f);
    float3 sharpened = (lc > 1e-4f) ? (base * (targetL / lc)) : base;
    float3 minC = min(base, min(min(n, s), min(w, e)));
    float3 maxC = max(base, max(max(n, s), max(w, e)));
    return float4(saturate(clamp(sharpened, minC - 0.06f, maxC + 0.06f)), 1.0f);
}
)HLSL";
