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

float4 PS(PixelIn pIn) : SV_Target
{
    float height = pIn.PosW.y;

    float hSample = gHeightMap.SampleLevel(gsamLinearClamp, pIn.TexC, 0).r;
    height = hSample * gHeightScale + gHeightOffset;

    float2 texel = 1.0f / gTerrainSize;
    float hC = hSample;
    float hX = gHeightMap.SampleLevel(gsamLinearClamp, pIn.TexC + float2(texel.x, 0), 0).r;
    float hZ = gHeightMap.SampleLevel(gsamLinearClamp, pIn.TexC + float2(0, texel.y), 0).r;

    hC *= gHeightScale;
    hX *= gHeightScale;
    hZ *= gHeightScale;

    float dHx = (hX - hC) / (gTerrainSize.x * texel.x);
    float dHz = (hZ - hC) / (gTerrainSize.y * texel.y);

    float slope = saturate(sqrt(dHx * dHx + dHz * dHz) * 1.0f);
    float slopeFactor = slope;
    
    float wMudHeight = 1.0f - smoothstep(gMudStartHeight,
                                         gMudStartHeight + gHeightBlendRange,
                                         height);

    float wGrassHeight = smoothstep(gMudStartHeight,
                                    gMudStartHeight + gHeightBlendRange,
                                    height) *
                         (1.0f - smoothstep(gGrassStartHeight,
                                            gGrassStartHeight + gHeightBlendRange,
                                            height));

    float wRockHeight = smoothstep(gRockStartHeight,
                                   gRockStartHeight + gHeightBlendRange,
                                   height);

    float wGrass = wGrassHeight;
    float wMud = wMudHeight;
    float wRock = wRockHeight;

    float highFactor = smoothstep(gGrassStartHeight, gRockStartHeight, height);
    float lowFactor = 1.0f - smoothstep(gMudStartHeight, gGrassStartHeight, height);
    
    float mudSlopeBoost = pow(saturate(slopeFactor + gMudSlopeBias), gMudSlopePower);
    wMud = saturate(wMud + mudSlopeBoost * 0.5f);

    float rockSlopeBoost = pow(saturate(slopeFactor + gRockSlopeBias), gRockSlopePower);
    wRock = saturate(wRock + rockSlopeBoost);

    float grassSlopeDamp = saturate(1.0f - slopeFactor * 1.2f);
    wGrass *= grassSlopeDamp;

    float sumW = wGrass + wMud + wRock + 1e-5f;
    wGrass /= sumW;
    wMud /= sumW;
    wRock /= sumW;

    float2 uvGrass = pIn.TexC * gGrassTiling;
    float2 uvMud = pIn.TexC * gMudTiling;
    float2 uvRock = pIn.TexC * gRockTiling;

    float3 albedoGrass = gGrassDiffuseMap.Sample(gsamAnisotropicWrap, uvGrass).rgb * 5.0f;
    float3 albedoMud = gMudDiffuseMap.Sample(gsamAnisotropicWrap, uvMud).rgb;
    float3 albedoRock = gRockDiffuseMap.Sample(gsamAnisotropicWrap, uvRock).rgb * 1.5f;

    float3 normalGrass = gGrassNormalMap.Sample(gsamAnisotropicWrap, uvGrass).xyz * 2.0f - 1.0f;
    float3 normalMud = gMudNormalMap.Sample(gsamAnisotropicWrap, uvMud).xyz * 2.0f - 1.0f;
    float3 normalRock = gRockNormalMap.Sample(gsamAnisotropicWrap, uvRock).xyz * 2.0f - 1.0f;

    float3 blendedNormal =
        wGrass * normalGrass +
        wMud * normalMud +
        wRock * normalRock;

    blendedNormal = normalize(blendedNormal);

    float3 albedo =
        wGrass * albedoGrass +
        wMud * albedoMud +
        wRock * albedoRock;

    float3 L = normalize(-gLights[0].Direction);
    float NdotL = saturate(dot(blendedNormal, L));
    float3 diffuse = albedo * NdotL;

    float t = 0.5f * (blendedNormal.y + 1.0f);
    float3 skyCol = float3(0.3, 0.4, 0.6);
    float3 groundCol = float3(0.1, 0.08, 0.06);
    float3 hemiAmbient = lerp(groundCol, skyCol, t);
    float3 color = diffuse + hemiAmbient * albedo;

    float dist = length(gEyePosW - pIn.PosW);
    float fogAmount = saturate((dist - gFogStart) / gFogRange) * 0.1f;
    color = lerp(color, gFogColor.xyz, fogAmount);
   
    //return float4(wGrass, wMud, wRock, 1.0f);

    return float4(color, 1.0f);
}

