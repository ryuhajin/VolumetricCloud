#include "Stage10UpsamplingMath.h"

#include <cmath>
#include <iostream>

namespace
{
bool Require(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}
}

int main()
{
    using namespace stage10upsampling;
    bool passed = true;
    passed &= Require(ScaledExtent(1920, kHalfScale) == 960 &&
                      ScaledExtent(1080, kHalfScale) == 540,
                      "half resolution extent");
    passed &= Require(ScaledExtent(1920, kTwoThirdsScale) == 1280 &&
                      ScaledExtent(1080, kTwoThirdsScale) == 720,
                      "two-thirds resolution extent");
    passed &= Require(ScaledExtent(1920, kThreeQuartersScale) == 1440 &&
                      ScaledExtent(1080, kThreeQuartersScale) == 810,
                      "three-quarters resolution extent");
    passed &= Require(ScaledExtent(1, kHalfScale) == 1 &&
                      ScaledExtent(0, kHalfScale) == 1,
                      "minimum one-pixel extent");
    passed &= Require(std::abs(PixelCenterUv(0, 4) - 0.125f) < 1.0e-6f &&
                      std::abs(PixelCenterUv(3, 4) - 0.875f) < 1.0e-6f,
                      "pixel-center UV mapping");
    passed &= Require(std::abs(OpacityWeightedDepth(600.0f, 0.3f, 5000.0f) -
                               2000.0f) < 1.0e-3f,
                      "opacity-weighted cloud depth");
    passed &= Require(OpacityWeightedDepth(0.0f, 0.0f, 4200.0f) == 4200.0f,
                      "empty ray stores scene limit");
    passed &= Require(RelativeDepthWeight(1000.0f, 1000.0f, 0.01f) > 0.999f &&
                      RelativeDepthWeight(1000.0f, 1100.0f, 0.01f) < 1.0e-4f,
                      "relative depth rejection");
    passed &= Require(TransmittanceWeight(0.5f, 0.5f, 0.1f) > 0.999f &&
                      TransmittanceWeight(0.5f, 0.9f, 0.1f) < 0.001f,
                      "transmittance rejection");
    const auto normalized = NormalizeWeight(1.5f, 3.0f, 1.0e-4f);
    const auto rejected = NormalizeWeight(1.0f, 1.0e-8f, 1.0e-4f);
    passed &= Require(normalized.second && std::abs(normalized.first - 0.5f) <
                      1.0e-6f && !rejected.second &&
                      std::isfinite(rejected.first),
                      "normalization and finite fallback");

    Stage10UpsamplingParameters invalid{};
    invalid.resolutionScale = NAN;
    invalid.filterMode = 99u;
    invalid.minimumWeight = -1.0f;
    const Stage10UpsamplingParameters sanitized = Sanitize(invalid);
    passed &= Require(sanitized.resolutionScale == 1.0f &&
                      sanitized.filterMode == 3u &&
                      sanitized.minimumWeight > 0.0f,
                      "parameter sanitization");
    return passed ? 0 : 1;
}
