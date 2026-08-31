// ============================================================================
//  AtmosphereParameters.h - 단계 14 물리 대기와 태양 제어 CPU 설정
// ============================================================================
#pragma once

#include <DirectXMath.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

enum class AtmospherePreset : std::uint32_t
{
    EarthClear,
    EarthHazy,
    Custom,
};

enum class SunControlMode : std::uint32_t
{
    Angles,
    TimeOfDay,
};

enum class Stage14DebugView : std::uint32_t
{
    None,
    Transmittance,
    OneMinusTransmittance,
    OpticalDepth,
    MultiScattering,
    SkyView,
    SkyIrradiance,
    AerialRadiance,
    AerialTransmittance,
    AtmosphereSun,
    SurfaceDirect,
    SurfaceSky,
    AerialOnly,
    HdrPreTone,
};

enum class Stage14DebugChannel : std::uint32_t
{
    Rgb,
    Red,
    Green,
    Blue,
};

struct AtmosphereParameters
{
    AtmospherePreset preset = AtmospherePreset::EarthClear;
    SunControlMode sunControlMode = SunControlMode::Angles;

    float bottomRadiusKm = 6360.0f;
    float topRadiusKm = 6460.0f;
    float rayleighScaleHeightKm = 8.0f;
    float mieScaleHeightKm = 1.2f;

    DirectX::XMFLOAT3 rayleighScatteringPerKm = {
        0.005802f, 0.013558f, 0.033100f
    };
    float rayleighScale = 1.0f;

    float mieScatteringPerKm = 0.003996f;
    float mieExtinctionPerKm = 0.004440f;
    float mieAbsorptionScale = 1.0f;
    float mieG = 0.8f;

    DirectX::XMFLOAT3 ozoneAbsorptionPerKm = {
        0.000650f, 0.001881f, 0.000085f
    };
    float ozoneScale = 1.0f;
    float ozoneCenterKm = 25.0f;
    float ozoneHalfWidthKm = 15.0f;
    float turbidity = 1.0f;

    DirectX::XMFLOAT3 solarIrradiance = { 1.0f, 1.0f, 1.0f };
    float sunAzimuthDegrees = -60.0f;
    float sunElevationDegrees = 18.0f;
    float timeOfDayHours = 7.5f;
    float timePlaybackMinutesPerSecond = 30.0f;
    bool timePlaybackEnabled = false;
    // schema 35 호환 필드다. Stage 14 통합 UI부터 시간 재생은 항상 순환한다.
    bool timeLoopEnabled = true;

    Stage14DebugView debugView = Stage14DebugView::None;
    Stage14DebugChannel debugChannel = Stage14DebugChannel::Rgb;
    float debugExposure = 1.0f;
    float aerialSlice = 0.0f;
};

