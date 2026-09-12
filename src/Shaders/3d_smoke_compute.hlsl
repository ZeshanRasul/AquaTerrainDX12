struct SmokeSphereObstacle
{
    float3 centre;
    float radius;
    uint enabled;
    float3 velocity;
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
    float vorticityEpsilon;
    
    float3 GridSpacing;
    float FluidDensity;

    float JacobiWeight; // Start with 2.0 / 3.0.
    float2 Padding; // Damping for density and temperature.
    uint sphereMovementEnabled;
    
    float3 origin;
    uint openTopEnabled;
    
    SmokeSphereObstacle sphereObstacle;

    int limiterMode; // 0 = clamp, 1 = revert, 2 = adaptive

    // Passive-advection reference harness (inert in normal runs; all zero).
    int periodicDomain;
    int referenceVelocityMode;   // 0 zero, 1 translation +x, 2 rotation about z
    float referenceSpeed;        // cells/step (translation) or rad/step (rotation)
    float referenceBlobSigma;    // cells
    float3 referenceBlobCentre;  // cells
};

// MacCormack limiter applied to a corrected value against its source-field
// stencil bounds [lo, hi] and the first-order fallback phiHat.
float ApplyLimiter(float corrected, float phiHat, float lo, float hi)
{
    if (limiterMode == 1) // revert to first order on any overshoot (Selle 2008)
        return (corrected < lo || corrected > hi) ? phiHat : corrected;

    if (limiterMode == 2)
    {
        // Adaptive: blend toward first order in proportion to how far the
        // correction overshoots the local bounds. In-bounds => full correction
        // (sharp); large overshoot => approaches revert (stable).
        const float over = max(0.0f, corrected - hi) + max(0.0f, lo - corrected);
        const float range = max(hi - lo, 1e-6f);
        const float blend = saturate(over / range);
        return lerp(corrected, phiHat, blend);
    }

    return clamp(corrected, lo, hi); // default: standard clamp
}

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
RWStructuredBuffer<float4> DiagnosticsOutput : register(u9);

Texture3D<float> DensityHat : register(t7);
Texture3D<float> TemperatureHat : register(t8);
Texture3D<float> DensityBar : register(t9);
Texture3D<float> TemperatureBar : register(t10);

RWTexture3D<float> VorticityX : register(u12);
RWTexture3D<float> VorticityY : register(u13);
RWTexture3D<float> VorticityZ : register(u14);
Texture3D<float> VorticityXInput : register(t11);
Texture3D<float> VorticityYInput : register(t12);
Texture3D<float> VorticityZInput : register(t13);

SamplerState LinearClamp : register(s0);
SamplerState LinearWrap : register(s1);

// Trilinear scalar sample. In the periodic reference case a WRAP sampler blends
// across the domain seam (texel N-1 with texel 0); otherwise clamp. This matches
// the modulo limiter stencil used when periodicDomain is set.
float SampleScalarField(Texture3D<float> field, float3 uvw)
{
    if (periodicDomain)
        return field.SampleLevel(LinearWrap, uvw, 0);
    return field.SampleLevel(LinearClamp, uvw, 0);
}

#ifdef SMOKE_MASS_AUDIT
// Recorded by the actual scalar kernel. x/y: before/after limiter;
// z/w: signed lower/upper limiter changes. Every cell is overwritten each step.
RWStructuredBuffer<float4> MassAudit : register(u15);
uint AuditIndex(uint3 id) { return (id.z * GridResolution.y + id.y) * GridResolution.x + id.x; }
[numthreads(8, 8, 4)]
void AuditPatternCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= GridResolution)) return;
    // Distinct values expose stale frames; impulse moves in all three axes.
    Density[id] = Dt;
    Temperature[id] = all(id == SourceCell) ? 7.25f : 0.0f;
}
#endif



float SphereSdf(float3 position, SmokeSphereObstacle sphere)
{
    return length(position - sphere.centre) - sphere.radius;
}

static const uint CELL_FLUID = 0;
static const uint CELL_SOLID = 1;
static const uint CELL_OPEN = 2;

