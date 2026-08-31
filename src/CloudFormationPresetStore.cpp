// ============================================================================
//  CloudFormationPresetStore.cpp - Stage 15B 독립 formation JSON/내장값
// ============================================================================
#include "CloudFormationPresetStore.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>

namespace
{
constexpr std::uintmax_t kMaximumPresetFileBytes = 1024u * 1024u;

PeriodicChannelSettings Channel(
    std::uint32_t seed, std::uint32_t macroPeriod,
    std::uint32_t detailPeriod, float detailWeight,
    float bias, float contrast)
{
    return { seed, macroPeriod, detailPeriod, detailWeight, bias, contrast };
}

CloudFormationSettings BasePhysicalFormation()
{
    CloudFormationSettings result;
    result.coverage = 0.68f;
    result.densityMultiplier = 1.15f;
    result.extinctionPerMeter = 0.00035f;
    result.detailErosion = 0.18f;
    result.weatherPreset = Stage5WeatherPreset::PeriodicPerlin;
    result.weather = WeatherMapGeneratorSettings{};
    result.weatherWorldSizeMeters = 64000.0f;
    result.shape = CloudShapeParameters{};
    result.shape.shapeMode = static_cast<std::uint32_t>(
        CloudShapeMode::WeatherPhysicalThickness);
    result.shape.stratusMinimumThicknessMeters = 1500.0f;
    result.shape.stratusMaximumThicknessMeters = 2500.0f;
    result.shape.cumulusMinimumThicknessMeters = 3000.0f;
    result.shape.cumulusMaximumThicknessMeters = 6000.0f;
    result.shape.stratusBottomFadeEnd = 0.06f;
    result.shape.stratusTopFadeStart = 0.65f;
    result.shape.mixedBottomFadeEnd = 0.10f;
    result.shape.mixedTopFadeStart = 0.86f;
    result.shape.cumulusBottomFadeEnd = 0.08f;
    result.shape.cumulusTopFadeStart = 0.93f;
    result.shape.cumulusUpperMassBottom = 0.65f;
    result.shape.cumulusUpperMassStart = 0.08f;
    result.shape.cumulusUpperMassEnd = 0.70f;
    result.shape.localBaseLiftMaxMeters = 0.0f;
    result.shape.footprintCoverageInfluence = 0.20f;
    result.domainBottomMeters = 1800.0f;
    result.domainThicknessMeters = 3700.0f;
    result.maximumViewTraceDistanceMeters = 50000.0f;
    result.viewTraceFadeStartDistanceMeters = 40000.0f;
    result.maximumLightTraceDistanceMeters = 20000.0f;
    result.baseNoiseWorldSizeMeters = 12000.0f;
    result.baseNoiseVerticalWorldSizeMeters = 12000.0f;
    result.detailNoiseWorldSizeMeters = 2000.0f;
    result.windDirection = { 0.9701425f, 0.0f, 0.2425356f };
    result.windSpeedMetersPerSecond = 12.0f;
    return result;
}

bool FinalizeBuiltIn(CloudFormationSettings& value,
                     CloudFormationSettings& outSettings)
{
    PreparedCloudFormation prepared;
    std::string ignoredStatus;
    if (!PrepareCloudFormationSettings(value, prepared, ignoredStatus))
        return false;
    outSettings = prepared.settings;
    return true;
}

const char* TargetId(const CloudFormationPresetTarget& target)
{
    if (!IsValidCloudFormationPresetTarget(target))
        return "invalid";
    if (target.group == CloudFormationPresetGroup::Custom)
        return "custom";
    if (target.group == CloudFormationPresetGroup::Concept)
    {
        switch (static_cast<CloudFormationConcept>(target.index))
        {
        case CloudFormationConcept::UrbanFairWeather:
            return "urban-fair-weather";
        case CloudFormationConcept::MeadowBrokenClouds:
            return "meadow-broken-clouds";
        case CloudFormationConcept::DesertCirrus:
            return "desert-cirrus";
        case CloudFormationConcept::SnowOvercast:
            return "snow-overcast";
        }
    }
    switch (static_cast<CloudFormationType>(target.index))
    {
    case CloudFormationType::Stratus: return "stratus";
    case CloudFormationType::Cumulus: return "cumulus";
    case CloudFormationType::Cirrus: return "cirrus";
    }
    return "invalid";
}

bool FindUniqueValueStart(const std::string& text, const char* key,
                          std::size_t& valueStart)
{
    const std::string token = std::string("\"") + key + "\"";
    const std::size_t first = text.find(token);
    if (first == std::string::npos ||
        text.find(token, first + token.size()) != std::string::npos)
    {
        return false;
    }
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
    {
        return false;
    }
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
    const unsigned long parsed = std::strtoul(
        text.c_str() + start, &end, 10);
    if (end == text.c_str() + start || !IsJsonValueEnd(end) ||
        parsed > std::numeric_limits<std::uint32_t>::max())
    {
        return false;
    }
    value = static_cast<std::uint32_t>(parsed);
    return true;
}

bool ParseString(const std::string& text, const char* key, std::string& value)
{
    std::size_t start = 0;
    if (!FindUniqueValueStart(text, key, start) || text[start] != '"')
        return false;
    const std::size_t end = text.find('"', start + 1);
    if (end == std::string::npos ||
        !IsJsonValueEnd(text.c_str() + end + 1))
    {
        return false;
    }
    value = text.substr(start + 1, end - start - 1);
    return true;
}

bool ParseChannel(const std::string& text, const char* prefix,
                  PeriodicChannelSettings& value)
{
    const std::string stem(prefix);
    return ParseUnsigned(text, (stem + "Seed").c_str(), value.seed) &&
        ParseUnsigned(text, (stem + "MacroPeriod").c_str(),
                      value.macroPeriod) &&
        ParseUnsigned(text, (stem + "DetailPeriod").c_str(),
                      value.detailPeriod) &&
        ParseFloat(text, (stem + "DetailWeight").c_str(),
                   value.detailWeight) &&
        ParseFloat(text, (stem + "Bias").c_str(), value.bias) &&
        ParseFloat(text, (stem + "Contrast").c_str(), value.contrast);
}

bool ParseFormation(const std::string& text, CloudFormationSettings& value)
{
    std::uint32_t weatherPreset = 0;
    std::uint32_t cloudTypeMode = 0;
    std::uint32_t shapeMode = 0;
    if (!ParseFloat(text, "coverage", value.coverage) ||
        !ParseFloat(text, "densityMultiplier", value.densityMultiplier) ||
        !ParseFloat(text, "extinctionPerMeter", value.extinctionPerMeter) ||
        !ParseFloat(text, "detailErosion", value.detailErosion) ||
        !ParseUnsigned(text, "weatherPreset", weatherPreset) ||
        !ParseChannel(text, "weatherCoverage", value.weather.coverage) ||
        !ParseChannel(text, "weatherCloudType", value.weather.cloudType) ||
        !ParseChannel(text, "weatherDensity", value.weather.density) ||
        !ParseChannel(text, "weatherLocalThickness",
                      value.weather.localThickness) ||
        !ParseFloat(text, "weatherCoverageThreshold",
                    value.weather.coverageThreshold) ||
        !ParseFloat(text, "weatherCoverageSoftness",
                    value.weather.coverageSoftness) ||
        !ParseFloat(text, "weatherDensityCoverageInfluence",
                    value.weather.densityCoverageInfluence) ||
        !ParseFloat(text, "weatherThicknessCoverageInfluence",
                    value.weather.thicknessCoverageInfluence) ||
        !ParseUnsigned(text, "weatherCloudTypeMode", cloudTypeMode) ||
        !ParseFloat(text, "weatherWorldSizeMeters",
                    value.weatherWorldSizeMeters) ||
        !ParseUnsigned(text, "shapeMode", shapeMode) ||
        !ParseFloat(text, "stratusMinimumThicknessMeters",
                    value.shape.stratusMinimumThicknessMeters) ||
        !ParseFloat(text, "stratusMaximumThicknessMeters",
                    value.shape.stratusMaximumThicknessMeters) ||
        !ParseFloat(text, "cumulusMinimumThicknessMeters",
                    value.shape.cumulusMinimumThicknessMeters) ||
        !ParseFloat(text, "cumulusMaximumThicknessMeters",
                    value.shape.cumulusMaximumThicknessMeters) ||
        !ParseFloat(text, "stratusBottomFadeEnd",
                    value.shape.stratusBottomFadeEnd) ||
        !ParseFloat(text, "stratusTopFadeStart",
                    value.shape.stratusTopFadeStart) ||
        !ParseFloat(text, "mixedBottomFadeEnd",
                    value.shape.mixedBottomFadeEnd) ||
        !ParseFloat(text, "mixedTopFadeStart",
                    value.shape.mixedTopFadeStart) ||
        !ParseFloat(text, "cumulusBottomFadeEnd",
                    value.shape.cumulusBottomFadeEnd) ||
        !ParseFloat(text, "cumulusTopFadeStart",
                    value.shape.cumulusTopFadeStart) ||
        !ParseFloat(text, "cumulusUpperMassBottom",
                    value.shape.cumulusUpperMassBottom) ||
        !ParseFloat(text, "cumulusUpperMassStart",
                    value.shape.cumulusUpperMassStart) ||
        !ParseFloat(text, "cumulusUpperMassEnd",
                    value.shape.cumulusUpperMassEnd) ||
        !ParseFloat(text, "localBaseLiftMaxMeters",
                    value.shape.localBaseLiftMaxMeters) ||
        !ParseFloat(text, "footprintCoverageInfluence",
                    value.shape.footprintCoverageInfluence) ||
        !ParseFloat(text, "cirrusFlowDirectionX",
                    value.shape.cirrusFlowDirectionXZ.x) ||
        !ParseFloat(text, "cirrusFlowDirectionZ",
                    value.shape.cirrusFlowDirectionXZ.y) ||
        !ParseFloat(text, "cirrusBaseAlongScaleMeters",
                    value.shape.cirrusBaseAlongScaleMeters) ||
        !ParseFloat(text, "cirrusBaseAcrossScaleMeters",
                    value.shape.cirrusBaseAcrossScaleMeters) ||
        !ParseFloat(text, "cirrusBaseVerticalScaleMeters",
                    value.shape.cirrusBaseVerticalScaleMeters) ||
        !ParseFloat(text, "cirrusDetailAlongScaleMeters",
                    value.shape.cirrusDetailAlongScaleMeters) ||
        !ParseFloat(text, "cirrusDetailAcrossScaleMeters",
                    value.shape.cirrusDetailAcrossScaleMeters) ||
        !ParseFloat(text, "cirrusDetailVerticalScaleMeters",
                    value.shape.cirrusDetailVerticalScaleMeters) ||
        !ParseFloat(text, "cirrusMinimumThicknessMeters",
                    value.shape.cirrusMinimumThicknessMeters) ||
        !ParseFloat(text, "cirrusMaximumThicknessMeters",
                    value.shape.cirrusMaximumThicknessMeters) ||
        !ParseFloat(text, "cirrusVerticalProfileCenter",
                    value.shape.cirrusVerticalProfileCenter) ||
        !ParseFloat(text, "cirrusVerticalProfileHalfWidth",
                    value.shape.cirrusVerticalProfileHalfWidth) ||
        !ParseFloat(text, "domainBottomMeters", value.domainBottomMeters) ||
        !ParseFloat(text, "domainThicknessMeters",
                    value.domainThicknessMeters) ||
        !ParseFloat(text, "maximumViewTraceDistanceMeters",
                    value.maximumViewTraceDistanceMeters) ||
        !ParseFloat(text, "viewTraceFadeStartDistanceMeters",
                    value.viewTraceFadeStartDistanceMeters) ||
        !ParseFloat(text, "maximumLightTraceDistanceMeters",
                    value.maximumLightTraceDistanceMeters) ||
        !ParseFloat(text, "baseNoiseWorldSizeMeters",
                    value.baseNoiseWorldSizeMeters) ||
        !ParseFloat(text, "baseNoiseVerticalWorldSizeMeters",
                    value.baseNoiseVerticalWorldSizeMeters) ||
        !ParseFloat(text, "detailNoiseWorldSizeMeters",
                    value.detailNoiseWorldSizeMeters) ||
        !ParseFloat(text, "windDirectionX", value.windDirection.x) ||
        !ParseFloat(text, "windDirectionY", value.windDirection.y) ||
        !ParseFloat(text, "windDirectionZ", value.windDirection.z) ||
        !ParseFloat(text, "windSpeedMetersPerSecond",
                    value.windSpeedMetersPerSecond))
    {
        return false;
    }
    value.weatherPreset = static_cast<Stage5WeatherPreset>(weatherPreset);
    value.weather.cloudTypeMode = static_cast<CloudTypeMode>(cloudTypeMode);
    value.shape.shapeMode = shapeMode;
    return true;
}

void WriteChannel(std::ostream& stream, const char* prefix,
                  const PeriodicChannelSettings& value)
{
    stream << "    \"" << prefix << "Seed\": " << value.seed << ",\n"
           << "    \"" << prefix << "MacroPeriod\": "
           << value.macroPeriod << ",\n"
           << "    \"" << prefix << "DetailPeriod\": "
           << value.detailPeriod << ",\n"
           << "    \"" << prefix << "DetailWeight\": "
           << value.detailWeight << ",\n"
           << "    \"" << prefix << "Bias\": " << value.bias << ",\n"
           << "    \"" << prefix << "Contrast\": "
           << value.contrast << ",\n";
}

void WriteFormation(std::ostream& stream, const CloudFormationSettings& value)
{
    stream << std::setprecision(std::numeric_limits<float>::max_digits10)
           << std::defaultfloat
           << "    \"coverage\": " << value.coverage << ",\n"
           << "    \"densityMultiplier\": " << value.densityMultiplier << ",\n"
           << "    \"extinctionPerMeter\": " << value.extinctionPerMeter << ",\n"
           << "    \"detailErosion\": " << value.detailErosion << ",\n"
           << "    \"weatherPreset\": "
           << static_cast<std::uint32_t>(value.weatherPreset) << ",\n";
    WriteChannel(stream, "weatherCoverage", value.weather.coverage);
    WriteChannel(stream, "weatherCloudType", value.weather.cloudType);
    WriteChannel(stream, "weatherDensity", value.weather.density);
    WriteChannel(stream, "weatherLocalThickness",
                 value.weather.localThickness);
    stream << "    \"weatherCoverageThreshold\": "
           << value.weather.coverageThreshold << ",\n"
           << "    \"weatherCoverageSoftness\": "
           << value.weather.coverageSoftness << ",\n"
           << "    \"weatherDensityCoverageInfluence\": "
           << value.weather.densityCoverageInfluence << ",\n"
           << "    \"weatherThicknessCoverageInfluence\": "
           << value.weather.thicknessCoverageInfluence << ",\n"
           << "    \"weatherCloudTypeMode\": "
           << static_cast<std::uint32_t>(value.weather.cloudTypeMode) << ",\n"
           << "    \"weatherWorldSizeMeters\": "
           << value.weatherWorldSizeMeters << ",\n"
           << "    \"shapeMode\": " << value.shape.shapeMode << ",\n"
           << "    \"stratusMinimumThicknessMeters\": "
           << value.shape.stratusMinimumThicknessMeters << ",\n"
           << "    \"stratusMaximumThicknessMeters\": "
           << value.shape.stratusMaximumThicknessMeters << ",\n"
           << "    \"cumulusMinimumThicknessMeters\": "
           << value.shape.cumulusMinimumThicknessMeters << ",\n"
           << "    \"cumulusMaximumThicknessMeters\": "
           << value.shape.cumulusMaximumThicknessMeters << ",\n"
           << "    \"stratusBottomFadeEnd\": "
           << value.shape.stratusBottomFadeEnd << ",\n"
           << "    \"stratusTopFadeStart\": "
           << value.shape.stratusTopFadeStart << ",\n"
           << "    \"mixedBottomFadeEnd\": "
           << value.shape.mixedBottomFadeEnd << ",\n"
           << "    \"mixedTopFadeStart\": "
           << value.shape.mixedTopFadeStart << ",\n"
           << "    \"cumulusBottomFadeEnd\": "
           << value.shape.cumulusBottomFadeEnd << ",\n"
           << "    \"cumulusTopFadeStart\": "
           << value.shape.cumulusTopFadeStart << ",\n"
           << "    \"cumulusUpperMassBottom\": "
           << value.shape.cumulusUpperMassBottom << ",\n"
           << "    \"cumulusUpperMassStart\": "
           << value.shape.cumulusUpperMassStart << ",\n"
           << "    \"cumulusUpperMassEnd\": "
           << value.shape.cumulusUpperMassEnd << ",\n"
           << "    \"localBaseLiftMaxMeters\": "
           << value.shape.localBaseLiftMaxMeters << ",\n"
           << "    \"footprintCoverageInfluence\": "
           << value.shape.footprintCoverageInfluence << ",\n"
           << "    \"cirrusFlowDirectionX\": "
           << value.shape.cirrusFlowDirectionXZ.x << ",\n"
           << "    \"cirrusFlowDirectionZ\": "
           << value.shape.cirrusFlowDirectionXZ.y << ",\n"
           << "    \"cirrusBaseAlongScaleMeters\": "
           << value.shape.cirrusBaseAlongScaleMeters << ",\n"
           << "    \"cirrusBaseAcrossScaleMeters\": "
           << value.shape.cirrusBaseAcrossScaleMeters << ",\n"
           << "    \"cirrusBaseVerticalScaleMeters\": "
           << value.shape.cirrusBaseVerticalScaleMeters << ",\n"
           << "    \"cirrusDetailAlongScaleMeters\": "
           << value.shape.cirrusDetailAlongScaleMeters << ",\n"
           << "    \"cirrusDetailAcrossScaleMeters\": "
           << value.shape.cirrusDetailAcrossScaleMeters << ",\n"
           << "    \"cirrusDetailVerticalScaleMeters\": "
           << value.shape.cirrusDetailVerticalScaleMeters << ",\n"
           << "    \"cirrusMinimumThicknessMeters\": "
           << value.shape.cirrusMinimumThicknessMeters << ",\n"
           << "    \"cirrusMaximumThicknessMeters\": "
           << value.shape.cirrusMaximumThicknessMeters << ",\n"
           << "    \"cirrusVerticalProfileCenter\": "
           << value.shape.cirrusVerticalProfileCenter << ",\n"
           << "    \"cirrusVerticalProfileHalfWidth\": "
           << value.shape.cirrusVerticalProfileHalfWidth << ",\n"
           << "    \"domainBottomMeters\": "
           << value.domainBottomMeters << ",\n"
           << "    \"domainThicknessMeters\": "
           << value.domainThicknessMeters << ",\n"
           << "    \"maximumViewTraceDistanceMeters\": "
           << value.maximumViewTraceDistanceMeters << ",\n"
           << "    \"viewTraceFadeStartDistanceMeters\": "
           << value.viewTraceFadeStartDistanceMeters << ",\n"
           << "    \"maximumLightTraceDistanceMeters\": "
           << value.maximumLightTraceDistanceMeters << ",\n"
           << "    \"baseNoiseWorldSizeMeters\": "
           << value.baseNoiseWorldSizeMeters << ",\n"
           << "    \"baseNoiseVerticalWorldSizeMeters\": "
           << value.baseNoiseVerticalWorldSizeMeters << ",\n"
           << "    \"detailNoiseWorldSizeMeters\": "
           << value.detailNoiseWorldSizeMeters << ",\n"
           << "    \"windDirectionX\": " << value.windDirection.x << ",\n"
           << "    \"windDirectionY\": " << value.windDirection.y << ",\n"
           << "    \"windDirectionZ\": " << value.windDirection.z << ",\n"
           << "    \"windSpeedMetersPerSecond\": "
           << value.windSpeedMetersPerSecond << "\n";
}
}

