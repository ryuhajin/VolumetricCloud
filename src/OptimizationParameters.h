// ============================================================================
//  OptimizationParameters.h - 단계 9 레이마칭 최적화 CPU/GPU 공유 설정
// ============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

enum class Stage9OptimizationPreset : std::int32_t
{
    Off,
    EarlyExitOnly,
    EmptySpaceOnly,
    Balanced,
    Custom,
};

// HLSL OptimizationCB(b5)의 두 16바이트 묶음과 순서가 정확히 같아야 한다.
struct alignas(16) OptimizationParameters
{
    std::uint32_t earlyExitEnabled = 1;
    std::uint32_t supportPrecheckEnabled = 1;
    std::uint32_t emptySpaceSkippingEnabled = 1;
    std::uint32_t emptySamplesBeforeCoarse = 3;

    float baseDensityEpsilon = 0.0001f;
    float coarseStepMultiplier = 4.0f;
    float optimizationPadding[2] = {};
};

static_assert(sizeof(OptimizationParameters) == 32,
              "OptimizationParameters must match OptimizationCB");

namespace stage9optimization
{
inline OptimizationParameters Sanitize(OptimizationParameters value)
{
    value.earlyExitEnabled = value.earlyExitEnabled ? 1u : 0u;
    value.supportPrecheckEnabled = value.supportPrecheckEnabled ? 1u : 0u;
    value.emptySpaceSkippingEnabled = value.emptySpaceSkippingEnabled ? 1u : 0u;
    value.emptySamplesBeforeCoarse =
        std::clamp(value.emptySamplesBeforeCoarse, 1u, 8u);
    value.baseDensityEpsilon = std::clamp(
        std::isfinite(value.baseDensityEpsilon)
            ? value.baseDensityEpsilon : 0.0001f,
        0.0f, 0.05f);
    value.coarseStepMultiplier = std::clamp(
        std::isfinite(value.coarseStepMultiplier)
            ? value.coarseStepMultiplier : 4.0f,
        1.0f, 8.0f);
    value.optimizationPadding[0] = 0.0f;
    value.optimizationPadding[1] = 0.0f;
    return value;
}

inline void ApplyPreset(OptimizationParameters& value,
                        Stage9OptimizationPreset preset)
{
    value = OptimizationParameters{};
    switch (preset)
    {
    case Stage9OptimizationPreset::Off:
        value.earlyExitEnabled = 0;
        value.supportPrecheckEnabled = 0;
        value.emptySpaceSkippingEnabled = 0;
        break;
    case Stage9OptimizationPreset::EarlyExitOnly:
        value.supportPrecheckEnabled = 0;
        value.emptySpaceSkippingEnabled = 0;
        break;
    case Stage9OptimizationPreset::EmptySpaceOnly:
        value.earlyExitEnabled = 0;
        break;
    case Stage9OptimizationPreset::Custom:
        value = Sanitize(value);
        return;
    case Stage9OptimizationPreset::Balanced:
    default:
        break;
    }
    value = Sanitize(value);
}
}
