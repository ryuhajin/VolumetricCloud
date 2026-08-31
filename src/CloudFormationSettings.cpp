// ============================================================================
//  CloudFormationSettings.cpp - Stage 15B 공통 구름 형성 검증/적용
// ============================================================================
#include "CloudFormationSettings.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <sstream>
#include <utility>

namespace
{
bool FiniteIn(float value, float minimum, float maximum)
{
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

bool ValidChannel(const PeriodicChannelSettings& value)
{
    return value.macroPeriod >= 1u && value.macroPeriod <= 8u &&
        value.detailPeriod >= 2u && value.detailPeriod <= 16u &&
        FiniteIn(value.detailWeight, 0.0f, 1.0f) &&
        FiniteIn(value.bias, -0.5f, 0.5f) &&
        FiniteIn(value.contrast, 0.25f, 3.0f);
}

bool FloatNear(float a, float b, float epsilon)
{
    return std::abs(a - b) <= epsilon;
}

bool ChannelEqual(const PeriodicChannelSettings& a,
                  const PeriodicChannelSettings& b, float epsilon)
{
    return a.seed == b.seed && a.macroPeriod == b.macroPeriod &&
        a.detailPeriod == b.detailPeriod &&
        FloatNear(a.detailWeight, b.detailWeight, epsilon) &&
        FloatNear(a.bias, b.bias, epsilon) &&
        FloatNear(a.contrast, b.contrast, epsilon);
}

void HashBytes(std::uint64_t& hash, const void* bytes, std::size_t size)
{
    const auto* data = static_cast<const std::uint8_t*>(bytes);
    for (std::size_t index = 0; index < size; ++index)
    {
        hash ^= data[index];
        hash *= 1099511628211ull;
    }
}

void HashFloat(std::uint64_t& hash, float value)
{
    if (value == 0.0f)
        value = 0.0f; // -0과 +0은 같은 formation 값으로 canonicalize한다.
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "float hash size changed");
    std::memcpy(&bits, &value, sizeof(bits));
    HashBytes(hash, &bits, sizeof(bits));
}

void HashUnsigned(std::uint64_t& hash, std::uint32_t value)
{
    HashBytes(hash, &value, sizeof(value));
}

void HashChannel(std::uint64_t& hash, const PeriodicChannelSettings& value)
{
    HashUnsigned(hash, value.seed);
    HashUnsigned(hash, value.macroPeriod);
    HashUnsigned(hash, value.detailPeriod);
    HashFloat(hash, value.detailWeight);
    HashFloat(hash, value.bias);
    HashFloat(hash, value.contrast);
}
}

