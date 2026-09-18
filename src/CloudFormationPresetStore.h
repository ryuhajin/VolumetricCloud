// ============================================================================
//  CloudFormationPresetStore.h - 내장 formation과 단일 Custom 저장
// ============================================================================
#pragma once

#include "CloudFormationSettings.h"

#include <cstdint>
#include <filesystem>
#include <string>

enum class CloudFormationPresetGroup : std::uint32_t
{
    Concept = 0,
    Type = 1,
    Custom = 2,
    None = 3,
};

enum class CloudFormationConcept : std::uint32_t
{
    UrbanFairWeather = 0,
    MeadowBrokenClouds = 1,
    SnowOvercast = 2,
};

enum class CloudFormationType : std::uint32_t
{
    Stratus = 0,
    Cumulus = 1,
    Mixed = 2,
};

struct CloudFormationPresetTarget
{
    CloudFormationPresetGroup group = CloudFormationPresetGroup::None;
    std::uint32_t index = 0u;
};

enum class CloudFormationPresetSource : std::uint32_t
{
    BuiltIn = 0,
    UserOverride = 1,
    Unsaved = 2,
};

CloudFormationPresetTarget ConceptFormationTarget(
    CloudFormationConcept concept);
CloudFormationPresetTarget TypeFormationTarget(CloudFormationType type);
CloudFormationPresetTarget CustomFormationTarget();
CloudFormationPresetTarget NoFormationTarget();
bool IsValidCloudFormationPresetTarget(
    const CloudFormationPresetTarget& target);
bool CloudFormationPresetTargetEqual(
    const CloudFormationPresetTarget& a,
    const CloudFormationPresetTarget& b);
const char* CloudFormationPresetTargetName(
    const CloudFormationPresetTarget& target);
const char* CloudFormationPresetGroupName(CloudFormationPresetGroup group);
const char* CloudFormationPresetSourceName(CloudFormationPresetSource source);
bool CloudFormationCanSaveToPreset(
    const CloudFormationPresetTarget& target, bool targetValid);
std::filesystem::path DefaultCloudFormationPresetRoot();
std::filesystem::path CloudFormationPresetPath(
    const std::filesystem::path& root,
    const CloudFormationPresetTarget& target);

// 내장 기본값. Type/Custom은 JSON 저장값으로 덮어쓸 수 있다.
bool ResolveBuiltInCloudFormation(
    CloudFormationConcept concept,
    CloudFormationSettings& outSettings);
bool ResolveBuiltInCloudFormation(
    CloudFormationType type,
    CloudFormationSettings& outSettings);
bool ResolveBuiltInCloudFormation(
    const CloudFormationPresetTarget& target,
    CloudFormationSettings& outSettings);

bool SaveCloudFormationPresetAtomic(
    const std::filesystem::path& path,
    const CloudFormationPresetTarget& target,
    const CloudFormationSettings& settings,
    std::string& status);
bool LoadCloudFormationPreset(
    const std::filesystem::path& path,
    const CloudFormationPresetTarget& expectedTarget,
    CloudFormationSettings& outSettings,
    std::string& status);

// Concept는 역사적 검증 전용 내장값이다. Type/Custom은 저장값을 우선한다. Custom은 schema 4를 쓰고 schema 1~3의 공통 두께/profile을 이관한다.
bool ResolveCloudFormationPreset(
    const std::filesystem::path& root,
    const CloudFormationPresetTarget& target,
    bool allowUserOverrides,
    CloudFormationSettings& outSettings,
    CloudFormationPresetSource& outSource,
    std::string& status);

bool InitializeSnowCustomPreset(const std::filesystem::path& root, std::string& status);
