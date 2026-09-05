// ============================================================================
//  HighCloudQuality.h - 최종 단일 High 렌더링 계약
// ============================================================================
#pragma once

#include <cstdint>

namespace highcloud
{
inline constexpr float kViewStepMeters = 100.0f;
inline constexpr std::uint32_t kMaximumViewSteps = 512u;
inline constexpr float kTransmittanceThreshold = 0.01f;

inline constexpr std::uint32_t kEmptySamplesBeforeCoarse = 3u;
inline constexpr float kBaseDensityEpsilon = 0.0001f;
inline constexpr float kCoarseStepMultiplier = 2.0f;
inline constexpr float kMaximumSearchStepMeters = 200.0f;

inline constexpr float kDistanceStepStartMeters = 24000.0f;
inline constexpr float kDistanceStepEndMeters = 50000.0f;
inline constexpr float kFarStepMultiplier = 1.25f;

inline constexpr std::uint32_t kConeSampleCount = 8u;
inline constexpr float kConeAngleDegrees = 2.0f;
inline constexpr float kLightFarSampleFraction = 0.77f;
inline constexpr float kLightRayBiasMeters = 1.0f;
}
