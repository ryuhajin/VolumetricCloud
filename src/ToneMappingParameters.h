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
    // [직접 조절: 코드] 0=ACES(기본/권장),1=LinearDebug,2=LegacyShoulder. 숫자 크기는 노출 강도가 아니다.
    ToneMappingMode mode = ToneMappingMode::AcesFitted;
    // [직접 조절] F3 Tone EV [강제 범위]/UI [-8,8], 기본 0. +1이면 HDR에 2배; 광학 두께를 바꾸지 않는다.
    float exposureEv = 0.0f;
    // [직접 조절] F3 Tone 백색점 K [3500,10000], 기본 6500. 낮추면 따뜻하고 높이면 차갑게 보정한다.
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