CloudFormationPresetTarget ConceptFormationTarget(
    CloudFormationConcept concept)
{
    return { CloudFormationPresetGroup::Concept,
             static_cast<std::uint32_t>(concept) };
}

CloudFormationPresetTarget TypeFormationTarget(CloudFormationType type)
{
    return { CloudFormationPresetGroup::Type,
             static_cast<std::uint32_t>(type) };
}

CloudFormationPresetTarget CustomFormationTarget()
{
    return { CloudFormationPresetGroup::Custom, 0u };
}

CloudFormationPresetTarget NoFormationTarget()
{
    return { CloudFormationPresetGroup::None, 0u };
}

bool IsValidCloudFormationPresetTarget(
    const CloudFormationPresetTarget& target)
{
    switch (target.group)
    {
    case CloudFormationPresetGroup::Concept:
        return target.index <= static_cast<std::uint32_t>(
            CloudFormationConcept::SnowOvercast);
    case CloudFormationPresetGroup::Type:
        return target.index <= static_cast<std::uint32_t>(
            CloudFormationType::Cirrus);
    case CloudFormationPresetGroup::Custom:
        return target.index == 0u;
    case CloudFormationPresetGroup::None:
        return false;
    }
    return false;
}

bool CloudFormationPresetTargetEqual(
    const CloudFormationPresetTarget& a,
    const CloudFormationPresetTarget& b)
{
    return a.group == b.group && a.index == b.index;
}

