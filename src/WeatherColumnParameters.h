// ============================================================================
//  WeatherColumnParameters.h - Weather RGBA를 물리 컬럼으로 해석하는 b10 계약
// ============================================================================
#pragma once

#include "CloudTypeSelection.h"

#include <algorithm>
#include <cstddef>
#include <cmath>

struct WeatherColumnSettings
{
    // 적용 전 domain thickness >= 선택 모드의 max thickness + max lift + 200m를
    // 검사한다. 이 파생식보다 작으면 구름 상단이 domain에서 잘리므로 apply를 거부한다.
    // 층운의 A=0/1 두께(m). 범위가 커지면 낮고 넓은 층운의 수직 부피가 늘어난다.
    float stratusMinimumThicknessMeters = 1500.0f;
    float stratusMaximumThicknessMeters = 2500.0f;
    // 적운의 A=0/1 두께(m). 범위가 커지면 적운 상부가 더 높게 발달한다.
    float cumulusMinimumThicknessMeters = 3000.0f;
    float cumulusMaximumThicknessMeters = 4600.0f;
    // 약한 지역의 바닥 상승 한계(m). 늘리면 밑면 기복이 커지며 domain 여유가 필요하다.
    float maximumBaseLiftMeters = 200.0f;
};

inline WeatherColumnSettings SanitizeWeatherColumnSettings(
    const WeatherColumnSettings& value)
{
    WeatherColumnSettings result = value;
    const auto finiteOr = [](float input, float fallback)
    {
        return std::isfinite(input) ? input : fallback;
    };
    result.stratusMinimumThicknessMeters = std::clamp(
        finiteOr(result.stratusMinimumThicknessMeters, 1500.0f), 1.0f, 6000.0f);
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
    result.maximumBaseLiftMeters = std::clamp(
        finiteOr(result.maximumBaseLiftMeters, 200.0f), 0.0f, 2000.0f);
    return result;
}

struct alignas(16) WeatherColumnParameters
{
    float stratusMinimumThicknessMeters = 1500.0f;
    float stratusMaximumThicknessMeters = 2500.0f;
    float cumulusMinimumThicknessMeters = 3000.0f;
    float cumulusMaximumThicknessMeters = 4600.0f;
    float maximumBaseLiftMeters = 200.0f;
    float fixedType = 0.5f;
    float regionalInfluence = 1.0f;
    float padding0 = 0.0f;
};

static_assert(sizeof(WeatherColumnParameters) == 32,
              "WeatherColumnParameters must match WeatherColumnCB");
static_assert(offsetof(WeatherColumnParameters, fixedType) == 20,
              "WeatherColumnParameters selection ABI changed");

inline WeatherColumnParameters ResolveWeatherColumnParameters(
    const WeatherColumnSettings& inputSettings,
    const CloudTypeSelection& selection)
{
    const WeatherColumnSettings settings =
        SanitizeWeatherColumnSettings(inputSettings);
    WeatherColumnParameters result;
    result.stratusMinimumThicknessMeters = settings.stratusMinimumThicknessMeters;
    result.stratusMaximumThicknessMeters = settings.stratusMaximumThicknessMeters;
    result.cumulusMinimumThicknessMeters = settings.cumulusMinimumThicknessMeters;
    result.cumulusMaximumThicknessMeters = settings.cumulusMaximumThicknessMeters;
    result.maximumBaseLiftMeters = settings.maximumBaseLiftMeters;
    result.fixedType = FixedCloudType(selection);
    result.regionalInfluence = RegionalTypeInfluence(selection);
    return result;
}
