// ============================================================================
//  GroundLightingParameters.h - 단계 14 지면 알베도와 전역 반사광 CPU 설정
// ============================================================================
#pragma once

#include <DirectXMath.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

enum class GroundMaterialPreset : std::uint32_t
{
    Concrete,
    Grass,
    Snow,
    Desert,
    Custom,
};

struct GroundLightingParameters
{
    GroundMaterialPreset preset = GroundMaterialPreset::Concrete;
    DirectX::XMFLOAT3 albedo = { 0.18f, 0.18f, 0.18f };
    float bounceMultiplier = 1.0f;
};

namespace stage14ground
{
inline DirectX::XMFLOAT3 PresetAlbedo(GroundMaterialPreset preset)
{
    switch (preset)
    {
    case GroundMaterialPreset::Grass:
        return { 0.05f, 0.20f, 0.04f };
    case GroundMaterialPreset::Snow:
        return { 0.80f, 0.85f, 0.90f };
    case GroundMaterialPreset::Desert:
        return { 0.45f, 0.30f, 0.16f };
    case GroundMaterialPreset::Concrete:
    case GroundMaterialPreset::Custom:
    default:
        return { 0.18f, 0.18f, 0.18f };
    }
}

inline void ApplyPreset(GroundLightingParameters& value,
                        GroundMaterialPreset preset)
{
    value.preset = preset;
    if (preset != GroundMaterialPreset::Custom)
        value.albedo = PresetAlbedo(preset);
}

inline GroundLightingParameters Sanitize(GroundLightingParameters value)
{
    auto channel = [](float v, float fallback)
    {
        return std::clamp(std::isfinite(v) ? v : fallback, 0.0f, 1.0f);
    };
    const DirectX::XMFLOAT3 fallback = PresetAlbedo(value.preset);
    value.albedo.x = channel(value.albedo.x, fallback.x);
    value.albedo.y = channel(value.albedo.y, fallback.y);
    value.albedo.z = channel(value.albedo.z, fallback.z);
    value.bounceMultiplier = std::clamp(
        std::isfinite(value.bounceMultiplier) ? value.bounceMultiplier : 1.0f,
        0.0f, 2.0f);
    return value;
}
}
