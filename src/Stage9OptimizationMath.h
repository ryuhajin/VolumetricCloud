#pragma once

#include "HighCloudQuality.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

namespace stage9
{
inline double Smoothstep(double a, double b, double x)
{
    const double t = std::clamp(
        (x - a) / std::max(b - a, 1.0e-9), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

inline double ViewStepMeters(double distanceMeters)
{
    const double factor = 1.0 +
        (highcloud::kFarStepMultiplier - 1.0) * Smoothstep(
            highcloud::kDistanceStepStartMeters,
            highcloud::kDistanceStepEndMeters,
            distanceMeters);
    return highcloud::kViewStepMeters * factor;
}

inline std::vector<double> BuildViewStepLengths(double segmentLength)
{
    std::vector<double> result;
    double cursor = 0.0;
    while (cursor < segmentLength &&
           result.size() < highcloud::kMaximumViewSteps)
    {
        const double step = std::min(
            ViewStepMeters(cursor), segmentLength - cursor);
        if (!(step > 0.0) || !std::isfinite(step))
            break;
        result.push_back(step);
        cursor += step;
    }
    return result;
}

inline double BeerLambert(double density, double extinction,
                          const std::vector<double>& stepLengths)
{
    const double distance = std::accumulate(
        stepLengths.begin(), stepLengths.end(), 0.0);
    return std::exp(-std::max(density, 0.0) *
                    std::max(extinction, 0.0) * distance);
}

inline double CoarseStepMeters(double fullStepMeters)
{
    return std::min(
        fullStepMeters * highcloud::kCoarseStepMultiplier,
        std::max<double>(highcloud::kMaximumSearchStepMeters,
                         fullStepMeters));
}

inline double RewindCoarseHit(double cursor, double coarseLength,
                              double segmentStart = 0.0)
{
    return std::max(segmentStart, cursor - std::max(coarseLength, 0.0));
}

inline bool ShouldEnterCoarse(std::uint32_t consecutiveEmptySamples)
{
    return consecutiveEmptySamples >= highcloud::kEmptySamplesBeforeCoarse;
}

inline bool ShouldEarlyExit(double transmittance)
{
    return std::isfinite(transmittance) &&
        static_cast<float>(transmittance) <=
            highcloud::kTransmittanceThreshold;
}

inline bool SupportDefinitelyEmpty(double localHeight,
                                   double verticalProfile,
                                   double weatherSupport,
                                   double coverage,
                                   double densityMultiplier)
{
    return localHeight < 0.0 || localHeight > 1.0 ||
        verticalProfile <= 0.0 || weatherSupport <= 0.0 ||
        coverage <= 0.0 || densityMultiplier <= 0.0;
}

inline double ConeBoundary(std::size_t index)
{
    if (index >= highcloud::kConeSampleCount)
        return 1.0;
    const double denominator =
        static_cast<double>(highcloud::kConeSampleCount - 1u);
    return std::pow(static_cast<double>(index) / denominator, 1.5) *
        highcloud::kLightFarSampleFraction;
}

struct ConeInterval
{
    double sampleDistance = 0.0;
    double weightMeters = 0.0;
    double radiusMeters = 0.0;
};

inline std::vector<ConeInterval> BuildConeIntervals(double segmentLength)
{
    std::vector<ConeInterval> result;
    if (!(segmentLength > 0.0))
        return result;
    result.reserve(highcloud::kConeSampleCount);
    const double tangent = std::tan(
        highcloud::kConeAngleDegrees * 3.14159265358979323846 / 180.0);
    for (std::size_t index = 0;
         index < highcloud::kConeSampleCount; ++index)
    {
        const double begin = ConeBoundary(index) * segmentLength;
        const double end = ConeBoundary(index + 1u) * segmentLength;
        const double midpoint = 0.5 * (begin + end);
        result.push_back({ midpoint, end - begin, midpoint * tangent });
    }
    return result;
}
}
