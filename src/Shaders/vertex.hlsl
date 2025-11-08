struct ObjectConstants
{
    float4x4 gWorldViewProj;
    uint matIndex;
};

ConstantBuffer<ObjectConstants> gObjConstants : register(b0);

struct VertexIn
{
    float3 PosL : POSITION;
    float4 Color : COLOR;
};

struct VertexOut
{
    float4 PosH : SV_Position;
    float4 Color : CoOLOR;
};
    


VertexOut VS(VertexIn vIn)
{
    VertexOut vOut;
    
    vOut.PosH = mul(float4(vIn.PosL, 1.0f), gWorldViewProj);
    
    vOut.Color = vIn.Color;
    
    return vOut;
}