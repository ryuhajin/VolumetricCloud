// ============================================================================
//  CloudLighting.hlsli - 단계 6 Light Ray와 단계 7 방향성 단일 산란 기반
// ----------------------------------------------------------------------------
//  렌더링 흐름에서의 위치
//  1. View Ray가 현재 구름 표본의 최종 밀도를 구한다.
//  2. 밀도가 있을 때만 표본에서 태양 방향으로 Light Ray를 만든다.
//  3. Light Ray는 Weather·Type·Height가 적용된 Base Density만 누적한다.
//  4. 광학 깊이를 Beer-Lambert 식으로 태양 투과율로 바꾼다.
//  5. 단계 7 Phase Factor를 방향성 산란량에 곱해 View Ray에 더한다.
//
//  Detail Erosion은 비용과 고주파 깜박임을 분리하기 위해 Light Ray에서 생략한다.
//  단계 8 환경광/다중 산란은 CloudEnvironment.hlsli가 이 결과 위에 더한다.
//  13-5 Light 전용 공백 precheck와 T<=0.0001 조기 종료를 사용한다.
//  View Ray의 고정 High early exit와 coarse march는 VolumetricClouds가 담당한다.
// ============================================================================
#ifndef VCLOUD_CLOUD_LIGHTING_HLSLI
#define VCLOUD_CLOUD_LIGHTING_HLSLI

#include "CloudDomainParameters.hlsli"
#include "Noise.hlsli"
#include "LightParameters.hlsli"
#include "PhaseFunction.hlsli"
#include "Stage12Shadow.hlsli"
#include "Stage14Atmosphere.hlsli"

static const float kLightEarlyExitOpticalDepth = 9.21034037;

struct LightMarchResult
{
    float transmittance; // 태양빛 생존 비율. 1=막힘 없음, 0=완전히 소멸.
    float opticalDepth;  // Base Density × 소멸계수 × 거리의 누적값.
};

struct DirectLightingResponse
{
    float shapedTransmittance; // shadowExponent가 적용된 직접광 투과율.
    float surfaceExposure;     // 1에 가까울수록 태양 쪽 얇은 외곽이다.
    float scopedPhase;         // 외곽 범위가 적용된 최종 Phase 배율.
};

DirectLightingResponse EvaluateDirectLightingResponse(
    float lightTransmittance, float phaseFactor)
{
    DirectLightingResponse result;
    float safeTransmittance = saturate(lightTransmittance);
    float safeShadowExponent = clamp(shadowExponent, 0.5, 4.0);
    float safeEdgeScale = clamp(edgeOpticalDepthScale, 0.25, 8.0);
    float safeEdgeInfluence = saturate(edgeInfluence);
    if (!(safeShadowExponent >= 0.5 && safeShadowExponent <= 4.0))
        safeShadowExponent = 1.0;
    if (!(safeEdgeScale >= 0.25 && safeEdgeScale <= 8.0))
        safeEdgeScale = 1.0;
    if (!(safeEdgeInfluence >= 0.0 && safeEdgeInfluence <= 1.0))
        safeEdgeInfluence = 0.0;
    result.shapedTransmittance = pow(
        safeTransmittance, safeShadowExponent);
    result.surfaceExposure = pow(safeTransmittance, safeEdgeScale);
    float phaseWeight = lerp(1.0, result.surfaceExposure, safeEdgeInfluence);
    result.scopedPhase = 1.0 +
        (clamp(phaseFactor, 0.0, kMaxPhaseFactor) - 1.0) * phaseWeight;
    return result;
}

float3 ComputeDirectInteractionColor(
    float density, float viewTransmittance, float viewStepLength,
    float3 incidentSun)
{
    float safeDensity = max(density, 0.0);
    float safeLength = max(viewStepLength, 0.0);
    float safeExtinction = max(extinctionCoefficient, 0.0);
    float stepTransmittance = exp(-safeDensity * safeExtinction * safeLength);
    float interactionFraction = saturate(1.0 - stepTransmittance);
    return saturate(viewTransmittance) * max(incidentSun, 0.0.xxx) *
           saturate(singleScatteringAlbedo) *
           interactionFraction;
}


