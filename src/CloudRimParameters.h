// ============================================================================
//  CloudRimParameters.h - Stage 15B full-resolution NTE rim CPU/GPU contract
// ============================================================================
#pragma once

#include <DirectXMath.h>

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdint>

enum class CloudRimPreset : std::uint32_t
{
    Off = 0,
    UrbanNte = 1,
    MeadowNte = 2,
    Custom = 3,
};

// HLSL CloudRimCB의 세 16바이트 레지스터와 정확히 일치한다. D3D11 SM5는
// b0~b13만 지원하므로 독립 Composite pass에서 비어 있는 b10을 재사용한다.
// Rim은 resolve된 full-resolution cloud pair에만 적용하며 history에는 쓰지 않는다.
struct alignas(16) CloudRimParameters
{
    std::uint32_t enabled = 0u;
    float widthPixels = 1.5f;
    float intensity = 0.45f;
    float opacityThreshold = 0.10f;

    float opacitySoftness = 0.08f;
    // 외곽 법선과 화면에 투영된 태양 방향 사이 cosine의 최소값이다.
    float sunAlignment = 0.0f;
    float sunPower = 2.0f;
    // 중심/이웃에 모두 구름이 있을 때 적용하는 상대 깊이 임계값이다.
    // 0이면 깊이 거부를 끈다.
    float cloudDepthRejectionThreshold = 0.08f;

    DirectX::XMFLOAT3 tint = { 1.0f, 0.95f, 0.85f };
    float radianceClamp = 8.0f;
};

static_assert(sizeof(CloudRimParameters) == 48,
              "CloudRimParameters must match CloudRimCB");
static_assert(offsetof(CloudRimParameters, enabled) == 0,
              "CloudRimParameters enabled ABI changed");
static_assert(offsetof(CloudRimParameters, widthPixels) == 4,
              "CloudRimParameters width ABI changed");
static_assert(offsetof(CloudRimParameters, intensity) == 8,
              "CloudRimParameters intensity ABI changed");
static_assert(offsetof(CloudRimParameters, opacityThreshold) == 12,
              "CloudRimParameters opacity threshold ABI changed");
static_assert(offsetof(CloudRimParameters, opacitySoftness) == 16,
              "CloudRimParameters opacity softness ABI changed");
static_assert(offsetof(CloudRimParameters, sunAlignment) == 20,
              "CloudRimParameters sun alignment ABI changed");
static_assert(offsetof(CloudRimParameters, sunPower) == 24,
              "CloudRimParameters sun power ABI changed");
static_assert(offsetof(CloudRimParameters,
                       cloudDepthRejectionThreshold) == 28,
              "CloudRimParameters depth rejection ABI changed");
static_assert(offsetof(CloudRimParameters, tint) == 32,
              "CloudRimParameters tint ABI changed");
static_assert(offsetof(CloudRimParameters, radianceClamp) == 44,
              "CloudRimParameters radiance clamp ABI changed");

namespace cloudrim
{
// D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT == 14 (b0~b13).
// CloudComposite는 Stage10UpsamplingCB를 읽지 않으므로 그 pass의 b10을 빌린다.
inline constexpr std::uint32_t kConstantBufferSlot = 10u;

inline float FiniteOr(float value, float fallback)
{
    return std::isfinite(value) ? value : fallback;
}

inline CloudRimParameters Sanitize(CloudRimParameters value)
{
    value.enabled = value.enabled != 0u ? 1u : 0u;
    value.widthPixels = std::clamp(
        FiniteOr(value.widthPixels, 1.5f), 0.0f, 8.0f);
    value.intensity = std::clamp(
        FiniteOr(value.intensity, 0.45f), 0.0f, 4.0f);
    value.opacityThreshold = std::clamp(
        FiniteOr(value.opacityThreshold, 0.10f), 0.0f, 1.0f);
    value.opacitySoftness = std::clamp(
        FiniteOr(value.opacitySoftness, 0.08f), 0.001f, 0.50f);
    value.sunAlignment = std::clamp(
        FiniteOr(value.sunAlignment, 0.0f), 0.0f, 0.99f);
    value.sunPower = std::clamp(
        FiniteOr(value.sunPower, 2.0f), 0.25f, 16.0f);
    value.cloudDepthRejectionThreshold = std::clamp(
        FiniteOr(value.cloudDepthRejectionThreshold, 0.08f), 0.0f, 1.0f);
    value.tint.x = std::clamp(FiniteOr(value.tint.x, 1.0f), 0.0f, 8.0f);
    value.tint.y = std::clamp(FiniteOr(value.tint.y, 0.95f), 0.0f, 8.0f);
    value.tint.z = std::clamp(FiniteOr(value.tint.z, 0.85f), 0.0f, 8.0f);
    value.radianceClamp = std::clamp(
        FiniteOr(value.radianceClamp, 8.0f), 0.0f, 64.0f);
    return value;
}

inline void ApplyPreset(CloudRimParameters& value, CloudRimPreset preset)
{
    if (preset == CloudRimPreset::Custom)
    {
        value = Sanitize(value);
        return;
    }

    value = CloudRimParameters{};
    switch (preset)
    {
    case CloudRimPreset::UrbanNte:
        value.enabled = 1u;
        value.widthPixels = 1.5f;
        value.intensity = 0.45f;
        break;
    case CloudRimPreset::MeadowNte:
        value.enabled = 1u;
        value.widthPixels = 1.25f;
        value.intensity = 0.30f;
        break;
    case CloudRimPreset::Off:
    default:
        value.enabled = 0u;
        break;
    }
    value = Sanitize(value);
}
}
