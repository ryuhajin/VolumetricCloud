// ============================================================================
//  Stage11TemporalMath.h - 단계 11 CPU 회귀용 jitter/reprojection/history 기준
// ============================================================================
#pragma once

#include "Stage11TemporalParameters.h"

#include <DirectXMath.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace stage11temporal
{
struct HistoryState
{
    std::uint32_t readIndex = 0u;
    std::uint32_t accumulatedFrames = 0u;
    bool valid = false;
    Stage11HistoryResetReason resetReason =
        Stage11HistoryResetReason::FirstFrame;
};

enum class CurrentSourceRejectionReason : std::uint32_t
{
    Valid = 0u,
    SceneClassMismatch = 1u,
    GeometryDepthMismatch = 2u,
    InvalidOrNoCandidate = 3u,
};

enum class InvalidCurrentResolution : std::uint32_t
{
    UseCurrent = 0u,
    HoldHistory = 1u,
    Transparent = 2u,
};

inline constexpr float kSceneSkyThresholdFraction = 0.999f;
inline constexpr float kPlaneDepthBaseTolerance = 8.0e-7f;
inline constexpr float kPlaneDepthPerPixelTolerance = 2.0e-7f;
inline constexpr float kOpaqueCloudTransmittance = 0.99f;
inline constexpr float kFastSourceDepthRelativeThreshold = 0.01f;
inline constexpr float kFastSourceDepthMinimumMeters = 1.0f;
inline constexpr float kFastSourceDepthMaximumMeters = 10.0f;

struct ScreenDepthPlaneGuide
{
    float deviceDepth = 1.0f;
    float slopeX = 0.0f;
    float slopeY = 0.0f;
    bool slopeXValid = false;
    bool slopeYValid = false;
};

inline void ResetHistoryState(HistoryState& state,
                              Stage11HistoryResetReason reason)
{
    state.readIndex = 0u;
    state.accumulatedFrames = 0u;
    state.valid = false;
    state.resetReason = reason;
}

inline void CommitHistoryState(HistoryState& state, bool temporalEnabled)
{
    state.readIndex = 1u - state.readIndex;
    state.valid = true;
    state.accumulatedFrames = temporalEnabled
        ? state.accumulatedFrames + 1u : 0u;
}

inline constexpr std::array<DirectX::XMFLOAT2, 4> kJitterPhases = {{
    { -0.25f, -0.25f },
    {  0.25f,  0.25f },
    {  0.25f, -0.25f },
    { -0.25f,  0.25f },
}};

inline DirectX::XMFLOAT2 JitterForFrame(std::uint32_t frameIndex)
{
    return kJitterPhases[frameIndex & 3u];
}

// Cloud Data texel i는 (i + 0.5 + jitter) / sourceExtent에서 계산된다.
// Full UV를 저해상도 texel index 공간으로 되돌릴 때 같은 jitter를 빼야
// HLSL SpatialReconstruct와 표본 중심이 일치한다.
inline DirectX::XMFLOAT2 ReconstructionSourcePosition(
    DirectX::XMFLOAT2 fullUv, DirectX::XMFLOAT2 sourceExtent,
    DirectX::XMFLOAT2 jitterLowResolutionTexels, bool jitterEnabled)
{
    const float width = std::max(sourceExtent.x, 1.0f);
    const float height = std::max(sourceExtent.y, 1.0f);
    const float jitterX = jitterEnabled ? jitterLowResolutionTexels.x : 0.0f;
    const float jitterY = jitterEnabled ? jitterLowResolutionTexels.y : 0.0f;
    return {
        fullUv.x * width - 0.5f - jitterX,
        fullUv.y * height - 0.5f - jitterY,
    };
}

inline bool IsFinite(float value)
{
    return std::isfinite(value);
}

inline DirectX::XMINT2 SourceGuidePixelForTap(
    DirectX::XMINT2 tapPixel, DirectX::XMINT2 lowResolutionExtent,
    DirectX::XMINT2 fullResolutionExtent,
    DirectX::XMFLOAT2 jitterLowResolutionTexels, bool jitterEnabled)
{
    const int lowWidth = std::max(lowResolutionExtent.x, 1);
    const int lowHeight = std::max(lowResolutionExtent.y, 1);
    const int fullWidth = std::max(fullResolutionExtent.x, 1);
    const int fullHeight = std::max(fullResolutionExtent.y, 1);
    const float jitterX = jitterEnabled ? jitterLowResolutionTexels.x : 0.0f;
    const float jitterY = jitterEnabled ? jitterLowResolutionTexels.y : 0.0f;
    const float uvX = std::clamp(
        (static_cast<float>(tapPixel.x) + 0.5f + jitterX) /
            static_cast<float>(lowWidth), 0.0f, 1.0f);
    const float uvY = std::clamp(
        (static_cast<float>(tapPixel.y) + 0.5f + jitterY) /
            static_cast<float>(lowHeight), 0.0f, 1.0f);
    return {
        std::clamp(static_cast<int>(uvX * fullWidth), 0, fullWidth - 1),
        std::clamp(static_cast<int>(uvY * fullHeight), 0, fullHeight - 1),
    };
}

inline bool SelectStableSlope(float centerDepth, float negativeDepth,
                              bool negativeGeometry, float positiveDepth,
                              bool positiveGeometry, float& slope)
{
    const bool negativeValid = negativeGeometry && IsFinite(negativeDepth);
    const bool positiveValid = positiveGeometry && IsFinite(positiveDepth);
    const float negativeSlope = centerDepth - negativeDepth;
    const float positiveSlope = positiveDepth - centerDepth;
    if (negativeValid && positiveValid)
    {
        slope = std::abs(negativeSlope) <= std::abs(positiveSlope)
            ? negativeSlope : positiveSlope;
        return true;
    }
    if (negativeValid)
    {
        slope = negativeSlope;
        return true;
    }
    if (positiveValid)
    {
        slope = positiveSlope;
        return true;
    }
    slope = 0.0f;
    return false;
}

inline float PlaneDepthTolerance(int deltaX, int deltaY)
{
    return kPlaneDepthBaseTolerance + kPlaneDepthPerPixelTolerance *
        static_cast<float>(std::abs(deltaX) + std::abs(deltaY));
}

inline bool FastSourceDepthAccepted(float sourceSceneLimitMeters,
                                    float targetSceneLimitMeters)
{
    if (!IsFinite(sourceSceneLimitMeters) ||
        !IsFinite(targetSceneLimitMeters) || sourceSceneLimitMeters < 0.0f ||
        targetSceneLimitMeters < 0.0f)
        return false;
    const float tolerance = std::clamp(
        targetSceneLimitMeters * kFastSourceDepthRelativeThreshold,
        kFastSourceDepthMinimumMeters, kFastSourceDepthMaximumMeters);
    return std::abs(sourceSceneLimitMeters - targetSceneLimitMeters) <=
        tolerance;
}

inline CurrentSourceRejectionReason ValidateCurrentSourcePlane(
    float sourceCloudDepthMeters, float sourceSceneLimitMeters,
    float farPlaneMeters, bool targetHasGeometry, bool sourceHasGeometry,
    const ScreenDepthPlaneGuide& targetGuide, int deltaX, int deltaY,
    float sourceDeviceDepth)
{
    if (!IsFinite(sourceCloudDepthMeters) ||
        !IsFinite(sourceSceneLimitMeters) || !IsFinite(farPlaneMeters) ||
        !IsFinite(targetGuide.deviceDepth) ||
        !IsFinite(sourceDeviceDepth) || farPlaneMeters <= 0.0f ||
        sourceCloudDepthMeters < 0.0f ||
        sourceCloudDepthMeters > farPlaneMeters ||
        sourceSceneLimitMeters < 0.0f ||
        sourceSceneLimitMeters > farPlaneMeters)
        return CurrentSourceRejectionReason::InvalidOrNoCandidate;
    if (sourceHasGeometry != targetHasGeometry)
        return CurrentSourceRejectionReason::SceneClassMismatch;
    if (!targetHasGeometry)
        return CurrentSourceRejectionReason::Valid;
    if (deltaX == 0 && deltaY == 0)
        return CurrentSourceRejectionReason::Valid;
    if ((deltaX != 0 && !targetGuide.slopeXValid) ||
        (deltaY != 0 && !targetGuide.slopeYValid))
        return CurrentSourceRejectionReason::InvalidOrNoCandidate;
    const float predictedDepth = targetGuide.deviceDepth +
        targetGuide.slopeX * static_cast<float>(deltaX) +
        targetGuide.slopeY * static_cast<float>(deltaY);
    return IsFinite(predictedDepth) &&
        std::abs(sourceDeviceDepth - predictedDepth) <=
            PlaneDepthTolerance(deltaX, deltaY)
        ? CurrentSourceRejectionReason::Valid
        : CurrentSourceRejectionReason::GeometryDepthMismatch;
}

inline bool NeighborhoodCloudDepthAccepted(
    float minimumCloudDepth, float maximumCloudDepth,
    bool cloudDepthValid, float currentTransmittance,
    float historyTransmittance, float historyCloudDepth,
    float relativeThreshold)
{
    if (!IsFinite(currentTransmittance) ||
        !IsFinite(historyTransmittance) || !IsFinite(relativeThreshold))
        return false;
    if (currentTransmittance >= kOpaqueCloudTransmittance ||
        historyTransmittance >= kOpaqueCloudTransmittance)
        return true;
    if (!cloudDepthValid || !IsFinite(minimumCloudDepth) ||
        !IsFinite(maximumCloudDepth) || !IsFinite(historyCloudDepth))
        return false;
    const float low = std::min(minimumCloudDepth, maximumCloudDepth);
    const float high = std::max(minimumCloudDepth, maximumCloudDepth);
    const float margin = std::max(relativeThreshold, 0.0f) *
        std::max(high, 1.0f);
    return historyCloudDepth >= low - margin &&
        historyCloudDepth <= high + margin;
}

template <std::size_t N>
inline bool NormalizeValidWeights(const std::array<float, N>& weights,
                                  const std::array<bool, N>& valid,
                                  std::array<float, N>& normalized)
{
    normalized.fill(0.0f);
    float sum = 0.0f;
    for (std::size_t index = 0; index < N; ++index)
    {
        if (!valid[index] || !IsFinite(weights[index]) || weights[index] < 0.0f)
            continue;
        normalized[index] = weights[index];
        sum += weights[index];
    }
    if (!IsFinite(sum) || sum <= 1.0e-6f)
    {
        normalized.fill(0.0f);
        return false;
    }
    for (float& weight : normalized)
        weight /= sum;
    return true;
}

template <std::size_t N>
inline int NearestValidCandidate(
    const DirectX::XMFLOAT2& sourcePosition,
    const std::array<DirectX::XMFLOAT2, N>& candidatePositions,
    const std::array<bool, N>& valid)
{
    int bestIndex = -1;
    float bestDistanceSquared = 1.0e30f;
    for (std::size_t index = 0; index < N; ++index)
    {
        if (!valid[index])
            continue;
        const float dx = candidatePositions[index].x - sourcePosition.x;
        const float dy = candidatePositions[index].y - sourcePosition.y;
        const float distanceSquared = dx * dx + dy * dy;
        // HLSL과 같이 strict <를 사용해 같은 거리는 먼저 순회한 후보가 이긴다.
        if (distanceSquared < bestDistanceSquared)
        {
            bestDistanceSquared = distanceSquared;
            bestIndex = static_cast<int>(index);
        }
    }
    return bestIndex;
}

inline DirectX::XMFLOAT2 JointSpatialDelta(
    const DirectX::XMFLOAT2& tapPixelIndex,
    const DirectX::XMFLOAT2& sourcePosition)
{
    return { tapPixelIndex.x - sourcePosition.x,
             tapPixelIndex.y - sourcePosition.y };
}

inline InvalidCurrentResolution ResolveInvalidCurrent(
    bool currentValid, bool acceptedHistory)
{
    if (currentValid)
        return InvalidCurrentResolution::UseCurrent;
    return acceptedHistory ? InvalidCurrentResolution::HoldHistory
                           : InvalidCurrentResolution::Transparent;
}

inline bool RelativeDifferenceAccepted(float current, float history,
                                       float threshold)
{
    if (!IsFinite(current) || !IsFinite(history) || !IsFinite(threshold))
        return false;
    const float scale = std::max({ std::abs(current), std::abs(history), 1.0f });
    return std::abs(current - history) <= std::max(threshold, 0.0f) * scale;
}

inline float NearHistoryWeight(float depthMeters, float fadeStartMeters,
                               float fadeEndMeters)
{
    if (!IsFinite(depthMeters) || !IsFinite(fadeStartMeters) ||
        !IsFinite(fadeEndMeters) || fadeEndMeters <= fadeStartMeters)
        return 0.0f;
    const float t = std::clamp((depthMeters - fadeStartMeters) /
        (fadeEndMeters - fadeStartMeters), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline float ClipHistory(float history, float neighborhoodMinimum,
                         float neighborhoodMaximum, float gamma)
{
    if (!IsFinite(history) || !IsFinite(neighborhoodMinimum) ||
        !IsFinite(neighborhoodMaximum) || !IsFinite(gamma))
        return IsFinite(neighborhoodMinimum) ? neighborhoodMinimum : 0.0f;
    const float low = std::min(neighborhoodMinimum, neighborhoodMaximum);
    const float high = std::max(neighborhoodMinimum, neighborhoodMaximum);
    const float center = 0.5f * (low + high);
    const float halfRange = 0.5f * (high - low) * std::max(gamma, 0.0f);
    return std::clamp(history, center - halfRange, center + halfRange);
}

inline float Accumulate(float current, float history, float historyWeight)
{
    if (!IsFinite(current))
        current = 0.0f;
    if (!IsFinite(history))
        history = current;
    const float weight = IsFinite(historyWeight)
        ? std::clamp(historyWeight, 0.0f, 1.0f) : 0.0f;
    return current + (history - current) * weight;
}

inline bool ReprojectWorldToUv(
    const DirectX::XMFLOAT3& currentWorld,
    const DirectX::XMFLOAT3& normalizedWindDirection,
    float windSpeedMetersPerSecond, float deltaTimeSeconds,
    const DirectX::XMFLOAT4X4& previousViewProjection,
    DirectX::XMFLOAT2& previousUv)
{
    using namespace DirectX;
    if (!IsFinite(currentWorld.x) || !IsFinite(currentWorld.y) ||
        !IsFinite(currentWorld.z) || !IsFinite(windSpeedMetersPerSecond) ||
        !IsFinite(deltaTimeSeconds))
        return false;
    const XMVECTOR world = XMLoadFloat3(&currentWorld) -
        XMLoadFloat3(&normalizedWindDirection) *
        std::max(windSpeedMetersPerSecond, 0.0f) *
        std::max(deltaTimeSeconds, 0.0f);
    const XMVECTOR clip = XMVector4Transform(
        XMVectorSetW(world, 1.0f), XMLoadFloat4x4(&previousViewProjection));
    XMFLOAT4 clipValue{};
    XMStoreFloat4(&clipValue, clip);
    if (!IsFinite(clipValue.x) || !IsFinite(clipValue.y) ||
        !IsFinite(clipValue.w) || clipValue.w <= 1.0e-6f)
        return false;
    const float inverseW = 1.0f / clipValue.w;
    previousUv = { clipValue.x * inverseW * 0.5f + 0.5f,
                   0.5f - clipValue.y * inverseW * 0.5f };
    return IsFinite(previousUv.x) && IsFinite(previousUv.y) &&
        previousUv.x >= 0.0f && previousUv.x <= 1.0f &&
        previousUv.y >= 0.0f && previousUv.y <= 1.0f;
}
}
