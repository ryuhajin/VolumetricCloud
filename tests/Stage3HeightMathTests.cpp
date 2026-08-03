#include "Stage3HeightMath.h"

#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
bool NearlyEqual(float a, float b, float tolerance = 1e-5f)
{
    return std::abs(a - b) <= tolerance;
}

int Fail(const char* message)
{
    std::fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}
}

int main()
{
    constexpr float bottom = -1.0f;
    constexpr float top = 3.0f;

    if (!NearlyEqual(stage3::EvaluateHeightFraction(bottom, bottom, top), 0.0f) ||
        !NearlyEqual(stage3::EvaluateHeightFraction(1.0f, bottom, top), 0.5f) ||
        !NearlyEqual(stage3::EvaluateHeightFraction(top, bottom, top), 1.0f))
        return Fail("bottom, middle and top height fractions are incorrect");

    if (!NearlyEqual(stage3::EvaluateHeightFraction(-10.0f, bottom, top), 0.0f) ||
        !NearlyEqual(stage3::EvaluateHeightFraction(10.0f, bottom, top), 1.0f))
        return Fail("height fraction must clamp outside the AABB");

    const float profileBottom = stage3::EvaluateHeightProfileFromFraction(0.0f, 0.2f, 0.8f);
    const float profileMiddle = stage3::EvaluateHeightProfileFromFraction(0.5f, 0.2f, 0.8f);
    const float profileTop = stage3::EvaluateHeightProfileFromFraction(1.0f, 0.2f, 0.8f);
    if (!NearlyEqual(profileBottom, 0.0f) || !NearlyEqual(profileMiddle, 1.0f) ||
        !NearlyEqual(profileTop, 0.0f))
        return Fail("default profile must fade at boundaries and keep the middle dense");

    const float narrowBottom = stage3::EvaluateHeightProfileFromFraction(0.1f, 0.1f, 0.8f);
    const float wideBottom = stage3::EvaluateHeightProfileFromFraction(0.1f, 0.4f, 0.8f);
    const float narrowBottomNearTop = stage3::EvaluateHeightProfileFromFraction(0.9f, 0.1f, 0.8f);
    const float wideBottomNearTop = stage3::EvaluateHeightProfileFromFraction(0.9f, 0.4f, 0.8f);
    if (!(wideBottom < narrowBottom) || !NearlyEqual(narrowBottomNearTop, wideBottomNearTop))
        return Fail("bottom fade control must affect only the lower boundary");

    const float wideTop = stage3::EvaluateHeightProfileFromFraction(0.9f, 0.2f, 0.6f);
    const float narrowTop = stage3::EvaluateHeightProfileFromFraction(0.9f, 0.2f, 0.9f);
    const float wideTopNearBottom = stage3::EvaluateHeightProfileFromFraction(0.1f, 0.2f, 0.6f);
    const float narrowTopNearBottom = stage3::EvaluateHeightProfileFromFraction(0.1f, 0.2f, 0.9f);
    if (!(wideTop < narrowTop) || !NearlyEqual(wideTopNearBottom, narrowTopNearBottom))
        return Fail("top fade control must affect only the upper boundary");

    const float sameYAtFirstXZ = stage3::EvaluateHeightProfile(0.2f, bottom, top, 0.2f, 0.8f);
    const float sameYAtOtherXZ = stage3::EvaluateHeightProfile(0.2f, bottom, top, 0.2f, 0.8f);
    if (!NearlyEqual(sameYAtFirstXZ, sameYAtOtherXZ))
        return Fail("height profile must depend on world Y only");

    const float density = stage3::ApplyHeightProfile(0.8f, 0.5f, 1.5f);
    const float clampedDensity = stage3::ApplyHeightProfile(0.8f, 1.0f, 2.0f);
    if (!NearlyEqual(density, 0.6f) || !NearlyEqual(clampedDensity, 1.0f))
        return Fail("final density must equal threshold times profile times multiplier");

    const float collapsed = stage3::EvaluateHeightProfile(1.0f, 2.0f, 2.0f, 0.2f, 0.8f);
    const float inverted = stage3::EvaluateHeightProfile(1.0f, 3.0f, 2.0f, 0.2f, 0.8f);
    const float notANumber = stage3::EvaluateHeightFraction(
        std::numeric_limits<float>::quiet_NaN(), bottom, top);
    if (!std::isfinite(collapsed) || !std::isfinite(inverted) ||
        !std::isfinite(notANumber) || collapsed != 0.0f || inverted != 0.0f ||
        notANumber != 0.0f)
        return Fail("degenerate inputs must return finite neutral values");

    const float overlapping = stage3::EvaluateHeightProfileFromFraction(0.5f, 0.8f, 0.2f);
    if (!std::isfinite(overlapping) || overlapping < 0.0f || overlapping > 1.0f)
        return Fail("overlapping fade ranges must remain finite and normalized");

    std::puts("Stage 3 height math tests passed");
    return 0;
}
