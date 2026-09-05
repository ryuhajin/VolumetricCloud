// ============================================================================
//  CloudFormationSettings.cpp - Stage 15 Weather/Shape/Domain 소유권 계약
// ============================================================================
#include "CloudFormationSettings.h"
#include "Fnv1a64.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <utility>

namespace
{
bool FiniteIn(float v, float lo, float hi)
{ return std::isfinite(v) && v >= lo && v <= hi; }

bool ValidChannel(const PeriodicChannelSettings& v)
{
    return v.macroPeriod >= 1u && v.macroPeriod <= 8u &&
        v.detailPeriod >= 2u && v.detailPeriod <= 16u &&
        FiniteIn(v.detailWeight, 0.0f, 1.0f) &&
        FiniteIn(v.bias, -0.5f, 0.5f) &&
        FiniteIn(v.contrast, 0.25f, 3.0f);
}

bool Near(float a, float b, float e = 1.0e-5f)
{ return std::isfinite(a) && std::isfinite(b) && std::abs(a - b) <= e; }

bool ChannelEqual(const PeriodicChannelSettings& a,
                  const PeriodicChannelSettings& b, float e)
{
    return a.seed == b.seed && a.macroPeriod == b.macroPeriod &&
        a.detailPeriod == b.detailPeriod && Near(a.detailWeight, b.detailWeight, e) &&
        Near(a.bias, b.bias, e) && Near(a.contrast, b.contrast, e);
}

void HashFloat(std::uint64_t& hash, float value)
{
    if (value == 0.0f) value = 0.0f;
    fnv1a64::Append(hash, &value, sizeof(value));
}

void HashUnsigned(std::uint64_t& hash, std::uint32_t value)
{ fnv1a64::Append(hash, &value, sizeof(value)); }

void HashChannel(std::uint64_t& hash, const PeriodicChannelSettings& value)
{
    HashUnsigned(hash, value.seed); HashUnsigned(hash, value.macroPeriod);
    HashUnsigned(hash, value.detailPeriod); HashFloat(hash, value.detailWeight);
    HashFloat(hash, value.bias); HashFloat(hash, value.contrast);
}
}

bool IsValidCloudFormationSettings(const CloudFormationSettings& value)
{
    const auto preset = static_cast<std::uint32_t>(value.weatherPreset);
    const auto selection = static_cast<std::uint32_t>(value.typeSelection.mode);
    if (preset > static_cast<std::uint32_t>(Stage5WeatherPreset::ChannelDebug) ||
        selection > static_cast<std::uint32_t>(CloudTypeSelectionMode::RegionalBlend))
        return false;

    const auto& generator = value.weather.generator;
    const auto& column = value.weather.column;
    const CloudShapeParameters safeShape = SanitizeCloudShapeParameters(value.shape);
    const WeatherColumnSettings safeColumn = SanitizeWeatherColumnSettings(column);
    return FiniteIn(value.coverage, 0.0f, 1.0f) &&
        FiniteIn(value.densityMultiplier, 0.0f, 5.0f) &&
        FiniteIn(value.extinctionPerMeter, 0.000001f, 0.01f) &&
        FiniteIn(value.detailErosion, 0.0f, 1.0f) &&
        ValidChannel(generator.coverage) && ValidChannel(generator.cloudType) &&
        ValidChannel(generator.density) && ValidChannel(generator.localThickness) &&
        FiniteIn(generator.coverageThreshold, 0.0f, 1.0f) &&
        FiniteIn(generator.coverageSoftness, 0.02f, 0.8f) &&
        FiniteIn(generator.densityCoverageInfluence, 0.0f, 1.0f) &&
        FiniteIn(generator.thicknessCoverageInfluence, 0.0f, 1.0f) &&
        FiniteIn(value.weather.worldSizeMeters, 17600.0f, 160000.0f) &&
        Near(column.stratusMinimumThicknessMeters, safeColumn.stratusMinimumThicknessMeters) &&
        Near(column.stratusMaximumThicknessMeters, safeColumn.stratusMaximumThicknessMeters) &&
        Near(column.cumulusMinimumThicknessMeters, safeColumn.cumulusMinimumThicknessMeters) &&
        Near(column.cumulusMaximumThicknessMeters, safeColumn.cumulusMaximumThicknessMeters) &&
        Near(column.maximumBaseLiftMeters, safeColumn.maximumBaseLiftMeters) &&
        Near(value.shape.stratusBottomFadeEnd, safeShape.stratusBottomFadeEnd) &&
        Near(value.shape.stratusTopFadeStart, safeShape.stratusTopFadeStart) &&
        Near(value.shape.mixedBottomFadeEnd, safeShape.mixedBottomFadeEnd) &&
        Near(value.shape.mixedTopFadeStart, safeShape.mixedTopFadeStart) &&
        Near(value.shape.cumulusBottomFadeEnd, safeShape.cumulusBottomFadeEnd) &&
        Near(value.shape.cumulusTopFadeStart, safeShape.cumulusTopFadeStart) &&
        Near(value.shape.cumulusUpperMassBottom, safeShape.cumulusUpperMassBottom) &&
        Near(value.shape.cumulusUpperMassStart, safeShape.cumulusUpperMassStart) &&
        Near(value.shape.cumulusUpperMassEnd, safeShape.cumulusUpperMassEnd) &&
        Near(value.shape.footprintCoverageInfluence,
             safeShape.footprintCoverageInfluence) &&
        FiniteIn(value.domainBottomMeters, -100000.0f, 100000.0f) &&
        FiniteIn(value.domainThicknessMeters, 1.0f, 100000.0f) &&
        FiniteIn(value.maximumViewTraceDistanceMeters, 1.0f, 200000.0f) &&
        FiniteIn(value.viewTraceFadeStartDistanceMeters, 0.0f,
                 value.maximumViewTraceDistanceMeters) &&
        FiniteIn(value.maximumLightTraceDistanceMeters, 1.0f, 200000.0f) &&
        FiniteIn(value.baseNoiseWorldSizeMeters, 1.0f, 200000.0f) &&
        FiniteIn(value.baseNoiseVerticalWorldSizeMeters, 1.0f, 200000.0f) &&
        FiniteIn(value.detailNoiseWorldSizeMeters, 1.0f, 100000.0f);
}

