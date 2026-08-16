// ============================================================================
//  Stage13OpticsLightingMath.h - 단계 13-5 km 광학·조명·LOD CPU 기준
// ============================================================================
#pragma once

#include "CloudLodParameters.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace stage13optics
{
constexpr double kOpenWorldLightTraceMeters = 20000.0;
constexpr double kReferenceLightStepMeters = 62.5;
constexpr std::uint32_t kReferenceLightSteps = 320u;
constexpr double kQualityLightStepMeters = 125.0;
constexpr std::uint32_t kQualityLightSteps = 160u;
constexpr double kBaselineLightStepMeters = 250.0;
constexpr std::uint32_t kBaselineLightSteps = 80u;
constexpr double kSmallestBaseWavelengthMeters = 12000.0 / 23.0;
constexpr double kSmallestDetailWavelengthMeters = 2000.0 / 5.0;
constexpr double kDisplayShoulderKnee = 0.80;

// LDR UNORM 출력에서 1을 넘는 선형 조명값을 하얗게 잘라 버리지 않도록
// 밝은 영역만 [knee, 1)로 부드럽게 압축한다. knee 이하는 정확히 보존한다.
inline double DisplayShoulderPeak(double peak)
{
    const double safePeak = std::isfinite(peak) ? std::max(peak, 0.0) : 0.0;
    if (safePeak <= kDisplayShoulderKnee)
        return safePeak;
    const double width = 1.0 - kDisplayShoulderKnee;
    const double excess = safePeak - kDisplayShoulderKnee;
    return kDisplayShoulderKnee + excess * width / (excess + width);
}

inline std::uint32_t RequiredSteps(double pathLengthMeters,
                                   double targetStepMeters,
                                   std::uint32_t maximumSteps)
{
    if (!std::isfinite(pathLengthMeters) ||
        !std::isfinite(targetStepMeters) || pathLengthMeters <= 0.0 ||
        targetStepMeters <= 0.0 || maximumSteps == 0u)
        return 0u;
    return std::min(maximumSteps, static_cast<std::uint32_t>(
        std::ceil(pathLengthMeters / targetStepMeters)));
}

inline double ActualStepLength(double pathLengthMeters,
                               std::uint32_t stepCount)
{
    return std::isfinite(pathLengthMeters) && pathLengthMeters > 0.0 &&
        stepCount > 0u ? pathLengthMeters / static_cast<double>(stepCount) : 0.0;
}

inline double UniformOpticalDepth(double density, double extinctionPerMeter,
                                  double pathLengthMeters)
{
    if (!std::isfinite(density) || !std::isfinite(extinctionPerMeter) ||
        !std::isfinite(pathLengthMeters))
        return 0.0;
    return std::max(density, 0.0) * std::max(extinctionPerMeter, 0.0) *
        std::max(pathLengthMeters, 0.0);
}

inline double IntegrateOpticalDepth(const std::vector<double>& densities,
                                    const std::vector<double>& lengthsMeters,
                                    double extinctionPerMeter)
{
    if (densities.size() != lengthsMeters.size() ||
        !std::isfinite(extinctionPerMeter))
        return 0.0;
    double result = 0.0;
    const double extinction = std::max(extinctionPerMeter, 0.0);
    for (std::size_t index = 0; index < densities.size(); ++index)
    {
        const double density = std::isfinite(densities[index])
            ? std::max(densities[index], 0.0) : 0.0;
        const double length = std::isfinite(lengthsMeters[index])
            ? std::max(lengthsMeters[index], 0.0) : 0.0;
        result += density * extinction * length;
    }
    return std::isfinite(result) ? std::max(result, 0.0) : 0.0;
}

inline double TransmittanceFromOpticalDepth(double opticalDepth)
{
    const double safeDepth = std::isfinite(opticalDepth)
        ? std::max(opticalDepth, 0.0) : 0.0;
    return std::clamp(std::exp(-safeDepth), 0.0, 1.0);
}

inline double SamplesPerWavelength(double wavelengthMeters,
                                   double stepMeters)
{
    return std::isfinite(wavelengthMeters) && wavelengthMeters > 0.0 &&
        std::isfinite(stepMeters) && stepMeters > 0.0
        ? wavelengthMeters / stepMeters : 0.0;
}

inline double Smoothstep(double edge0, double edge1, double value)
{
    if (!std::isfinite(edge0) || !std::isfinite(edge1) ||
        !std::isfinite(value) || edge1 <= edge0)
        return value <= edge0 ? 0.0 : 1.0;
    const double x = std::clamp((value - edge0) / (edge1 - edge0), 0.0, 1.0);
    return x * x * (3.0 - 2.0 * x);
}

inline double DetailLodFactor(double distanceMeters,
                              const CloudLodParameters& input)
{
    const CloudLodParameters value = stage13lod::Sanitize(input);
    if (value.detailLodEnabled == 0u)
        return 1.0;
    return 1.0 - Smoothstep(value.detailLodStartMeters,
                            value.detailLodEndMeters, distanceMeters);
}

inline double FilterDetail(double sampledDetail, double lodFactor,
                           double neutralValue)
{
    const double detail = std::clamp(
        std::isfinite(sampledDetail) ? sampledDetail : neutralValue, 0.0, 1.0);
    const double lod = std::clamp(
        std::isfinite(lodFactor) ? lodFactor : 0.0, 0.0, 1.0);
    const double neutral = std::clamp(
        std::isfinite(neutralValue) ? neutralValue : 0.5, 0.0, 1.0);
    return neutral + (detail - neutral) * lod;
}

inline bool ShouldSampleDetail(double distanceMeters, double baseDensity,
                               double erosionStrength,
                               const CloudLodParameters& input)
{
    const CloudLodParameters value = stage13lod::Sanitize(input);
    const bool insideLod = value.detailLodEnabled == 0u ||
        (std::isfinite(distanceMeters) &&
         distanceMeters < value.detailLodEndMeters);
    return insideLod && std::isfinite(baseDensity) && baseDensity > 0.0 &&
        std::isfinite(erosionStrength) && erosionStrength > 0.0;
}

inline double WeightedDetailMean(const std::vector<std::uint8_t>& rgba,
                                 const std::array<double, 4>& weights)
{
    if (rgba.empty() || rgba.size() % 4u != 0u)
        return 0.5;
    std::array<double, 4> safeWeights = {};
    for (std::size_t channel = 0; channel < safeWeights.size(); ++channel)
        safeWeights[channel] = std::isfinite(weights[channel])
            ? std::max(weights[channel], 0.0) : 0.0;
    double total = 0.0;
    for (std::size_t texel = 0; texel < rgba.size(); texel += 4u)
    {
        double value = 0.0;
        for (std::size_t channel = 0; channel < 4u; ++channel)
            value += (static_cast<double>(rgba[texel + channel]) / 255.0) *
                     safeWeights[channel];
        total += std::clamp(value, 0.0, 1.0);
    }
    return std::clamp(total / static_cast<double>(rgba.size() / 4u), 0.0, 1.0);
}
}
