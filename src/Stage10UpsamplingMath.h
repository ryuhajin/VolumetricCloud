// ============================================================================
//  Stage10UpsamplingMath.h - 단계 10 CPU 회귀용 크기·깊이·가중치 기준
// ============================================================================
#pragma once

#include "Stage10UpsamplingParameters.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
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

enum class JointHardRejectionReason : std::uint32_t
{
    Accepted = 0,
    SceneClassMismatch = 1,
    GeometryPlaneMismatch = 2,
    InvalidSource = 3,
};

inline JointHardRejectionReason ClassifyJointHardTap(
    bool sourceValuesFinite, bool targetHasGeometry, bool sourceHasGeometry,
    float sourceDeviceDepth, float predictedDeviceDepth, float tolerance)
{
    if (!sourceValuesFinite)
        return JointHardRejectionReason::InvalidSource;
    if (targetHasGeometry != sourceHasGeometry)
        return JointHardRejectionReason::SceneClassMismatch;
    // Sky/Sky는 scene plane을 hard 조건으로 쓰지 않는다. 이후 Cloud Depth/T
    // neighborhood가 soft weight를 결정한다.
    if (!targetHasGeometry)
        return JointHardRejectionReason::Accepted;
    if (!std::isfinite(sourceDeviceDepth) ||
        !std::isfinite(predictedDeviceDepth) || !std::isfinite(tolerance) ||
        tolerance < 0.0f)
        return JointHardRejectionReason::InvalidSource;
    return std::abs(sourceDeviceDepth - predictedDeviceDepth) <= tolerance
        ? JointHardRejectionReason::Accepted
        : JointHardRejectionReason::GeometryPlaneMismatch;
}

inline std::uint32_t CountJoint4HardAccepted(
    const std::array<JointHardRejectionReason, 4>& reasons)
{
    return static_cast<std::uint32_t>(std::count(
        reasons.begin(), reasons.end(), JointHardRejectionReason::Accepted));
}

enum class JointFallbackMode : std::uint32_t
{
    WeightedResult,
    NearestHardValid,
    Transparent,
};

inline JointFallbackMode SelectJointFallback(
    std::uint32_t hardAcceptedTapCount, float weightSum, float minimumWeight)
{
    if (hardAcceptedTapCount == 0u)
        return JointFallbackMode::Transparent;
    return std::isfinite(weightSum) && std::isfinite(minimumWeight) &&
        weightSum >= std::max(minimumWeight, 0.0f)
        ? JointFallbackMode::WeightedResult
        : JointFallbackMode::NearestHardValid;
}
}
