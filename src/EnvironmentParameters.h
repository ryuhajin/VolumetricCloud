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
    // Physical Atmosphere의 LUT 입사광만 조절하며 배경 대기 LUT를 다시 만들지 않는다.
    // [직접 조절] F3/scene 하늘 fill 배율. [강제 범위]/UI [0,2], 초기 1/내장 0.85~1.10. 증가하면 구름 하늘광만 밝아진다.
    float physicalSkyFillScale = 1.0f;

    // 13-5 외곽광 보완. 중립값(0, 1, 0)은 기존 환경광을 정확히 보존한다.
    // [직접 조절] Environment 코드. [강제 범위] [0,1], 초기 0/Urban 0.55 권장. 증가하면 태양 차폐를 지면 반사광에 더 반영한다. 하늘광에는 적용하지 않는다.
    float ambientShadowCoupling = 0.0f;
    // [직접 조절] Environment 코드. [강제 범위] [0.1,8], 초기 1/Urban 0.50 권장. 증가하면 차폐된 곳의 지면 반사광이 더 어두워진다.
    float ambientShadowExponent = 1.0f;
    // [직접 조절] F3/scene. [강제 범위]/UI [0,1], 초기 0/내장 0.55~0.80. 증가하면 다중 산란을 차폐된 내부에 집중한다.
    float multipleScatteringInteriorBlend = 0.0f;
    // [직접 조절] F3/scene 지면 fill 배율. [강제 범위]/UI [0,2], 초기 1/내장 0.85~1.10. 증가하면 구름 하부 반사광이 밝아진다.
    float physicalGroundFillScale = 1.0f;
};

static_assert(sizeof(EnvironmentParameters) == 48,
              "EnvironmentParameters must match EnvironmentCB");
static_assert(offsetof(EnvironmentParameters, physicalSkyFillScale) == 28,
              "physicalSkyFillScale must match EnvironmentCB offset 28");
static_assert(offsetof(EnvironmentParameters, physicalGroundFillScale) == 44,
              "physicalGroundFillScale must match EnvironmentCB offset 44");

namespace stage8environment
{
inline EnvironmentParameters Sanitize(EnvironmentParameters value)
{
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
        value.physicalSkyFillScale = 0.0f;
        value.physicalGroundFillScale = 0.0f;
        value.multipleScatteringEnabled = 0.0f;
        value.multipleScatteringOctaves = 0;
        break;
    case Stage8EnvironmentPreset::StrongFill:
        value.physicalSkyFillScale = 1.25f;
        value.physicalGroundFillScale = 1.25f;
        value.ambientOcclusionStrength = 0.80f;
        value.multipleScatteringOctaves = 3;
        value.multipleScatteringAttenuation = 0.45f;
        break;
    case Stage8EnvironmentPreset::GroundCheck:
        value.physicalSkyFillScale = 0.0f;
        value.physicalGroundFillScale = 1.0f;
        value.ambientOcclusionStrength = 0.0f;
        value.multipleScatteringEnabled = 0.0f;
        value.multipleScatteringOctaves = 0;
        break;
    case Stage8EnvironmentPreset::PortfolioHero:
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
