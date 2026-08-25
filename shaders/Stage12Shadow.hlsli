// ============================================================================
//  Stage12Shadow.hlsli - Deep optical-depth cache 공용 조회
// ============================================================================
#ifndef VCLOUD_STAGE12_SHADOW_HLSLI
#define VCLOUD_STAGE12_SHADOW_HLSLI

#include "Stage12ShadowParameters.hlsli"

Texture2DArray<float> stage12NearOpticalDepth : register(t6);
Texture2DArray<float> stage12FarOpticalDepth : register(t7);
SamplerState stage12LinearClampSampler : register(s2);

struct Stage12ShadowSample
{
    float transmittance;
    float opticalDepth;
    float nearOpticalDepth;
    float farOpticalDepth;
    float nearWeight;
    float farValidity;
    float valid;
};

float2 Stage12CacheUv(float3 position, float3 center, float widthMeters)
{
    float3 delta = position - center;
    float invWidth = rcp(max(widthMeters, 1.0));
    return float2(dot(delta, stage12LightRight),
                  dot(delta, stage12LightUp)) * invWidth + 0.5;
}

float Stage12CascadeEdge(float2 uv)
{
    return max(abs(uv.x - 0.5), abs(uv.y - 0.5)) * 2.0;
}

float Stage12SampleTau(Texture2DArray<float> cacheTexture, float2 uv,
                       float worldHeight, uint sliceCount)
{
    float heightFraction = saturate(
        (worldHeight - stage12CloudBottomMeters) /
        max(stage12CloudTopMeters - stage12CloudBottomMeters, 1.0));
    float slice = heightFraction * (float)(max(sliceCount, 2u) - 1u);
    uint lower = (uint)floor(slice);
    uint upper = min(lower + 1u, max(sliceCount, 2u) - 1u);
    float lowerTau = cacheTexture.SampleLevel(
        stage12LinearClampSampler, float3(uv, (float)lower), 0);
    float upperTau = cacheTexture.SampleLevel(
        stage12LinearClampSampler, float3(uv, (float)upper), 0);
    return clamp(lerp(lowerTau, upperTau, frac(slice)),
                 0.0, stage12MaximumOpticalDepth);
}

Stage12ShadowSample SampleStage12DeepShadow(float3 position)
{
    Stage12ShadowSample result;
    result.transmittance = 1.0;
    result.opticalDepth = 0.0;
    result.nearOpticalDepth = 0.0;
    result.farOpticalDepth = 0.0;
    result.nearWeight = 0.0;
    result.farValidity = 0.0;
    result.valid = 0.0;
    bool enabled = stage12ShadowMode == kStage12DeepCache &&
        stage12CacheReady != 0u &&
        stage12LightForward.y >= stage12MinimumSunY;
    if (!enabled)
        return result;

    float2 nearUv = Stage12CacheUv(
        position, stage12NearCenter, stage12NearWidthMeters);
    float2 farUv = Stage12CacheUv(
        position, stage12FarCenter, stage12FarWidthMeters);
    float nearEdge = Stage12CascadeEdge(nearUv);
    float farEdge = Stage12CascadeEdge(farUv);
    result.nearWeight = 1.0 - smoothstep(
        stage12NearCoreEnd, stage12NearBlendEnd, nearEdge);
    result.farValidity = 1.0 - smoothstep(
        stage12FarFadeStart, stage12FarFadeEnd, farEdge);
    result.nearOpticalDepth = Stage12SampleTau(
        stage12NearOpticalDepth, nearUv, position.y,
        stage12NearSliceCount);
    result.farOpticalDepth = Stage12SampleTau(
        stage12FarOpticalDepth, farUv, position.y,
        stage12FarSliceCount);
    float nearT = exp(-result.nearOpticalDepth);
    float farT = lerp(1.0, exp(-result.farOpticalDepth), result.farValidity);
    result.transmittance = saturate(lerp(farT, nearT, result.nearWeight));
    result.opticalDepth = min(-log(max(result.transmittance, 0.0001)),
                              stage12MaximumOpticalDepth);
    result.valid = 1.0;
    return result;
}

float Stage12SurfaceTransmittance(float3 surfacePosition)
{
    // 구름층 아래 표면은 bottom slice(전체 구름 기둥)를 읽는다.
    surfacePosition.y = stage12CloudBottomMeters;
    return SampleStage12DeepShadow(surfacePosition).transmittance;
}

float Stage12SurfaceFactor(float cloudTransmittance)
{
    float shadowed = stage12SurfaceAmbientFloor +
        (1.0 - stage12SurfaceAmbientFloor) * saturate(cloudTransmittance);
    return lerp(1.0, shadowed, stage12SurfaceShadowStrength);
}

// 카메라 레이와 무관하게 Texture2DArray의 한 slice를 화면 UV에 그대로 펼친다.
// tau/maxTau는 실제 값 대부분이 검게 뭉치므로 1-exp(-tau*exposure)로 시각화한다.
float4 Stage12DebugCacheTexture(float2 uv, bool nearCascade)
{
    if (stage12ShadowMode != kStage12DeepCache || stage12CacheReady == 0u)
        return float4(0.45, 0.0, 0.0, 1.0); // cache unavailable 표시

    uint slice = nearCascade
        ? min(stage12DebugNearSlice, max(stage12NearSliceCount, 2u) - 1u)
        : min(stage12DebugFarSlice, max(stage12FarSliceCount, 2u) - 1u);
    float tau = nearCascade
        ? stage12NearOpticalDepth.SampleLevel(
            stage12LinearClampSampler, float3(saturate(uv), (float)slice), 0)
        : stage12FarOpticalDepth.SampleLevel(
            stage12LinearClampSampler, float3(saturate(uv), (float)slice), 0);
    float exposed = 1.0 - exp(-max(tau, 0.0) * stage12CacheDebugExposure);
    return float4(saturate(exposed).xxx, 1.0);
}

#endif
