// ============================================================================
//  LightParameters.h - 단계 6 태양광/단일 산란 CPU/GPU 공유 설정
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

    std::uint32_t maxLightSteps = 16;
    float lightStepSize = 0.25f;
    float lightRayBias = 0.01f;
    float lightPadding = 0.0f;
};

static_assert(sizeof(LightParameters) == 48, "LightParameters must match LightCB");

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
        std::isfinite(value.lightStepSize) ? value.lightStepSize : 0.25f,
        1e-4f, 10.0f);
    value.lightRayBias = std::clamp(
        std::isfinite(value.lightRayBias) ? value.lightRayBias : 0.01f,
        0.0f, 1.0f);
    value.lightPadding = 0.0f;
    return value;
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
