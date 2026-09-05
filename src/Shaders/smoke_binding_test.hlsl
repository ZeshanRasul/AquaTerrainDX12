cbuffer SmokeSourceConstants : register(b0)
{
    uint3 GridResolution;
    float Dt;

    uint3 SourceCell;
    float DensityRate;

    float TemperatureRate;
    float3 Padding;
};

RWTexture3D<float> Density : register(u0);
RWTexture3D<float> Temperature : register(u1);

[numthreads(8, 8, 4)]
void ClearSourceFieldsCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= GridResolution))
        return;

    Density[id] = 0.0f;
    Temperature[id] = 0.0f;
}

[numthreads(8, 8, 4)]
void InjectSourceCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= GridResolution))
        return;

    if (all(id == SourceCell))
    {
        Density[id] += DensityRate * Dt;
        Temperature[id] += TemperatureRate * Dt;
    }
}