CloudFormationSettings SanitizeCloudFormationSettings(const CloudFormationSettings& value)
{
    CloudFormationSettings result = value;
    const auto finiteOr = [](float v, float fallback)
    { return std::isfinite(v) ? v : fallback; };
    result.coverage = std::clamp(finiteOr(result.coverage, 0.38f), 0.0f, 1.0f);
    result.densityMultiplier = std::clamp(finiteOr(result.densityMultiplier, 1.10f), 0.0f, 5.0f);
    result.extinctionPerMeter = std::clamp(finiteOr(result.extinctionPerMeter, 0.00036f), 0.000001f, 0.01f);
    result.detailErosion = std::clamp(finiteOr(result.detailErosion, 0.24f), 0.0f, 1.0f);
    if (static_cast<std::uint32_t>(result.weatherPreset) >
        static_cast<std::uint32_t>(Stage5WeatherPreset::ChannelDebug))
        result.weatherPreset = Stage5WeatherPreset::PeriodicPerlin;
    result.weather = SanitizeWeatherMapDefinition(result.weather);
    result.weather.worldSizeMeters = std::clamp(result.weather.worldSizeMeters, 17600.0f, 160000.0f);
    result.typeSelection = SanitizeCloudTypeSelection(result.typeSelection);
    result.shape = SanitizeCloudShapeParameters(result.shape);
    result.domainBottomMeters = std::clamp(finiteOr(result.domainBottomMeters, 1800.0f), -100000.0f, 100000.0f);
    result.domainThicknessMeters = std::clamp(finiteOr(result.domainThicknessMeters, 3700.0f), 1.0f, 100000.0f);
    result.maximumViewTraceDistanceMeters = std::clamp(finiteOr(result.maximumViewTraceDistanceMeters, 50000.0f), 1.0f, 200000.0f);
    result.viewTraceFadeStartDistanceMeters = std::clamp(finiteOr(result.viewTraceFadeStartDistanceMeters, 40000.0f), 0.0f, result.maximumViewTraceDistanceMeters);
    result.maximumLightTraceDistanceMeters = std::clamp(finiteOr(result.maximumLightTraceDistanceMeters, 20000.0f), 1.0f, 200000.0f);
    result.baseNoiseWorldSizeMeters = std::clamp(finiteOr(result.baseNoiseWorldSizeMeters, 12000.0f), 1.0f, 200000.0f);
    result.baseNoiseVerticalWorldSizeMeters = std::clamp(finiteOr(result.baseNoiseVerticalWorldSizeMeters, 12000.0f), 1.0f, 200000.0f);
    result.detailNoiseWorldSizeMeters = std::clamp(finiteOr(result.detailNoiseWorldSizeMeters, 2000.0f), 1.0f, 100000.0f);
    return result;
}

