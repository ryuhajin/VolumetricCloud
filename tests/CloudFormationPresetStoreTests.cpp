// ============================================================================
//  CloudFormationPresetStoreTests.cpp - Stage 15B formation/8-slot 회귀
// ============================================================================
#include "CloudFormationPresetStore.h"

#include <chrono>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

std::filesystem::path MakeTemporaryRoot()
{
    const auto serial = std::chrono::high_resolution_clock::now()
        .time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
        ("vcloud-formation-store-" + std::to_string(serial));
}

struct TemporaryRoot
{
    std::filesystem::path path = MakeTemporaryRoot();
    ~TemporaryRoot()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

std::string ReadFileBytes(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(input),
             std::istreambuf_iterator<char>() };
}

void WriteFileBytes(const std::filesystem::path& path,
                    const std::string& bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    Require(static_cast<bool>(output), "Write JSON mutation fixture");
}

std::string ReplaceJsonNumber(const std::string& input, const char* key,
                              const char* replacement)
{
    std::string result = input;
    const std::string token = std::string("\"") + key + "\"";
    const std::size_t keyPosition = result.find(token);
    Require(keyPosition != std::string::npos, "Find JSON mutation key");
    const std::size_t colon = result.find(':', keyPosition + token.size());
    Require(colon != std::string::npos, "Find JSON mutation colon");
    const std::size_t valueStart = result.find_first_not_of(
        " \t\r\n", colon + 1u);
    Require(valueStart != std::string::npos,
            "Find JSON mutation value start");
    const std::size_t valueEnd = result.find_first_of(",}\r\n", valueStart);
    Require(valueEnd != std::string::npos, "Find JSON mutation value end");
    result.replace(valueStart, valueEnd - valueStart, replacement);
    return result;
}
}

