// ============================================================================
//  CloudShapeParameters.h - 단계 13-4B Weather 기반 물리 두께와 세로 프로파일 b7
// ============================================================================
#pragma once

#include <DirectXMath.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

enum class CloudShapeMode : std::uint32_t
{
    LegacyNormalizedLayer = 0,
    WeatherPhysicalThickness = 1,
    CirrusPhysicalLayer = 2,
};

namespace cloudshape
{
inline CloudShapeMode Mode(std::uint32_t value)
{
    if (value > static_cast<std::uint32_t>(
            CloudShapeMode::CirrusPhysicalLayer))
        return CloudShapeMode::LegacyNormalizedLayer;
    return static_cast<CloudShapeMode>(value);
}

inline const char* ModeName(std::uint32_t value)
{
    switch (Mode(value))
    {
    case CloudShapeMode::WeatherPhysicalThickness:
        return "weatherPhysicalThickness";
    case CloudShapeMode::CirrusPhysicalLayer:
        return "cirrusPhysicalLayer";
    case CloudShapeMode::LegacyNormalizedLayer:
    default:
        return "legacyNormalizedLayer";
    }
}

inline const char* ModeDisplayName(std::uint32_t value)
{
    switch (Mode(value))
    {
    case CloudShapeMode::WeatherPhysicalThickness:
        return "Weather Physical Thickness";
    case CloudShapeMode::CirrusPhysicalLayer:
        return "Cirrus Physical Layer";
    case CloudShapeMode::LegacyNormalizedLayer:
    default:
        return "Legacy Normalized Layer";
    }
}

inline bool UsesPhysicalBulkAdvection(std::uint32_t value)
{
    const CloudShapeMode mode = Mode(value);
    return mode == CloudShapeMode::WeatherPhysicalThickness ||
           mode == CloudShapeMode::CirrusPhysicalLayer;
}
}

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

    DirectX::XMFLOAT2 cirrusFlowDirectionXZ = { 0.9396926f, 0.3420201f };
    float cirrusBaseAlongScaleMeters = 20000.0f;
    float cirrusBaseAcrossScaleMeters = 4000.0f;

    float cirrusBaseVerticalScaleMeters = 1000.0f;
    float cirrusDetailAlongScaleMeters = 6000.0f;
    float cirrusDetailAcrossScaleMeters = 1500.0f;
    float cirrusDetailVerticalScaleMeters = 500.0f;

    float cirrusMinimumThicknessMeters = 500.0f;
    float cirrusMaximumThicknessMeters = 1500.0f;
    float cirrusVerticalProfileCenter = 0.48f;
    float cirrusVerticalProfileHalfWidth = 0.45f;
};

static_assert(sizeof(CloudShapeParameters) == 112,
              "CloudShapeParameters must match CloudShapeCB");

inline CloudShapeParameters SanitizeCloudShapeParameters(
    const CloudShapeParameters& value)
{
    CloudShapeParameters result = value;
    const auto finiteOr = [](float input, float fallback)
    {
        return std::isfinite(input) ? input : fallback;
    };
    if (result.shapeMode >
        static_cast<std::uint32_t>(CloudShapeMode::CirrusPhysicalLayer))
    {
        result.shapeMode = static_cast<std::uint32_t>(
            CloudShapeMode::LegacyNormalizedLayer);
    }
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
    float flowX = finiteOr(result.cirrusFlowDirectionXZ.x, 0.9396926f);
    float flowZ = finiteOr(result.cirrusFlowDirectionXZ.y, 0.3420201f);
    const float flowLength = std::sqrt(flowX * flowX + flowZ * flowZ);
    if (flowLength <= 1.0e-6f)
    {
        flowX = 0.9396926f;
        flowZ = 0.3420201f;
    }
    else
    {
        flowX /= flowLength;
        flowZ /= flowLength;
    }
    result.cirrusFlowDirectionXZ = { flowX, flowZ };
    result.cirrusBaseAlongScaleMeters = std::clamp(
        finiteOr(result.cirrusBaseAlongScaleMeters, 20000.0f), 100.0f, 200000.0f);
    result.cirrusBaseAcrossScaleMeters = std::clamp(
        finiteOr(result.cirrusBaseAcrossScaleMeters, 4000.0f), 100.0f, 200000.0f);
    result.cirrusBaseVerticalScaleMeters = std::clamp(
        finiteOr(result.cirrusBaseVerticalScaleMeters, 1000.0f), 50.0f, 20000.0f);
    result.cirrusDetailAlongScaleMeters = std::clamp(
        finiteOr(result.cirrusDetailAlongScaleMeters, 6000.0f), 50.0f, 100000.0f);
    result.cirrusDetailAcrossScaleMeters = std::clamp(
        finiteOr(result.cirrusDetailAcrossScaleMeters, 1500.0f), 50.0f, 100000.0f);
    result.cirrusDetailVerticalScaleMeters = std::clamp(
        finiteOr(result.cirrusDetailVerticalScaleMeters, 500.0f), 25.0f, 10000.0f);
    result.cirrusMinimumThicknessMeters = std::clamp(
        finiteOr(result.cirrusMinimumThicknessMeters, 500.0f), 50.0f, 6000.0f);
    result.cirrusMaximumThicknessMeters = std::clamp(
        finiteOr(result.cirrusMaximumThicknessMeters, 1500.0f),
        result.cirrusMinimumThicknessMeters, 6000.0f);
    result.cirrusVerticalProfileCenter = std::clamp(
        finiteOr(result.cirrusVerticalProfileCenter, 0.48f), 0.0f, 1.0f);
    result.cirrusVerticalProfileHalfWidth = std::clamp(
        finiteOr(result.cirrusVerticalProfileHalfWidth, 0.45f), 0.01f, 1.0f);
    result.padding0 = 0.0f;
    result.padding1 = 0.0f;
    return result;
}
