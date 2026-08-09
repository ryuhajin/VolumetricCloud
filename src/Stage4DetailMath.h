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
    float detailLodFactor = 1.0f;
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
    bool sampleDetail,
    float detailLodFactor = 1.0f)
{
    DetailDensitySample result;
    result.baseDensity = stage3::SaturateFinite(baseDensity);
    result.finalDensity = result.baseDensity;
    result.detailLodFactor = std::clamp(
        std::isfinite(detailLodFactor) ? detailLodFactor : 0.0f, 0.0f, 1.0f);
    if (!sampleDetail || result.baseDensity <= 0.0f ||
        !std::isfinite(erosionStrength) || erosionStrength <= 0.0f)
        return result;

    float rawDetail = 0.5f;
    if (result.detailLodFactor > 0.0f)
    {
        const NoiseFieldSample detail = SampleNoiseField(
            worldPosition, timeSeconds, detailScale, windDirection,
            detailWindSpeed, detailOffset);
        rawDetail = stage3::SaturateFinite(detail.value);
        result.detailNoiseUvw = detail.uvw;
        result.detailSampled = true;
    }
    result.detailNoise = 0.5f +
        (rawDetail - 0.5f) * result.detailLodFactor;
    result.erosion = result.detailNoise * std::max(erosionStrength, 0.0f);
    result.finalDensity = stage3::SaturateFinite(
        result.baseDensity - result.erosion);
    return result;
}
}