uint ClassifyCell(int3 cell)
{
    // Side and bottom walls take priority at corners.
    if (cell.x < 0 || cell.x >= int(GridResolution.x) ||
        cell.z < 0 || cell.z >= int(GridResolution.z) ||
        cell.y < 0)
    {
        return CELL_SOLID;
    }

    if (openTopEnabled)
    {
        if (cell.y >= int(GridResolution.y))
            return CELL_OPEN;
    }
    else
    {
        if (cell.y >= int(GridResolution.y))
            return CELL_SOLID;
    }

    // Diagnostic walls only. Modes 10-13 are never set by normal simulation.
    if (referenceVelocityMode >= 11 && referenceVelocityMode <= 13)
    {
        float3 q = (float3(cell) + 0.5f) / float3(GridResolution);
        float centre = referenceVelocityMode == 13 ? 0.5f : 0.5f + 0.5f / GridResolution.x;
        if (referenceVelocityMode == 12) centre += 0.25f * (q.y - 0.5f);
        float width = (referenceVelocityMode == 13 ? 0.25f : 1.0f) / GridResolution.x;
        if (abs(q.x-centre) <= 0.5f*width) return CELL_SOLID;
    }
    if (referenceVelocityMode >= 14 && referenceVelocityMode <= 16)
    {
        float3 q=(float3(cell)+0.5f)/float3(GridResolution);
        bool baffle=q.x>=0.5f && q.x<0.53125f && q.y>=0.1875f && q.y<0.8125f;
        float gap=referenceVelocityMode==15?0.03125f:0.125f;
        bool slot=referenceVelocityMode!=14 && q.y>=0.375f && q.y<0.375f+gap;
        if(baffle && !slot)return CELL_SOLID;
    }
    if (sphereObstacle.enabled != 0)
    {
        float3 position =
            origin + (float3(cell) + 0.5f) * GridSpacing;

        if (SphereSdf(position, sphereObstacle) <= 0.0f)
            return CELL_SOLID;
    }

    return CELL_FLUID;
}

bool IsSphereCell(int3 cell)
{
    if (any(cell < 0) || any(cell >= int3(GridResolution)))
        return false;

    const float3 position =
        origin + (float3(cell) + 0.5f) * GridSpacing;

    return sphereObstacle.enabled != 0 &&
        SphereSdf(position, sphereObstacle) <= 0.0f;
}

float BoundaryVelocityU(int3 face)
{
    const bool sphereBlocked =
        IsSphereCell(face - int3(1, 0, 0)) ||
        IsSphereCell(face);

    return sphereBlocked ? sphereObstacle.velocity.x : 0.0f;
}

float BoundaryVelocityV(int3 face)
{
    const bool sphereBlocked =
        IsSphereCell(face - int3(0, 1, 0)) ||
        IsSphereCell(face);

    return sphereBlocked ? sphereObstacle.velocity.y : 0.0f;
}

float BoundaryVelocityW(int3 face)
{
    const bool sphereBlocked =
        IsSphereCell(face - int3(0, 0, 1)) ||
        IsSphereCell(face);

    return sphereBlocked ? sphereObstacle.velocity.z : 0.0f;
}

bool IsSolidCell(int3 cell)
{
    return ClassifyCell(cell) == CELL_SOLID;
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

// Pure SL advect of an arbitrary scalar SRV at cell id; no dissipation.
float AdvectRaw(Texture3D<float> field, uint3 id)
{
    float3 q = float3(id) + 0.5f;
    float3 departure = BackTrace(q);
    float3 uvw = departure / float3(GridResolution);
    return SampleScalarField(field, uvw);
}

// Min/max over the SAME 8 texels the trilinear sample reads at `departure`.
// Footprint MUST match SampleLevel or the clamp is wrong.
void TrilinearMinMax(Texture3D<float> field, float3 departure,
                     out float lo, out float hi)
{
    float3 p = departure - 0.5f; // match cell-centre convention
    int3 b = int3(floor(p));
    lo = 1e30f;
    hi = -1e30f;
    [unroll]
    for (int i = 0; i < 8; ++i)
    {
        int3 off = b + int3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        int3 c = periodicDomain
            ? (off % int3(GridResolution) + int3(GridResolution)) % int3(GridResolution)
            : clamp(off, int3(0, 0, 0), int3(GridResolution) - 1);
        float v = field.Load(int4(c, 0));
        lo = min(lo, v);
        hi = max(hi, v);
    }
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

// Passive-advection reference harness: smooth Gaussian blob at the domain centre.
// The CPU analytic reference uses the IDENTICAL formula (cell-centred samples),
// so a perfect transport scheme reproduces it exactly. Amplitude 1, sigma =
// 0.12 * Nx cells. Writes Density (u0) and clears Temperature (u1).
[numthreads(8, 8, 4)]
void SetReferenceBlobCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= GridResolution))
        return;
    const float sigma = referenceBlobSigma;
    const float3 d = (float3(id) + 0.5f) - referenceBlobCentre;
    Density[id] = exp(-dot(d, d) / (2.0f * sigma * sigma));
    Temperature[id] = 0.0f;
}