const char* CloudFormationPresetTargetName(
    const CloudFormationPresetTarget& target)
{
    if (target.group == CloudFormationPresetGroup::None)
        return "None";
    if (!IsValidCloudFormationPresetTarget(target))
        return "Invalid";
    if (target.group == CloudFormationPresetGroup::Custom)
        return "Custom";
    if (target.group == CloudFormationPresetGroup::Concept)
    {
        switch (static_cast<CloudFormationConcept>(target.index))
        {
        case CloudFormationConcept::UrbanFairWeather:
            return "Urban Fair Weather";
        case CloudFormationConcept::MeadowBrokenClouds:
            return "Meadow Broken Clouds";
        case CloudFormationConcept::DesertCirrus:
            return "Desert Cirrus";
        case CloudFormationConcept::SnowOvercast:
            return "Snow Overcast";
        }
    }
    switch (static_cast<CloudFormationType>(target.index))
    {
    case CloudFormationType::Stratus: return "Stratus";
    case CloudFormationType::Cumulus: return "Cumulus";
    case CloudFormationType::Cirrus: return "Cirrus";
    }
    return "Invalid";
}

const char* CloudFormationPresetGroupName(CloudFormationPresetGroup group)
{
    switch (group)
    {
    case CloudFormationPresetGroup::Concept: return "F4";
    case CloudFormationPresetGroup::Type: return "F1";
    case CloudFormationPresetGroup::Custom: return "Custom";
    case CloudFormationPresetGroup::None: return "None";
    }
    return "Invalid";
}

