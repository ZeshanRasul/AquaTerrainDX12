cbuffer WaterCB : register(b4)
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
    float2 uv : TEXCOORD0;
};

struct VSOutput
{
    float4 posH : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 worldNormal : TEXCOORD1;
    float2 uv : TEXCOORD2;
};

VSOutput VS_Main(VSInput vin)
{
    VSOutput vout;

    float3 worldPos = mul(float4(vin.pos, 1.0f), gWorld).xyz;

    // Flat plane normal up
    float3 worldNormal = float3(0.0f, 1.0f, 0.0f);

    vout.worldPos = worldPos;
    vout.worldNormal = worldNormal;
    vout.uv = vin.uv;
    vout.posH = mul(float4(worldPos, 1.0f), gViewProj);

    return vout;
}

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
    float3 N = normalize(pin.worldNormal);
    float3 V = normalize(gCameraPos - pin.worldPos);

    float cosTheta = saturate(dot(N, V));

    // Cheap Schlick Fresnel
    float3 F0 = float3(0.02f, 0.02f, 0.02f); // water-ish
    float fresnel = pow(1.0f - cosTheta, 5.0f);
    float3 F = F0 + (1.0f - F0) * fresnel;

    // Approx reflection towards sky
    float3 R = reflect(-V, N);
    float3 sky = EvaluateSky(R);

    float3 base = gWaterColor;
    float3 color = lerp(base, sky, F);

    float alpha = 0.6f; // tweak

    return float4(color, alpha);
}
