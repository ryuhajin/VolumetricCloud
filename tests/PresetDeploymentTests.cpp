#include "CloudFormationPresetStore.h"
#include "LightingPresetStore.h"
#include "PresetPaths.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

static void Require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "expected shipped preset directory");
        const auto shipped = std::filesystem::u8path(argv[1]);
        std::string status;
        CloudFormationPresetSource source;
        for (unsigned i = 0; i < 4; ++i)
        {
            CloudFormationSettings formation;
            const auto target = i == 3 ? CustomFormationTarget() :
                TypeFormationTarget(static_cast<CloudFormationType>(i));
            Require(ResolveCloudFormationPreset(shipped, target, true,
                formation, source, status) && source == CloudFormationPresetSource::UserOverride,
                "shipped formation JSON must load without fallback");
            LightingPresetSettings lighting;
            Require(ResolveLightingPreset(shipped, static_cast<Stage15ConceptPreset>(i + 3),
                true, lighting, source, status) && source == CloudFormationPresetSource::UserOverride,
                "shipped lighting JSON must load without fallback");
        }
        const auto temporary = std::filesystem::temp_directory_path() /
            ("vcloud-paths-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        const auto originalCwd = std::filesystem::current_path();
        std::filesystem::create_directories(temporary / "source");
        std::filesystem::create_directories(temporary / "unrelated");
        std::filesystem::current_path(temporary / "unrelated");
        Require(SelectPresetRoot(temporary / "source", temporary / "exe") == temporary / "source",
            "development source wins independently of cwd");
        Require(SelectPresetRoot(temporary / "missing", temporary / "exe") == temporary / "exe/presets",
            "source-less deployment uses executable directory");
        // 선택 루트의 파일 누락은 다른 폴더 대신 내장값으로 처리한다.
        LightingPresetSettings lighting;
        Require(ResolveLightingPreset(temporary / "source", Stage15ConceptPreset::BrightNoon,
            true, lighting, source, status) && source == CloudFormationPresetSource::BuiltIn,
            "missing slot stays in selected root");
        Require(SaveLightingPreset(temporary / "source", Stage15ConceptPreset::BrightNoon,
            lighting, status), "save uses selected source root");
        Require(!std::filesystem::exists(temporary / "exe/presets"), "no silent alternate save");
        std::filesystem::current_path(originalCwd);
        std::filesystem::remove_all(temporary);
        std::cout << "PRESET_DEPLOYMENT=PASS\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
