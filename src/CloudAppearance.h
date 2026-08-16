// ============================================================================
//  CloudAppearance.h - 단계 13-4E 구름 외형 프리셋과 Custom 저장 계약
// ============================================================================
#pragma once

#include "CloudParameters.h"
#include "CloudShapeParameters.h"
#include "WeatherMap.h"

#include <filesystem>
#include <string>

enum class CloudAppearancePreset : std::uint32_t
{
    DenseMixedDefault = 0,
    Stratus = 1,
    Cumulus = 2,
    Custom = 3,
    CustomUnsaved = 4,
};

// 외형 버튼이 소유하는 값만 모은다. Weather seed/period와 wind/offset,
// sampling/lighting은 의도적으로 포함하지 않는다.
struct CloudAppearanceSettings
{
    CloudTypeMode cloudTypeMode = CloudTypeMode::WeatherMap;
    float globalCoverage = 0.68f;
    float densityMultiplier = 1.15f;
    float extinctionPerMeter = 0.00035f;
    float detailErosion = 0.18f;

    float weatherThreshold = 0.50f;
    float weatherSoftness = 0.20f;
    float coverageBias = 0.0f;
    float coverageContrast = 1.05f;
    float densityCoverageLink = 0.45f;
    float thicknessCoverageLink = 0.60f;
    float cloudTypeBias = 0.08f;

    float stratusMinimumThicknessMeters = 1500.0f;
    float stratusMaximumThicknessMeters = 2500.0f;
    float cumulusMinimumThicknessMeters = 3000.0f;
    float cumulusMaximumThicknessMeters = 6000.0f;

    float stratusBottomFadeEnd = 0.06f;
    float stratusTopFadeStart = 0.65f;
    float mixedBottomFadeEnd = 0.10f;
    float mixedTopFadeStart = 0.86f;
    float cumulusBottomFadeEnd = 0.08f;
    float cumulusTopFadeStart = 0.93f;
    float cumulusUpperMassBottom = 0.65f;
    float cumulusUpperMassStart = 0.08f;
    float cumulusUpperMassEnd = 0.70f;
};

const char* CloudAppearancePresetName(CloudAppearancePreset preset);
CloudAppearanceSettings DenseMixedAppearance();
CloudAppearanceSettings StratusAppearance();
CloudAppearanceSettings CumulusAppearance();
CloudAppearanceSettings CaptureCloudAppearance(
    const CloudParameters& cloud,
    const CloudShapeParameters& shape,
    const WeatherMapGeneratorSettings& weather);
void ApplyCloudAppearance(const CloudAppearanceSettings& appearance,
                          CloudParameters& cloud,
                          CloudShapeParameters& shape,
                          WeatherMapGeneratorSettings& weather);
bool CloudAppearanceSettingsEqual(const CloudAppearanceSettings& a,
                                  const CloudAppearanceSettings& b,
                                  float epsilon = 1.0e-6f);
bool IsValidCloudAppearanceSettings(const CloudAppearanceSettings& value);

// HLSL의 단계 13-4E density 계약과 같은 CPU 회귀 기준이다.
float EvaluateAppearanceWeatherSupport(float weatherCoverage);
float EvaluateAppearanceHorizontalCoverage(float globalCoverage,
                                           float weatherCoverage,
                                           float typedFootprintScale);
float EvaluateAppearanceBaseDensity(float globalCoverage,
                                    float weatherCoverage,
                                    float rawNoise,
                                    float typedFootprintScale,
                                    float typedVerticalProfile,
                                    float densityMultiplier,
                                    float weatherDensityModifier,
                                    bool insideLocalColumn);
float ResolvePipelineComparisonTime(bool comparisonActive,
                                    float normalEffectiveTime);

// schema 29 Custom 전용 파일. load 실패 시 outSettings를 변경하지 않는다.
bool SaveCustomCloudAppearanceAtomic(
    const std::filesystem::path& path,
    const CloudAppearanceSettings& settings,
    std::string& status);
bool LoadCustomCloudAppearance(
    const std::filesystem::path& path,
    CloudAppearanceSettings& outSettings,
    std::string& status);
