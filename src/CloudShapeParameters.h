// ============================================================================
//  CloudShapeParameters.h - 단계 13-4B Weather 기반 물리 두께와 세로 프로파일 b7
// ============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

enum class CloudShapeMode : std::uint32_t
{
    LegacyNormalizedLayer = 0,
    WeatherPhysicalThickness = 1,
};

struct alignas(16) CloudShapeParameters
{
    std::uint32_t shapeMode =
        static_cast<std::uint32_t>(CloudShapeMode::LegacyNormalizedLayer);
    float stratusMinimumThicknessMeters = 1000.0f;
    float stratusMaximumThicknessMeters = 2000.0f;
    float cumulusMinimumThicknessMeters = 2000.0f;

    float cumulusMaximumThicknessMeters = 6000.0f;
    float stratusBottomFadeEnd = 0.08f;
    float stratusTopFadeStart = 0.55f;
    float mixedBottomFadeEnd = 0.12f;

    float mixedTopFadeStart = 0.78f;
    float cumulusBottomFadeEnd = 0.10f;
    float cumulusTopFadeStart = 0.88f;
    float cumulusUpperMassBottom = 0.55f;

    float cumulusUpperMassStart = 0.10f;
    float cumulusUpperMassEnd = 0.65f;
    float padding0 = 0.0f;
    float padding1 = 0.0f;
};

static_assert(sizeof(CloudShapeParameters) == 64,
              "CloudShapeParameters must match CloudShapeCB");

inline CloudShapeParameters SanitizeCloudShapeParameters(
    const CloudShapeParameters& value)
{
    CloudShapeParameters result = value;
    const auto finiteOr = [](float input, float fallback)
    {
        return std::isfinite(input) ? input : fallback;
    };
    result.shapeMode = result.shapeMode ==
        static_cast<std::uint32_t>(CloudShapeMode::WeatherPhysicalThickness)
        ? result.shapeMode
        : static_cast<std::uint32_t>(CloudShapeMode::LegacyNormalizedLayer);
    result.stratusMinimumThicknessMeters = std::clamp(
        finiteOr(result.stratusMinimumThicknessMeters, 1000.0f), 1.0f, 6000.0f);
    result.stratusMaximumThicknessMeters = std::clamp(
        finiteOr(result.stratusMaximumThicknessMeters, 2000.0f),
        result.stratusMinimumThicknessMeters, 6000.0f);
    result.cumulusMinimumThicknessMeters = std::clamp(
        finiteOr(result.cumulusMinimumThicknessMeters, 2000.0f),
        result.stratusMinimumThicknessMeters, 6000.0f);
    result.cumulusMaximumThicknessMeters = std::clamp(
        finiteOr(result.cumulusMaximumThicknessMeters, 6000.0f),
        std::max(result.stratusMaximumThicknessMeters,
                 result.cumulusMinimumThicknessMeters), 6000.0f);
    result.stratusBottomFadeEnd = std::clamp(
        finiteOr(result.stratusBottomFadeEnd, 0.08f), 0.01f, 0.99f);
    result.stratusTopFadeStart = std::clamp(
        finiteOr(result.stratusTopFadeStart, 0.55f),
        result.stratusBottomFadeEnd, 0.99f);
    result.mixedBottomFadeEnd = std::clamp(
        finiteOr(result.mixedBottomFadeEnd, 0.12f), 0.01f, 0.99f);
    result.mixedTopFadeStart = std::clamp(
        finiteOr(result.mixedTopFadeStart, 0.78f),
        result.mixedBottomFadeEnd, 0.99f);
    result.cumulusBottomFadeEnd = std::clamp(
        finiteOr(result.cumulusBottomFadeEnd, 0.10f), 0.01f, 0.99f);
    result.cumulusTopFadeStart = std::clamp(
        finiteOr(result.cumulusTopFadeStart, 0.88f),
        result.cumulusBottomFadeEnd, 0.99f);
    result.cumulusUpperMassBottom = std::clamp(
        finiteOr(result.cumulusUpperMassBottom, 0.55f), 0.0f, 1.0f);
    result.cumulusUpperMassStart = std::clamp(
        finiteOr(result.cumulusUpperMassStart, 0.10f), 0.0f, 0.99f);
    result.cumulusUpperMassEnd = std::clamp(
        finiteOr(result.cumulusUpperMassEnd, 0.65f),
        result.cumulusUpperMassStart + 0.01f, 1.0f);
    result.padding0 = 0.0f;
    result.padding1 = 0.0f;
    return result;
}
