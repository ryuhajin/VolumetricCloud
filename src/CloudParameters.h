// ============================================================================
//  CloudParameters.h - 단계 3 높이 프로파일에서 CPU/GPU가 공유하는 설정
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
};

static_assert(sizeof(CloudParameters) == 96, "CloudParameters must match CloudCB");
