// [학습 지도] b3 + 시선/태양 단위방향 → 상대 HG phase → CloudLighting/Environment. 무차원; 구름 밀도/T는 그대로.
// [수정 안내] [직접 조절]의 CPU/UI 원본을 수정한다. 강제 범위는 입력 계약이며 화질 보장이 아니다.
// 별도 권장 구간이 없는 값은 표시된 기본값을 비교 출발점으로 삼는다. b/t/u/s는 버퍼/읽기/쓰기/샘플러 슬롯.
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
#ifndef VCLOUD_TEST_PHASE_CAP
#define VCLOUD_TEST_PHASE_CAP 2.5
#endif
// 05 검사 전용 compile override. 일반 렌더/핫 리로드는 기존2.5를 유지한다.
static const float kMaxAppliedPhaseFactor = VCLOUD_TEST_PHASE_CAP;
#ifndef VCLOUD_TEST_RIM_CAP
#define VCLOUD_TEST_RIM_CAP 2.5
#endif
static const float kMaxRimPhaseFactor = VCLOUD_TEST_RIM_CAP;

struct PhaseSample
{
    float cosTheta;      // [-1,1]. +1=태양을 바라봄, -1=태양 반대 방향.
    float forwardLobe;   // 양의 g를 사용한 전방 HG 값.
    float backwardLobe;  // 음의 g를 사용한 후방 HG 값.
    float dualLobe;      // phaseBlend로 두 lobe를 섞은 원본 값.
    float phaseFactor;   // 직접 단일 산란에 실제로 곱할 LDR 안전 [0,2.5] 배율.
    float rimPhaseFactor; // 림 전용 상한. Multiple은 기존 phaseFactor를 사용한다.
    float unboundedPhaseFactor; // raw HG 상한16과 intensity 적용 후, 최종 상한 전.
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
// [위상 순서] 1. 두 방향 정규화/유효성 검사 → cosTheta [-1,1].
// 2. 양/음 g의 HG를 계산 → phaseBlend로 두 봉우리 혼합.
// 3. 원본을 [0,16]으로 제한 → intensity로 중립 1과 보간 → 최종 [0,2.5].
// 방향 부호가 뒤집히면 태양 반대쪽이 밝아진다. 이 함수는 T/tau를 바꾸지 않는다.
PhaseSample EvaluateDualLobePhase(
    float3 viewRayDirection, float3 lightDirectionToSun)
{
    PhaseSample result;
    result.cosTheta = 0.0;
    result.forwardLobe = 1.0;
    result.backwardLobe = 1.0;
    result.dualLobe = 1.0;
    result.phaseFactor = 1.0;
    result.rimPhaseFactor = 1.0;
    result.unboundedPhaseFactor = 1.0;
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
        result.unboundedPhaseFactor = result.phaseFactor;
        result.rimPhaseFactor = clamp(result.phaseFactor, 0.0, kMaxRimPhaseFactor);
        result.phaseFactor = clamp(
            result.phaseFactor, 0.0, kMaxAppliedPhaseFactor);
    }
    return result;
}

#endif