bool PrepareCloudFormationSettings(const CloudFormationSettings& value,
    PreparedCloudFormation& outPrepared, std::string& status,
    float requiredTopHeadroomMeters)
{
    if (!IsValidCloudFormationSettings(value))
    {
        status = "Formation rejected: invalid or out-of-range value";
        return false;
    }
    PreparedCloudFormation prepared;
    prepared.settings = SanitizeCloudFormationSettings(value);
    prepared.domain.cloudBottomAltitude = prepared.settings.domainBottomMeters;
    prepared.domain.cloudLayerThickness = prepared.settings.domainThicknessMeters;
    prepared.domain.maxViewTraceDistance = prepared.settings.maximumViewTraceDistanceMeters;
    prepared.domain.viewTraceFadeStartDistance = prepared.settings.viewTraceFadeStartDistanceMeters;
    prepared.domain.maxLightTraceDistance = prepared.settings.maximumLightTraceDistanceMeters;
    prepared.domain = SanitizeCloudDomainParameters(prepared.domain);
    prepared.fit = cloudshapedomain::EvaluateFit(prepared.settings.weather.column,
        prepared.settings.typeSelection, prepared.domain, requiredTopHeadroomMeters);
    if (!prepared.fit.valid)
    {
        std::ostringstream message;
        message << "Formation rejected: Weather column requires "
                << prepared.fit.requiredLayerThicknessMeters << " m, domain provides "
                << prepared.fit.availableLayerThicknessMeters << " m";
        status = message.str();
        return false;
    }
    prepared.domain.cloudLightingReferenceAltitudeMeters =
        cloudshapedomain::LightingReferenceAltitudeMeters(
            prepared.settings.weather.column, prepared.settings.typeSelection,
            prepared.domain);
    prepared.domain = SanitizeCloudDomainParameters(prepared.domain);
    outPrepared = std::move(prepared);
    status = "Formation preflight succeeded";
    return true;
}

CloudFormationSettings CaptureCloudFormationSettingsUnchecked(
    const CloudParameters& cloud, const CloudShapeParameters& shape,
    const CloudDomainParameters& domain, Stage5WeatherPreset weatherPreset,
    const WeatherMapDefinition& weather, const CloudTypeSelection& typeSelection,
    const NoiseVolumeParameters& noise)
{
    CloudFormationSettings result;
    result.coverage = cloud.coverage; result.densityMultiplier = cloud.densityMultiplier;
    result.extinctionPerMeter = cloud.extinctionCoefficient;
    result.detailErosion = cloud.detailErosionStrength;
    result.weatherPreset = weatherPreset; result.weather = weather;
    result.weather.worldSizeMeters = cloud.weatherMapWorldSize;
    result.typeSelection = typeSelection; result.shape = shape;
    result.domainBottomMeters = domain.cloudBottomAltitude;
    result.domainThicknessMeters = domain.cloudLayerThickness;
    result.maximumViewTraceDistanceMeters = domain.maxViewTraceDistance;
    result.viewTraceFadeStartDistanceMeters = domain.viewTraceFadeStartDistance;
    result.maximumLightTraceDistanceMeters = domain.maxLightTraceDistance;
    result.baseNoiseWorldSizeMeters = noise.baseWorldSizeMeters;
    result.baseNoiseVerticalWorldSizeMeters = noise.baseVerticalWorldSizeMeters;
    result.detailNoiseWorldSizeMeters = noise.detailWorldSizeMeters;
    return result;
}

CloudFormationSettings CaptureCloudFormationSettings(
    const CloudParameters& cloud, const CloudShapeParameters& shape,
    const CloudDomainParameters& domain, Stage5WeatherPreset weatherPreset,
    const WeatherMapDefinition& weather, const CloudTypeSelection& typeSelection,
    const NoiseVolumeParameters& noise)
{
    return SanitizeCloudFormationSettings(CaptureCloudFormationSettingsUnchecked(
        cloud, shape, domain, weatherPreset, weather, typeSelection, noise));
}

void WriteCloudFormationToRuntime(const PreparedCloudFormation& formation,
    CloudParameters& cloud, CloudShapeParameters& shape,
    CloudDomainParameters& domain, Stage5WeatherPreset& weatherPreset,
    WeatherMapDefinition& weather, CloudTypeSelection& typeSelection,
    NoiseVolumeParameters& noise)
{
    const auto& value = formation.settings;
    cloud.coverage = value.coverage; cloud.densityMultiplier = value.densityMultiplier;
    cloud.extinctionCoefficient = value.extinctionPerMeter;
    cloud.detailErosionStrength = value.detailErosion;
    cloud.weatherMapWorldSize = value.weather.worldSizeMeters;
    cloud.cloudBoundsMin.y = formation.domain.cloudBottomAltitude;
    cloud.cloudBoundsMax.y = formation.domain.cloudBottomAltitude + formation.domain.cloudLayerThickness;
    shape = value.shape; domain = formation.domain; weatherPreset = value.weatherPreset;
    weather = value.weather; typeSelection = value.typeSelection;
    noise.baseWorldSizeMeters = value.baseNoiseWorldSizeMeters;
    noise.baseVerticalWorldSizeMeters = value.baseNoiseVerticalWorldSizeMeters;
    noise.detailWorldSizeMeters = value.detailNoiseWorldSizeMeters;
}

