#include "3d_smoke_compute.hlsl"

// A discrete curl of a vertex streamfunction plus a discrete pressure gradient.
// Domain length and dt stay fixed across resolutions. Boundary normal velocity
// is exactly zero. Mode 4 omits the gradient, providing an independent control.
float ProjectionPsi(int i, int j)
{
    if (i <= 0 || j <= 0 || i >= int(GridResolution.x) || j >= int(GridResolution.y)) return 0;
    const float pi = 3.14159265358979323846f;
    float x = sin(pi * i / GridResolution.x), y = sin(pi * j / GridResolution.y);
    return 0.12f * x * x * y * y;
}
float ProjectionPotential(int3 c)
{
    const float pi2 = 6.28318530717958647692f;
    float3 q = (float3(c) + 0.5f) / float3(GridResolution);
    return 0.035f * cos(pi2*q.x) * cos(pi2*q.y) * cos(pi2*q.z);
}
[numthreads(8,8,4)]
void SetProjectionFieldsCS(uint3 id : SV_DispatchThreadID)
{
    int3 c = int3(id);
    bool perturb = referenceVelocityMode != 4;
    if (all(id < GridResolution))
    {
        float3 q = (float3(id)+0.5f)/float3(GridResolution);
        float3 d = q-(float3(0.43f,0.61f,0.5f)+referenceBlobCentre/float3(GridResolution));
        Density[id] = referenceBlobSigma < 0 ? (all(abs(d) < float3(0.12f,0.10f,0.12f)) ? 1.0f : 0.0f)
            : exp(-dot(d,d)/(2*0.085f*0.085f));
        Temperature[id] = 0;
    }
    if (all(id < GridResolution+uint3(1,0,0)))
    {
        float v=0;
        if (id.x>0 && id.x<GridResolution.x)
        {
            v=(ProjectionPsi(c.x,c.y+1)-ProjectionPsi(c.x,c.y))/hy;
            if (perturb) v+=(ProjectionPotential(c)-ProjectionPotential(c-int3(1,0,0)))/hx;
        }
        VelocityU[id]=v;
    }
    if (all(id < GridResolution+uint3(0,1,0)))
    {
        float v=0;
        if (id.y>0 && id.y<GridResolution.y)
        {
            v=-(ProjectionPsi(c.x+1,c.y)-ProjectionPsi(c.x,c.y))/hx;
            if (perturb) v+=(ProjectionPotential(c)-ProjectionPotential(c-int3(0,1,0)))/hy;
        }
        VelocityV[id]=v;
    }
    if (all(id < GridResolution+uint3(0,0,1)))
    {
        float v=0;
        if (id.z>0 && id.z<GridResolution.z && perturb)
            v=(ProjectionPotential(c)-ProjectionPotential(c-int3(0,0,1)))/hz;
        VelocityW[id]=v;
    }
}
