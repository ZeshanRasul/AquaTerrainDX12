Texture3D<float> Fields[16] : register(t0);
StructuredBuffer<float4> Positions : register(t16);
RWStructuredBuffer<float> Results : register(u0);
cbuffer Settings : register(b0) { uint Count; uint Size; uint Bx; uint By; uint Bz; };
SamplerState LinearClamp : register(s0);
[numthreads(64, 1, 1)]
void Main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Count) return;
    float3 uv = Positions[id.x].xyz;
    precise float3 p = uv * Size - 0.5;
    Results[id.x * 20 + 0] = p.x - Bx;
    Results[id.x * 20 + 1] = p.y - By;
    Results[id.x * 20 + 2] = p.z - Bz;
    Results[id.x * 20 + 3] = 0;
    [unroll] for (uint i = 0; i < 16; ++i)
        Results[id.x * 20 + 4 + i] = Fields[i].SampleLevel(LinearClamp, uv, 0);
}