namespace stage14atmosphere
{
inline float FiniteOr(float value, float fallback)
{
    return std::isfinite(value) ? value : fallback;
}

inline AtmosphereParameters Sanitize(AtmosphereParameters value)
{
    value.bottomRadiusKm = std::clamp(
        FiniteOr(value.bottomRadiusKm, 6360.0f), 1.0f, 100000.0f);
    value.topRadiusKm = std::clamp(
        FiniteOr(value.topRadiusKm, 6460.0f),
        value.bottomRadiusKm + 1.0f, value.bottomRadiusKm + 1000.0f);
    value.rayleighScaleHeightKm = std::clamp(
        FiniteOr(value.rayleighScaleHeightKm, 8.0f), 4.0f, 16.0f);
    value.mieScaleHeightKm = std::clamp(
        FiniteOr(value.mieScaleHeightKm, 1.2f), 0.5f, 4.0f);
    value.rayleighScale = std::clamp(
        FiniteOr(value.rayleighScale, 1.0f), 0.25f, 4.0f);
    value.mieAbsorptionScale = std::clamp(
        FiniteOr(value.mieAbsorptionScale, 1.0f), 0.0f, 4.0f);
    value.mieG = std::clamp(FiniteOr(value.mieG, 0.8f), 0.0f, 0.95f);
    value.ozoneScale = std::clamp(
        FiniteOr(value.ozoneScale, 1.0f), 0.0f, 4.0f);
    value.ozoneCenterKm = std::clamp(
        FiniteOr(value.ozoneCenterKm, 25.0f), 0.0f, 100.0f);
    value.ozoneHalfWidthKm = std::clamp(
        FiniteOr(value.ozoneHalfWidthKm, 15.0f), 0.1f, 100.0f);
    value.turbidity = std::clamp(
        FiniteOr(value.turbidity, 1.0f), 0.25f, 4.0f);
    value.sunAzimuthDegrees = std::clamp(
        FiniteOr(value.sunAzimuthDegrees, -60.0f), -180.0f, 180.0f);
    value.sunElevationDegrees = std::clamp(
        FiniteOr(value.sunElevationDegrees, 18.0f), -6.0f, 90.0f);
    value.timeOfDayHours = std::clamp(
        FiniteOr(value.timeOfDayHours, 7.5f), 5.5f, 19.5f);
    value.timePlaybackMinutesPerSecond = std::clamp(
        FiniteOr(value.timePlaybackMinutesPerSecond, 30.0f), 30.0f, 120.0f);
    value.timeLoopEnabled = true;
    value.sunControlMode = value.timePlaybackEnabled
        ? SunControlMode::TimeOfDay : SunControlMode::Angles;
    value.debugExposure = std::clamp(
        FiniteOr(value.debugExposure, 1.0f), 0.001f, 128.0f);
    value.aerialSlice = std::clamp(
        FiniteOr(value.aerialSlice, 0.0f), 0.0f, 31.0f);

    auto sanitizeColor = [](DirectX::XMFLOAT3& color,
                            const DirectX::XMFLOAT3& fallback)
    {
        color.x = std::max(FiniteOr(color.x, fallback.x), 0.0f);
        color.y = std::max(FiniteOr(color.y, fallback.y), 0.0f);
        color.z = std::max(FiniteOr(color.z, fallback.z), 0.0f);
    };
    sanitizeColor(value.rayleighScatteringPerKm,
                  { 0.005802f, 0.013558f, 0.033100f });
    sanitizeColor(value.ozoneAbsorptionPerKm,
                  { 0.000650f, 0.001881f, 0.000085f });
    sanitizeColor(value.solarIrradiance, { 1.0f, 1.0f, 1.0f });
    value.mieScatteringPerKm = std::max(
        FiniteOr(value.mieScatteringPerKm, 0.003996f), 0.0f);
    value.mieExtinctionPerKm = std::max(
        FiniteOr(value.mieExtinctionPerKm, 0.004440f),
        value.mieScatteringPerKm);
    return value;
}

inline void ApplyPreset(AtmosphereParameters& value,
                        AtmospherePreset preset)
{
    value.preset = preset;
    if (preset == AtmospherePreset::Custom)
        return;

    value.bottomRadiusKm = 6360.0f;
    value.topRadiusKm = 6460.0f;
    value.rayleighScaleHeightKm = 8.0f;
    value.mieScaleHeightKm = 1.2f;
    value.rayleighScatteringPerKm = {
        0.005802f, 0.013558f, 0.033100f
    };
    value.rayleighScale = 1.0f;
    value.mieScatteringPerKm = 0.003996f;
    value.mieExtinctionPerKm = 0.004440f;
    value.mieAbsorptionScale = 1.0f;
    value.mieG = 0.8f;
    value.ozoneAbsorptionPerKm = {
        0.000650f, 0.001881f, 0.000085f
    };
    value.ozoneScale = 1.0f;
    value.ozoneCenterKm = 25.0f;
    value.ozoneHalfWidthKm = 15.0f;
    value.turbidity = preset == AtmospherePreset::EarthHazy ? 2.5f : 1.0f;
}
}
