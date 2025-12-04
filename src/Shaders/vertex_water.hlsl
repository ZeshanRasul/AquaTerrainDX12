cbuffer WaterCB : register(b3)
{
    float4x4 gWorld;
    float4x4 gViewProj;
    float3 gCameraPos;
    float gTime;
    float3 gWaterColor;
    float gPad0;
};

struct VSInput
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

VSOutput VS(VSInput vin)
{
    VSOutput vout;

    float3 worldPos = mul(float4(vin.PosL, 1.0f), gWorld).xyz;

    // Flat plane normal up
    float3 worldNormal = float3(0.0f, 1.0f, 0.0f);

    vout.PosW = worldPos;
    vout.PosH = mul(float4(worldPos, 1.0f), gViewProj);
    vout.NormalW = worldNormal;
    vin.TexC = vin.TexC;
    return vout;
}