bool IsValidCloudFormationSettings(const CloudFormationSettings& value)
{
    const std::uint32_t weatherPreset =
        static_cast<std::uint32_t>(value.weatherPreset);
    const std::uint32_t cloudType =
        static_cast<std::uint32_t>(value.weather.cloudTypeMode);
    const CloudShapeMode shapeMode = cloudshape::Mode(value.shape.shapeMode);
    if (weatherPreset >
            static_cast<std::uint32_t>(Stage5WeatherPreset::ChannelDebug) ||
        cloudType > static_cast<std::uint32_t>(CloudTypeMode::WeatherMap) ||
        (shapeMode != CloudShapeMode::WeatherPhysicalThickness &&
         shapeMode != CloudShapeMode::CirrusPhysicalLayer))
    {
        return false;
    }

    // Cirrus는 Weather G를 cloud family로 해석하지 않는다. 전용 shape variant와
    // 실제 G texture를 함께 사용한다는 계약을 명시해 잘못된 fixed-G 조합을 막는다.
    if (shapeMode == CloudShapeMode::CirrusPhysicalLayer &&
        value.weather.cloudTypeMode != CloudTypeMode::WeatherMap)
    {
        return false;
    }

    const CloudShapeParameters sanitizedShape =
        SanitizeCloudShapeParameters(value.shape);
    const auto shapeFloatEqual = [](float a, float b)
    {
        return std::isfinite(a) && std::abs(a - b) <= 1.0e-5f;
    };
    const float flowLength = std::sqrt(
        value.shape.cirrusFlowDirectionXZ.x *
            value.shape.cirrusFlowDirectionXZ.x +
        value.shape.cirrusFlowDirectionXZ.y *
            value.shape.cirrusFlowDirectionXZ.y);
    if (!std::isfinite(flowLength) || flowLength <= 1.0e-6f ||
        sanitizedShape.shapeMode != value.shape.shapeMode ||
        !shapeFloatEqual(sanitizedShape.stratusMinimumThicknessMeters,
                         value.shape.stratusMinimumThicknessMeters) ||
        !shapeFloatEqual(sanitizedShape.stratusMaximumThicknessMeters,
                         value.shape.stratusMaximumThicknessMeters) ||
        !shapeFloatEqual(sanitizedShape.cumulusMinimumThicknessMeters,
                         value.shape.cumulusMinimumThicknessMeters) ||
        !shapeFloatEqual(sanitizedShape.cumulusMaximumThicknessMeters,
                         value.shape.cumulusMaximumThicknessMeters) ||
        !shapeFloatEqual(sanitizedShape.stratusBottomFadeEnd,
                         value.shape.stratusBottomFadeEnd) ||
        !shapeFloatEqual(sanitizedShape.stratusTopFadeStart,
                         value.shape.stratusTopFadeStart) ||
        !shapeFloatEqual(sanitizedShape.mixedBottomFadeEnd,
                         value.shape.mixedBottomFadeEnd) ||
        !shapeFloatEqual(sanitizedShape.mixedTopFadeStart,
                         value.shape.mixedTopFadeStart) ||
        !shapeFloatEqual(sanitizedShape.cumulusBottomFadeEnd,
                         value.shape.cumulusBottomFadeEnd) ||
        !shapeFloatEqual(sanitizedShape.cumulusTopFadeStart,
                         value.shape.cumulusTopFadeStart) ||
        !shapeFloatEqual(sanitizedShape.cumulusUpperMassBottom,
                         value.shape.cumulusUpperMassBottom) ||
        !shapeFloatEqual(sanitizedShape.cumulusUpperMassStart,
                         value.shape.cumulusUpperMassStart) ||
        !shapeFloatEqual(sanitizedShape.cumulusUpperMassEnd,
                         value.shape.cumulusUpperMassEnd) ||
        !shapeFloatEqual(sanitizedShape.localBaseLiftMaxMeters,
                         value.shape.localBaseLiftMaxMeters) ||
        !shapeFloatEqual(sanitizedShape.footprintCoverageInfluence,
                         value.shape.footprintCoverageInfluence) ||
        !shapeFloatEqual(sanitizedShape.cirrusBaseAlongScaleMeters,
                         value.shape.cirrusBaseAlongScaleMeters) ||
        !shapeFloatEqual(sanitizedShape.cirrusBaseAcrossScaleMeters,
                         value.shape.cirrusBaseAcrossScaleMeters) ||
        !shapeFloatEqual(sanitizedShape.cirrusBaseVerticalScaleMeters,
                         value.shape.cirrusBaseVerticalScaleMeters) ||
        !shapeFloatEqual(sanitizedShape.cirrusDetailAlongScaleMeters,
                         value.shape.cirrusDetailAlongScaleMeters) ||
        !shapeFloatEqual(sanitizedShape.cirrusDetailAcrossScaleMeters,
                         value.shape.cirrusDetailAcrossScaleMeters) ||
        !shapeFloatEqual(sanitizedShape.cirrusDetailVerticalScaleMeters,
                         value.shape.cirrusDetailVerticalScaleMeters) ||
        !shapeFloatEqual(sanitizedShape.cirrusMinimumThicknessMeters,
                         value.shape.cirrusMinimumThicknessMeters) ||
        !shapeFloatEqual(sanitizedShape.cirrusMaximumThicknessMeters,
                         value.shape.cirrusMaximumThicknessMeters) ||
        !shapeFloatEqual(sanitizedShape.cirrusVerticalProfileCenter,
                         value.shape.cirrusVerticalProfileCenter) ||
        !shapeFloatEqual(sanitizedShape.cirrusVerticalProfileHalfWidth,
                         value.shape.cirrusVerticalProfileHalfWidth))
    {
        return false;
    }

    return FiniteIn(value.coverage, 0.0f, 1.0f) &&
        FiniteIn(value.densityMultiplier, 0.0f, 5.0f) &&
        FiniteIn(value.extinctionPerMeter, 0.000001f, 0.01f) &&
        FiniteIn(value.detailErosion, 0.0f, 1.0f) &&
        ValidChannel(value.weather.coverage) &&
        ValidChannel(value.weather.cloudType) &&
        ValidChannel(value.weather.density) &&
        ValidChannel(value.weather.localThickness) &&
        FiniteIn(value.weather.coverageThreshold, 0.0f, 1.0f) &&
        FiniteIn(value.weather.coverageSoftness, 0.02f, 0.8f) &&
        FiniteIn(value.weather.densityCoverageInfluence, 0.0f, 1.0f) &&
        FiniteIn(value.weather.thicknessCoverageInfluence, 0.0f, 1.0f) &&
        FiniteIn(value.weatherWorldSizeMeters, 17600.0f, 160000.0f) &&
        FiniteIn(value.domainBottomMeters, -100000.0f, 100000.0f) &&
        FiniteIn(value.domainThicknessMeters, 1.0f, 100000.0f) &&
        FiniteIn(value.maximumViewTraceDistanceMeters, 1.0f, 200000.0f) &&
        FiniteIn(value.viewTraceFadeStartDistanceMeters, 0.0f,
                 value.maximumViewTraceDistanceMeters) &&
        FiniteIn(value.maximumLightTraceDistanceMeters, 1.0f, 200000.0f) &&
        FiniteIn(value.baseNoiseWorldSizeMeters, 1.0f, 200000.0f) &&
        FiniteIn(value.baseNoiseVerticalWorldSizeMeters, 1.0f, 200000.0f) &&
        FiniteIn(value.detailNoiseWorldSizeMeters, 1.0f, 100000.0f) &&
        std::isfinite(value.windDirection.x) &&
        std::isfinite(value.windDirection.y) &&
        std::isfinite(value.windDirection.z) &&
        FiniteIn(value.windSpeedMetersPerSecond, 0.0f, 1000.0f) &&
        (value.windDirection.x * value.windDirection.x +
         value.windDirection.z * value.windDirection.z) > 1.0e-12f;
}

