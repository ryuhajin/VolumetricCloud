// ============================================================================
//  ToneMappingParameters.h - 단계 14 최종 HDR 출력 CPU 설정
// ============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

enum class ToneMappingMode : std::uint32_t
{
    AcesFitted,
    LinearDebug,
    LegacyShoulder,
};

struct ToneMappingParameters
{
    ToneMappingMode mode = ToneMappingMode::AcesFitted;
    float exposureEv = 0.0f;
    float whiteBalanceKelvin = 6500.0f;
};

namespace stage14tone
{
inline ToneMappingParameters Sanitize(ToneMappingParameters value)
{
    value.exposureEv = std::clamp(
        std::isfinite(value.exposureEv) ? value.exposureEv : 0.0f,
        -8.0f, 8.0f);
    value.whiteBalanceKelvin = std::clamp(
        std::isfinite(value.whiteBalanceKelvin)
            ? value.whiteBalanceKelvin : 6500.0f,
        3500.0f, 10000.0f);
    return value;
}
}
