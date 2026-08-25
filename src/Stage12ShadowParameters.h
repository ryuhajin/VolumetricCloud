// ============================================================================
//  Stage12ShadowParameters.h - 단계 12 Cloud Shadow/Deep Cache 공유 계약
// ============================================================================
#pragma once

#include <DirectXMath.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

enum class Stage12ShadowMode : std::uint32_t
{
    DirectReference = 0,
    DeepCache = 1,
};

enum class Stage12ShadowPreset : std::uint32_t
{
    Fast256 = 0,
    Balanced512 = 1,
};

// HLSL ShadowCB(b12)의 열 개 16바이트 묶음과 순서가 정확히 같아야 한다.
struct alignas(16) Stage12ShadowParameters
{
    std::uint32_t shadowMode =
        static_cast<std::uint32_t>(Stage12ShadowMode::DeepCache);
    std::uint32_t shadowPreset =
        static_cast<std::uint32_t>(Stage12ShadowPreset::Balanced512);
    std::uint32_t nearResolution = 512;
    std::uint32_t farResolution = 512;

    DirectX::XMFLOAT3 lightRight{ 1.0f, 0.0f, 0.0f };
    float nearWidthMeters = 24000.0f;

    DirectX::XMFLOAT3 lightUp{ 0.0f, 0.0f, 1.0f };
    float farWidthMeters = 128000.0f;

    DirectX::XMFLOAT3 lightForward{ 0.0f, 1.0f, 0.0f };
    float cloudBottomMeters = 1500.0f;

    DirectX::XMFLOAT3 nearCenter{};
    float cloudTopMeters = 7500.0f;

    DirectX::XMFLOAT3 farCenter{};
    float maximumOpticalDepth = 9.21034037f;

    std::uint32_t nearSliceCount = 80;
    std::uint32_t farSliceCount = 40;
    std::uint32_t surfaceShadowEnabled = 1;
    std::uint32_t cacheReady = 0;

    float nearCoreEnd = 0.80f;
    float nearBlendEnd = 0.95f;
    float farFadeStart = 0.90f;
    float farFadeEnd = 1.00f;

    float surfaceShadowStrength = 1.0f;
    float surfaceAmbientFloor = 0.35f;
    float minimumSunY = 0.052335956f; // sin(3 degrees)
    float cacheDebugExposure = 4.0f;

    std::uint32_t dispatchCascade = 0;
    std::uint32_t debugNearSlice = 0;
    std::uint32_t debugFarSlice = 0;
    std::uint32_t padding0 = 0;
};

static_assert(sizeof(Stage12ShadowParameters) == 160,
              "Stage12ShadowParameters must match ShadowCB");

namespace stage12shadow
{
inline constexpr std::uint32_t kFastResolution = 256;
inline constexpr std::uint32_t kBalancedResolution = 512;
inline constexpr std::uint32_t kNearSlices = 80;
inline constexpr std::uint32_t kFarSlices = 40;
inline constexpr float kNearWidthMeters = 24000.0f;
inline constexpr float kFarWidthMeters = 128000.0f;

inline const char* ModeName(Stage12ShadowMode mode)
{
    return mode == Stage12ShadowMode::DeepCache
        ? "Deep Cache" : "Direct Reference";
}

inline const char* PresetName(Stage12ShadowPreset preset)
{
    return preset == Stage12ShadowPreset::Fast256
        ? "Fast 256" : "Balanced 512";
}

inline std::uint32_t Resolution(Stage12ShadowPreset preset)
{
    return preset == Stage12ShadowPreset::Fast256
        ? kFastResolution : kBalancedResolution;
}

inline std::uint64_t CacheBytes(Stage12ShadowPreset preset)
{
    const std::uint64_t resolution = Resolution(preset);
    return resolution * resolution *
        static_cast<std::uint64_t>(kNearSlices + kFarSlices) * sizeof(float);
}

inline Stage12ShadowParameters Sanitize(Stage12ShadowParameters value)
{
    value.shadowMode = std::min(
        value.shadowMode,
        static_cast<std::uint32_t>(Stage12ShadowMode::DeepCache));
    value.shadowPreset = std::min(
        value.shadowPreset,
        static_cast<std::uint32_t>(Stage12ShadowPreset::Balanced512));
    const auto preset = static_cast<Stage12ShadowPreset>(value.shadowPreset);
    value.nearResolution = Resolution(preset);
    value.farResolution = value.nearResolution;
    value.nearWidthMeters = kNearWidthMeters;
    value.farWidthMeters = kFarWidthMeters;
    value.nearSliceCount = kNearSlices;
    value.farSliceCount = kFarSlices;
    value.surfaceShadowEnabled = value.surfaceShadowEnabled ? 1u : 0u;
    value.cacheReady = value.cacheReady ? 1u : 0u;
    value.nearCoreEnd = 0.80f;
    value.nearBlendEnd = 0.95f;
    value.farFadeStart = 0.90f;
    value.farFadeEnd = 1.00f;
    value.maximumOpticalDepth = 9.21034037f;
    value.minimumSunY = 0.052335956f;
    value.surfaceShadowStrength = std::isfinite(value.surfaceShadowStrength)
        ? std::clamp(value.surfaceShadowStrength, 0.0f, 1.0f) : 1.0f;
    value.surfaceAmbientFloor = std::isfinite(value.surfaceAmbientFloor)
        ? std::clamp(value.surfaceAmbientFloor, 0.0f, 1.0f) : 0.35f;
    value.cacheDebugExposure = std::isfinite(value.cacheDebugExposure)
        ? std::clamp(value.cacheDebugExposure, 0.1f, 20.0f) : 4.0f;
    value.dispatchCascade = std::min(value.dispatchCascade, 1u);
    value.debugNearSlice = std::min(
        value.debugNearSlice, value.nearSliceCount - 1u);
    value.debugFarSlice = std::min(
        value.debugFarSlice, value.farSliceCount - 1u);
    value.padding0 = 0u;
    return value;
}

inline void ApplyPreset(Stage12ShadowParameters& value,
                        Stage12ShadowPreset preset)
{
    value.shadowPreset = static_cast<std::uint32_t>(preset);
    const std::uint32_t resolution = Resolution(preset);
    value.nearResolution = resolution;
    value.farResolution = resolution;
}

inline Stage12ShadowMode ModeFromSnapshot(
    std::uint32_t schemaVersion, std::uint32_t serializedMode)
{
    return schemaVersion >= 34u && serializedMode ==
        static_cast<std::uint32_t>(Stage12ShadowMode::DeepCache)
        ? Stage12ShadowMode::DeepCache : Stage12ShadowMode::DirectReference;
}
}
