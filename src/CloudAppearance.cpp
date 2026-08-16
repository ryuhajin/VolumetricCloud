// ============================================================================
//  CloudAppearance.cpp - 단계 13-4E 외형 프리셋/밀도 기준/엄격한 JSON 29
// ============================================================================
#include "CloudAppearance.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>

namespace
{
float Saturate(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

float Smoothstep(float edge0, float edge1, float value)
{
    const float t = Saturate((value - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

float RemapCoverage(float noise, float coverage)
{
    const float threshold = 1.0f - Saturate(coverage);
    return Saturate((noise - threshold) / std::max(coverage, 1.0e-4f));
}

bool FiniteIn(float value, float minimum, float maximum)
{
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

bool FindUniqueValueStart(const std::string& text, const char* key,
                          std::size_t& valueStart)
{
    const std::string token = std::string("\"") + key + "\"";
    const std::size_t first = text.find(token);
    if (first == std::string::npos ||
        text.find(token, first + token.size()) != std::string::npos)
        return false;
    const std::size_t colon = text.find(':', first + token.size());
    if (colon == std::string::npos)
        return false;
    valueStart = text.find_first_not_of(" \t\r\n", colon + 1);
    return valueStart != std::string::npos;
}

bool IsJsonValueEnd(const char* end)
{
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')
        ++end;
    return *end == ',' || *end == '}' || *end == '\0';
}

bool ParseFloat(const std::string& text, const char* key, float& value)
{
    std::size_t start = 0;
    if (!FindUniqueValueStart(text, key, start))
        return false;
    char* end = nullptr;
    const float parsed = std::strtof(text.c_str() + start, &end);
    if (end == text.c_str() + start || !std::isfinite(parsed) ||
        !IsJsonValueEnd(end))
        return false;
    value = parsed;
    return true;
}

bool ParseUnsigned(const std::string& text, const char* key,
                   std::uint32_t& value)
{
    std::size_t start = 0;
    if (!FindUniqueValueStart(text, key, start))
        return false;
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(text.c_str() + start, &end, 10);
    if (end == text.c_str() + start || !IsJsonValueEnd(end) ||
        parsed > std::numeric_limits<std::uint32_t>::max())
        return false;
    value = static_cast<std::uint32_t>(parsed);
    return true;
}

bool ParseString(const std::string& text, const char* key, std::string& value)
{
    std::size_t start = 0;
    if (!FindUniqueValueStart(text, key, start) || text[start] != '"')
        return false;
    const std::size_t end = text.find('"', start + 1);
    if (end == std::string::npos)
        return false;
    if (!IsJsonValueEnd(text.c_str() + end + 1))
        return false;
    value = text.substr(start + 1, end - start - 1);
    return true;
}

void WriteSettings(std::ostream& stream, const CloudAppearanceSettings& value)
{
    stream << std::fixed << std::setprecision(6)
           << "    \"cloudTypeMode\": "
           << static_cast<std::uint32_t>(value.cloudTypeMode) << ",\n"
           << "    \"globalCoverage\": " << value.globalCoverage << ",\n"
           << "    \"densityMultiplier\": " << value.densityMultiplier << ",\n"
           << "    \"extinctionPerMeter\": " << value.extinctionPerMeter << ",\n"
           << "    \"detailErosion\": " << value.detailErosion << ",\n"
           << "    \"weatherThreshold\": " << value.weatherThreshold << ",\n"
           << "    \"weatherSoftness\": " << value.weatherSoftness << ",\n"
           << "    \"coverageBias\": " << value.coverageBias << ",\n"
           << "    \"coverageContrast\": " << value.coverageContrast << ",\n"
           << "    \"densityCoverageLink\": " << value.densityCoverageLink << ",\n"
           << "    \"thicknessCoverageLink\": " << value.thicknessCoverageLink << ",\n"
           << "    \"cloudTypeBias\": " << value.cloudTypeBias << ",\n"
           << "    \"stratusMinimumThicknessMeters\": " << value.stratusMinimumThicknessMeters << ",\n"
           << "    \"stratusMaximumThicknessMeters\": " << value.stratusMaximumThicknessMeters << ",\n"
           << "    \"cumulusMinimumThicknessMeters\": " << value.cumulusMinimumThicknessMeters << ",\n"
           << "    \"cumulusMaximumThicknessMeters\": " << value.cumulusMaximumThicknessMeters << ",\n"
           << "    \"stratusBottomFadeEnd\": " << value.stratusBottomFadeEnd << ",\n"
           << "    \"stratusTopFadeStart\": " << value.stratusTopFadeStart << ",\n"
           << "    \"mixedBottomFadeEnd\": " << value.mixedBottomFadeEnd << ",\n"
           << "    \"mixedTopFadeStart\": " << value.mixedTopFadeStart << ",\n"
           << "    \"cumulusBottomFadeEnd\": " << value.cumulusBottomFadeEnd << ",\n"
           << "    \"cumulusTopFadeStart\": " << value.cumulusTopFadeStart << ",\n"
           << "    \"cumulusUpperMassBottom\": " << value.cumulusUpperMassBottom << ",\n"
           << "    \"cumulusUpperMassStart\": " << value.cumulusUpperMassStart << ",\n"
           << "    \"cumulusUpperMassEnd\": " << value.cumulusUpperMassEnd << "\n";
}

bool ParseSettings(const std::string& text, CloudAppearanceSettings& value)
{
    std::uint32_t type = 0;
    return ParseUnsigned(text, "cloudTypeMode", type) &&
        type <= static_cast<std::uint32_t>(CloudTypeMode::WeatherMap) &&
        (value.cloudTypeMode = static_cast<CloudTypeMode>(type), true) &&
        ParseFloat(text, "globalCoverage", value.globalCoverage) &&
        ParseFloat(text, "densityMultiplier", value.densityMultiplier) &&
        ParseFloat(text, "extinctionPerMeter", value.extinctionPerMeter) &&
        ParseFloat(text, "detailErosion", value.detailErosion) &&
        ParseFloat(text, "weatherThreshold", value.weatherThreshold) &&
        ParseFloat(text, "weatherSoftness", value.weatherSoftness) &&
        ParseFloat(text, "coverageBias", value.coverageBias) &&
        ParseFloat(text, "coverageContrast", value.coverageContrast) &&
        ParseFloat(text, "densityCoverageLink", value.densityCoverageLink) &&
        ParseFloat(text, "thicknessCoverageLink", value.thicknessCoverageLink) &&
        ParseFloat(text, "cloudTypeBias", value.cloudTypeBias) &&
        ParseFloat(text, "stratusMinimumThicknessMeters", value.stratusMinimumThicknessMeters) &&
        ParseFloat(text, "stratusMaximumThicknessMeters", value.stratusMaximumThicknessMeters) &&
        ParseFloat(text, "cumulusMinimumThicknessMeters", value.cumulusMinimumThicknessMeters) &&
        ParseFloat(text, "cumulusMaximumThicknessMeters", value.cumulusMaximumThicknessMeters) &&
        ParseFloat(text, "stratusBottomFadeEnd", value.stratusBottomFadeEnd) &&
        ParseFloat(text, "stratusTopFadeStart", value.stratusTopFadeStart) &&
        ParseFloat(text, "mixedBottomFadeEnd", value.mixedBottomFadeEnd) &&
        ParseFloat(text, "mixedTopFadeStart", value.mixedTopFadeStart) &&
        ParseFloat(text, "cumulusBottomFadeEnd", value.cumulusBottomFadeEnd) &&
        ParseFloat(text, "cumulusTopFadeStart", value.cumulusTopFadeStart) &&
        ParseFloat(text, "cumulusUpperMassBottom", value.cumulusUpperMassBottom) &&
        ParseFloat(text, "cumulusUpperMassStart", value.cumulusUpperMassStart) &&
        ParseFloat(text, "cumulusUpperMassEnd", value.cumulusUpperMassEnd);
}
}

const char* CloudAppearancePresetName(CloudAppearancePreset preset)
{
    switch (preset)
    {
    case CloudAppearancePreset::DenseMixedDefault: return "Dense Mixed Default";
    case CloudAppearancePreset::Stratus: return "Stratus";
    case CloudAppearancePreset::Cumulus: return "Cumulus";
    case CloudAppearancePreset::Custom: return "Custom";
    case CloudAppearancePreset::CustomUnsaved: return "Custom Unsaved";
    default: return "Dense Mixed Default";
    }
}

CloudAppearanceSettings DenseMixedAppearance()
{
    return {};
}

CloudAppearanceSettings StratusAppearance()
{
    CloudAppearanceSettings value = DenseMixedAppearance();
    value.cloudTypeMode = CloudTypeMode::Stratus;
    value.globalCoverage = 0.72f;
    value.densityMultiplier = 1.20f;
    value.extinctionPerMeter = 0.00042f;
    value.detailErosion = 0.12f;
    value.weatherThreshold = 0.49f;
    value.weatherSoftness = 0.22f;
    value.coverageBias = 0.01f;
    value.coverageContrast = 1.03f;
    value.densityCoverageLink = 0.50f;
    value.thicknessCoverageLink = 0.65f;
    value.cloudTypeBias = 0.0f;
    value.stratusMaximumThicknessMeters = 2800.0f;
    value.stratusBottomFadeEnd = 0.05f;
    value.stratusTopFadeStart = 0.72f;
    return value;
}

CloudAppearanceSettings CumulusAppearance()
{
    CloudAppearanceSettings value = DenseMixedAppearance();
    value.cloudTypeMode = CloudTypeMode::Cumulus;
    value.globalCoverage = 0.68f;
    value.densityMultiplier = 1.25f;
    value.extinctionPerMeter = 0.00038f;
    value.detailErosion = 0.18f;
    value.weatherThreshold = 0.52f;
    value.weatherSoftness = 0.20f;
    value.coverageBias = 0.0f;
    value.coverageContrast = 1.05f;
    value.densityCoverageLink = 0.45f;
    value.thicknessCoverageLink = 0.70f;
    value.cloudTypeBias = 0.0f;
    value.cumulusBottomFadeEnd = 0.08f;
    value.cumulusTopFadeStart = 0.94f;
    value.cumulusUpperMassBottom = 0.65f;
    value.cumulusUpperMassStart = 0.08f;
    value.cumulusUpperMassEnd = 0.70f;
    return value;
}

CloudAppearanceSettings CaptureCloudAppearance(
    const CloudParameters& cloud, const CloudShapeParameters& shape,
    const WeatherMapGeneratorSettings& weather)
{
    CloudAppearanceSettings value;
    value.cloudTypeMode = weather.cloudTypeMode;
    value.globalCoverage = cloud.coverage;
    value.densityMultiplier = cloud.densityMultiplier;
    value.extinctionPerMeter = cloud.extinctionCoefficient;
    value.detailErosion = cloud.detailErosionStrength;
    value.weatherThreshold = weather.coverageThreshold;
    value.weatherSoftness = weather.coverageSoftness;
    value.coverageBias = weather.coverage.bias;
    value.coverageContrast = weather.coverage.contrast;
    value.densityCoverageLink = weather.densityCoverageInfluence;
    value.thicknessCoverageLink = weather.thicknessCoverageInfluence;
    value.cloudTypeBias = weather.cloudType.bias;
    value.stratusMinimumThicknessMeters = shape.stratusMinimumThicknessMeters;
    value.stratusMaximumThicknessMeters = shape.stratusMaximumThicknessMeters;
    value.cumulusMinimumThicknessMeters = shape.cumulusMinimumThicknessMeters;
    value.cumulusMaximumThicknessMeters = shape.cumulusMaximumThicknessMeters;
    value.stratusBottomFadeEnd = shape.stratusBottomFadeEnd;
    value.stratusTopFadeStart = shape.stratusTopFadeStart;
    value.mixedBottomFadeEnd = shape.mixedBottomFadeEnd;
    value.mixedTopFadeStart = shape.mixedTopFadeStart;
    value.cumulusBottomFadeEnd = shape.cumulusBottomFadeEnd;
    value.cumulusTopFadeStart = shape.cumulusTopFadeStart;
    value.cumulusUpperMassBottom = shape.cumulusUpperMassBottom;
    value.cumulusUpperMassStart = shape.cumulusUpperMassStart;
    value.cumulusUpperMassEnd = shape.cumulusUpperMassEnd;
    return value;
}

void ApplyCloudAppearance(const CloudAppearanceSettings& value,
                          CloudParameters& cloud,
                          CloudShapeParameters& shape,
                          WeatherMapGeneratorSettings& weather)
{
    cloud.coverage = value.globalCoverage;
    cloud.densityMultiplier = value.densityMultiplier;
    cloud.extinctionCoefficient = value.extinctionPerMeter;
    cloud.detailErosionStrength = value.detailErosion;
    weather.coverageThreshold = value.weatherThreshold;
    weather.coverageSoftness = value.weatherSoftness;
    weather.coverage.bias = value.coverageBias;
    weather.coverage.contrast = value.coverageContrast;
    weather.densityCoverageInfluence = value.densityCoverageLink;
    weather.thicknessCoverageInfluence = value.thicknessCoverageLink;
    weather.cloudType.bias = value.cloudTypeBias;
    weather.cloudTypeMode = value.cloudTypeMode;
    shape.stratusMinimumThicknessMeters = value.stratusMinimumThicknessMeters;
    shape.stratusMaximumThicknessMeters = value.stratusMaximumThicknessMeters;
    shape.cumulusMinimumThicknessMeters = value.cumulusMinimumThicknessMeters;
    shape.cumulusMaximumThicknessMeters = value.cumulusMaximumThicknessMeters;
    shape.stratusBottomFadeEnd = value.stratusBottomFadeEnd;
    shape.stratusTopFadeStart = value.stratusTopFadeStart;
    shape.mixedBottomFadeEnd = value.mixedBottomFadeEnd;
    shape.mixedTopFadeStart = value.mixedTopFadeStart;
    shape.cumulusBottomFadeEnd = value.cumulusBottomFadeEnd;
    shape.cumulusTopFadeStart = value.cumulusTopFadeStart;
    shape.cumulusUpperMassBottom = value.cumulusUpperMassBottom;
    shape.cumulusUpperMassStart = value.cumulusUpperMassStart;
    shape.cumulusUpperMassEnd = value.cumulusUpperMassEnd;
}

bool CloudAppearanceSettingsEqual(const CloudAppearanceSettings& a,
                                  const CloudAppearanceSettings& b,
                                  float epsilon)
{
    if (a.cloudTypeMode != b.cloudTypeMode)
        return false;
    const std::array<float, 24> av = {
        a.globalCoverage, a.densityMultiplier, a.extinctionPerMeter,
        a.detailErosion, a.weatherThreshold, a.weatherSoftness,
        a.coverageBias, a.coverageContrast, a.densityCoverageLink,
        a.thicknessCoverageLink, a.cloudTypeBias,
        a.stratusMinimumThicknessMeters, a.stratusMaximumThicknessMeters,
        a.cumulusMinimumThicknessMeters, a.cumulusMaximumThicknessMeters,
        a.stratusBottomFadeEnd, a.stratusTopFadeStart,
        a.mixedBottomFadeEnd, a.mixedTopFadeStart,
        a.cumulusBottomFadeEnd, a.cumulusTopFadeStart,
        a.cumulusUpperMassBottom, a.cumulusUpperMassStart,
        a.cumulusUpperMassEnd,
    };
    const std::array<float, 24> bv = {
        b.globalCoverage, b.densityMultiplier, b.extinctionPerMeter,
        b.detailErosion, b.weatherThreshold, b.weatherSoftness,
        b.coverageBias, b.coverageContrast, b.densityCoverageLink,
        b.thicknessCoverageLink, b.cloudTypeBias,
        b.stratusMinimumThicknessMeters, b.stratusMaximumThicknessMeters,
        b.cumulusMinimumThicknessMeters, b.cumulusMaximumThicknessMeters,
        b.stratusBottomFadeEnd, b.stratusTopFadeStart,
        b.mixedBottomFadeEnd, b.mixedTopFadeStart,
        b.cumulusBottomFadeEnd, b.cumulusTopFadeStart,
        b.cumulusUpperMassBottom, b.cumulusUpperMassStart,
        b.cumulusUpperMassEnd,
    };
    for (std::size_t index = 0; index < av.size(); ++index)
    {
        if (std::abs(av[index] - bv[index]) > epsilon)
            return false;
    }
    return true;
}

bool IsValidCloudAppearanceSettings(const CloudAppearanceSettings& v)
{
    return static_cast<std::uint32_t>(v.cloudTypeMode) <= 3u &&
        FiniteIn(v.globalCoverage, 0.0f, 1.0f) &&
        FiniteIn(v.densityMultiplier, 0.0f, 5.0f) &&
        FiniteIn(v.extinctionPerMeter, 0.000001f, 0.01f) &&
        FiniteIn(v.detailErosion, 0.0f, 1.0f) &&
        FiniteIn(v.weatherThreshold, 0.0f, 1.0f) &&
        FiniteIn(v.weatherSoftness, 0.001f, 0.5f) &&
        FiniteIn(v.coverageBias, -1.0f, 1.0f) &&
        FiniteIn(v.coverageContrast, 0.01f, 4.0f) &&
        FiniteIn(v.densityCoverageLink, 0.0f, 1.0f) &&
        FiniteIn(v.thicknessCoverageLink, 0.0f, 1.0f) &&
        FiniteIn(v.cloudTypeBias, -1.0f, 1.0f) &&
        FiniteIn(v.stratusMinimumThicknessMeters, 1.0f, 6000.0f) &&
        FiniteIn(v.stratusMaximumThicknessMeters, v.stratusMinimumThicknessMeters, 6000.0f) &&
        FiniteIn(v.cumulusMinimumThicknessMeters, 1.0f, 6000.0f) &&
        FiniteIn(v.cumulusMaximumThicknessMeters, v.cumulusMinimumThicknessMeters, 6000.0f) &&
        FiniteIn(v.stratusBottomFadeEnd, 0.01f, 0.99f) &&
        FiniteIn(v.stratusTopFadeStart, v.stratusBottomFadeEnd, 0.99f) &&
        FiniteIn(v.mixedBottomFadeEnd, 0.01f, 0.99f) &&
        FiniteIn(v.mixedTopFadeStart, v.mixedBottomFadeEnd, 0.99f) &&
        FiniteIn(v.cumulusBottomFadeEnd, 0.01f, 0.99f) &&
        FiniteIn(v.cumulusTopFadeStart, v.cumulusBottomFadeEnd, 0.99f) &&
        FiniteIn(v.cumulusUpperMassBottom, 0.0f, 1.0f) &&
        FiniteIn(v.cumulusUpperMassStart, 0.0f, 0.99f) &&
        FiniteIn(v.cumulusUpperMassEnd, v.cumulusUpperMassStart + 0.01f, 1.0f);
}

float EvaluateAppearanceWeatherSupport(float weatherCoverage)
{
    return Smoothstep(0.02f, 0.20f, weatherCoverage);
}

float EvaluateAppearanceHorizontalCoverage(float globalCoverage,
                                           float weatherCoverage,
                                           float typedFootprintScale)
{
    const float weatherFactor = 0.70f + 0.30f * Saturate(weatherCoverage);
    const float footprintFactor = 0.80f + 0.20f * Saturate(typedFootprintScale);
    return Saturate(globalCoverage * weatherFactor * footprintFactor);
}

float EvaluateAppearanceBaseDensity(float globalCoverage,
                                    float weatherCoverage,
                                    float rawNoise,
                                    float typedFootprintScale,
                                    float typedVerticalProfile,
                                    float densityMultiplier,
                                    float weatherDensityModifier,
                                    bool insideLocalColumn)
{
    if (!insideLocalColumn)
        return 0.0f;
    const float horizontalCoverage = EvaluateAppearanceHorizontalCoverage(
        globalCoverage, weatherCoverage, typedFootprintScale);
    return EvaluateAppearanceWeatherSupport(weatherCoverage) *
        RemapCoverage(rawNoise, horizontalCoverage) *
        Saturate(typedVerticalProfile) * std::max(densityMultiplier, 0.0f) *
        std::max(weatherDensityModifier, 0.0f);
}

float ResolvePipelineComparisonTime(bool comparisonActive,
                                    float normalEffectiveTime)
{
    if (comparisonActive)
        return 0.0f;
    return std::isfinite(normalEffectiveTime) ? normalEffectiveTime : 0.0f;
}

bool SaveCustomCloudAppearanceAtomic(const std::filesystem::path& path,
                                     const CloudAppearanceSettings& settings,
                                     std::string& status)
{
    if (!IsValidCloudAppearanceSettings(settings))
    {
        status = "Custom save rejected: invalid or out-of-range value";
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
    {
        status = "Custom save failed: cannot create directory";
        return false;
    }
    const std::filesystem::path temporary = path.wstring() + L".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            status = "Custom save failed: cannot open temporary file";
            return false;
        }
        file << "{\n  \"schemaVersion\": 29,\n"
             << "  \"implementationStage\": \"13-4E\",\n"
             << "  \"cloudAppearance\": {\n"
             << "    \"activePreset\": \"Custom\",\n";
        WriteSettings(file, settings);
        file << "  }\n}\n";
        file.flush();
        if (!file)
        {
            status = "Custom save failed: incomplete temporary file";
            file.close();
            std::filesystem::remove(temporary, error);
            return false;
        }
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        std::filesystem::remove(temporary, error);
        status = "Custom save failed: atomic replace failed";
        return false;
    }
    status = "Custom appearance saved";
    return true;
}

bool LoadCustomCloudAppearance(const std::filesystem::path& path,
                               CloudAppearanceSettings& outSettings,
                               std::string& status)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        status = "No saved Custom appearance; Dense Mixed fallback is available";
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();
    std::uint32_t schema = 0;
    std::string stage;
    std::string preset;
    CloudAppearanceSettings parsed;
    if (!ParseUnsigned(text, "schemaVersion", schema) || schema != 29u ||
        !ParseString(text, "implementationStage", stage) || stage != "13-4E" ||
        !ParseString(text, "activePreset", preset) || preset != "Custom" ||
        !ParseSettings(text, parsed) || !IsValidCloudAppearanceSettings(parsed))
    {
        status = "Custom load rejected: schema 29 data is missing, corrupt, or out of range";
        return false;
    }
    outSettings = parsed;
    status = "Saved Custom appearance loaded into memory";
    return true;
}
