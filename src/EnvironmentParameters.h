// ============================================================================
//  EnvironmentParameters.h - 단계 8 환경광·저비용 다중 산란 CPU/GPU 공유 설정
// ============================================================================
#pragma once

#include <DirectXMath.h>
#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdint>

enum class Stage8EnvironmentPreset : std::int32_t
{
    Off,
    Balanced,
    StrongFill,
    GroundCheck,
    PortfolioHero,
    Custom,
};

// HLSL EnvironmentCB(b4)와 16바이트 묶음 순서가 정확히 일치해야 한다.
// 색은 linear RGB, strength와 근사 계수는 단위 없는 값이다.
struct alignas(16) EnvironmentParameters
{
    DirectX::XMFLOAT3 skyColor = { 0.35f, 0.50f, 0.75f };
    float skyStrength = 0.12f;

    DirectX::XMFLOAT3 groundColor = { 0.18f, 0.12f, 0.08f };
    float groundStrength = 0.05f;

    float ambientOcclusionStrength = 1.50f;
    float ambientHeightInfluence = 0.65f;
    float multipleScatteringEnabled = 1.0f;
    std::uint32_t multipleScatteringOctaves = 2;

    float multipleScatteringAttenuation = 0.20f;
    float multipleScatteringExtinctionFactor = 0.50f;
    float multipleScatteringPhaseFactor = 0.25f;
    // Physical Atmosphere의 LUT 입사광만 조절한다. 기존 분석적
    // sky/ground color·strength와 분리해 배경 대기 LUT를 다시 만들지 않는다.
    float physicalSkyFillScale = 1.0f;

    // 13-5 외곽광 보완. 중립값(0, 1, 0)은 기존 환경광을 정확히 보존한다.
    float ambientShadowCoupling = 0.0f;
    float ambientShadowExponent = 1.0f;
    float multipleScatteringInteriorBlend = 0.0f;
    float physicalGroundFillScale = 1.0f;
};

static_assert(sizeof(EnvironmentParameters) == 80,
              "EnvironmentParameters must match EnvironmentCB");
static_assert(offsetof(EnvironmentParameters, physicalSkyFillScale) == 60,
              "physicalSkyFillScale must match EnvironmentCB offset 60");
static_assert(offsetof(EnvironmentParameters, physicalGroundFillScale) == 76,
              "physicalGroundFillScale must match EnvironmentCB offset 76");

