cbuffer SmokeSourceConstants : register(b0)
{
    uint3 GridResolution;
    float Dt;

    uint3 SourceCell;
    float DensityRate;

    float TemperatureRate;
    float ambientTemperature;
    float temperatureBuoyancy;
    float smokeWeight;
    
    float hx;
    float hy;
    float hz;
    float pad;
    
    float3 GridSpacing;
    float FluidDensity;

    float JacobiWeight; // Start with 2.0 / 3.0.
    float3 Padding;
};

// Used by clear and source injection.
RWTexture3D<float> Density : register(u0);
RWTexture3D<float> Temperature : register(u1);
RWTexture3D<float> VelocityU : register(u2);
RWTexture3D<float> VelocityV : register(u3);
RWTexture3D<float> VelocityW : register(u4);
RWTexture3D<float> PressureReadInput : register(u7);
RWTexture3D<float> PressureWriteInput : register(u8);

// Used by buoyancy.
Texture3D<float> DensityInput : register(t0);
Texture3D<float> TemperatureInput : register(t1);

// Used by divergence.
Texture3D<float> VelocityUInput : register(t2);
Texture3D<float> VelocityVInput : register(t3);
Texture3D<float> VelocityWInput : register(t4);
RWTexture3D<float> Divergence : register(u5);

// Used by pressure solve.
Texture3D<float> DivergenceRead : register(t5);
Texture3D<float> PressureRead : register(t6);
RWTexture3D<float> PressureWrite : register(u6);

[numthreads(8, 8, 4)]
void ClearSourceFieldsCS(uint3 id : SV_DispatchThreadID)
{
    // Scalar textures: Nx × Ny × Nz.
    if (all(id < GridResolution))
    {
        Density[id] = 0.0f;
        Temperature[id] = 0.0f;
    }

    if (id.x <= GridResolution.x &&
    id.y < GridResolution.y &&
    id.z < GridResolution.z)
    {
        VelocityU[id] = 0.0f;
    }

    if (id.x < GridResolution.x &&
    id.y <= GridResolution.y &&
    id.z < GridResolution.z)
    {
        VelocityV[id] = 0.0f;
    }

    if (id.x < GridResolution.x &&
    id.y < GridResolution.y &&
    id.z <= GridResolution.z)
    {
        VelocityW[id] = 0.0f;
    }
    
    if (all(id < GridResolution))
    {
        PressureReadInput[id] = 0.0f;
        PressureWriteInput[id] = 0.0f;
    }
}

[numthreads(8, 8, 4)]
void ClearPressureCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= GridResolution))
        return;

    PressureReadInput[id] = 0.0f;
    PressureWriteInput[id] = 0.0f;
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

[numthreads(8, 8, 4)]
void ApplyBuoyancyCS(uint3 id : SV_DispatchThreadID)
{
    if (Dt <= 0.0f)
        return;

    // Interior V faces only:
    // x in [0, Nx), y in [1, Ny), z in [0, Nz).
    if (id.x >= GridResolution.x ||
        id.z >= GridResolution.z ||
        id.y == 0 ||
        id.y >= GridResolution.y)
    {
        return;
    }

    const uint3 below = id - uint3(0, 1, 0);
    const uint3 above = id;

    const float densityAtFace = 0.5f * (
        max(DensityInput.Load(int4(below, 0)), 0.0f) +
        max(DensityInput.Load(int4(above, 0)), 0.0f));

    const float temperatureAtFace = 0.5f * (
        TemperatureInput.Load(int4(below, 0)) +
        TemperatureInput.Load(int4(above, 0)));

    const float excessTemperature =
        max(temperatureAtFace - ambientTemperature, 0.0f);

    const float upwardAcceleration =
        temperatureBuoyancy * excessTemperature -
        smokeWeight * densityAtFace;

    VelocityV[id] += Dt * upwardAcceleration;
}

[numthreads(8, 8, 4)]
void ApplyDivergenceCS(uint3 id : SV_DispatchThreadID)
{
    Divergence[id] =
    (VelocityUInput.Load(int4(id + uint3(1, 0, 0), 0)) - VelocityUInput.Load(int4(id, 0))) / hx +
    (VelocityVInput.Load(int4(id + uint3(0, 1, 0), 0)) - VelocityVInput.Load(int4(id, 0))) / hy +
    (VelocityWInput.Load(int4(id + uint3(0, 0, 1), 0)) - VelocityWInput.Load(int4(id, 0))) / hz;
}

[numthreads(8, 8, 4)]
void ApplyPressureCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= GridResolution))
        return;

    const int3 cell = int3(id);

    const float ax = 1.0f / (GridSpacing.x * GridSpacing.x);
    const float ay = 1.0f / (GridSpacing.y * GridSpacing.y);
    const float az = 1.0f / (GridSpacing.z * GridSpacing.z);

    float neighbourSum = 0.0f;
    float diagonal = 0.0f;

    if (id.x > 0)
    {
        neighbourSum += ax *
            PressureRead.Load(int4(cell + int3(-1, 0, 0), 0));
        diagonal += ax;
    }

    if (id.x + 1 < GridResolution.x)
    {
        neighbourSum += ax *
            PressureRead.Load(int4(cell + int3(1, 0, 0), 0));
        diagonal += ax;
    }

    if (id.y > 0)
    {
        neighbourSum += ay *
            PressureRead.Load(int4(cell + int3(0, -1, 0), 0));
        diagonal += ay;
    }

    if (id.y + 1 < GridResolution.y)
    {
        neighbourSum += ay *
            PressureRead.Load(int4(cell + int3(0, 1, 0), 0));
        diagonal += ay;
    }

    if (id.z > 0)
    {
        neighbourSum += az *
            PressureRead.Load(int4(cell + int3(0, 0, -1), 0));
        diagonal += az;
    }

    if (id.z + 1 < GridResolution.z)
    {
        neighbourSum += az *
            PressureRead.Load(int4(cell + int3(0, 0, 1), 0));
        diagonal += az;
    }

    const float divergence =
        DivergenceRead.Load(int4(cell, 0));

    const float rhs = (FluidDensity / Dt) * divergence;

    const float oldPressure =
        PressureRead.Load(int4(cell, 0));

    const float candidate = (neighbourSum - rhs) / diagonal;

    PressureWrite[id] = lerp(
        oldPressure,
        candidate,
        JacobiWeight);
}