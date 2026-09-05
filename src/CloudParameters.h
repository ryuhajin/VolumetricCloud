// ============================================================================
//  CloudParameters.h - High 물리 구름 CPU/GPU 공유 설정
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
    DirectSingleScattering = 27,
    PhaseCosTheta = 28,
    ForwardPhaseLobe = 29,
    BackwardPhaseLobe = 30,
    DualPhaseFactor = 31,
    AccumulatedDirectLighting = 32,
    CloudSegmentLength = 33,
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
    CloudDepth = 56,
    SilverLiningContribution = 57,
    ShapedSunVisibility = 58,
    AmbientVisibility = 59,
    Stage12NearOpticalDepth = 74,
    Stage12FarOpticalDepth = 75,
    Stage12CascadeSelection = 76,
    Stage12SurfaceTransmittance = 77,
    // Weather A/G가 전역 바닥에서 올린 로컬 컬럼 바닥 높이다.
    LocalBaseOffset = 81,
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
    float extinctionCoefficient = 1.0f;

    std::int32_t debugMode = static_cast<std::int32_t>(CloudDebugMode::Composite);
    float coverage = 0.55f;
    // ABI mirror only. Renderer가 세션 전역 CloudMotionParameters(기본 12m/s)로 패킹한다.
    float windSpeed = 12.0f;
    float noiseOffset = 0.0f;

    DirectX::XMFLOAT3 windDirection = { 0.9701425f, 0.0f, 0.2425356f };
    float detailErosionStrength = 0.25f;

    float detailNoiseOffset = 17.3f;
    float weatherMapWorldSize = 16.0f;
    DirectX::XMFLOAT2 weatherMapOffset = { 0.0f, 0.0f };
};

static_assert(sizeof(CloudParameters) == 80, "CloudParameters must match CloudCB");
