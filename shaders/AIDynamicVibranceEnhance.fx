// AI Dynamic Vibrance Enhance - ReShade Shader (Windows ReShade & Linux vkBasalt)
// Safely removable anytime via the 'Remove .fx' button or uninstall script.

#include "ReShade.fxh"

uniform bool UI_EnableDynamicVibrance <
    ui_label = "Enable AI Dynamic Vibrance";
> = true;

uniform float UI_Intensity <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;
    ui_label = "Intensity (%)";
> = 50.0;

uniform float UI_SaturationBoost <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;
    ui_label = "Saturation boost (%)";
> = 50.0;

uniform bool UI_EnableDarkLighting <
    ui_label = "Enable Dark / Shadow Lighting";
> = false;

uniform float UI_DarkLighting <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;
    ui_label = "Dark / Shadow lighting (%)";
> = 50.0;

uniform bool UI_EnableAO <
    ui_label = "Enable Ambient Occlusion";
> = false;

uniform float UI_AOIntensity <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;
    ui_label = "AO Intensity (%)";
> = 50.0;

uniform float UI_AORadius <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;
    ui_label = "AO Radius (%)";
> = 50.0;

uniform bool UI_AOUseDepth <
    ui_label = "Use 3D Depth Buffer SSAO (auto fallback if unavailable)";
> = true;

uniform bool UI_EnableDynamicHdr <
    ui_label = "Enable Dynamic HDR Tone";
> = false;

uniform float UI_DynamicHdr <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;
    ui_label = "Dynamic HDR tone (%)";
> = 50.0;

uniform float UI_HdrMidGray <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;
    ui_label = "HDR Mid Gray / Contrast (%)";
> = 50.0;

uniform bool UI_EnableClarity <
    ui_label = "Enable Clarity / Details";
> = false;

uniform float UI_Clarity <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;
    ui_label = "Clarity (%)";
> = 50.0;

uniform bool UI_EnableBC <
    ui_label = "Enable Brightness / Contrast";
> = false;

uniform float UI_Brightness <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;
    ui_label = "Brightness (%)";
> = 50.0;

uniform float UI_Contrast <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;
    ui_label = "Contrast (%)";
> = 50.0;

uniform bool UI_EnableSharpen <
    ui_label = "Enable Sharpen+ (AMD CAS)";
> = false;

uniform float UI_Sharpen <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;
    ui_label = "Sharpness (%)";
> = 50.0;

uniform bool UI_EnableGammaExp <
    ui_label = "Enable Gamma / Exposure";
> = false;

uniform float UI_Exposure <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;
    ui_label = "Exposure (%)";
> = 50.0;

uniform float UI_Gamma <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;
    ui_label = "Gamma (%)";
> = 50.0;

uniform bool UI_EnableColorTemp <
    ui_label = "Enable Color Temperature & Tint";
> = false;

uniform float UI_Temperature <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;
    ui_label = "Temperature (%)";
> = 50.0;

uniform float UI_Tint <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;
    ui_label = "Tint (%)";
> = 50.0;

uniform bool UI_EnableNightMode <
    ui_label = "Enable Night Mode / Blue Light";
> = false;

uniform float UI_NightMode <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;
    ui_label = "Warmth (%)";
> = 50.0;

uniform bool UI_EnableColorblind <
    ui_label = "Enable Colorblind Assist";
> = false;

uniform int UI_ColorblindType <
    ui_type = "combo";
    ui_items = "Protanopia (Red)\0Deuteranopia (Green)\0Tritanopia (Blue-Yellow)\0";
    ui_label = "Colorblind mode";
> = 0;

uniform float UI_ColorblindStrength <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;
    ui_label = "Assist Strength (%)";
> = 50.0;

uniform bool UI_EnableBW <
    ui_label = "Enable Black & White";
> = false;

uniform float UI_BW <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;
    ui_label = "Black & White (%)";
> = 100.0;

uniform bool UI_EnableVignette <
    ui_label = "Enable Cinematic Vignette";
> = false;

uniform float UI_Vignette <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;
    ui_label = "Vignette Strength (%)";
> = 50.0;

uniform bool UI_EnableBloom <
    ui_label = "Enable Cinematic Bloom";
> = false;

uniform float UI_BloomIntensity <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;
    ui_label = "Bloom Intensity (%)";
> = 50.0;

uniform float UI_BloomThreshold <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;
    ui_label = "Bloom Threshold (%)";
> = 60.0;

uniform bool UI_EnableFilmic <
    ui_label = "Enable Filmic Grading";
> = false;

uniform int UI_FilmicMode <
    ui_type = "combo";
    ui_items = "ACES\0AgX\0Technicolor\0";
    ui_label = "Filmic Curve Mode";
> = 0;

uniform float UI_FilmicStrength <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;
    ui_label = "Filmic Grading Strength (%)";
> = 50.0;

uniform bool UI_EnableDeband <
    ui_label = "Enable Deband";
> = false;

uniform float UI_DebandStrength <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;
    ui_label = "Deband Strength (%)";
> = 50.0;

uniform bool UI_SplitPreview <
    ui_label = "Enable Split Preview (Before / After)";
> = false;

uniform float UI_SplitPosition <
    ui_type = "slider";
    ui_min = 5.0; ui_max = 95.0; ui_step = 1.0;
    ui_label = "Split Preview Position (%)";
> = 50.0;


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
