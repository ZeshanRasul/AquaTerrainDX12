// Experiment-only normalized source injection. SourceRate is a rate per second;
// the host supplies the fixed physical dt. Density/temperature updates are equal.
cbuffer InjectionConstants : register(b0)
{
    uint3 GridResolution;
    float Dt;
};
ByteAddressBuffer SourceRate : register(t0);
RWTexture3D<float> Density : register(u0);
RWTexture3D<float> Temperature : register(u1);

[numthreads(8, 8, 4)]
void InjectNormalizedSourceCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= GridResolution)) return;
    const uint index = (id.z * GridResolution.y + id.y) * GridResolution.x + id.x;
    precise float increment = asfloat(SourceRate.Load(index * 4)) * Dt;
    precise float density = Density[id];
    precise float temperature = Temperature[id];
    density = density + increment;
    temperature = temperature + increment;
    Density[id] = density;
    Temperature[id] = temperature;
}
