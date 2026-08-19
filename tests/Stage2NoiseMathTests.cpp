#include "Stage2NoiseMath.h"

#include <cmath>
#include <cstdio>

namespace
{
using stage2::DensitySample;
using stage2::Float3;

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

bool Finite(const DensitySample& sample)
{
    return std::isfinite(sample.rawNoise) &&
           std::isfinite(sample.thresholdDensity) &&
           std::isfinite(sample.finalDensity) &&
           std::isfinite(sample.noiseUvw.x) &&
           std::isfinite(sample.noiseUvw.y) &&
           std::isfinite(sample.noiseUvw.z);
}

int Fail(const char* message)
{
    std::fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}
}

int main()
{
    const Float3 world = { 1.37f, -0.42f, 2.11f };
    const Float3 wind = { 1.0f, 0.0f, 0.25f };

    // 같은 noise 좌표는 언제나 같은 값을 반환해야 한다.
    const float deterministicA = stage2::SampleBaseNoise({ 0.31f, -1.27f, 2.83f });
    const float deterministicB = stage2::SampleBaseNoise({ 0.31f, -1.27f, 2.83f });
    if (!NearlyEqual(deterministicA, deterministicB))
        return Fail("value noise is not deterministic");

    // 여러 양수/음수 셀에서 raw noise가 항상 0~1이고 finite인지 검사한다.
    for (int z = -4; z <= 4; ++z)
    {
        for (int y = -4; y <= 4; ++y)
        {
            for (int x = -4; x <= 4; ++x)
            {
                const float value = stage2::SampleBaseNoise(
                    { x * 0.37f, y * 0.29f, z * 0.43f });
                if (!std::isfinite(value) || value < 0.0f || value > 1.0f)
                    return Fail("raw noise must be finite and in [0,1]");
            }
        }
    }

    // smooth interpolation 때문에 셀 경계 양쪽 값이 연속이어야 한다.
    const float leftBoundary = stage2::SampleBaseNoise({ 0.9999f, 0.37f, -0.22f });
    const float rightBoundary = stage2::SampleBaseNoise({ 1.0001f, 0.37f, -0.22f });
    if (std::abs(leftBoundary - rightBoundary) > 1e-3f)
        return Fail("noise is discontinuous across a lattice boundary");

    // 같은 raw noise에서는 coverage가 높을수록 밀도가 줄어들면 안 된다.
    for (int sampleIndex = 0; sampleIndex <= 10; ++sampleIndex)
    {
        const float raw = sampleIndex / 10.0f;
        const float sparse = stage2::RemapCoverage(raw, 0.35f);
        const float dense = stage2::RemapCoverage(raw, 0.75f);
        if (dense + 1e-6f < sparse)
            return Fail("higher coverage should not reduce density");
    }
    if (!NearlyEqual(stage2::RemapCoverage(0.9f, 0.0f), 0.0f))
        return Fail("zero coverage must produce empty density without division");

    const DensitySample normal = stage2::SampleCloudDensity(
        world, 2.0f, 0.35f, 0.55f, 1.0f, wind, 0.25f, 0.0f);
    const DensitySample amplified = stage2::SampleCloudDensity(
        world, 2.0f, 0.35f, 0.55f, 2.0f, wind, 0.25f, 0.0f);
    if (amplified.finalDensity + 1e-6f < normal.finalDensity ||
        amplified.finalDensity > 1.0f)
        return Fail("density multiplier must increase and clamp final density");

    // 카메라 위치는 함수 입력이 아니므로 같은 월드 위치/시간은 같은 밀도다.
    const DensitySample sameWorld = stage2::SampleCloudDensity(
        world, 2.0f, 0.35f, 0.55f, 1.0f, wind, 0.25f, 0.0f);
    if (!NearlyEqual(normal.rawNoise, sameWorld.rawNoise) ||
        !NearlyEqual(normal.finalDensity, sameWorld.finalDensity))
        return Fail("same world position must keep the same density");

    // f(x-vt): dt 뒤 +wind*speed*dt만큼 이동한 점은 이전과 같은 무늬를 본다.
    const float deltaTime = 3.0f;
    const Float3 normalizedWind = stage2::NormalizeOrZero(wind);
    const DensitySample beforeMove = stage2::SampleCloudDensity(
        world, 1.0f, 0.35f, 0.55f, 1.0f, wind, 0.25f, 0.0f);
    const DensitySample afterMove = stage2::SampleCloudDensity(
        world + normalizedWind * (0.25f * deltaTime), 1.0f + deltaTime,
        0.35f, 0.55f, 1.0f, wind, 0.25f, 0.0f);
    if (!NearlyEqual(beforeMove.noiseUvw, afterMove.noiseUvw, 2e-5f) ||
        !NearlyEqual(beforeMove.rawNoise, afterMove.rawNoise, 2e-5f))
        return Fail("noise pattern does not move in the requested wind direction");

    // scale과 offset은 같은 월드 위치의 noise 좌표를 실제로 바꿔야 한다.
    const DensitySample largeBlob = stage2::SampleCloudDensity(
        world, 0.0f, 0.18f, 0.55f, 1.0f, wind, 0.0f, 0.0f);
    const DensitySample smallBlob = stage2::SampleCloudDensity(
        world, 0.0f, 0.70f, 0.55f, 1.0f, wind, 0.0f, 0.0f);
    const DensitySample baseOffset = stage2::SampleCloudDensity(
        world, 0.0f, 0.35f, 0.55f, 1.0f, wind, 0.0f, 0.0f);
    const DensitySample offset = stage2::SampleCloudDensity(
        world, 0.0f, 0.35f, 0.55f, 1.0f, wind, 0.0f, 0.73f);
    if (NearlyEqual(largeBlob.noiseUvw, smallBlob.noiseUvw) ||
        NearlyEqual(baseOffset.noiseUvw, offset.noiseUvw))
        return Fail("scale and offset must change noise coordinates");

    const DensitySample zeroWind = stage2::SampleCloudDensity(
        world, 1000.0f, 0.35f, 0.55f, 1.0f, {}, 50.0f, 0.0f);
    if (!Finite(normal) || !Finite(amplified) || !Finite(zeroWind) ||
        normal.rawNoise < 0.0f || normal.rawNoise > 1.0f ||
        normal.thresholdDensity < 0.0f || normal.thresholdDensity > 1.0f ||
        normal.finalDensity < 0.0f || normal.finalDensity > 1.0f)
        return Fail("all density outputs must be finite and normalized");

    std::puts("Stage 2 noise math tests passed");
    return 0;
}
