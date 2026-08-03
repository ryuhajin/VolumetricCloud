// ============================================================================
//  Stage2NoiseMath.h - 단계 2 CPU 수치 회귀용 단일 3D value noise
// ----------------------------------------------------------------------------
//  HLSL의 월드→noise 좌표, 8-corner 보간과 coverage remap 규칙을 CPU에서
//  독립 검사한다. 실제 렌더링에는 사용하지 않는다.
// ============================================================================
#pragma once

#include <algorithm>
#include <cmath>

namespace stage2
{
struct Float3
{
    float x;
    float y;
    float z;
};

struct DensitySample
{
    float rawNoise = 0.0f;
    float thresholdDensity = 0.0f;
    float finalDensity = 0.0f;
    Float3 noiseUvw = {};
};

inline Float3 operator+(const Float3& a, const Float3& b)
{
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}

inline Float3 operator-(const Float3& a, const Float3& b)
{
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}

inline Float3 operator*(const Float3& value, float scale)
{
    return { value.x * scale, value.y * scale, value.z * scale };
}

inline float Fraction(float value)
{
    return value - std::floor(value);
}

inline float Lerp(float a, float b, float amount)
{
    return a + (b - a) * amount;
}

inline Float3 NormalizeOrZero(const Float3& value)
{
    const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    if (!(length > 1e-6f))
        return {};
    return value * (1.0f / length);
}

inline float HashNoiseCorner(const Float3& latticePoint)
{
    Float3 scrambled = {
        Fraction(latticePoint.x * 0.1031f),
        Fraction(latticePoint.y * 0.1031f),
        Fraction(latticePoint.z * 0.1031f),
    };
    const float mixed = scrambled.x * (scrambled.y + 33.33f) +
                        scrambled.y * (scrambled.z + 33.33f) +
                        scrambled.z * (scrambled.x + 33.33f);
    scrambled = scrambled + Float3{ mixed, mixed, mixed };
    return Fraction((scrambled.x + scrambled.y) * scrambled.z);
}

inline float SampleBaseNoise(const Float3& noiseUvw)
{
    const Float3 cell = {
        std::floor(noiseUvw.x), std::floor(noiseUvw.y), std::floor(noiseUvw.z)
    };
    const Float3 local = {
        Fraction(noiseUvw.x), Fraction(noiseUvw.y), Fraction(noiseUvw.z)
    };
    const Float3 smooth = {
        local.x * local.x * (3.0f - 2.0f * local.x),
        local.y * local.y * (3.0f - 2.0f * local.y),
        local.z * local.z * (3.0f - 2.0f * local.z),
    };

    const float n000 = HashNoiseCorner(cell + Float3{ 0, 0, 0 });
    const float n100 = HashNoiseCorner(cell + Float3{ 1, 0, 0 });
    const float n010 = HashNoiseCorner(cell + Float3{ 0, 1, 0 });
    const float n110 = HashNoiseCorner(cell + Float3{ 1, 1, 0 });
    const float n001 = HashNoiseCorner(cell + Float3{ 0, 0, 1 });
    const float n101 = HashNoiseCorner(cell + Float3{ 1, 0, 1 });
    const float n011 = HashNoiseCorner(cell + Float3{ 0, 1, 1 });
    const float n111 = HashNoiseCorner(cell + Float3{ 1, 1, 1 });

    const float x00 = Lerp(n000, n100, smooth.x);
    const float x10 = Lerp(n010, n110, smooth.x);
    const float x01 = Lerp(n001, n101, smooth.x);
    const float x11 = Lerp(n011, n111, smooth.x);
    const float y0 = Lerp(x00, x10, smooth.y);
    const float y1 = Lerp(x01, x11, smooth.y);
    return std::clamp(Lerp(y0, y1, smooth.z), 0.0f, 1.0f);
}

inline float RemapCoverage(float rawNoise, float coverage)
{
    const float safeCoverage = std::clamp(coverage, 0.0f, 1.0f);
    if (!(safeCoverage > 1e-4f))
        return 0.0f;
    const float threshold = 1.0f - safeCoverage;
    return std::clamp((rawNoise - threshold) / safeCoverage, 0.0f, 1.0f);
}

inline DensitySample SampleCloudDensity(const Float3& worldPosition,
                                        float timeSeconds,
                                        float baseNoiseScale,
                                        float coverage,
                                        float densityMultiplier,
                                        const Float3& windDirection,
                                        float windSpeed,
                                        float noiseOffset)
{
    DensitySample sample;
    const Float3 safeWind = NormalizeOrZero(windDirection);
    const Float3 stationaryWorld = worldPosition -
        safeWind * (std::max(windSpeed, 0.0f) * std::max(timeSeconds, 0.0f));
    const float safeScale = std::max(baseNoiseScale, 1e-4f);
    sample.noiseUvw = stationaryWorld * safeScale +
        Float3{ noiseOffset, noiseOffset, noiseOffset };
    sample.rawNoise = SampleBaseNoise(sample.noiseUvw);
    sample.thresholdDensity = RemapCoverage(sample.rawNoise, coverage);
    sample.finalDensity = std::clamp(
        sample.thresholdDensity * std::max(densityMultiplier, 0.0f), 0.0f, 1.0f);
    return sample;
}
}
