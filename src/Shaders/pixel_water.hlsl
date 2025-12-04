Texture2D gDiffuseMap : register(t0);

SamplerState gsamPointWrap : register(s0);
SamplerState gsamPointClamp : register(s1);
SamplerState gsamLinearWrap : register(s2);
SamplerState gsamLinearClamp : register(s3);
SamplerState gsamAnisotropicWrap : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);

cbuffer WaterCB : register(b3)
{
    float4x4 gWorld;
    float4x4 gViewProj;
    float3 gCameraPos;
    float gTime;
    float3 gWaterColor;
    float gPad0;
};

struct VSInput
{
    float3 pos : POSITION;
    float3 NormalW : NORMAL;
    float2 uv : TEXCOORD0;
};

struct VSOutput
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float2 TexC : TEXCOORD0;
};

float3 EvaluateSky(float3 dir)
{
    // Very simple: horizon brighter, zenith darker
    float t = saturate(dir.y * 0.5f + 0.5f);
    float3 top = float3(0.55f, 0.75f, 0.95f);
    float3 bottom = float3(0.7f, 0.8f, 0.9f);
    return lerp(bottom, top, t);
}

float4 PS(VSOutput pin) : SV_TARGET
{
    float3 diffuseAlbedo = (gDiffuseMap.Sample(gsamAnisotropicWrap, pin.TexC), 1.0) * float3(0.65, 0.75, 0.90);
    
    float3 N = normalize(pin.NormalW);
    float3 V = normalize(gCameraPos - pin.PosW);

    float cosTheta = saturate(dot(N, V));

    // Cheap Schlick Fresnel
    float3 F0 = float3(0.02f, 0.02f, 0.02f); // water-ish
    float fresnel = pow(1.0f - cosTheta, 5.0f);
    float3 F = F0 + (1.0f - F0) * fresnel;

    // Approx reflection towards sky
    float3 R = reflect(-V, N);
    float3 sky = EvaluateSky(R);

    float3 base = diffuseAlbedo.rgb;
    float3 color = lerp(base, sky, F);

    float alpha = 0.6f; // tweak

    return float4(color, alpha);
}
