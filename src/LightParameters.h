// ============================================================================
//  LightParameters.h - 단계 7 태양광·Dual-lobe Phase CPU/GPU 공유 설정
// ============================================================================
#pragma once

#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>

enum class Stage6SunPreset : std::int32_t
{
    Noon,
    LowEast,
    LowWest,
    Custom,
};

enum class Stage7PhasePreset : std::int32_t
{
    Off,
    Balanced,
    SilverLining,
    BackscatterCheck,
    Custom,
};

// HLSL LightCB(b3)와 16바이트 묶음 순서가 정확히 일치해야 한다.
// directionToSun은 빛이 진행해 오는 반대 방향이 아니라, 현재 구름 표본에서
// 태양을 향해 나가는 길이 1의 월드 방향이다.
struct alignas(16) LightParameters
{
    // [파생 값] F3 대기 태양 각도에서 생성한 표본→태양 xyz 단위벡터. Light sanitize 고도 [0,90]도; 기본 Urban 방위 -60/고도 18도.
    DirectX::XMFLOAT3 directionToSun = {
        0.4580787f, 0.8143622f, 0.3562834f
    };
    // [직접 조절] F3/Stage15 concept 태양 배율. [강제 범위] 유한 [0,무제한). [권장 범위] UI [0,8], 내장 0.75~1; 실제 빛은 b9.w로 전달.
    float sunIntensity = 1.0f;

    // [직접 조절] Light/scene 선형 RGB tint. [강제 범위] 성분별 유한 [0,무제한). [권장 범위] 초기 (1,0.95,0.85) 주변; b9.xyz로 재패킹된다.
    DirectX::XMFLOAT3 sunColor = { 1.0f, 0.95f, 0.85f };
    // [직접 조절] Light/scene, 소멸 중 산란 비율 sigma_s/sigma_t. [강제 범위] [0,1], 기본/권장 1; 낮추면 흡수가 늘어 어두워진다.
    float singleScatteringAlbedo = 1.0f;

    // [직접 조절] CPU phase preset. [강제 범위] 0 또는 1(입력 >=0.5면 1). 초기 0, Urban 1; 0은 방향 배율만 등방성 1로 만든다.
    float phaseEnabled = 0.0f;
    // [직접 조절] F3/phase preset. [강제 범위] [0,0.95], 초기 0.65/Urban 0.75. 증가하면 태양 방향 봉우리가 좁고 강해진다.
    float forwardScatteringG = 0.65f;
    // [직접 조절] phase preset 코드. [강제 범위] [-0.95,0], 초기 -0.25/Urban -0.15. 더 음수이면 태양 반대 방향에 집중한다.
    float backwardScatteringG = -0.25f;
    // [직접 조절] phase preset 코드. [강제 범위] [0,1], 초기 0.80/Urban 0.90. 0=후방, 1=전방 lobe; 기존 프리셋 주변에서 조절.
    float phaseBlend = 0.80f;

    // [직접 조절] F3/phase preset. [강제 범위] [0,1], 초기 0.25/Urban 0.20. 증가하면 등방성 1에서 방향성 결과로 이동한다.
    float phaseIntensity = 0.25f;
    // 13-5 외곽광 보완. 중립값(0, 1, 1)은 승인된 단계 8/13-5 직접광을 보존한다.
    // [직접 조절] F3/scene. [강제 범위] [0,1], 초기 0/내장 0.35~0.85. 증가하면 phase 효과를 태양 노출 외곽에 한정한다.
    float edgeInfluence = 0.0f;
    // [직접 조절] scene 코드. [강제 범위] [0.25,8], 초기 1/내장 1.4~2.0 권장. T의 지수; 증가하면 외곽광 영역이 좁아진다.
    float edgeOpticalDepthScale = 1.0f;
    // [직접 조절] scene 코드. [강제 범위] [0.5,4], 초기 1/내장 1.15~1.35 권장. 증가하면 T^지수가 작아져 직접광 그림자가 깊어진다.
    float shadowExponent = 1.0f;
    // 06 사용자 승인: 강도2, 깊이1. 강도1은 이전 기준을 재현하는 실제 배율이다.
    float rimIntensity = 2.0f;
    float rimDepthScale = 1.0f;
    float rimPadding[2] = {};
};

static_assert(sizeof(LightParameters) == 80, "LightParameters must match LightCB");
static_assert(offsetof(LightParameters, rimIntensity) == 64);
static_assert(offsetof(LightParameters, rimDepthScale) == 68);

