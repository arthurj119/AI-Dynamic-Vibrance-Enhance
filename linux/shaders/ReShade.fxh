// Bundled MIT-licensed ReShade FX compatibility header for Linux vkBasalt
// Part of AI Dynamic Vibrance Enhance (v2.0.0) - https://github.com/arthurj119/AI-Dynamic-Vibrance-Enhance
#pragma once

#if !defined(__RESHADE__) || __RESHADE__ < 30000
    #ifndef __RESHADE_PERFORMANCE_MODE__
        #define __RESHADE_PERFORMANCE_MODE__ 0
    #endif
#endif

#ifndef BUFFER_WIDTH
    #define BUFFER_WIDTH 1920
#endif
#ifndef BUFFER_HEIGHT
    #define BUFFER_HEIGHT 1080
#endif
#ifndef BUFFER_RCP_WIDTH
    #define BUFFER_RCP_WIDTH (1.0 / BUFFER_WIDTH)
#endif
#ifndef BUFFER_RCP_HEIGHT
    #define BUFFER_RCP_HEIGHT (1.0 / BUFFER_HEIGHT)
#endif
#ifndef BUFFER_PIXEL_SIZE
    #define BUFFER_PIXEL_SIZE float2(BUFFER_RCP_WIDTH, BUFFER_RCP_HEIGHT)
#endif
#ifndef BUFFER_SCREEN_SIZE
    #define BUFFER_SCREEN_SIZE float2(BUFFER_WIDTH, BUFFER_HEIGHT)
#endif

#ifndef RESHADE_DEPTH_INPUT_IS_UPSIDE_DOWN
    #define RESHADE_DEPTH_INPUT_IS_UPSIDE_DOWN 0
#endif
#ifndef RESHADE_DEPTH_INPUT_IS_REVERSED
    #define RESHADE_DEPTH_INPUT_IS_REVERSED 0
#endif
#ifndef RESHADE_DEPTH_INPUT_IS_LOGARITHMIC
    #define RESHADE_DEPTH_INPUT_IS_LOGARITHMIC 0
#endif
#ifndef RESHADE_DEPTH_LINEARIZATION_FAR_PLANE
    #define RESHADE_DEPTH_LINEARIZATION_FAR_PLANE 1000.0
#endif

namespace ReShade
{
    static const float2 PixelSize = float2(BUFFER_RCP_WIDTH, BUFFER_RCP_HEIGHT);
    static const float2 ScreenSize = float2(BUFFER_WIDTH, BUFFER_HEIGHT);

    texture BackBufferTex : COLOR;
    texture DepthBufferTex : DEPTH;

    sampler BackBuffer { Texture = BackBufferTex; };
    sampler DepthBuffer { Texture = DepthBufferTex; };

    float GetLinearizedDepth(float2 texcoord)
    {
#if RESHADE_DEPTH_INPUT_IS_UPSIDE_DOWN
        texcoord.y = 1.0 - texcoord.y;
#endif
        float depth = tex2Dlod(DepthBuffer, float4(texcoord, 0, 0)).x;

#if RESHADE_DEPTH_INPUT_IS_LOGARITHMIC
        const float C = 0.01;
        depth = (exp(depth * log(C + 1.0)) - 1.0) / C;
#endif
#if RESHADE_DEPTH_INPUT_IS_REVERSED
        depth = 1.0 - depth;
#endif
        const float N = 1.0;
        depth /= RESHADE_DEPTH_LINEARIZATION_FAR_PLANE - depth * (RESHADE_DEPTH_LINEARIZATION_FAR_PLANE - N);
        return saturate(depth);
    }
}

void PostProcessVS(in uint id : SV_VertexID, out float4 position : SV_Position, out float2 texcoord : TEXCOORD)
{
    texcoord.x = (id == 2) ? 2.0 : 0.0;
    texcoord.y = (id == 1) ? 2.0 : 0.0;
    position = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