// Prescribed, time-invariant velocity for the transport reference cases. Written
// once at reset. Mode 1: constant +x translation. Mode 2: solid-body rotation
// about the z axis through the domain centre. Velocities are scaled so the
// per-step cell displacement equals referenceSpeed (translation) or the per-step
// angle equals referenceSpeed (rotation), assuming a cubic grid.
[numthreads(8, 8, 4)]
void SetPrescribedVelocityCS(uint3 id : SV_DispatchThreadID)
{
    const uint3 n = GridResolution;
    const float cx = n.x * 0.5f;
    const float cy = n.y * 0.5f;
    const float omega = referenceSpeed / Dt; // rad/s (rotation)
    // U faces: (n.x+1) x n.y x n.z, positioned at (i, j+0.5, k+0.5) cells.
    if (all(id < n + uint3(1, 0, 0)))
    {
        float u = 0.0f;
        if (referenceVelocityMode == 1) u = referenceSpeed * hx / Dt;
        else if (referenceVelocityMode == 2) u = -omega * (((id.y + 0.5f) - cy) * hy);
        VelocityU[id] = u;
    }
    // V faces: n.x x (n.y+1) x n.z, positioned at (i+0.5, j, k+0.5) cells.
    if (all(id < n + uint3(0, 1, 0)))
    {
        float v = 0.0f;
        if (referenceVelocityMode == 2) v = omega * (((id.x + 0.5f) - cx) * hx);
        VelocityV[id] = v;
    }
    // W faces: no z motion in either mode.
    if (all(id < n + uint3(0, 0, 1)))
        VelocityW[id] = 0.0f;
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
        VelocityV[id] = BoundaryVelocityV(int3(id));
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

    float u0 = IsBlockedU(c) ? BoundaryVelocityU(c) :
        VelocityUInput.Load(int4(c, 0));
    float u1 = IsBlockedU(right) ? BoundaryVelocityU(right) :
        VelocityUInput.Load(int4(right, 0));

    float v0 = IsBlockedV(c) ? BoundaryVelocityV(c) :
        VelocityVInput.Load(int4(c, 0));
    float v1 = IsBlockedV(above) ? BoundaryVelocityV(above) :
        VelocityVInput.Load(int4(above, 0));

    float w0 = IsBlockedW(c) ? BoundaryVelocityW(c) :
        VelocityWInput.Load(int4(c, 0));
    float w1 = IsBlockedW(front) ? BoundaryVelocityW(front) :
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
        uint type = ClassifyCell(neighbour);
        float weight = weights[direction / 2];

        if (type == CELL_FLUID)
        {
            neighbourSum += weight *
            PressureRead.Load(int4(neighbour, 0));

            diagonal += weight;
        }
        else if (type == CELL_OPEN)
        {
        // Atmospheric pressure at the top face.
        // Distance from the cell centre is half a cell.
            diagonal += 2.0f * weight;
        }

    // Solid neighbour: no contribution.
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

void SubtractPressureGradientCS(uint3 id : SV_DispatchThreadID)
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
            VelocityU[id] = BoundaryVelocityU(cell);
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
        
        if (id.y == GridResolution.y && openTopEnabled != 0)
        {
            int3 below = int3(id) - int3(0, 1, 0);

            if (ClassifyCell(below) == CELL_SOLID)
            {
                VelocityV[id] = sphereObstacle.velocity.y;
            }
            else
            {
                float pressureBelow =
            PressureRead.Load(int4(below, 0));

                VelocityV[id] -=
            (Dt / FluidDensity) *
            (0.0f - pressureBelow) /
            (0.5f * hy);
            }
        }
        else
        {
        
            if (id.y == 0 || id.y == GridResolution.y || IsBlockedV(cell))
            {
                VelocityV[id] = BoundaryVelocityV(cell);
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
    }

    // W: Nx � Ny � (Nz + 1)
    if (id.x < GridResolution.x &&
        id.y < GridResolution.y &&
        id.z <= GridResolution.z)
    {
        if (id.z == 0 || id.z == GridResolution.z || IsBlockedW(cell))
        {
            VelocityW[id] = BoundaryVelocityW(cell);
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
void AdvectScalarsCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= GridResolution))
        return;
#ifdef SMOKE_MASS_AUDIT
    MassAudit[AuditIndex(id)] = 0;
#endif

    if (IsSolidCell(int3(id)))
    {
        Density[id] = 0.0f;
        Temperature[id] = ambientTemperature;
        return;
    }
    
    float3 q = float3(id) + 0.5f;
    float3 departure = BackTrace(q);

    float3 uvw = departure / float3(GridResolution);

    if (!periodicDomain && departure.y >= float(GridResolution.y))
    {
        Density[id] = 0.0f;
        Temperature[id] = ambientTemperature;
        return;
    }

#ifdef SMOKE_MASS_AUDIT
    float auditRaw = SampleScalarField(DensityInput, uvw);
    MassAudit[AuditIndex(id)] = float4(auditRaw, auditRaw, 0, 0);
#endif
    Density[id] =
        max(SampleScalarField(DensityInput, uvw) * exp(-Padding.x * Dt), 0.0f);

    Temperature[id] =
        ambientTemperature + (SampleScalarField(TemperatureInput, uvw) - ambientTemperature) * exp(-Padding.y * Dt);
}

[numthreads(8, 8, 4)]

void AdvectVelocityCS(uint3 id : SV_DispatchThreadID)
{
    uint3 n = GridResolution;

    if (all(id < n + uint3(1, 0, 0)))
    {
        if (id.x == 0 || id.x == n.x || IsBlockedU(int3(id)))
        {
            VelocityU[id] = BoundaryVelocityU(int3(id));
        }
        else
        {
            float3 q = float3(id) + float3(0.0f, 0.5f, 0.5f);
            VelocityU[id] = SampleU(BackTrace(q));
        }
    }

    if (all(id < n + uint3(0, 1, 0)))
    {
        if (IsBlockedV(int3(id)))
        {
            VelocityV[id] = BoundaryVelocityV(int3(id));
        }
        else
        {
            float3 q =
            float3(id) + float3(0.5f, 0.0f, 0.5f);

            VelocityV[id] = SampleV(BackTrace(q));
        }
    }

    if (all(id < n + uint3(0, 0, 1)))
    {
        if (id.z == 0 || id.z == n.z || IsBlockedW(int3(id)))
        {
            VelocityW[id] = BoundaryVelocityW(int3(id));
        }
        else
        {
            float3 q = float3(id) + float3(0.5f, 0.5f, 0.0f);
            VelocityW[id] = SampleW(BackTrace(q));
        }
    }
}

// Numerical diagnostics deliberately use one 256-thread group. Each thread
// walks a strided subset of the small reference grid, then the group performs
// one deterministic tree reduction. The passes run after the solver timestamp.
groupshared float DiagnosticSum[256];
groupshared float DiagnosticMaximum[256];
groupshared uint DiagnosticCount[256];
groupshared uint DiagnosticNonfinite[256];

bool IsFiniteDiagnostic(float value)
{
    return (asuint(value) & 0x7f800000u) != 0x7f800000u;
}

void FinishDiagnosticReduction(uint lane, uint outputIndex)
{
    GroupMemoryBarrierWithGroupSync();

    [unroll]
    for (uint stride = 128; stride > 0; stride >>= 1)
    {
        if (lane < stride)
        {
            DiagnosticSum[lane] += DiagnosticSum[lane + stride];
            DiagnosticMaximum[lane] = max(
                DiagnosticMaximum[lane],
                DiagnosticMaximum[lane + stride]);
            DiagnosticCount[lane] += DiagnosticCount[lane + stride];
            DiagnosticNonfinite[lane] += DiagnosticNonfinite[lane + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (lane == 0)
    {
        DiagnosticsOutput[outputIndex] = float4(
            DiagnosticSum[0],
            DiagnosticMaximum[0],
            float(DiagnosticCount[0]),
            float(DiagnosticNonfinite[0]));
    }
}

void ReduceDivergence(uint lane, uint outputIndex)
{
    const uint cellCount =
        GridResolution.x * GridResolution.y * GridResolution.z;
    float sumSquares = 0.0f;
    float maximumAbsolute = 0.0f;
    uint validCount = 0;
    uint nonfiniteCount = 0;

    for (uint linearIndex = lane; linearIndex < cellCount; linearIndex += 256)
    {
        const uint x = linearIndex % GridResolution.x;
        const uint yz = linearIndex / GridResolution.x;
        const uint y = yz % GridResolution.y;
        const uint z = yz / GridResolution.y;
        const int3 cell = int3(x, y, z);

        if (ClassifyCell(cell) != CELL_FLUID)
            continue;

        const float value = DivergenceRead.Load(int4(cell, 0));
        if (!IsFiniteDiagnostic(value))
        {
            ++nonfiniteCount;
            continue;
        }

        sumSquares += value * value;
        maximumAbsolute = max(maximumAbsolute, abs(value));
        ++validCount;
    }

    DiagnosticSum[lane] = sumSquares;
    DiagnosticMaximum[lane] = maximumAbsolute;
    DiagnosticCount[lane] = validCount;
    DiagnosticNonfinite[lane] = nonfiniteCount;
    FinishDiagnosticReduction(lane, outputIndex);
}

[numthreads(256, 1, 1)]
void ReduceDivergenceBeforeCS(
    uint3 dispatchThreadId : SV_DispatchThreadID,
    uint lane : SV_GroupIndex)
{
    ReduceDivergence(lane, 0);
}

[numthreads(256, 1, 1)]
void ReduceDivergenceAfterCS(
    uint3 dispatchThreadId : SV_DispatchThreadID,
    uint lane : SV_GroupIndex)
{
    ReduceDivergence(lane, 1);
}

[numthreads(256, 1, 1)]
void ReduceVelocityCS(
    uint3 dispatchThreadId : SV_DispatchThreadID,
    uint lane : SV_GroupIndex)
{
    const uint cellCount =
        GridResolution.x * GridResolution.y * GridResolution.z;
    float sumSpeedSquared = 0.0f;
    float maximumSpeed = 0.0f;
    uint validCount = 0;
    uint nonfiniteCount = 0;

    for (uint linearIndex = lane; linearIndex < cellCount; linearIndex += 256)
    {
        const uint x = linearIndex % GridResolution.x;
        const uint yz = linearIndex / GridResolution.x;
        const uint y = yz % GridResolution.y;
        const uint z = yz / GridResolution.y;
        const int3 cell = int3(x, y, z);

        if (ClassifyCell(cell) != CELL_FLUID)
            continue;

        const float3 velocity = float3(
            0.5f * (VelocityUInput.Load(int4(cell, 0)) +
                    VelocityUInput.Load(int4(cell + int3(1, 0, 0), 0))),
            0.5f * (VelocityVInput.Load(int4(cell, 0)) +
                    VelocityVInput.Load(int4(cell + int3(0, 1, 0), 0))),
            0.5f * (VelocityWInput.Load(int4(cell, 0)) +
                    VelocityWInput.Load(int4(cell + int3(0, 0, 1), 0))));

        if (!IsFiniteDiagnostic(velocity.x) ||
            !IsFiniteDiagnostic(velocity.y) ||
            !IsFiniteDiagnostic(velocity.z))
        {
            ++nonfiniteCount;
            continue;
        }

        const float speedSquared = dot(velocity, velocity);
        sumSpeedSquared += speedSquared;
        maximumSpeed = max(maximumSpeed, sqrt(speedSquared));
        ++validCount;
    }

    DiagnosticSum[lane] = sumSpeedSquared;
    DiagnosticMaximum[lane] = maximumSpeed;
    DiagnosticCount[lane] = validCount;
    DiagnosticNonfinite[lane] = nonfiniteCount;
    FinishDiagnosticReduction(lane, 2);
}

// Enstrophy reduction: sum of |omega|^2 over fluid cells, and peak |omega|.
// Reads the cell-centred vorticity textures (recomputed from the final velocity
// before this dispatch). Writes diagnostics record 3.
[numthreads(256, 1, 1)]
void ReduceEnstrophyCS(
    uint3 dispatchThreadId : SV_DispatchThreadID,
    uint lane : SV_GroupIndex)
{
    const uint cellCount =
        GridResolution.x * GridResolution.y * GridResolution.z;
    float sumMagnitudeSquared = 0.0f;
    float maximumMagnitude = 0.0f;
    uint validCount = 0;
    uint nonfiniteCount = 0;

    for (uint linearIndex = lane; linearIndex < cellCount; linearIndex += 256)
    {
        const uint x = linearIndex % GridResolution.x;
        const uint yz = linearIndex / GridResolution.x;
        const uint y = yz % GridResolution.y;
        const uint z = yz / GridResolution.y;
        const int3 cell = int3(x, y, z);

        if (ClassifyCell(cell) != CELL_FLUID)
            continue;

        const float3 omega = float3(
            VorticityXInput.Load(int4(cell, 0)),
            VorticityYInput.Load(int4(cell, 0)),
            VorticityZInput.Load(int4(cell, 0)));

        if (!IsFiniteDiagnostic(omega.x) ||
            !IsFiniteDiagnostic(omega.y) ||
            !IsFiniteDiagnostic(omega.z))
        {
            ++nonfiniteCount;
            continue;
        }

        const float magnitudeSquared = dot(omega, omega);
        sumMagnitudeSquared += magnitudeSquared;
        maximumMagnitude = max(maximumMagnitude, sqrt(magnitudeSquared));
        ++validCount;
    }

    DiagnosticSum[lane] = sumMagnitudeSquared;
    DiagnosticMaximum[lane] = maximumMagnitude;
    DiagnosticCount[lane] = validCount;
    DiagnosticNonfinite[lane] = nonfiniteCount;
    FinishDiagnosticReduction(lane, 3);
}

// Reused for BOTH hat (input = φⁿ, Dt = +dt) and bar (input = φ̂, Dt = −dt).
[numthreads(8, 8, 4)]
void AdvectScalarsRawCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= GridResolution))
        return;
    if (IsSolidCell(int3(id)))
    {
        Density[id] = 0;
        Temperature[id] = ambientTemperature;
        return;
    }
    Density[id] = AdvectRaw(DensityInput, id);
    Temperature[id] = AdvectRaw(TemperatureInput, id);
}

// Combine + limiter + dissipation. Writes the final φⁿ⁺¹.
[numthreads(8, 8, 4)]
void MacCormackScalarsCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= GridResolution))
        return;
