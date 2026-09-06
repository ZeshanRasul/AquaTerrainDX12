struct SmokeSphereObstacle
{
    float3 centre;
    float radius;
    uint enabled;
    float3 padding;
};

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
    float3 Padding; // Damping for density and temperature.
    
    float3 origin;
    float pad2;
    
    SmokeSphereObstacle sphereObstacle;
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

SamplerState LinearClamp : register(s0);



float SphereSdf(float3 position, SmokeSphereObstacle sphere)
{
    return length(position - sphere.centre) - sphere.radius;
}

bool IsSolidCell(int3 cell)
{
    // Treat the existing outer box as solid too.
    if (any(cell < 0) || any(cell >= int3(GridResolution)))
        return true;

    if (sphereObstacle.enabled == 0)
        return false;
    
    float3 position =
        origin + (float3(cell) + 0.5f) * GridSpacing;

    return SphereSdf(position, sphereObstacle) <= 0.0f;
}

float SampleU(float3 q)
{
    float3 uvw =
        (q + float3(0.5f, 0.0f, 0.0f)) /
        float3(GridResolution + uint3(1, 0, 0));

    return VelocityUInput.SampleLevel(LinearClamp, uvw, 0);
}

bool IsBlockedU(int3 face)
{
    return IsSolidCell(face - int3(1, 0, 0)) ||
           IsSolidCell(face);
}

bool IsBlockedV(int3 face)
{
    return IsSolidCell(face - int3(0, 1, 0)) ||
           IsSolidCell(face);
}
bool IsBlockedW(int3 face)
{
    return IsSolidCell(face - int3(0, 0, 1)) ||
           IsSolidCell(face);
}

float SampleV(float3 q)
{
    float3 uvw =
        (q + float3(0.0f, 0.5f, 0.0f)) /
        float3(GridResolution + uint3(0, 1, 0));

    return VelocityVInput.SampleLevel(LinearClamp, uvw, 0);
}

float SampleW(float3 q)
{
    float3 uvw =
        (q + float3(0.0f, 0.0f, 0.5f)) /
        float3(GridResolution + uint3(0, 0, 1));

    return VelocityWInput.SampleLevel(LinearClamp, uvw, 0);
}

float3 SampleVelocity(float3 q)
{
    return float3(SampleU(q), SampleV(q), SampleW(q));
}

float3 BackTrace(float3 q)
{
    float3 velocity0 = SampleVelocity(q);

    float3 midpoint =
        q - 0.5f * Dt * velocity0 / GridSpacing;

    float3 midpointVelocity = SampleVelocity(midpoint);

    return q - Dt * midpointVelocity / GridSpacing;
}

[numthreads(8, 8, 4)]
void ClearSourceFieldsCS(uint3 id : SV_DispatchThreadID)
{
    // Scalar textures: Nx � Ny � Nz.
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

    if (IsSolidCell(int3(id)))
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

    if (IsBlockedV(int3(id)))
    {
        VelocityV[id] = 0.0f;
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
    if (any(id >= GridResolution))
        return;

    int3 c = int3(id);

    if (IsSolidCell(c))
    {
        Divergence[id] = 0.0f;
        return;
    }

    int3 right = c + int3(1, 0, 0);
    int3 above = c + int3(0, 1, 0);
    int3 front = c + int3(0, 0, 1);

    float u0 = IsBlockedU(c) ? 0.0f :
        VelocityUInput.Load(int4(c, 0));
    float u1 = IsBlockedU(right) ? 0.0f :
        VelocityUInput.Load(int4(right, 0));

    float v0 = IsBlockedV(c) ? 0.0f :
        VelocityVInput.Load(int4(c, 0));
    float v1 = IsBlockedV(above) ? 0.0f :
        VelocityVInput.Load(int4(above, 0));

    float w0 = IsBlockedW(c) ? 0.0f :
        VelocityWInput.Load(int4(c, 0));
    float w1 = IsBlockedW(front) ? 0.0f :
        VelocityWInput.Load(int4(front, 0));

    Divergence[id] =
        (u1 - u0) / hx +
        (v1 - v0) / hy +
        (w1 - w0) / hz;
}

[numthreads(8, 8, 4)]
void ApplyPressureCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= GridResolution))
        return;

    int3 cell = int3(id);

    if (IsSolidCell(cell) || Dt <= 0.0f)
    {
        PressureWrite[id] = 0.0f;
        return;
    }

    float3 weights = 1.0f / (GridSpacing * GridSpacing);

    const int3 offsets[6] =
    {
        int3(-1, 0, 0), int3(1, 0, 0),
        int3(0, -1, 0), int3(0, 1, 0),
        int3(0, 0, -1), int3(0, 0, 1)
    };

    float neighbourSum = 0.0f;
    float diagonal = 0.0f;

    [unroll]
    for (int direction = 0; direction < 6; ++direction)
    {
        int3 neighbour = cell + offsets[direction];

        if (!IsSolidCell(neighbour))
        {
            float weight = weights[direction / 2];

            neighbourSum += weight *
                PressureRead.Load(int4(neighbour, 0));

            diagonal += weight;
        }
    }

    if (diagonal <= 0.0f)
    {
        PressureWrite[id] = 0.0f;
        return;
    }

    float divergence = DivergenceRead.Load(int4(cell, 0));
    float rhs = (FluidDensity / Dt) * divergence;
    float candidate = (neighbourSum - rhs) / diagonal;
    float oldPressure = PressureRead.Load(int4(cell, 0));

    PressureWrite[id] =
        lerp(oldPressure, candidate, JacobiWeight);
}

