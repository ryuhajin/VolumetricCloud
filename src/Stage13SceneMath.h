// ============================================================================
//  Stage13SceneMath.h - 단계 13-4D 단일 포트폴리오 씬 CPU 기준
// ============================================================================
#pragma once

#include "CloudParameters.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace stage13scene
{
inline constexpr float kGroundSizeMeters = 10000.0f;
inline constexpr float kGroundHalfSizeMeters = kGroundSizeMeters * 0.5f;
inline constexpr float kBuildingWidthMeters = 20.0f;
inline constexpr float kBuildingHeightMeters = 60.0f;
inline constexpr float kBuildingDepthMeters = 20.0f;
inline constexpr float kSupportedSkyRadiusMeters = 50000.0f;
inline constexpr float kMoveSpeedMetersPerSecond = 1000.0f;
inline constexpr float kMinimumMoveSpeedMetersPerSecond = 1.0f;
inline constexpr float kMaximumMoveSpeedMetersPerSecond = 2000.0f;
inline constexpr float kFastMoveMultiplier = 4.0f;
inline constexpr float kMaximumMovementDeltaSeconds = 0.1f;
inline constexpr float kWheelMovementSecondsPerNotch = 0.25f;

inline float SanitizeMoveSpeed(float speedMetersPerSecond)
{
    return std::clamp(
        std::isfinite(speedMetersPerSecond)
            ? speedMetersPerSecond : kMoveSpeedMetersPerSecond,
        kMinimumMoveSpeedMetersPerSecond,
        kMaximumMoveSpeedMetersPerSecond);
}

inline bool IsCameraMovementKey(std::uint32_t virtualKey)
{
    return virtualKey == static_cast<std::uint32_t>('W') ||
           virtualKey == static_cast<std::uint32_t>('A') ||
           virtualKey == static_cast<std::uint32_t>('S') ||
           virtualKey == static_cast<std::uint32_t>('D');
}

inline int DebugDigitFromVirtualKey(std::uint32_t virtualKey)
{
    if (virtualKey >= static_cast<std::uint32_t>('0') &&
        virtualKey <= static_cast<std::uint32_t>('9'))
        return static_cast<int>(virtualKey - static_cast<std::uint32_t>('0'));
    constexpr std::uint32_t kNumpad0 = 0x60u;
    constexpr std::uint32_t kNumpad9 = 0x69u;
    if (virtualKey >= kNumpad0 && virtualKey <= kNumpad9)
        return static_cast<int>(virtualKey - kNumpad0);
    return -1;
}

inline bool IsPortfolioGlobalKey(std::uint32_t virtualKey)
{
    constexpr std::uint32_t kF1 = 0x70u;
    constexpr std::uint32_t kF8 = 0x77u;
    return DebugDigitFromVirtualKey(virtualKey) >= 0 ||
           IsCameraMovementKey(virtualKey) ||
           (virtualKey >= kF1 && virtualKey <= kF8);
}

inline CloudDebugMode DebugModeFromDigit(int digit)
{
    constexpr CloudDebugMode modes[] = {
        CloudDebugMode::Composite,
        CloudDebugMode::RawNoise,
        CloudDebugMode::WeatherCoverage,
        CloudDebugMode::BaseDensity,
        CloudDebugMode::DetailNoise,
        CloudDebugMode::FinalDensity,
        CloudDebugMode::ViewOpticalDepth,
        CloudDebugMode::AccumulatedDirectLighting,
        CloudDebugMode::Transmittance,
        CloudDebugMode::LightTransmittance,
    };
    return digit >= 0 && digit < static_cast<int>(std::size(modes))
        ? modes[digit] : CloudDebugMode::Composite;
}

inline CloudDebugMode SanitizeDebugMode(CloudDebugMode mode)
{
    const std::int32_t value = static_cast<std::int32_t>(mode);
    const bool regularDiagnostic = (value >= 8 && value <= 25) ||
        (value >= 27 && value <= 33) || (value >= 35 && value <= 59);
    return value == 0 || regularDiagnostic ||
        (value >= 74 && value <= 77) || value == 81
        ? mode : CloudDebugMode::Composite;
}

inline const wchar_t* DebugModeName(CloudDebugMode mode)
{
    switch (mode)
    {
    case CloudDebugMode::Composite: return L"Composite";
    case CloudDebugMode::Transmittance: return L"View Transmittance";
    case CloudDebugMode::ConstantDensity: return L"Sample Density";
    case CloudDebugMode::RawNoise: return L"Raw Noise";
    case CloudDebugMode::ThresholdDensity: return L"Threshold Density";
    case CloudDebugMode::FinalDensity: return L"Final Density";
    case CloudDebugMode::NoiseUvw: return L"Noise UVW";
    case CloudDebugMode::HeightFraction: return L"Height Fraction";
    case CloudDebugMode::HeightProfile: return L"Height Profile";
    case CloudDebugMode::BaseDensity: return L"Base Density";
    case CloudDebugMode::DetailNoise: return L"Detail Noise";
    case CloudDebugMode::Erosion: return L"Erosion";
    case CloudDebugMode::DetailSampleMask: return L"Detail Sample Mask";
    case CloudDebugMode::WeatherCoverage: return L"Weather Coverage";
    case CloudDebugMode::CloudType: return L"Cloud Type";
    case CloudDebugMode::WeatherThresholdDensity: return L"Weather Threshold";
    case CloudDebugMode::TypedShapeProfile: return L"Typed Shape Profile";
    case CloudDebugMode::LightTransmittance: return L"Light Transmittance";
    case CloudDebugMode::LightOpticalDepth: return L"Light Optical Depth";
    case CloudDebugMode::DirectSingleScattering: return L"Direct Scattering";
    case CloudDebugMode::PhaseCosTheta: return L"Phase cosTheta";
    case CloudDebugMode::ForwardPhaseLobe: return L"Forward Phase";
    case CloudDebugMode::BackwardPhaseLobe: return L"Backward Phase";
    case CloudDebugMode::DualPhaseFactor: return L"Dual Phase Factor";
    case CloudDebugMode::AccumulatedDirectLighting: return L"Accumulated Direct";
    case CloudDebugMode::CloudSegmentLength: return L"Cloud Segment Length";
    case CloudDebugMode::CloudHitMask: return L"Cloud Hit Mask";
    case CloudDebugMode::BaseVolumeR: return L"Base Volume R";
    case CloudDebugMode::BaseVolumeG: return L"Base Volume G";
    case CloudDebugMode::BaseVolumeB: return L"Base Volume B";
    case CloudDebugMode::BaseVolumeA: return L"Base Volume A";
    case CloudDebugMode::BaseVolumeCombined: return L"Base Volume Combined";
    case CloudDebugMode::DetailVolumeR: return L"Detail Volume R";
    case CloudDebugMode::DetailVolumeG: return L"Detail Volume G";
    case CloudDebugMode::DetailVolumeB: return L"Detail Volume B";
    case CloudDebugMode::DetailVolumeA: return L"Detail Volume A";
    case CloudDebugMode::DetailVolumeCombined: return L"Detail Volume Combined";
    case CloudDebugMode::TextureWrapDifference: return L"Texture Wrap Difference";
    case CloudDebugMode::WeatherThicknessPotential: return L"Thickness Potential";
    case CloudDebugMode::LocalThickness: return L"Local Thickness";
    case CloudDebugMode::LocalHeightFraction: return L"Local Height";
    case CloudDebugMode::LocalBaseOffset: return L"Local Base Offset";
    case CloudDebugMode::EffectiveShapeCoverage: return L"Shape Coverage";
    case CloudDebugMode::BaseSupportBeforeDensity: return L"Base Support";
    case CloudDebugMode::ViewOpticalDepth: return L"View Optical Depth";
    case CloudDebugMode::AccumulatedSkyAmbient: return L"Sky Ambient";
    case CloudDebugMode::AccumulatedGroundBounce: return L"Ground Bounce";
    case CloudDebugMode::AccumulatedMultipleScattering: return L"Multiple Scattering";
    case CloudDebugMode::CloudDepth: return L"Cloud Depth";
    case CloudDebugMode::SilverLiningContribution: return L"Silver Lining Contribution";
    case CloudDebugMode::ShapedSunVisibility: return L"Shaped Sun Visibility";
    case CloudDebugMode::AmbientVisibility: return L"Ambient Visibility";
    case CloudDebugMode::Stage12NearOpticalDepth: return L"Stage 12 Near Cache Texture";
    case CloudDebugMode::Stage12FarOpticalDepth: return L"Stage 12 Far Cache Texture";
    case CloudDebugMode::Stage12CascadeSelection: return L"Stage 12 Cascade World Lookup";
    case CloudDebugMode::Stage12SurfaceTransmittance: return L"Stage 12 Surface T";
    default: return L"Composite";
    }
}

inline bool ShouldBlockSceneKeyboard(bool wantsTextInput, bool activeUiItem)
{
    return wantsTextInput || activeUiItem;
}

inline float MovementDistance(
    float deltaSeconds, bool fast,
    float speedMetersPerSecond = kMoveSpeedMetersPerSecond)
{
    const float safeDelta = std::clamp(
        std::isfinite(deltaSeconds) ? deltaSeconds : 0.0f,
        0.0f, kMaximumMovementDeltaSeconds);
    const float speed = SanitizeMoveSpeed(speedMetersPerSecond) *
        (fast ? kFastMoveMultiplier : 1.0f);
    return safeDelta * speed;
}

inline float WheelMovementDistance(
    float wheelDelta, bool fast,
    float speedMetersPerSecond = kMoveSpeedMetersPerSecond)
{
    if (!std::isfinite(wheelDelta))
        return 0.0f;
    const float notches = wheelDelta / 120.0f;
    const float speed = SanitizeMoveSpeed(speedMetersPerSecond) *
        (fast ? kFastMoveMultiplier : 1.0f);
    return notches * speed * kWheelMovementSecondsPerNotch;
}
}
