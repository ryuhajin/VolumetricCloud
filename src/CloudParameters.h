// ============================================================================
//  CloudParameters.h - 단계 5 Weather Map/Cloud Type CPU/GPU 공유 설정
// ============================================================================
#pragma once

#include <DirectXMath.h>
#include <cstdint>

enum class CloudDebugMode : std::int32_t
{
    Composite = 0,
    RayDirection = 1,
    SceneDepth = 2,
    WorldPosition = 3,
    ScreenUv = 4,
    CloudLayerEntryDistance = 5,
    CloudLayerExitDistance = 6,
    ViewStepCount = 7,
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
    TypedHeightProfile = 23,
    LightTransmittance = 24,
    LightOpticalDepth = 25,
    TotalLightSamples = 26,
    DirectSingleScattering = 27,
    PhaseCosTheta = 28,
    ForwardPhaseLobe = 29,
    BackwardPhaseLobe = 30,
    DualPhaseFactor = 31,
    AccumulatedDirectLighting = 32,
    DetailLodFactor = 33,
    ExecutedViewSteps = 38,
    CoarseSkippedRatio = 39,
    EarlyExitSavings = 40,
    SupportPrecheckMask = 41,
    MarchStateTransitions = 42,
};

enum class Stage1ValidationPreset : std::int32_t
{
    DefaultVolume,
    WideVolume,
    ThinVolume,
    ThickVolume,
    FineStep,
    CoarseStep,
};

enum class Stage2NoisePreset : std::int32_t
{
    DefaultNoise,
    SparseCoverage,
    DenseCoverage,
    LargeBlobs,
    SmallBlobs,
    StoppedWind,
    FastWind,
    OffsetNoise,
    Custom,
};

enum class Stage4DetailPreset : std::int32_t
{
    DetailOff,
    DefaultDetail,
    FineDetail,
    StrongErosion,
    Custom,
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
    float cloudBottomAltitude = 1500.0f;
    float cloudLayerThickness = 3000.0f;
    float maxViewTraceDistance = 50000.0f;
    float densityMultiplier = 0.65f;

    float maxLightTraceDistance = 20000.0f;
    float noiseLabPreviewWorldSize = 32000.0f;
    float stepSize = 100.0f;
    float viewTraceFadeStartDistance = 40000.0f;

    std::uint32_t maxViewSteps = 512;
    float extinctionCoefficient = 0.00075f;
    float transmittanceThreshold = 0.01f;
    std::int32_t debugMode = static_cast<std::int32_t>(CloudDebugMode::Composite);

    float baseNoiseScale = 0.00035f;
    float coverage = 0.55f;
    float windSpeed = 12.0f;
    float noiseOffset = 0.0f;

    DirectX::XMFLOAT3 windDirection = { 0.9701425f, 0.0f, 0.2425356f };
    float bottomFadeEnd = 0.20f;

    float topFadeStart = 0.80f;
    float detailLodFadeStartDistance = 8000.0f;
    float detailLodFadeEndDistance = 20000.0f;
    float detailLodPadding = 0.0f;

    float detailNoiseScale = 0.0025f;
    float detailErosionStrength = 0.25f;
    float detailWindSpeed = 18.0f;
    float detailNoiseOffset = 17.3f;

    float weatherMapWorldSize = 32000.0f;
    float weatherMapWindSpeed = 8.0f;
    DirectX::XMFLOAT2 weatherMapOffset = { 0.0f, 0.0f };
};

static_assert(sizeof(CloudParameters) == 128, "CloudParameters must match CloudCB");
