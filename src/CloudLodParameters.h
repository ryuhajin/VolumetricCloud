// ============================================================================
//  CloudLodParameters.h - 단계 13-5 Detail 거리 LOD CPU/GPU 공유 설정
// ============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// HLSL CloudLodCB(b8)와 16바이트 묶음 순서가 정확히 일치해야 한다.
// 거리는 meter, detailNeutralValue는 실제 Detail Texture3D와 현재 weights로
// 계산한 무차원 평균이다.
struct alignas(16) CloudLodParameters
{
    std::uint32_t detailLodEnabled = 1u;
    float detailLodStartMeters = 32000.0f;
    float detailLodEndMeters = 48000.0f;
    float detailNeutralValue = 0.5f;
};

static_assert(sizeof(CloudLodParameters) == 16,
              "CloudLodParameters must match CloudLodCB");

namespace stage13lod
{
inline CloudLodParameters Sanitize(CloudLodParameters value)
{
    value.detailLodEnabled = value.detailLodEnabled != 0u ? 1u : 0u;
    value.detailLodStartMeters = std::clamp(
        std::isfinite(value.detailLodStartMeters)
            ? value.detailLodStartMeters : 32000.0f,
        0.0f, 100000.0f);
    value.detailLodEndMeters = std::clamp(
        std::isfinite(value.detailLodEndMeters)
            ? value.detailLodEndMeters : 48000.0f,
        value.detailLodStartMeters + 1.0f, 100001.0f);
    value.detailNeutralValue = std::clamp(
        std::isfinite(value.detailNeutralValue)
            ? value.detailNeutralValue : 0.5f,
        0.0f, 1.0f);
    return value;
}
}
