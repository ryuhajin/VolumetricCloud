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
//  단계 9 Early Exit는 아직 없다.
// ============================================================================
#ifndef VCLOUD_CLOUD_LIGHTING_HLSLI
#define VCLOUD_CLOUD_LIGHTING_HLSLI

#include "Ray.hlsli"
#include "Noise.hlsli"
#include "LightParameters.hlsli"
#include "PhaseFunction.hlsli"

struct LightMarchResult
{
    float transmittance; // 태양빛 생존 비율. 1=막힘 없음, 0=완전히 소멸.
    float opticalDepth;  // Base Density × 소멸계수 × 거리의 누적값.
    float stepCount;     // 실제 Light Ray 표본 수. 디버그 표시를 위해 float로 보관.
};

// 현재 View 표본에서 태양까지 구름이 얼마나 빛을 가리는지 계산한다.
// samplePosition은 월드 위치(m), lightDirection은 표본→태양 단위 방향이다.
// 길이가 거의 0인 방향, 퇴화 평면층 또는 유효 이탈 구간이 없으면 빛을 막을
// 구름을 계산할 수 없으므로 중립값 transmittance=1을 반환한다.
LightMarchResult ComputeLightTransmittance(
    float3 samplePosition, float3 lightDirection)
{
    LightMarchResult result = { 1.0, 0.0, 0.0 };
    float directionLengthSquared = dot(lightDirection, lightDirection);
    // 1. 잘못된 평면층이나 방향이면 아래 계산을 건너뛰고 중립값을 반환한다.
    bool validInput = cloudLayerThickness > 1e-5 &&
                      maxLightTraceDistance > 0.0 &&
                      directionLengthSquared > 1e-8;
    if (validInput)
    {
        float3 safeDirection = lightDirection * rsqrt(directionLengthSquared);

        // 2. 현재 표면을 다시 맞히지 않도록 아주 조금 태양 쪽에서 시작한다.
        float safeBias = clamp(lightRayBias, 0.0, 100.0);
        float3 rayOrigin = samplePosition + safeDirection * safeBias;
        float layerTop = cloudBottomAltitude + cloudLayerThickness;
        float segmentStart = 0.0;
        float segmentEnd = 0.0;
        bool intersects = false;
        if (abs(safeDirection.y) <= 1e-6)
        {
            intersects = rayOrigin.y >= cloudBottomAltitude &&
                         rayOrigin.y <= layerTop;
            segmentEnd = maxLightTraceDistance;
        }
        else
        {
            float bottomDistance =
                (cloudBottomAltitude - rayOrigin.y) / safeDirection.y;
            float topDistance = (layerTop - rayOrigin.y) / safeDirection.y;
            float layerNear = min(bottomDistance, topDistance);
            float layerFar = max(bottomDistance, topDistance);
            segmentStart = max(layerNear, 0.0);
            segmentEnd = min(layerFar, maxLightTraceDistance);
            intersects = segmentEnd > segmentStart;
        }
        float segmentLength = segmentEnd - segmentStart;
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
                // 단계 9: Height·Weather가 빈 곳에서는 비싼 3D Base Noise를 읽지 않는다.
                // Light Ray는 여전히 단계 8과 같은 Base Density만 적분하며 step 수와
                // 광학 깊이 수식은 바꾸지 않는다.
                CloudDensitySample lightDensity = MakeEmptyCloudDensitySample();
                if (supportPrecheckEnabled != 0u)
                    EvaluateBaseCloudDensityFast(lightSamplePosition, time, lightDensity);
                else
                    lightDensity = EvaluateBaseCloudDensity(lightSamplePosition, time);
                float baseDensity = lightDensity.baseDensity;
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
// phaseFactor는 단계 7에서 카메라와 태양 각도로 한 픽셀에 한 번 계산한다.
// 이 값은 새로 들어오는 빛만 바꾸며 투과율과 광학 깊이는 바꾸지 않는다.
float3 IntegrateSingleScattering(
    float density, float lightTransmittance,
    float viewTransmittance, float viewStepLength,
    float phaseFactor)
{
    float safeDensity = max(density, 0.0);
    float safeLength = max(viewStepLength, 0.0);
    float safeExtinction = max(extinctionCoefficient, 0.0);
    float stepTransmittance = exp(-safeDensity * safeExtinction * safeLength);

    float stepAlpha = 1.0 - stepTransmittance;
    return saturate(viewTransmittance) * max(sunColor, 0.0.xxx) *
           max(sunIntensity, 0.0) * saturate(lightTransmittance) *
           saturate(singleScatteringAlbedo) * max(stepAlpha, 0.0) *
           clamp(phaseFactor, 0.0, kMaxPhaseFactor);
}

#endif
