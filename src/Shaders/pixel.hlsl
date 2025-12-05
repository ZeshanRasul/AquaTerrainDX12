#ifndef NUM_DIR_LIGHTS
    #define NUM_DIR_LIGHTS 1
#endif

#include "LightingUtil.hlsl"
Texture2D gGrassDiffuseMap : register(t0);
    
Texture2D gGrassNormalMap : register(t1);
Texture2D gMudDiffuseMap : register(t2);
Texture2D gMudNormalMap : register(t3);
TextureCube gCubeMap : register(t4);

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

cbuffer cbMaterial : register(b1)
{
    float4 gDiffuseAlbedo;
    float3 gFresnelR0;
    float gRoughness;
    float4x4 gMatTransform;
}

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

float4 PS(PixelIn pIn) : SV_Target
{
    float height = pIn.PosW.y;
    float3 N = normalize(pIn.NormalW);
    float Ny = saturate(N.y); // 1 = flat, 0 = vertical

    float gMudStartHeight = 7.0f; 
    float gGrassStartHeight = 9.0f; 
    float gHeightBlendRange = 3.0f; 

    float gMaxGrassSlope = 0.75f;
    float gMudSlopeBias = 0.2f;
    float gMudSlopePower = 2.0f;

    float gMudTiling = 1.0f;
    float gGrassTiling = 2.0f;

    float wGrass = smoothstep(gGrassStartHeight - gHeightBlendRange,
                              gGrassStartHeight + gHeightBlendRange,
                              height);

    float wMud = 1.0f - wGrass;

    float slopeFactor = 1.0f - Ny; // 0 flat, 1 vertical

    float mudSlopeBoost = pow(saturate(slopeFactor + gMudSlopeBias), gMudSlopePower);
    wMud = saturate(wMud + mudSlopeBoost);

    float sumW = wGrass + wMud + 1e-5f;
    wGrass /= sumW;
    wMud /= sumW;

    float2 uvGrass = pIn.TexC * gGrassTiling;
    float2 uvMud = pIn.TexC * gMudTiling;

    float3 albedoGrass = gGrassDiffuseMap.Sample(gsamAnisotropicWrap, uvGrass).rgb;
    float3 albedoMud = gMudDiffuseMap.Sample(gsamAnisotropicWrap, uvMud).rgb;

    float3 normalGrass = gGrassNormalMap.Sample(gsamAnisotropicWrap, uvGrass).xyz * 2.0f - 1.0f;
    float3 normalMud = gMudNormalMap.Sample(gsamAnisotropicWrap, uvMud).xyz * 2.0f - 1.0f;

    float3 blendedNormal =
        wGrass * normalGrass +
        wMud * normalMud;

    blendedNormal = normalize(blendedNormal);

    float3 albedo =
        wGrass * albedoGrass +
        wMud * albedoMud;

    float3 L = normalize(-gLights[0].Direction);
    float NdotL = saturate(dot(blendedNormal, L));

    float3 diffuse = albedo * NdotL;

    float3 ambient = albedo * 0.1f;

    float3 color = diffuse + ambient;

    return float4(color, 1.0f);
}
