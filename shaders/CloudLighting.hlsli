// ============================================================================
//  CloudLighting.hlsli - 단계 6 태양 Light Ray와 단일 산란
// ----------------------------------------------------------------------------
//  렌더링 흐름에서의 위치
//  1. View Ray가 현재 구름 표본의 최종 밀도를 구한다.
//  2. 밀도가 있을 때만 표본에서 태양 방향으로 Light Ray를 만든다.
//  3. Light Ray는 Weather·Type·Height가 적용된 Base Density만 누적한다.
//  4. 광학 깊이를 Beer-Lambert 식으로 태양 투과율로 바꾼다.
//  5. 태양색 × 태양 투과율 × 현재 밀도를 View Ray 산란에 더한다.
//
//  Detail Erosion은 비용과 고주파 깜박임을 분리하기 위해 Light Ray에서 생략한다.
//  단계 7 Phase Function, 단계 8 환경광/다중 산란, 단계 9 Early Exit는 아직 없다.
// ============================================================================
#ifndef VCLOUD_CLOUD_LIGHTING_HLSLI
#define VCLOUD_CLOUD_LIGHTING_HLSLI

#include "Ray.hlsli"
#include "Noise.hlsli"
#include "LightParameters.hlsli"

struct LightMarchResult
{
    float transmittance; // 태양빛 생존 비율. 1=막힘 없음, 0=완전히 소멸.
    float opticalDepth;  // Base Density × 소멸계수 × 거리의 누적값.
    float stepCount;     // 실제 Light Ray 표본 수. 디버그 표시를 위해 float로 보관.
};

// 현재 View 표본에서 태양까지 구름이 얼마나 빛을 가리는지 계산한다.
// samplePosition은 월드 위치(m), lightDirection은 표본→태양 단위 방향이다.
// 길이가 거의 0인 방향, 퇴화 AABB 또는 유효 이탈 구간이 없으면 빛을 막을
// 구름을 계산할 수 없으므로 중립값 transmittance=1을 반환한다.
LightMarchResult ComputeLightTransmittance(
    float3 samplePosition, float3 lightDirection)
{
    LightMarchResult result = { 1.0, 0.0, 0.0 };
    float directionLengthSquared = dot(lightDirection, lightDirection);
    // 1. 잘못된 박스나 방향이면 아래 계산을 건너뛰고 중립값을 반환한다.
    bool validInput = !any(cloudBoundsMax <= cloudBoundsMin) &&
                      directionLengthSquared > 1e-8;
    if (validInput)
    {
        float3 safeDirection = lightDirection * rsqrt(directionLengthSquared);

        // 2. 현재 표면을 다시 맞히지 않도록 아주 조금 태양 쪽에서 시작한다.
        float safeBias = clamp(lightRayBias, 0.0, 1.0);
        float3 rayOrigin = samplePosition + safeDirection * safeBias;
        float tNear = 0.0;
        float tFar = 0.0;
        bool intersects = IntersectRayAABB(
            rayOrigin, safeDirection, cloudBoundsMin, cloudBoundsMax, tNear, tFar);
        float segmentStart = max(tNear, 0.0);
        float segmentLength = tFar - segmentStart;
        if (intersects && segmentLength > 1e-5)
        {
            // 3. 전체 이탈 구간을 maxLightSteps 안에서 균등하게 다시 나눈다.
            float safeTargetStep = max(lightStepSize, 1e-4);
            uint safeMaxSteps = max(maxLightSteps, 1u);
            uint stepCount = min(safeMaxSteps,
                                 (uint)ceil(segmentLength / safeTargetStep));
            float actualStepLength = segmentLength / (float)stepCount;
            float safeExtinction = max(extinctionCoefficient, 0.0);

            // 4. Weather·Cloud Type·Height를 포함한 Base만 누적한다.
            float opticalDepth = 0.0;
            [loop]
            for (uint stepIndex = 0u; stepIndex < stepCount; ++stepIndex)
            {
                float sampleDistance = segmentStart +
                    ((float)stepIndex + 0.5) * actualStepLength;
                float3 lightSamplePosition =
                    rayOrigin + safeDirection * sampleDistance;
                float baseDensity = EvaluateBaseCloudDensity(
                    lightSamplePosition, time).baseDensity;
                opticalDepth += max(baseDensity, 0.0) *
                                safeExtinction * actualStepLength;
            }

            // 5. 광학 깊이가 클수록 지수적으로 태양빛이 줄어든다.
            result.opticalDepth = max(opticalDepth, 0.0);
            result.transmittance = saturate(exp(-result.opticalDepth));
            result.stepCount = (float)stepCount;
        }
    }
    return result;
}

// 한 View Ray 구간에서 카메라 방향으로 새로 들어오는 직접 태양광을 계산한다.
// Phase Function이 아직 없으므로 방향에 따른 산란 차이는 적용하지 않는다.
float3 IntegrateSingleScattering(
    float density, float lightTransmittance,
    float viewTransmittance, float viewStepLength)
{
    float safeDensity = max(density, 0.0);
    float safeLength = max(viewStepLength, 0.0);
    float safeExtinction = max(extinctionCoefficient, 0.0);
    float stepTransmittance = exp(-safeDensity * safeExtinction * safeLength);

    // extinction이 0에 가까우면 0으로 나누지 않고 직사각형 적분으로 되돌아간다.
    float densityIntegral = safeExtinction > 1e-6
        ? (1.0 - stepTransmittance) / safeExtinction
        : safeDensity * safeLength;
    return saturate(viewTransmittance) * max(sunColor, 0.0.xxx) *
           max(sunIntensity, 0.0) * saturate(lightTransmittance) *
           max(scatteringCoefficient, 0.0) * max(densityIntegral, 0.0);
}

#endif