const char* CloudFormationPresetSourceName(CloudFormationPresetSource source)
{
    switch (source)
    {
    case CloudFormationPresetSource::BuiltIn: return "Built-in";
    case CloudFormationPresetSource::UserOverride: return "User Override";
    case CloudFormationPresetSource::Unsaved: return "Unsaved";
    }
    return "Unsaved";
}

bool CloudFormationCanSaveToPreset(
    const CloudFormationPresetTarget& target, bool targetValid)
{
    return targetValid && IsValidCloudFormationPresetTarget(target) &&
        target.group != CloudFormationPresetGroup::Custom;
}

bool CloudFormationCanRestoreBuiltIn(
    const CloudFormationPresetTarget& target, bool targetValid,
    CloudFormationPresetSource source)
{
    return CloudFormationCanSaveToPreset(target, targetValid) &&
        source == CloudFormationPresetSource::UserOverride;
}

std::filesystem::path DefaultCloudFormationPresetRoot()
{
    return std::filesystem::path("captures") / "noise-lab" /
        "cloud-presets";
}

std::filesystem::path CloudFormationPresetPath(
    const std::filesystem::path& root,
    const CloudFormationPresetTarget& target)
{
    if (!IsValidCloudFormationPresetTarget(target))
        return {};
    if (target.group == CloudFormationPresetGroup::Custom)
        return root / "custom.json";
    if (target.group == CloudFormationPresetGroup::Concept)
        return root / "concepts" / (std::string(TargetId(target)) + ".json");
    return root / "types" / (std::string(TargetId(target)) + ".json");
}

