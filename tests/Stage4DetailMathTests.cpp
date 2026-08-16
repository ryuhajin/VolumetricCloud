#include "Stage4DetailMath.h"

#include <cmath>
#include <cstdio>

namespace
{
using stage4::DetailDensitySample;
using stage4::NoiseFieldSample;
using stage4::Float3;

bool NearlyEqual(float a, float b, float tolerance = 1e-5f)
{
    return std::abs(a - b) <= tolerance;
}

bool NearlyEqual(const Float3& a, const Float3& b, float tolerance = 1e-5f)
{
    return NearlyEqual(a.x, b.x, tolerance) &&
           NearlyEqual(a.y, b.y, tolerance) &&
           NearlyEqual(a.z, b.z, tolerance);
}

bool Finite(const DetailDensitySample& sample)
{
    return std::isfinite(sample.baseDensity) &&
           std::isfinite(sample.detailNoise) &&
           std::isfinite(sample.erosion) &&
           std::isfinite(sample.finalDensity) &&
           std::isfinite(sample.detailNoiseUvw.x) &&
           std::isfinite(sample.detailNoiseUvw.y) &&
           std::isfinite(sample.detailNoiseUvw.z);
}

int Fail(const char* message)
{
    std::fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}
}

int main()
{
    const Float3 world = { 1.37f, 0.42f, -2.11f };
    const Float3 wind = { 1.0f, 0.0f, 0.25f };

    const NoiseFieldSample detailA = stage4::SampleNoiseField(
        world, 2.0f, 2.5f, wind, 0.45f, 17.3f);
    const NoiseFieldSample detailB = stage4::SampleNoiseField(
        world, 2.0f, 2.5f, wind, 0.45f, 17.3f);
    if (!NearlyEqual(detailA.value, detailB.value) ||
        !NearlyEqual(detailA.uvw, detailB.uvw) ||
        detailA.value < 0.0f || detailA.value > 1.0f)
        return Fail("detail value noise must be deterministic and normalized");

    const NoiseFieldSample coarse = stage4::SampleNoiseField(
        world, 0.0f, 2.5f, wind, 0.0f, 17.3f);
    const NoiseFieldSample fine = stage4::SampleNoiseField(
        world, 0.0f, 6.0f, wind, 0.0f, 17.3f);
    const NoiseFieldSample baseField = stage4::SampleNoiseField(
        world, 0.0f, 0.35f, wind, 0.0f, 0.0f);
    if (NearlyEqual(coarse.uvw, fine.uvw))
        return Fail("detail scale must change detail UVW independently");
    if (NearlyEqual(baseField.uvw, coarse.uvw) ||
        NearlyEqual(baseField.uvw, fine.uvw))
        return Fail("base and detail scales must produce independent UVW");

    const float deltaTime = 3.0f;
    const Float3 safeWind = stage2::NormalizeOrZero(wind);
    const NoiseFieldSample beforeMove = stage4::SampleNoiseField(
        world, 1.0f, 2.5f, wind, 0.45f, 17.3f);
    const NoiseFieldSample afterMove = stage4::SampleNoiseField(
        world + safeWind * (0.45f * deltaTime), 1.0f + deltaTime,
        2.5f, wind, 0.45f, 17.3f);
    if (!NearlyEqual(beforeMove.uvw, afterMove.uvw, 2e-5f) ||
        !NearlyEqual(beforeMove.value, afterMove.value, 2e-5f))
        return Fail("detail pattern must move at its own wind speed");
    const NoiseFieldSample baseAtLaterTime = stage4::SampleNoiseField(
        world, 1.0f + deltaTime, 0.35f, wind, 0.0f, 0.0f);
    const NoiseFieldSample detailAtLaterTime = stage4::SampleNoiseField(
        world, 1.0f + deltaTime, 2.5f, wind, 0.45f, 17.3f);
    if (!NearlyEqual(baseField.uvw, baseAtLaterTime.uvw) ||
        NearlyEqual(coarse.uvw, detailAtLaterTime.uvw))
        return Fail("base and detail wind speeds must move independently");

    const float base = stage4::EvaluateBaseDensity(0.9f, 0.55f, 0.8f, 1.0f);
    const DetailDensitySample eroded = stage4::ApplyDetailErosion(
        base, world, 0.0f, 2.5f, 0.25f, wind, 0.45f, 17.3f, true);
    if (!eroded.detailSampled || eroded.finalDensity > eroded.baseDensity + 1e-6f ||
        !NearlyEqual(eroded.finalDensity,
                     std::max(eroded.baseDensity - eroded.erosion, 0.0f)))
        return Fail("detail must subtract erosion instead of adding density");

    const DetailDensitySample detailOff = stage4::ApplyDetailErosion(
        base, world, 0.0f, 2.5f, 0.0f, wind, 0.45f, 17.3f, true);
    const DetailDensitySample emptyBase = stage4::ApplyDetailErosion(
        0.0f, world, 0.0f, 2.5f, 0.25f, wind, 0.45f, 17.3f, true);
    const DetailDensitySample disabled = stage4::ApplyDetailErosion(
        base, world, 0.0f, 2.5f, 0.25f, wind, 0.45f, 17.3f, false);
    if (detailOff.detailSampled || emptyBase.detailSampled || disabled.detailSampled ||
        !NearlyEqual(detailOff.finalDensity, base) ||
        !NearlyEqual(disabled.finalDensity, base))
        return Fail("neutral paths must preserve base and skip detail sampling");

    const DetailDensitySample strong = stage4::ApplyDetailErosion(
        0.1f, world, 0.0f, 2.5f, 10.0f, wind, 0.45f, 17.3f, true);
    if (!Finite(strong) || strong.finalDensity < 0.0f || strong.finalDensity > 1.0f)
        return Fail("strong erosion must remain finite and clamped");

    const float baseBeforeDetailChange = stage4::EvaluateBaseDensity(
        0.72f, 0.55f, 0.65f, 1.2f);
    (void)stage4::ApplyDetailErosion(
        baseBeforeDetailChange, world, 9.0f, 9.0f, 0.9f,
        wind, 2.5f, -31.0f, true);
    const float baseAfterDetailChange = stage4::EvaluateBaseDensity(
        0.72f, 0.55f, 0.65f, 1.2f);
    if (!NearlyEqual(baseBeforeDetailChange, baseAfterDetailChange))
        return Fail("detail settings must not affect base density");

    std::puts("Stage 4 detail erosion math tests passed");
    return 0;
}
