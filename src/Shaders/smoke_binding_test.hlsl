cbuffer SmokeBindingConstants : register(b0)
{
    uint3 GridResolution;
    float PatternScale;
};

Texture3D<float> InputField : register(t0);
RWTexture3D<float> OutputField : register(u0);

[numthreads(8, 8, 4)]
void WritePatternCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= GridResolution))
        return;

    // Distinct weights make each axis recognisable.
    // Integer-valued output also makes exact inspection easy.
    OutputField[id] =
        PatternScale *
        (float(id.x) + 10.0f * float(id.y) + 100.0f * float(id.z));
}

[numthreads(8, 8, 4)]
void CopyFieldCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= GridResolution))
        return;

    OutputField[id] = InputField.Load(int4(id, 0));
}