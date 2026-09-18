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
    Custom,
};

struct GroundLightingParameters
{
    // [직접 조절] Concrete/Grass/Snow/Custom. 초기 Concrete; preset 적용 시 albedo 변경, Custom은 현재 albedo 보존.
    GroundMaterialPreset preset = GroundMaterialPreset::Concrete;
    // [직접 조절] F3/지면 preset, xyz=선형 RGB 반사율 [0,1]. 기본 Concrete (0.18,0.18,0.18), Snow ~0.8 권장. 증가하면 지면과 반사광이 밝아진다.
    DirectX::XMFLOAT3 albedo = { 0.18f, 0.18f, 0.18f };
    // [직접 조절] F3/scene 지면 반사광 배율 [0,2], 기본 1/내장 1~1.5 권장. 구름 하부 입사광을 키운다.
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
