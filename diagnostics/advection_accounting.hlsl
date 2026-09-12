// Independent observer. Never defines SMOKE_MASS_AUDIT or writes production fields.
#include "../src/Shaders/3d_smoke_compute.hlsl"
RWStructuredBuffer<float4> Accounting : register(u15);
// METHOD: 0 SL, 1 raw forward/reverse, 2 MacCormack combine.
[numthreads(8,8,4)]
void TraceAccountingCS(uint3 id : SV_DispatchThreadID)
{
    if(any(id>=GridResolution)) return;
    uint r=5*((id.z*GridResolution.y+id.y)*GridResolution.x+id.x);
    float3 departure=BackTrace(float3(id)+.5f);
    float before=DensityInput.Load(int4(id,0));
#if METHOD == 2
    float sampled=DensityHat.Load(int4(id,0));
#else
    float sampled=SampleScalarField(DensityInput,departure/float3(GridResolution));
#endif
    bool solid=IsSolidCell(int3(id));
    bool top=!periodicDomain && departure.y>=float(GridResolution.y);
    float cleared=solid?0:sampled;
#if METHOD == 1
    float rejected=cleared; // Raw passes deliberately do not reject top departure.
#else
    float rejected=(!solid && top)?0:cleared;
#endif
    float corrected=rejected, limited=rejected;
#if METHOD == 2
    if(!solid && !top) {
        corrected=sampled+.5f*(before-DensityBar.Load(int4(id,0)));
        float lo,hi; TrilinearMinMax(DensityInput,departure,lo,hi);
        limited=ApplyLimiter(corrected,sampled,lo,hi);
    }
#endif
#if METHOD == 1
    float damped=limited, floored=limited;
#else
    float damped=limited*exp(-Padding.x*Dt), floored=max(damped,0);
#endif
    // Values, not GPU-reduced deltas: subtract/accumulate in float64 on CPU.
    Accounting[r]=float4(before,sampled,cleared,rejected);
    Accounting[r+1]=float4(corrected,limited,damped,floored);
    Accounting[r+2]=float4(departure,solid?1:0);
    Accounting[r+3]=float4(top?1:0,any(departure<0)||any(departure>=float3(GridResolution))?1:0,
        any(departure<.5f)||any(departure>float3(GridResolution)-.5f)?1:0,damped<0?1:0);
    int3 base=int3(floor(departure-.5f));
    Accounting[r+4]=float4(any(base<0)||any(base+1>=int3(GridResolution))?1:0,
        corrected<limited?1:0,corrected>limited?1:0,0);
}
