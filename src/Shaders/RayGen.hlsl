#include "Common.hlsl"
#define MaxLights 16
struct Light
{
    float3 Strength;
    float FalloffStart;
    float3 Direction;
    float FalloffEnd;
    float3 Position;
    float SpotPower;
};
// Raytracing output texture, accessed as a UAV
RWTexture2D<float4> gOutput : register(u0);

// Raytracing acceleration structure, accessed as a SRV
RaytracingAccelerationStructure SceneBVH : register(t0);

cbuffer cbPass : register(b0)
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
    
    Light gLights[MaxLights];
};

cbuffer PostProcess : register(b3)
{
    float Exposure;
    int ToneMapMode;
    int DebugMode;
    float pad;
}

float3 LinearToSRGB(float3 x)
{
    const float a = 0.055f;
    float3 lo = 12.92f * x;
    float3 hi = (1.0f + a) * pow(x, 1.0f / 2.4f) - a;
    
    return select(lo, hi, x > 0.0031308f);
}

float3 ApplyExposure(float3 color, float exposure)
{
    return color * exp2(exposure);
}

float3 ToneMapReinhard(float3 x)
{
    return x / (1.0f + x);
}

float3 RRTAndODTFit(float3 v)
{
    float3 a = v * (v + 0.0245786f) - 0.000090537f;
    float3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
    return a / b;
}

float3 ToneMapACES(float3 color)
{
    color = RRTAndODTFit(color);
    return saturate(color);

}

float3 PostProcess(float3 hdrColor)
{
    float3 color = ApplyExposure(hdrColor, Exposure);

    if (ToneMapMode == 1)
    {
        color = ToneMapReinhard(color);
    }
    else if (ToneMapMode == 2)
    {
        color = ToneMapACES(color);
    }
    else
    {
        color = saturate(color);
    }

    color = LinearToSRGB(color);

    return color;
}

[shader("raygeneration")]
void RayGen()
{
  // Initialize the ray payload
    HitInfo payload;
    payload.colorAndDistance = float4(0, 0, 0, 0);
    payload.depth = 0;
    payload.eta = 1.0f;

    uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 dims = DispatchRaysDimensions().xy;

    // Normalized coordinates in NDC (-1..1)
    float2 pixelCenter = (float2(launchIndex) + 0.5f) / float2(dims);
    float2 d = pixelCenter * 2.0f - 1.0f; // NDC coords

    d.y = -d.y;

    // Construct a ray through the pixel in world space
    float4 originVS = float4(0, 0, 0, 1);
    float4 targetVS = mul(float4(d.x, d.y, 1.0, 1.0), gInvProj);
    targetVS /= targetVS.w;

    float3 originWS = mul(originVS, gInvView).xyz;
    float3 targetWS = mul(targetVS, gInvView).xyz;
    float3 dirWS = normalize(targetWS - originWS);

    RayDesc ray;
    ray.Origin = originWS;
    ray.Direction = dirWS;
    ray.TMin = 0.1f;
    ray.TMax = 1e38f;
    
    TraceRay(
    SceneBVH,
    RAY_FLAG_NONE,
    0XFF,
    0,
    3,
    0,
    ray,
    payload);
    
    float3 finalColor = PostProcess(payload.colorAndDistance.rgb);
    
    gOutput[launchIndex] = float4(finalColor, 1.f);
}
