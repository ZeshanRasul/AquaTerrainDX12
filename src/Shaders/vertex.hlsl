#include "LightingUtil.hlsl"

Texture2D gGrassDiffuseMap : register(t0);
    
Texture2D gGrassNormalMap : register(t1);
Texture2D gMudDiffuseMap : register(t2);
Texture2D gMudNormalMap : register(t3);
TextureCube gCubeMap : register(t4);
    
Texture2D<float> gHeightMap : register(t5);
SamplerState gsamPointWrap : register(s0);
SamplerState gsamPointClamp : register(s1);
SamplerState gsamLinearWrap : register(s2);
SamplerState gsamLinearClamp : register(s3);
SamplerState gsamAnisotropicWrap : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gTexTransform;
}

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

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
};

struct VSOutput
    
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float2 TexC : TEXCOORD;
};

VSOutput VS(VertexIn vIn)
{
    VSOutput vout;
    float2 gTerrainSize = float2(460.0f, 460.0f); 
    float gHeightScale = 50.0f; 
    
    float3 posW = mul(float4(vIn.PosL, 1.0f), gWorld).xyz;

    float u = (posW.x / gTerrainSize.x) + 0.5f;
    float v = (posW.z / gTerrainSize.y) + 0.5f;
    float2 uv = float2(u, v);

    float height = gHeightMap.SampleLevel(gsamLinearClamp, uv, 0.0f);

    posW.y = height * gHeightScale;
    float4x4 gWorldViewProj = mul(gWorld, gViewProj);
    float4 posH = mul(float4(posW, 1.0f), gWorldViewProj);
    
    vout.PosH = posH;
    vout.PosW = posW;
    vout.PosW.y = posH.y;
    vout.NormalW = vIn.NormalL; 
    vout.TexC = uv;

    return vout;
}