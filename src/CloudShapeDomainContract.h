// ============================================================================
//  CloudShapeDomainContract.h - Stage 15B 전역 도메인/로컬 형상 CPU 계약
// ============================================================================
#pragma once

#include "CloudDomainParameters.h"
#include "CloudShapeParameters.h"
#include "WeatherMap.h"

#include <algorithm>
#include <cmath>

namespace cloudshapedomain
{
struct FitResult
{
    bool valid = false;
    float activeMaximumThicknessMeters = 0.0f;
    float maximumBaseLiftMeters = 0.0f;
    float requiredLayerThicknessMeters = 0.0f;
    float availableLayerThicknessMeters = 0.0f;
    float remainingHeadroomMeters = 0.0f;
};

inline float ActiveMaximumThicknessMeters(
    const CloudShapeParameters& input, CloudTypeMode cloudTypeMode)
{
    const CloudShapeParameters shape = SanitizeCloudShapeParameters(input);
    if (cloudshape::Mode(shape.shapeMode) == CloudShapeMode::CirrusPhysicalLayer)
        return shape.cirrusMaximumThicknessMeters;
    if (cloudshape::Mode(shape.shapeMode) !=
        CloudShapeMode::WeatherPhysicalThickness)
        return 0.0f;

    switch (cloudTypeMode)
    {
    case CloudTypeMode::Stratus:
        return shape.stratusMaximumThicknessMeters;
    case CloudTypeMode::Mixed:
        return 0.5f * (shape.stratusMaximumThicknessMeters +
                       shape.cumulusMaximumThicknessMeters);
    case CloudTypeMode::Cumulus:
        return shape.cumulusMaximumThicknessMeters;
    case CloudTypeMode::WeatherMap:
    default:
        return std::max(shape.stratusMaximumThicknessMeters,
                        shape.cumulusMaximumThicknessMeters);
    }
}

inline FitResult EvaluateFit(
    const CloudShapeParameters& inputShape, CloudTypeMode cloudTypeMode,
    const CloudDomainParameters& inputDomain,
    float requiredHeadroomMeters = 0.0f)
{
    const CloudShapeParameters shape = SanitizeCloudShapeParameters(inputShape);
    const CloudDomainParameters domain =
        SanitizeCloudDomainParameters(inputDomain);
    FitResult result;
    result.activeMaximumThicknessMeters =
        ActiveMaximumThicknessMeters(shape, cloudTypeMode);
    result.availableLayerThicknessMeters = domain.cloudLayerThickness;
    const float safeHeadroom = std::max(
        std::isfinite(requiredHeadroomMeters) ? requiredHeadroomMeters : 0.0f,
        0.0f);

    if (cloudshape::Mode(shape.shapeMode) == CloudShapeMode::CirrusPhysicalLayer)
    {
        // Cirrus는 domain 안의 center fraction을 기준으로 위아래 절반씩 펼친다.
        const float lowerSpace = domain.cloudLayerThickness *
            shape.cirrusVerticalProfileCenter;
        const float upperSpace = domain.cloudLayerThickness *
            (1.0f - shape.cirrusVerticalProfileCenter);
        const float requiredHalf = 0.5f *
            result.activeMaximumThicknessMeters + safeHeadroom;
        result.requiredLayerThicknessMeters =
            result.activeMaximumThicknessMeters + 2.0f * safeHeadroom;
        result.remainingHeadroomMeters = std::min(lowerSpace, upperSpace) -
            0.5f * result.activeMaximumThicknessMeters;
        result.valid = lowerSpace >= requiredHalf && upperSpace >= requiredHalf;
        return result;
    }

    result.maximumBaseLiftMeters =
        cloudshape::Mode(shape.shapeMode) ==
                CloudShapeMode::WeatherPhysicalThickness
            ? shape.localBaseLiftMaxMeters : 0.0f;
    result.requiredLayerThicknessMeters =
        result.activeMaximumThicknessMeters +
        result.maximumBaseLiftMeters + safeHeadroom;
    result.remainingHeadroomMeters = domain.cloudLayerThickness -
        result.activeMaximumThicknessMeters - result.maximumBaseLiftMeters;
    result.valid = cloudshape::Mode(shape.shapeMode) ==
                       CloudShapeMode::LegacyNormalizedLayer ||
        result.requiredLayerThicknessMeters <=
            domain.cloudLayerThickness + 1.0e-3f;
    return result;
}

inline float LightingReferenceAltitudeMeters(
    const CloudShapeParameters& inputShape, CloudTypeMode cloudTypeMode,
    const CloudDomainParameters& inputDomain)
{
    const CloudShapeParameters shape = SanitizeCloudShapeParameters(inputShape);
    const CloudDomainParameters domain =
        SanitizeCloudDomainParameters(inputDomain);
    if (cloudshape::Mode(shape.shapeMode) == CloudShapeMode::CirrusPhysicalLayer)
    {
        return domain.cloudBottomAltitude + domain.cloudLayerThickness *
            shape.cirrusVerticalProfileCenter;
    }
    if (cloudshape::Mode(shape.shapeMode) ==
        CloudShapeMode::WeatherPhysicalThickness)
    {
        return std::clamp(
            domain.cloudBottomAltitude + 0.5f *
                ActiveMaximumThicknessMeters(shape, cloudTypeMode),
            domain.cloudBottomAltitude,
            domain.cloudBottomAltitude + domain.cloudLayerThickness);
    }
    return domain.cloudBottomAltitude + 0.5f * domain.cloudLayerThickness;
}
}
