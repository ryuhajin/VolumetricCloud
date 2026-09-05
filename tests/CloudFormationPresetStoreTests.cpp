#include "CloudFormationPresetStore.h"

#include <chrono>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

std::string ReadText(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(input),
             std::istreambuf_iterator<char>() };
}

void WriteText(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
    Require(output.good(), "write fixture");
}

bool ReplaceJsonNumber(std::string& text, const char* key,
                       const char* replacement)
{
    const std::string token = std::string("\"") + key + "\"";
    const std::size_t keyPosition = text.find(token);
    if (keyPosition == std::string::npos)
        return false;
    const std::size_t colon = text.find(':', keyPosition + token.size());
    const std::size_t begin = text.find_first_not_of(" \t\r\n", colon + 1u);
    const std::size_t end = text.find_first_of(",}\r\n", begin);
    if (colon == std::string::npos || begin == std::string::npos ||
        end == std::string::npos)
        return false;
    text.replace(begin, end - begin, replacement);
    return true;
}
}

int main()
{
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("vcloud-formation-" + std::to_string(nonce));
    std::filesystem::create_directories(root);

    const CloudFormationPresetTarget custom = CustomFormationTarget();
    const std::filesystem::path customPath =
        CloudFormationPresetPath(root, custom);
    Require(customPath == root / "custom-cloud.json",
            "Custom uses one schema 1 file");
    Require(!CloudFormationCanSaveToPreset(
                TypeFormationTarget(CloudFormationType::Mixed), true) &&
            CloudFormationCanSaveToPreset(custom, true),
            "only Custom is writable");

    for (std::uint32_t index = 0u; index < 3u; ++index)
    {
        CloudFormationSettings type;
        Require(ResolveBuiltInCloudFormation(
                    static_cast<CloudFormationType>(index), type),
                "all three type presets resolve");
        PreparedCloudFormation prepared;
        std::string status;
        Require(PrepareCloudFormationSettings(
                    type, prepared, status, 200.0f),
                "type preset has 200m domain headroom");

        CloudFormationSettings concept;
        Require(ResolveBuiltInCloudFormation(
                    static_cast<CloudFormationConcept>(index), concept),
                "all three concepts resolve");
        Require(PrepareCloudFormationSettings(
                    concept, prepared, status, 200.0f),
                "concept has 200m domain headroom");
    }

    CloudFormationSettings original;
    Require(ResolveBuiltInCloudFormation(
                CloudFormationType::Mixed, original),
            "resolve Mixed fixture");
    original.coverage = 0.63f;
    std::string status;
    Require(SaveCloudFormationPresetAtomic(
                customPath, custom, original, status),
            "atomic Custom save");
    Require(std::filesystem::exists(customPath) &&
            !std::filesystem::exists(customPath.wstring() + L".tmp"),
            "atomic save leaves only final file");
    const std::string savedText = ReadText(customPath);
    Require(savedText.find("\"schemaVersion\": 2") != std::string::npos &&
            savedText.find("windSpeedMetersPerSecond") == std::string::npos &&
            savedText.find("quality") == std::string::npos &&
            savedText.find("lighting") == std::string::npos &&
            savedText.find("debug") == std::string::npos,
            "schema 2 stores formation without session motion");

    CloudFormationSettings loaded;
    Require(LoadCloudFormationPreset(
                customPath, custom, loaded, status) &&
            CloudFormationSettingsEqual(
                SanitizeCloudFormationSettings(original), loaded),
            "Custom schema round-trip");

    std::string schema1 = savedText;
    Require(ReplaceJsonNumber(schema1, "schemaVersion", "1"),
            "schema 1 migration fixture version");
    const std::size_t selectionKey = schema1.find("\"cloudTypeSelection\"");
    Require(selectionKey != std::string::npos,
            "schema 1 migration fixture selection");
    schema1.replace(selectionKey, std::strlen("\"cloudTypeSelection\""),
                    "\"weatherCloudTypeMode\"");
    WriteText(customPath, schema1);
    Require(LoadCloudFormationPreset(customPath, custom, loaded, status) &&
            loaded.typeSelection.mode == CloudTypeSelectionMode::RegionalBlend,
            "schema 1 type mode migrates while legacy wind is ignored");

    std::string malformed = savedText;
    const std::size_t coverageKey = malformed.find("\"coverage\"");
    Require(coverageKey != std::string::npos, "malformed fixture edit");
    malformed.replace(coverageKey, 10u, "\"coveragx\"");
    WriteText(customPath, malformed);
    Require(!LoadCloudFormationPreset(customPath, custom, loaded, status),
            "malformed JSON rejected");

    std::string badRange = savedText;
    Require(ReplaceJsonNumber(badRange, "coverage", "2.0"),
            "range fixture edit");
    WriteText(customPath, badRange);
    Require(!LoadCloudFormationPreset(customPath, custom, loaded, status),
            "out-of-range formation rejected");

    std::string badFit = savedText;
    Require(ReplaceJsonNumber(
                badFit, "domainThicknessMeters", "100.0"),
            "domain fixture edit");
    WriteText(customPath, badFit);
    Require(!LoadCloudFormationPreset(customPath, custom, loaded, status),
            "domain-fit failure rejected");

    std::string oldSchema = savedText;
    Require(ReplaceJsonNumber(oldSchema, "schemaVersion", "30"),
            "schema fixture edit");
    WriteText(customPath, oldSchema);
    Require(!LoadCloudFormationPreset(customPath, custom, loaded, status),
            "legacy schema rejected without migration");

    WriteText(customPath, savedText);
    CloudFormationSettings invalid = original;
    invalid.coverage = 2.0f;
    Require(!SaveCloudFormationPresetAtomic(
                customPath, custom, invalid, status),
            "invalid save rejected");
    Require(ReadText(customPath) == savedText,
            "failed save preserves prior Custom atomically");

    std::filesystem::create_directories(root / "cloud-presets");
    WriteText(root / "cloud-presets" / "legacy.json",
              "{\"schemaVersion\":30,\"coverage\":0.0}");
    CloudFormationPresetSource source = CloudFormationPresetSource::Unsaved;
    CloudFormationSettings resolved;
    Require(ResolveCloudFormationPreset(
                root, TypeFormationTarget(CloudFormationType::Stratus),
                true, resolved, source, status) &&
            source == CloudFormationPresetSource::BuiltIn &&
            resolved.coverage == 0.40f,
            "legacy cloud-presets directory is ignored");

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
    Require(!cleanupError, "temporary fixture cleanup");
    std::cout << "CloudFormationPresetStore passed\n";
    return 0;
}
