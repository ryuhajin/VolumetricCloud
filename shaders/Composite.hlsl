// 복원된 cloud scattering/transmittance를 full-resolution 하늘과 합성한다.
#include "CloudNoise.hlsli"
#include "CloudAtmosphere.hlsli"

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

cbuffer cbCamera : register(b0)
{
    float4x4 invViewProj;
    float3 cameraPos;
    float time;
    float2 rayJitterNdc;
    float2 renderSize;
    uint temporalOutput;
    uint3 _cameraPad;
};

Texture2D<float3> resolvedScattering : register(t4);
Texture2D<float2> resolvedMetadata : register(t5);

float3 UnjitteredRay(float2 uv)
{
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 nearH = mul(float4(ndc, 0.0, 1.0), invViewProj);
    float4 farH = mul(float4(ndc, 1.0, 1.0), invViewProj);
    return normalize(farH.xyz / farH.w - nearH.xyz / nearH.w);
}

float4 main(VSOut input) : SV_TARGET
{
    float3 scattering = resolvedScattering.SampleLevel(noiseVolumeSampler, input.uv, 0);
    float2 metadata = resolvedMetadata.SampleLevel(noiseVolumeSampler, input.uv, 0);
    if (renderMode == 21)
        return float4((1.0 - metadata.y).xxx, 1.0);
    if (renderMode == 22)
        return float4(scattering, 1.0);

    float3 rayDirection = UnjitteredRay(input.uv);
    float3 sky = CloudSkyColor(
        rayDirection, CloudSunDirection(sunAzimuth, sunElevation));
    float3 linearColor = scattering + sky * metadata.y * max(skyExposure, 0.0);
    return float4(CloudToneMap(linearColor), 1.0);
}
