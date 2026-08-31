#include "HighCloudQuality.h"
#include "Stage9OptimizationMath.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

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

void RequireContains(const std::string& text, const char* token)
{
    Require(text.find(token) != std::string::npos,
            "CPU/HLSL High constants must match");
}
}

int main()
{
    Require(highcloud::kViewStepMeters == 100.0f &&
            highcloud::kMaximumViewSteps == 512u &&
            highcloud::kTransmittanceThreshold == 0.01f,
            "High view budget");
    Require(highcloud::kEmptySamplesBeforeCoarse == 3u &&
            highcloud::kBaseDensityEpsilon == 0.0001f &&
            highcloud::kCoarseStepMultiplier == 2.0f &&
            highcloud::kMaximumSearchStepMeters == 200.0f,
            "High empty-space skipping constants");
    Require(std::abs(stage9::ViewStepMeters(0.0) - 100.0) < 1e-9 &&
            std::abs(stage9::ViewStepMeters(50000.0) - 125.0) < 1e-9,
            "fixed distance step range");
    Require(stage9::CoarseStepMeters(100.0) == 200.0 &&
            stage9::ShouldEnterCoarse(3u) &&
            !stage9::ShouldEnterCoarse(2u),
            "fixed coarse search contract");
    Require(stage9::ShouldEarlyExit(0.01) &&
            !stage9::ShouldEarlyExit(0.01001),
            "fixed early exit threshold");
    Require(stage9::RewindCoarseHit(350.0, 200.0) == 150.0 &&
            stage9::RewindCoarseHit(100.0, 200.0) == 0.0,
            "coarse hit rewind");
    Require(stage9::SupportDefinitelyEmpty(
                -0.01, 1.0, 1.0, 1.0, 1.0) &&
            !stage9::SupportDefinitelyEmpty(
                0.5, 1.0, 1.0, 1.0, 1.0),
            "support precheck exact-zero contract");

    const auto intervals = stage9::BuildConeIntervals(20000.0);
    Require(intervals.size() == highcloud::kConeSampleCount,
            "fixed cone tap count");
    double totalWeight = 0.0;
    for (const auto& interval : intervals)
    {
        Require(std::isfinite(interval.sampleDistance) &&
                interval.weightMeters > 0.0 && interval.radiusMeters >= 0.0,
                "finite cone interval");
        totalWeight += interval.weightMeters;
    }
    Require(std::abs(totalWeight - 20000.0) < 1e-8,
            "cone intervals cover light segment");
    Require(highcloud::kConeAngleDegrees == 2.0f &&
            highcloud::kLightFarSampleFraction == 0.77f &&
            highcloud::kLightRayBiasMeters == 1.0f,
            "fixed cone shape");

#ifdef VCLOUD_HIGH_QUALITY_HLSL
    std::ifstream hlsl(VCLOUD_HIGH_QUALITY_HLSL, std::ios::binary);
    Require(static_cast<bool>(hlsl), "High HLSL contract must be readable");
    const std::string source{
        std::istreambuf_iterator<char>(hlsl),
        std::istreambuf_iterator<char>()};
    RequireContains(source, "kHighViewStepMeters = 100.0;");
    RequireContains(source, "kHighMaximumViewSteps = 512u;");
    RequireContains(source, "kHighTransmittanceThreshold = 0.01;");
    RequireContains(source, "kHighEmptySamplesBeforeCoarse = 3u;");
    RequireContains(source, "kHighBaseDensityEpsilon = 0.0001;");
    RequireContains(source, "kHighCoarseStepMultiplier = 2.0;");
    RequireContains(source, "kHighMaximumSearchStepMeters = 200.0;");
    RequireContains(source, "kHighDistanceStepStartMeters = 24000.0;");
    RequireContains(source, "kHighDistanceStepEndMeters = 50000.0;");
    RequireContains(source, "kHighFarStepMultiplier = 1.25;");
    RequireContains(source, "kHighConeSampleCount = 8u;");
    RequireContains(source, "kHighConeAngleDegrees = 2.0;");
    RequireContains(source, "kHighLightFarSampleFraction = 0.77;");
    RequireContains(source, "kHighLightRayBiasMeters = 1.0;");
#endif

    std::cout << "HighCloudQuality passed\n";
    return 0;
}