#ifdef SMOKE_MASS_AUDIT
    MassAudit[AuditIndex(id)] = 0;
#endif
    if (IsSolidCell(int3(id)))
    {
        Density[id] = 0;
        Temperature[id] = ambientTemperature;
        return;
    }

    float3 q = float3(id) + 0.5f;
    float3 departure = BackTrace(q);
    if (!periodicDomain && departure.y >= float(GridResolution.y))
    {
        Density[id] = 0;
        Temperature[id] = ambientTemperature;
        return;
    }

    // Density
    float dN = DensityInput.Load(int4(id, 0)); // φⁿ  (bound at t0)
    float dH = DensityHat.Load(int4(id, 0)); // φ̂   (new SRV)
    float dB = DensityBar.Load(int4(id, 0)); // φ̄   (new SRV)
    float d = dH + 0.5f * (dN - dB);
    float dlo, dhi;
    TrilinearMinMax(DensityInput, departure, dlo, dhi);
#ifdef SMOKE_MASS_AUDIT
    float auditCorrected = d;
#endif
    d = ApplyLimiter(d, dH, dlo, dhi);
#ifdef SMOKE_MASS_AUDIT
    MassAudit[AuditIndex(id)] = float4(auditCorrected, d,
        auditCorrected < dlo ? d - auditCorrected : 0,
        auditCorrected > dhi ? d - auditCorrected : 0);
