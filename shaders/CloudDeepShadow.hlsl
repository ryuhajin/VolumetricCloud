// ============================================================================
//  CloudDeepShadow.hlsl - 단계 12 Near/Far 누적 광학 깊이 생성
// ============================================================================

cbuffer cbCamera : register(b0)
{
    float4x4 invViewProj;
    float4x4 invProjection;
    float4x4 invViewRotation;
    float3 cameraPos;
    float time;
    float2 renderSize;
    float nearPlane;
    float farPlane;
};

#include "CloudDomainParameters.hlsli"
#include "Noise.hlsli"
#include "Stage12ShadowParameters.hlsli"

RWTexture2DArray<float> stage12OpticalDepthOutput : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    bool nearCascade = stage12DispatchCascade == 0u;
    uint resolution = nearCascade
        ? stage12NearResolution : stage12FarResolution;
    uint sliceCount = nearCascade
        ? stage12NearSliceCount : stage12FarSliceCount;
    if (dispatchThreadId.x >= resolution || dispatchThreadId.y >= resolution)
        return;

    float widthMeters = nearCascade
        ? stage12NearWidthMeters : stage12FarWidthMeters;
    float3 center = nearCascade ? stage12NearCenter : stage12FarCenter;
    float2 planeOffset =
        ((float2(dispatchThreadId.xy) + 0.5) / (float)resolution - 0.5) *
        widthMeters;
    float3 planePosition = center +
        stage12LightRight * planeOffset.x +
        stage12LightUp * planeOffset.y;

    const uint topSlice = max(sliceCount, 2u) - 1u;
    stage12OpticalDepthOutput[uint3(dispatchThreadId.xy, topSlice)] = 0.0;
    float opticalDepth = 0.0;
    float verticalInterval =
        (stage12CloudTopMeters - stage12CloudBottomMeters) /
        (float)topSlice;
    float rayInterval = verticalInterval /
        max(stage12LightForward.y, stage12MinimumSunY);

    [loop]
    for (int slice = (int)topSlice - 1; slice >= 0; --slice)
    {
        if (opticalDepth >= stage12MaximumOpticalDepth)
        {
            stage12OpticalDepthOutput[
                uint3(dispatchThreadId.xy, (uint)slice)] =
                stage12MaximumOpticalDepth;
            continue;
        }
        float heightFraction = ((float)slice + 0.5) / (float)topSlice;
        float sampleHeight = lerp(stage12CloudBottomMeters,
                                  stage12CloudTopMeters, heightFraction);
        float alongLight = (sampleHeight - planePosition.y) /
            max(stage12LightForward.y, stage12MinimumSunY);
        float3 samplePosition = planePosition +
            stage12LightForward * alongLight;
        float density = EvaluateLightCloudDensity(samplePosition, time);
        opticalDepth = min(stage12MaximumOpticalDepth,
            opticalDepth + max(density, 0.0) *
            max(extinctionCoefficient, 0.0) * rayInterval);
        stage12OpticalDepthOutput[
            uint3(dispatchThreadId.xy, (uint)slice)] = opticalDepth;
    }
}
