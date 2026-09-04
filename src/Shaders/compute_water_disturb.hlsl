cbuffer WaterDisturbConstants : register(b0)
{
    uint gGridWidth;
    uint gGridHeight;
    float gCenterX;
    float gCenterY;
    float gRadius;
    float gStrength;
    float gPad0;
    float gPad1;
};

RWTexture2D<float> gHeight : register(u0);

[numthreads(16, 16, 1)]
void CS(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint x = dispatchThreadID.x;
    uint y = dispatchThreadID.y;

    if (x >= gGridWidth || y >= gGridHeight)
    {
        return;
    }

    float2 p = float2((float)x, (float)y);
    float2 c = float2(gCenterX, gCenterY);
    float dist = distance(p, c);

    if (dist > gRadius)
    {
        return;
    }

    float falloff = 1.0f - saturate(dist / max(gRadius, 0.001f));
    gHeight[uint2(x, y)] += gStrength * falloff;
}
