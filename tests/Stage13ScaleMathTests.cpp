#include "Stage13ScaleMath.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

bool Near(double a, double b, double epsilon = 1e-10)
{
    return std::abs(a - b) <= epsilon;
}
}

int main()
{
    using namespace stage13scale;

    const SimilarityParameters original;
    const double worldPosition = 7.25;
    const double density = 0.42;
    const double pathLength = 2.4;
    const double baseNoiseCoordinate = NoiseCoordinate(
        worldPosition, original.baseNoiseCyclesPerMeter);
    const double detailNoiseCoordinate = NoiseCoordinate(
        worldPosition, original.detailNoiseCyclesPerMeter);
    const double weatherCoordinate = WeatherCoordinate(
        worldPosition, original.weatherWorldSizeMeters);
    const double opticalDepth = OpticalDepth(
        density, original.extinctionPerMeter, pathLength);
    const double scatteringInteraction = SingleScatteringInteraction(
        density, original.extinctionPerMeter, pathLength,
        original.singleScatteringAlbedo);
    const double scatteringPerMeter = ScatteringPerMeter(
        original.extinctionPerMeter, original.singleScatteringAlbedo);
    const double baseSamples = SamplesPerWavelength(
        original.baseNoiseCyclesPerMeter, original.viewStepMeters);
    const double detailSamples = SamplesPerWavelength(
        original.detailNoiseCyclesPerMeter, original.viewStepMeters);

    for (double scale : { 1.0, 10.0, 100.0, 1000.0 })
    {
        const SimilarityParameters scaled = ScaleSimilarity(original, scale);
        Require(Near(NoiseCoordinate(
                         worldPosition * scale,
                         scaled.baseNoiseCyclesPerMeter),
                     baseNoiseCoordinate),
                "base noise coordinate must be invariant under similarity scaling");
        Require(Near(NoiseCoordinate(
                         worldPosition * scale,
                         scaled.detailNoiseCyclesPerMeter),
                     detailNoiseCoordinate),
                "detail noise coordinate must be invariant under similarity scaling");
        Require(Near(WeatherCoordinate(
                         worldPosition * scale,
                         scaled.weatherWorldSizeMeters),
                     weatherCoordinate),
                "weather UV must be invariant under similarity scaling");
        Require(Near(OpticalDepth(
                         density, scaled.extinctionPerMeter,
                         pathLength * scale),
                     opticalDepth),
                "optical depth must be invariant under similarity scaling");
        Require(Near(SingleScatteringInteraction(
                         density, scaled.extinctionPerMeter,
                         pathLength * scale,
                         scaled.singleScatteringAlbedo),
                     scatteringInteraction, 1e-5),
                "single scattering must be invariant under similarity scaling");
        Require(Near(scaled.singleScatteringAlbedo,
                     original.singleScatteringAlbedo),
                "single-scattering albedo must remain dimensionless and unscaled");
        Require(Near(ScatteringPerMeter(
                         scaled.extinctionPerMeter,
                         scaled.singleScatteringAlbedo),
                     scatteringPerMeter / scale),
                "sigma_s must scale inversely with similarity scale");
        Require(Near(SamplesPerWavelength(
                         scaled.baseNoiseCyclesPerMeter,
                         scaled.viewStepMeters),
                     baseSamples),
                "base samples per wavelength must remain constant");
        Require(Near(SamplesPerWavelength(
                         scaled.detailNoiseCyclesPerMeter,
                         scaled.viewStepMeters),
                     detailSamples),
                "detail samples per wavelength must remain constant");
        Require(scaled.densityMultiplier == original.densityMultiplier &&
                scaled.maxViewSteps == original.maxViewSteps &&
                scaled.maxLightSteps == original.maxLightSteps,
                "dimensionless values and iteration limits must not scale");
        Require(Near(NoiseCoordinate(
                         original.baseWindMetersPerSecond * scale,
                         scaled.baseNoiseCyclesPerMeter),
                     NoiseCoordinate(original.baseWindMetersPerSecond,
                                     original.baseNoiseCyclesPerMeter)) &&
                Near(NoiseCoordinate(
                         original.detailWindMetersPerSecond * scale,
                         scaled.detailNoiseCyclesPerMeter),
                     NoiseCoordinate(original.detailWindMetersPerSecond,
                                     original.detailNoiseCyclesPerMeter)),
                "wind-driven noise coordinate speed must remain invariant");
    }

    const SimilarityParameters thousand = ScaleSimilarity(original, 1000.0);
    Require(Near(thousand.horizontalSizeMeters, 16000.0) &&
            Near(thousand.verticalSizeMeters, 3000.0) &&
            Near(thousand.layerBottomAltitudeMeters, -1000.0) &&
            Near(thousand.maxViewTraceDistanceMeters, 50000.0) &&
            Near(thousand.viewFadeStartDistanceMeters, 40000.0) &&
            Near(thousand.maxLightTraceDistanceMeters, 20000.0) &&
            Near(thousand.viewStepMeters, 100.0) &&
            Near(thousand.lightStepMeters, 250.0) &&
            Near(thousand.lightRayBiasMeters, 10.0) &&
            Near(thousand.weatherWorldSizeMeters, 16000.0) &&
            Near(thousand.baseNoiseCyclesPerMeter, 0.00035) &&
            Near(thousand.detailNoiseCyclesPerMeter, 0.0025) &&
            Near(thousand.extinctionPerMeter, 0.001) &&
            Near(thousand.baseWindMetersPerSecond, 250.0) &&
            Near(thousand.detailWindMetersPerSecond, 450.0) &&
            Near(thousand.weatherWindMetersPerSecond, 100.0),
            "the 1000x preset must match the documented meter values");

    Require(!IsValidScale(0.0) && !IsValidScale(-1.0) &&
            !IsValidScale(std::numeric_limits<double>::quiet_NaN()),
            "zero, negative and NaN similarity scales must be invalid");
    Require(Near(WeatherCoordinate(-1.0, 16.0), 15.0 / 16.0),
            "negative world positions must wrap to positive weather UV");
    Require(Near(OpticalDepth(0.5, 1.0, 3.0), 1.5) &&
            Near(std::exp(-OpticalDepth(0.5, 1.0, 3.0)), std::exp(-1.5)),
            "Beer-Lambert reference optical depth must remain explicit");
    Require(Near(FeatureWavelengthMeters(0.35), 1.0 / 0.35) &&
            Near(SamplesPerWavelength(2.5, 0.1), 4.0),
            "wavelength and step sampling diagnostics must match Stage 8 defaults");
    return 0;
}
