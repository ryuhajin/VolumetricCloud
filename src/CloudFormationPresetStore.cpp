// ============================================================================
//  CloudFormationPresetStore.cpp - 내장 formation과 단일 Custom JSON
// ============================================================================
#include "CloudFormationPresetStore.h"
#include "PresetJsonSyntax.h"
#include "PresetPaths.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cerrno>
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
    result.weather = WeatherMapDefinition{};
    result.shape = CloudShapeParameters{};
    result.weather.column.maximumBaseLiftMeters = 0.0f;
    result.shape.footprintCoverageInfluence = 0.20f;
    result.domainBottomMeters = 1800.0f;
    result.domainThicknessMeters = 3700.0f;
    result.maximumViewTraceDistanceMeters = 50000.0f;
    result.viewTraceFadeStartDistanceMeters = 40000.0f;
    result.maximumLightTraceDistanceMeters = 20000.0f;
    result.baseNoiseWorldSizeMeters = 12000.0f;
    result.baseNoiseVerticalWorldSizeMeters = 12000.0f;
    result.detailNoiseWorldSizeMeters = 2000.0f;
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
        case CloudFormationConcept::SnowOvercast:
            return "snow-overcast";
        }
    }
    switch (static_cast<CloudFormationType>(target.index))
    {
    case CloudFormationType::Stratus: return "stratus";
    case CloudFormationType::Cumulus: return "cumulus";
    case CloudFormationType::Mixed: return "mixed";
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
    if (text[start] < '0' || text[start] > '9') return false;
    errno = 0;
    const unsigned long parsed = std::strtoul(
        text.c_str() + start, &end, 10);
    if (end == text.c_str() + start || !IsJsonValueEnd(end) ||
        errno == ERANGE || parsed > std::numeric_limits<std::uint32_t>::max())
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

bool ParseFormation(const std::string& text, std::uint32_t schemaVersion,
                    CloudFormationSettings& value)
{
    std::uint32_t weatherPreset = 0;
    std::uint32_t cloudTypeMode = 0;
    if (!ParseFloat(text, "coverage", value.coverage) ||
        !ParseFloat(text, "densityMultiplier", value.densityMultiplier) ||
        !ParseFloat(text, "extinctionPerMeter", value.extinctionPerMeter) ||
        !ParseFloat(text, "detailErosion", value.detailErosion) ||
        !ParseUnsigned(text, "weatherPreset", weatherPreset) ||
        !ParseChannel(text, "weatherCoverage", value.weather.generator.coverage) ||
        !ParseChannel(text, "weatherDensity", value.weather.generator.density) ||
        !ParseChannel(text, "weatherLocalThickness",
                      value.weather.generator.localThickness) ||
        !ParseFloat(text, "weatherCoverageThreshold",
                    value.weather.generator.coverageThreshold) ||
        !ParseFloat(text, "weatherCoverageSoftness",
                    value.weather.generator.coverageSoftness) ||
        !ParseFloat(text, "weatherDensityCoverageInfluence",
                    value.weather.generator.densityCoverageInfluence) ||
        !ParseFloat(text, "weatherThicknessCoverageInfluence",
                    value.weather.generator.thicknessCoverageInfluence) ||
        !ParseFloat(text, "weatherWorldSizeMeters",
                    value.weather.worldSizeMeters) ||
        !ParseFloat(text, "localBaseLiftMaxMeters",
                    value.weather.column.maximumBaseLiftMeters) ||
        !ParseFloat(text, "footprintCoverageInfluence",
                    value.shape.footprintCoverageInfluence) ||
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
                    value.detailNoiseWorldSizeMeters))
    {
        return false;
    }
    value.shape.detailCoreProtection = 0.0f;
    if (text.find("\"detailCoreProtection\"") != std::string::npos &&
        !ParseFloat(text, "detailCoreProtection", value.shape.detailCoreProtection)) return false;
    value.shape.densityShaping = 0.0f;
    if (schemaVersion >= 3u &&
        !ParseFloat(text, "densityShaping", value.shape.densityShaping))
        return false;
    if (schemaVersion == 1u)
    {
        // schema 1 wind는 의도적으로 읽지 않는다. 세션 전역 motion을 보존한다.
        if (!ParseUnsigned(text, "weatherCloudTypeMode", cloudTypeMode))
            return false;
        value.typeSelection = MigrateLegacyCloudTypeMode(cloudTypeMode);
    }
    else
    {
        if (!ParseUnsigned(text, "cloudTypeSelection", cloudTypeMode))
            return false;
        value.typeSelection.mode =
            static_cast<CloudTypeSelectionMode>(cloudTypeMode);
    }
    if (cloudTypeMode > (schemaVersion < 4u ? 3u : 2u)) return false;
    value.typeSelection = MigrateLegacyCloudTypeMode(cloudTypeMode);
    if (schemaVersion < 4u)
    {
        // 과거 G 노이즈는 읽기 호환 검증만 하고 런타임으로 전달하지 않는다.
        PeriodicChannelSettings retiredG;
        if (!ParseChannel(text, "weatherCloudType", retiredG)) return false;
        if(retiredG.macroPeriod<1 || retiredG.macroPeriod>8 || retiredG.detailPeriod<2 || retiredG.detailPeriod>16 ||
           retiredG.detailWeight<0 || retiredG.detailWeight>1 || retiredG.bias<-.5f || retiredG.bias>.5f ||
           retiredG.contrast<.25f || retiredG.contrast>3.f) return false;
        float stratusMinimumThicknessMeters=0.f;
        if (!ParseFloat(text,"stratusMinimumThicknessMeters",stratusMinimumThicknessMeters)) return false;
        float stratusMaximumThicknessMeters=0.f;
        if (!ParseFloat(text,"stratusMaximumThicknessMeters",stratusMaximumThicknessMeters)) return false;
        float cumulusMinimumThicknessMeters=0.f;
        if (!ParseFloat(text,"cumulusMinimumThicknessMeters",cumulusMinimumThicknessMeters)) return false;
        float cumulusMaximumThicknessMeters=0.f;
        if (!ParseFloat(text,"cumulusMaximumThicknessMeters",cumulusMaximumThicknessMeters)) return false;
        float stratusBottomFadeEnd=0.f;
        if (!ParseFloat(text,"stratusBottomFadeEnd",stratusBottomFadeEnd)) return false;
        float stratusTopFadeStart=0.f;
        if (!ParseFloat(text,"stratusTopFadeStart",stratusTopFadeStart)) return false;
        float mixedBottomFadeEnd=0.f;
        if (!ParseFloat(text,"mixedBottomFadeEnd",mixedBottomFadeEnd)) return false;
        float mixedTopFadeStart=0.f;
        if (!ParseFloat(text,"mixedTopFadeStart",mixedTopFadeStart)) return false;
        float cumulusBottomFadeEnd=0.f;
        if (!ParseFloat(text,"cumulusBottomFadeEnd",cumulusBottomFadeEnd)) return false;
        float cumulusTopFadeStart=0.f;
        if (!ParseFloat(text,"cumulusTopFadeStart",cumulusTopFadeStart)) return false;
        float cumulusUpperMassBottom=0.f;
        if (!ParseFloat(text,"cumulusUpperMassBottom",cumulusUpperMassBottom)) return false;
        float cumulusUpperMassStart=0.f;
        if (!ParseFloat(text,"cumulusUpperMassStart",cumulusUpperMassStart)) return false;
        float cumulusUpperMassEnd=0.f;
        if (!ParseFloat(text,"cumulusUpperMassEnd",cumulusUpperMassEnd)) return false;

        if(stratusMinimumThicknessMeters<1.f || stratusMaximumThicknessMeters<stratusMinimumThicknessMeters ||
           cumulusMinimumThicknessMeters<stratusMinimumThicknessMeters || cumulusMaximumThicknessMeters<cumulusMinimumThicknessMeters ||
           cumulusMaximumThicknessMeters<stratusMaximumThicknessMeters || cumulusMaximumThicknessMeters>6000.f) return false;
        if(stratusBottomFadeEnd<.01f || stratusTopFadeStart<stratusBottomFadeEnd || stratusTopFadeStart>.99f ||
           mixedBottomFadeEnd<.01f || mixedTopFadeStart<mixedBottomFadeEnd || mixedTopFadeStart>.99f ||
           cumulusBottomFadeEnd<.01f || cumulusTopFadeStart<cumulusBottomFadeEnd || cumulusTopFadeStart>.99f ||
           cumulusUpperMassBottom<0.f || cumulusUpperMassBottom>1.f || cumulusUpperMassStart<0.f ||
           cumulusUpperMassEnd<cumulusUpperMassStart+.01f || cumulusUpperMassEnd>1.f) return false;
        float t=FixedCloudType(value.typeSelection);
        value.weather.column.minimumThicknessMeters=stratusMinimumThicknessMeters*(1.f-t)+cumulusMinimumThicknessMeters*t;
        value.weather.column.maximumThicknessMeters=stratusMaximumThicknessMeters*(1.f-t)+cumulusMaximumThicknessMeters*t;
        value.shape.bottomFadeEnd=t==0.f?stratusBottomFadeEnd:(t==1.f?cumulusBottomFadeEnd:mixedBottomFadeEnd);
        value.shape.topFadeStart=t==0.f?stratusTopFadeStart:(t==1.f?cumulusTopFadeStart:mixedTopFadeStart);
        value.shape.lowerDensityScale=t==1.f?cumulusUpperMassBottom:1.f;
        value.shape.upperTransitionStart=cumulusUpperMassStart;
        value.shape.upperTransitionEnd=cumulusUpperMassEnd;
    }
    else
    {
        if (!ParseFloat(text,"minimumThicknessMeters",value.weather.column.minimumThicknessMeters)) return false;
        if (!ParseFloat(text,"maximumThicknessMeters",value.weather.column.maximumThicknessMeters)) return false;
        if (!ParseFloat(text,"bottomFadeEnd",value.shape.bottomFadeEnd)) return false;
        if (!ParseFloat(text,"topFadeStart",value.shape.topFadeStart)) return false;
        if (!ParseFloat(text,"lowerDensityScale",value.shape.lowerDensityScale)) return false;
        if (!ParseFloat(text,"upperTransitionStart",value.shape.upperTransitionStart)) return false;
        if (!ParseFloat(text,"upperTransitionEnd",value.shape.upperTransitionEnd)) return false;
    }
    value.weatherPreset = static_cast<Stage5WeatherPreset>(weatherPreset);
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
    WriteChannel(stream, "weatherCoverage", value.weather.generator.coverage);
    WriteChannel(stream, "weatherDensity", value.weather.generator.density);
    WriteChannel(stream, "weatherLocalThickness",
                 value.weather.generator.localThickness);
    stream << "    \"weatherCoverageThreshold\": "
           << value.weather.generator.coverageThreshold << ",\n"
           << "    \"weatherCoverageSoftness\": "
           << value.weather.generator.coverageSoftness << ",\n"
           << "    \"weatherDensityCoverageInfluence\": "
           << value.weather.generator.densityCoverageInfluence << ",\n"
           << "    \"weatherThicknessCoverageInfluence\": "
           << value.weather.generator.thicknessCoverageInfluence << ",\n"
           << "    \"cloudTypeSelection\": "
           << static_cast<std::uint32_t>(value.typeSelection.mode) << ",\n"
           << "    \"weatherWorldSizeMeters\": "
           << value.weather.worldSizeMeters << ",\n"
           << "    \"minimumThicknessMeters\": " << value.weather.column.minimumThicknessMeters << ",\n"
           << "    \"maximumThicknessMeters\": " << value.weather.column.maximumThicknessMeters << ",\n"
           << "    \"bottomFadeEnd\": " << value.shape.bottomFadeEnd << ",\n"
           << "    \"topFadeStart\": " << value.shape.topFadeStart << ",\n"
           << "    \"lowerDensityScale\": " << value.shape.lowerDensityScale << ",\n"
           << "    \"upperTransitionStart\": " << value.shape.upperTransitionStart << ",\n"
           << "    \"upperTransitionEnd\": " << value.shape.upperTransitionEnd << ",\n"
           << "    \"localBaseLiftMaxMeters\": "
           << value.weather.column.maximumBaseLiftMeters << ",\n"
           << "    \"footprintCoverageInfluence\": "
           << value.shape.footprintCoverageInfluence << ",\n"
           << "    \"detailCoreProtection\": " << value.shape.detailCoreProtection << ",\n"
           << "    \"densityShaping\": " << value.shape.densityShaping << ",\n"
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
           << value.detailNoiseWorldSizeMeters << "\n";
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
            CloudFormationType::Mixed);
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
        case CloudFormationConcept::SnowOvercast:
            return "Snow Overcast";
        }
    }
    switch (static_cast<CloudFormationType>(target.index))
    {
    case CloudFormationType::Stratus: return "Stratus";
    case CloudFormationType::Cumulus: return "Cumulus";
    case CloudFormationType::Mixed: return "Altocumulus";
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
    case CloudFormationPresetSource::UserOverride: return "Saved";
    case CloudFormationPresetSource::Unsaved: return "Modified";
    }
    return "Modified";
}