namespace stage6light
{
inline constexpr float kDefaultRimPhaseCap = 2.5f;
inline DirectX::XMFLOAT3 DirectionFromAngles(float azimuthDegrees,
                                             float elevationDegrees)
{
    constexpr float kDegreesToRadians = 0.01745329251994329577f;
    const float azimuth = azimuthDegrees * kDegreesToRadians;
    const float elevation = std::clamp(elevationDegrees, 0.0f, 90.0f) *
                            kDegreesToRadians;
    const float horizontal = std::cos(elevation);
    return { horizontal * std::cos(azimuth), std::sin(elevation),
             horizontal * std::sin(azimuth) };
}

inline void AnglesFromDirection(const DirectX::XMFLOAT3& direction,
                                float& azimuthDegrees, float& elevationDegrees)
{
    constexpr float kRadiansToDegrees = 57.295779513082320876f;
    const float length = std::sqrt(direction.x * direction.x +
                                   direction.y * direction.y +
                                   direction.z * direction.z);
    if (!std::isfinite(length) || length <= 1e-6f)
    {
        azimuthDegrees = 45.0f;
        elevationDegrees = 70.0f;
        return;
    }
    const float x = direction.x / length;
    const float y = std::clamp(direction.y / length, -1.0f, 1.0f);
    const float z = direction.z / length;
    azimuthDegrees = std::atan2(z, x) * kRadiansToDegrees;
    elevationDegrees = std::asin(y) * kRadiansToDegrees;
}

inline LightParameters Sanitize(LightParameters value)
{
    float azimuth = 45.0f;
    float elevation = 70.0f;
    AnglesFromDirection(value.directionToSun, azimuth, elevation);
    value.directionToSun = DirectionFromAngles(azimuth, elevation);
    value.sunIntensity = std::max(
        std::isfinite(value.sunIntensity) ? value.sunIntensity : 0.0f, 0.0f);
    value.sunColor.x = std::max(std::isfinite(value.sunColor.x) ? value.sunColor.x : 0.0f, 0.0f);
    value.sunColor.y = std::max(std::isfinite(value.sunColor.y) ? value.sunColor.y : 0.0f, 0.0f);
    value.sunColor.z = std::max(std::isfinite(value.sunColor.z) ? value.sunColor.z : 0.0f, 0.0f);
    value.singleScatteringAlbedo = std::clamp(
        std::isfinite(value.singleScatteringAlbedo)
            ? value.singleScatteringAlbedo
            : 1.0f,
        0.0f, 1.0f);
    value.phaseEnabled = std::isfinite(value.phaseEnabled) &&
                         value.phaseEnabled >= 0.5f ? 1.0f : 0.0f;
    value.forwardScatteringG = std::clamp(
        std::isfinite(value.forwardScatteringG) ? value.forwardScatteringG : 0.65f,
        0.0f, 0.95f);
    value.backwardScatteringG = std::clamp(
        std::isfinite(value.backwardScatteringG) ? value.backwardScatteringG : -0.25f,
        -0.95f, 0.0f);
    value.phaseBlend = std::clamp(
        std::isfinite(value.phaseBlend) ? value.phaseBlend : 0.80f,
        0.0f, 1.0f);
    value.phaseIntensity = std::clamp(
        std::isfinite(value.phaseIntensity) ? value.phaseIntensity : 0.25f,
        0.0f, 1.0f);
    value.edgeInfluence = std::clamp(
        std::isfinite(value.edgeInfluence) ? value.edgeInfluence : 0.0f,
        0.0f, 1.0f);
    value.edgeOpticalDepthScale = std::clamp(
        std::isfinite(value.edgeOpticalDepthScale)
            ? value.edgeOpticalDepthScale : 1.0f,
        0.25f, 8.0f);
    value.shadowExponent = std::clamp(
        std::isfinite(value.shadowExponent) ? value.shadowExponent : 1.0f,
        0.5f, 4.0f);
    value.rimIntensity = std::clamp(std::isfinite(value.rimIntensity) ? value.rimIntensity : 2.0f, 0.0f, 4.0f);
    value.rimDepthScale = std::clamp(std::isfinite(value.rimDepthScale) ? value.rimDepthScale : 1.0f, 0.5f, 2.0f);
    value.rimPadding[0] = value.rimPadding[1] = 0;
    return value;
}

inline void ApplyPhasePreset(LightParameters& value, Stage7PhasePreset preset)
{
    switch (preset)
    {
    case Stage7PhasePreset::Balanced:
        value.phaseEnabled = 1.0f;
        value.forwardScatteringG = 0.65f;
        value.backwardScatteringG = -0.25f;
        value.phaseBlend = 0.80f;
        value.phaseIntensity = 0.25f;
        value.edgeInfluence = 0.0f;
        value.edgeOpticalDepthScale = 1.0f;
        value.shadowExponent = 1.0f;
        break;
    case Stage7PhasePreset::SilverLining:
        value.phaseEnabled = 1.0f;
        value.forwardScatteringG = 0.75f;
        value.backwardScatteringG = -0.15f;
        value.phaseBlend = 0.90f;
        value.phaseIntensity = 0.20f;
        value.edgeInfluence = 0.85f;
        value.edgeOpticalDepthScale = 2.0f;
        value.shadowExponent = 1.35f;
        break;
    case Stage7PhasePreset::BackscatterCheck:
        value.phaseEnabled = 1.0f;
        value.forwardScatteringG = 0.40f;
        value.backwardScatteringG = -0.55f;
        value.phaseBlend = 0.30f;
        value.phaseIntensity = 0.25f;
        value.edgeInfluence = 0.0f;
        value.edgeOpticalDepthScale = 1.0f;
        value.shadowExponent = 1.0f;
        break;
    case Stage7PhasePreset::Custom:
        value = Sanitize(value);
        return;
    case Stage7PhasePreset::Off:
    default:
        value.phaseEnabled = 0.0f;
        value.forwardScatteringG = 0.65f;
        value.backwardScatteringG = -0.25f;
        value.phaseBlend = 0.80f;
        value.phaseIntensity = 0.25f;
        value.edgeInfluence = 0.0f;
        value.edgeOpticalDepthScale = 1.0f;
        value.shadowExponent = 1.0f;
        break;
    }
    value = Sanitize(value);
}

inline LightParameters Preset(Stage6SunPreset preset)
{
    LightParameters value;
    switch (preset)
    {
    case Stage6SunPreset::LowEast:
        value.directionToSun = DirectionFromAngles(-60.0f, 18.0f);
        break;
    case Stage6SunPreset::LowWest:
        value.directionToSun = DirectionFromAngles(120.0f, 18.0f);
        break;
    case Stage6SunPreset::Noon:
    case Stage6SunPreset::Custom:
    default:
        value.directionToSun = DirectionFromAngles(45.0f, 70.0f);
        break;
    }
    return value;
}
}