#endif
    Density[id] = max(d * exp(-Padding.x * Dt), 0.0f); // dissipation applied ONCE, here

    // Temperature (same pattern, ambient-relative)
    float tN = TemperatureInput.Load(int4(id, 0));
    float tH = TemperatureHat.Load(int4(id, 0));
    float tB = TemperatureBar.Load(int4(id, 0));
    float t = tH + 0.5f * (tN - tB);
    float tlo, thi;
    TrilinearMinMax(TemperatureInput, departure, tlo, thi);
    t = ApplyLimiter(t, tH, tlo, thi);
    Temperature[id] = ambientTemperature + (t - ambientTemperature) * exp(-Padding.y * Dt);
}

float3 CellVelocity(int3 c) // cell-centred, matches ReduceVelocityCS averaging
{
    return float3(
        0.5f * (VelocityUInput.Load(int4(c, 0)) + VelocityUInput.Load(int4(c + int3(1, 0, 0), 0))),
        0.5f * (VelocityVInput.Load(int4(c, 0)) + VelocityVInput.Load(int4(c + int3(0, 1, 0), 0))),
        0.5f * (VelocityWInput.Load(int4(c, 0)) + VelocityWInput.Load(int4(c + int3(0, 0, 1), 0))));
}

[numthreads(8, 8, 4)]
void ComputeVorticityCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= GridResolution))
        return;
    int3 c = int3(id);
    if (IsSolidCell(c))
    {
        VorticityX[id] = 0;
        VorticityY[id] = 0;
        VorticityZ[id] = 0;
        return;
    }

    float3 uxp = CellVelocity(c + int3(1, 0, 0)), uxm = CellVelocity(c - int3(1, 0, 0));
    float3 uyp = CellVelocity(c + int3(0, 1, 0)), uym = CellVelocity(c - int3(0, 1, 0));
    float3 uzp = CellVelocity(c + int3(0, 0, 1)), uzm = CellVelocity(c - int3(0, 0, 1));

    VorticityX[id] = (uyp.z - uym.z) / (2 * hy) - (uzp.y - uzm.y) / (2 * hz);
    VorticityY[id] = (uzp.x - uzm.x) / (2 * hz) - (uxp.z - uxm.z) / (2 * hx);
    VorticityZ[id] = (uxp.y - uxm.y) / (2 * hx) - (uyp.x - uym.x) / (2 * hy);
}

