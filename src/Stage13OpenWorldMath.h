// ============================================================================
//  Stage13OpenWorldMath.h - 단계 13-3 실제 오픈 월드 시작값과 수치 진단
// ============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

enum class Stage13ScenePreset : std::int32_t
{
    Custom,
    Similarity1x,
    Similarity10x,
    Similarity100x,
    Similarity1000x,
    OpenWorld,
};

enum class OpenWorldPipelinePreset : std::int32_t
{
    Custom,
    Legacy1000Baseline,
    Texture3D,
    PeriodicWeather,
    PhysicalShape,
    FullOpenWorld,
};

namespace stage13openworld
{
struct Parameters
{
    double previewHalfSizeMeters = 32000.0;
    double layerBottomMeters = 1500.0;
    double layerThicknessMeters = 6000.0;
    double maxViewTraceMeters = 50000.0;
    double viewFadeStartMeters = 40000.0;
    double maxLightTraceMeters = 20000.0;
    double viewStepMeters = 100.0;
    std::uint32_t maxViewSteps = 512;
    double lightStepMeters = 250.0;
    std::uint32_t maxLightSteps = 80;
    double lightRayBiasMeters = 1.0;
    double baseNoiseCyclesPerMeter = 0.00035;
    double detailNoiseCyclesPerMeter = 0.0025;
    double weatherWorldSizeMeters = 64000.0;
    std::uint32_t weatherResolution = 256;
    double densityMultiplier = 1.0;
    double extinctionPerMeter = 0.00025;
    double singleScatteringAlbedo = 1.0;
    double baseWindMetersPerSecond = 12.0;
    double detailWindMetersPerSecond = 18.0;
    double weatherWindMetersPerSecond = 8.0;
    double coverage = 0.55;
    double bottomFadeEnd = 0.20;
    double topFadeStart = 0.80;
    double detailErosionStrength = 0.25;
};

inline double WavelengthMeters(double cyclesPerMeter)
{
    return std::isfinite(cyclesPerMeter) && cyclesPerMeter > 0.0
        ? 1.0 / cyclesPerMeter : 0.0;
}

inline double SamplesPerWavelength(double cyclesPerMeter, double stepMeters)
{
    return std::isfinite(stepMeters) && stepMeters > 0.0
        ? WavelengthMeters(cyclesPerMeter) / stepMeters : 0.0;
}

inline std::uint32_t RequiredSteps(double distanceMeters, double stepMeters)
{
    if (!std::isfinite(distanceMeters) || !std::isfinite(stepMeters) ||
        distanceMeters <= 0.0 || stepMeters <= 0.0)
        return 0;
    return static_cast<std::uint32_t>(std::ceil(distanceMeters / stepMeters));
}

inline double VerticalOpticalDepth(const Parameters& value)
{
    return std::max(value.densityMultiplier, 0.0) *
        std::max(value.extinctionPerMeter, 0.0) *
        std::max(value.layerThicknessMeters, 0.0);
}

inline double WeatherTexelMeters(const Parameters& value)
{
    return value.weatherResolution > 0
        ? value.weatherWorldSizeMeters / value.weatherResolution : 0.0;
}

inline double ViewDistanceFade(double distanceMeters, const Parameters& value)
{
    if (!std::isfinite(distanceMeters))
        return 0.0;
    if (distanceMeters <= value.viewFadeStartMeters)
        return 1.0;
    if (distanceMeters >= value.maxViewTraceMeters)
        return 0.0;
    const double x = std::clamp(
        (distanceMeters - value.viewFadeStartMeters) /
        (value.maxViewTraceMeters - value.viewFadeStartMeters), 0.0, 1.0);
    const double smooth = x * x * (3.0 - 2.0 * x);
    return 1.0 - smooth;
}

inline Stage13ScenePreset SimilarityPreset(double scale)
{
    if (scale == 1.0) return Stage13ScenePreset::Similarity1x;
    if (scale == 10.0) return Stage13ScenePreset::Similarity10x;
    if (scale == 100.0) return Stage13ScenePreset::Similarity100x;
    if (scale == 1000.0) return Stage13ScenePreset::Similarity1000x;
    return Stage13ScenePreset::Custom;
}

inline const char* PresetName(Stage13ScenePreset preset)
{
    switch (preset)
    {
    case Stage13ScenePreset::Similarity1x: return "Similarity1x";
    case Stage13ScenePreset::Similarity10x: return "Similarity10x";
    case Stage13ScenePreset::Similarity100x: return "Similarity100x";
    case Stage13ScenePreset::Similarity1000x: return "Similarity1000x";
    case Stage13ScenePreset::OpenWorld: return "OpenWorld";
    default: return "Custom";
    }
}


inline const char* PipelinePresetName(OpenWorldPipelinePreset preset)
{
    switch (preset)
    {
    case OpenWorldPipelinePreset::Legacy1000Baseline:
        return "Legacy1000Baseline";
    case OpenWorldPipelinePreset::Texture3D:
        return "1000xTexture3D";
    case OpenWorldPipelinePreset::PeriodicWeather:
        return "1000xPeriodicWeather";
    case OpenWorldPipelinePreset::PhysicalShape:
        return "1000xPhysicalShape";
    case OpenWorldPipelinePreset::FullOpenWorld:
        return "FullOpenWorld";
    default:
        return "Custom";
    }
}
}
