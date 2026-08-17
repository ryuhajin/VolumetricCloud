// ============================================================================
//  OptimizationParameters.h - 단계 9 View/Light 기본 최적화 CPU/GPU 공유 설정
// ============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

enum class Stage9OptimizationPreset : std::uint32_t
{
    Fast = 0,
    Balanced = 1,
    Conservative = 2,
    ApprovedReference = 3,
    FineReference = 4,
    Custom = 5,
};

enum class Stage9LightSamplingMode : std::uint32_t
{
    StraightRay = 0,
    DeterministicCone = 1,
};

// HLSL OptimizationCB(b9)의 네 16바이트 묶음과 순서가 정확히 같아야 한다.
struct alignas(16) OptimizationParameters
{
    std::uint32_t supportPrecheckEnabled = 0;
    std::uint32_t emptySpaceSkippingEnabled = 0;
    std::uint32_t viewEarlyExitEnabled = 0;
    std::uint32_t distanceStepEnabled = 0;

    std::uint32_t emptySamplesBeforeCoarse = 3;
    float baseDensityEpsilon = 0.0001f;
    float coarseStepMultiplier = 2.0f;
    float maxSearchStepMeters = 400.0f;

    float distanceStepStartMeters = 16000.0f;
    float distanceStepEndMeters = 48000.0f;
    float farStepMultiplier = 1.0f;
    float optimizationPadding0 = 0.0f;

    std::uint32_t lightSamplingMode =
        static_cast<std::uint32_t>(Stage9LightSamplingMode::StraightRay);
    std::uint32_t coneSampleCount = 6;
    float coneAngleDegrees = 3.0f;
    float lightFarSampleFraction = 0.85f;
};

static_assert(sizeof(OptimizationParameters) == 64,
              "OptimizationParameters must match OptimizationCB");