bool ResolveBuiltInCloudFormation(
    CloudFormationConcept concept, CloudFormationSettings& outSettings)
{
    CloudFormationSettings value = BasePhysicalFormation();
    switch (concept)
    {
    case CloudFormationConcept::UrbanFairWeather:
        value.coverage = 0.38f;
        value.densityMultiplier = 1.10f;
        value.extinctionPerMeter = 0.00036f;
        value.detailErosion = 0.24f;
        value.weather.cloudTypeMode = CloudTypeMode::Cumulus;
        value.weather.coverage = Channel(1013u, 4u, 11u, 0.42f, -0.02f, 1.15f);
        value.weather.cloudType = Channel(2017u, 2u, 4u, 0.20f, 0.20f, 0.85f);
        value.weather.density = Channel(3019u, 3u, 6u, 0.25f, 0.0f, 0.75f);
        value.weather.localThickness = Channel(4021u, 2u, 5u, 0.20f, 0.0f, 0.90f);
        value.weather.coverageThreshold = 0.53f;
        value.weather.coverageSoftness = 0.14f;
        value.shape.cumulusMinimumThicknessMeters = 2000.0f;
        value.shape.cumulusMaximumThicknessMeters = 3200.0f;
        value.shape.cumulusTopFadeStart = 0.94f;
        value.shape.localBaseLiftMaxMeters = 300.0f;
        value.shape.footprintCoverageInfluence = 0.50f;
        value.domainBottomMeters = 1800.0f;
        value.domainThicknessMeters = 3700.0f;
        break;
    case CloudFormationConcept::MeadowBrokenClouds:
        value.coverage = 0.64f;
        value.densityMultiplier = 1.20f;
        value.extinctionPerMeter = 0.00039f;
        value.detailErosion = 0.20f;
        value.weather.cloudTypeMode = CloudTypeMode::WeatherMap;
        value.weather.coverage = Channel(1103u, 3u, 9u, 0.38f, 0.0f, 1.05f);
        value.weather.cloudType = Channel(2101u, 2u, 5u, 0.28f, 0.08f, 1.0f);
        value.weather.density = Channel(3109u, 3u, 7u, 0.30f, 0.05f, 0.90f);
        value.weather.localThickness = Channel(4103u, 2u, 6u, 0.32f, 0.05f, 1.05f);
        value.weather.coverageThreshold = 0.485f;
        value.weather.coverageSoftness = 0.18f;
        value.shape.stratusMinimumThicknessMeters = 1500.0f;
        value.shape.stratusMaximumThicknessMeters = 2500.0f;
        value.shape.cumulusMinimumThicknessMeters = 3000.0f;
        value.shape.cumulusMaximumThicknessMeters = 4600.0f;
        value.shape.localBaseLiftMaxMeters = 200.0f;
        value.shape.footprintCoverageInfluence = 0.40f;
        value.domainBottomMeters = 1500.0f;
        value.domainThicknessMeters = 5000.0f;
        break;
    case CloudFormationConcept::DesertCirrus:
        value.coverage = 0.42f;
        value.densityMultiplier = 0.40f;
        value.extinctionPerMeter = 0.00010f;
        value.detailErosion = 0.22f;
        value.weather.cloudTypeMode = CloudTypeMode::WeatherMap;
        value.weather.coverage = Channel(1201u, 2u, 8u, 0.30f, -0.04f, 1.20f);
        value.weather.cloudType = Channel(2203u, 2u, 4u, 0.20f, 0.0f, 0.85f);
        value.weather.density = Channel(3203u, 2u, 6u, 0.22f, -0.10f, 0.80f);
        value.weather.localThickness = Channel(4201u, 2u, 5u, 0.18f, -0.05f, 0.75f);
        value.weather.coverageThreshold = 0.52f;
        value.weather.coverageSoftness = 0.13f;
        value.weatherWorldSizeMeters = 128000.0f;
        value.shape.shapeMode = static_cast<std::uint32_t>(
            CloudShapeMode::CirrusPhysicalLayer);
        value.shape.localBaseLiftMaxMeters = 0.0f;
        value.shape.footprintCoverageInfluence = 0.0f;
        value.domainBottomMeters = 7000.0f;
        value.domainThicknessMeters = 3500.0f;
        value.maximumViewTraceDistanceMeters = 60000.0f;
        value.viewTraceFadeStartDistanceMeters = 50000.0f;
        break;
    case CloudFormationConcept::SnowOvercast:
        value.coverage = 0.90f;
        value.densityMultiplier = 1.25f;
        value.extinctionPerMeter = 0.00046f;
        value.detailErosion = 0.10f;
        value.weather.cloudTypeMode = CloudTypeMode::Stratus;
        value.weather.coverage = Channel(1301u, 2u, 6u, 0.20f, 0.10f, 0.85f);
        value.weather.cloudType = Channel(2309u, 2u, 4u, 0.15f, -0.30f, 0.60f);
        value.weather.density = Channel(3301u, 2u, 5u, 0.18f, 0.05f, 0.75f);
        value.weather.localThickness = Channel(4303u, 2u, 4u, 0.15f, 0.0f, 0.70f);
        value.weather.coverageThreshold = 0.46f;
        value.weather.coverageSoftness = 0.20f;
        value.shape.stratusMinimumThicknessMeters = 1500.0f;
        value.shape.stratusMaximumThicknessMeters = 2300.0f;
        value.shape.stratusBottomFadeEnd = 0.05f;
        value.shape.stratusTopFadeStart = 0.72f;
        value.shape.localBaseLiftMaxMeters = 0.0f;
        value.shape.footprintCoverageInfluence = 0.20f;
        value.domainBottomMeters = 1500.0f;
        value.domainThicknessMeters = 2500.0f;
        break;
    default:
        return false;
    }
    return FinalizeBuiltIn(value, outSettings);
}

