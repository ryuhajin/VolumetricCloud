#include "Stage15CirrusMath.h"
#include "Stage15Parameters.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace
{
[[noreturn]] void Fail(const char* message)
{
    std::fprintf(stderr, "Stage15PresetMath failure: %s\n", message);
    std::exit(1);
}

bool Near(float a, float b, float epsilon = 1.0e-4f)
{
    return std::abs(a - b) <= epsilon;
}

double WeatherOccupancy(const WeatherMapData& map)
{
    std::size_t occupied = 0;
    const std::size_t count = map.rgba.size() / 4u;
    for (std::size_t i = 0; i < count; ++i)
    {
        if (map.rgba[i * 4u] >= 128u)
            ++occupied;
    }
    return static_cast<double>(occupied) /
        static_cast<double>(std::max<std::size_t>(count, 1u));
}
}

int main()
{
    using namespace stage15;
    using namespace std::literals;
    if (cloudshape::ModeName(static_cast<std::uint32_t>(
            CloudShapeMode::LegacyNormalizedLayer)) !=
            "legacyNormalizedLayer"sv ||
        cloudshape::ModeName(static_cast<std::uint32_t>(
            CloudShapeMode::WeatherPhysicalThickness)) !=
            "weatherPhysicalThickness"sv ||
        cloudshape::ModeName(static_cast<std::uint32_t>(
            CloudShapeMode::CirrusPhysicalLayer)) !=
            "cirrusPhysicalLayer"sv ||
        cloudshape::ModeDisplayName(static_cast<std::uint32_t>(
            CloudShapeMode::CirrusPhysicalLayer)) !=
            "Cirrus Physical Layer"sv)
        Fail("cloud shape names must preserve the schema/UI contract");
    if (cloudshape::UsesPhysicalBulkAdvection(static_cast<std::uint32_t>(
            CloudShapeMode::LegacyNormalizedLayer)) ||
        !cloudshape::UsesPhysicalBulkAdvection(static_cast<std::uint32_t>(
            CloudShapeMode::WeatherPhysicalThickness)) ||
        !cloudshape::UsesPhysicalBulkAdvection(static_cast<std::uint32_t>(
            CloudShapeMode::CirrusPhysicalLayer)) ||
        cloudshape::UsesPhysicalBulkAdvection(999u))
        Fail("only Weather and Cirrus physical modes use bulk advection");

    const Stage15QualityDescriptor low = ResolveRealtimeQuality(
        Stage15QualityPreset::Low);
    const Stage15QualityDescriptor medium = ResolveRealtimeQuality(
        Stage15QualityPreset::Medium);
    const Stage15QualityDescriptor high = ResolveRealtimeQuality(
        Stage15QualityPreset::High);

    if (!(low.viewStepMeters > medium.viewStepMeters &&
          low.maximumViewSamples < medium.maximumViewSamples &&
          low.optimization.coneSampleCount < medium.optimization.coneSampleCount &&
          medium.optimization.coneSampleCount < high.optimization.coneSampleCount))
        Fail("quality sampling budgets must be monotonic");
    if (!Near(medium.viewStepMeters, 100.0f) ||
        medium.maximumViewSamples != 512u ||
        !Near(medium.optimization.distanceStepStartMeters, 16000.0f) ||
        !Near(medium.optimization.distanceStepEndMeters, 48000.0f) ||
        !Near(medium.optimization.farStepMultiplier, 1.5f) ||
        medium.optimization.coneSampleCount != 6u ||
        medium.maximumLightSamples != 80u ||
        !Near(medium.lightStepMeters, 250.0f) ||
        !Near(medium.viewTransmittanceThreshold, 0.01f) ||
        medium.upsampling.filterMode != static_cast<std::uint32_t>(
            Stage10UpsampleFilter::Joint4) ||
        !Near(medium.upsampling.resolutionScale, 0.5f) ||
        !Near(medium.upsampling.sceneDepthRelativeSigma, 0.0025f) ||
        !Near(medium.upsampling.cloudDepthRelativeSigma, 0.01f) ||
        !Near(medium.upsampling.transmittanceSigma, 0.10f) ||
        !Near(medium.upsampling.minimumWeight, 1.0e-4f) ||
        medium.shadowPreset != Stage12ShadowPreset::Balanced512)
        Fail("Medium must exactly preserve the approved Balanced inputs");
    if (high.resolutionPreset != Stage10ResolutionPreset::Full ||
        !Near(high.upsampling.resolutionScale, 1.0f) ||
        high.upsampling.filterMode != static_cast<std::uint32_t>(
            Stage10UpsampleFilter::Nearest) ||
        high.temporalMode != Stage11TemporalMode::FullResolution ||
        !Near(high.temporalTuning.historyWeight, 0.85f) ||
        !Near(high.viewStepMeters, 100.0f) ||
        high.maximumViewSamples != 512u ||
        !Near(high.optimization.maxSearchStepMeters, 200.0f) ||
        !Near(high.optimization.distanceStepStartMeters, 24000.0f) ||
        !Near(high.optimization.distanceStepEndMeters, 50000.0f) ||
        !Near(high.optimization.farStepMultiplier, 1.25f) ||
        high.optimization.coneSampleCount != 8u ||
        high.shadowPreset != Stage12ShadowPreset::Balanced512)
        Fail("High must be the initial Full/Temporal performance candidate");
    if (low.optimization.coarseStepMultiplier != 2.0f ||
        low.optimization.farStepMultiplier != 1.5f)
        Fail("Low must not reuse the rejected Stage 9 Fast multipliers");
    Stage15QualityDescriptor customQuality = medium;
    customQuality.viewTransmittanceThreshold = 0.02f;
    customQuality.upsampling.cloudDepthRelativeSigma = 0.04f;
    customQuality.temporalTuning.historyWeight = 0.80f;
    if (QualityDescriptorEqual(customQuality, medium))
        Fail("quality equality must include early-exit and upsampling tuning");

    const Stage15QualityDescriptor capture = ResolveCaptureQuality();
    const Stage15QualityDescriptor captureFromLow = ResolveQuality(
        Stage15QualityPreset::Low, Stage15DiagnosticMode::CaptureStill);
    const Stage15QualityDescriptor captureFromMedium = ResolveQuality(
        Stage15QualityPreset::Medium, Stage15DiagnosticMode::CaptureStill);
    const Stage15QualityDescriptor captureFromHigh = ResolveQuality(
        Stage15QualityPreset::High, Stage15DiagnosticMode::CaptureStill);
    const Stage15QualityDescriptor reference = ResolveReferenceQuality();
    if (!QualityDescriptorEqual(capture, captureFromLow) ||
        !QualityDescriptorEqual(capture, captureFromMedium) ||
        !QualityDescriptorEqual(capture, captureFromHigh))
        Fail("Capture resolver must not inherit the previous realtime quality");
    if (capture.resolutionPreset != Stage10ResolutionPreset::Full ||
        capture.upsampling.filterMode != static_cast<std::uint32_t>(
            Stage10UpsampleFilter::Nearest) ||
        capture.temporalMode != Stage11TemporalMode::Off ||
        !Near(capture.viewStepMeters, 50.0f) ||
        capture.maximumViewSamples != 1024u ||
        !Near(capture.viewTransmittanceThreshold, 0.005f) ||
        capture.optimization.supportPrecheckEnabled != 1u ||
        capture.optimization.emptySpaceSkippingEnabled != 1u ||
        capture.optimization.viewEarlyExitEnabled != 1u ||
        capture.optimization.distanceStepEnabled != 0u ||
        !Near(capture.optimization.maxSearchStepMeters, 100.0f) ||
        capture.optimization.coneSampleCount != 8u ||
        !Near(capture.optimization.coneAngleDegrees, 2.0f) ||
        !Near(capture.optimization.lightFarSampleFraction, 0.77f) ||
        capture.detailLod.detailLodEnabled != 0u ||
        capture.shadowMode != Stage12ShadowMode::DeepCache ||
        capture.shadowPreset != Stage12ShadowPreset::Balanced512)
        Fail("Capture Still diagnostic contract is incomplete");
    if (reference.resolutionPreset != Stage10ResolutionPreset::Full ||
        reference.temporalMode != Stage11TemporalMode::Off ||
        !stage9optimization::UsesReferenceShader(reference.optimization) ||
        reference.shadowMode != Stage12ShadowMode::DirectReference)
        Fail("Reference diagnostic must use Fine/Direct reference paths");

    DirectX::XMFLOAT2 captureJitterSum{};
    for (std::uint32_t sample = 0; sample < kCaptureSampleCount; ++sample)
    {
        const DirectX::XMFLOAT2 jitter = CaptureJitterForSample(sample);
        captureJitterSum.x += jitter.x;
        captureJitterSum.y += jitter.y;
        for (std::uint32_t earlier = 0; earlier < sample; ++earlier)
        {
            const DirectX::XMFLOAT2 previous =
                CaptureJitterForSample(earlier);
            if (jitter.x == previous.x && jitter.y == previous.y)
                Fail("Capture jitter samples must be unique");
        }
    }
    const DirectX::XMFLOAT2 invalidJitter =
        CaptureJitterForSample(kCaptureSampleCount);
    if (!Near(captureJitterSum.x, 0.0f) ||
        !Near(captureJitterSum.y, 0.0f) ||
        !Near(invalidJitter.x, 0.0f) || !Near(invalidJitter.y, 0.0f))
        Fail("Capture jitter must be zero-mean with a safe invalid sample");
    if (CaptureStateForCompletedSamples(0u) !=
            Stage15CaptureState::Accumulating ||
        CaptureStateForCompletedSamples(3u) !=
            Stage15CaptureState::Accumulating ||
        CaptureStateForCompletedSamples(4u) != Stage15CaptureState::Ready ||
        CaptureStateForCompletedSamples(0u, true) !=
            Stage15CaptureState::Failed)
        Fail("Capture state helper must finish exactly after four samples");

    const Stage15ConceptPreset concepts[] = {
        Stage15ConceptPreset::UrbanFairWeather,
        Stage15ConceptPreset::MeadowBrokenClouds,
        Stage15ConceptPreset::DesertCirrus,
        Stage15ConceptPreset::SnowOvercast,
    };
    const double minimumOccupancy[] = { 0.30, 0.55, 0.15, 0.85 };
    const double maximumOccupancy[] = { 0.45, 0.70, 0.30, 0.95 };
    std::uint64_t previousHash = 0;
    bool occupancyPassed = true;
    for (std::size_t i = 0; i < 4; ++i)
    {
        const Stage15ConceptDescriptor first = ResolveConcept(concepts[i]);
        const Stage15ConceptDescriptor second = ResolveConcept(concepts[i]);
        const WeatherMapData firstMap = BuildWeatherMap(
            first.weatherPreset, first.weather);
        const WeatherMapData secondMap = BuildWeatherMap(
            second.weatherPreset, second.weather);
        const std::uint64_t hash = HashWeatherMap(firstMap);
        if (hash == 0u || hash != HashWeatherMap(secondMap) ||
            (previousHash != 0u && hash == previousHash))
            Fail("concept Weather maps must be deterministic and distinct");
        previousHash = hash;
        const double occupancy = WeatherOccupancy(firstMap);
        std::printf("[STAGE15][WEATHER] %s occupancy=%.5f hash=%llu\n",
                    ConceptName(concepts[i]), occupancy,
                    static_cast<unsigned long long>(hash));
        occupancyPassed &= occupancy >= minimumOccupancy[i] &&
                           occupancy <= maximumOccupancy[i];
    }
    if (!occupancyPassed)
        Fail("concept Weather R occupancy is outside its target range");

    const Stage15ConceptDescriptor urban = ResolveConcept(
        Stage15ConceptPreset::UrbanFairWeather);
    const Stage15ConceptDescriptor desert = ResolveConcept(
        Stage15ConceptPreset::DesertCirrus);
    const Stage15ConceptDescriptor snow = ResolveConcept(
        Stage15ConceptPreset::SnowOvercast);
    if (!Near(urban.domain.cloudBottomAltitude, 1800.0f) ||
        !Near(urban.domain.cloudLayerThickness, 3700.0f) ||
        urban.ground.preset != GroundMaterialPreset::Concrete ||
        !Near(urban.ground.bounceMultiplier, 1.0f))
        Fail("Urban concept mapping is incorrect");
    if (desert.shape.shapeMode != static_cast<std::uint32_t>(
            CloudShapeMode::CirrusPhysicalLayer) ||
        !Near(desert.domain.maxViewTraceDistance, 60000.0f) ||
        !Near(desert.domain.viewTraceFadeStartDistance, 50000.0f) ||
        desert.atmosphere.preset != AtmospherePreset::EarthHazy ||
        desert.ground.preset != GroundMaterialPreset::Desert ||
        !Near(desert.ground.bounceMultiplier, 0.5f))
        Fail("Desert Cirrus must preserve the 60 km far-plane contract");
    if (snow.ground.preset != GroundMaterialPreset::Snow ||
        !Near(snow.ground.bounceMultiplier, 1.5f))
        Fail("Snow ground bounce mapping is incorrect");

    LightParameters conceptLightA = urban.light;
    LightParameters conceptLightB = conceptLightA;
    conceptLightB.maxLightSteps += 1u;
    conceptLightB.lightStepSize *= 0.5f;
    conceptLightB.lightRayBias *= 2.0f;
    if (!ConceptLightEqual(conceptLightA, conceptLightB))
        Fail("quality/developer light sampling must not change Concept ownership");
    conceptLightB = conceptLightA;
    conceptLightB.sunColor.x *= 0.5f;
    if (ConceptLightEqual(conceptLightA, conceptLightB))
        Fail("Sun Tint edits must make Concept Custom");
    conceptLightB = conceptLightA;
    conceptLightB.phaseBlend *= 0.5f;
    if (ConceptLightEqual(conceptLightA, conceptLightB))
        Fail("phase appearance edits must make Concept Custom");
    Stage12ShadowParameters conceptShadowA;
    Stage12ShadowParameters conceptShadowB = conceptShadowA;
    conceptShadowB.shadowPreset = static_cast<std::uint32_t>(
        Stage12ShadowPreset::Fast256);
    if (!ConceptShadowEqual(conceptShadowA, conceptShadowB))
        Fail("quality-owned Shadow preset must not change Concept ownership");
    conceptShadowB = conceptShadowA;
    conceptShadowB.surfaceShadowStrength = 0.25f;
    if (ConceptShadowEqual(conceptShadowA, conceptShadowB))
        Fail("surface shadow appearance edits must make Concept Custom");
    conceptShadowB = conceptShadowA;
    conceptShadowB.surfaceShadowEnabled = 0u;
    if (ConceptShadowEqual(conceptShadowA, conceptShadowB))
        Fail("surface shadow enable edits must make Concept Custom");
    conceptShadowB = conceptShadowA;
    conceptShadowB.surfaceAmbientFloor = 0.5f;
    if (ConceptShadowEqual(conceptShadowA, conceptShadowB))
        Fail("surface shadow ambient floor edits must make Concept Custom");

    Stage15ResolvedSettings ownership = Resolve(
        Stage15QualityPreset::Low,
        Stage15ConceptPreset::MeadowBrokenClouds,
        Stage15DiagnosticMode::None);
    const std::uint64_t meadowHash = HashWeatherMap(BuildWeatherMap(
        ownership.concept.weatherPreset, ownership.concept.weather));
    ownership.quality = ResolveRealtimeQuality(Stage15QualityPreset::High);
    if (HashWeatherMap(BuildWeatherMap(
            ownership.concept.weatherPreset, ownership.concept.weather)) != meadowHash ||
        ownership.concept.ground.preset != GroundMaterialPreset::Grass)
        Fail("quality resolution must not mutate concept-owned appearance");

    if (sizeof(CloudShapeParameters) != 112u ||
        offsetof(CloudShapeParameters, cirrusFlowDirectionXZ) != 64u ||
        offsetof(CloudShapeParameters, cirrusMinimumThicknessMeters) != 96u)
        Fail("CloudShapeCB b7 ABI must be 112 bytes with an appended Cirrus block");

    CloudShapeParameters invalid;
    invalid.shapeMode = static_cast<std::uint32_t>(
        CloudShapeMode::CirrusPhysicalLayer);
    invalid.cirrusFlowDirectionXZ = { NAN, INFINITY };
    invalid.cirrusBaseAlongScaleMeters = NAN;
    invalid.cirrusMinimumThicknessMeters = -100.0f;
    invalid.cirrusMaximumThicknessMeters = INFINITY;
    invalid.cirrusVerticalProfileCenter = NAN;
    invalid.cirrusVerticalProfileHalfWidth = -1.0f;
    const CloudShapeParameters safe = SanitizeCloudShapeParameters(invalid);
    const stage15cirrus::Basis basis = stage15cirrus::DirectionBasis(safe);
    const float dot = basis.along.x * basis.across.x +
                      basis.along.y * basis.across.y;
    if (!std::isfinite(safe.cirrusBaseAlongScaleMeters) ||
        safe.cirrusMaximumThicknessMeters < safe.cirrusMinimumThicknessMeters ||
        !Near(dot, 0.0f, 1.0e-5f))
        Fail("Cirrus sanitize and directional basis must remain finite");

    const DirectX::XMFLOAT3 world = { 12345.0f, 8750.0f, -6789.0f };
    const DirectX::XMFLOAT3 uvwA = stage15cirrus::DirectionalUvw(
        world, desert.shape, false);
    const DirectX::XMFLOAT3 uvwB = stage15cirrus::DirectionalUvw(
        world, desert.shape, false);
    if (!Near(uvwA.x, uvwB.x) || !Near(uvwA.y, uvwB.y) ||
        !Near(uvwA.z, uvwB.z))
        Fail("Cirrus coordinates must be world anchored and camera independent");
    const float localThickness = stage15cirrus::LocalThickness(
        0.5f, desert.shape);
    const float centerY = desert.domain.cloudBottomAltitude +
        desert.domain.cloudLayerThickness *
            desert.shape.cirrusVerticalProfileCenter;
    const float centerHeight = stage15cirrus::LocalHeightFraction(
        centerY, desert.domain.cloudBottomAltitude,
        desert.domain.cloudBottomAltitude + desert.domain.cloudLayerThickness,
        localThickness, desert.shape);
    if (!Near(centerHeight, 0.5f) ||
        stage15cirrus::VerticalProfile(centerHeight, desert.shape) <= 0.99f ||
        stage15cirrus::VerticalProfile(-0.01f, desert.shape) != 0.0f ||
        stage15cirrus::VerticalProfile(1.01f, desert.shape) != 0.0f)
        Fail("Cirrus profile must peak in the local layer and be zero outside");

    std::puts("Stage15PresetMath passed");
    return 0;
}
