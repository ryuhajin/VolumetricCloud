// ============================================================================
//  Stage6LightMath.h - 단계 6 GPU 조명 수식의 CPU 회귀 기준
// ============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace stage6
{
struct LightMarchResult
{
    float transmittance = 1.0f;
    float opticalDepth = 0.0f;
    std::uint32_t stepCount = 0;
};

inline LightMarchResult MarchConstantDensity(float pathLength, float density,
                                             float extinction, float targetStep,
                                             std::uint32_t maxSteps)
{
    LightMarchResult result;
    if (!std::isfinite(pathLength) || !std::isfinite(density) ||
        !std::isfinite(extinction) || pathLength <= 0.0f)
        return result;
    const float safeStep = std::max(
        std::isfinite(targetStep) ? targetStep : 0.0f, 1e-4f);
    const std::uint32_t safeMaxSteps = std::max(maxSteps, 1u);
    result.stepCount = std::min(
        safeMaxSteps, static_cast<std::uint32_t>(std::ceil(pathLength / safeStep)));
    const float actualStep = pathLength / static_cast<float>(result.stepCount);
    for (std::uint32_t index = 0; index < result.stepCount; ++index)
        result.opticalDepth += std::max(density, 0.0f) *
                               std::max(extinction, 0.0f) * actualStep;
    result.transmittance = std::clamp(std::exp(-result.opticalDepth), 0.0f, 1.0f);
    return result;
}

inline float SelectLightRayDensity(float baseDensity, float /*finalDensity*/)
{
    return std::max(baseDensity, 0.0f);
}

inline bool ShouldTraceLightRay(float finalDensity)
{
    return std::isfinite(finalDensity) && finalDensity > 0.0f;
}

inline float IntegrateSingleScattering(float density, float lightTransmittance,
                                       float viewTransmittance, float stepLength,
                                       float extinction, float sunIntensity,
                                       float singleScatteringAlbedo)
{
    const float safeDensity = std::max(
        std::isfinite(density) ? density : 0.0f, 0.0f);
    const float safeLength = std::max(
        std::isfinite(stepLength) ? stepLength : 0.0f, 0.0f);
    const float safeExtinction = std::max(
        std::isfinite(extinction) ? extinction : 0.0f, 0.0f);
    const float safeViewTransmittance = std::clamp(
        std::isfinite(viewTransmittance) ? viewTransmittance : 0.0f,
        0.0f, 1.0f);
    const float safeLightTransmittance = std::clamp(
        std::isfinite(lightTransmittance) ? lightTransmittance : 0.0f,
        0.0f, 1.0f);
    const float safeSunIntensity = std::max(
        std::isfinite(sunIntensity) ? sunIntensity : 0.0f, 0.0f);
    const float safeAlbedo = std::clamp(
        std::isfinite(singleScatteringAlbedo)
            ? singleScatteringAlbedo : 0.0f,
        0.0f, 1.0f);
    const float stepTransmittance = std::exp(
        -safeDensity * safeExtinction * safeLength);
    const float stepAlpha = 1.0f - stepTransmittance;
    return safeViewTransmittance * safeLightTransmittance *
           safeSunIntensity * safeAlbedo *
           std::max(stepAlpha, 0.0f);
}
}