CloudFormationSettings SanitizeCloudFormationSettings(
    const CloudFormationSettings& value)
{
    CloudFormationSettings result = value;
    const auto finiteOr = [](float input, float fallback)
    {
        return std::isfinite(input) ? input : fallback;
    };
    result.coverage = std::clamp(finiteOr(result.coverage, 0.38f), 0.0f, 1.0f);
    result.densityMultiplier = std::clamp(
        finiteOr(result.densityMultiplier, 1.10f), 0.0f, 5.0f);
    result.extinctionPerMeter = std::clamp(
        finiteOr(result.extinctionPerMeter, 0.00036f), 0.000001f, 0.01f);
    result.detailErosion = std::clamp(
        finiteOr(result.detailErosion, 0.24f), 0.0f, 1.0f);
    if (static_cast<std::uint32_t>(result.weatherPreset) >
        static_cast<std::uint32_t>(Stage5WeatherPreset::ChannelDebug))
    {
        result.weatherPreset = Stage5WeatherPreset::PeriodicPerlin;
    }
    result.weather = SanitizeWeatherMapGeneratorSettings(result.weather);
    result.weatherWorldSizeMeters = std::clamp(
        finiteOr(result.weatherWorldSizeMeters, 64000.0f),
        17600.0f, 160000.0f);
    result.shape = SanitizeCloudShapeParameters(result.shape);
    if (cloudshape::Mode(result.shape.shapeMode) ==
        CloudShapeMode::CirrusPhysicalLayer)
    {
        result.weather.cloudTypeMode = CloudTypeMode::WeatherMap;
    }
    const float windLength = std::sqrt(
        result.windDirection.x * result.windDirection.x +
        result.windDirection.z * result.windDirection.z);
    if (!std::isfinite(windLength) || windLength <= 1.0e-6f)
    {
        result.windDirection = { 0.9701425f, 0.0f, 0.2425356f };
    }
    else
    {
        result.windDirection.x /= windLength;
        result.windDirection.y = 0.0f;
        result.windDirection.z /= windLength;
    }
    result.windSpeedMetersPerSecond = std::clamp(
        finiteOr(result.windSpeedMetersPerSecond, 12.0f), 0.0f, 1000.0f);
    result.domainBottomMeters = std::clamp(
        finiteOr(result.domainBottomMeters, 1800.0f),
        -100000.0f, 100000.0f);
    result.domainThicknessMeters = std::clamp(
        finiteOr(result.domainThicknessMeters, 3700.0f), 1.0f, 100000.0f);
    result.maximumViewTraceDistanceMeters = std::clamp(
        finiteOr(result.maximumViewTraceDistanceMeters, 50000.0f),
        1.0f, 200000.0f);
    result.viewTraceFadeStartDistanceMeters = std::clamp(
        finiteOr(result.viewTraceFadeStartDistanceMeters, 40000.0f),
        0.0f, result.maximumViewTraceDistanceMeters);
    result.maximumLightTraceDistanceMeters = std::clamp(
        finiteOr(result.maximumLightTraceDistanceMeters, 20000.0f),
        1.0f, 200000.0f);
    result.baseNoiseWorldSizeMeters = std::clamp(
        finiteOr(result.baseNoiseWorldSizeMeters, 12000.0f),
        1.0f, 200000.0f);
    result.baseNoiseVerticalWorldSizeMeters = std::clamp(
        finiteOr(result.baseNoiseVerticalWorldSizeMeters, 12000.0f),
        1.0f, 200000.0f);
    result.detailNoiseWorldSizeMeters = std::clamp(
        finiteOr(result.detailNoiseWorldSizeMeters, 2000.0f),
        1.0f, 100000.0f);
    return result;
}

