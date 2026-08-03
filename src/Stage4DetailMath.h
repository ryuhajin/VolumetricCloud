#pragma once

#include "Stage2NoiseMath.h"
#include "Stage3HeightMath.h"

#include <algorithm>
#include <cmath>

namespace stage4
{
using stage2::Float3;

struct NoiseFieldSample
{
    float value = 0.0f;
    Float3 uvw = {};
};

struct DetailDensitySample
{
    float baseDensity = 0.0f;
    float detailNoise = 0.0f;
    float erosion = 0.0f;
    float finalDensity = 0.0f;
    Float3 detailNoiseUvw = {};
    bool detailSampled = false;
};

inline NoiseFieldSample SampleNoiseField(const Float3& worldPosition,
                                         float timeSeconds,
                                         float scale,
                                         const Float3& windDirection,
                                         float windSpeed,
                                         float offset)
{
    const Float3 safeWind = stage2::NormalizeOrZero(windDirection);
    const Float3 stationaryWorld = worldPosition - safeWind *
        (std::max(windSpeed, 0.0f) * std::max(timeSeconds, 0.0f));
    NoiseFieldSample result;
    result.uvw = stationaryWorld * std::max(scale, 1e-4f) +
                 Float3{ offset, offset, offset };
    result.value = stage2::SampleBaseNoise(result.uvw);
    return result;
}

inline float EvaluateBaseDensity(float rawNoise, float coverage,
                                 float heightProfile, float densityMultiplier)
{
    return stage3::ApplyHeightProfile(
        stage2::RemapCoverage(rawNoise, coverage),
        heightProfile, densityMultiplier);
}

inline DetailDensitySample ApplyDetailErosion(
    float baseDensity,
    const Float3& worldPosition,
    float timeSeconds,
    float detailScale,
    float erosionStrength,
    const Float3& windDirection,
    float detailWindSpeed,
    float detailOffset,
    bool sampleDetail)
{
    DetailDensitySample result;
    result.baseDensity = stage3::SaturateFinite(baseDensity);
    result.finalDensity = result.baseDensity;
    if (!sampleDetail || result.baseDensity <= 0.0f ||
        !std::isfinite(erosionStrength) || erosionStrength <= 0.0f)
        return result;

    const NoiseFieldSample detail = SampleNoiseField(
        worldPosition, timeSeconds, detailScale, windDirection,
        detailWindSpeed, detailOffset);
    result.detailNoise = stage3::SaturateFinite(detail.value);
    result.detailNoiseUvw = detail.uvw;
    result.erosion = result.detailNoise * std::max(erosionStrength, 0.0f);
    result.finalDensity = stage3::SaturateFinite(
        result.baseDensity - result.erosion);
    result.detailSampled = true;
    return result;
}
}
