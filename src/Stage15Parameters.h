// ============================================================================
//  Stage15Parameters.h - 최종 품질/콘셉트/진단 프리셋의 CPU 전용 계약
// ----------------------------------------------------------------------------
//  새 GPU 상수버퍼를 만들지 않고 descriptor를 기존 단계 5~14 상태로 해석한다.
// ============================================================================
#pragma once

#include "AtmosphereParameters.h"
#include "CloudAppearance.h"
#include "CloudFormationPresetStore.h"
#include "CloudDomainParameters.h"
#include "CloudLodParameters.h"
#include "CloudRimParameters.h"
#include "CloudShapeDomainContract.h"
#include "EnvironmentParameters.h"
#include "GroundLightingParameters.h"
#include "LightParameters.h"
#include "OptimizationParameters.h"
#include "Stage10UpsamplingParameters.h"
#include "Stage11TemporalParameters.h"
#include "Stage12ShadowParameters.h"

#include <array>
#include <cstdint>
#include <string>

enum class Stage15QualityPreset : std::uint32_t
{
    Low = 0,
    Medium = 1,
    High = 2,
    Custom = 3,
};

enum class Stage15ConceptPreset : std::uint32_t
{
    UrbanFairWeather = 0,
    MeadowBrokenClouds = 1,
    DesertCirrus = 2,
    SnowOvercast = 3,
    Custom = 4,
};

enum class Stage15DiagnosticMode : std::uint32_t
{
    None = 0,
    CaptureStill = 1,
    Reference = 2,
};

enum class Stage15CaptureState : std::uint32_t
{
    Inactive = 0,
    PendingNative = 1,
    Accumulating = 2,
    Ready = 3,
    Failed = 4,
};

// Overlay와 output smoke가 추정값이 아니라 실제 Win32/D3D 리소스 크기를
// 같은 이름으로 전달하도록 Renderer/NoiseLab 사이에 공유하는 CPU 계약이다.
struct Stage15OutputExtentSnapshot
{
    int physicalClientWidth = 0;
    int physicalClientHeight = 0;
    int swapChainWidth = 0;
    int swapChainHeight = 0;
    int viewportWidth = 0;
    int viewportHeight = 0;
    int sceneColorWidth = 0;
    int sceneColorHeight = 0;
    int sceneDepthWidth = 0;
    int sceneDepthHeight = 0;
    int cloudWidth = 0;
    int cloudHeight = 0;
    int historyWidth = 0;
    int historyHeight = 0;
    unsigned int dpi = 96;
    float dpiScale = 1.0f;
    bool perMonitorV2 = false;
    bool native1080p = false;
};

enum class Stage15TransitionFailurePoint : std::uint32_t
{
    CloudTargetPreflight = 0,
    ShadowResourcePreflight = 1,
    WeatherPreflight = 2,
};

struct Stage15TemporalTuning
{
    std::uint32_t neighborhoodClampingEnabled = 1u;
    float historyWeight = 0.90f;
    float sceneDepthRelativeThreshold = 0.0025f;
    float cloudDepthRelativeThreshold = 0.02f;
    float transmittanceThreshold = 0.08f;
    float nearHistoryFadeStartMeters = 1000.0f;
    float nearHistoryFadeEndMeters = 3000.0f;
    float maxReprojectionMotionPixels = 96.0f;
    float clipGamma = 1.0f;
};

struct Stage15QualityDescriptor
{
    Stage10ResolutionPreset resolutionPreset = Stage10ResolutionPreset::Half;
    Stage10UpsamplingParameters upsampling = {};
    Stage11TemporalMode temporalMode = Stage11TemporalMode::Stable4Phase;
    Stage15TemporalTuning temporalTuning = {};
    float viewStepMeters = 100.0f;
    std::uint32_t maximumViewSamples = 512u;
    float viewTransmittanceThreshold = 0.01f;
    float lightStepMeters = 250.0f;
    std::uint32_t maximumLightSamples = 80u;
    OptimizationParameters optimization = {};
    CloudLodParameters detailLod = {};
    Stage12ShadowMode shadowMode = Stage12ShadowMode::DeepCache;
    Stage12ShadowPreset shadowPreset = Stage12ShadowPreset::Balanced512;
};

struct Stage15ConceptDescriptor
{
    // Stage 15B 이후 구름 형성의 단일 원본. 아래 weather/appearance/domain/shape
    // 필드는 구형 CPU 테스트·metadata를 위한 호환 projection이며 ResolveConcept
    // 마지막에 반드시 이 snapshot에서 파생한다.
    CloudFormationSettings formation = {};
    Stage5WeatherPreset weatherPreset = Stage5WeatherPreset::PeriodicPerlin;
    WeatherMapGeneratorSettings weather = {};
    CloudAppearanceSettings appearance = {};
    CloudDomainParameters domain = {};
    CloudShapeParameters shape = {};
    float weatherWorldSizeMeters = 64000.0f;
    LightParameters light = {};
    Stage6SunPreset sunPreset = Stage6SunPreset::LowEast;
    Stage7PhasePreset phasePreset = Stage7PhasePreset::SilverLining;
    EnvironmentParameters environment = {};
    Stage8EnvironmentPreset environmentPreset = Stage8EnvironmentPreset::Custom;
    CloudRimParameters rim = {};
    AtmosphereParameters atmosphere = {};
    GroundLightingParameters ground = {};
    std::uint32_t surfaceShadowEnabled = 1u;
    float surfaceShadowStrength = 0.55f;
    float surfaceAmbientFloor = 0.35f;
};

