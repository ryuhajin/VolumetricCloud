#pragma once

#include <algorithm>
#include <cmath>

namespace stage3
{
inline float SaturateFinite(float value)
{
    if (!std::isfinite(value))
        return 0.0f;
    return std::clamp(value, 0.0f, 1.0f);
}

inline float SmoothStep(float edge0, float edge1, float value)
{
    if (!(edge1 > edge0))
        return 0.0f;
    const float t = SaturateFinite((value - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

inline float EvaluateHeightFraction(float worldY, float cloudBottom, float cloudTop)
{
    const float thickness = cloudTop - cloudBottom;
    if (!std::isfinite(worldY) || !std::isfinite(thickness) || thickness <= 1e-6f)
        return 0.0f;
    return SaturateFinite((worldY - cloudBottom) / thickness);
}

inline float EvaluateHeightProfileFromFraction(float heightFraction,
                                               float bottomFadeEnd,
                                               float topFadeStart)
{
    const float safeBottom = std::clamp(bottomFadeEnd, 0.01f, 0.99f);
    const float safeTop = std::clamp(topFadeStart, 0.01f, 0.99f);
    const float height = SaturateFinite(heightFraction);
    const float bottomFade = SmoothStep(0.0f, safeBottom, height);
    const float topFade = 1.0f - SmoothStep(safeTop, 1.0f, height);
    return SaturateFinite(bottomFade * topFade);
}

inline float EvaluateHeightProfile(float worldY, float cloudBottom, float cloudTop,
                                   float bottomFadeEnd, float topFadeStart)
{
    if (!(cloudTop - cloudBottom > 1e-6f))
        return 0.0f;
    return EvaluateHeightProfileFromFraction(
        EvaluateHeightFraction(worldY, cloudBottom, cloudTop),
        bottomFadeEnd, topFadeStart);
}

inline float ApplyHeightProfile(float thresholdDensity, float heightProfile,
                                float densityMultiplier)
{
    return SaturateFinite(std::max(thresholdDensity, 0.0f) *
                          SaturateFinite(heightProfile) *
                          std::max(densityMultiplier, 0.0f));
}
}