float3 OmegaVec(int3 c)
{
    return float3(VorticityXInput.Load(int4(c, 0)),
                                       VorticityYInput.Load(int4(c, 0)),
                                       VorticityZInput.Load(int4(c, 0)));
}
float OmegaMag(int3 c)
{
    return length(OmegaVec(c));
}

float3 ConfinementForceAtCell(int3 c)
{
    float3 eta = float3(
        (OmegaMag(c + int3(1, 0, 0)) - OmegaMag(c - int3(1, 0, 0))) / (2 * hx),
        (OmegaMag(c + int3(0, 1, 0)) - OmegaMag(c - int3(0, 1, 0))) / (2 * hy),
        (OmegaMag(c + int3(0, 0, 1)) - OmegaMag(c - int3(0, 0, 1))) / (2 * hz));
    float3 N = eta / (length(eta) + 1e-20f);
    return vorticityEpsilon * float3(hx, hy, hz) * cross(N, OmegaVec(c));
}

[numthreads(8, 8, 4)]
void ApplyConfinementForceCS(uint3 id : SV_DispatchThreadID) // per face, like SubtractPressureGradientCS
{
    uint3 n = GridResolution;
    int3 c = int3(id);
    // U faces
    if (id.x <= n.x && id.y < n.y && id.z < n.z && !IsBlockedU(c) && id.x != 0 && id.x != n.x)
        VelocityU[id] += Dt * 0.5f * (ConfinementForceAtCell(c - int3(1, 0, 0)).x + ConfinementForceAtCell(c).x);
    // V faces
    if (id.x < n.x && id.y <= n.y && id.z < n.z && !IsBlockedV(c) && id.y != 0 && id.y != n.y)
        VelocityV[id] += Dt * 0.5f * (ConfinementForceAtCell(c - int3(0, 1, 0)).y + ConfinementForceAtCell(c).y);
    // W faces
    if (id.x < n.x && id.y < n.y && id.z <= n.z && !IsBlockedW(c) && id.z != 0 && id.z != n.z)
        VelocityW[id] += Dt * 0.5f * (ConfinementForceAtCell(c - int3(0, 0, 1)).z + ConfinementForceAtCell(c).z);
}
