// ============================================================================
//  Stage10UpsamplingMath.h - 단계 10 CPU 회귀용 크기·깊이·가중치 기준
// ============================================================================
#pragma once

#include "Stage10UpsamplingParameters.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace stage10upsampling
{
inline float PixelCenterUv(int pixel, int extent)
{
    return (static_cast<float>(pixel) + 0.5f) /
        static_cast<float>(std::max(extent, 1));
}

inline float OpacityWeightedDepth(float moment, float opacityWeight,
                                  float sceneLimit)
{
    if (!std::isfinite(moment) || !std::isfinite(opacityWeight) ||
        !std::isfinite(sceneLimit) || opacityWeight <= 1.0e-6f)
        return std::isfinite(sceneLimit) ? std::max(sceneLimit, 0.0f) : 0.0f;
    return std::clamp(moment / opacityWeight, 0.0f,
                      std::max(sceneLimit, 0.0f));
}

inline float RelativeDepthWeight(float reference, float candidate,
                                 float relativeSigma)
{
    const float scale = std::max(
        std::max(std::abs(reference), std::abs(candidate)), 1.0f);
    const float sigma = std::max(relativeSigma * scale, 1.0e-6f);
    const float delta = std::abs(reference - candidate) / sigma;
    return std::exp(-0.5f * delta * delta);
}

inline float TransmittanceWeight(float reference, float candidate,
                                 float sigma)
{
    const float safeSigma = std::max(sigma, 1.0e-6f);
    const float delta = std::abs(reference - candidate) / safeSigma;
    return std::exp(-0.5f * delta * delta);
}

inline std::pair<float, bool> NormalizeWeight(float weightedValue,
                                               float weightSum,
                                               float minimumWeight)
{
    const bool accepted = std::isfinite(weightedValue) &&
        std::isfinite(weightSum) && weightSum >= minimumWeight;
    return { accepted ? weightedValue / weightSum : 0.0f, accepted };
}
}
