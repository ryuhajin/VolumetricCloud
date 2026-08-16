#pragma once

#include "Stage2NoiseMath.h"
#include "Stage3HeightMath.h"

#include <algorithm>
#include <cmath>

namespace stage5
{
using stage2::Float3;

struct Float2
{
    float x = 0.0f;
    float y = 0.0f;
};

inline float SaturateFinite(float value)
{
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}

inline float FracFinite(float value)
{
    if (!std::isfinite(value))
        return 0.0f;
    return value - std::floor(value);
}

inline Float2 ComputeWeatherUv(const Float3& worldPosition, float timeSeconds,
                               float worldSize, const Float3& windDirection,
                               float windSpeed, const Float2& offset)
{
    const float safeWorldSize = std::isfinite(worldSize) && worldSize > 1e-4f
        ? worldSize : 1e-4f;
    const float windLength = std::sqrt(
        windDirection.x * windDirection.x + windDirection.z * windDirection.z);
    const float windX = std::isfinite(windLength) && windLength > 1e-6f
        ? windDirection.x / windLength : 0.0f;
    const float windZ = std::isfinite(windLength) && windLength > 1e-6f
        ? windDirection.z / windLength : 0.0f;
    const float safeTime = std::isfinite(timeSeconds)
        ? std::max(timeSeconds, 0.0f) : 0.0f;
    const float safeSpeed = std::isfinite(windSpeed)
        ? std::max(windSpeed, 0.0f) : 0.0f;
    const float offsetX = std::isfinite(offset.x) ? offset.x : 0.0f;
    const float offsetY = std::isfinite(offset.y) ? offset.y : 0.0f;
    return {
        FracFinite((worldPosition.x - windX * safeSpeed * safeTime) /
            safeWorldSize + offsetX),
        FracFinite((worldPosition.z - windZ * safeSpeed * safeTime) /
            safeWorldSize + offsetY),
    };
}

inline float DecodeCanonicalWeatherChannel(float value)
{
    value = SaturateFinite(value);
    if (std::abs(value - 0.5f) <= (1.0f / 255.0f + 1e-6f))
        return 0.5f;
    if (value <= 0.5f / 255.0f)
        return 0.0f;
    if (value >= 254.5f / 255.0f)
        return 1.0f;
    return value;
}

inline float RemapCoverage(float rawNoise, float coverage)
{
    const float safeCoverage = SaturateFinite(coverage);
    if (safeCoverage <= 1e-4f)
        return 0.0f;
    return SaturateFinite(
        (SaturateFinite(rawNoise) - (1.0f - safeCoverage)) / safeCoverage);
}

inline float Smoothstep(float edge0, float edge1, float value)
{
    const float t = SaturateFinite((value - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

inline float EvaluateStratusProfile(float heightFraction)
{
    const float h = SaturateFinite(heightFraction);
    return SaturateFinite(Smoothstep(0.0f, 0.08f, h) *
        (1.0f - Smoothstep(0.38f, 0.55f, h)));
}

inline float EvaluateCumulusProfile(float heightFraction,
                                    float bottomFadeEnd, float topFadeStart)
{
    const float h = SaturateFinite(heightFraction);
    const float bottom = std::max(
        std::clamp(bottomFadeEnd, 0.01f, 0.99f), 0.12f);
    const float top = std::max(
        std::clamp(topFadeStart, 0.01f, 0.99f), 0.88f);
    const float envelope = Smoothstep(0.0f, bottom, h) *
        (1.0f - Smoothstep(top, 1.0f, h));
    const float upperMass = 0.55f + 0.45f * Smoothstep(0.10f, 0.65f, h);
    return SaturateFinite(envelope * upperMass);
}

inline float EvaluateTypedHeightProfile(float heightFraction, float cloudType,
                                        float bottomFadeEnd, float topFadeStart)
{
    const float type = SaturateFinite(cloudType);
    const float mixed = stage3::EvaluateHeightProfileFromFraction(
        heightFraction, bottomFadeEnd, topFadeStart);
    const float stratus = EvaluateStratusProfile(heightFraction);
    const float cumulus = EvaluateCumulusProfile(
        heightFraction, bottomFadeEnd, topFadeStart);
    return type <= 0.5f
        ? stratus + (mixed - stratus) * (type * 2.0f)
        : mixed + (cumulus - mixed) * ((type - 0.5f) * 2.0f);
}

inline float EvaluateLocalTopFraction(float localHeightPotential,
                                      float cloudType,
                                      float minimumThicknessFraction,
                                      float heightVariation,
                                      float cumulusTopBoost)
{
    const float minimumThickness = std::clamp(
        std::isfinite(minimumThicknessFraction) ? minimumThicknessFraction : 0.40f,
        0.10f, 1.0f);
    const float rawTop = std::max(
        minimumThickness, SaturateFinite(localHeightPotential));
    const float cumulusAmount = Smoothstep(
        0.5f, 1.0f, SaturateFinite(cloudType));
    const float typedTop = rawTop + (1.0f - rawTop) *
        SaturateFinite(cumulusTopBoost) * cumulusAmount;
    return 1.0f + (typedTop - 1.0f) * SaturateFinite(heightVariation);
}

inline float EvaluateLocalHeightFraction(float globalHeightFraction,
                                         float localTopFraction)
{
    return SaturateFinite(globalHeightFraction) /
        std::max(SaturateFinite(localTopFraction), 1e-4f);
}

inline float EvaluateMixedFootprintCutoff(float heightFraction)
{
    const float h = SaturateFinite(heightFraction);
    const float lower = 0.22f + (0.04f - 0.22f) *
        Smoothstep(0.0f, 0.35f, h);
    return lower + (0.38f - lower) * Smoothstep(0.65f, 1.0f, h);
}

inline float EvaluateCumulusFootprintCutoff(float heightFraction)
{
    const float h = SaturateFinite(heightFraction);
    const float lower = 0.32f + (0.03f - 0.32f) *
        Smoothstep(0.0f, 0.38f, h);
    return lower + (0.62f - lower) * Smoothstep(0.62f, 1.0f, h);
}

inline float EvaluateTypedFootprintCutoff(float heightFraction, float cloudType)
{
    const float type = SaturateFinite(cloudType);
    const float stratus = 0.10f;
    const float mixed = EvaluateMixedFootprintCutoff(heightFraction);
    const float cumulus = EvaluateCumulusFootprintCutoff(heightFraction);
    return type <= 0.5f
        ? stratus + (mixed - stratus) * (type * 2.0f)
        : mixed + (cumulus - mixed) * ((type - 0.5f) * 2.0f);
}

// Weather R의 부드러운 가장자리를 높이별로 안쪽으로 밀어 수평 폭을 바꾼다.
// R=1은 cutoff와 무관하게 정확히 1이라 Uniform Legacy 회귀를 보존한다.
inline float EvaluateTypedWeatherCoverage(float weatherCoverage,
                                          float heightFraction,
                                          float cloudType)
{
    const float coverage = SaturateFinite(weatherCoverage);
    const float cutoff = EvaluateTypedFootprintCutoff(
        heightFraction, cloudType);
    return SaturateFinite((coverage - cutoff) / std::max(1.0f - cutoff, 1e-4f));
}

inline float EvaluateWeatherBaseDensity(float rawNoise, float globalCoverage,
                                        float weatherCoverage, float cloudType,
                                        float weatherBlue, float heightFraction,
                                        float bottomFadeEnd, float topFadeStart,
                                        float densityMultiplier,
                                        float localHeightPotential = 1.0f,
                                        float minimumThicknessFraction = 0.40f,
                                        float heightVariation = 0.0f,
                                        float cumulusTopBoost = 0.35f)
{
    const float localTop = EvaluateLocalTopFraction(
        localHeightPotential, cloudType, minimumThicknessFraction,
        heightVariation, cumulusTopBoost);
    if (SaturateFinite(heightFraction) > localTop)
        return 0.0f;
    const float localHeight = EvaluateLocalHeightFraction(
        heightFraction, localTop);
    const float shapedCoverage = EvaluateTypedWeatherCoverage(
        weatherCoverage, localHeight, cloudType);
    const float effectiveCoverage = SaturateFinite(globalCoverage) *
        shapedCoverage;
    const float threshold = RemapCoverage(rawNoise, effectiveCoverage);
    const float profile = EvaluateTypedHeightProfile(
        localHeight, cloudType, bottomFadeEnd, topFadeStart);
    const float weatherDensity = 0.5f + DecodeCanonicalWeatherChannel(weatherBlue);
    return SaturateFinite(threshold * profile *
        std::max(densityMultiplier, 0.0f) * weatherDensity);
}
}