struct Stage15ResolvedSettings
{
    Stage15QualityPreset qualityPreset = Stage15QualityPreset::Medium;
    Stage15ConceptPreset conceptPreset = Stage15ConceptPreset::UrbanFairWeather;
    Stage15DiagnosticMode diagnosticMode = Stage15DiagnosticMode::None;
    Stage15QualityDescriptor quality = {};
    Stage15ConceptDescriptor concept = {};
};

namespace stage15
{
inline constexpr std::uint32_t kCaptureSampleCount = 4u;
inline constexpr std::array<DirectX::XMFLOAT2, kCaptureSampleCount>
    kCaptureJitterPixels = {{
        { -0.25f, -0.25f },
        {  0.25f,  0.25f },
        {  0.25f, -0.25f },
        { -0.25f,  0.25f },
    }};

inline DirectX::XMFLOAT2 CaptureJitterForSample(std::uint32_t sampleIndex)
{
    return sampleIndex < kCaptureSampleCount
        ? kCaptureJitterPixels[sampleIndex] : DirectX::XMFLOAT2{};
}

inline Stage15CaptureState CaptureStateForCompletedSamples(
    std::uint32_t completedSamples, bool failed = false)
{
    if (failed)
        return Stage15CaptureState::Failed;
    return completedSamples >= kCaptureSampleCount
        ? Stage15CaptureState::Ready : Stage15CaptureState::Accumulating;
}

inline const char* CaptureStateName(Stage15CaptureState state)
{
    switch (state)
    {
    case Stage15CaptureState::Inactive: return "Inactive";
    case Stage15CaptureState::PendingNative: return "Pending Native";
    case Stage15CaptureState::Accumulating: return "Accumulating";
    case Stage15CaptureState::Ready: return "Ready";
    case Stage15CaptureState::Failed: return "Failed";
    }
    return "Inactive";
}

inline Stage15TemporalTuning CaptureTemporalTuning(
    const Stage11TemporalParameters& value)
{
    return {
        value.neighborhoodClampingEnabled,
        value.historyWeight,
        value.sceneDepthRelativeThreshold,
        value.cloudDepthRelativeThreshold,
        value.transmittanceThreshold,
        value.nearHistoryFadeStartMeters,
        value.nearHistoryFadeEndMeters,
        value.maxReprojectionMotionPixels,
        value.clipGamma,
    };
}

inline void ApplyTemporalTuning(Stage11TemporalParameters& target,
                                const Stage15TemporalTuning& tuning)
{
    target.neighborhoodClampingEnabled =
        tuning.neighborhoodClampingEnabled;
    target.historyWeight = tuning.historyWeight;
    target.sceneDepthRelativeThreshold =
        tuning.sceneDepthRelativeThreshold;
    target.cloudDepthRelativeThreshold =
        tuning.cloudDepthRelativeThreshold;
    target.transmittanceThreshold = tuning.transmittanceThreshold;
    target.nearHistoryFadeStartMeters =
        tuning.nearHistoryFadeStartMeters;
    target.nearHistoryFadeEndMeters = tuning.nearHistoryFadeEndMeters;
    target.maxReprojectionMotionPixels =
        tuning.maxReprojectionMotionPixels;
    target.clipGamma = tuning.clipGamma;
}

inline bool TemporalTuningEqual(const Stage15TemporalTuning& a,
                                const Stage15TemporalTuning& b)
{
    return a.neighborhoodClampingEnabled == b.neighborhoodClampingEnabled &&
        a.historyWeight == b.historyWeight &&
        a.sceneDepthRelativeThreshold == b.sceneDepthRelativeThreshold &&
        a.cloudDepthRelativeThreshold == b.cloudDepthRelativeThreshold &&
        a.transmittanceThreshold == b.transmittanceThreshold &&
        a.nearHistoryFadeStartMeters == b.nearHistoryFadeStartMeters &&
        a.nearHistoryFadeEndMeters == b.nearHistoryFadeEndMeters &&
        a.maxReprojectionMotionPixels == b.maxReprojectionMotionPixels &&
        a.clipGamma == b.clipGamma;
}

inline const char* QualityName(Stage15QualityPreset preset)
{
    switch (preset)
    {
    case Stage15QualityPreset::Low: return "Low";
    case Stage15QualityPreset::Medium: return "Medium";
    case Stage15QualityPreset::High: return "High";
    case Stage15QualityPreset::Custom: return "Custom";
    }
    return "Custom";
}

inline const char* ConceptName(Stage15ConceptPreset preset)
{
    switch (preset)
    {
    case Stage15ConceptPreset::UrbanFairWeather: return "Urban Fair Weather";
    case Stage15ConceptPreset::MeadowBrokenClouds: return "Meadow Broken Clouds";
    case Stage15ConceptPreset::DesertCirrus: return "Desert Cirrus";
    case Stage15ConceptPreset::SnowOvercast: return "Snow Overcast";
    case Stage15ConceptPreset::Custom: return "Custom";
    }
    return "Custom";
}

inline const char* DiagnosticName(Stage15DiagnosticMode mode)
{
    switch (mode)
    {
    case Stage15DiagnosticMode::None: return "None";
    case Stage15DiagnosticMode::CaptureStill: return "Capture Still";
    case Stage15DiagnosticMode::Reference: return "Reference";
    }
    return "None";
}

inline Stage15QualityDescriptor ResolveRealtimeQuality(
    Stage15QualityPreset preset)
{
    Stage15QualityDescriptor result;
    result.upsampling = Stage10UpsamplingParameters{};
    result.upsampling.filterMode = static_cast<std::uint32_t>(
        Stage10UpsampleFilter::Joint4);
    result.temporalTuning = CaptureTemporalTuning(
        Stage11TemporalParameters{});
    result.optimization = OptimizationParameters{};
    result.optimization.supportPrecheckEnabled = 1u;
    result.optimization.emptySpaceSkippingEnabled = 1u;
    result.optimization.viewEarlyExitEnabled = 1u;
    result.optimization.distanceStepEnabled = 1u;
    result.optimization.emptySamplesBeforeCoarse = 3u;
    result.optimization.coarseStepMultiplier = 2.0f;
    result.optimization.lightSamplingMode = static_cast<std::uint32_t>(
        Stage9LightSamplingMode::DeterministicCone);
    result.optimization.coneAngleDegrees = 2.0f;
    result.optimization.lightFarSampleFraction = 0.77f;
    result.detailLod.detailLodEnabled = 1u;

    switch (preset)
    {
    case Stage15QualityPreset::Low:
        result.viewStepMeters = 150.0f;
        result.maximumViewSamples = 384u;
        result.optimization.maxSearchStepMeters = 300.0f;
        result.optimization.distanceStepStartMeters = 12000.0f;
        result.optimization.distanceStepEndMeters = 48000.0f;
        result.optimization.farStepMultiplier = 1.5f;
        result.optimization.coneSampleCount = 5u;
        result.detailLod.detailLodStartMeters = 16000.0f;
        result.detailLod.detailLodEndMeters = 32000.0f;
        result.shadowPreset = Stage12ShadowPreset::Fast256;
        break;
    case Stage15QualityPreset::High:
        result.resolutionPreset = Stage10ResolutionPreset::Full;
        result.upsampling.filterMode = static_cast<std::uint32_t>(
            Stage10UpsampleFilter::Nearest);
        result.temporalMode = Stage11TemporalMode::FullResolution;
        result.temporalTuning.historyWeight = 0.85f;
        result.viewStepMeters = 100.0f;
        result.maximumViewSamples = 512u;
        result.optimization.maxSearchStepMeters = 200.0f;
        result.optimization.distanceStepStartMeters = 24000.0f;
        result.optimization.distanceStepEndMeters = 50000.0f;
        result.optimization.farStepMultiplier = 1.25f;
        result.optimization.coneSampleCount = 8u;
        result.detailLod.detailLodStartMeters = 48000.0f;
        result.detailLod.detailLodEndMeters = 60000.0f;
        break;
    case Stage15QualityPreset::Medium:
    case Stage15QualityPreset::Custom:
    default:
        result.viewStepMeters = 100.0f;
        result.maximumViewSamples = 512u;
        result.optimization.maxSearchStepMeters = 400.0f;
        result.optimization.distanceStepStartMeters = 16000.0f;
        result.optimization.distanceStepEndMeters = 48000.0f;
        result.optimization.farStepMultiplier = 1.5f;
        result.optimization.coneSampleCount = 6u;
        result.detailLod.detailLodStartMeters = 32000.0f;
        result.detailLod.detailLodEndMeters = 48000.0f;
        break;
    }
    result.upsampling.resolutionScale =
        stage10upsampling::ResolutionScale(result.resolutionPreset);
    result.upsampling = stage10upsampling::Sanitize(result.upsampling);
    result.optimization = stage9optimization::Sanitize(result.optimization);
    result.detailLod = stage13lod::Sanitize(result.detailLod);
    return result;
}

inline Stage15QualityDescriptor ResolveCaptureQuality()
{
    Stage15QualityDescriptor result{};
    result.resolutionPreset = Stage10ResolutionPreset::Full;
    result.upsampling = Stage10UpsamplingParameters{};
    result.upsampling.resolutionScale =
        stage10upsampling::ResolutionScale(result.resolutionPreset);
    result.upsampling.filterMode = static_cast<std::uint32_t>(
        Stage10UpsampleFilter::Nearest);
    result.temporalMode = Stage11TemporalMode::Off;
    result.temporalTuning = CaptureTemporalTuning(
        Stage11TemporalParameters{});
    result.viewStepMeters = 50.0f;
    result.maximumViewSamples = 1024u;
    result.viewTransmittanceThreshold = 0.005f;
    result.lightStepMeters = 250.0f;
    result.maximumLightSamples = 80u;
    result.optimization = OptimizationParameters{};
    result.optimization.supportPrecheckEnabled = 1u;
    result.optimization.emptySpaceSkippingEnabled = 1u;
    result.optimization.viewEarlyExitEnabled = 1u;
    result.optimization.distanceStepEnabled = 0u;
    result.optimization.emptySamplesBeforeCoarse = 3u;
    result.optimization.coarseStepMultiplier = 2.0f;
    result.optimization.maxSearchStepMeters = 100.0f;
    result.optimization.lightSamplingMode = static_cast<std::uint32_t>(
        Stage9LightSamplingMode::DeterministicCone);
    result.optimization.coneSampleCount = 8u;
    result.optimization.coneAngleDegrees = 2.0f;
    result.optimization.lightFarSampleFraction = 0.77f;
    result.detailLod = CloudLodParameters{};
    result.detailLod.detailLodEnabled = 0u;
    result.shadowMode = Stage12ShadowMode::DeepCache;
    result.shadowPreset = Stage12ShadowPreset::Balanced512;
    result.upsampling = stage10upsampling::Sanitize(result.upsampling);
    result.optimization = stage9optimization::Sanitize(result.optimization);
    result.detailLod = stage13lod::Sanitize(result.detailLod);
    return result;
}

inline Stage15QualityDescriptor ResolveReferenceQuality()
{
    Stage15QualityDescriptor result{};
    result.resolutionPreset = Stage10ResolutionPreset::Full;
    result.upsampling = Stage10UpsamplingParameters{};
    result.upsampling.resolutionScale =
        stage10upsampling::ResolutionScale(result.resolutionPreset);
    result.upsampling.filterMode = static_cast<std::uint32_t>(
        Stage10UpsampleFilter::Nearest);
    result.temporalMode = Stage11TemporalMode::Off;
    result.temporalTuning = CaptureTemporalTuning(
        Stage11TemporalParameters{});
    result.viewStepMeters = 50.0f;
    result.maximumViewSamples = 1024u;
    result.viewTransmittanceThreshold = 0.01f;
    result.lightStepMeters = 62.5f;
    result.maximumLightSamples = 320u;
    result.optimization = OptimizationParameters{};
    result.optimization.lightSamplingMode = static_cast<std::uint32_t>(
        Stage9LightSamplingMode::StraightRay);
    result.detailLod = CloudLodParameters{};
    result.detailLod.detailLodEnabled = 0u;
    result.shadowMode = Stage12ShadowMode::DirectReference;
    result.shadowPreset = Stage12ShadowPreset::Balanced512;
    result.upsampling = stage10upsampling::Sanitize(result.upsampling);
    result.optimization = stage9optimization::Sanitize(result.optimization);
    result.detailLod = stage13lod::Sanitize(result.detailLod);
    return result;
}

inline Stage15QualityDescriptor ResolveQuality(
    Stage15QualityPreset preset, Stage15DiagnosticMode diagnostic)
{
    switch (diagnostic)
    {
    case Stage15DiagnosticMode::CaptureStill:
        return ResolveCaptureQuality();
    case Stage15DiagnosticMode::Reference:
        return ResolveReferenceQuality();
    case Stage15DiagnosticMode::None:
    default:
        return ResolveRealtimeQuality(preset);
    }
}

inline bool QualityDescriptorEqual(const Stage15QualityDescriptor& a,
                                   const Stage15QualityDescriptor& b)
{
    return a.resolutionPreset == b.resolutionPreset &&
        a.upsampling.resolutionScale == b.upsampling.resolutionScale &&
        a.upsampling.filterMode == b.upsampling.filterMode &&
        a.upsampling.sceneDepthRelativeSigma ==
            b.upsampling.sceneDepthRelativeSigma &&
        a.upsampling.cloudDepthRelativeSigma ==
            b.upsampling.cloudDepthRelativeSigma &&
        a.upsampling.transmittanceSigma ==
            b.upsampling.transmittanceSigma &&
        a.upsampling.minimumWeight == b.upsampling.minimumWeight &&
        a.upsampling.padding0 == b.upsampling.padding0 &&
        a.upsampling.padding1 == b.upsampling.padding1 &&
        a.temporalMode == b.temporalMode &&
        TemporalTuningEqual(a.temporalTuning, b.temporalTuning) &&
        a.viewStepMeters == b.viewStepMeters &&
        a.maximumViewSamples == b.maximumViewSamples &&
        a.viewTransmittanceThreshold == b.viewTransmittanceThreshold &&
        a.lightStepMeters == b.lightStepMeters &&
        a.maximumLightSamples == b.maximumLightSamples &&
        a.optimization.supportPrecheckEnabled ==
            b.optimization.supportPrecheckEnabled &&
        a.optimization.emptySpaceSkippingEnabled ==
            b.optimization.emptySpaceSkippingEnabled &&
        a.optimization.viewEarlyExitEnabled ==
            b.optimization.viewEarlyExitEnabled &&
        a.optimization.distanceStepEnabled ==
            b.optimization.distanceStepEnabled &&
        a.optimization.emptySamplesBeforeCoarse ==
            b.optimization.emptySamplesBeforeCoarse &&
        a.optimization.baseDensityEpsilon ==
            b.optimization.baseDensityEpsilon &&
        a.optimization.coarseStepMultiplier ==
            b.optimization.coarseStepMultiplier &&
        a.optimization.maxSearchStepMeters ==
            b.optimization.maxSearchStepMeters &&
        a.optimization.distanceStepStartMeters ==
            b.optimization.distanceStepStartMeters &&
        a.optimization.distanceStepEndMeters ==
            b.optimization.distanceStepEndMeters &&
        a.optimization.farStepMultiplier ==
            b.optimization.farStepMultiplier &&
        a.optimization.optimizationPadding0 ==
            b.optimization.optimizationPadding0 &&
        a.optimization.lightSamplingMode ==
            b.optimization.lightSamplingMode &&
        a.optimization.coneSampleCount ==
            b.optimization.coneSampleCount &&
        a.optimization.coneAngleDegrees ==
            b.optimization.coneAngleDegrees &&
        a.optimization.lightFarSampleFraction ==
            b.optimization.lightFarSampleFraction &&
        a.detailLod.detailLodEnabled == b.detailLod.detailLodEnabled &&
        a.detailLod.detailLodStartMeters ==
            b.detailLod.detailLodStartMeters &&
        a.detailLod.detailLodEndMeters ==
            b.detailLod.detailLodEndMeters &&
        a.detailLod.detailNeutralValue == b.detailLod.detailNeutralValue &&
        a.shadowMode == b.shadowMode &&
        a.shadowPreset == b.shadowPreset;
}

// 태양 방향은 Atmosphere의 azimuth/elevation이 canonical source다. LightCB에서
// 그 파생값, Stage 15 Quality의 두 sampling budget, 독립 개발 tuning인 ray
// bias를 제외하면 나머지는 Concept 외형이다.
inline bool ConceptLightEqual(LightParameters a, LightParameters b)
{
    return a.sunIntensity == b.sunIntensity &&
        a.sunColor.x == b.sunColor.x &&
        a.sunColor.y == b.sunColor.y &&
        a.sunColor.z == b.sunColor.z &&
        a.singleScatteringAlbedo == b.singleScatteringAlbedo &&
        a.phaseEnabled == b.phaseEnabled &&
        a.forwardScatteringG == b.forwardScatteringG &&
        a.backwardScatteringG == b.backwardScatteringG &&
        a.phaseBlend == b.phaseBlend &&
        a.phaseIntensity == b.phaseIntensity &&
        a.edgeInfluence == b.edgeInfluence &&
        a.edgeOpticalDepthScale == b.edgeOpticalDepthScale &&
        a.shadowExponent == b.shadowExponent;
}

inline bool ConceptShadowEqual(const Stage12ShadowParameters& a,
                               const Stage12ShadowParameters& b)
{
    return a.surfaceShadowEnabled == b.surfaceShadowEnabled &&
        a.surfaceShadowStrength == b.surfaceShadowStrength &&
        a.surfaceAmbientFloor == b.surfaceAmbientFloor;
}

inline bool ConceptAtmosphereEqual(AtmosphereParameters a,
                                   AtmosphereParameters b)
{
    // LUT/화면 외형과 무관한 debugView/debugChannel/debugExposure/aerialSlice는
    // Concept ownership에서 제외한다. padding을 포함한 raw memory는 비교하지 않는다.
    return a.mode == b.mode &&
        a.preset == b.preset &&
        a.sunControlMode == b.sunControlMode &&
        a.bottomRadiusKm == b.bottomRadiusKm &&
        a.topRadiusKm == b.topRadiusKm &&
        a.rayleighScaleHeightKm == b.rayleighScaleHeightKm &&
        a.mieScaleHeightKm == b.mieScaleHeightKm &&
        a.rayleighScatteringPerKm.x == b.rayleighScatteringPerKm.x &&
        a.rayleighScatteringPerKm.y == b.rayleighScatteringPerKm.y &&
        a.rayleighScatteringPerKm.z == b.rayleighScatteringPerKm.z &&
        a.rayleighScale == b.rayleighScale &&
        a.mieScatteringPerKm == b.mieScatteringPerKm &&
        a.mieExtinctionPerKm == b.mieExtinctionPerKm &&
        a.mieAbsorptionScale == b.mieAbsorptionScale &&
        a.mieG == b.mieG &&
        a.ozoneAbsorptionPerKm.x == b.ozoneAbsorptionPerKm.x &&
        a.ozoneAbsorptionPerKm.y == b.ozoneAbsorptionPerKm.y &&
        a.ozoneAbsorptionPerKm.z == b.ozoneAbsorptionPerKm.z &&
        a.ozoneScale == b.ozoneScale &&
        a.ozoneCenterKm == b.ozoneCenterKm &&
        a.ozoneHalfWidthKm == b.ozoneHalfWidthKm &&
        a.turbidity == b.turbidity &&
        a.solarIrradiance.x == b.solarIrradiance.x &&
        a.solarIrradiance.y == b.solarIrradiance.y &&
        a.solarIrradiance.z == b.solarIrradiance.z &&
        a.sunAzimuthDegrees == b.sunAzimuthDegrees &&
        a.sunElevationDegrees == b.sunElevationDegrees &&
        a.timeOfDayHours == b.timeOfDayHours &&
        a.timePlaybackMinutesPerSecond ==
            b.timePlaybackMinutesPerSecond &&
        a.timePlaybackEnabled == b.timePlaybackEnabled &&
        a.timeLoopEnabled == b.timeLoopEnabled;
}

inline PeriodicChannelSettings Channel(
    std::uint32_t seed, std::uint32_t macroPeriod,
    std::uint32_t detailPeriod, float detailWeight,
    float bias, float contrast)
{
    return { seed, macroPeriod, detailPeriod, detailWeight, bias, contrast };
}

inline Stage15ConceptDescriptor ResolveConcept(Stage15ConceptPreset preset)
{
    Stage15ConceptDescriptor result;
    result.domain.domainType = static_cast<std::uint32_t>(
        CloudDomainType::PlanarLayer);
    result.shape.shapeMode = static_cast<std::uint32_t>(
        CloudShapeMode::WeatherPhysicalThickness);
    stage6light::ApplyPhasePreset(result.light, Stage7PhasePreset::SilverLining);
    stage8environment::ApplyPreset(
        result.environment, Stage8EnvironmentPreset::PortfolioHero);
    stage14atmosphere::ApplyPreset(result.atmosphere, AtmospherePreset::EarthClear);
    result.atmosphere.mode = AtmosphereMode::Physical;

    switch (preset)
    {
    case Stage15ConceptPreset::MeadowBrokenClouds:
        result.appearance = DenseMixedAppearance();
        result.appearance.globalCoverage = 0.64f;
        result.appearance.densityMultiplier = 1.20f;
        result.appearance.extinctionPerMeter = 0.00039f;
        result.appearance.detailErosion = 0.20f;
        result.appearance.stratusMinimumThicknessMeters = 1500.0f;
        result.appearance.stratusMaximumThicknessMeters = 2500.0f;
        result.appearance.cumulusMinimumThicknessMeters = 3000.0f;
        result.appearance.cumulusMaximumThicknessMeters = 4600.0f;
        result.appearance.localBaseLiftMaxMeters = 200.0f;
        result.appearance.footprintCoverageInfluence = 0.40f;
        result.weather.cloudTypeMode = CloudTypeMode::WeatherMap;
        result.weather.coverage = Channel(1103u, 3u, 9u, 0.38f, 0.0f, 1.05f);
        result.weather.cloudType = Channel(2101u, 2u, 5u, 0.28f, 0.08f, 1.0f);
        result.weather.density = Channel(3109u, 3u, 7u, 0.30f, 0.05f, 0.90f);
        result.weather.localThickness = Channel(4103u, 2u, 6u, 0.32f, 0.05f, 1.05f);
        result.weather.coverageThreshold = 0.485f;
        result.weather.coverageSoftness = 0.18f;
        result.domain.cloudBottomAltitude = 1500.0f;
        result.domain.cloudLayerThickness = 5000.0f;
        result.weatherWorldSizeMeters = 64000.0f;
        result.light.sunIntensity = 0.95f;
        result.light.forwardScatteringG = 0.75f;
        result.light.edgeInfluence = 0.70f;
        result.light.edgeOpticalDepthScale = 1.8f;
        result.light.shadowExponent = 1.30f;
        result.environment.physicalSkyFillScale = 0.95f;
        result.environment.physicalGroundFillScale = 0.95f;
        result.environment.multipleScatteringInteriorBlend = 0.60f;
        cloudrim::ApplyPreset(result.rim, CloudRimPreset::MeadowNte);
        stage14ground::ApplyPreset(result.ground, GroundMaterialPreset::Grass);
        result.ground.bounceMultiplier = 1.0f;
        result.surfaceShadowStrength = 0.65f;
        break;
    case Stage15ConceptPreset::DesertCirrus:
        result.appearance = DenseMixedAppearance();
        // Weather R의 15~30% 점유율과 density coverage는 다른 계약이다.
        // 현재 RemapCoverage 의미에서 0.22는 Texture3D 최대값까지 잘라 Cirrus가
        // 완전히 사라지므로, Weather mask 안의 가느다란 결을 남기는 0.42로 보정한다.
        result.appearance.globalCoverage = 0.42f;
        result.appearance.densityMultiplier = 0.40f;
        result.appearance.extinctionPerMeter = 0.00010f;
        result.appearance.detailErosion = 0.22f;
        result.appearance.localBaseLiftMaxMeters = 0.0f;
        result.appearance.footprintCoverageInfluence = 0.0f;
        result.weather.cloudTypeMode = CloudTypeMode::WeatherMap;
        result.weather.coverage = Channel(1201u, 2u, 8u, 0.30f, -0.04f, 1.20f);
        result.weather.cloudType = Channel(2203u, 2u, 4u, 0.20f, 0.0f, 0.85f);
        result.weather.density = Channel(3203u, 2u, 6u, 0.22f, -0.10f, 0.80f);
        result.weather.localThickness = Channel(4201u, 2u, 5u, 0.18f, -0.05f, 0.75f);
        result.weather.coverageThreshold = 0.52f;
        result.weather.coverageSoftness = 0.13f;
        result.domain.cloudBottomAltitude = 7000.0f;
        result.domain.cloudLayerThickness = 3500.0f;
        result.domain.maxViewTraceDistance = 60000.0f;
        result.domain.viewTraceFadeStartDistance = 50000.0f;
        result.weatherWorldSizeMeters = 128000.0f;
        result.shape.shapeMode = static_cast<std::uint32_t>(
            CloudShapeMode::CirrusPhysicalLayer);
        result.light.sunIntensity = 1.05f;
        result.light.forwardScatteringG = 0.78f;
        result.light.edgeInfluence = 0.20f;
        result.light.edgeOpticalDepthScale = 1.2f;
        result.light.shadowExponent = 1.05f;
        result.environment.physicalSkyFillScale = 1.0f;
        result.environment.physicalGroundFillScale = 1.0f;
        result.environment.multipleScatteringInteriorBlend = 0.25f;
        cloudrim::ApplyPreset(result.rim, CloudRimPreset::Off);
        stage14atmosphere::ApplyPreset(result.atmosphere, AtmospherePreset::EarthHazy);
        stage14ground::ApplyPreset(result.ground, GroundMaterialPreset::Desert);
        result.ground.bounceMultiplier = 0.5f;
        result.surfaceShadowStrength = 0.10f;
        break;
    case Stage15ConceptPreset::SnowOvercast:
        result.appearance = StratusAppearance();
        result.appearance.globalCoverage = 0.90f;
        result.appearance.densityMultiplier = 1.25f;
        result.appearance.extinctionPerMeter = 0.00046f;
        result.appearance.detailErosion = 0.10f;
        result.appearance.stratusMinimumThicknessMeters = 1500.0f;
        result.appearance.stratusMaximumThicknessMeters = 2300.0f;
        result.appearance.localBaseLiftMaxMeters = 0.0f;
        result.appearance.footprintCoverageInfluence = 0.20f;
        result.weather.cloudTypeMode = CloudTypeMode::Stratus;
        result.weather.coverage = Channel(1301u, 2u, 6u, 0.20f, 0.10f, 0.85f);
        result.weather.cloudType = Channel(2309u, 2u, 4u, 0.15f, -0.30f, 0.60f);
        result.weather.density = Channel(3301u, 2u, 5u, 0.18f, 0.05f, 0.75f);
        result.weather.localThickness = Channel(4303u, 2u, 4u, 0.15f, 0.0f, 0.70f);
        result.weather.coverageThreshold = 0.46f;
        result.weather.coverageSoftness = 0.20f;
        result.domain.cloudBottomAltitude = 1500.0f;
        result.domain.cloudLayerThickness = 2500.0f;
        result.weatherWorldSizeMeters = 64000.0f;
        result.light.sunIntensity = 0.75f;
        result.light.forwardScatteringG = 0.75f;
        result.light.edgeInfluence = 0.35f;
        result.light.edgeOpticalDepthScale = 1.4f;
        result.light.shadowExponent = 1.15f;
        result.environment.physicalSkyFillScale = 1.10f;
        result.environment.physicalGroundFillScale = 1.10f;
        result.environment.multipleScatteringInteriorBlend = 0.80f;
        cloudrim::ApplyPreset(result.rim, CloudRimPreset::Off);
        stage14ground::ApplyPreset(result.ground, GroundMaterialPreset::Snow);
        result.ground.bounceMultiplier = 1.5f;
        result.surfaceShadowStrength = 0.40f;
        break;
    case Stage15ConceptPreset::UrbanFairWeather:
    case Stage15ConceptPreset::Custom:
    default:
        result.appearance = CumulusAppearance();
        result.appearance.globalCoverage = 0.38f;
        result.appearance.densityMultiplier = 1.10f;
        result.appearance.extinctionPerMeter = 0.00036f;
        result.appearance.detailErosion = 0.24f;
        result.appearance.cumulusMinimumThicknessMeters = 2000.0f;
        result.appearance.cumulusMaximumThicknessMeters = 3200.0f;
        result.appearance.localBaseLiftMaxMeters = 300.0f;
        result.appearance.footprintCoverageInfluence = 0.50f;
        result.weather.cloudTypeMode = CloudTypeMode::Cumulus;
        result.weather.coverage = Channel(1013u, 4u, 11u, 0.42f, -0.02f, 1.15f);
        result.weather.cloudType = Channel(2017u, 2u, 4u, 0.20f, 0.20f, 0.85f);
        result.weather.density = Channel(3019u, 3u, 6u, 0.25f, 0.0f, 0.75f);
        result.weather.localThickness = Channel(4021u, 2u, 5u, 0.20f, 0.0f, 0.90f);
        result.weather.coverageThreshold = 0.53f;
        result.weather.coverageSoftness = 0.14f;
        result.domain.cloudBottomAltitude = 1800.0f;
        result.domain.cloudLayerThickness = 3700.0f;
        result.weatherWorldSizeMeters = 64000.0f;
        result.light.sunIntensity = 1.0f;
        result.light.forwardScatteringG = 0.75f;
        result.light.edgeInfluence = 0.85f;
        result.light.edgeOpticalDepthScale = 2.0f;
        result.light.shadowExponent = 1.35f;
        result.environment.physicalSkyFillScale = 0.85f;
        result.environment.physicalGroundFillScale = 0.85f;
        result.environment.multipleScatteringInteriorBlend = 0.55f;
        cloudrim::ApplyPreset(result.rim, CloudRimPreset::UrbanNte);
        stage14ground::ApplyPreset(result.ground, GroundMaterialPreset::Concrete);
        result.ground.bounceMultiplier = 1.0f;
        result.surfaceShadowStrength = 0.55f;
        break;
    }

    CloudFormationConcept formationConcept =
        CloudFormationConcept::UrbanFairWeather;
    switch (preset)
    {
    case Stage15ConceptPreset::MeadowBrokenClouds:
        formationConcept = CloudFormationConcept::MeadowBrokenClouds;
        break;
    case Stage15ConceptPreset::DesertCirrus:
        formationConcept = CloudFormationConcept::DesertCirrus;
        break;
    case Stage15ConceptPreset::SnowOvercast:
        formationConcept = CloudFormationConcept::SnowOvercast;
        break;
    case Stage15ConceptPreset::UrbanFairWeather:
    case Stage15ConceptPreset::Custom:
    default:
        formationConcept = CloudFormationConcept::UrbanFairWeather;
        break;
    }
    CloudFormationSettings canonicalFormation;
    PreparedCloudFormation preparedFormation;
    std::string formationStatus;
    if (ResolveBuiltInCloudFormation(
            formationConcept, canonicalFormation) &&
        PrepareCloudFormationSettings(
            canonicalFormation, preparedFormation, formationStatus, 200.0f))
    {
        result.formation = preparedFormation.settings;
        result.weatherPreset = result.formation.weatherPreset;
        result.weather = result.formation.weather;
        result.shape = result.formation.shape;
        result.domain = preparedFormation.domain;
        result.weatherWorldSizeMeters =
            result.formation.weatherWorldSizeMeters;

        CloudParameters compatibilityCloud;
        compatibilityCloud.coverage = result.formation.coverage;
        compatibilityCloud.densityMultiplier =
            result.formation.densityMultiplier;
        compatibilityCloud.extinctionCoefficient =
            result.formation.extinctionPerMeter;
        compatibilityCloud.detailErosionStrength =
            result.formation.detailErosion;
        result.appearance = CaptureCloudAppearance(
            compatibilityCloud, result.shape, result.weather);
    }
    result.light = stage6light::Sanitize(result.light);
    result.environment = stage8environment::Sanitize(result.environment);
    result.rim = cloudrim::Sanitize(result.rim);
    result.atmosphere = stage14atmosphere::Sanitize(result.atmosphere);
    result.ground = stage14ground::Sanitize(result.ground);
    return result;
}

inline Stage15ResolvedSettings Resolve(
    Stage15QualityPreset quality, Stage15ConceptPreset concept,
    Stage15DiagnosticMode diagnostic)
{
    Stage15ResolvedSettings result;
    result.qualityPreset = quality;
    result.conceptPreset = concept;
    result.diagnosticMode = diagnostic;
    result.quality = ResolveQuality(quality, diagnostic);
    result.concept = ResolveConcept(concept);
    return result;
}
}
