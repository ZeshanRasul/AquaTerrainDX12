#include "LightingUtil.hlsl"

Texture2D gDepth : register(t0);

SamplerState gsamPointWrap : register(s0);
SamplerState gsamPointClamp : register(s1);
SamplerState gsamLinearWrap : register(s2);
SamplerState gsamLinearClamp : register(s3);
SamplerState gsamAnisotropicWrap : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);


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

float Wave(float2 p, float2 dir, float speed, float freq)
{
    return sin(dot(p, dir) * freq + gTime * speed);
}

float3 ComputeWaterNormal(float3 posW)
{
    float2 p = posW.xz;

    float w1 = Wave(p, normalize(float2(1.0f, 0.3f)), 1.2f, 0.08f);
    float w2 = Wave(p, normalize(float2(-0.4f, 1.0f)), 0.8f, 0.13f);
    float w3 = Wave(p, normalize(float2(0.7f, -0.6f)), 1.7f, 0.22f);

    float h = w1 * 0.25f + w2 * 0.12f + w3 * 0.05f;

    float eps = 0.4f;

    float hx =
        Wave(p + float2(eps, 0), normalize(float2(1.0f, 0.3f)), 1.2f, 0.08f) * 0.25f +
        Wave(p + float2(eps, 0), normalize(float2(-0.4f, 1.0f)), 0.8f, 0.13f) * 0.12f +
        Wave(p + float2(eps, 0), normalize(float2(0.7f, -0.6f)), 1.7f, 0.22f) * 0.05f;

    float hz =
        Wave(p + float2(0, eps), normalize(float2(1.0f, 0.3f)), 1.2f, 0.08f) * 0.25f +
        Wave(p + float2(0, eps), normalize(float2(-0.4f, 1.0f)), 0.8f, 0.13f) * 0.12f +
        Wave(p + float2(0, eps), normalize(float2(0.7f, -0.6f)), 1.7f, 0.22f) * 0.05f;

    float dhdx = (hx - h) / eps;
    float dhdz = (hz - h) / eps;

    return normalize(float3(-dhdx, 1.0f, -dhdz));
}

float Hash21(float2 p)
{
    p = frac(p * float2(123.34f, 456.21f));
    p += dot(p, p + 45.32f);
    return frac(p.x * p.y);
}

float ValueNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);

    float a = Hash21(i);
    float b = Hash21(i + float2(1, 0));
    float c = Hash21(i + float2(0, 1));
    float d = Hash21(i + float2(1, 1));

    float2 u = f * f * (3.0f - 2.0f * f);

    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float FBM(float2 p)
{
    float v = 0.0f;
    float amp = 0.5f;

    v += ValueNoise(p) * amp;
    p *= 2.03f;
    amp *= 0.5f;
    v += ValueNoise(p) * amp;
    p *= 2.01f;
    amp *= 0.5f;
    v += ValueNoise(p) * amp;

    return v;
}

float4 PS(VSOutput pin) : SV_TARGET
{
    float gAbsorptionStrength = 0.6f; 
    float3 gShallowWaterColor = float3(0.0f, 0.3f, 0.5f);
    float3 gDeepWaterColor = float3(0.0f, 0.05f, 0.1f);
    float gBaseAlpha = 0.5f;
    
    float2 uv = pin.PosH.xy * gInvRenderTargetSize;

    float sceneDepthNonLinear = gDepth.SampleLevel(gsamPointClamp, uv, 0).r;

    float waterDepthNonLinear = pin.PosH.z;

    float sceneDepthLinear = LinearizeDepth(sceneDepthNonLinear);
    float waterDepthLinear = LinearizeDepth(waterDepthNonLinear);

    float thickness = max(sceneDepthLinear - waterDepthLinear, 0.0f);

    float absorption = saturate(thickness * gAbsorptionStrength);

    float3 waterColor = lerp(gShallowWaterColor, gDeepWaterColor, absorption);

    float3 N = ComputeWaterNormal(pin.PosW);
    float3 V = normalize(gEyePosW - pin.PosW);
    float NdotV = saturate(dot(N, V));

    float fresnel = pow(1.0f - NdotV, 5.0f);

    float3 R = reflect(-V, N);
    float3 skyReflection = EvaluateSky(R);

    float reflectionStrength = lerp(0.05f, 0.65f, fresnel);
    waterColor = lerp(waterColor, skyReflection, reflectionStrength);
    
    float depthAlpha = saturate(thickness * 0.4f);
    float alpha = saturate(lerp(0.15f, 0.75f, depthAlpha));
    alpha = max(alpha, fresnel * 0.9f);
    
    float3 L = normalize(-gLights[0].Direction);
    float3 H = normalize(L + V);

    float spec = pow(saturate(dot(N, H)), 256.0f);
    float sparkle = pow(saturate(dot(N, H)), 800.0f);
 
    float foam = 1.0f - smoothstep(0.05f, 35.0f, thickness);

    float2 foamUV = pin.PosW.xz * 0.7f;
    foamUV += float2(gTime * 0.08f, gTime * 0.03f);

    float noise = FBM(foamUV);

    float breakup = smoothstep(0.35f, 0.85f, noise);

    foam *= breakup;
    
    waterColor = lerp(waterColor, float3(0.85f, 0.95f, 1.0f), foam * 0.9f);
    alpha = max(alpha, foam * 0.85f);
    
    waterColor += spec * float3(1.0f, 0.95f, 0.85f) * 0.4f;
    waterColor += sparkle * float3(1.0f, 0.95f, 0.8f) * 1.2f;
    return float4(waterColor, alpha);
}

