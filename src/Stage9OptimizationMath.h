#pragma once

#include "OptimizationParameters.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <vector>

namespace stage9
{
inline double Smoothstep(double a, double b, double x)
{
    const double t = std::clamp((x - a) / std::max(b - a, 1.0e-9), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

inline double ViewStepMeters(double baseStep, double distance,
                             const OptimizationParameters& parameters)
{
    if (parameters.distanceStepEnabled == 0u)
        return baseStep;
    const double factor = 1.0 +
        (parameters.farStepMultiplier - 1.0) * Smoothstep(
            parameters.distanceStepStartMeters,
            parameters.distanceStepEndMeters, distance);
    return baseStep * factor;
}

inline double ConeBoundary(std::size_t index, std::size_t count,
                           double farSampleFraction)
{
    if (count == 0u)
        return 0.0;
    if (count == 1u)
        return index == 0u ? 0.0 : 1.0;
    if (index >= count)
        return 1.0;
    const double nearFraction = std::clamp(farSampleFraction, 0.50, 0.98);
    return std::pow(static_cast<double>(index) /
                    static_cast<double>(count - 1u), 1.5) * nearFraction;
}

inline std::vector<double> BuildViewStepLengths(
    double segmentLength, double baseStep,
    const OptimizationParameters& parameters, std::size_t maxSteps)
{
    std::vector<double> result;
    double cursor = 0.0;
    while (cursor < segmentLength && result.size() < maxSteps)
    {
        const double step = std::min(
            ViewStepMeters(baseStep, cursor, parameters),
            segmentLength - cursor);
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

inline double RewindCoarseHit(double cursor, double coarseLength,
                              double segmentStart = 0.0)
{
    return std::max(segmentStart, cursor - std::max(coarseLength, 0.0));
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

struct ConeInterval
{
    double sampleDistance = 0.0;
    double weightMeters = 0.0;
    double radiusMeters = 0.0;
};

inline std::vector<ConeInterval> BuildConeIntervals(
    double segmentLength, std::size_t count, double angleDegrees,
    double farSampleFraction = 0.85)
{
    std::vector<ConeInterval> result;
    if (!(segmentLength > 0.0) || count == 0u)
        return result;
    result.reserve(count);
    const double tangent = std::tan(std::clamp(angleDegrees, 0.0, 8.0) *
                                    3.14159265358979323846 / 180.0);
    for (std::size_t i = 0; i < count; ++i)
    {
        const double begin = count == 1u ? 0.0 :
            ConeBoundary(i, count, farSampleFraction) * segmentLength;
        const double end = count == 1u ? segmentLength :
            ConeBoundary(i + 1u, count, farSampleFraction) * segmentLength;
        const double midpoint = 0.5 * (begin + end);
        result.push_back({ midpoint, end - begin, midpoint * tangent });
    }
    return result;
}
}
