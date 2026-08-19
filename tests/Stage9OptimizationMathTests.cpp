#include "Stage9OptimizationMath.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numeric>

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
}

int main()
{
    Require(sizeof(OptimizationParameters) == 64u, "OptimizationCB size");

    OptimizationParameters parameters;
    std::uint32_t viewSteps = 0;
    std::uint32_t lightSteps = 0;
    float viewStep = 0.0f;
    float lightStep = 0.0f;
    float threshold = 0.0f;
    stage9optimization::ApplyPreset(
        parameters, Stage9OptimizationPreset::Balanced,
        viewSteps, viewStep, lightSteps, lightStep, threshold);
    Require(viewSteps == 512u && viewStep == 100.0f,
            "Balanced view budget");
    Require(parameters.coneSampleCount == 6u &&
            parameters.farStepMultiplier == 1.5f &&
            parameters.coneAngleDegrees == 2.0f &&
            std::abs(parameters.lightFarSampleFraction - 0.77f) < 1e-6f,
            "Balanced optimization values");
    Require(!stage9optimization::UsesReferenceShader(parameters),
            "Balanced optimized shader");

    stage9optimization::ApplyPreset(
        parameters, Stage9OptimizationPreset::ApprovedReference,
        viewSteps, viewStep, lightSteps, lightStep, threshold);
    Require(stage9optimization::UsesReferenceShader(parameters),
            "Approved reference shader");
    Require(lightSteps == 80u && lightStep == 250.0f,
            "Approved light budget");

    parameters.distanceStepEnabled = 1u;
    parameters.farStepMultiplier = 2.0f;
    Require(std::abs(stage9::ViewStepMeters(100.0, 0.0, parameters) - 100.0) < 1e-6,
            "near view step");
    Require(std::abs(stage9::ViewStepMeters(100.0, 50000.0, parameters) - 200.0) < 1e-6,
            "far view step");
    const auto viewLengths = stage9::BuildViewStepLengths(
        50123.0, 100.0, parameters, 1024u);
    const double covered = std::accumulate(
        viewLengths.begin(), viewLengths.end(), 0.0);
    Require(std::abs(covered - 50123.0) < 1e-8,
            "variable steps cover final remainder");
    Require(std::abs(stage9::BeerLambert(0.25, 0.002, viewLengths) -
                     std::exp(-0.25 * 0.002 * 50123.0)) < 1e-12,
            "variable step Beer-Lambert");
    Require(stage9::RewindCoarseHit(850.0, 400.0) == 450.0 &&
            stage9::RewindCoarseHit(200.0, 400.0) == 0.0,
            "coarse hit rewind");
    Require(stage9::SupportDefinitelyEmpty(-0.01, 1.0, 1.0, 1.0, 1.0) &&
            stage9::SupportDefinitelyEmpty(0.5, 0.0, 1.0, 1.0, 1.0) &&
            !stage9::SupportDefinitelyEmpty(0.5, 1.0, 1.0, 1.0, 1.0),
            "support precheck exact-zero factors");

    for (std::size_t count : { 5u, 6u, 8u, 12u })
    {
        const auto intervals = stage9::BuildConeIntervals(20000.0, count, 3.0);
        Require(intervals.size() == count, "cone interval count");
        double totalWeight = 0.0;
        for (const auto& interval : intervals)
        {
            Require(std::isfinite(interval.sampleDistance) &&
                    std::isfinite(interval.weightMeters) &&
                    interval.weightMeters > 0.0 && interval.radiusMeters >= 0.0,
                    "finite cone interval");
            totalWeight += interval.weightMeters;
        }
        Require(std::abs(totalWeight - 20000.0) < 1e-8,
                "cone weights cover segment");
    }

    std::cout << "Stage9OptimizationMath passed\n";
    return 0;
}
