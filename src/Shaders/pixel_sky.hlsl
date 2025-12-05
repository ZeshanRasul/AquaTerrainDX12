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

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosL : POSITION;
};

float4 PS(VertexOut pin) : SV_Target
{
    return gCubeMap.Sample(gsamLinearWrap, pin.PosL);
}
