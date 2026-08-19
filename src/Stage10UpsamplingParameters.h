// ============================================================================
//  Stage10UpsamplingParameters.h - 단계 10 저해상도/공간 업샘플링 공유 설정
// ============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

enum class Stage10ResolutionPreset : std::uint32_t
{
    Half = 0,
    TwoThirds = 1,
    ThreeQuarters = 2,
    Full = 3,
    Custom = 4,
};

enum class Stage10UpsampleFilter : std::uint32_t
{
    Nearest = 0,
    Bilinear = 1,
    Joint4 = 2,
    Joint9 = 3,
};

// HLSL UpsamplingCB(b10)의 두 16바이트 묶음과 순서가 정확히 같아야 한다.
struct alignas(16) Stage10UpsamplingParameters
{
    float resolutionScale = 1.0f;
    std::uint32_t filterMode =
        static_cast<std::uint32_t>(Stage10UpsampleFilter::Nearest);
    float sceneDepthRelativeSigma = 0.0025f;
    float cloudDepthRelativeSigma = 0.01f;

    float transmittanceSigma = 0.10f;
    float minimumWeight = 1.0e-4f;
    float padding0 = 0.0f;
    float padding1 = 0.0f;
};

static_assert(sizeof(Stage10UpsamplingParameters) == 32,
              "Stage10UpsamplingParameters must match UpsamplingCB");

namespace stage10upsampling
{
inline constexpr float kHalfScale = 0.5f;
inline constexpr float kTwoThirdsScale = 2.0f / 3.0f;
inline constexpr float kThreeQuartersScale = 0.75f;

// 2026-08-19 사용자 검증을 통과한 활성 비교 목록이다. 제외한 enum 값은
// schema 32 숫자 안정성과 과거 실패 화면 재현을 위해 위 enum에 보존한다.
inline constexpr Stage10ResolutionPreset kRuntimeResolutionCandidates[] = {
    Stage10ResolutionPreset::Half,
    Stage10ResolutionPreset::Full,
};
inline constexpr Stage10UpsampleFilter kRuntimeFilterCandidates[] = {
    Stage10UpsampleFilter::Nearest,
    Stage10UpsampleFilter::Bilinear,
    Stage10UpsampleFilter::Joint4,
};

inline const char* ResolutionPresetName(Stage10ResolutionPreset preset)
{
    switch (preset)
    {
    case Stage10ResolutionPreset::Half: return "50% Axis";
    case Stage10ResolutionPreset::TwoThirds: return "67% Axis";
    case Stage10ResolutionPreset::ThreeQuarters: return "75% Axis";
    case Stage10ResolutionPreset::Full: return "Full";
    case Stage10ResolutionPreset::Custom: return "Custom";
    }
    return "Full";
}

inline const char* FilterName(Stage10UpsampleFilter filter)
{
    switch (filter)
    {
    case Stage10UpsampleFilter::Nearest: return "Nearest";
    case Stage10UpsampleFilter::Bilinear: return "Bilinear";
    case Stage10UpsampleFilter::Joint4: return "Depth/Cloud/T Joint 4";
    case Stage10UpsampleFilter::Joint9: return "Depth/Cloud/T Joint 9";
    }
    return "Depth/Cloud/T Joint 4";
}

inline float ResolutionScale(Stage10ResolutionPreset preset)
{
    switch (preset)
    {
    case Stage10ResolutionPreset::Half: return kHalfScale;
    case Stage10ResolutionPreset::TwoThirds: return kTwoThirdsScale;
    case Stage10ResolutionPreset::ThreeQuarters: return kThreeQuartersScale;
    case Stage10ResolutionPreset::Full: return 1.0f;
    case Stage10ResolutionPreset::Custom: return 1.0f;
    }
    return 1.0f;
}

inline int ScaledExtent(int fullExtent, float scale)
{
    if (fullExtent <= 0)
        return 1;
    const float safeScale = std::isfinite(scale)
        ? std::clamp(scale, 1.0f / 16.0f, 1.0f) : 1.0f;
    return std::max(1, static_cast<int>(
        std::ceil(static_cast<float>(fullExtent) * safeScale)));
}

inline Stage10UpsamplingParameters Sanitize(
    Stage10UpsamplingParameters value)
{
    value.resolutionScale = std::isfinite(value.resolutionScale)
        ? std::clamp(value.resolutionScale, 1.0f / 16.0f, 1.0f) : 1.0f;
    value.filterMode = std::min(
        value.filterMode,
        static_cast<std::uint32_t>(Stage10UpsampleFilter::Joint9));
    value.sceneDepthRelativeSigma =
        std::isfinite(value.sceneDepthRelativeSigma)
        ? std::clamp(value.sceneDepthRelativeSigma, 1.0e-6f, 1.0f)
        : 0.0025f;
    value.cloudDepthRelativeSigma =
        std::isfinite(value.cloudDepthRelativeSigma)
        ? std::clamp(value.cloudDepthRelativeSigma, 1.0e-6f, 1.0f)
        : 0.01f;
    value.transmittanceSigma = std::isfinite(value.transmittanceSigma)
        ? std::clamp(value.transmittanceSigma, 1.0e-4f, 1.0f) : 0.10f;
    value.minimumWeight = std::isfinite(value.minimumWeight)
        ? std::clamp(value.minimumWeight, 1.0e-8f, 0.1f) : 1.0e-4f;
    value.padding0 = 0.0f;
    value.padding1 = 0.0f;
    return value;
}

inline void ApplyResolutionPreset(Stage10UpsamplingParameters& value,
                                  Stage10ResolutionPreset preset)
{
    value.resolutionScale = ResolutionScale(preset);
}
}
