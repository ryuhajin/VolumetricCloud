#include "FormationParameterRanges.h"
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
    Require(CloudFormationCanSaveToPreset(
                TypeFormationTarget(CloudFormationType::Mixed), true) &&
            CloudFormationCanSaveToPreset(custom, true),
            "only Custom is writable");

    for (std::uint32_t index = 0u; index < 3u; ++index)
    {
        CloudFormationSettings type;
        Require(ResolveBuiltInCloudFormation(
                    static_cast<CloudFormationType>(index), type),
                "all three type presets resolve");
        // UI에서 노출하는 실제 허용 끝값은 설정 검증에서도 그대로 유효해야 한다.
        for (bool upper : {false, true}) {
            auto edge = type;
            auto& channel = edge.weather.generator.coverage;
            channel.macroPeriod = upper ? formationrange::macroMax : formationrange::macroMin;
            channel.detailPeriod = upper ? formationrange::detailMax : formationrange::detailMin;
            channel.bias = upper ? formationrange::biasMax : formationrange::biasMin;
            channel.contrast = upper ? formationrange::contrastMax : formationrange::contrastMin;
            edge.weather.generator.coverageSoftness = upper ? formationrange::softnessMax : formationrange::softnessMin;
            edge.extinctionPerMeter = upper ? formationrange::extinctionMax : formationrange::extinctionMin;
            edge.densityMultiplier = upper ? formationrange::densityMax : 0.f;
            edge.weather.worldSizeMeters = upper ? 64000.f : 3000.f;
            edge.baseNoiseWorldSizeMeters = upper ? formationrange::baseSizeMax : 1.f;
            edge.detailNoiseWorldSizeMeters = upper ? formationrange::detailSizeMax : 1.f;
            Require(IsValidCloudFormationSettings(edge), "UI endpoints must pass canonical validation");
            Require(CloudFormationSettingsEqual(edge, SanitizeCloudFormationSettings(edge)),
                "UI endpoints must not snap back after sanitize");
            channel.macroPeriod = formationrange::macroMax + 1;
            Require(!IsValidCloudFormationSettings(edge), "out of range period remains rejected");
        }
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
                CloudFormationConcept::MeadowBrokenClouds, original),
            "resolve Mixed fixture");
    original.coverage = 0.63f;
    original.shape.densityShaping = 0.70f;
    std::string status;
    Require(SaveCloudFormationPresetAtomic(
                customPath, custom, original, status),
            "atomic Custom save");
    Require(std::filesystem::exists(customPath) &&
            !std::filesystem::exists(customPath.wstring() + L".tmp"),
            "atomic save leaves only final file");
    const std::string savedText = ReadText(customPath);
    Require(savedText.find("\"schemaVersion\": 4") != std::string::npos &&
            savedText.find("windSpeedMetersPerSecond") == std::string::npos &&
            savedText.find("quality") == std::string::npos &&
            savedText.find("lighting") == std::string::npos &&
            savedText.find("debug") == std::string::npos,
            "schema 3 stores formation without session motion");

    CloudFormationSettings loaded;
    Require(LoadCloudFormationPreset(
                customPath, custom, loaded, status) &&
            CloudFormationSettingsEqual(
                SanitizeCloudFormationSettings(original), loaded),
            "Custom schema round-trip");

    Require(savedText.find("stratusMinimumThicknessMeters")==std::string::npos &&
        savedText.find("weatherCloudTypeSeed")==std::string::npos,"schema4 omits retired fields");
    std::string legacyText=savedText;
    legacyText.insert(legacyText.find('{')+1, R"LEGACY(
"stratusMinimumThicknessMeters": 1500,
"stratusMaximumThicknessMeters": 2500,
"cumulusMinimumThicknessMeters": 3000,
"cumulusMaximumThicknessMeters": 4600,
"stratusBottomFadeEnd": 0.06,
"stratusTopFadeStart": 0.65,
"mixedBottomFadeEnd": 0.1,
"mixedTopFadeStart": 0.86,
"cumulusBottomFadeEnd": 0.08,
"cumulusTopFadeStart": 0.93,
"cumulusUpperMassBottom": 0.65,
"cumulusUpperMassStart": 0.08,
"cumulusUpperMassEnd": 0.7,
"weatherCloudTypeSeed": 2027,
"weatherCloudTypeMacroPeriod": 2,
"weatherCloudTypeDetailPeriod": 4,
"weatherCloudTypeDetailWeight": 0.2,
"weatherCloudTypeBias": 0,
"weatherCloudTypeContrast": 0.85,
)LEGACY");
    Require(ReplaceJsonNumber(legacyText,"cloudTypeSelection","3"),"legacy regional fixture");
    std::string schema1 = legacyText;
    Require(ReplaceJsonNumber(schema1, "schemaVersion", "1"),
            "schema 1 migration fixture version");
    const std::size_t selectionKey = schema1.find("\"cloudTypeSelection\"");
    Require(selectionKey != std::string::npos,
            "schema 1 migration fixture selection");
    schema1.replace(selectionKey, std::strlen("\"cloudTypeSelection\""),
                    "\"weatherCloudTypeMode\"");
    WriteText(customPath, schema1);
    Require(LoadCloudFormationPreset(customPath, custom, loaded, status) &&
            loaded.typeSelection.mode == CloudTypeSelectionMode::FixedMixed &&
            loaded.shape.densityShaping == 0.0f,
            "schema 1 type mode migrates while legacy wind is ignored");

    std::string schema2 = legacyText;
    Require(ReplaceJsonNumber(schema2, "schemaVersion", "2"), "schema 2 fixture");
    WriteText(customPath, schema2);
    Require(LoadCloudFormationPreset(customPath, custom, loaded, status) &&
            loaded.shape.densityShaping == 0.0f, "schema 2 preserves old density");
    std::string schema3=legacyText;
    Require(ReplaceJsonNumber(schema3,"schemaVersion","3"),"schema3 fixture");
    WriteText(customPath,schema3);
    Require(LoadCloudFormationPreset(customPath,custom,loaded,status) &&
        loaded.weather.column.minimumThicknessMeters==2250.f &&
        loaded.weather.column.maximumThicknessMeters==3550.f &&
        loaded.typeSelection.mode==CloudTypeSelectionMode::FixedMixed &&
        loaded.shape.densityShaping==.70f,"schema3 regional migrates to common Mixed");
    for (unsigned mode=0;mode<4;++mode) {
        auto legacy=schema3;
        Require(ReplaceJsonNumber(legacy,"cloudTypeSelection",std::to_string(mode).c_str()),"legacy mode fixture");
        WriteText(customPath,legacy);
        Require(LoadCloudFormationPreset(customPath,custom,loaded,status),"all legacy type modes migrate");
        const float expectedMin=mode==0?1500.f:(mode==2?3000.f:2250.f);
        const float expectedMax=mode==0?2500.f:(mode==2?4600.f:3550.f);
        Require(loaded.weather.column.minimumThicknessMeters==expectedMin &&
            loaded.weather.column.maximumThicknessMeters==expectedMax,"legacy active thickness preserved");
    }
    auto edited=original;
    edited.shape.bottomFadeEnd=.22f; edited.shape.topFadeStart=.74f;
    edited.shape.lowerDensityScale=.4f; edited.shape.upperTransitionStart=.2f; edited.shape.upperTransitionEnd=.8f;
    Require(SaveCloudFormationPresetAtomic(customPath,custom,edited,status) &&
        LoadCloudFormationPreset(customPath,custom,loaded,status) && CloudFormationSettingsEqual(edited,loaded),
        "five common profile controls round trip");
    auto badProfile=savedText;
    Require(ReplaceJsonNumber(badProfile,"bottomFadeEnd","0.95") && ReplaceJsonNumber(badProfile,"topFadeStart","0.2"),"invalid profile fixture");
    WriteText(customPath,badProfile);
    Require(!LoadCloudFormationPreset(customPath,custom,loaded,status),"inverted profile rejected atomically");
    std::string invalidShaping = savedText;
    Require(ReplaceJsonNumber(invalidShaping, "densityShaping", "1.1"), "shaping range fixture");
    WriteText(customPath, invalidShaping);
    Require(!LoadCloudFormationPreset(customPath, custom, loaded, status), "invalid shaping rejected");
    std::string missingShaping = savedText;
    missingShaping.replace(missingShaping.find("densityShaping"), 14, "densityMissing");
    WriteText(customPath, missingShaping);
    Require(!LoadCloudFormationPreset(customPath, custom, loaded, status), "schema 3 requires shaping");

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
            resolved.coverage == 0.60f,
            "legacy cloud-presets directory is ignored");

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
    Require(!cleanupError, "temporary fixture cleanup");
    std::cout << "CloudFormationPresetStore passed\n";
    return 0;
}
