// [학습 지도] b3/b8 + Base density/Deep Cache → 태양 T/tau와 표본 직접광 → CloudEnvironment. 거리 m, sigma 1/m.
// [수정 안내] [직접 조절]의 CPU/UI 원본을 수정한다. 강제 범위는 입력 계약이며 화질 보장이 아니다.
// 별도 권장 구간이 없는 값은 표시된 기본값을 비교 출발점으로 삼는다. b/t/u/s는 버퍼/읽기/쓰기/샘플러 슬롯.
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
    float surfaceExposure;     // 태양 경로 T의 가중치. 화면 실루엣/시선 두께 판정은 아니다.
    float scopedPhase;         // 외곽 범위가 적용된 최종 Phase 배율.
    float basePhase;           // 중립+음의 phase. 림 변경으로 바뀌지 않는다.
    float rimPhase;            // 양의 phase 성분. Direct 안에 한 번만 더한다.
};

// [직접광 모양] 1. T를 [0,1]로 제한. 2. T^shadowExponent로 차폐 대비를 만든다.
// 3. T^edgeScale은 태양 경로의 낮은 광학 깊이를 우대한다. 화면 외곽만 선택하지 않는다.
// 4. phase-1에 외곽 가중치를 곱한 뒤 1을 더해 중립 배율을 유지한다.
// 이 지수는 밀도/캐시 tau를 바꾸지 않고 직접광의 보이는 대비만 조절한다.
DirectLightingResponse EvaluateDirectLightingResponse(
    float lightTransmittance, float phaseFactor, float rimPhaseFactor)
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
    result.basePhase = 1.0 + min(phaseFactor - 1.0, 0.0) * phaseWeight;
    float rimWeight = lerp(1.0, pow(safeTransmittance,
        safeEdgeScale * clamp(rimDepthScale, 0.5, 2.0)), safeEdgeInfluence);
    result.rimPhase = clamp(rimIntensity, 0.0, 4.0) *
        max(rimPhaseFactor - 1.0, 0.0) * rimWeight;
    result.scopedPhase = result.basePhase + result.rimPhase;
    return result;
}

// [구간 적분] dTau=density*sigma_t*ds, Tstep=exp(-dTau).
// Tview*incidentSun*albedo*(1-Tstep)는 이번 구간에서 관찰자에게 추가되는 RGB다.
// (1-Tstep)를 단순 density로 대체하면 step 길이에 따라 밝기가 달라진다.
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
// [cone fallback 순서] 입력 표본 월드 m와 태양 방향 → (T,tau).
// 1. 태양에 직교하는 tangent/bitangent와 1m bias 시작점 생성.
// 2. 구름층 교차를 구하고 8개 비균일 구간으로 분할.
// 3. 2도 cone 원판을 golden angle로 읽되 각 구간 길이(m)를 가중치로 유지.
// 4. Base 밀도*sigma_t*구간 길이를 tau에 더하고 T≈0.0001이면 종료.
// 표본 수만 줄이거나 길이 가중치를 빼면 그림자 밴딩/밝기 오차가 생긴다.
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
#if defined(VCLOUD_TEST_CACHE_DETAIL)
        density = SampleCloudDensity(position, time, true).finalDensity;
#endif
        opticalDepth += max(density, 0.0) * safeExtinction * intervalLength;
        if (opticalDepth >= kLightEarlyExitOpticalDepth)
            break;
    }
    result.opticalDepth = max(opticalDepth, 0.0);
    result.transmittance = saturate(exp(-result.opticalDepth));
    return result;
}

// [조회 선택] 먼저 t6/t7 Deep Cache를 읽고 valid일 때 그 T/tau를 재사용한다.
// 자원 미준비/낮은 태양으로 valid=0이면 cone 적분. Far 영역 밖은 cache가 T=1로
// fade한 유효 결과이므로 단순히 영역 밖이라는 이유로 cone을 다시 수행하지 않는다.
#if defined(VCLOUD_TEST_SOLAR_REFERENCE_STEP)
static bool solarReferencePixel = false;
#endif
LightMarchResult ComputeLightTransmittance(
    float3 samplePosition, float3 lightDirection)
{
    LightMarchResult result = { 1.0, 0.0 };
#if defined(VCLOUD_TEST_SOLAR_REFERENCE_STEP)
    // 05 테스트 전용: 지정 ROI 또는 전체 화면에서 같은 Base 밀도를 직접 적분한다.
    if (solarReferencePixel)
    {
        float lengthMeters = max(stage12CloudTopMeters - samplePosition.y, 0) / lightDirection.y;
        uint count = max((uint)ceil(lengthMeters / VCLOUD_TEST_SOLAR_REFERENCE_STEP), 1u);
        float ds = lengthMeters / count;
        [loop] for (uint i = 0; i < count; ++i)
        {
#if defined(VCLOUD_TEST_BOUNDARY_SHADOW_DETAIL)
            // 별도 밀도 표현 실험. N0~N3의 동일 Base 모델 수치 오차와 구분한다.
            result.opticalDepth += max(SampleCloudDensity(
                samplePosition + lightDirection * ((i + .5) * ds), time, true).finalDensity, 0) * extinctionCoefficient * ds;
#else
            result.opticalDepth += max(EvaluateLightCloudDensity(
                samplePosition + lightDirection * ((i + .5) * ds), time), 0) * extinctionCoefficient * ds;
#endif
            if (result.opticalDepth >= stage12MaximumOpticalDepth) break;
        }
        result.opticalDepth = min(result.opticalDepth, stage12MaximumOpticalDepth);
        result.transmittance = exp(-result.opticalDepth);
        return result;
    }
#endif
#if defined(VCLOUD_TEST_SUN_UNOCCLUDED)
    // 05 원인 분리 전용. 태양 차폐를 제거해 View/입사광의 줄무늬와 구별한다.
    return result;
#endif
    Stage12ShadowSample cached = SampleStage12DeepShadow(samplePosition);
    if (cached.valid > 0.5)
    {
        result.transmittance = cached.transmittance;
        result.opticalDepth = cached.opticalDepth;
        float cacheWeight = Stage12SunTransitionWeight();
        if (cacheWeight < 1.0)
        {
            LightMarchResult cone = ComputeLightTransmittanceCone(samplePosition, lightDirection);
            // cone T가0으로 underflow해도 원래tau를 보존한다. T와tau를 따로 섞지 않는다.
            result.opticalDepth = lerp(cone.opticalDepth, cached.opticalDepth, cacheWeight);
            result.transmittance = exp(-result.opticalDepth);
        }
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
    float phaseFactor, float3 incidentSun, float rimPhaseFactor)
{
    DirectLightingResponse response = EvaluateDirectLightingResponse(
        lightTransmittance, phaseFactor, rimPhaseFactor);
    return ComputeDirectInteractionColor(
        density, viewTransmittance, viewStepLength, incidentSun) *
        response.shapedTransmittance * max(response.scopedPhase, 0.0);
}

#endif
