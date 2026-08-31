#include "DeveloperUiSettings.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>

namespace
{
[[noreturn]] void Fail(const char* message)
{
    std::cerr << "[DEVELOPER-UI][FAIL] " << message << '\n';
    std::exit(1);
}

void Require(bool condition, const char* message)
{
    if (!condition)
        Fail(message);
}

bool Near(float actual, float expected)
{
    return std::abs(actual - expected) <= 1e-5f;
}
}

int main()
{
    Require(Near(developerui::SanitizeUserZoom(1.0f), 1.0f),
            "default zoom changed");
    Require(Near(developerui::SanitizeUserZoom(0.5f), 1.0f) &&
            Near(developerui::SanitizeUserZoom(3.0f), 2.0f),
            "zoom range clamp failed");
    Require(Near(developerui::SanitizeUserZoom(1.274f), 1.25f) &&
            Near(developerui::SanitizeUserZoom(1.276f), 1.30f),
            "zoom 5 percent quantization failed");
    Require(Near(developerui::SanitizeUserZoom(
                     std::numeric_limits<float>::quiet_NaN()), 1.0f),
            "non-finite zoom must return default");
    Require(Near(developerui::EffectiveScale(96u, 1.0f), 1.0f) &&
            Near(developerui::EffectiveScale(144u, 1.25f), 1.875f) &&
            Near(developerui::EffectiveScale(192u, 2.0f), 4.0f),
            "DPI x user zoom contract failed");

    std::error_code error;
    const std::filesystem::path root =
        std::filesystem::temp_directory_path(error) /
        (L"VolumetricCloudDeveloperUiSettings-" +
         std::to_wstring(GetCurrentProcessId()));
    Require(!error, "temporary directory lookup failed");
    std::filesystem::create_directories(root, error);
    Require(!error, "temporary directory creation failed");
    const std::filesystem::path path = root / L"developer-ui.json";

    developerui::Settings saved;
    saved.userZoom = 1.274f;
    std::string status;
    Require(developerui::SaveAtomic(path, saved, status),
            "atomic save failed");
    Require(!std::filesystem::exists(path.wstring() + L".tmp"),
            "temporary file survived successful replace");
    developerui::Settings loaded;
    loaded.userZoom = 2.0f;
    Require(developerui::Load(path, loaded, status) &&
            Near(loaded.userZoom, 1.25f),
            "schema 1 round trip failed");

    {
        std::ofstream invalid(path, std::ios::binary | std::ios::trunc);
        invalid << "{\"schemaVersion\":2,\"userZoom\":1.50}";
    }
    loaded.userZoom = 1.75f;
    Require(!developerui::Load(path, loaded, status) &&
            Near(loaded.userZoom, 1.75f),
            "invalid schema must not mutate live settings");
    {
        std::ofstream clamped(path, std::ios::binary | std::ios::trunc);
        clamped << "{\"schemaVersion\":1,\"userZoom\":9.0}";
    }
    Require(developerui::Load(path, loaded, status) &&
            Near(loaded.userZoom, 2.0f),
            "schema 1 load must sanitize zoom to the public range");

    const std::filesystem::path replacementDirectory = root / L"directory";
    std::filesystem::create_directories(replacementDirectory, error);
    Require(!error, "replacement failure fixture creation failed");
    Require(!developerui::SaveAtomic(replacementDirectory, saved, status),
            "directory target must fail atomic replace");
    Require(!std::filesystem::exists(
                replacementDirectory.wstring() + L".tmp"),
            "failed replace left a temporary file");

    std::filesystem::remove(path, error);
    std::filesystem::remove(replacementDirectory, error);
    std::filesystem::remove(root, error);
    std::cout << "[DEVELOPER-UI] schema=1 dpi=144 zoom=1.25 effective=1.875 PASS\n";
    return 0;
}
