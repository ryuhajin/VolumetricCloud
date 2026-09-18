// 과거 단계8 분석적 환경광 회귀 전용. 런타임에 포함하지 않는다.
// ============================================================================
//  LegacyEnvironmentParameters.h - 단계 8 환경광·저비용 다중 산란 CPU/GPU 공유 설정
// ============================================================================
#pragma once

#include <DirectXMath.h>
#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdint>

enum class LegacyEnvironmentPreset : std::int32_t
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
struct alignas(16) LegacyEnvironmentParameters
{
    // [호환 유지] xyz 선형 RGB, 초기 (0.35,0.50,0.75), sanitize [0,16]. Physical 경로는 LUT를 읽으므로 현재 화면 튜닝에 사용하지 않는다.
    DirectX::XMFLOAT3 skyColor = { 0.35f, 0.50f, 0.75f };
    // [호환 유지] 무차원 초기 0.12, sanitize [0,4]. Physical 경로 효과 없음; physicalSkyFillScale을 조절한다.
    float skyStrength = 0.12f;

    // [호환 유지] xyz 선형 RGB, 초기 (0.18,0.12,0.08), sanitize [0,16]. 현재 지면 색은 b9 albedo가 소유한다.
    DirectX::XMFLOAT3 groundColor = { 0.18f, 0.12f, 0.08f };
    // [호환 유지] 무차원 초기 0.05, sanitize [0,4]. 현재 반사광은 physicalGroundFillScale을 조절한다.
    float groundStrength = 0.05f;

    // [직접 조절] Environment preset 코드. [강제 범위] [0,16], 초기 1.5/Urban 1.8 권장. 증가하면 고밀도 내부의 fill이 어두워진다.
    float ambientOcclusionStrength = 1.50f;
    // [직접 조절] Environment 코드. [강제 범위] [0,1], 기본/권장 0.65. 증가하면 낮은 local height의 하늘광이 줄어든다.
    float ambientHeightInfluence = 0.65f;
    // [직접 조절] Environment preset. [강제 범위] 0/1, 초기/권장 1. 0이면 다중 산란 근사만 끈다.
    float multipleScatteringEnabled = 1.0f;
    // [직접 조절] Environment preset. [강제 범위] 정수 [0,4], 기본/권장 2. 증가하면 추가 산란 항과 계산량이 늘어난다.
    std::uint32_t multipleScatteringOctaves = 2;

    // [직접 조절] Environment 코드. [강제 범위] [0,1], 초기 0.20/Urban 0.15 권장. 증가하면 octave별 에너지가 더 오래 남는다.
    float multipleScatteringAttenuation = 0.20f;
    // [직접 조절] Environment 코드. [강제 범위] [0,1], 기본/권장 0.50. 감소하면 근사 tau가 작아져 빛이 내부까지 들어간다.
    float multipleScatteringExtinctionFactor = 0.50f;
    // [직접 조절] Environment 코드. [강제 범위] [0,1], 초기 0.25/Urban 0.15 권장. 감소하면 고차 산란 방향성이 등방성 1에 가까워진다.
    float multipleScatteringPhaseFactor = 0.25f;
    // Physical Atmosphere의 LUT 입사광만 조절한다. 기존 분석적
    // sky/ground color·strength와 분리해 배경 대기 LUT를 다시 만들지 않는다.
    // [직접 조절] F3/scene 하늘 fill 배율. [강제 범위]/UI [0,2], 초기 1/내장 0.85~1.10. 증가하면 구름 하늘광만 밝아진다.
    float physicalSkyFillScale = 1.0f;

    // 13-5 외곽광 보완. 중립값(0, 1, 0)은 기존 환경광을 정확히 보존한다.
    // [직접 조절] Environment 코드. [강제 범위] [0,1], 초기 0/Urban 0.55 권장. 증가하면 태양 차폐를 환경광에도 더 반영한다.
    float ambientShadowCoupling = 0.0f;
    // [직접 조절] Environment 코드. [강제 범위] [0.1,8], 초기 1/Urban 0.50 권장. 증가하면 차폐된 곳의 ambient가 더 어두워진다.
    float ambientShadowExponent = 1.0f;
    // [직접 조절] F3/scene. [강제 범위]/UI [0,1], 초기 0/내장 0.55~0.80. 증가하면 다중 산란을 차폐된 내부에 집중한다.
    float multipleScatteringInteriorBlend = 0.0f;
    // [직접 조절] F3/scene 지면 fill 배율. [강제 범위]/UI [0,2], 초기 1/내장 0.85~1.10. 증가하면 구름 하부 반사광이 밝아진다.
    float physicalGroundFillScale = 1.0f;
};

static_assert(sizeof(LegacyEnvironmentParameters) == 80,
              "LegacyEnvironmentParameters must match EnvironmentCB");
static_assert(offsetof(LegacyEnvironmentParameters, physicalSkyFillScale) == 60,
              "physicalSkyFillScale must match EnvironmentCB offset 60");
static_assert(offsetof(LegacyEnvironmentParameters, physicalGroundFillScale) == 76,
              "physicalGroundFillScale must match EnvironmentCB offset 76");

namespace legacyenvironment
{
inline float SafeNonNegative(float value, float fallback)
{
    return std::clamp(std::isfinite(value) ? value : fallback, 0.0f, 16.0f);
}

inline LegacyEnvironmentParameters Sanitize(LegacyEnvironmentParameters value)
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

inline void ApplyPreset(LegacyEnvironmentParameters& value,
                        LegacyEnvironmentPreset preset)
{
    if (preset == LegacyEnvironmentPreset::Custom)
    {
        value = Sanitize(value);
        return;
    }
    value = LegacyEnvironmentParameters{};
    switch (preset)
    {
    case LegacyEnvironmentPreset::Off:
        value.skyStrength = 0.0f;
        value.groundStrength = 0.0f;
        value.physicalSkyFillScale = 0.0f;
        value.physicalGroundFillScale = 0.0f;
        value.multipleScatteringEnabled = 0.0f;
        value.multipleScatteringOctaves = 0;
        break;
    case LegacyEnvironmentPreset::StrongFill:
        value.skyStrength = 0.40f;
        value.groundStrength = 0.15f;
        value.physicalSkyFillScale = 1.25f;
        value.physicalGroundFillScale = 1.25f;
        value.ambientOcclusionStrength = 0.80f;
        value.multipleScatteringOctaves = 3;
        value.multipleScatteringAttenuation = 0.45f;
        break;
    case LegacyEnvironmentPreset::GroundCheck:
        value.skyStrength = 0.0f;
        value.groundStrength = 0.35f;
        value.physicalSkyFillScale = 0.0f;
        value.physicalGroundFillScale = 1.0f;
        value.ambientOcclusionStrength = 0.0f;
        value.multipleScatteringEnabled = 0.0f;
        value.multipleScatteringOctaves = 0;
        break;
    case LegacyEnvironmentPreset::PortfolioHero:
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
    case LegacyEnvironmentPreset::Balanced:
    default:
        break;
    }
    value = Sanitize(value);
}
}
