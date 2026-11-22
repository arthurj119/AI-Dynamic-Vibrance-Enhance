// AI Dynamic Vibrance Enhance - ReShade Shader (Legacy Exclusive Fullscreen Support)
// Safely removable anytime via the 'Remove .fx' button in AI Dynamic Vibrance Enhance.

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

uniform bool UI_EnableDynamicHdr <
    ui_label = "Enable Dynamic HDR Tone";
> = false;

uniform float UI_DynamicHdr <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 100.0; ui_step = 1.0;
    ui_label = "Dynamic HDR tone (%)";
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
