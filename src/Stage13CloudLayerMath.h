#pragma once

#include <algorithm>
#include <cmath>

namespace stage13
{
struct CloudLayerInterval
{
    bool hit = false;
    double start = 0.0;
    double end = 0.0;
};

inline CloudLayerInterval IntersectPlanarLayer(
    double originY, double directionY, double bottomAltitude,
    double layerThickness, double sceneDistance, double maximumTraceDistance)
{
    CloudLayerInterval result;
    const bool finite = std::isfinite(originY) && std::isfinite(directionY) &&
        std::isfinite(bottomAltitude) && std::isfinite(layerThickness) &&
        std::isfinite(sceneDistance) && std::isfinite(maximumTraceDistance);
    if (!finite || layerThickness <= 1e-5 || sceneDistance <= 0.0 ||
        maximumTraceDistance <= 0.0)
        return result;

    const double topAltitude = bottomAltitude + layerThickness;
    const double traceLimit = std::min(sceneDistance, maximumTraceDistance);
    if (std::abs(directionY) <= 1e-6)
    {
        if (originY < bottomAltitude || originY > topAltitude)
            return result;
        result.hit = true;
        result.end = traceLimit;
        return result;
    }

    const double bottomDistance = (bottomAltitude - originY) / directionY;
    const double topDistance = (topAltitude - originY) / directionY;
    result.start = std::max(std::min(bottomDistance, topDistance), 0.0);
    result.end = std::min(std::max(bottomDistance, topDistance), traceLimit);
    result.hit = result.end > result.start;
    if (!result.hit)
        result = {};
    return result;
}

inline double HeightFraction(double worldY, double bottomAltitude,
                             double layerThickness)
{
    if (!std::isfinite(worldY) || !std::isfinite(bottomAltitude) ||
        !std::isfinite(layerThickness) || layerThickness <= 1e-6)
        return 0.0;
    return std::clamp((worldY - bottomAltitude) / layerThickness, 0.0, 1.0);
}

inline double DistanceFade(double distance, double fadeStart,
                           double maximumDistance)
{
    if (!std::isfinite(distance) || !std::isfinite(fadeStart) ||
        !std::isfinite(maximumDistance) || maximumDistance <= 0.0)
        return 0.0;
    const double start = std::clamp(fadeStart, 0.0, maximumDistance);
    const double width = maximumDistance - start;
    if (width <= 1e-6)
        return distance < maximumDistance ? 1.0 : 0.0;
    const double t = std::clamp((distance - start) / width, 0.0, 1.0);
    const double smooth = t * t * (3.0 - 2.0 * t);
    return 1.0 - smooth;
}

inline double PeriodicWeatherCoordinate(double worldCoordinate,
                                        double worldSize,
                                        double offset = 0.0)
{
    if (!std::isfinite(worldCoordinate) || !std::isfinite(worldSize) ||
        !std::isfinite(offset) || worldSize <= 1e-6)
        return 0.0;
    double value = std::fmod(worldCoordinate / worldSize + offset, 1.0);
    if (value < 0.0)
        value += 1.0;
    return value;
}
}