bool PrepareCloudFormationSettings(
    const CloudFormationSettings& value, PreparedCloudFormation& outPrepared,
    std::string& status, float requiredTopHeadroomMeters)
{
    if (!IsValidCloudFormationSettings(value))
    {
        status = "Formation rejected: invalid or out-of-range value";
        return false;
    }

    PreparedCloudFormation prepared;
    prepared.settings = SanitizeCloudFormationSettings(value);
    prepared.domain.domainType = static_cast<std::uint32_t>(
        CloudDomainType::PlanarLayer);
    prepared.domain.cloudBottomAltitude = prepared.settings.domainBottomMeters;
    prepared.domain.cloudLayerThickness =
        prepared.settings.domainThicknessMeters;
    prepared.domain.maxViewTraceDistance =
        prepared.settings.maximumViewTraceDistanceMeters;
    prepared.domain.viewTraceFadeStartDistance =
        prepared.settings.viewTraceFadeStartDistanceMeters;
    prepared.domain.maxLightTraceDistance =
        prepared.settings.maximumLightTraceDistanceMeters;
    prepared.domain = SanitizeCloudDomainParameters(prepared.domain);

    prepared.fit = cloudshapedomain::EvaluateFit(
        prepared.settings.shape, prepared.settings.weather.cloudTypeMode,
        prepared.domain, requiredTopHeadroomMeters);
    if (!prepared.fit.valid)
    {
        std::ostringstream message;
        message << "Formation rejected: local shape requires "
                << prepared.fit.requiredLayerThicknessMeters
                << " m, domain provides "
                << prepared.fit.availableLayerThicknessMeters << " m";
        status = message.str();
        return false;
    }

    prepared.domain.cloudLightingReferenceAltitudeMeters =
        cloudshapedomain::LightingReferenceAltitudeMeters(
            prepared.settings.shape,
            prepared.settings.weather.cloudTypeMode,
            prepared.domain);
    prepared.domain = SanitizeCloudDomainParameters(prepared.domain);
    prepared.weatherMap = BuildWeatherMap(
        prepared.settings.weatherPreset, prepared.settings.weather);
    if (!IsValidWeatherMapData(prepared.weatherMap))
    {
        status = "Formation rejected: Weather Map generation failed";
        return false;
    }

    outPrepared = std::move(prepared);
    status = "Formation preflight succeeded";
    return true;
}

