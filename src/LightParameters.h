// ============================================================================
//  LightParameters.h - 단계 7 태양광·Dual-lobe Phase CPU/GPU 공유 설정
// ============================================================================
#pragma once

#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

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
    DirectX::XMFLOAT3 directionToSun = {
        0.4580787f, 0.8143622f, 0.3562834f
    };
    float sunIntensity = 1.0f;

    DirectX::XMFLOAT3 sunColor = { 1.0f, 0.95f, 0.85f };
    float scatteringCoefficient = 1.0f;

    std::uint32_t maxLightSteps = 32;
    float lightStepSize = 250.0f;
    float lightRayBias = 1.0f;
    float phaseEnabled = 0.0f;

    float forwardScatteringG = 0.65f;
    float backwardScatteringG = -0.25f;
    float phaseBlend = 0.80f;
    float phaseIntensity = 0.25f;
};

static_assert(sizeof(LightParameters) == 64, "LightParameters must match LightCB");

namespace stage6light
{
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
    value.scatteringCoefficient = std::max(
        std::isfinite(value.scatteringCoefficient) ? value.scatteringCoefficient : 0.0f, 0.0f);
    value.maxLightSteps = std::clamp(value.maxLightSteps, 1u, 64u);
    value.lightStepSize = std::clamp(
        std::isfinite(value.lightStepSize) ? value.lightStepSize : 250.0f,
        1e-4f, 5000.0f);
    value.lightRayBias = std::clamp(
        std::isfinite(value.lightRayBias) ? value.lightRayBias : 1.0f,
        0.0f, 100.0f);
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
        break;
    case Stage7PhasePreset::SilverLining:
        value.phaseEnabled = 1.0f;
        value.forwardScatteringG = 0.80f;
        value.backwardScatteringG = -0.15f;
        value.phaseBlend = 0.90f;
        value.phaseIntensity = 0.20f;
        break;
    case Stage7PhasePreset::BackscatterCheck:
        value.phaseEnabled = 1.0f;
        value.forwardScatteringG = 0.40f;
        value.backwardScatteringG = -0.55f;
        value.phaseBlend = 0.30f;
        value.phaseIntensity = 0.25f;
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
