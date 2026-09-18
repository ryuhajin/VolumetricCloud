// 공통 지역 두께(m)와 고정 타입을 GPU b10으로 전달한다.
#pragma once
#include "CloudTypeSelection.h"
#include "FormationParameterRanges.h"
#include <algorithm>
#include <cstddef>
#include <cmath>
struct WeatherColumnSettings
{
    float minimumThicknessMeters = 2250.0f;
    float maximumThicknessMeters = 3550.0f;
    float maximumBaseLiftMeters = 200.0f;
};
inline WeatherColumnSettings SanitizeWeatherColumnSettings(WeatherColumnSettings v)
{
    v.minimumThicknessMeters = std::clamp(std::isfinite(v.minimumThicknessMeters) ? v.minimumThicknessMeters : 2250.0f, 1.0f, 6000.0f);
    v.maximumThicknessMeters = std::clamp(std::isfinite(v.maximumThicknessMeters) ? v.maximumThicknessMeters : 3550.0f, v.minimumThicknessMeters, 6000.0f);
    v.maximumBaseLiftMeters = std::clamp(std::isfinite(v.maximumBaseLiftMeters) ? v.maximumBaseLiftMeters : 200.0f, 0.0f, 2000.0f);
    return v;
}
struct alignas(16) WeatherColumnParameters
{
    float minimumThicknessMeters = 2250.0f;
    float maximumThicknessMeters = 3550.0f;
    float paddingThickness0 = 0.0f;
    float paddingThickness1 = 0.0f;
    float maximumBaseLiftMeters = 200.0f;
    float fixedType = 0.5f;
    float paddingSelection = 0.0f;
    float padding0 = 0.0f;
};
static_assert(sizeof(WeatherColumnParameters)==32, "WeatherColumnCB size");
static_assert(offsetof(WeatherColumnParameters, fixedType)==20, "WeatherColumnCB type offset");
inline WeatherColumnParameters ResolveWeatherColumnParameters(const WeatherColumnSettings& input, const CloudTypeSelection& selection)
{
    const auto v=SanitizeWeatherColumnSettings(input);
    WeatherColumnParameters r;
    r.minimumThicknessMeters=v.minimumThicknessMeters;
    r.maximumThicknessMeters=v.maximumThicknessMeters;
    r.maximumBaseLiftMeters=v.maximumBaseLiftMeters;
    r.fixedType=FixedCloudType(selection);
    return r;
}
