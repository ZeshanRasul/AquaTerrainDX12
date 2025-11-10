#include "Common.hlsl"

struct STriVertex
{
    float3 vertex;
    float3 normal;
};

StructuredBuffer<STriVertex> BTriVertex : register(t0);


[shader("closesthit")] 
void ClosestHit(inout HitInfo payload, Attributes attrib) 
{
    float3 barycentrics = float3(1.0f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);
    
    const float3 A = float3(1.0, 0.0, 0.0);
    const float3 B = float3(0.0, 1.0, 0.0);
    const float3 C = float3(0.0, 0.0, 1.0);
    
    uint vertId = 3 * PrimitiveIndex();
    float3 hitColor = BTriVertex[vertId + 0].normal * barycentrics.x +
                  BTriVertex[vertId + 1].normal * barycentrics.y +
                  BTriVertex[vertId + 2].normal * barycentrics.z;
    
    payload.colorAndDistance = float4(1.0, 1.0, 1.0, RayTCurrent());
}