float ConeBoundaryFraction(uint index, uint count)
{
    if (index >= count)
        return 1.0;
    float denominator = max((float)(count - 1u), 1.0);
    return pow((float)index / denominator, 1.5) *
        kHighLightFarSampleFraction;
}

// 같은 태양 직선의 균일 표본이 만드는 평행 띠를 줄이기 위해 태양 축 주변을
// golden-angle로 넓혀 읽는다. 각 표본은 담당 구간 길이를 그대로 가중치로 써
// 균일 밀도에서도 Beer-Lambert 광학 깊이를 보존한다.
LightMarchResult ComputeLightTransmittanceCone(
    float3 samplePosition, float3 lightDirection)
{
    LightMarchResult result = { 1.0, 0.0 };
    float directionLengthSquared = dot(lightDirection, lightDirection);
    if (directionLengthSquared <= 1e-8)
        return result;

    float3 safeDirection = lightDirection * rsqrt(directionLengthSquared);
    float3 helper = abs(safeDirection.y) < 0.999
        ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(helper, safeDirection));
    float3 bitangent = cross(safeDirection, tangent);
    float3 rayOrigin = samplePosition + safeDirection *
        kHighLightRayBiasMeters;
    float segmentStart = 0.0;
    float segmentEnd = 0.0;
    bool intersects = IntersectCloudDomain(
        rayOrigin, safeDirection, 1e30, true, segmentStart, segmentEnd);
    float segmentLength = segmentEnd - segmentStart;
    if (!intersects || segmentLength <= 1e-5)
        return result;

    uint count = kHighConeSampleCount;
    float safeExtinction = max(extinctionCoefficient, 0.0);
    float coneTangent = tan(radians(kHighConeAngleDegrees));
    float opticalDepth = 0.0;
    static const float goldenAngle = 2.39996323;
    [loop]
    for (uint index = 0u; index < count; ++index)
    {
        float beginFraction = ConeBoundaryFraction(index, count);
        float endFraction = ConeBoundaryFraction(index + 1u, count);
        float beginDistance = beginFraction * segmentLength;
        float endDistance = endFraction * segmentLength;
        float intervalLength = max(endDistance - beginDistance, 0.0);
        float centerDistance = 0.5 * (beginDistance + endDistance);
        float diskRadius = sqrt(((float)index + 0.5) / (float)count);
        float angle = (float)index * goldenAngle;
        float2 disk = diskRadius * float2(cos(angle), sin(angle));
        float coneRadius = centerDistance * coneTangent;
        float3 position = rayOrigin + safeDirection *
            (segmentStart + centerDistance) +
            (tangent * disk.x + bitangent * disk.y) * coneRadius;
        float density = EvaluateLightCloudDensity(position, time);
        opticalDepth += max(density, 0.0) * safeExtinction * intervalLength;
        if (opticalDepth >= kLightEarlyExitOpticalDepth)
            break;
    }
    result.opticalDepth = max(opticalDepth, 0.0);
    result.transmittance = saturate(exp(-result.opticalDepth));
    return result;
}

LightMarchResult ComputeLightTransmittance(
    float3 samplePosition, float3 lightDirection)
{
    LightMarchResult result = { 1.0, 0.0 };
    Stage12ShadowSample cached = SampleStage12DeepShadow(samplePosition);
    if (cached.valid > 0.5)
    {
        result.transmittance = cached.transmittance;
        result.opticalDepth = cached.opticalDepth;
    }
    else
        result = ComputeLightTransmittanceCone(samplePosition, lightDirection);
    return result;
}

// 한 View Ray 구간에서 카메라 방향으로 새로 들어오는 직접 태양광을 계산한다.
// phaseFactor는 단계 7에서 카메라와 태양 각도로 한 픽셀에 한 번 계산한다.
// 이 값은 새로 들어오는 빛만 바꾸며 투과율과 광학 깊이는 바꾸지 않는다.
float3 IntegrateSingleScattering(
    float density, float lightTransmittance,
    float viewTransmittance, float viewStepLength,
    float phaseFactor, float3 incidentSun)
{
    DirectLightingResponse response = EvaluateDirectLightingResponse(
        lightTransmittance, phaseFactor);
    return ComputeDirectInteractionColor(
        density, viewTransmittance, viewStepLength, incidentSun) *
        response.shapedTransmittance * max(response.scopedPhase, 0.0);
}

#endif