CloudFormationSettings CaptureCloudFormationSettingsUnchecked(
    const CloudParameters& cloud, const CloudShapeParameters& shape,
    const CloudDomainParameters& domain, Stage5WeatherPreset weatherPreset,
    const WeatherMapGeneratorSettings& weather,
    const NoiseVolumeParameters& noise)
{
    CloudFormationSettings result;
    result.coverage = cloud.coverage;
    result.densityMultiplier = cloud.densityMultiplier;
    result.extinctionPerMeter = cloud.extinctionCoefficient;
    result.detailErosion = cloud.detailErosionStrength;
    result.weatherPreset = weatherPreset;
    result.weather = weather;
    result.weatherWorldSizeMeters = cloud.weatherMapWorldSize;
    result.shape = shape;
    result.domainBottomMeters = domain.cloudBottomAltitude;
    result.domainThicknessMeters = domain.cloudLayerThickness;
    result.maximumViewTraceDistanceMeters = domain.maxViewTraceDistance;
    result.viewTraceFadeStartDistanceMeters =
        domain.viewTraceFadeStartDistance;
    result.maximumLightTraceDistanceMeters = domain.maxLightTraceDistance;
    result.baseNoiseWorldSizeMeters = noise.baseWorldSizeMeters;
    result.baseNoiseVerticalWorldSizeMeters =
        noise.baseVerticalWorldSizeMeters;
    result.detailNoiseWorldSizeMeters = noise.detailWorldSizeMeters;
    result.windDirection = cloud.windDirection;
    result.windSpeedMetersPerSecond = cloud.windSpeed;
    return result;
}

CloudFormationSettings CaptureCloudFormationSettings(
    const CloudParameters& cloud, const CloudShapeParameters& shape,
    const CloudDomainParameters& domain, Stage5WeatherPreset weatherPreset,
    const WeatherMapGeneratorSettings& weather,
    const NoiseVolumeParameters& noise)
{
    return SanitizeCloudFormationSettings(
        CaptureCloudFormationSettingsUnchecked(
            cloud, shape, domain, weatherPreset, weather, noise));
}

void WriteCloudFormationToRuntime(
    const PreparedCloudFormation& formation, CloudParameters& cloud,
    CloudShapeParameters& shape, CloudDomainParameters& domain,
    Stage5WeatherPreset& weatherPreset,
    WeatherMapGeneratorSettings& weather, NoiseVolumeParameters& noise)
{
    const CloudFormationSettings& value = formation.settings;
    cloud.coverage = value.coverage;
    cloud.densityMultiplier = value.densityMultiplier;
    cloud.extinctionCoefficient = value.extinctionPerMeter;
    cloud.detailErosionStrength = value.detailErosion;
    cloud.weatherMapWorldSize = value.weatherWorldSizeMeters;
    cloud.windDirection = value.windDirection;
    cloud.windSpeed = value.windSpeedMetersPerSecond;
    cloud.cloudBoundsMin.y = formation.domain.cloudBottomAltitude;
    cloud.cloudBoundsMax.y = formation.domain.cloudBottomAltitude +
        formation.domain.cloudLayerThickness;
    shape = value.shape;
    domain = formation.domain;
    weatherPreset = value.weatherPreset;
    weather = value.weather;
    noise.noiseSource = static_cast<std::uint32_t>(NoiseSource::Texture3D);
    noise.baseWorldSizeMeters = value.baseNoiseWorldSizeMeters;
    noise.baseVerticalWorldSizeMeters = value.baseNoiseVerticalWorldSizeMeters;
    noise.detailWorldSizeMeters = value.detailNoiseWorldSizeMeters;
}