bool ResolveBuiltInCloudFormation(
    CloudFormationType type, CloudFormationSettings& outSettings)
{
    // 아래 값은 해당 F4 resolver 호출 결과가 아니다. Stage 15B 도입 시점의
    // Snow/Urban/Desert formation을 복사한 독립 F1 기본값이다.
    CloudFormationSettings value = BasePhysicalFormation();
    switch (type)
    {
    case CloudFormationType::Stratus:
        value.coverage = 0.90f;
        value.densityMultiplier = 1.25f;
        value.extinctionPerMeter = 0.00046f;
        value.detailErosion = 0.10f;
        value.weather.cloudTypeMode = CloudTypeMode::Stratus;
        value.weather.coverage = Channel(1301u, 2u, 6u, 0.20f, 0.10f, 0.85f);
        value.weather.cloudType = Channel(2309u, 2u, 4u, 0.15f, -0.30f, 0.60f);
        value.weather.density = Channel(3301u, 2u, 5u, 0.18f, 0.05f, 0.75f);
        value.weather.localThickness = Channel(4303u, 2u, 4u, 0.15f, 0.0f, 0.70f);
        value.weather.coverageThreshold = 0.46f;
        value.weather.coverageSoftness = 0.20f;
        value.shape.stratusMinimumThicknessMeters = 1500.0f;
        value.shape.stratusMaximumThicknessMeters = 2300.0f;
        value.shape.stratusBottomFadeEnd = 0.05f;
        value.shape.stratusTopFadeStart = 0.72f;
        value.shape.localBaseLiftMaxMeters = 0.0f;
        value.shape.footprintCoverageInfluence = 0.20f;
        value.domainBottomMeters = 1500.0f;
        value.domainThicknessMeters = 2500.0f;
        break;
    case CloudFormationType::Cumulus:
        value.coverage = 0.38f;
        value.densityMultiplier = 1.10f;
        value.extinctionPerMeter = 0.00036f;
        value.detailErosion = 0.24f;
        value.weather.cloudTypeMode = CloudTypeMode::Cumulus;
        value.weather.coverage = Channel(1013u, 4u, 11u, 0.42f, -0.02f, 1.15f);
        value.weather.cloudType = Channel(2017u, 2u, 4u, 0.20f, 0.20f, 0.85f);
        value.weather.density = Channel(3019u, 3u, 6u, 0.25f, 0.0f, 0.75f);
        value.weather.localThickness = Channel(4021u, 2u, 5u, 0.20f, 0.0f, 0.90f);
        value.weather.coverageThreshold = 0.53f;
        value.weather.coverageSoftness = 0.14f;
        value.shape.cumulusMinimumThicknessMeters = 2000.0f;
        value.shape.cumulusMaximumThicknessMeters = 3200.0f;
        value.shape.cumulusTopFadeStart = 0.94f;
        value.shape.localBaseLiftMaxMeters = 300.0f;
        value.shape.footprintCoverageInfluence = 0.50f;
        value.domainBottomMeters = 1800.0f;
        value.domainThicknessMeters = 3700.0f;
        break;
    case CloudFormationType::Cirrus:
        value.coverage = 0.42f;
        value.densityMultiplier = 0.40f;
        value.extinctionPerMeter = 0.00010f;
        value.detailErosion = 0.22f;
        value.weather.cloudTypeMode = CloudTypeMode::WeatherMap;
        value.weather.coverage = Channel(1201u, 2u, 8u, 0.30f, -0.04f, 1.20f);
        value.weather.cloudType = Channel(2203u, 2u, 4u, 0.20f, 0.0f, 0.85f);
        value.weather.density = Channel(3203u, 2u, 6u, 0.22f, -0.10f, 0.80f);
        value.weather.localThickness = Channel(4201u, 2u, 5u, 0.18f, -0.05f, 0.75f);
        value.weather.coverageThreshold = 0.52f;
        value.weather.coverageSoftness = 0.13f;
        value.weatherWorldSizeMeters = 128000.0f;
        value.shape.shapeMode = static_cast<std::uint32_t>(
            CloudShapeMode::CirrusPhysicalLayer);
        value.shape.localBaseLiftMaxMeters = 0.0f;
        value.shape.footprintCoverageInfluence = 0.0f;
        value.domainBottomMeters = 7000.0f;
        value.domainThicknessMeters = 3500.0f;
        value.maximumViewTraceDistanceMeters = 60000.0f;
        value.viewTraceFadeStartDistanceMeters = 50000.0f;
        break;
    default:
        return false;
    }
    return FinalizeBuiltIn(value, outSettings);
}

bool ResolveBuiltInCloudFormation(
    const CloudFormationPresetTarget& target,
    CloudFormationSettings& outSettings)
{
    if (!IsValidCloudFormationPresetTarget(target))
        return false;
    if (target.group == CloudFormationPresetGroup::Concept)
    {
        return ResolveBuiltInCloudFormation(
            static_cast<CloudFormationConcept>(target.index), outSettings);
    }
    if (target.group == CloudFormationPresetGroup::Type)
    {
        return ResolveBuiltInCloudFormation(
            static_cast<CloudFormationType>(target.index), outSettings);
    }
    return false;
}

