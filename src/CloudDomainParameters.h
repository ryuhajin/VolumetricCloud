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
    // 구름층 외곽 AABB가 시작하는 월드 Y(m). 올리면 구름층 전체가 함께 상승한다.
    float cloudBottomAltitude = 1500.0f;
    // local thickness와 base lift를 모두 담는 최대 수직 공간(m). Weather column의
    // max thickness + max lift + headroom보다 작으면 위쪽 구름이 잘린다.
    float cloudLayerThickness = 3000.0f;
    float maxViewTraceDistance = 50000.0f;
    float viewTraceFadeStartDistance = 40000.0f;

    float maxLightTraceDistance = 20000.0f;
    // Physical Atmosphere가 구름층 입사광을 한 번 조회할 대표 절대 고도다.
    // 전역 교차 도메인 중앙과 분리해 local shape 범위 변경이 조명 기준을
    // 우연히 바꾸지 않게 한다.
    float cloudLightingReferenceAltitudeMeters = 3000.0f;
    float domainPadding0 = 0.0f;
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