bool CloudFormationSettingsEqual(const CloudFormationSettings& a,
                                 const CloudFormationSettings& b,
                                 float epsilon)
{
    if (a.weatherPreset != b.weatherPreset ||
        a.weather.cloudTypeMode != b.weather.cloudTypeMode ||
        a.shape.shapeMode != b.shape.shapeMode ||
        !ChannelEqual(a.weather.coverage, b.weather.coverage, epsilon) ||
        !ChannelEqual(a.weather.cloudType, b.weather.cloudType, epsilon) ||
        !ChannelEqual(a.weather.density, b.weather.density, epsilon) ||
        !ChannelEqual(a.weather.localThickness,
                      b.weather.localThickness, epsilon))
    {
        return false;
    }
    const std::array<float, 48> av = {{
        a.coverage, a.densityMultiplier, a.extinctionPerMeter,
        a.detailErosion, a.weather.coverageThreshold,
        a.weather.coverageSoftness, a.weather.densityCoverageInfluence,
        a.weather.thicknessCoverageInfluence, a.weatherWorldSizeMeters,
        a.shape.stratusMinimumThicknessMeters,
        a.shape.stratusMaximumThicknessMeters,
        a.shape.cumulusMinimumThicknessMeters,
        a.shape.cumulusMaximumThicknessMeters,
        a.shape.stratusBottomFadeEnd, a.shape.stratusTopFadeStart,
        a.shape.mixedBottomFadeEnd, a.shape.mixedTopFadeStart,
        a.shape.cumulusBottomFadeEnd, a.shape.cumulusTopFadeStart,
        a.shape.cumulusUpperMassBottom, a.shape.cumulusUpperMassStart,
        a.shape.cumulusUpperMassEnd, a.shape.localBaseLiftMaxMeters,
        a.shape.footprintCoverageInfluence,
        a.shape.cirrusFlowDirectionXZ.x,
        a.shape.cirrusFlowDirectionXZ.y,
        a.shape.cirrusBaseAlongScaleMeters,
        a.shape.cirrusBaseAcrossScaleMeters,
        a.shape.cirrusBaseVerticalScaleMeters,
        a.shape.cirrusDetailAlongScaleMeters,
        a.shape.cirrusDetailAcrossScaleMeters,
        a.shape.cirrusDetailVerticalScaleMeters,
        a.shape.cirrusMinimumThicknessMeters,
        a.shape.cirrusMaximumThicknessMeters,
        a.shape.cirrusVerticalProfileCenter,
        a.shape.cirrusVerticalProfileHalfWidth,
        a.domainBottomMeters, a.domainThicknessMeters,
        a.maximumViewTraceDistanceMeters,
        a.viewTraceFadeStartDistanceMeters,
        a.maximumLightTraceDistanceMeters,
        a.baseNoiseWorldSizeMeters, a.baseNoiseVerticalWorldSizeMeters,
        a.detailNoiseWorldSizeMeters, a.windDirection.x,
        a.windDirection.y, a.windDirection.z,
        a.windSpeedMetersPerSecond,
    }};
    const std::array<float, 48> bv = {{
        b.coverage, b.densityMultiplier, b.extinctionPerMeter,
        b.detailErosion, b.weather.coverageThreshold,
        b.weather.coverageSoftness, b.weather.densityCoverageInfluence,
        b.weather.thicknessCoverageInfluence, b.weatherWorldSizeMeters,
        b.shape.stratusMinimumThicknessMeters,
        b.shape.stratusMaximumThicknessMeters,
        b.shape.cumulusMinimumThicknessMeters,
        b.shape.cumulusMaximumThicknessMeters,
        b.shape.stratusBottomFadeEnd, b.shape.stratusTopFadeStart,
        b.shape.mixedBottomFadeEnd, b.shape.mixedTopFadeStart,
        b.shape.cumulusBottomFadeEnd, b.shape.cumulusTopFadeStart,
        b.shape.cumulusUpperMassBottom, b.shape.cumulusUpperMassStart,
        b.shape.cumulusUpperMassEnd, b.shape.localBaseLiftMaxMeters,
        b.shape.footprintCoverageInfluence,
        b.shape.cirrusFlowDirectionXZ.x,
        b.shape.cirrusFlowDirectionXZ.y,
        b.shape.cirrusBaseAlongScaleMeters,
        b.shape.cirrusBaseAcrossScaleMeters,
        b.shape.cirrusBaseVerticalScaleMeters,
        b.shape.cirrusDetailAlongScaleMeters,
        b.shape.cirrusDetailAcrossScaleMeters,
        b.shape.cirrusDetailVerticalScaleMeters,
        b.shape.cirrusMinimumThicknessMeters,
        b.shape.cirrusMaximumThicknessMeters,
        b.shape.cirrusVerticalProfileCenter,
        b.shape.cirrusVerticalProfileHalfWidth,
        b.domainBottomMeters, b.domainThicknessMeters,
        b.maximumViewTraceDistanceMeters,
        b.viewTraceFadeStartDistanceMeters,
        b.maximumLightTraceDistanceMeters,
        b.baseNoiseWorldSizeMeters, b.baseNoiseVerticalWorldSizeMeters,
        b.detailNoiseWorldSizeMeters, b.windDirection.x,
        b.windDirection.y, b.windDirection.z,
        b.windSpeedMetersPerSecond,
    }};
    for (std::size_t index = 0; index < av.size(); ++index)
    {
        if (!FloatNear(av[index], bv[index], epsilon))
            return false;
    }
    return true;
}

