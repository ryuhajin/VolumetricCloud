// 고정 형상 타입. Weather G 기반 지역 선택은 schema4부터 제거한다.
#pragma once
#include <cstdint>
enum class CloudTypeSelectionMode : std::uint32_t { FixedStratus=0, FixedMixed=1, FixedCumulus=2 };
struct CloudTypeSelection { CloudTypeSelectionMode mode=CloudTypeSelectionMode::FixedMixed; };
inline CloudTypeSelection SanitizeCloudTypeSelection(CloudTypeSelection v)
{
    if(static_cast<std::uint32_t>(v.mode)>2u) v.mode=CloudTypeSelectionMode::FixedMixed;
    return v;
}
inline float FixedCloudType(const CloudTypeSelection& v)
{
    return static_cast<float>(static_cast<std::uint32_t>(SanitizeCloudTypeSelection(v).mode))*0.5f;
}
inline float ResolveEffectiveCloudType(const CloudTypeSelection& v, float /*legacySample*/=0.5f) { return FixedCloudType(v); }
// 과거 Regional(3)은 고정 Mixed로 이관한다. 파일 로더가 범위 유효성을 먼저 검사한다.
inline CloudTypeSelection MigrateLegacyCloudTypeMode(std::uint32_t mode)
{ return SanitizeCloudTypeSelection({static_cast<CloudTypeSelectionMode>(mode)}); }
