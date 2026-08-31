// ============================================================================
//  CloudFormationPresetStore.h - Stage 15B 독립 F1/F4 formation 슬롯
// ============================================================================
#pragma once

#include "CloudAppearance.h"
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
    DesertCirrus = 2,
    SnowOvercast = 3,
};

enum class CloudFormationType : std::uint32_t
{
    Stratus = 0,
    Cumulus = 1,
    Cirrus = 2,
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

enum class LegacyCloudFormationMigrationResult : std::uint32_t
{
    NotNeeded = 0,
    NoLegacyFile = 1,
    Migrated = 2,
    Rejected = 3,
    SaveFailed = 4,
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
bool CloudFormationCanRestoreBuiltIn(
    const CloudFormationPresetTarget& target, bool targetValid,
    CloudFormationPresetSource source);

std::filesystem::path DefaultCloudFormationPresetRoot();
std::filesystem::path CloudFormationPresetPath(
    const std::filesystem::path& root,
    const CloudFormationPresetTarget& target);

// Concept와 Type의 내장값은 별도 resolver다. Type resolver는 Concept resolver를
// 호출하지 않으며, Stage 15B 시작 시점 값만 복사해 이후 독립적으로 발전한다.
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

// Concept/Type은 override가 없거나 손상되면 해당 내장값으로 성공한다. Custom은
// 내장값이 없으므로 실패하며 outSettings를 바꾸지 않는다. 자동 smoke/performance는
// allowUserOverrides=false로 사용자 파일을 완전히 우회한다.
bool ResolveCloudFormationPreset(
    const std::filesystem::path& root,
    const CloudFormationPresetTarget& target,
    bool allowUserOverrides,
    CloudFormationSettings& outSettings,
    CloudFormationPresetSource& outSource,
    std::string& status);

bool RemoveCloudFormationPresetOverride(
    const std::filesystem::path& root,
    const CloudFormationPresetTarget& target,
    std::string& status);

CloudFormationSettings ConvertLegacyCloudAppearanceToFormation(
    const CloudAppearanceSettings& legacy,
    const CloudFormationSettings& baseFormation);
LegacyCloudFormationMigrationResult MigrateLegacyCustomCloudFormation(
    const std::filesystem::path& legacyAppearancePath,
    const std::filesystem::path& newPresetRoot,
    const CloudFormationSettings& baseFormation,
    std::string& status);