int main()
{
    const auto urbanTarget = ConceptFormationTarget(
        CloudFormationConcept::UrbanFairWeather);
    const auto snowTarget = ConceptFormationTarget(
        CloudFormationConcept::SnowOvercast);
    const auto desertTarget = ConceptFormationTarget(
        CloudFormationConcept::DesertCirrus);
    const auto stratusTarget = TypeFormationTarget(
        CloudFormationType::Stratus);
    const auto cumulusTarget = TypeFormationTarget(
        CloudFormationType::Cumulus);
    const auto cirrusTarget = TypeFormationTarget(
        CloudFormationType::Cirrus);
    const auto customTarget = CustomFormationTarget();

    Require(CloudFormationPresetPath("root", urbanTarget) ==
                std::filesystem::path("root/concepts/urban-fair-weather.json"),
            "Urban slot path");
    Require(CloudFormationPresetPath("root", stratusTarget) ==
                std::filesystem::path("root/types/stratus.json"),
            "Stratus slot path");
    Require(CloudFormationPresetPath("root", customTarget) ==
                std::filesystem::path("root/custom.json"),
            "Custom slot path");
    Require(CloudFormationCanSaveToPreset(urbanTarget, true) &&
                !CloudFormationCanSaveToPreset(customTarget, true) &&
                !CloudFormationCanSaveToPreset(urbanTarget, false),
            "Save to Preset activation follows target ownership");
    Require(!CloudFormationCanRestoreBuiltIn(
                urbanTarget, true, CloudFormationPresetSource::BuiltIn) &&
            CloudFormationCanRestoreBuiltIn(
                urbanTarget, true,
                CloudFormationPresetSource::UserOverride) &&
            !CloudFormationCanRestoreBuiltIn(
                customTarget, true,
                CloudFormationPresetSource::UserOverride),
            "Restore Built-in activates only for a Concept/Type override");

    CloudFormationSettings urban, snow, desert;
    CloudFormationSettings stratus, cumulus, cirrus;
    Require(ResolveBuiltInCloudFormation(
                CloudFormationConcept::UrbanFairWeather, urban) &&
            ResolveBuiltInCloudFormation(
                CloudFormationConcept::SnowOvercast, snow) &&
            ResolveBuiltInCloudFormation(
                CloudFormationConcept::DesertCirrus, desert) &&
            ResolveBuiltInCloudFormation(CloudFormationType::Stratus,
                                         stratus) &&
            ResolveBuiltInCloudFormation(CloudFormationType::Cumulus,
                                         cumulus) &&
            ResolveBuiltInCloudFormation(CloudFormationType::Cirrus,
                                         cirrus),
            "Built-in resolvers");
    Require(CloudFormationSettingsEqual(stratus, snow),
            "F1 Stratus initial copy equals F4 Snow");
    Require(CloudFormationSettingsEqual(cumulus, urban),
            "F1 Cumulus initial copy equals F4 Urban");
    Require(CloudFormationSettingsEqual(cirrus, desert),
            "F1 Cirrus initial copy equals F4 Desert");
    Require(cloudshape::Mode(cirrus.shape.shapeMode) ==
                CloudShapeMode::CirrusPhysicalLayer &&
            cirrus.weather.cloudTypeMode == CloudTypeMode::WeatherMap,
            "Cirrus uses shape variant and Weather G texture");

    PreparedCloudFormation prepared;
    std::string status;
    Require(PrepareCloudFormationSettings(urban, prepared, status),
            "Urban formation preflight");
    Require(prepared.fit.valid &&
                prepared.fit.remainingHeadroomMeters >= 200.0f - 1.0e-4f,
            "Urban keeps required 200m top headroom");
    CloudFormationSettings invalidFit = urban;
    invalidFit.domainThicknessMeters = 3600.0f;
    Require(!PrepareCloudFormationSettings(
                invalidFit, prepared, status),
            "Domain-fit failure is rejected atomically");

    TemporaryRoot temporary;
    const std::filesystem::path root = temporary.path / "cloud-presets";
    CloudFormationSettings editedStratus = stratus;
    editedStratus.coverage = 0.73f;
    Require(SaveCloudFormationPresetAtomic(
                CloudFormationPresetPath(root, stratusTarget), stratusTarget,
                editedStratus, status),
            "Save independent F1 Stratus override");
    Require(!std::filesystem::exists(
                CloudFormationPresetPath(root, snowTarget)),
            "F1 save does not create F4 Snow file");

    CloudFormationPresetSource source = CloudFormationPresetSource::BuiltIn;
    CloudFormationSettings loaded;
    Require(!ResolveCloudFormationPreset(
                root, customTarget, true, loaded, source, status) &&
            status == "No saved Custom",
            "Missing Custom reports the exact no-save state");
    const std::filesystem::path customPath =
        CloudFormationPresetPath(root, customTarget);
    const std::string corruptCustomBytes = "{ broken custom preserved }";
    WriteFileBytes(customPath, corruptCustomBytes);
    Require(!ResolveCloudFormationPreset(
                root, customTarget, true, loaded, source, status) &&
            status.find("rejected") != std::string::npos &&
            ReadFileBytes(customPath) == corruptCustomBytes,
            "Corrupt Custom is preserved and rejected instead of reported saved");
    std::error_code removeCorruptCustomError;
    std::filesystem::remove(customPath, removeCorruptCustomError);
    Require(!removeCorruptCustomError,
            "Remove corrupt Custom before valid Custom round-trip");
    Require(ResolveCloudFormationPreset(
                root, stratusTarget, true, loaded, source, status) &&
            source == CloudFormationPresetSource::UserOverride &&
            CloudFormationSettingsEqual(loaded, editedStratus),
            "F1 override schema-1 round-trip");
    Require(ResolveCloudFormationPreset(
                root, snowTarget, true, loaded, source, status) &&
            source == CloudFormationPresetSource::BuiltIn &&
            CloudFormationSettingsEqual(loaded, snow),
            "F4 Snow remains independent built-in");

    CloudFormationSettings editedUrban = urban;
    editedUrban.densityMultiplier = 1.37f;
    Require(SaveCloudFormationPresetAtomic(
                CloudFormationPresetPath(root, urbanTarget), urbanTarget,
                editedUrban, status),
            "Save independent F4 Urban override");
    Require(ResolveCloudFormationPreset(
                root, cumulusTarget, true, loaded, source, status) &&
            source == CloudFormationPresetSource::BuiltIn &&
            CloudFormationSettingsEqual(loaded, cumulus),
            "F4 save does not change F1 Cumulus");

    CloudFormationSettings custom = desert;
    custom.coverage = 0.31f;
    Require(SaveCloudFormationPresetAtomic(
                CloudFormationPresetPath(root, customTarget), customTarget,
                custom, status),
            "Save Custom slot");
    Require(ResolveCloudFormationPreset(
                root, customTarget, true, loaded, source, status) &&
            source == CloudFormationPresetSource::UserOverride &&
            CloudFormationSettingsEqual(loaded, custom),
            "Custom round-trip");

    const std::array<CloudFormationPresetTarget, 8> allTargets = {{
        ConceptFormationTarget(CloudFormationConcept::UrbanFairWeather),
        ConceptFormationTarget(CloudFormationConcept::MeadowBrokenClouds),
        ConceptFormationTarget(CloudFormationConcept::DesertCirrus),
        ConceptFormationTarget(CloudFormationConcept::SnowOvercast),
        TypeFormationTarget(CloudFormationType::Stratus),
        TypeFormationTarget(CloudFormationType::Cumulus),
        TypeFormationTarget(CloudFormationType::Cirrus),
        CustomFormationTarget(),
    }};
    for (const CloudFormationPresetTarget& target : allTargets)
    {
        CloudFormationSettings fixture = urban;
        if (target.group != CloudFormationPresetGroup::Custom)
            Require(ResolveBuiltInCloudFormation(target, fixture),
                    "Resolve eight-slot fixture");
        Require(SaveCloudFormationPresetAtomic(
                    CloudFormationPresetPath(root, target), target,
                    fixture, status),
                "Populate independent eight-slot fixture");
    }
    std::array<std::string, 8> slotBytes = {};
    for (std::size_t index = 0; index < allTargets.size(); ++index)
    {
        slotBytes[index] = ReadFileBytes(
            CloudFormationPresetPath(root, allTargets[index]));
        Require(!slotBytes[index].empty(), "Read eight-slot fixture bytes");
    }
    CloudFormationSettings editedSnow;
    Require(ResolveBuiltInCloudFormation(
                CloudFormationConcept::SnowOvercast, editedSnow),
            "Resolve Snow isolation edit");
    editedSnow.coverage = 0.82f;
    Require(SaveCloudFormationPresetAtomic(
                CloudFormationPresetPath(root, allTargets[3]), allTargets[3],
                editedSnow, status),
            "Overwrite exactly one of eight slots");
    for (std::size_t index = 0; index < allTargets.size(); ++index)
    {
        const std::string after = ReadFileBytes(
            CloudFormationPresetPath(root, allTargets[index]));
        Require(index == 3 ? after != slotBytes[index]
                           : after == slotBytes[index],
                "Saving one slot preserves the other seven byte-for-byte");
        slotBytes[index] = after;
    }
    CloudFormationSettings editedCustom = urban;
    editedCustom.coverage = 0.73f;
    Require(SaveCloudFormationPresetAtomic(
                CloudFormationPresetPath(root, customTarget), customTarget,
                editedCustom, status),
            "Save current formation as independent Custom slot");
    for (std::size_t index = 0; index < allTargets.size(); ++index)
    {
        const std::string after = ReadFileBytes(
            CloudFormationPresetPath(root, allTargets[index]));
        Require(index == 7 ? after != slotBytes[index]
                           : after == slotBytes[index],
                "Save as Custom preserves the other seven byte-for-byte");
    }

    CloudFormationSettings bypassed;
    Require(ResolveCloudFormationPreset(
                root, stratusTarget, false, bypassed, source, status) &&
            source == CloudFormationPresetSource::BuiltIn &&
            CloudFormationSettingsEqual(bypassed, stratus),
            "Automated runs bypass user override");
    Require(!ResolveCloudFormationPreset(
                root, customTarget, false, bypassed, source, status),
            "Automated runs cannot load user Custom");

    const std::filesystem::path corruptPath =
        CloudFormationPresetPath(root, desertTarget);
    std::filesystem::create_directories(corruptPath.parent_path());
    {
        std::ofstream corrupt(corruptPath, std::ios::binary);
        corrupt << "{ broken json preserved }";
    }
    Require(ResolveCloudFormationPreset(
                root, desertTarget, true, loaded, source, status) &&
            source == CloudFormationPresetSource::BuiltIn &&
            CloudFormationSettingsEqual(loaded, desert) &&
            std::filesystem::exists(corruptPath),
            "Corrupt concept file is preserved and falls back");

    const CloudFormationPresetTarget meadowTarget = ConceptFormationTarget(
        CloudFormationConcept::MeadowBrokenClouds);
    CloudFormationSettings meadow;
    Require(ResolveBuiltInCloudFormation(
                CloudFormationConcept::MeadowBrokenClouds, meadow),
            "Resolve Meadow invalid-file fallback baseline");
    const std::filesystem::path outOfRangePath =
        CloudFormationPresetPath(root, meadowTarget);
    const std::string outOfRangeBytes = ReplaceJsonNumber(
        ReadFileBytes(outOfRangePath), "weatherWorldSizeMeters", "1000");
    WriteFileBytes(outOfRangePath, outOfRangeBytes);
    Require(ResolveCloudFormationPreset(
                root, meadowTarget, true, loaded, source, status) &&
            source == CloudFormationPresetSource::BuiltIn &&
            CloudFormationSettingsEqual(loaded, meadow) &&
            ReadFileBytes(outOfRangePath) == outOfRangeBytes,
            "Out-of-range override is preserved and falls back to built-in");

    const std::filesystem::path domainFitPath =
        CloudFormationPresetPath(root, urbanTarget);
    const std::string domainFitBytes = ReplaceJsonNumber(
        ReadFileBytes(domainFitPath), "domainThicknessMeters", "3000");
    WriteFileBytes(domainFitPath, domainFitBytes);
    Require(ResolveCloudFormationPreset(
                root, urbanTarget, true, loaded, source, status) &&
            source == CloudFormationPresetSource::BuiltIn &&
            CloudFormationSettingsEqual(loaded, urban) &&
            ReadFileBytes(domainFitPath) == domainFitBytes,
            "Domain-fit override is preserved and falls back to built-in");

    CloudFormationSettings badRange = urban;
    badRange.weatherWorldSizeMeters = 1000.0f;
    Require(!SaveCloudFormationPresetAtomic(
                temporary.path / "bad.json", urbanTarget, badRange, status),
            "Out-of-range preset save rejected");
    const std::filesystem::path replaceFailure =
        temporary.path / "replace-failure.json";
    std::filesystem::create_directories(replaceFailure);
    Require(!SaveCloudFormationPresetAtomic(
                replaceFailure, urbanTarget, urban, status) &&
            std::filesystem::is_directory(replaceFailure) &&
            !std::filesystem::exists(
                replaceFailure.wstring() + L".tmp"),
            "Atomic replace failure preserves destination and removes temp");

    CloudParameters cloud;
    cloud.stepSize = 77.0f;
    cloud.maxViewSteps = 333u;
    cloud.noiseOffset = 9.0f;
    cloud.weatherMapOffset = { 3.0f, 4.0f };
    CloudShapeParameters shape;
    CloudDomainParameters domain;
    WeatherMapGeneratorSettings weather;
    Stage5WeatherPreset weatherPreset = Stage5WeatherPreset::ChannelDebug;
    NoiseVolumeParameters noise;
    noise.seed = 919u;
    noise.baseResolution = 64u;
    noise.detailResolution = 16u;
    const DirectX::XMUINT4 originalBaseFrequencies = noise.baseFrequencies;
    Require(PrepareCloudFormationSettings(cirrus, prepared, status),
            "Cirrus preflight");
    WriteCloudFormationToRuntime(
        prepared, cloud, shape, domain, weatherPreset, weather, noise);
    Require(cloud.stepSize == 77.0f && cloud.maxViewSteps == 333u &&
                cloud.noiseOffset == 9.0f &&
                cloud.weatherMapOffset.x == 3.0f &&
                cloud.weatherMapOffset.y == 4.0f,
            "Formation apply preserves quality/time offsets");
    Require(noise.seed == 919u && noise.baseResolution == 64u &&
                noise.detailResolution == 16u &&
                noise.noiseSource == static_cast<std::uint32_t>(
                    NoiseSource::Texture3D) &&
                noise.baseFrequencies.x == originalBaseFrequencies.x &&
                noise.baseFrequencies.y == originalBaseFrequencies.y &&
                noise.baseFrequencies.z == originalBaseFrequencies.z &&
                noise.baseFrequencies.w == originalBaseFrequencies.w,
            "Cirrus formation preserves shared noise texture identity");

    // Raw ImGui 후보를 먼저 감지해야 sanitize collision이 runtime에 남지 않는다.
    shape.cirrusMaximumThicknessMeters =
        shape.cirrusMinimumThicknessMeters;
    const CloudFormationSettings rawShapeBaseline =
        CaptureCloudFormationSettingsUnchecked(
            cloud, shape, domain, weatherPreset, weather, noise);
    const CloudFormationSettings canonicalShapeBaseline =
        CaptureCloudFormationSettings(
            cloud, shape, domain, weatherPreset, weather, noise);
    shape.cirrusMaximumThicknessMeters =
        shape.cirrusMinimumThicknessMeters - 10.0f;
    const CloudFormationSettings rawShapeCollision =
        CaptureCloudFormationSettingsUnchecked(
            cloud, shape, domain, weatherPreset, weather, noise);
    const CloudFormationSettings canonicalShapeCollision =
        CaptureCloudFormationSettings(
            cloud, shape, domain, weatherPreset, weather, noise);
    Require(!CloudFormationSettingsEqual(
                rawShapeBaseline, rawShapeCollision, 0.0f) &&
            CloudFormationSettingsEqual(
                canonicalShapeBaseline, canonicalShapeCollision),
            "Raw min/max edit is visible even when sanitize returns prior canonical value");

    shape = prepared.settings.shape;
    const CloudFormationSettings rawWindBaseline =
        CaptureCloudFormationSettingsUnchecked(
            cloud, shape, domain, weatherPreset, weather, noise);
    const CloudFormationSettings canonicalWindBaseline =
        CaptureCloudFormationSettings(
            cloud, shape, domain, weatherPreset, weather, noise);
    cloud.windDirection = {
        cloud.windDirection.x * 2.0f, 0.75f,
        cloud.windDirection.z * 2.0f
    };
    const CloudFormationSettings rawWindCollision =
        CaptureCloudFormationSettingsUnchecked(
            cloud, shape, domain, weatherPreset, weather, noise);
    const CloudFormationSettings canonicalWindCollision =
        CaptureCloudFormationSettings(
            cloud, shape, domain, weatherPreset, weather, noise);
    Require(!CloudFormationSettingsEqual(
                rawWindBaseline, rawWindCollision, 0.0f) &&
            CloudFormationSettingsEqual(
                canonicalWindBaseline, canonicalWindCollision),
            "Raw wind length/Y edit is visible even when canonical direction is unchanged");

    const std::filesystem::path legacyPath =
        temporary.path / "legacy-custom.json";
    CloudAppearanceSettings legacy = CumulusAppearance();
    Require(SaveCustomCloudAppearanceAtomic(legacyPath, legacy, status),
            "Create legacy Custom fixture");
    CloudFormationSettings migrationBase = urban;
    const std::filesystem::path migrationRoot =
        temporary.path / "migration";
    Require(MigrateLegacyCustomCloudFormation(
                legacyPath, migrationRoot, migrationBase, status) ==
                LegacyCloudFormationMigrationResult::Migrated,
            "Schema 29/30 Custom migration");
    Require(ResolveCloudFormationPreset(
                migrationRoot, customTarget, true, loaded, source, status) &&
            loaded.coverage == legacy.globalCoverage &&
            loaded.weather.cloudTypeMode == legacy.cloudTypeMode &&
            loaded.shape.cumulusMaximumThicknessMeters ==
                legacy.cumulusMaximumThicknessMeters &&
            loaded.domainThicknessMeters >=
                legacy.cumulusMaximumThicknessMeters + 200.0f,
            "Schema 29/30 migration preserves 6 km shape and derives fitting domain");

    std::cout << "CloudFormationPresetStoreTests passed\n";
    return EXIT_SUCCESS;
}