bool CloudFormationCanSaveToPreset(
    const CloudFormationPresetTarget& target, bool targetValid)
{
    return targetValid && IsValidCloudFormationPresetTarget(target) &&
        (target.group == CloudFormationPresetGroup::Custom || target.group == CloudFormationPresetGroup::Type);
}

std::filesystem::path DefaultCloudFormationPresetRoot()
{
    std::wstring executable(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(),
        static_cast<DWORD>(executable.size()));
    if (length == 0 || length >= executable.size())
        throw std::runtime_error("Cannot resolve executable directory for presets");
    executable.resize(length);
    std::filesystem::path source;
#ifdef VCLOUD_PRESET_SOURCE_DIR
    source = std::filesystem::u8path(VCLOUD_PRESET_SOURCE_DIR);
#endif
    return SelectPresetRoot(source, std::filesystem::path(executable).parent_path());
}

std::filesystem::path DefaultNoiseLabOutputRoot()
{
    return std::filesystem::path("captures") / "noise-lab";
}

std::filesystem::path CloudFormationPresetPath(
    const std::filesystem::path& root,
    const CloudFormationPresetTarget& target)
{
    if (!IsValidCloudFormationPresetTarget(target))
        return {};
    if (target.group == CloudFormationPresetGroup::Custom)
        return root / "custom-cloud.json";
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
        value.shape.densityShaping = 0.70f; // 02 사용자 승인.
        value.densityMultiplier = 1.10f;
        value.extinctionPerMeter = 0.00036f;
        value.detailErosion = 0.24f;
        value.typeSelection.mode = CloudTypeSelectionMode::FixedCumulus;
        value.weather.generator.coverage = Channel(1013u, 4u, 11u, 0.42f, -0.02f, 1.15f);
        value.weather.generator.density = Channel(3019u, 3u, 6u, 0.25f, 0.0f, 0.75f);
        value.weather.generator.localThickness = Channel(4021u, 2u, 5u, 0.20f, 0.0f, 0.90f);
        value.weather.generator.coverageThreshold = 0.53f;
        value.weather.generator.coverageSoftness = 0.14f;
        value.weather.column.maximumBaseLiftMeters = 300.0f;
        value.shape.footprintCoverageInfluence = 0.50f;
        value.domainBottomMeters = 1800.0f;
        value.domainThicknessMeters = 3700.0f;
        value.weather.column.minimumThicknessMeters = 2000.000000f;
        value.weather.column.maximumThicknessMeters = 3200.000000f;
        value.shape.bottomFadeEnd = 0.080000f;
        value.shape.topFadeStart = 0.940000f;
        value.shape.lowerDensityScale = 0.650000f;
        break;
    case CloudFormationConcept::MeadowBrokenClouds:
        value.coverage = 0.64f;
        value.densityMultiplier = 1.20f;
        value.extinctionPerMeter = 0.00039f;
        value.detailErosion = 0.20f;
        value.typeSelection.mode = CloudTypeSelectionMode::FixedMixed;
        value.weather.generator.coverage = Channel(1103u, 3u, 9u, 0.38f, 0.0f, 1.05f);
        value.weather.generator.density = Channel(3109u, 3u, 7u, 0.30f, 0.05f, 0.90f);
        value.weather.generator.localThickness = Channel(4103u, 2u, 6u, 0.32f, 0.05f, 1.05f);
        value.weather.generator.coverageThreshold = 0.485f;
        value.weather.generator.coverageSoftness = 0.18f;
        value.weather.column.maximumBaseLiftMeters = 200.0f;
        value.shape.footprintCoverageInfluence = 0.40f;
        value.domainBottomMeters = 1500.0f;
        value.domainThicknessMeters = 5000.0f;
        value.weather.column.minimumThicknessMeters = 2250.000000f;
        value.weather.column.maximumThicknessMeters = 3550.000000f;
        value.shape.bottomFadeEnd = 0.100000f;
        value.shape.topFadeStart = 0.860000f;
        value.shape.lowerDensityScale = 1.000000f;
        break;
    case CloudFormationConcept::SnowOvercast:
        value.coverage = 0.90f;
        value.densityMultiplier = 1.25f;
        value.extinctionPerMeter = 0.00046f;
        value.detailErosion = 0.10f;
        value.typeSelection.mode = CloudTypeSelectionMode::FixedStratus;
        value.weather.generator.coverage = Channel(1301u, 2u, 6u, 0.20f, 0.10f, 0.85f);
        value.weather.generator.density = Channel(3301u, 2u, 5u, 0.18f, 0.05f, 0.75f);
        value.weather.generator.localThickness = Channel(4303u, 2u, 4u, 0.15f, 0.0f, 0.70f);
        value.weather.generator.coverageThreshold = 0.46f;
        value.weather.generator.coverageSoftness = 0.20f;
        value.weather.column.maximumBaseLiftMeters = 0.0f;
        value.shape.footprintCoverageInfluence = 0.20f;
        value.domainBottomMeters = 1500.0f;
        value.domainThicknessMeters = 2500.0f;
        value.weather.column.minimumThicknessMeters = 1500.000000f;
        value.weather.column.maximumThicknessMeters = 2300.000000f;
        value.shape.bottomFadeEnd = 0.050000f;
        value.shape.topFadeStart = 0.720000f;
        value.shape.lowerDensityScale = 1.000000f;
        break;
    default:
        return false;
    }
    return FinalizeBuiltIn(value, outSettings);
}