std::uint64_t HashCloudFormationSettings(const CloudFormationSettings& input)
{
    const CloudFormationSettings value =
        SanitizeCloudFormationSettings(input);
    std::uint64_t hash = 1469598103934665603ull;
    HashFloat(hash, value.coverage);
    HashFloat(hash, value.densityMultiplier);
    HashFloat(hash, value.extinctionPerMeter);
    HashFloat(hash, value.detailErosion);
    HashUnsigned(hash, static_cast<std::uint32_t>(value.weatherPreset));
    HashChannel(hash, value.weather.coverage);
    HashChannel(hash, value.weather.cloudType);
    HashChannel(hash, value.weather.density);
    HashChannel(hash, value.weather.localThickness);
    HashFloat(hash, value.weather.coverageThreshold);
    HashFloat(hash, value.weather.coverageSoftness);
    HashFloat(hash, value.weather.densityCoverageInfluence);
    HashFloat(hash, value.weather.thicknessCoverageInfluence);
    HashUnsigned(hash, static_cast<std::uint32_t>(value.weather.cloudTypeMode));
    HashFloat(hash, value.weatherWorldSizeMeters);
    HashUnsigned(hash, value.shape.shapeMode);
    const std::array<float, 38> fields = {{
        value.shape.stratusMinimumThicknessMeters,
        value.shape.stratusMaximumThicknessMeters,
        value.shape.cumulusMinimumThicknessMeters,
        value.shape.cumulusMaximumThicknessMeters,
        value.shape.stratusBottomFadeEnd,
        value.shape.stratusTopFadeStart,
        value.shape.mixedBottomFadeEnd,
        value.shape.mixedTopFadeStart,
        value.shape.cumulusBottomFadeEnd,
        value.shape.cumulusTopFadeStart,
        value.shape.cumulusUpperMassBottom,
        value.shape.cumulusUpperMassStart,
        value.shape.cumulusUpperMassEnd,
        value.shape.localBaseLiftMaxMeters,
        value.shape.footprintCoverageInfluence,
        value.shape.cirrusFlowDirectionXZ.x,
        value.shape.cirrusFlowDirectionXZ.y,
        value.shape.cirrusBaseAlongScaleMeters,
        value.shape.cirrusBaseAcrossScaleMeters,
        value.shape.cirrusBaseVerticalScaleMeters,
        value.shape.cirrusDetailAlongScaleMeters,
        value.shape.cirrusDetailAcrossScaleMeters,
        value.shape.cirrusDetailVerticalScaleMeters,
        value.shape.cirrusMinimumThicknessMeters,
        value.shape.cirrusMaximumThicknessMeters,
        value.shape.cirrusVerticalProfileCenter,
        value.shape.cirrusVerticalProfileHalfWidth,
        value.domainBottomMeters, value.domainThicknessMeters,
        value.maximumViewTraceDistanceMeters,
        value.viewTraceFadeStartDistanceMeters,
        value.maximumLightTraceDistanceMeters,
        value.baseNoiseWorldSizeMeters,
        value.baseNoiseVerticalWorldSizeMeters,
        value.detailNoiseWorldSizeMeters,
        value.windDirection.x, value.windDirection.y,
        value.windDirection.z,
    }};
    for (float field : fields)
        HashFloat(hash, field);
    HashFloat(hash, value.windSpeedMetersPerSecond);
    return hash;
}
