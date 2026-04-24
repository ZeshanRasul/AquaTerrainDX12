#ifndef NUM_DIR_LIGHTS
    #define NUM_DIR_LIGHTS 1
#endif

#include "LightingUtil.hlsl"

Texture2D gGrassDiffuseMap : register(t0);
Texture2D gGrassNormalMap : register(t1);
Texture2D gMudDiffuseMap : register(t2);
Texture2D gMudNormalMap : register(t3);
TextureCube gCubeMap : register(t4);
Texture2D<float> gHeightMap : register(t5);
Texture2D gRockDiffuseMap : register(t6);
Texture2D gRockNormalMap : register(t7);

SamplerState gsamPointWrap : register(s0);
SamplerState gsamPointClamp : register(s1);
SamplerState gsamLinearWrap : register(s2);
SamplerState gsamLinearClamp : register(s3);
SamplerState gsamAnisotropicWrap : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);

struct PixelIn
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float2 TexC : TEXCOORD;
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

cbuffer cbTerrain : register(b3)
{
    float2 gTerrainSize;
    float gHeightScale;
    float gHeightOffset;

    float gMudStartHeight;
    float gGrassStartHeight;

    float gRockStartHeight;
    float gHeightBlendRange;

    float gMudSlopeBias;
    float gMudSlopePower;

    float gRockSlopeBias;
    float gRockSlopePower;

    float gMudTiling;
    float gGrassTiling;

    float gRockTiling;
    float gPad;
};

// Reconstruct a TBN matrix from screen-space derivatives so normal maps
// are correctly transformed to world space without needing a tangent vertex attribute.
float3x3 ReconstructTBN(float3 N, float3 posW, float2 uv)
{
    float3 dp1 = ddx(posW);
    float3 dp2 = ddy(posW);
    float2 duv1 = ddx(uv);
    float2 duv2 = ddy(uv);

    float3 dp2perp = cross(dp2, N);
    float3 dp1perp = cross(N, dp1);
    float3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    float3 B = dp2perp * duv1.y + dp1perp * duv2.y;

    float invMax = rsqrt(max(dot(T, T), dot(B, B)));
    return float3x3(T * invMax, B * invMax, N);
}

// Perturb the geometric normal using a tangent-space normal map sample.
float3 PerturbNormal(float3 N, float3 posW, float2 uv, float3 tsNormal)
{
    float3x3 TBN = ReconstructTBN(N, posW, uv);
    return normalize(mul(tsNormal, TBN));
}

float4 PS(PixelIn pIn) : SV_Target
{
    float height = pIn.PosW.y;
    float3 N = normalize(pIn.NormalW);
    float Ny = saturate(N.y);

    float2 uvGrass = pIn.TexC * gGrassTiling;
    float2 uvMud = pIn.TexC * gMudTiling;
    float2 uvRock = pIn.TexC * gRockTiling;

    float3 albedoGrass = gGrassDiffuseMap.Sample(gsamAnisotropicWrap, uvGrass).rgb;
    float3 albedoMud = gMudDiffuseMap.Sample(gsamAnisotropicWrap, uvMud).rgb;
    float3 albedoRock = gRockDiffuseMap.Sample(gsamAnisotropicWrap, uvRock).rgb;

    float3 tsNormalGrass = gGrassNormalMap.Sample(gsamAnisotropicWrap, uvGrass).xyz * 2.0f - 1.0f;
    float3 tsNormalMud = gMudNormalMap.Sample(gsamAnisotropicWrap, uvMud).xyz * 2.0f - 1.0f;
    float3 tsNormalRock = gRockNormalMap.Sample(gsamAnisotropicWrap, uvRock).xyz * 2.0f - 1.0f;

    float mudToGrass = smoothstep(gMudStartHeight, gGrassStartHeight, height);
    float grassToRock = smoothstep(gRockStartHeight - gHeightBlendRange, gRockStartHeight + gHeightBlendRange, height);

    float wMud = 1.0f - mudToGrass;
    float wGrass = mudToGrass * (1.0f - grassToRock);
    float wRock = grassToRock;

    float slopeFactor = 1.0f - Ny;

    float mudSlope = 1.0f - pow(saturate((slopeFactor - gMudSlopeBias) / max(1e-5f, 1.0f - gMudSlopeBias)), gMudSlopePower);
    float rockSlope = pow(saturate((slopeFactor - gRockSlopeBias) / max(1e-5f, 1.0f - gRockSlopeBias)), gRockSlopePower);

    mudSlope *= (1.0f - smoothstep(gGrassStartHeight, gRockStartHeight, height));
    rockSlope *= smoothstep(gGrassStartHeight, gRockStartHeight + gHeightBlendRange, height);

    wMud *= mudSlope;
    wRock = saturate(max(wRock, rockSlope));

    float sumW = wMud + wGrass + wRock + 1e-5f;
    wMud /= sumW;
    wGrass /= sumW;
    wRock /= sumW;

    // -------------------------------------------------------------------------
    // Blend albedo
    // -------------------------------------------------------------------------
    float3 albedo = wGrass * albedoGrass
                  + wMud * albedoMud
                  + wRock * albedoRock;

    // -------------------------------------------------------------------------
    // Blend and perturb normals (each layer transformed through its own TBN)
    // -------------------------------------------------------------------------
    float3 worldNormalGrass = PerturbNormal(N, pIn.PosW, uvGrass, tsNormalGrass);
    float3 worldNormalMud = PerturbNormal(N, pIn.PosW, uvMud, tsNormalMud);
    float3 worldNormalRock = PerturbNormal(N, pIn.PosW, uvRock, tsNormalRock);

    float3 blendedNormal = normalize(wGrass * worldNormalGrass
                                   + wMud * worldNormalMud
                                   + wRock * worldNormalRock);

    // -------------------------------------------------------------------------
    // Lighting — diffuse + hemisphere ambient
    // -------------------------------------------------------------------------
    float3 L = normalize(-gLights[0].Direction);
    float NdotL = saturate(dot(blendedNormal, L));
    float3 diffuse = albedo * gLights[0].Strength * NdotL;

    float t = 0.5f * (blendedNormal.y + 1.0f);
    float3 skyCol = float3(0.45f, 0.50f, 0.60f);
    float3 groundCol = float3(0.12f, 0.10f, 0.08f);
    float3 hemiAmbient = lerp(groundCol, skyCol, t) * albedo;

    float3 ambient = gAmbientLight.rgb * albedo + hemiAmbient;

    float3 color = diffuse + ambient;

    return float4(color, 1.0f);
}