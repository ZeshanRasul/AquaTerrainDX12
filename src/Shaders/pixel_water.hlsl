#include "LightingUtil.hlsl"

Texture2D gDepth : register(t0);

SamplerState gSamplerPointClamp : register(s0);

cbuffer WaterCB : register(b3)
{
    float4x4 gWorld;
    float4x4 gViewProj2;
    float3 gCameraPos;
    float gTime;
    float3 gWaterColor;
    float gPad0;
};
    
cbuffer cbPass : register(b2)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float3 gEyePosW;
    float cbPerObjectPad1;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float cbPerObjectPad2;
    float cbPerObjectPad3;
    float4 gAmbientLight;
    
    float4 gFogColor;
    float gFogStart;
    float gFogRange;
    
    Light gLights[MaxLights];
};

struct VSOutput
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float2 TexC : TEXCOORD;
};

float LinearizeDepth(float depth)
{
    float n = gNearZ;
    float f = gFarZ;

    return (n * f) / (f - depth * (f - n));
}

float3 EvaluateSky(float3 dir)
{
    // Very simple: horizon brighter, zenith darker
    float t = saturate(dir.y * 0.5f + 0.5f);
    float3 top = float3(0.55f, 0.75f, 0.95f);
    float3 bottom = float3(0.7f, 0.8f, 0.9f);
    return lerp(bottom, top, t);
}

float4 PS(VSOutput pin) : SV_TARGET
{
    float gAbsorptionStrength = 0.6f; 
    float3 gShallowWaterColor = float3(0.0f, 0.3f, 0.5f);
    float3 gDeepWaterColor = float3(0.0f, 0.05f, 0.1f);
    float gBaseAlpha = 0.5f;
    
    float2 uv = pin.PosH.xy * gInvRenderTargetSize;

    float sceneDepthNonLinear = gDepth.SampleLevel(gSamplerPointClamp, uv, 0).r;

    float waterDepthNonLinear = pin.PosH.z;

    float sceneDepthLinear = LinearizeDepth(sceneDepthNonLinear);
    float waterDepthLinear = LinearizeDepth(waterDepthNonLinear);

    float thickness = max(sceneDepthLinear - waterDepthLinear, 0.0f);

    float absorption = saturate(thickness * gAbsorptionStrength);

    float3 waterColor = lerp(gShallowWaterColor, gDeepWaterColor, absorption);

    float3 N = normalize(pin.NormalW);
    float3 V = normalize(gEyePosW - pin.PosW);
    float NdotV = saturate(dot(N, V));

    float fresnel = pow(1.0f - NdotV, 5.0f);

    float alpha = saturate(lerp(gBaseAlpha, 1.0f, fresnel));

    if (thickness <= 0.0f)
    {
        alpha *= 0.3f;
    }

    return float4(waterColor, alpha);
}

