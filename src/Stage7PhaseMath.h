// ============================================================================
//  Stage7PhaseMath.h - 단계 7 HG Phase Function의 CPU 회귀 기준
// ============================================================================
#pragma once

#include <algorithm>
#include <cmath>

namespace stage7
{
constexpr float kMaxPhaseFactor = 16.0f;
// 현재 swap chain은 LDR UNORM이므로 실제 조명에 적용하는 배율은 더 낮게
// 제한한다. raw HG/dual 값은 16까지 유지해 진단 곡선의 방향성은 보존한다.
constexpr float kMaxAppliedPhaseFactor = 2.5f;

struct Direction3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct PhaseSample
{
    float cosTheta = 0.0f;
    float forwardLobe = 1.0f;
    float backwardLobe = 1.0f;
    float dualLobe = 1.0f;
    float phaseFactor = 1.0f;
};

inline float HenyeyGreenstein(float cosTheta, float g)
{
    if (!std::isfinite(cosTheta) || !std::isfinite(g))
        return 1.0f;
    const float safeCosTheta = std::clamp(cosTheta, -1.0f, 1.0f);
    const float safeG = std::clamp(g, -0.95f, 0.95f);
    const float denominatorBase = std::max(
        1.0f + safeG * safeG - 2.0f * safeG * safeCosTheta, 1e-4f);
    const float value = (1.0f - safeG * safeG) /
        (denominatorBase * std::sqrt(denominatorBase));
    return std::isfinite(value) ? std::max(value, 0.0f) : 1.0f;
}

inline bool Normalize(Direction3 value, Direction3& normalized)
{
    const float lengthSquared = value.x * value.x + value.y * value.y +
                                value.z * value.z;
    if (!std::isfinite(lengthSquared) || lengthSquared <= 1e-8f)
        return false;
    const float inverseLength = 1.0f / std::sqrt(lengthSquared);
    normalized = { value.x * inverseLength, value.y * inverseLength,
                   value.z * inverseLength };
    return std::isfinite(normalized.x) && std::isfinite(normalized.y) &&
           std::isfinite(normalized.z);
}

inline PhaseSample EvaluateDualLobePhase(
    Direction3 viewRayDirection, Direction3 directionToSun,
    bool enabled, float forwardG, float backwardG,
    float blend, float intensity)
{
    PhaseSample result;
    Direction3 safeView;
    Direction3 safeSun;
    if (!Normalize(viewRayDirection, safeView) ||
        !Normalize(directionToSun, safeSun))
        return result;

    result.cosTheta = std::clamp(
        safeView.x * safeSun.x + safeView.y * safeSun.y +
        safeView.z * safeSun.z, -1.0f, 1.0f);
    result.forwardLobe = HenyeyGreenstein(
        result.cosTheta, std::clamp(
            std::isfinite(forwardG) ? forwardG : 0.65f, 0.0f, 0.95f));
    result.backwardLobe = HenyeyGreenstein(
        result.cosTheta, std::clamp(
            std::isfinite(backwardG) ? backwardG : -0.25f, -0.95f, 0.0f));
    const float safeBlend = std::clamp(
        std::isfinite(blend) ? blend : 0.80f, 0.0f, 1.0f);
    result.dualLobe = result.backwardLobe +
        (result.forwardLobe - result.backwardLobe) * safeBlend;
    const float safeIntensity = std::clamp(
        std::isfinite(intensity) ? intensity : 0.25f, 0.0f, 1.0f);
    const float boundedDual = std::clamp(
        std::isfinite(result.dualLobe) ? result.dualLobe : 1.0f,
        0.0f, kMaxPhaseFactor);
    result.phaseFactor = enabled
        ? 1.0f + (boundedDual - 1.0f) * safeIntensity
        : 1.0f;
    result.phaseFactor = std::clamp(
        result.phaseFactor, 0.0f, kMaxAppliedPhaseFactor);
    return result;
}
}
