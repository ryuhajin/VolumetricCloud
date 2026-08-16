// ============================================================================
//  CloudDomainParameters.h - 단계 13 구름 교차 도메인 CPU/GPU 공유 설정
// ============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

enum class CloudDomainType : std::uint32_t
{
    AabbReference = 0,
    PlanarLayer = 1,
    SphericalShell = 2,
};

struct alignas(16) CloudDomainParameters
{
    std::uint32_t domainType =
        static_cast<std::uint32_t>(CloudDomainType::AabbReference);
    float cloudBottomAltitude = 1500.0f;
    float cloudLayerThickness = 3000.0f;
    float maxViewTraceDistance = 50000.0f;

    float viewTraceFadeStartDistance = 40000.0f;
    float maxLightTraceDistance = 20000.0f;
    float domainPadding[2] = {};
};

static_assert(sizeof(CloudDomainParameters) == 32,
              "CloudDomainParameters must match CloudDomainCB");

inline CloudDomainParameters SanitizeCloudDomainParameters(
    const CloudDomainParameters& value)
{
    CloudDomainParameters result = value;
    if (result.domainType >
        static_cast<std::uint32_t>(CloudDomainType::PlanarLayer))
    {
        // SphericalShell은 단계 13-6 전까지 선택하지 않는다.
        result.domainType =
            static_cast<std::uint32_t>(CloudDomainType::AabbReference);
    }
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
    result.domainPadding[0] = 0.0f;
    result.domainPadding[1] = 0.0f;
    return result;
}