[numthreads(8, 8, 4)]

    void SubtractPressureGradientCS
    (
    uint3 id : SV_DispatchThreadID)
{
    const float scale = Dt / FluidDensity;
    const int3 cell = int3(id);

    // U: (Nx + 1) � Ny � Nz
    if (id.x <= GridResolution.x &&
        id.y < GridResolution.y &&
        id.z < GridResolution.z)
    {
        if (id.x == 0 || id.x == GridResolution.x || IsBlockedU(cell))
        {
            VelocityU[id] = 0.0f;
        }
        else
        {
            const float right =
                PressureRead.Load(int4(cell, 0));

            const float left =
                PressureRead.Load(int4(cell - int3(1, 0, 0), 0));

            VelocityU[id] -= scale * (right - left) / hx;
        }
    }

    // V: Nx � (Ny + 1) � Nz
    if (id.x < GridResolution.x &&
        id.y <= GridResolution.y &&
        id.z < GridResolution.z)
    {
        if (id.y == 0 || id.y == GridResolution.y || IsBlockedV(cell))
        {
            VelocityV[id] = 0.0f;
        }
        else
        {
            const float above =
                PressureRead.Load(int4(cell, 0));

            const float below =
                PressureRead.Load(int4(cell - int3(0, 1, 0), 0));

            VelocityV[id] -= scale * (above - below) / hy;
        }
    }

    // W: Nx � Ny � (Nz + 1)
    if (id.x < GridResolution.x &&
        id.y < GridResolution.y &&
        id.z <= GridResolution.z)
    {
        if (id.z == 0 || id.z == GridResolution.z || IsBlockedW(cell))
        {
            VelocityW[id] = 0.0f;
        }
        else
        {
            const float front =
                PressureRead.Load(int4(cell, 0));

            const float back =
                PressureRead.Load(int4(cell - int3(0, 0, 1), 0));

            VelocityW[id] -= scale * (front - back) / hz;
        }
    }
}

[numthreads(8, 8, 4)]

    void AdvectScalarsCS
    (
    uint3 id : SV_DispatchThreadID)
{
    if (any(id >= GridResolution))
        return;

    if (IsSolidCell(int3(id)))
    {
        Density[id] = 0.0f;
        Temperature[id] = ambientTemperature;
        return;
    }
    
    float3 q = float3(id) + 0.5f;
    float3 departure = BackTrace(q);

    float3 uvw = departure / float3(GridResolution);

    Density[id] =
        max(DensityInput.SampleLevel(LinearClamp, uvw, 0) * exp(-Padding.x * Dt), 0.0f);

    Temperature[id] =
        ambientTemperature + (TemperatureInput.SampleLevel(LinearClamp, uvw, 0) - ambientTemperature) * exp(-Padding.y * Dt);
}

[numthreads(8, 8, 4)]

    void AdvectVelocityCS
    (
    uint3 id : SV_DispatchThreadID)
{
    uint3 n = GridResolution;

    if (all(id < n + uint3(1, 0, 0)))
    {
        if (id.x == 0 || id.x == n.x || IsBlockedU(int3(id)))
        {
            VelocityU[id] = 0.0f;
        }
        else
        {
            float3 q = float3(id) + float3(0.0f, 0.5f, 0.5f);
            VelocityU[id] = SampleU(BackTrace(q));
        }
    }

    if (all(id < n + uint3(0, 1, 0)))
    {
        if (id.y == 0 || id.y == n.y || IsBlockedV(int3(id)))
        {
            VelocityV[id] = 0.0f;
        }
        else
        {
            float3 q = float3(id) + float3(0.5f, 0.0f, 0.5f);
            VelocityV[id] = SampleV(BackTrace(q));
        }
    }

    if (all(id < n + uint3(0, 0, 1)))
    {
        if (id.z == 0 || id.z == n.z || IsBlockedW(int3(id)))
        {
            VelocityW[id] = 0.0f;
        }
        else
        {
            float3 q = float3(id) + float3(0.5f, 0.5f, 0.0f);
            VelocityW[id] = SampleW(BackTrace(q));
        }
    }
}