bool ResolveBuiltInCloudFormation(
    CloudFormationType type, CloudFormationSettings& outSettings)
{
    // F1 타입은 장면 콘셉트와 독립된 formation 기본값이다.
    CloudFormationSettings value = BasePhysicalFormation();
    value.shape.densityShaping = 0.70f; // 세 Type 모두 02 사용자 승인.
    switch (type)
    {
    case CloudFormationType::Stratus:
        value.baseNoiseWorldSizeMeters = 22000.0f;
        value.baseNoiseVerticalWorldSizeMeters = 8000.0f;
        value.weather.generator.coverage = Channel(1013u, 2u, 5u, .18f, .08f, .85f);
        value.coverage = 0.60f;
        value.densityMultiplier = 1.20f;
        value.extinctionPerMeter = 0.00042f;
        value.detailErosion = 0.12f;
        value.typeSelection.mode = CloudTypeSelectionMode::FixedStratus;
        value.weather.generator.coverageThreshold = 0.49f;
        value.weather.generator.coverageSoftness = 0.22f;
        value.weather.generator.coverage.bias = 0.01f;
        value.weather.generator.coverage.contrast = 1.03f;
        value.weather.generator.densityCoverageInfluence = 0.50f;
        value.weather.generator.thicknessCoverageInfluence = 0.65f;
        value.weather.column.maximumBaseLiftMeters = 0.0f;
        value.shape.footprintCoverageInfluence = 0.20f;
        value.domainBottomMeters = 1500.0f;
        value.domainThicknessMeters = 1050.0f;
        value.weather.column.minimumThicknessMeters = 450.0f;
        value.weather.column.maximumThicknessMeters = 850.0f;
        value.shape.bottomFadeEnd = 0.050000f;
        value.shape.topFadeStart = 0.720000f;
        value.shape.lowerDensityScale = 1.000000f;
        break;
    case CloudFormationType::Cumulus:
        value.coverage = 0.48f;
        value.densityMultiplier = 1.25f;
        value.extinctionPerMeter = 0.00038f;
        value.detailErosion = 0.18f;
        value.typeSelection.mode = CloudTypeSelectionMode::FixedCumulus;
        value.weather.generator.coverageThreshold = 0.56f;
        value.weather.generator.coverageSoftness = 0.14f;
        value.weather.generator.coverage.bias = 0.0f;
        value.weather.generator.coverage.contrast = 1.05f;
        value.weather.generator.densityCoverageInfluence = 0.45f;
        value.weather.generator.thicknessCoverageInfluence = 0.70f;
        value.weather.column.maximumBaseLiftMeters = 300.0f;
        value.shape.footprintCoverageInfluence = 0.50f;
        value.domainBottomMeters = 1800.0f;
        value.domainThicknessMeters = 3700.0f;
        value.weather.column.minimumThicknessMeters = 2000.000000f;
        value.weather.column.maximumThicknessMeters = 3200.000000f;
        value.shape.bottomFadeEnd = 0.080000f;
        value.shape.topFadeStart = 0.940000f;
        value.shape.lowerDensityScale = 0.650000f;
        break;
    case CloudFormationType::Mixed:
        value.baseNoiseWorldSizeMeters = 4200.0f;
        value.baseNoiseVerticalWorldSizeMeters = 5000.0f;
        value.detailNoiseWorldSizeMeters = 700.0f;
        value.weather.generator.coverage = Channel(1103u, 5u, 13u, .40f, .05f, 1.05f);
        value.coverage = 0.42f;
        value.densityMultiplier = 1.15f;
        value.extinctionPerMeter = 0.00035f;
        value.detailErosion = 0.18f;
        value.typeSelection.mode = CloudTypeSelectionMode::FixedMixed;
        value.weather.generator.coverageThreshold = 0.35f;
        value.weather.generator.coverageSoftness = 0.20f;
        value.weather.generator.coverage.bias = 0.0f;
        value.weather.generator.coverage.contrast = 1.05f;
        value.weather.generator.densityCoverageInfluence = 0.45f;
        value.weather.generator.thicknessCoverageInfluence = 0.60f;
        value.weather.column.maximumBaseLiftMeters = 100.0f;
        value.shape.footprintCoverageInfluence = 0.40f;
        value.domainBottomMeters = 3000.0f;
        value.domainThicknessMeters = 1450.0f;
        value.weather.column.minimumThicknessMeters = 650.0f;
        value.weather.column.maximumThicknessMeters = 1150.0f;
        value.shape.bottomFadeEnd = 0.100000f;
        value.shape.topFadeStart = 0.860000f;
        value.shape.lowerDensityScale = 1.000000f;
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
    if (path.empty() || !IsValidCloudFormationPresetTarget(target) ||
        (target.group != CloudFormationPresetGroup::Custom && target.group != CloudFormationPresetGroup::Type))
    {
        status = "Formation save rejected: only formation slots are writable";
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
             << "  \"schemaVersion\": 4,\n"
             << (target.group == CloudFormationPresetGroup::Custom ? "  \"snowDefaultInitialized\": 1,\n" : "")
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
    if (path.empty() || !IsValidCloudFormationPresetTarget(expectedTarget) ||
        (expectedTarget.group != CloudFormationPresetGroup::Custom && expectedTarget.group != CloudFormationPresetGroup::Type))
    {
        status = "Formation load rejected: only formation slots are file-backed";
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
        status = "No saved Custom";
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
    if (!presetjson::Valid(text)) { status = "Malformed formation JSON"; return false; }
    std::uint32_t schemaVersion = 0;
    if (!ParseUnsigned(text, "schemaVersion", schemaVersion) ||
        (schemaVersion != 1u && schemaVersion != 2u && schemaVersion != 3u && schemaVersion != 4u))
    {
        status = "Formation override rejected: schema or slot mismatch";
        return false;
    }

    CloudFormationSettings parsed;
    if (!ParseFormation(text, schemaVersion, parsed))
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
        (schemaVersion < 4u ? " legacy override migrated to common profile (Regional -> Mixed)" : " user override loaded");
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
    if (allowUserOverrides && target.group != CloudFormationPresetGroup::Concept)
    {
        std::error_code error;
        const auto path = CloudFormationPresetPath(root, target);
        const bool exists = std::filesystem::exists(path, error);
        if (error) { status = "Cannot inspect formation file"; return false; }
        if (exists)
        {
            CloudFormationSettings loaded;
            if (!LoadCloudFormationPreset(path, target, loaded, status)) return false;
            outSettings = loaded;
            outSource = CloudFormationPresetSource::UserOverride;
            return true;
        }
    }
    CloudFormationSettings builtIn;
    if (!(target.group == CloudFormationPresetGroup::Custom
        ? ResolveBuiltInCloudFormation(CloudFormationConcept::SnowOvercast, builtIn)
        : ResolveBuiltInCloudFormation(target, builtIn)))
    {
        status = "Formation resolve failed: no built-in preset";
        return false;
    }
    outSettings = builtIn;
    outSource = CloudFormationPresetSource::BuiltIn;
    status = std::string(CloudFormationPresetTargetName(target)) +
        " built-in loaded";
    return true;
}

// 승인된 일회성 교체. 완료 표식은 Custom 본문과 같은 원자 교체에 포함된다.
bool InitializeSnowCustomPreset(const std::filesystem::path& root, std::string& status)
{
    const auto target = CustomFormationTarget();
    const auto path = CloudFormationPresetPath(root, target);
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (error) { status = "Custom initialization: cannot inspect file"; return false; }
    if (exists) {
        std::ifstream in(path, std::ios::binary);
        if (!in) { status = "Custom initialization: cannot read file"; return false; }
        std::ostringstream text; text << in.rdbuf();
        std::uint32_t initialized = 0;
        if (ParseUnsigned(text.str(), "snowDefaultInitialized", initialized) && initialized == 1) return true;
        auto backup = path; backup += L".before-snow.bak";
        if (!std::filesystem::exists(backup, error))
            std::filesystem::copy_file(path, backup, std::filesystem::copy_options::none, error);
        if (error) { status = "Custom initialization: backup failed"; return false; }
    }
    CloudFormationSettings snow;
    if (!ResolveBuiltInCloudFormation(CloudFormationConcept::SnowOvercast, snow)) return false;
    return SaveCloudFormationPresetAtomic(path, target, snow, status);
}
