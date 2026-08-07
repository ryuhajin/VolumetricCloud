// ============================================================================
//  PhaseFunction.hlsli - 단계 7 Dual-lobe Henyey-Greenstein 위상 함수
// ----------------------------------------------------------------------------
//  방향 정의
//  - viewRayDirection: 카메라에서 현재 구름 표본으로 향하는 정규화 월드 방향.
//  - directionToSun: 현재 구름 표본에서 태양으로 향하는 정규화 월드 방향.
//  - cosTheta = dot(viewRayDirection, directionToSun).
//    카메라가 태양을 바라보면 두 화살표가 나란해 +1이고 전방 산란이 강해진다.
//
//  출력에는 단위가 없다. 1/(4*pi)를 생략해 g=0인 등방성 HG가 정확히 1이 되며,
//  단계 6 산란을 기준으로 방향에 따른 상대 밝기만 바꾼다. Phase는 산란량에만
//  곱하고 View/Light 투과율과 광학 깊이에는 관여하지 않는다.
// ============================================================================
#ifndef VCLOUD_PHASE_FUNCTION_HLSLI
#define VCLOUD_PHASE_FUNCTION_HLSLI

#include "LightParameters.hlsli"

static const float kMaxPhaseFactor = 16.0;

struct PhaseSample
{
    float cosTheta;      // [-1,1]. +1=태양을 바라봄, -1=태양 반대 방향.
    float forwardLobe;   // 양의 g를 사용한 전방 HG 값.
    float backwardLobe;  // 음의 g를 사용한 후방 HG 값.
    float dualLobe;      // phaseBlend로 두 lobe를 섞은 원본 값.
    float phaseFactor;   // 직접 단일 산란에 실제로 곱할 안전한 [0,16] 배율.
};

// cosTheta와 비대칭도 g로 방향별 상대 산란량을 계산한다.
// g=0은 모든 방향 1, 양수는 +1, 음수는 -1 방향에 봉우리가 생긴다.
// 분모를 1e-4 이상으로 제한해 |g|가 1에 가까울 때 0 나누기와 Inf를 막는다.
float HenyeyGreenstein(float cosTheta, float g)
{
    float safeCosTheta = clamp(cosTheta, -1.0, 1.0);
    float safeG = clamp(g, -0.95, 0.95);
    if (!(safeCosTheta >= -1.0 && safeCosTheta <= 1.0))
        safeCosTheta = 0.0;
    if (!(safeG >= -0.95 && safeG <= 0.95))
        safeG = 0.0;
    float denominatorBase = max(
        1.0 + safeG * safeG - 2.0 * safeG * safeCosTheta, 1e-4);
    float value = (1.0 - safeG * safeG) /
                  (denominatorBase * sqrt(denominatorBase));
    return max(value, 0.0);
}

// 한 픽셀의 카메라 레이와 태양 방향에서 전방·후방 HG 및 적용 배율을 만든다.
// 두 방향의 길이가 거의 0이거나 finite하지 않으면 등방성 중립값 1을 반환한다.
PhaseSample EvaluateDualLobePhase(
    float3 viewRayDirection, float3 lightDirectionToSun)
{
    PhaseSample result;
    result.cosTheta = 0.0;
    result.forwardLobe = 1.0;
    result.backwardLobe = 1.0;
    result.dualLobe = 1.0;
    result.phaseFactor = 1.0;
    float viewLengthSquared = dot(viewRayDirection, viewRayDirection);
    float sunLengthSquared = dot(lightDirectionToSun, lightDirectionToSun);
    bool validDirections = viewLengthSquared > 1e-8 &&
                           sunLengthSquared > 1e-8 &&
                           viewLengthSquared < 1e20 &&
                           sunLengthSquared < 1e20;
    if (validDirections)
    {
        float3 safeView = viewRayDirection * rsqrt(viewLengthSquared);
        float3 safeSun = lightDirectionToSun * rsqrt(sunLengthSquared);
        result.cosTheta = clamp(dot(safeView, safeSun), -1.0, 1.0);

        float safeForwardG = clamp(forwardScatteringG, 0.0, 0.95);
        float safeBackwardG = clamp(backwardScatteringG, -0.95, 0.0);
        float safeBlend = saturate(phaseBlend);
        float safeIntensity = saturate(phaseIntensity);
        if (!(safeForwardG >= 0.0 && safeForwardG <= 0.95)) safeForwardG = 0.65;
        if (!(safeBackwardG >= -0.95 && safeBackwardG <= 0.0)) safeBackwardG = -0.25;
        if (!(safeBlend >= 0.0 && safeBlend <= 1.0)) safeBlend = 0.80;
        if (!(safeIntensity >= 0.0 && safeIntensity <= 1.0)) safeIntensity = 0.25;

        result.forwardLobe = HenyeyGreenstein(result.cosTheta, safeForwardG);
        result.backwardLobe = HenyeyGreenstein(result.cosTheta, safeBackwardG);
        result.dualLobe = lerp(
            result.backwardLobe, result.forwardLobe, safeBlend);

        // Enabled가 꺼지면 계산된 진단 lobe는 남기되 적용 배율만 정확히 1로 만든다.
        float boundedDual = clamp(result.dualLobe, 0.0, kMaxPhaseFactor);
        result.phaseFactor = phaseEnabled >= 0.5
            ? lerp(1.0, boundedDual, safeIntensity)
            : 1.0;
        result.phaseFactor = clamp(result.phaseFactor, 0.0, kMaxPhaseFactor);
    }
    return result;
}

#endif
