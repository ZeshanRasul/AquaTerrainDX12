// Diagnostic entry generation only. ProductionPS is the unchanged production
// pixel shader, prepended by the host (only the refinement loop bound differs).
cbuffer CaptureCamera : register(b1)
{
    float3 eye; float tanHalfFov;
    float3 forward; float imageWidth;
    float3 right; float imageHeight;
    float3 up; float unused;
};
float4 CaptureVS(uint id : SV_VertexID) : SV_POSITION
{
    return float4(id == 2 ? 3 : -1, id == 1 ? 3 : -1, 0, 1);
}
float4 CapturePS(float4 position : SV_POSITION) : SV_TARGET
{
    float2 ndc = float2(2 * position.x / imageWidth - 1,
                       1 - 2 * position.y / imageHeight);
    float3 direction = normalize(forward + tanHalfFov * (ndc.x * right + ndc.y * up));
    float3 a = (0 - eye) / direction;
    float3 b = (1 - eye) / direction;
    float3 nearWall = min(a, b), farWall = max(a, b);
    float enter = max(nearWall.x, max(nearWall.y, nearWall.z));
    float leave = min(farWall.x, min(farWall.y, farWall.z));
    if (leave < max(enter, 0)) return 0;
    PixelIn input;
    input.PositionHomogeneous = position;
    input.PositionLocal = float3(-0.75, -0.25, -0.75)
        + saturate(eye + enter * direction) * float3(1.5, 0.5, 1.5);
    return ProductionPS(input);
}
