// ============================================================================
//  CloudParameters.h - 단계 5 Weather Map/Cloud Type CPU/GPU 공유 설정
// ============================================================================
#pragma once

#include <DirectXMath.h>
#include <cstdint>

enum class CloudDebugMode : std::int32_t
{
    Composite = 0,
    Transmittance = 8,
    ConstantDensity = 9,
    RawNoise = 10,
    ThresholdDensity = 11,
    FinalDensity = 12,
    NoiseUvw = 13,
    HeightFraction = 14,
    HeightProfile = 15,
    BaseDensity = 16,
    DetailNoise = 17,
    Erosion = 18,
    DetailSampleMask = 19,
    WeatherCoverage = 20,
    CloudType = 21,
    WeatherThresholdDensity = 22,
    TypedShapeProfile = 23,
    LightTransmittance = 24,
    LightOpticalDepth = 25,
    TotalLightSamples = 26,
    DirectSingleScattering = 27,
    PhaseCosTheta = 28,
    ForwardPhaseLobe = 29,
    BackwardPhaseLobe = 30,
    DualPhaseFactor = 31,
    AccumulatedDirectLighting = 32,
    CloudSegmentLength = 33,
    ActualViewStepLength = 34,
    CloudHitMask = 35,
    BaseVolumeR = 36,
    BaseVolumeG = 37,
    BaseVolumeB = 38,
    BaseVolumeA = 39,
    BaseVolumeCombined = 40,
    DetailVolumeR = 41,
    DetailVolumeG = 42,
    DetailVolumeB = 43,
    DetailVolumeA = 44,
    DetailVolumeCombined = 45,
    TextureWrapDifference = 46,
    WeatherThicknessPotential = 47,
    LocalThickness = 48,
    LocalHeightFraction = 49,
    EffectiveShapeCoverage = 50,
    BaseSupportBeforeDensity = 51,
    ViewOpticalDepth = 52,
    AccumulatedSkyAmbient = 53,
    AccumulatedGroundBounce = 54,
    AccumulatedMultipleScattering = 55,
    DetailLodFactor = 56,
    SilverLiningContribution = 57,
    ShapedSunVisibility = 58,
    AmbientVisibility = 59,
    ExecutedViewSamples = 60,
    SkippedDistance = 61,
    EarlyExitSavings = 62,
    SupportPrecheckSkip = 63,
    LowResolutionGrid = 64,
    UpsampleSceneRejection = 65,
    UpsampleCloudDepthWeight = 66,
    UpsampleTransmittanceWeight = 67,
};

enum class Stage5WeatherPreset : std::int32_t
{
    UniformLegacy,
    PeriodicPerlin,
    ChannelDebug,
};

// HLSL CloudCB와 16바이트 묶음 순서가 정확히 일치해야 한다.
struct alignas(16) CloudParameters
{
    DirectX::XMFLOAT3 cloudBoundsMin = { -8.0f, -1.0f, -8.0f };
    float densityMultiplier = 1.0f;

    DirectX::XMFLOAT3 cloudBoundsMax = { 8.0f, 2.0f, 8.0f };
    float stepSize = 0.1f;

    std::uint32_t maxViewSteps = 128;
    float extinctionCoefficient = 1.0f;
    float transmittanceThreshold = 0.01f;
    std::int32_t debugMode = static_cast<std::int32_t>(CloudDebugMode::Composite);

    float baseNoiseScale = 0.35f;
    float coverage = 0.55f;
    float windSpeed = 0.25f;
    float noiseOffset = 0.0f;

    DirectX::XMFLOAT3 windDirection = { 0.9701425f, 0.0f, 0.2425356f };
    float bottomFadeEnd = 0.20f;

    float topFadeStart = 0.80f;
    float minimumLocalThicknessFraction = 0.40f;
    float localHeightVariation = 0.0f;
    float cumulusTopBoost = 0.35f;

    float detailNoiseScale = 2.5f;
    float detailErosionStrength = 0.25f;
    float detailWindSpeed = 0.45f; // Legacy 전용. Physical은 windSpeed를 공유한다.
    float detailNoiseOffset = 17.3f;

    float weatherMapWorldSize = 16.0f;
    float weatherMapWindSpeed = 0.10f; // Legacy 전용. Physical은 windSpeed를 공유한다.
    DirectX::XMFLOAT2 weatherMapOffset = { 0.0f, 0.0f };
};

static_assert(sizeof(CloudParameters) == 128, "CloudParameters must match CloudCB");
