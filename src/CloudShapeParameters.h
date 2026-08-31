// ============================================================================
//  CloudShapeParameters.h - 물리 컬럼 형상 전용 64바이트 b7 계약
// ============================================================================
#pragma once

#include <algorithm>
#include <cstddef>
#include <cmath>

struct alignas(16) CloudShapeParameters
{
    float stratusMinimumThicknessMeters = 1500.0f;
    float stratusMaximumThicknessMeters = 2500.0f;
    float cumulusMinimumThicknessMeters = 3000.0f;
    float cumulusMaximumThicknessMeters = 4600.0f;

    float stratusBottomFadeEnd = 0.06f;
    float stratusTopFadeStart = 0.65f;
    float mixedBottomFadeEnd = 0.10f;
    float mixedTopFadeStart = 0.86f;

    float cumulusBottomFadeEnd = 0.08f;
    float cumulusTopFadeStart = 0.93f;
    float cumulusUpperMassBottom = 0.65f;
    float cumulusUpperMassStart = 0.08f;

    float cumulusUpperMassEnd = 0.70f;
    float localBaseLiftMaxMeters = 200.0f;
    float footprintCoverageInfluence = 0.40f;
    float padding0 = 0.0f;
};

static_assert(sizeof(CloudShapeParameters) == 64,
              "CloudShapeParameters must match CloudShapeCB");
static_assert(offsetof(CloudShapeParameters, localBaseLiftMaxMeters) == 52,
              "CloudShapeParameters local base lift ABI changed");
static_assert(offsetof(CloudShapeParameters, footprintCoverageInfluence) == 56,
              "CloudShapeParameters footprint influence ABI changed");

inline CloudShapeParameters SanitizeCloudShapeParameters(
    const CloudShapeParameters& value)
{
    CloudShapeParameters result = value;
    const auto finiteOr = [](float input, float fallback)
    {
        return std::isfinite(input) ? input : fallback;
    };
    result.stratusMinimumThicknessMeters = std::clamp(
        finiteOr(result.stratusMinimumThicknessMeters, 1500.0f),
        1.0f, 6000.0f);
    result.stratusMaximumThicknessMeters = std::clamp(
        finiteOr(result.stratusMaximumThicknessMeters, 2500.0f),
        result.stratusMinimumThicknessMeters, 6000.0f);
    result.cumulusMinimumThicknessMeters = std::clamp(
        finiteOr(result.cumulusMinimumThicknessMeters, 3000.0f),
        result.stratusMinimumThicknessMeters, 6000.0f);
    result.cumulusMaximumThicknessMeters = std::clamp(
        finiteOr(result.cumulusMaximumThicknessMeters, 4600.0f),
        std::max(result.stratusMaximumThicknessMeters,
                 result.cumulusMinimumThicknessMeters), 6000.0f);
    result.stratusBottomFadeEnd = std::clamp(
        finiteOr(result.stratusBottomFadeEnd, 0.06f), 0.01f, 0.99f);
    result.stratusTopFadeStart = std::clamp(
        finiteOr(result.stratusTopFadeStart, 0.65f),
        result.stratusBottomFadeEnd, 0.99f);
    result.mixedBottomFadeEnd = std::clamp(
        finiteOr(result.mixedBottomFadeEnd, 0.10f), 0.01f, 0.99f);
    result.mixedTopFadeStart = std::clamp(
        finiteOr(result.mixedTopFadeStart, 0.86f),
        result.mixedBottomFadeEnd, 0.99f);
    result.cumulusBottomFadeEnd = std::clamp(
        finiteOr(result.cumulusBottomFadeEnd, 0.08f), 0.01f, 0.99f);
    result.cumulusTopFadeStart = std::clamp(
        finiteOr(result.cumulusTopFadeStart, 0.93f),
        result.cumulusBottomFadeEnd, 0.99f);
    result.cumulusUpperMassBottom = std::clamp(
        finiteOr(result.cumulusUpperMassBottom, 0.65f), 0.0f, 1.0f);
    result.cumulusUpperMassStart = std::clamp(
        finiteOr(result.cumulusUpperMassStart, 0.08f), 0.0f, 0.99f);
    result.cumulusUpperMassEnd = std::clamp(
        finiteOr(result.cumulusUpperMassEnd, 0.70f),
        result.cumulusUpperMassStart + 0.01f, 1.0f);
    result.localBaseLiftMaxMeters = std::clamp(
        finiteOr(result.localBaseLiftMaxMeters, 200.0f), 0.0f, 2000.0f);
    result.footprintCoverageInfluence = std::clamp(
        finiteOr(result.footprintCoverageInfluence, 0.40f), 0.0f, 1.0f);
    result.padding0 = 0.0f;
    return result;
}
