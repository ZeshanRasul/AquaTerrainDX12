struct ShadowHitInfo
{
    bool isHit;
};

struct Attributes
{
    float2 uv;
};

[shader("miss")]
void ShadowMiss(inout ShadowHitInfo hit)
{
    hit.isHit = false;
}