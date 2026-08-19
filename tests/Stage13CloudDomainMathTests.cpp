#include "Stage13CloudDomainMath.h"

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
    using stage13domain::IntersectPlanarLayer;

    const auto below = IntersectPlanarLayer(
        0.0, 1.0, 1500.0, 3000.0, 60000.0, 50000.0);
    Require(below.hit && Near(below.start, 1500.0) && Near(below.end, 4500.0),
            "below-layer ray must enter bottom and leave top");

    const auto inside = IntersectPlanarLayer(
        3000.0, 0.25, 1500.0, 3000.0, 60000.0, 50000.0);
    Require(inside.hit && Near(inside.start, 0.0) && Near(inside.end, 6000.0),
            "inside-layer ray must start at camera");

    const auto above = IntersectPlanarLayer(
        6000.0, -1.0, 1500.0, 3000.0, 60000.0, 50000.0);
    Require(above.hit && Near(above.start, 1500.0) && Near(above.end, 4500.0),
            "above-layer ray must sort downward intersections");

    Require(!IntersectPlanarLayer(
                0.0, 0.0, 1500.0, 3000.0, 60000.0, 50000.0).hit,
            "parallel ray outside layer must miss");
    const auto parallelInside = IntersectPlanarLayer(
        3000.0, 0.0, 1500.0, 3000.0, 80000.0, 50000.0);
    Require(parallelInside.hit && Near(parallelInside.start, 0.0) &&
            Near(parallelInside.end, 50000.0),
            "parallel ray inside layer must use finite trace cap");

    const auto depthClipped = IntersectPlanarLayer(
        0.0, 1.0, 1500.0, 3000.0, 2000.0, 50000.0);
    Require(depthClipped.hit && Near(depthClipped.end, 2000.0),
            "scene depth must clip layer interval");
    Require(!IntersectPlanarLayer(
                0.0, 1.0, 1500.0, 0.0, 60000.0, 50000.0).hit,
            "degenerate layer must miss");
    Require(!IntersectPlanarLayer(
                std::numeric_limits<double>::quiet_NaN(), 1.0,
                1500.0, 3000.0, 60000.0, 50000.0).hit,
            "NaN input must return neutral miss");

    Require(Near(stage13domain::DistanceFade(39000.0, 40000.0, 50000.0), 1.0) &&
            Near(stage13domain::DistanceFade(45000.0, 40000.0, 50000.0), 0.5) &&
            Near(stage13domain::DistanceFade(50000.0, 40000.0, 50000.0), 0.0),
            "40-50 km distance fade must be smooth and finite");
    return 0;
}
