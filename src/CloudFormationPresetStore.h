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

// Concept와 Type은 파일로 덮어쓸 수 없는 내장값이다.
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

// Concept/Type은 항상 내장값을 적용한다. Custom만 schema 1 파일을 읽는다.
bool ResolveCloudFormationPreset(
    const std::filesystem::path& root,
    const CloudFormationPresetTarget& target,
    bool allowUserOverrides,
    CloudFormationSettings& outSettings,
    CloudFormationPresetSource& outSource,
    std::string& status);
