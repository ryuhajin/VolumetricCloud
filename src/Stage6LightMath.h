// ============================================================================
//  Stage6LightMath.h - 단계 6 GPU 조명 수식의 CPU 회귀 기준
// ============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

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
                                             std::uint32_t maxSteps,
                                             float earlyExitTransmittance = 0.0f)
{
    LightMarchResult result;
    if (!std::isfinite(pathLength) || !std::isfinite(density) ||
        !std::isfinite(extinction) || pathLength <= 0.0f)
        return result;
    const float safeStep = std::max(
        std::isfinite(targetStep) ? targetStep : 0.0f, 1e-4f);
    const std::uint32_t safeMaxSteps = std::max(maxSteps, 1u);
    const std::uint32_t plannedStepCount = std::min(
        safeMaxSteps, static_cast<std::uint32_t>(std::ceil(pathLength / safeStep)));
    const float actualStep = pathLength / static_cast<float>(plannedStepCount);
    const bool earlyExitEnabled = std::isfinite(earlyExitTransmittance) &&
                                  earlyExitTransmittance > 0.0f &&
                                  earlyExitTransmittance < 1.0f;
    const float earlyExitOpticalDepth = earlyExitEnabled
        ? -std::log(earlyExitTransmittance)
        : std::numeric_limits<float>::infinity();
    for (std::uint32_t index = 0; index < plannedStepCount; ++index)
    {
        result.opticalDepth += std::max(density, 0.0f) *
                               std::max(extinction, 0.0f) * actualStep;
        result.stepCount = index + 1u;
        if (result.opticalDepth >= earlyExitOpticalDepth)
            break;
    }
    result.transmittance = std::clamp(std::exp(-result.opticalDepth), 0.0f, 1.0f);
    return result;
}

inline float SelectLightRayDensity(float baseDensity, float /*finalDensity*/)
{
    return std::max(baseDensity, 0.0f);
}

inline float ShapeLightTransmittance(float lightTransmittance,
                                     float shadowExponent)
{
    const float transmittance = std::clamp(
        std::isfinite(lightTransmittance) ? lightTransmittance : 1.0f,
        0.0f, 1.0f);
    const float exponent = std::clamp(
        std::isfinite(shadowExponent) ? shadowExponent : 1.0f,
        0.5f, 4.0f);
    return std::pow(transmittance, exponent);
}

inline float ComputeSurfaceExposure(float lightTransmittance,
                                    float opticalDepthScale)
{
    const float transmittance = std::clamp(
        std::isfinite(lightTransmittance) ? lightTransmittance : 1.0f,
        0.0f, 1.0f);
    const float scale = std::clamp(
        std::isfinite(opticalDepthScale) ? opticalDepthScale : 1.0f,
        0.25f, 8.0f);
    return std::pow(transmittance, scale);
}

inline float ScopePhaseToSurface(float phaseFactor, float surfaceExposure,
                                 float edgeInfluence)
{
    const float phase = std::clamp(
        std::isfinite(phaseFactor) ? phaseFactor : 1.0f, 0.0f, 16.0f);
    const float exposure = std::clamp(
        std::isfinite(surfaceExposure) ? surfaceExposure : 1.0f,
        0.0f, 1.0f);
    const float influence = std::clamp(
        std::isfinite(edgeInfluence) ? edgeInfluence : 0.0f,
        0.0f, 1.0f);
    const float weight = 1.0f + (exposure - 1.0f) * influence;
    return 1.0f + (phase - 1.0f) * weight;
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
    const float safeDensity = std::max(density, 0.0f);
    const float safeLength = std::max(stepLength, 0.0f);
    const float safeExtinction = std::max(extinction, 0.0f);
    const float stepTransmittance = std::exp(
        -safeDensity * safeExtinction * safeLength);
    const float interactionFraction = std::clamp(
        1.0f - stepTransmittance, 0.0f, 1.0f);
    return std::clamp(viewTransmittance, 0.0f, 1.0f) *
           std::clamp(lightTransmittance, 0.0f, 1.0f) *
           std::max(sunIntensity, 0.0f) *
           std::clamp(singleScatteringAlbedo, 0.0f, 1.0f) *
           interactionFraction;
}
}
