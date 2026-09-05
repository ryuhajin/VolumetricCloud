// ============================================================================
//  CloudTypeSelection.h - 저장된 Weather G와 렌더 타입 선택의 소유권 분리
// ============================================================================
#pragma once

#include <algorithm>
#include <cstdint>

enum class CloudTypeSelectionMode : std::uint32_t
{
    FixedStratus = 0,
    FixedMixed = 1,
    FixedCumulus = 2,
    RegionalBlend = 3,
};

struct CloudTypeSelection
{
    CloudTypeSelectionMode mode = CloudTypeSelectionMode::RegionalBlend;
};

inline CloudTypeSelection SanitizeCloudTypeSelection(CloudTypeSelection value)
{
    if (static_cast<std::uint32_t>(value.mode) >
        static_cast<std::uint32_t>(CloudTypeSelectionMode::RegionalBlend))
        value.mode = CloudTypeSelectionMode::FixedMixed;
    return value;
}

inline float FixedCloudType(const CloudTypeSelection& input)
{
    switch (SanitizeCloudTypeSelection(input).mode)
    {
    case CloudTypeSelectionMode::FixedStratus: return 0.0f;
    case CloudTypeSelectionMode::FixedCumulus: return 1.0f;
    case CloudTypeSelectionMode::FixedMixed:
    case CloudTypeSelectionMode::RegionalBlend:
    default: return 0.5f;
    }
}

inline float RegionalTypeInfluence(const CloudTypeSelection& input)
{
    return SanitizeCloudTypeSelection(input).mode ==
        CloudTypeSelectionMode::RegionalBlend ? 1.0f : 0.0f;
}

inline float ResolveEffectiveCloudType(
    const CloudTypeSelection& selection, float sampledRegionalType)
{
    const float influence = RegionalTypeInfluence(selection);
    return std::clamp(FixedCloudType(selection) * (1.0f - influence) +
        std::clamp(sampledRegionalType, 0.0f, 1.0f) * influence, 0.0f, 1.0f);
}

// schema 1의 0/1/2/3(Stratus/Mixed/Cumulus/WeatherMap)을 동등하게 이관한다.
inline CloudTypeSelection MigrateLegacyCloudTypeMode(std::uint32_t mode)
{
    CloudTypeSelection result;
    switch (mode)
    {
    case 0u: result.mode = CloudTypeSelectionMode::FixedStratus; break;
    case 2u: result.mode = CloudTypeSelectionMode::FixedCumulus; break;
    case 3u: result.mode = CloudTypeSelectionMode::RegionalBlend; break;
    case 1u:
    default: result.mode = CloudTypeSelectionMode::FixedMixed; break;
    }
    return result;
}