bool SaveCloudFormationPresetAtomic(
    const std::filesystem::path& path,
    const CloudFormationPresetTarget& target,
    const CloudFormationSettings& settings, std::string& status)
{
    if (path.empty() || !IsValidCloudFormationPresetTarget(target))
    {
        status = "Formation save rejected: invalid preset target";
        return false;
    }
    PreparedCloudFormation prepared;
    if (!PrepareCloudFormationSettings(settings, prepared, status))
        return false;

    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
    {
        status = "Formation save failed: cannot create preset directory";
        return false;
    }
    const std::filesystem::path temporary = path.wstring() + L".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            status = "Formation save failed: cannot open temporary file";
            return false;
        }
        file << "{\n"
             << "  \"schemaVersion\": 1,\n"
             << "  \"presetGroup\": \""
             << (target.group == CloudFormationPresetGroup::Concept
                    ? "concept" :
                (target.group == CloudFormationPresetGroup::Type
                    ? "type" : "custom")) << "\",\n"
             << "  \"presetId\": \"" << TargetId(target) << "\",\n"
             << "  \"cloudFormation\": {\n";
        WriteFormation(file, prepared.settings);
        file << "  }\n}\n";
        file.flush();
        if (!file)
        {
            file.close();
            std::filesystem::remove(temporary, error);
            status = "Formation save failed: incomplete temporary file";
            return false;
        }
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        std::filesystem::remove(temporary, error);
        status = "Formation save failed: atomic replace failed";
        return false;
    }
    status = std::string(CloudFormationPresetTargetName(target)) +
        " formation saved";
    return true;
}

bool LoadCloudFormationPreset(
    const std::filesystem::path& path,
    const CloudFormationPresetTarget& expectedTarget,
    CloudFormationSettings& outSettings, std::string& status)
{
    if (path.empty() || !IsValidCloudFormationPresetTarget(expectedTarget))
    {
        status = "Formation load rejected: invalid preset target";
        return false;
    }
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (error)
    {
        status = "Formation override rejected: cannot inspect file";
        return false;
    }
    if (!exists)
    {
        status = expectedTarget.group == CloudFormationPresetGroup::Custom
            ? "No saved Custom"
            : "No saved formation override";
        return false;
    }
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error)
    {
        status = "Formation override rejected: cannot inspect file";
        return false;
    }
    if (size == 0u || size > kMaximumPresetFileBytes)
    {
        status = "Formation override rejected: invalid file size";
        return false;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        status = "Formation override rejected: cannot open file";
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();
    std::uint32_t schemaVersion = 0;
    std::string group;
    std::string id;
    const char* expectedGroup =
        expectedTarget.group == CloudFormationPresetGroup::Concept
            ? "concept" :
        (expectedTarget.group == CloudFormationPresetGroup::Type
            ? "type" : "custom");
    if (!ParseUnsigned(text, "schemaVersion", schemaVersion) ||
        schemaVersion != 1u ||
        !ParseString(text, "presetGroup", group) ||
        group != expectedGroup ||
        !ParseString(text, "presetId", id) || id != TargetId(expectedTarget))
    {
        status = "Formation override rejected: schema or slot mismatch";
        return false;
    }

    CloudFormationSettings parsed;
    if (!ParseFormation(text, parsed))
    {
        status = "Formation override rejected: missing or malformed field";
        return false;
    }
    PreparedCloudFormation prepared;
    if (!PrepareCloudFormationSettings(parsed, prepared, status))
    {
        status = "Formation override " + status;
        return false;
    }
    outSettings = prepared.settings;
    status = std::string(CloudFormationPresetTargetName(expectedTarget)) +
        " user override loaded";
    return true;
}

bool ResolveCloudFormationPreset(
    const std::filesystem::path& root,
    const CloudFormationPresetTarget& target, bool allowUserOverrides,
    CloudFormationSettings& outSettings,
    CloudFormationPresetSource& outSource, std::string& status)
{
    if (!IsValidCloudFormationPresetTarget(target))
    {
        status = "Formation resolve rejected: invalid preset target";
        return false;
    }
    const std::filesystem::path path = CloudFormationPresetPath(root, target);
    if (allowUserOverrides)
    {
        CloudFormationSettings loaded;
        std::string loadStatus;
        if (LoadCloudFormationPreset(path, target, loaded, loadStatus))
        {
            outSettings = loaded;
            outSource = CloudFormationPresetSource::UserOverride;
            status = loadStatus;
            return true;
        }
        if (target.group == CloudFormationPresetGroup::Custom)
        {
            status = loadStatus;
            return false;
        }
        CloudFormationSettings builtIn;
        if (!ResolveBuiltInCloudFormation(target, builtIn))
        {
            status = "Formation resolve failed: no built-in preset";
            return false;
        }
        outSettings = builtIn;
        outSource = CloudFormationPresetSource::BuiltIn;
        status = loadStatus + "; built-in fallback applied";
        return true;
    }

    if (target.group == CloudFormationPresetGroup::Custom)
    {
        status = "Custom formation unavailable while user overrides are ignored";
        return false;
    }
    CloudFormationSettings builtIn;
    if (!ResolveBuiltInCloudFormation(target, builtIn))
    {
        status = "Formation resolve failed: no built-in preset";
        return false;
    }
    outSettings = builtIn;
    outSource = CloudFormationPresetSource::BuiltIn;
    status = std::string(CloudFormationPresetTargetName(target)) +
        " built-in loaded (user overrides ignored)";
    return true;
}

bool RemoveCloudFormationPresetOverride(
    const std::filesystem::path& root,
    const CloudFormationPresetTarget& target, std::string& status)
{
    if (!IsValidCloudFormationPresetTarget(target) ||
        target.group == CloudFormationPresetGroup::Custom)
    {
        status = "Restore rejected: only F1/F4 presets have built-in values";
        return false;
    }
    std::error_code error;
    const std::filesystem::path path = CloudFormationPresetPath(root, target);
    const bool removed = std::filesystem::remove(path, error);
    if (error)
    {
        status = "Restore failed: cannot remove preset override";
        return false;
    }
    status = std::string(CloudFormationPresetTargetName(target)) +
        (removed ? " override removed" : " already uses built-in values");
    return true;
}

