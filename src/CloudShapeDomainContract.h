// ============================================================================
//  CloudShapeDomainContract.h - Stage 15B 전역 도메인/로컬 형상 CPU 계약
// ============================================================================
#pragma once

#include "CloudDomainParameters.h"
#include "CloudShapeParameters.h"
#include "CloudTypeSelection.h"
#include "WeatherColumnParameters.h"

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
    const WeatherColumnSettings& input,
    const CloudTypeSelection& inputSelection)
{
    const WeatherColumnSettings column = SanitizeWeatherColumnSettings(input);
    switch (SanitizeCloudTypeSelection(inputSelection).mode)
    {
    case CloudTypeSelectionMode::FixedStratus:
        return column.stratusMaximumThicknessMeters;
    case CloudTypeSelectionMode::FixedMixed:
        return 0.5f * (column.stratusMaximumThicknessMeters +
                       column.cumulusMaximumThicknessMeters);
    case CloudTypeSelectionMode::FixedCumulus:
        return column.cumulusMaximumThicknessMeters;
    case CloudTypeSelectionMode::RegionalBlend:
    default:
        return std::max(column.stratusMaximumThicknessMeters,
                        column.cumulusMaximumThicknessMeters);
    }
}

inline FitResult EvaluateFit(
    const WeatherColumnSettings& inputColumn,
    const CloudTypeSelection& typeSelection,
    const CloudDomainParameters& inputDomain,
    float requiredHeadroomMeters = 0.0f)
{
    const WeatherColumnSettings column =
        SanitizeWeatherColumnSettings(inputColumn);
    const CloudDomainParameters domain =
        SanitizeCloudDomainParameters(inputDomain);
    FitResult result;
    result.activeMaximumThicknessMeters =
        ActiveMaximumThicknessMeters(column, typeSelection);
    result.availableLayerThicknessMeters = domain.cloudLayerThickness;
    const float safeHeadroom = std::max(
        std::isfinite(requiredHeadroomMeters) ? requiredHeadroomMeters : 0.0f,
        0.0f);

    result.maximumBaseLiftMeters = column.maximumBaseLiftMeters;
    result.requiredLayerThicknessMeters =
        result.activeMaximumThicknessMeters +
        result.maximumBaseLiftMeters + safeHeadroom;
    result.remainingHeadroomMeters = domain.cloudLayerThickness -
        result.activeMaximumThicknessMeters - result.maximumBaseLiftMeters;
    result.valid = result.requiredLayerThicknessMeters <=
        domain.cloudLayerThickness + 1.0e-3f;
    return result;
}

inline float LightingReferenceAltitudeMeters(
    const WeatherColumnSettings& inputColumn,
    const CloudTypeSelection& typeSelection,
    const CloudDomainParameters& inputDomain)
{
    const WeatherColumnSettings column =
        SanitizeWeatherColumnSettings(inputColumn);
    const CloudDomainParameters domain =
        SanitizeCloudDomainParameters(inputDomain);
    return std::clamp(
        domain.cloudBottomAltitude + 0.5f *
            ActiveMaximumThicknessMeters(column, typeSelection),
        domain.cloudBottomAltitude,
        domain.cloudBottomAltitude + domain.cloudLayerThickness);
}
}
