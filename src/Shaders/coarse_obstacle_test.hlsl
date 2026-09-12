#include "projection_experiment.hlsl"
[numthreads(8,8,4)]
void SetCoarseObstacleFieldsCS(uint3 id:SV_DispatchThreadID)
{
    if(all(id<GridResolution))
    {
        float3 q=(float3(id)+0.5f)/float3(GridResolution);
        // Compact upstream slab, separated from every diagnostic wall initially.
        Density[id]=!IsSolidCell(int3(id)) && q.x>=0.20f && q.x<0.35f &&
                    q.y>=0.25f && q.y<0.75f && q.z>=0.25f && q.z<0.75f ? 1.0f : 0.0f;
        Temperature[id]=0;
    }
    // Kinematic stress field, deliberately not divergence-free. Every blocked
    // face has exactly zero normal velocity; no pressure or force ambiguity.
    if(referenceVelocityMode>=14)
    {
        int3 c=int3(id);
        if(all(id<GridResolution+uint3(1,0,0)))
            VelocityU[id]=IsBlockedU(c)?0.0f:(ProjectionPsi(c.x,c.y+1)-ProjectionPsi(c.x,c.y))/hy;
        if(all(id<GridResolution+uint3(0,1,0)))
            VelocityV[id]=IsBlockedV(c)?0.0f:-(ProjectionPsi(c.x+1,c.y)-ProjectionPsi(c.x,c.y))/hx;
        if(all(id<GridResolution+uint3(0,0,1)))VelocityW[id]=0;
        return;
    }
    if(all(id<GridResolution+uint3(1,0,0)))
        VelocityU[id]=IsBlockedU(int3(id))?0.0f:referenceSpeed*hx/Dt;
    if(all(id<GridResolution+uint3(0,1,0))) VelocityV[id]=0;
    if(all(id<GridResolution+uint3(0,0,1))) VelocityW[id]=0;
}