namespace stage9optimization
{
inline const char* PresetName(Stage9OptimizationPreset preset)
{
    switch (preset)
    {
    case Stage9OptimizationPreset::Fast: return "Fast";
    case Stage9OptimizationPreset::Balanced: return "Balanced";
    case Stage9OptimizationPreset::Conservative: return "Conservative";
    case Stage9OptimizationPreset::ApprovedReference: return "Approved Reference";
    case Stage9OptimizationPreset::FineReference: return "Fine Reference";
    case Stage9OptimizationPreset::Custom: return "Custom";
    default: return "Approved Reference";
    }
}

inline OptimizationParameters Sanitize(OptimizationParameters value)
{
    value.supportPrecheckEnabled = value.supportPrecheckEnabled ? 1u : 0u;
    value.emptySpaceSkippingEnabled = value.emptySpaceSkippingEnabled ? 1u : 0u;
    value.viewEarlyExitEnabled = value.viewEarlyExitEnabled ? 1u : 0u;
    value.distanceStepEnabled = value.distanceStepEnabled ? 1u : 0u;
    value.emptySamplesBeforeCoarse = std::clamp(
        value.emptySamplesBeforeCoarse, 1u, 8u);
    value.baseDensityEpsilon = std::clamp(
        std::isfinite(value.baseDensityEpsilon) ? value.baseDensityEpsilon : 0.0001f,
        0.0f, 0.05f);
    value.coarseStepMultiplier = std::clamp(
        std::isfinite(value.coarseStepMultiplier) ? value.coarseStepMultiplier : 2.0f,
        1.0f, 8.0f);
    value.maxSearchStepMeters = std::clamp(
        std::isfinite(value.maxSearchStepMeters) ? value.maxSearchStepMeters : 400.0f,
        1.0f, 2000.0f);
    value.distanceStepStartMeters = std::clamp(
        std::isfinite(value.distanceStepStartMeters) ? value.distanceStepStartMeters : 16000.0f,
        0.0f, 50000.0f);
    value.distanceStepEndMeters = std::clamp(
        std::isfinite(value.distanceStepEndMeters) ? value.distanceStepEndMeters : 48000.0f,
        value.distanceStepStartMeters + 1.0f, 50000.0f);
    value.farStepMultiplier = std::clamp(
        std::isfinite(value.farStepMultiplier) ? value.farStepMultiplier : 1.0f,
        1.0f, 4.0f);
    value.optimizationPadding0 = 0.0f;
    value.lightSamplingMode = std::min(
        value.lightSamplingMode,
        static_cast<std::uint32_t>(Stage9LightSamplingMode::DeterministicCone));
    value.coneSampleCount = std::clamp(value.coneSampleCount, 5u, 12u);
    value.coneAngleDegrees = std::clamp(
        std::isfinite(value.coneAngleDegrees) ? value.coneAngleDegrees : 3.0f,
        0.0f, 8.0f);
    value.lightFarSampleFraction = std::clamp(
        std::isfinite(value.lightFarSampleFraction) ? value.lightFarSampleFraction : 0.85f,
        0.50f, 0.98f);
    return value;
}

inline bool UsesReferenceShader(const OptimizationParameters& value)
{
    return value.supportPrecheckEnabled == 0u &&
        value.emptySpaceSkippingEnabled == 0u &&
        value.viewEarlyExitEnabled == 0u &&
        value.distanceStepEnabled == 0u &&
        value.lightSamplingMode ==
            static_cast<std::uint32_t>(Stage9LightSamplingMode::StraightRay);
}

inline void ApplyPreset(OptimizationParameters& value,
                        Stage9OptimizationPreset preset,
                        std::uint32_t& maxViewSteps, float& viewStepMeters,
                        std::uint32_t& maxLightSteps, float& lightStepMeters,
                        float& transmittanceThreshold)
{
    value = OptimizationParameters{};
    maxViewSteps = 512u;
    viewStepMeters = 100.0f;
    maxLightSteps = 80u;
    lightStepMeters = 250.0f;
    transmittanceThreshold = 0.01f;
    switch (preset)
    {
    case Stage9OptimizationPreset::Fast:
        value.supportPrecheckEnabled = 1u;
        value.emptySpaceSkippingEnabled = 1u;
        value.viewEarlyExitEnabled = 1u;
        value.distanceStepEnabled = 1u;
        value.coarseStepMultiplier = 4.0f;
        value.farStepMultiplier = 2.0f;
        value.lightSamplingMode = static_cast<std::uint32_t>(
            Stage9LightSamplingMode::DeterministicCone);
        value.coneSampleCount = 5u;
        value.coneAngleDegrees = 4.0f;
        transmittanceThreshold = 0.02f;
        break;
    case Stage9OptimizationPreset::Balanced:
        value.supportPrecheckEnabled = 1u;
        value.emptySpaceSkippingEnabled = 1u;
        value.viewEarlyExitEnabled = 1u;
        value.distanceStepEnabled = 1u;
        value.coarseStepMultiplier = 2.0f;
        value.farStepMultiplier = 1.5f;
        value.lightSamplingMode = static_cast<std::uint32_t>(
            Stage9LightSamplingMode::DeterministicCone);
        value.coneSampleCount = 6u;
        // 자동 화질 스윕에서 4/3도는 Cumulus Light P99 기준을 넘었고,
        // 같은 6탭 비용의 2도/77%가 처음으로 세 외형을 모두 통과했다.
        value.coneAngleDegrees = 2.0f;
        value.lightFarSampleFraction = 0.77f;
        break;
    case Stage9OptimizationPreset::Conservative:
        value.supportPrecheckEnabled = 1u;
        value.emptySpaceSkippingEnabled = 1u;
        value.viewEarlyExitEnabled = 1u;
        value.coarseStepMultiplier = 2.0f;
        value.lightSamplingMode = static_cast<std::uint32_t>(
            Stage9LightSamplingMode::DeterministicCone);
        value.coneSampleCount = 12u;
        value.coneAngleDegrees = 2.0f;
        transmittanceThreshold = 0.005f;
        break;
    case Stage9OptimizationPreset::FineReference:
        maxViewSteps = 1024u;
        viewStepMeters = 50.0f;
        maxLightSteps = 320u;
        lightStepMeters = 62.5f;
        break;
    case Stage9OptimizationPreset::ApprovedReference:
    case Stage9OptimizationPreset::Custom:
    default:
        break;
    }
    value = Sanitize(value);
}
}
