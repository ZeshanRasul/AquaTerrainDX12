cbuffer SmokeConstants : register(b0)
{
    float4x4 gWorld;
    float4x4 gViewProj;
    float3 gCameraPositionLocal;
    float gDensityScale;
    float3 gSmokeColour;
    float gAbsorption;
    float3 gLightDirectionTexture;
    float gStepScale;
};

struct VertexIn
{
    float3 PositionLocal : POSITION;
    float3 NormalLocal : NORMAL;
    float2 TexCoord : TEXCOORD;
};

struct VertexOut
{
    float4 PositionHomogeneous : SV_POSITION;
    float3 PositionLocal : POSITION0;
};

VertexOut VS(VertexIn input)
{
    VertexOut output;
    const float4 positionWorld = mul(float4(input.PositionLocal, 1.0f), gWorld);
    output.PositionHomogeneous = mul(positionWorld, gViewProj);
    output.PositionLocal = input.PositionLocal;
    return output;
}