CloudFormationSettings ConvertLegacyCloudAppearanceToFormation(
    const CloudAppearanceSettings& legacy,
    const CloudFormationSettings& baseFormation)
{
    CloudFormationSettings result = baseFormation;
    result.coverage = legacy.globalCoverage;
    result.densityMultiplier = legacy.densityMultiplier;
    result.extinctionPerMeter = legacy.extinctionPerMeter;
    result.detailErosion = legacy.detailErosion;
    result.weather.cloudTypeMode = legacy.cloudTypeMode;
    result.weather.coverageThreshold = legacy.weatherThreshold;
    result.weather.coverageSoftness = legacy.weatherSoftness;
    result.weather.coverage.bias = legacy.coverageBias;
    result.weather.coverage.contrast = legacy.coverageContrast;
    result.weather.densityCoverageInfluence = legacy.densityCoverageLink;
    result.weather.thicknessCoverageInfluence = legacy.thicknessCoverageLink;
    result.weather.cloudType.bias = legacy.cloudTypeBias;
    result.shape.shapeMode = static_cast<std::uint32_t>(
        CloudShapeMode::WeatherPhysicalThickness);
    result.shape.stratusMinimumThicknessMeters =
        legacy.stratusMinimumThicknessMeters;
    result.shape.stratusMaximumThicknessMeters =
        legacy.stratusMaximumThicknessMeters;
    result.shape.cumulusMinimumThicknessMeters =
        legacy.cumulusMinimumThicknessMeters;
    result.shape.cumulusMaximumThicknessMeters =
        legacy.cumulusMaximumThicknessMeters;
    result.shape.localBaseLiftMaxMeters = legacy.localBaseLiftMaxMeters;
    result.shape.footprintCoverageInfluence =
        legacy.footprintCoverageInfluence;
    result.shape.stratusBottomFadeEnd = legacy.stratusBottomFadeEnd;
    result.shape.stratusTopFadeStart = legacy.stratusTopFadeStart;
    result.shape.mixedBottomFadeEnd = legacy.mixedBottomFadeEnd;
    result.shape.mixedTopFadeStart = legacy.mixedTopFadeStart;
    result.shape.cumulusBottomFadeEnd = legacy.cumulusBottomFadeEnd;
    result.shape.cumulusTopFadeStart = legacy.cumulusTopFadeStart;
    result.shape.cumulusUpperMassBottom = legacy.cumulusUpperMassBottom;
    result.shape.cumulusUpperMassStart = legacy.cumulusUpperMassStart;
    result.shape.cumulusUpperMassEnd = legacy.cumulusUpperMassEnd;
    return SanitizeCloudFormationSettings(result);
}

LegacyCloudFormationMigrationResult MigrateLegacyCustomCloudFormation(
    const std::filesystem::path& legacyAppearancePath,
    const std::filesystem::path& newPresetRoot,
    const CloudFormationSettings& baseFormation, std::string& status)
{
    const CloudFormationPresetTarget custom = CustomFormationTarget();
    const std::filesystem::path customPath =
        CloudFormationPresetPath(newPresetRoot, custom);
    std::error_code error;
    const bool customExists = std::filesystem::exists(customPath, error);
    if (error)
    {
        status = "Custom formation migration skipped: cannot inspect custom.json";
        return LegacyCloudFormationMigrationResult::NotNeeded;
    }
    if (customExists)
    {
        status = "Custom formation migration skipped: custom.json exists";
        return LegacyCloudFormationMigrationResult::NotNeeded;
    }
    error.clear();
    if (!std::filesystem::exists(legacyAppearancePath, error) || error)
    {
        status = "Custom formation migration skipped: no legacy file";
        return LegacyCloudFormationMigrationResult::NoLegacyFile;
    }

    CloudAppearanceSettings legacy;
    std::string legacyStatus;
    if (!LoadCustomCloudAppearance(
            legacyAppearancePath, legacy, legacyStatus))
    {
        status = "Legacy Custom preserved but rejected: " + legacyStatus;
        return LegacyCloudFormationMigrationResult::Rejected;
    }
    CloudFormationSettings converted =
        ConvertLegacyCloudAppearanceToFormation(legacy, baseFormation);
    // schema 29/30에는 domain이 없었다. 새 F1 Cumulus(3.7 km)를 그대로
    // 빌리면 과거 Custom의 6 km 두께가 대부분 migration 단계에서 탈락한다.
    // Appearance 값은 손대지 않고, 새 snapshot이 200 m top 여유를 만족하는
    // 최소 domain만 파생해 기존 파일을 실제로 불러올 수 있게 한다.
    constexpr float kMigrationTopHeadroomMeters = 200.0f;
    const float requiredDomainThickness =
        cloudshapedomain::ActiveMaximumThicknessMeters(
            converted.shape, converted.weather.cloudTypeMode) +
        converted.shape.localBaseLiftMaxMeters +
        kMigrationTopHeadroomMeters;
    if (std::isfinite(requiredDomainThickness))
    {
        converted.domainThicknessMeters = std::max(
            converted.domainThicknessMeters, requiredDomainThickness);
    }
    PreparedCloudFormation prepared;
    if (!PrepareCloudFormationSettings(converted, prepared, status))
    {
        status = "Legacy Custom preserved but " + status;
        return LegacyCloudFormationMigrationResult::Rejected;
    }
    if (!SaveCloudFormationPresetAtomic(
            customPath, custom, prepared.settings, status))
    {
        return LegacyCloudFormationMigrationResult::SaveFailed;
    }
    status = "Legacy schema 29/30 Custom migrated to formation schema 1";
    return LegacyCloudFormationMigrationResult::Migrated;
}
