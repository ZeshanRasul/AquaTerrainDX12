Texture3D<float> gDensity : register(t0);
Texture2D<float> gSceneDepth : register(t1);
SamplerState gLinearClamp : register(s0);

cbuffer SmokeConstants : register(b0)
{
    float4x4 gWorld;
    float4x4 gViewProj;
    float3 gCameraPositionLocal;
    float gDensityScale;
    float3 gSmokeColour;
    float gAbsorption;
    float3 gLightDirectionTexture;
    float gStepScale;
};

struct PixelIn
{
    float4 PositionHomogeneous : SV_POSITION;
    float3 PositionLocal : POSITION0;
};

float Hash12(float2 value)
{
    const float3 p = frac(float3(value.xyx) * 0.1031f);
    const float3 q = p + dot(p, p.yzx + 33.33f);
    return frac((q.x + q.y) * q.z);
}

float4 PS(PixelIn input) : SV_TARGET
{
    // The shapeGeo box used by the renderer was created with these dimensions.
    const float3 boxMinimum = float3(-0.75f, -0.25f, -0.75f);
    const float3 boxSize = float3(1.5f, 0.5f, 1.5f);

    const float3 entryPosition =
        saturate((input.PositionLocal - boxMinimum) / boxSize);
    const float3 cameraPosition =
        (gCameraPositionLocal - boxMinimum) / boxSize;
    const float3 rayDirection = normalize(entryPosition - cameraPosition);

    // Select the unit-box wall reached in the direction of travel.
    const float3 exitWall = float3(
        rayDirection.x >= 0.0f ? 1.0f : 0.0f,
        rayDirection.y >= 0.0f ? 1.0f : 0.0f,
        rayDirection.z >= 0.0f ? 1.0f : 0.0f);
    const float3 safeDirection = float3(
        abs(rayDirection.x) > 1.0e-5f
            ? rayDirection.x : (rayDirection.x < 0.0f ? -1.0e-5f : 1.0e-5f),
        abs(rayDirection.y) > 1.0e-5f
            ? rayDirection.y : (rayDirection.y < 0.0f ? -1.0e-5f : 1.0e-5f),
        abs(rayDirection.z) > 1.0e-5f
            ? rayDirection.z : (rayDirection.z < 0.0f ? -1.0e-5f : 1.0e-5f));
    const float3 exitDistances = (exitWall - entryPosition) / safeDirection;
    const float distanceToExit = max(
        0.0f, min(exitDistances.x, min(exitDistances.y, exitDistances.z)));

    const float stepLength = max(gStepScale / 32.0f, 0.003f);
    const float jitter = Hash12(input.PositionHomogeneous.xy);
	const float sceneDepth = gSceneDepth.Load(
		int3(int2(input.PositionHomogeneous.xy), 0));
    float distanceAlongRay = jitter * stepLength;
    float transmittance = 1.0f;
    float3 accumulatedColour = 0.0f;

    const float lightAlignment =
        dot(-rayDirection, normalize(-gLightDirectionTexture));
    const float phase =
        0.65f + 0.35f * pow(saturate(lightAlignment * 0.5f + 0.5f), 2.0f);

    [loop]
    for (int stepIndex = 0; stepIndex < 96; ++stepIndex)
    {
        if (distanceAlongRay > distanceToExit || transmittance < 0.01f)
            break;

        const float3 samplePosition =
            entryPosition + rayDirection * distanceAlongRay;
		const float3 samplePositionLocal =
			boxMinimum + samplePosition * boxSize;
		const float4 samplePositionWorld =
			mul(float4(samplePositionLocal, 1.0f), gWorld);
		const float4 samplePositionHomogeneous =
			mul(samplePositionWorld, gViewProj);
		const float sampleDepth =
			samplePositionHomogeneous.z / samplePositionHomogeneous.w;

		// Stop when the ray reaches opaque scene geometry.
		if (sampleDepth >= sceneDepth - 1.0e-5f)
			break;

        const float density = max(
            gDensity.SampleLevel(gLinearClamp, samplePosition, 0.0f), 0.0f);
        const float extinction = density * gDensityScale * gAbsorption;
        const float sampleAlpha = 1.0f - exp(-extinction * stepLength);
        const float heightLighting = 0.72f + 0.28f * samplePosition.y;
        const float3 sampleColour =
            gSmokeColour * phase * heightLighting;

        accumulatedColour +=
            transmittance * sampleAlpha * sampleColour;
        transmittance *= 1.0f - sampleAlpha;
        distanceAlongRay += stepLength;
    }

    const float accumulatedAlpha = 1.0f - transmittance;
    clip(accumulatedAlpha - 0.001f);

    // RGB is premultiplied; the smoke PSO uses ONE, INV_SRC_ALPHA blending.
    return float4(accumulatedColour, accumulatedAlpha);
}
