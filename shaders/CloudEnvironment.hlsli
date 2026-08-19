// ============================================================================
//  CloudEnvironment.hlsli - 단계 8 분석적 환경광과 저비용 다중 산란
// ----------------------------------------------------------------------------
//  1. 최종 밀도와 높이로 하늘/지면 가중치와 Ambient Occlusion을 만든다.
//  2. 단계 6 Light Ray의 광학 깊이를 재사용해 추가 레이 없이 산란 octave를 만든다.
//  3. Direct/Sky/Ground/Multiple을 같은 View 구간 적분으로 카메라에 누적한다.
//  환경광에는 방향 Phase를 곱하지 않으며, 다중 산란만 약해진 Phase를 사용한다.
// ============================================================================
#ifndef VCLOUD_CLOUD_ENVIRONMENT_HLSLI
#define VCLOUD_CLOUD_ENVIRONMENT_HLSLI

#include "EnvironmentParameters.hlsli"
#include "CloudLighting.hlsli"

struct EnvironmentLightingSample
{
    float3 direct;
    float3 silverLiningContribution;
    float3 skyAmbient;
    float3 groundBounce;
    float3 multipleScattering;
    float shapedSunVisibility;
    float ambientVisibility;
    float diagnosticWeight;
};

float ComputeInteractionFraction(float density, float viewStepLength)
{
    float safeDensity = max(density, 0.0);
    float safeLength = max(viewStepLength, 0.0);
    float safeExtinction = max(extinctionCoefficient, 0.0);
    float stepTransmittance = exp(-safeDensity * safeExtinction * safeLength);
    return saturate(1.0 - stepTransmittance);
}

float ComputeMultipleScatteringFactor(float lightOpticalDepth,
                                      float lightTransmittance,
                                      float phaseFactor)
{
    bool enabled = multipleScatteringEnabled >= 0.5 &&
                   multipleScatteringOctaves > 0u;
    uint octaveCount = enabled ? min(multipleScatteringOctaves, 4u) : 0u;
    float energy = saturate(multipleScatteringAttenuation);
    float extinctionScale = saturate(multipleScatteringExtinctionFactor);
    float phaseScale = saturate(multipleScatteringPhaseFactor);
    float safeDepth = max(lightOpticalDepth, 0.0);
    float safePhase = clamp(phaseFactor, 0.0, kMaxPhaseFactor);
    float result = 0.0;
    [unroll]
    for (uint octave = 0u; octave < 4u; ++octave)
    {
        if (octave < octaveCount)
        {
            float octaveLight = exp(-safeDepth * extinctionScale);
            float octavePhase = lerp(1.0, safePhase, phaseScale);
            result += energy * octaveLight * max(octavePhase, 0.0);
            energy *= saturate(multipleScatteringAttenuation);
            extinctionScale *= saturate(multipleScatteringExtinctionFactor);
            phaseScale *= saturate(multipleScatteringPhaseFactor);
        }
    }
    float interiorWeight = lerp(
        1.0, 1.0 - saturate(lightTransmittance),
        saturate(multipleScatteringInteriorBlend));
    return max(result * interiorWeight, 0.0);
}

EnvironmentLightingSample EvaluateEnvironmentLighting(
    CloudDensitySample densitySample, LightMarchResult lightResult,
    PhaseSample phase, float viewTransmittance, float viewStepLength)
{
    EnvironmentLightingSample result = (EnvironmentLightingSample)0;
    float density = max(densitySample.finalDensity, 0.0);
    float height = saturate(densitySample.heightFraction);
    float localVisibility = exp(
        -density * max(ambientOcclusionStrength, 0.0));
    float safeAmbientExponent = clamp(ambientShadowExponent, 0.1, 8.0);
    if (!(safeAmbientExponent >= 0.1 && safeAmbientExponent <= 8.0))
        safeAmbientExponent = 1.0;
    float directionalVisibility = pow(
        saturate(lightResult.transmittance), safeAmbientExponent);
    float visibility = localVisibility * lerp(
        1.0, directionalVisibility, saturate(ambientShadowCoupling));
    float skyWeight = lerp(1.0, height, saturate(ambientHeightInfluence));
    float groundWeight = 1.0 - height;
    float interactionFraction = ComputeInteractionFraction(
        density, viewStepLength);
    float common = saturate(viewTransmittance) *
                   saturate(singleScatteringAlbedo) * interactionFraction;

    // Off 프리셋에서 단계 7과 같은 함수·연산 순서를 사용해 직접광을 보존한다.
    DirectLightingResponse directResponse = EvaluateDirectLightingResponse(
        lightResult.transmittance, phase.phaseFactor);
    float3 directInteraction = ComputeDirectInteractionColor(
        density, viewTransmittance, viewStepLength);
    result.direct = directInteraction * directResponse.shapedTransmittance *
                    max(directResponse.scopedPhase, 0.0);
    result.silverLiningContribution = directInteraction *
        directResponse.shapedTransmittance *
        max(directResponse.scopedPhase - 1.0, 0.0);
    result.skyAmbient = common * max(skyColor, 0.0.xxx) *
                        max(skyStrength, 0.0) * skyWeight * visibility;
    result.groundBounce = common * max(groundColor, 0.0.xxx) *
                          max(groundStrength, 0.0) * groundWeight * visibility;
    float multipleFactor = ComputeMultipleScatteringFactor(
        lightResult.opticalDepth, lightResult.transmittance,
        phase.phaseFactor);
    result.multipleScattering = common * max(sunColor, 0.0.xxx) *
                                max(sunIntensity, 0.0) * multipleFactor;
    result.shapedSunVisibility = directResponse.shapedTransmittance;
    result.ambientVisibility = saturate(visibility);
    result.diagnosticWeight = common;
    return result;
}

#endif