namespace stage8environment
{
inline float SafeNonNegative(float value, float fallback)
{
    return std::clamp(std::isfinite(value) ? value : fallback, 0.0f, 16.0f);
}

inline EnvironmentParameters Sanitize(EnvironmentParameters value)
{
    value.skyColor.x = SafeNonNegative(value.skyColor.x, 0.35f);
    value.skyColor.y = SafeNonNegative(value.skyColor.y, 0.50f);
    value.skyColor.z = SafeNonNegative(value.skyColor.z, 0.75f);
    value.skyStrength = std::clamp(
        std::isfinite(value.skyStrength) ? value.skyStrength : 0.12f,
        0.0f, 4.0f);
    value.groundColor.x = SafeNonNegative(value.groundColor.x, 0.18f);
    value.groundColor.y = SafeNonNegative(value.groundColor.y, 0.12f);
    value.groundColor.z = SafeNonNegative(value.groundColor.z, 0.08f);
    value.groundStrength = std::clamp(
        std::isfinite(value.groundStrength) ? value.groundStrength : 0.05f,
        0.0f, 4.0f);
    value.ambientOcclusionStrength = std::clamp(
        std::isfinite(value.ambientOcclusionStrength)
            ? value.ambientOcclusionStrength : 1.50f,
        0.0f, 16.0f);
    value.ambientHeightInfluence = std::clamp(
        std::isfinite(value.ambientHeightInfluence)
            ? value.ambientHeightInfluence : 0.65f,
        0.0f, 1.0f);
    value.multipleScatteringEnabled =
        std::isfinite(value.multipleScatteringEnabled) &&
        value.multipleScatteringEnabled >= 0.5f ? 1.0f : 0.0f;
    value.multipleScatteringOctaves =
        std::min(value.multipleScatteringOctaves, 4u);
    value.multipleScatteringAttenuation = std::clamp(
        std::isfinite(value.multipleScatteringAttenuation)
            ? value.multipleScatteringAttenuation : 0.20f,
        0.0f, 1.0f);
    value.multipleScatteringExtinctionFactor = std::clamp(
        std::isfinite(value.multipleScatteringExtinctionFactor)
            ? value.multipleScatteringExtinctionFactor : 0.50f,
        0.0f, 1.0f);
    value.multipleScatteringPhaseFactor = std::clamp(
        std::isfinite(value.multipleScatteringPhaseFactor)
            ? value.multipleScatteringPhaseFactor : 0.25f,
        0.0f, 1.0f);
    value.physicalSkyFillScale = std::clamp(
        std::isfinite(value.physicalSkyFillScale)
            ? value.physicalSkyFillScale : 1.0f,
        0.0f, 2.0f);
    value.ambientShadowCoupling = std::clamp(
        std::isfinite(value.ambientShadowCoupling)
            ? value.ambientShadowCoupling : 0.0f,
        0.0f, 1.0f);
    value.ambientShadowExponent = std::clamp(
        std::isfinite(value.ambientShadowExponent)
            ? value.ambientShadowExponent : 1.0f,
        0.1f, 8.0f);
    value.multipleScatteringInteriorBlend = std::clamp(
        std::isfinite(value.multipleScatteringInteriorBlend)
            ? value.multipleScatteringInteriorBlend : 0.0f,
        0.0f, 1.0f);
    value.physicalGroundFillScale = std::clamp(
        std::isfinite(value.physicalGroundFillScale)
            ? value.physicalGroundFillScale : 1.0f,
        0.0f, 2.0f);
    return value;
}

inline void ApplyPreset(EnvironmentParameters& value,
                        Stage8EnvironmentPreset preset)
{
    if (preset == Stage8EnvironmentPreset::Custom)
    {
        value = Sanitize(value);
        return;
    }
    value = EnvironmentParameters{};
    switch (preset)
    {
    case Stage8EnvironmentPreset::Off:
        value.skyStrength = 0.0f;
        value.groundStrength = 0.0f;
        value.physicalSkyFillScale = 0.0f;
        value.physicalGroundFillScale = 0.0f;
        value.multipleScatteringEnabled = 0.0f;
        value.multipleScatteringOctaves = 0;
        break;
    case Stage8EnvironmentPreset::StrongFill:
        value.skyStrength = 0.40f;
        value.groundStrength = 0.15f;
        value.physicalSkyFillScale = 1.25f;
        value.physicalGroundFillScale = 1.25f;
        value.ambientOcclusionStrength = 0.80f;
        value.multipleScatteringOctaves = 3;
        value.multipleScatteringAttenuation = 0.45f;
        break;
    case Stage8EnvironmentPreset::GroundCheck:
        value.skyStrength = 0.0f;
        value.groundStrength = 0.35f;
        value.physicalSkyFillScale = 0.0f;
        value.physicalGroundFillScale = 1.0f;
        value.ambientOcclusionStrength = 0.0f;
        value.multipleScatteringEnabled = 0.0f;
        value.multipleScatteringOctaves = 0;
        break;
    case Stage8EnvironmentPreset::PortfolioHero:
        value.skyColor = { 0.24f, 0.38f, 0.70f };
        value.skyStrength = 0.10f;
        value.groundColor = { 0.18f, 0.10f, 0.07f };
        value.groundStrength = 0.025f;
        value.ambientOcclusionStrength = 1.80f;
        value.ambientHeightInfluence = 0.65f;
        value.multipleScatteringOctaves = 2;
        value.multipleScatteringAttenuation = 0.15f;
        value.multipleScatteringExtinctionFactor = 0.50f;
        value.multipleScatteringPhaseFactor = 0.15f;
        value.ambientShadowCoupling = 0.55f;
        value.ambientShadowExponent = 0.50f;
        value.multipleScatteringInteriorBlend = 0.75f;
        value.physicalSkyFillScale = 1.0f;
        value.physicalGroundFillScale = 1.0f;
        break;
    case Stage8EnvironmentPreset::Balanced:
    default:
        break;
    }
    value = Sanitize(value);
}
}
