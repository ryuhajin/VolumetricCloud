// ============================================================================
//  Stage13ScaleMath.h - 단계 13-0/13-2 공간 단위와 상사 변환 기준
// ----------------------------------------------------------------------------
//  Renderer의 런타임 프리셋과 CPU 회귀 테스트가 함께 사용한다. meter 기반 길이를
//  S배 확대할 때 길이·속도는 S배, 역길이(noise cycle/m, extinction 1/m)는
//  1/S배 해야 같은 무차원 결과가 유지된다.
// ============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace stage13scale
{
struct SimilarityParameters
{
    double horizontalSizeMeters = 16.0;
    double verticalSizeMeters = 3.0;
    double layerBottomAltitudeMeters = -1.0;
    double maxViewTraceDistanceMeters = 50.0;
    double viewFadeStartDistanceMeters = 40.0;
    double maxLightTraceDistanceMeters = 20.0;
    double viewStepMeters = 0.1;
    double lightStepMeters = 0.25;
    double lightRayBiasMeters = 0.01;
    double weatherWorldSizeMeters = 16.0;
    double baseNoiseCyclesPerMeter = 0.35;
    double detailNoiseCyclesPerMeter = 2.5;
    double extinctionPerMeter = 1.0;
    double singleScatteringAlbedo = 1.0;
    double baseWindMetersPerSecond = 0.25;
    double detailWindMetersPerSecond = 0.45;
    double weatherWindMetersPerSecond = 0.10;
    double densityMultiplier = 1.0;
    std::uint32_t maxViewSteps = 128;
    std::uint32_t maxLightSteps = 16;
};

inline bool IsValidScale(double scale)
{
    return std::isfinite(scale) && scale > 0.0;
}

inline SimilarityParameters ScaleSimilarity(const SimilarityParameters& source,
                                            double scale)
{
    if (!IsValidScale(scale))
        return source;

    SimilarityParameters result = source;
    result.horizontalSizeMeters *= scale;
    result.verticalSizeMeters *= scale;
    result.layerBottomAltitudeMeters *= scale;
    result.maxViewTraceDistanceMeters *= scale;
    result.viewFadeStartDistanceMeters *= scale;
    result.maxLightTraceDistanceMeters *= scale;
    result.viewStepMeters *= scale;
    result.lightStepMeters *= scale;
    result.lightRayBiasMeters *= scale;
    result.weatherWorldSizeMeters *= scale;
    result.baseNoiseCyclesPerMeter /= scale;
    result.detailNoiseCyclesPerMeter /= scale;
    result.extinctionPerMeter /= scale;
    result.baseWindMetersPerSecond *= scale;
    result.detailWindMetersPerSecond *= scale;
    result.weatherWindMetersPerSecond *= scale;
    return result;
}

inline double NoiseCoordinate(double worldDistanceMeters,
                              double cyclesPerMeter)
{
    if (!std::isfinite(worldDistanceMeters) ||
        !std::isfinite(cyclesPerMeter))
        return 0.0;
    return worldDistanceMeters * cyclesPerMeter;
}

inline double WeatherCoordinate(double worldDistanceMeters,
                                double weatherWorldSizeMeters)
{
    if (!std::isfinite(worldDistanceMeters) ||
        !std::isfinite(weatherWorldSizeMeters) ||
        weatherWorldSizeMeters <= 0.0)
        return 0.0;
    double coordinate = std::fmod(
        worldDistanceMeters / weatherWorldSizeMeters, 1.0);
    return coordinate < 0.0 ? coordinate + 1.0 : coordinate;
}

inline double OpticalDepth(double density, double extinctionPerMeter,
                           double pathLengthMeters)
{
    if (!std::isfinite(density) || !std::isfinite(extinctionPerMeter) ||
        !std::isfinite(pathLengthMeters))
        return 0.0;
    return std::max(density, 0.0) * std::max(extinctionPerMeter, 0.0) *
        std::max(pathLengthMeters, 0.0);
}

inline double FeatureWavelengthMeters(double cyclesPerMeter)
{
    return std::isfinite(cyclesPerMeter) && cyclesPerMeter > 0.0
        ? 1.0 / cyclesPerMeter
        : 0.0;
}

inline double ScatteringPerMeter(double extinctionPerMeter,
                                 double singleScatteringAlbedo)
{
    if (!std::isfinite(extinctionPerMeter) ||
        !std::isfinite(singleScatteringAlbedo))
        return 0.0;
    return std::max(extinctionPerMeter, 0.0) *
        std::clamp(singleScatteringAlbedo, 0.0, 1.0);
}

inline double SingleScatteringInteraction(double density,
                                          double extinctionPerMeter,
                                          double pathLengthMeters,
                                          double singleScatteringAlbedo)
{
    return std::clamp(singleScatteringAlbedo, 0.0, 1.0) *
        (1.0 - std::exp(-OpticalDepth(
            density, extinctionPerMeter, pathLengthMeters)));
}

inline double SamplesPerWavelength(double cyclesPerMeter,
                                   double stepLengthMeters)
{
    if (!std::isfinite(stepLengthMeters) || stepLengthMeters <= 0.0)
        return 0.0;
    return FeatureWavelengthMeters(cyclesPerMeter) / stepLengthMeters;
}
}
