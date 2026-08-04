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
    AabbEntryDistance = 5,
    AabbExitDistance = 6,
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
    float heightProfilePadding[3] = {};

    float detailNoiseScale = 2.5f;
    float detailErosionStrength = 0.25f;
    float detailWindSpeed = 0.45f;
    float detailNoiseOffset = 17.3f;

    float weatherMapWorldSize = 16.0f;
    float weatherMapWindSpeed = 0.10f;
    DirectX::XMFLOAT2 weatherMapOffset = { 0.0f, 0.0f };
};

static_assert(sizeof(CloudParameters) == 128, "CloudParameters must match CloudCB");
