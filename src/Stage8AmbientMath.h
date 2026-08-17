// ============================================================================
//  Stage8AmbientMath.h - 단계 8 환경광·다중 산란 GPU 수식의 CPU 회귀 기준
// ============================================================================
#pragma once

#include "EnvironmentParameters.h"

#include <algorithm>
#include <cmath>

namespace stage8
{
struct Weights
{
    float sky = 1.0f;
    float ground = 0.0f;
    float ambientOcclusion = 1.0f;
};

struct Color3
{
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

struct AmbientRadiance
{
    Color3 sky;
    Color3 ground;
    Weights weights;
};

inline float AmbientVisibility(float density, float lightTransmittance,
                               const EnvironmentParameters& input)
{
    const EnvironmentParameters value = stage8environment::Sanitize(input);
    const float safeDensity = std::max(
        std::isfinite(density) ? density : 0.0f, 0.0f);
    const float transmittance = std::clamp(
        std::isfinite(lightTransmittance) ? lightTransmittance : 1.0f,
        0.0f, 1.0f);
    const float localVisibility = std::exp(
        -safeDensity * value.ambientOcclusionStrength);
    const float directionalVisibility = std::pow(
        transmittance, value.ambientShadowExponent);
    const float coupledVisibility = 1.0f +
        (directionalVisibility - 1.0f) * value.ambientShadowCoupling;
    return std::clamp(localVisibility * coupledVisibility, 0.0f, 1.0f);
}

inline float MultipleScatteringInteriorWeight(
    float lightTransmittance, const EnvironmentParameters& input)
{
    const EnvironmentParameters value = stage8environment::Sanitize(input);
    const float transmittance = std::clamp(
        std::isfinite(lightTransmittance) ? lightTransmittance : 1.0f,
        0.0f, 1.0f);
    return 1.0f - transmittance * value.multipleScatteringInteriorBlend;
}

inline Weights EvaluateWeights(float heightFraction, float density,
                               const EnvironmentParameters& input)
{
    const EnvironmentParameters value = stage8environment::Sanitize(input);
    const float height = std::clamp(
        std::isfinite(heightFraction) ? heightFraction : 0.0f, 0.0f, 1.0f);
    const float safeDensity = std::max(
        std::isfinite(density) ? density : 0.0f, 0.0f);
    Weights result;
    result.sky = 1.0f + (height - 1.0f) * value.ambientHeightInfluence;
    result.ground = 1.0f - height;
    result.ambientOcclusion = std::exp(
        -safeDensity * value.ambientOcclusionStrength);
    return result;
}

inline AmbientRadiance EvaluateAmbientRadiance(
    float heightFraction, float density,
    const EnvironmentParameters& input)
{
    const EnvironmentParameters value = stage8environment::Sanitize(input);
    AmbientRadiance result;
    result.weights = EvaluateWeights(heightFraction, density, value);
    const float skyScale = value.skyStrength * result.weights.sky *
                           result.weights.ambientOcclusion;
    const float groundScale = value.groundStrength * result.weights.ground *
                              result.weights.ambientOcclusion;
    result.sky = { value.skyColor.x * skyScale,
                   value.skyColor.y * skyScale,
                   value.skyColor.z * skyScale };
    result.ground = { value.groundColor.x * groundScale,
                      value.groundColor.y * groundScale,
                      value.groundColor.z * groundScale };
    return result;
}

inline float MultipleScatteringFactor(float lightOpticalDepth,
                                      float phaseFactor,
                                      const EnvironmentParameters& input)
{
    const EnvironmentParameters value = stage8environment::Sanitize(input);
    if (value.multipleScatteringEnabled < 0.5f ||
        value.multipleScatteringOctaves == 0)
        return 0.0f;

    const float opticalDepth = std::max(
        std::isfinite(lightOpticalDepth) ? lightOpticalDepth : 0.0f, 0.0f);
    const float phase = std::clamp(
        std::isfinite(phaseFactor) ? phaseFactor : 1.0f, 0.0f, 16.0f);
    float energy = value.multipleScatteringAttenuation;
    float extinctionScale = value.multipleScatteringExtinctionFactor;
    float phaseScale = value.multipleScatteringPhaseFactor;
    float result = 0.0f;
    for (std::uint32_t octave = 0;
         octave < value.multipleScatteringOctaves; ++octave)
    {
        const float octaveLight = std::exp(-opticalDepth * extinctionScale);
        const float octavePhase = 1.0f + (phase - 1.0f) * phaseScale;
        result += energy * octaveLight * std::max(octavePhase, 0.0f);
        energy *= value.multipleScatteringAttenuation;
        extinctionScale *= value.multipleScatteringExtinctionFactor;
        phaseScale *= value.multipleScatteringPhaseFactor;
    }
    result *= MultipleScatteringInteriorWeight(
        std::exp(-opticalDepth), value);
    return std::isfinite(result) ? std::max(result, 0.0f) : 0.0f;
}
}
