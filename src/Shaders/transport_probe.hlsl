#include "3d_smoke_compute.hlsl"
RWStructuredBuffer<float4> TransportProbe : register(u15);

// Diagnostic interpolation of the SAME source field at the SAME departure.
// Float fractions and rounded 1/256 fractions are separate comparison models;
// neither model replaces the production hardware sample.
float ProbeInterpolation(int3 b, float3 f)
{
    float v[8];
    [unroll] for (int i=0;i<8;++i)
        v[i]=DensityInput.Load(int4(clamp(b+int3(i&1,(i>>1)&1,(i>>2)&1),int3(0,0,0),int3(GridResolution)-1),0));
    precise float result=lerp(lerp(lerp(v[0],v[1],f.x),lerp(v[2],v[3],f.x),f.y),
                              lerp(lerp(v[4],v[5],f.x),lerp(v[6],v[7],f.x),f.y),f.z);
    return result;
}
// Isolated sealed-domain projection experiment: only density interpolation changes.
// Same solid/outflow guards, attenuation, temperature sample and BackTrace as SL.
[numthreads(8,8,4)]
void AdvectFloatDensityCS(uint3 id : SV_DispatchThreadID)
{
    if(any(id>=GridResolution))return;
    if(IsSolidCell(int3(id)))
    {
        Density[id]=0.0f; Temperature[id]=ambientTemperature; return;
    }
    float3 q=float3(id)+0.5f;
    float3 departure=BackTrace(q);
    float3 uvw=departure/float3(GridResolution);
    if(!periodicDomain && departure.y>=float(GridResolution.y))
    {
        Density[id]=0.0f; Temperature[id]=ambientTemperature; return;
    }
    float3 p=departure-0.5f;
    int3 b=int3(floor(p));
    Density[id]=max(ProbeInterpolation(b,p-float3(b))*exp(-Padding.x*Dt),0.0f);
    Temperature[id]=ambientTemperature+(SampleScalarField(TemperatureInput,uvw)-ambientTemperature)*exp(-Padding.y*Dt);
}
[numthreads(8,8,4)]
void TraceTransportCS(uint3 id : SV_DispatchThreadID)
{
    if(any(id>=GridResolution))return;
    uint r=4*((id.z*GridResolution.y+id.y)*GridResolution.x+id.x);
    float3 q=float3(id)+0.5f;
    float3 midpoint=q-0.5f*Dt*SampleVelocity(q)/GridSpacing;
    float3 departure=BackTrace(q);
    float3 p=departure-0.5f;
    int3 b=int3(floor(p));
    float3 f=p-float3(b);
    float lo,hi;TrilinearMinMax(DensityInput,departure,lo,hi);
    TransportProbe[r]=float4(departure,SampleScalarField(DensityInput,departure/float3(GridResolution)));
    TransportProbe[r+1]=float4(midpoint,ProbeInterpolation(b,f));
    TransportProbe[r+2]=float4(f,ProbeInterpolation(b,round(f*256.0f)/256.0f));
    TransportProbe[r+3]=float4(lo,hi,DensityInput.Load(int4(id,0)),0);
}
[numthreads(8,8,4)]
void RecordTransportOutputCS(uint3 id : SV_DispatchThreadID)
{
    if(any(id>=GridResolution))return;
    uint r=4*((id.z*GridResolution.y+id.y)*GridResolution.x+id.x)+3;
    float4 value=TransportProbe[r];
    value.w=DensityInput.Load(int4(id,0));
    TransportProbe[r]=value;
}
