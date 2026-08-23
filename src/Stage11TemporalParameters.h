// ============================================================================
//  Stage11TemporalParameters.h - 단계 11 Jitter/Temporal CPU-GPU 공유 계약
// ============================================================================
#pragma once

#include <DirectXMath.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

enum class Stage11TemporalMode : std::uint32_t
{
    Off = 0,
    Stable4Phase = 1,
};

enum class Stage11HistoryResetReason : std::uint32_t
{
    None = 0,
    FirstFrame = 1,
    Resize = 2,
    ResolutionOrFilter = 3,
    TemporalToggle = 4,
    CameraCut = 5,
    ParametersChanged = 6,
    TimeDiscontinuity = 7,
    ShaderReload = 8,
    ResourcesRecreated = 9,
    Manual = 10,
};

// HLSL TemporalCB(b11)의 아홉 16바이트 묶음과 순서가 정확히 같아야 한다.
struct alignas(16) Stage11TemporalParameters
{
    DirectX::XMFLOAT4X4 previousViewProjection{};

    DirectX::XMFLOAT3 previousCameraPosition{};
    float deltaTimeSeconds = 0.0f;

    DirectX::XMFLOAT2 jitterOffsetLowResTexels{};
    std::uint32_t frameIndex = 0;
    std::uint32_t historyValid = 0;

    std::uint32_t temporalEnabled = 0;
    std::uint32_t jitterEnabled = 0;
    std::uint32_t neighborhoodClampingEnabled = 1;
    std::uint32_t resetReason =
        static_cast<std::uint32_t>(Stage11HistoryResetReason::FirstFrame);

    float historyWeight = 0.90f;
    float sceneDepthRelativeThreshold = 0.0025f;
    float cloudDepthRelativeThreshold = 0.02f;
    float transmittanceThreshold = 0.08f;

    float nearHistoryFadeStartMeters = 1000.0f;
    float nearHistoryFadeEndMeters = 3000.0f;
    float maxReprojectionMotionPixels = 96.0f;
    float clipGamma = 1.0f;
};

static_assert(sizeof(Stage11TemporalParameters) == 144,
              "Stage11TemporalParameters must match TemporalCB");

namespace stage11temporal
{
inline Stage11TemporalParameters Sanitize(Stage11TemporalParameters value)
{
    value.temporalEnabled = value.temporalEnabled ? 1u : 0u;
    value.jitterEnabled = value.jitterEnabled ? 1u : 0u;
    value.neighborhoodClampingEnabled =
        value.neighborhoodClampingEnabled ? 1u : 0u;
    value.historyValid = value.historyValid ? 1u : 0u;
    value.historyWeight = std::isfinite(value.historyWeight)
        ? std::clamp(value.historyWeight, 0.0f, 0.99f) : 0.90f;
    value.sceneDepthRelativeThreshold =
        std::isfinite(value.sceneDepthRelativeThreshold)
        ? std::clamp(value.sceneDepthRelativeThreshold, 1.0e-6f, 1.0f)
        : 0.0025f;
    value.cloudDepthRelativeThreshold =
        std::isfinite(value.cloudDepthRelativeThreshold)
        ? std::clamp(value.cloudDepthRelativeThreshold, 1.0e-6f, 1.0f)
        : 0.02f;
    value.transmittanceThreshold =
        std::isfinite(value.transmittanceThreshold)
        ? std::clamp(value.transmittanceThreshold, 0.0f, 1.0f) : 0.08f;
    value.nearHistoryFadeStartMeters =
        std::isfinite(value.nearHistoryFadeStartMeters)
        ? std::max(value.nearHistoryFadeStartMeters, 0.0f) : 1000.0f;
    value.nearHistoryFadeEndMeters =
        std::isfinite(value.nearHistoryFadeEndMeters)
        ? std::max(value.nearHistoryFadeEndMeters,
                   value.nearHistoryFadeStartMeters + 1.0f)
        : 3000.0f;
    value.maxReprojectionMotionPixels =
        std::isfinite(value.maxReprojectionMotionPixels)
        ? std::clamp(value.maxReprojectionMotionPixels, 1.0f, 4096.0f)
        : 96.0f;
    value.clipGamma = std::isfinite(value.clipGamma)
        ? std::clamp(value.clipGamma, 0.0f, 4.0f) : 1.0f;
    value.deltaTimeSeconds = std::isfinite(value.deltaTimeSeconds)
        ? std::clamp(value.deltaTimeSeconds, 0.0f, 0.25f) : 0.0f;
    return value;
}

inline void ApplyMode(Stage11TemporalParameters& value,
                      Stage11TemporalMode mode)
{
    const bool enabled = mode == Stage11TemporalMode::Stable4Phase;
    value.temporalEnabled = enabled ? 1u : 0u;
    value.jitterEnabled = enabled ? 1u : 0u;
}

// 전체 snapshot loader가 schema 32 이하를 받을 때 temporal 필드가 없다는
// 사실을 명시적으로 Off로 해석하는 마이그레이션 계약이다.
inline Stage11TemporalMode ModeFromSnapshot(
    std::uint32_t schemaVersion, std::uint32_t serializedMode)
{
    return schemaVersion >= 33u && serializedMode ==
        static_cast<std::uint32_t>(Stage11TemporalMode::Stable4Phase)
        ? Stage11TemporalMode::Stable4Phase : Stage11TemporalMode::Off;
}
}
