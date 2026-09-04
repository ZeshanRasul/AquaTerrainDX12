cbuffer WaterSimConstants : register(b0)
{
    uint gGridWidth;
    uint gGridHeight;
    float gK1;
    float gK2;
    float gK3;
    float gPad0;
    float gPad1;
    float gPad2;
};

Texture2D<float> gPrevHeight : register(t0);
Texture2D<float> gCurrHeight : register(t1);
RWTexture2D<float> gOutHeight : register(u0);

[numthreads(16, 16, 1)]
void CS(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint x = dispatchThreadID.x;
    uint y = dispatchThreadID.y;

    if (x >= gGridWidth || y >= gGridHeight)
    {
        return;
    }

    bool isBoundary = (x == 0) || (y == 0) || (x == (gGridWidth - 1)) || (y == (gGridHeight - 1));

    if (isBoundary)
    {
        gOutHeight[uint2(x, y)] = 0.0f;
        return;
    }

    float prev = gPrevHeight[uint2(x, y)];
    float curr = gCurrHeight[uint2(x, y)];

    float up = gCurrHeight[uint2(x, y - 1)];
    float down = gCurrHeight[uint2(x, y + 1)];
    float left = gCurrHeight[uint2(x - 1, y)];
    float right = gCurrHeight[uint2(x + 1, y)];

    float next =
        gK1 * prev +
        gK2 * curr +
        gK3 * (up + down + left + right);

    gOutHeight[uint2(x, y)] = next;
}