bool CloudFormationSettingsEqual(const CloudFormationSettings& a,
                                 const CloudFormationSettings& b, float epsilon)
{
    if (a.weatherPreset != b.weatherPreset || a.typeSelection.mode != b.typeSelection.mode ||
        !ChannelEqual(a.weather.generator.coverage, b.weather.generator.coverage, epsilon) ||
        !ChannelEqual(a.weather.generator.cloudType, b.weather.generator.cloudType, epsilon) ||
        !ChannelEqual(a.weather.generator.density, b.weather.generator.density, epsilon) ||
        !ChannelEqual(a.weather.generator.localThickness, b.weather.generator.localThickness, epsilon))
        return false;
    const float* as = reinterpret_cast<const float*>(&a.shape);
    const float* bs = reinterpret_cast<const float*>(&b.shape);
    for (std::size_t i = 0; i < sizeof(CloudShapeParameters) / sizeof(float); ++i)
        if (!Near(as[i], bs[i], epsilon)) return false;
    const float av[] = {a.coverage,a.densityMultiplier,a.extinctionPerMeter,a.detailErosion,
        a.weather.generator.coverageThreshold,a.weather.generator.coverageSoftness,
        a.weather.generator.densityCoverageInfluence,a.weather.generator.thicknessCoverageInfluence,
        a.weather.column.stratusMinimumThicknessMeters,a.weather.column.stratusMaximumThicknessMeters,
        a.weather.column.cumulusMinimumThicknessMeters,a.weather.column.cumulusMaximumThicknessMeters,
        a.weather.column.maximumBaseLiftMeters,a.weather.worldSizeMeters,a.domainBottomMeters,
        a.domainThicknessMeters,a.maximumViewTraceDistanceMeters,a.viewTraceFadeStartDistanceMeters,
        a.maximumLightTraceDistanceMeters,a.baseNoiseWorldSizeMeters,a.baseNoiseVerticalWorldSizeMeters,
        a.detailNoiseWorldSizeMeters};
    const float bv[] = {b.coverage,b.densityMultiplier,b.extinctionPerMeter,b.detailErosion,
        b.weather.generator.coverageThreshold,b.weather.generator.coverageSoftness,
        b.weather.generator.densityCoverageInfluence,b.weather.generator.thicknessCoverageInfluence,
        b.weather.column.stratusMinimumThicknessMeters,b.weather.column.stratusMaximumThicknessMeters,
        b.weather.column.cumulusMinimumThicknessMeters,b.weather.column.cumulusMaximumThicknessMeters,
        b.weather.column.maximumBaseLiftMeters,b.weather.worldSizeMeters,b.domainBottomMeters,
        b.domainThicknessMeters,b.maximumViewTraceDistanceMeters,b.viewTraceFadeStartDistanceMeters,
        b.maximumLightTraceDistanceMeters,b.baseNoiseWorldSizeMeters,b.baseNoiseVerticalWorldSizeMeters,
        b.detailNoiseWorldSizeMeters};
    for (std::size_t i = 0; i < std::size(av); ++i)
        if (!Near(av[i], bv[i], epsilon)) return false;
    return true;
}

std::uint64_t HashCloudFormationSettings(const CloudFormationSettings& input)
{
    const auto value = SanitizeCloudFormationSettings(input);
    std::uint64_t hash = fnv1a64::kOffsetBasis;
    HashFloat(hash,value.coverage); HashFloat(hash,value.densityMultiplier);
    HashFloat(hash,value.extinctionPerMeter); HashFloat(hash,value.detailErosion);
    HashUnsigned(hash,static_cast<std::uint32_t>(value.weatherPreset));
    HashUnsigned(hash,static_cast<std::uint32_t>(value.typeSelection.mode));
    HashChannel(hash,value.weather.generator.coverage);
    HashChannel(hash,value.weather.generator.cloudType);
    HashChannel(hash,value.weather.generator.density);
    HashChannel(hash,value.weather.generator.localThickness);
    fnv1a64::Append(hash,&value.weather.generator.coverageThreshold,sizeof(float)*4u);
    fnv1a64::Append(hash,&value.weather.column,sizeof(value.weather.column));
    HashFloat(hash,value.weather.worldSizeMeters);
    fnv1a64::Append(hash,&value.shape,sizeof(value.shape));
    const float remaining[] = {value.domainBottomMeters,value.domainThicknessMeters,
        value.maximumViewTraceDistanceMeters,value.viewTraceFadeStartDistanceMeters,
        value.maximumLightTraceDistanceMeters,value.baseNoiseWorldSizeMeters,
        value.baseNoiseVerticalWorldSizeMeters,value.detailNoiseWorldSizeMeters};
    fnv1a64::Append(hash,remaining,sizeof(remaining));
    return hash;
}
