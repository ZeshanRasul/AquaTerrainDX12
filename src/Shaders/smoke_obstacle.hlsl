cbuffer ObstacleConstants : register(b0)
{
    float4x4 WorldViewProjection;
    float3 InverseWorldScale;
    float Padding0;
    float3 LightDirection;
    float Padding1;
    float4 Colour;
};

struct VertexIn
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
};

struct PixelIn
{
    float4 Position : SV_POSITION;
    float3 Normal : NORMAL;
};

PixelIn VS(VertexIn input)
{
    PixelIn output;
    output.Position = mul(float4(input.Position, 1.0f), WorldViewProjection);
    // The obstacle transform has scale and translation but no rotation.
    output.Normal = input.Normal * InverseWorldScale;
    return output;
}

float4 PS(PixelIn input) : SV_TARGET
{
    float diffuse = saturate(dot(normalize(input.Normal), -normalize(LightDirection)));
    return float4(Colour.rgb * (0.25f + 0.75f * diffuse), 1.0f);
}
