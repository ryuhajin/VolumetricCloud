// ============================================================================
//  CloudDomainParameters.h - 단계 13 구름 교차 도메인 CPU/GPU 공유 설정
// ============================================================================
#pragma once

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdint>

struct alignas(16) CloudDomainParameters
{
    // Planar 구름층 외곽이 시작하는 월드 Y(m). 올리면 구름층 전체가 함께 상승한다.
    // [직접 조절] Formation/F1 절대 월드 Y(m). Formation [-100000,100000], domain sanitize는 finite만 보장. 초기 1500/Urban 1800; 올리면 층 전체 상승.
    float cloudBottomAltitude = 1500.0f;
    // local thickness와 base lift를 모두 담는 최대 수직 공간(m). Weather column의
    // max thickness + max lift + headroom보다 작으면 위쪽 구름이 잘린다.
    // [직접 조절] Formation/F1 도메인 두께 m. Formation [1,100000], domain sanitize >=0.0001. 초기 3000/Urban 3700. 최대 local 두께+lift+200m를 담아야 한다.
    float cloudLayerThickness = 3000.0f;
    // [직접 조절] Formation 최대 시선 거리 m. Formation [1,200000], domain sanitize >=0.0001. 기본/권장 50000; 늘리면 원경과 비용이 증가한다.
    float maxViewTraceDistance = 50000.0f;
    // [직접 조절] Formation 거리 fade 시작 m. [강제 범위] [0,maxViewTraceDistance], 기본/권장 40000. 낮추면 원경이 더 일찍 희미해진다.
    float viewTraceFadeStartDistance = 40000.0f;

    // [직접 조절] Formation cone 추적 거리 m. Formation [1,200000], domain sanitize >=0.0001. 기본/권장 20000; Deep Cache 크기와는 별개다.
    float maxLightTraceDistance = 20000.0f;
    // Physical Atmosphere가 구름층 입사광을 한 번 조회할 대표 절대 고도다.
    // 전역 교차 도메인 중앙과 분리해 local shape 범위 변경이 조명 기준을
    // 우연히 바꾸지 않게 한다.
    // [파생 값] Formation의 유효 shape 대표 절대 고도 m. domain sanitize [bottom,top], 구조체 초기 3000. LUT 입사광 조회 기준이며 직접 튜닝하지 않는다.
    float cloudLightingReferenceAltitudeMeters = 3000.0f;
    // [패딩] 16바이트 packing을 위한 예약 칸, 0 유지. 화면 효과 없음; 삭제/순서 변경 금지.
    float domainPadding0 = 0.0f;
    // [패딩] 16바이트 packing을 위한 예약 칸, 0 유지. 화면 효과 없음; 삭제/순서 변경 금지.
    float domainPadding1 = 0.0f;
};

static_assert(sizeof(CloudDomainParameters) == 32,
              "CloudDomainParameters must match CloudDomainCB");
static_assert(offsetof(CloudDomainParameters,
                       cloudLightingReferenceAltitudeMeters) == 20,
              "CloudDomainParameters lighting altitude ABI changed");

inline CloudDomainParameters SanitizeCloudDomainParameters(
    const CloudDomainParameters& value)
{
    CloudDomainParameters result = value;
    const auto finiteOr = [](float input, float fallback)
    {
        return std::isfinite(input) ? input : fallback;
    };
    result.cloudBottomAltitude = finiteOr(result.cloudBottomAltitude, 1500.0f);
    result.cloudLayerThickness = std::max(
        finiteOr(result.cloudLayerThickness, 3000.0f), 1e-4f);
    result.maxViewTraceDistance = std::max(
        finiteOr(result.maxViewTraceDistance, 50000.0f), 1e-4f);
    result.viewTraceFadeStartDistance = std::clamp(
        finiteOr(result.viewTraceFadeStartDistance, 40000.0f),
        0.0f, result.maxViewTraceDistance);
    result.maxLightTraceDistance = std::max(
        finiteOr(result.maxLightTraceDistance, 20000.0f), 1e-4f);
    const float layerTop = result.cloudBottomAltitude +
        result.cloudLayerThickness;
    result.cloudLightingReferenceAltitudeMeters = std::clamp(
        finiteOr(result.cloudLightingReferenceAltitudeMeters,
                 result.cloudBottomAltitude +
                     0.5f * result.cloudLayerThickness),
        result.cloudBottomAltitude, layerTop);
    result.domainPadding0 = 0.0f;
    result.domainPadding1 = 0.0f;
    return result;
}
