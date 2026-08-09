#include "Stage13CloudLayerMath.h"

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

bool Near(double a, double b, double epsilon = 1e-6)
{
    return std::abs(a - b) <= epsilon;
}
}

int main()
{
    using stage13::IntersectPlanarLayer;
    const auto below = IntersectPlanarLayer(0.0, 1.0, 1500.0, 3000.0,
                                            50000.0, 50000.0);
    Require(below.hit && Near(below.start, 1500.0) && Near(below.end, 4500.0),
            "below camera must enter at 1.5 km and leave at 4.5 km");

    const auto inside = IntersectPlanarLayer(3000.0, 0.25, 1500.0, 3000.0,
                                             50000.0, 50000.0);
    Require(inside.hit && Near(inside.start, 0.0) && Near(inside.end, 6000.0),
            "inside camera must start at zero");

    const auto above = IntersectPlanarLayer(6000.0, -1.0, 1500.0, 3000.0,
                                            50000.0, 50000.0);
    Require(above.hit && Near(above.start, 1500.0) && Near(above.end, 4500.0),
            "above camera must produce sorted downward interval");

    Require(!IntersectPlanarLayer(0.0, 0.0, 1500.0, 3000.0,
                                  50000.0, 50000.0).hit,
            "parallel ray outside layer must miss");
    const auto parallelInside = IntersectPlanarLayer(
        3000.0, 0.0, 1500.0, 3000.0, 80000.0, 50000.0);
    Require(parallelInside.hit && Near(parallelInside.end, 50000.0),
            "parallel ray inside layer must use trace cap");
    const auto depthClipped = IntersectPlanarLayer(
        0.0, 1.0, 1500.0, 3000.0, 2000.0, 50000.0);
    Require(depthClipped.hit && Near(depthClipped.end, 2000.0),
            "scene depth must clip the layer interval");
    Require(!IntersectPlanarLayer(0.0, 1.0, 1500.0, 0.0,
                                  50000.0, 50000.0).hit,
            "degenerate layer must miss");
    Require(!IntersectPlanarLayer(std::numeric_limits<double>::quiet_NaN(),
                                  1.0, 1500.0, 3000.0, 50000.0, 50000.0).hit,
            "NaN input must miss");

    Require(Near(stage13::HeightFraction(1500.0, 1500.0, 3000.0), 0.0) &&
            Near(stage13::HeightFraction(3000.0, 1500.0, 3000.0), 0.5) &&
            Near(stage13::HeightFraction(4500.0, 1500.0, 3000.0), 1.0),
            "height fraction endpoints and midpoint must match");
    Require(Near(stage13::DistanceFade(39000.0, 40000.0, 50000.0), 1.0) &&
            Near(stage13::DistanceFade(45000.0, 40000.0, 50000.0), 0.5) &&
            Near(stage13::DistanceFade(50000.0, 40000.0, 50000.0), 0.0),
            "distance fade must be smooth across 40-50 km");

    const double uvPositive = stage13::PeriodicWeatherCoordinate(1234.0, 32000.0);
    const double uvWrapped = stage13::PeriodicWeatherCoordinate(33234.0, 32000.0);
    const double uvNegative = stage13::PeriodicWeatherCoordinate(-100.0, 32000.0);
    Require(Near(uvPositive, uvWrapped) && uvNegative > 0.99 && uvNegative < 1.0,
            "weather coordinates must wrap across positive and negative worlds");
    Require(Near(uvPositive,
                 stage13::PeriodicWeatherCoordinate(1234.0, 32000.0)),
            "weather coordinate must depend on world position, not camera position");

    Require(Near(stage13::DetailLodFactor(0.0, 8000.0, 20000.0), 1.0) &&
            Near(stage13::DetailLodFactor(8000.0, 8000.0, 20000.0), 1.0) &&
            Near(stage13::DetailLodFactor(14000.0, 8000.0, 20000.0), 0.5) &&
            Near(stage13::DetailLodFactor(20000.0, 8000.0, 20000.0), 0.0) &&
            Near(stage13::DetailLodFactor(50000.0, 8000.0, 20000.0), 0.0),
            "detail LOD must smoothly fall from 8 to 20 km");
    Require(stage13::DetailLodFactor(10000.0, 8000.0, 20000.0) >
            stage13::DetailLodFactor(18000.0, 8000.0, 20000.0),
            "detail LOD must decrease monotonically");
    Require(Near(stage13::FilteredDetailNoise(0.8, 1.0), 0.8) &&
            Near(stage13::FilteredDetailNoise(0.8, 0.5), 0.65) &&
            Near(stage13::FilteredDetailNoise(0.8, 0.0), 0.5),
            "detail filtering must converge to the 0.5 mean");
    Require(stage13::ShouldSampleDetail(1.0, 0.5, 0.25) &&
            !stage13::ShouldSampleDetail(0.0, 0.5, 0.25),
            "far detail must preserve mean erosion without a noise call");
    Require(Near(stage13::DetailLodFactor(
                9000.0, 8000.0, 8000.0), 0.0) &&
            Near(stage13::DetailLodFactor(
                std::numeric_limits<double>::quiet_NaN(), 8000.0, 20000.0), 0.0),
            "degenerate and NaN detail LOD inputs must be neutral");
    return 0;
}
