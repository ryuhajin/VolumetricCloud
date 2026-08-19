#include "CloudAppearance.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
[[noreturn]] void Fail(const char* message)
{
    std::fprintf(stderr, "CloudAppearance failure: %s\n", message);
    std::exit(1);
}

bool Near(float a, float b, float tolerance = 1.0e-6f)
{
    return std::abs(a - b) <= tolerance;
}

struct Occupancy
{
    double nonZero = 0.0;
    double core = 0.0;
};

Occupancy MeasureOccupancy(const CloudAppearanceSettings& appearance)
{
    WeatherMapGeneratorSettings generator;
    CloudParameters cloud;
    CloudShapeParameters shape;
    ApplyCloudAppearance(appearance, cloud, shape, generator);
    const WeatherMapData map = BuildWeatherMap(
        Stage5WeatherPreset::PeriodicPerlin, generator);
    std::size_t nonZero = 0;
    std::size_t core = 0;
    const std::size_t pixels = map.rgba.size() / 4u;
    for (std::size_t index = 0; index < pixels; ++index)
    {
        const std::uint8_t coverage = map.rgba[index * 4u];
        if (coverage > 1u)
            ++nonZero;
        if (coverage >= 128u)
            ++core;
    }
    return { 100.0 * nonZero / pixels, 100.0 * core / pixels };
}
}

int main()
{
    CloudAppearancePreset decoded = CloudAppearancePreset::CustomUnsaved;
    const CloudAppearancePreset selectable[] = {
        CloudAppearancePreset::DenseMixedDefault,
        CloudAppearancePreset::Stratus,
        CloudAppearancePreset::Cumulus,
        CloudAppearancePreset::Custom,
    };
    for (CloudAppearancePreset expected : selectable)
    {
        decoded = CloudAppearancePreset::CustomUnsaved;
        if (!TryDecodeCloudAppearancePresetRequest(
                static_cast<int>(expected), decoded) || decoded != expected)
            Fail("selectable appearance request did not decode");
    }
    decoded = CloudAppearancePreset::Stratus;
    if (TryDecodeCloudAppearancePresetRequest(-1, decoded) ||
        decoded != CloudAppearancePreset::Stratus ||
        TryDecodeCloudAppearancePresetRequest(
            static_cast<int>(CloudAppearancePreset::CustomUnsaved), decoded) ||
        decoded != CloudAppearancePreset::Stratus ||
        TryDecodeCloudAppearancePresetRequest(99, decoded) ||
        decoded != CloudAppearancePreset::Stratus)
        Fail("invalid appearance request must be rejected without mutation");

    const CloudAppearanceSettings dense = DenseMixedAppearance();
    const CloudAppearanceSettings stratus = StratusAppearance();
    const CloudAppearanceSettings cumulus = CumulusAppearance();
    if (dense.cloudTypeMode != CloudTypeMode::WeatherMap ||
        !Near(dense.globalCoverage, 0.68f) ||
        !Near(dense.densityMultiplier, 1.15f) ||
        !Near(dense.extinctionPerMeter, 0.00035f) ||
        !Near(dense.detailErosion, 0.18f) ||
        !Near(dense.weatherThreshold, 0.50f) ||
        !Near(dense.weatherSoftness, 0.20f) ||
        !Near(dense.densityCoverageLink, 0.45f) ||
        !Near(dense.thicknessCoverageLink, 0.60f) ||
        !Near(dense.cloudTypeBias, 0.08f))
        Fail("Dense Mixed exact contract changed");
    if (stratus.cloudTypeMode != CloudTypeMode::Stratus ||
        !Near(stratus.globalCoverage, 0.40f) ||
        !Near(stratus.stratusMaximumThicknessMeters, 2800.0f) ||
        !Near(stratus.stratusBottomFadeEnd, 0.05f) ||
        !Near(stratus.stratusTopFadeStart, 0.72f) ||
        cumulus.cloudTypeMode != CloudTypeMode::Cumulus ||
        !Near(cumulus.globalCoverage, 0.45f) ||
        !Near(cumulus.densityMultiplier, 1.25f) ||
        !Near(cumulus.cumulusTopFadeStart, 0.94f) ||
        !Near(cumulus.cumulusUpperMassBottom, 0.65f) ||
        !Near(cumulus.cumulusUpperMassStart, 0.08f) ||
        !Near(cumulus.cumulusUpperMassEnd, 0.70f))
        Fail("Stratus/Cumulus exact contract changed");

    const Occupancy denseOccupancy = MeasureOccupancy(dense);
    const Occupancy stratusOccupancy = MeasureOccupancy(stratus);
    const Occupancy cumulusOccupancy = MeasureOccupancy(cumulus);
    std::printf("[13-4E][OCCUPANCY] dense=%.2f/%.2f stratus=%.2f/%.2f cumulus=%.2f/%.2f\n",
                denseOccupancy.nonZero, denseOccupancy.core,
                stratusOccupancy.nonZero, stratusOccupancy.core,
                cumulusOccupancy.nonZero, cumulusOccupancy.core);
    if (denseOccupancy.nonZero < 79.0 || denseOccupancy.nonZero > 81.0 ||
        denseOccupancy.core < 48.0 || denseOccupancy.core > 50.0 ||
        stratusOccupancy.nonZero < 86.0 || stratusOccupancy.nonZero > 89.0 ||
        stratusOccupancy.core < 55.0 || stratusOccupancy.core > 58.0 ||
        cumulusOccupancy.nonZero < 72.0 || cumulusOccupancy.nonZero > 75.0 ||
        cumulusOccupancy.core < 41.0 || cumulusOccupancy.core > 43.0)
        Fail("default Weather occupancy left the approved ranges");

    if (EvaluateAppearanceBaseDensity(0.68f, 0.0f, 1.0f, 1.0f,
                                      1.0f, 1.0f, 1.0f, true) != 0.0f ||
        EvaluateAppearanceBaseDensity(0.68f, 1.0f, 1.0f, 1.0f,
                                      0.0f, 1.0f, 1.0f, true) != 0.0f ||
        EvaluateAppearanceBaseDensity(0.68f, 1.0f, 1.0f, 1.0f,
                                      1.0f, 1.0f, 1.0f, false) != 0.0f)
        Fail("empty Weather/profile/outside layer must have zero density");
    const float quarter = EvaluateAppearanceBaseDensity(
        0.68f, 0.8f, 0.95f, 0.5f, 0.25f, 1.2f, 1.1f, true);
    const float half = EvaluateAppearanceBaseDensity(
        0.68f, 0.8f, 0.95f, 0.5f, 0.50f, 1.2f, 1.1f, true);
    if (!(quarter > 0.0f) || !Near(half, quarter * 2.0f, 1.0e-5f) ||
        !Near(EvaluateAppearanceHorizontalCoverage(0.68f, 0.8f, 0.1f),
              EvaluateAppearanceHorizontalCoverage(0.68f, 0.8f, 0.1f)))
        Fail("vertical profile must multiply density once, not shrink threshold");

    const float lightReference = EvaluateAppearanceLightBaseDensity(
        0.45f, 0.8f, 0.95f, 0.5f, 0.75f, 1.25f, 1.1f, true);
    const float baseReference = EvaluateAppearanceBaseDensity(
        0.45f, 0.8f, 0.95f, 0.5f, 0.75f, 1.25f, 1.1f, true);
    if (!Near(lightReference, baseReference, 1.0e-6f) ||
        EvaluateAppearanceLightBaseDensity(
            0.40f, 0.0f, 0.95f, 0.5f, 0.75f, 1.20f, 1.0f, true) != 0.0f ||
        EvaluateAppearanceLightBaseDensity(
            0.40f, 0.8f, 0.95f, 0.5f, 0.0f, 1.20f, 1.0f, true) != 0.0f ||
        EvaluateAppearanceLightBaseDensity(
            0.45f, 0.8f, 0.95f, 0.5f, 0.75f, 1.25f, 1.0f, false) != 0.0f)
        Fail("Light-only Base Density must match or reject proven empty samples");

    WeatherMapGeneratorSettings generator;
    generator.coverage.seed = 987654u;
    generator.coverage.macroPeriod = 7u;
    CloudParameters cloud;
    cloud.windSpeed = 123.0f;
    cloud.noiseOffset = 19.0f;
    CloudShapeParameters shape;
    ApplyCloudAppearance(cumulus, cloud, shape, generator);
    if (generator.coverage.seed != 987654u ||
        generator.coverage.macroPeriod != 7u ||
        !Near(cloud.windSpeed, 123.0f) || !Near(cloud.noiseOffset, 19.0f))
        Fail("appearance preset must preserve seeds, periods, wind, and offset");
    if (ResolvePipelineComparisonTime(true, 17.5f) != 0.0f ||
        !Near(ResolvePipelineComparisonTime(false, 17.5f), 17.5f))
        Fail("Pipeline Compare must use render-only time zero");

    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        (L"vcloud-appearance-test-" + std::to_wstring(GetCurrentProcessId()));
    const std::filesystem::path file = root / L"custom" / L"noise-settings.json";
    std::string status;
    if (!SaveCustomCloudAppearanceAtomic(file, cumulus, status))
        Fail("valid Custom settings did not save atomically");
    CloudAppearanceSettings loaded = dense;
    if (!LoadCustomCloudAppearance(file, loaded, status) ||
        !CloudAppearanceSettingsEqual(loaded, cumulus))
        Fail("schema 29 Custom round trip changed values");

    CloudAppearanceSettings sentinel = stratus;
    CloudAppearanceSettings missingResult = sentinel;
    if (LoadCustomCloudAppearance(root / L"missing.json", missingResult, status) ||
        !CloudAppearanceSettingsEqual(missingResult, sentinel))
        Fail("missing Custom file must not mutate renderer candidate state");
    {
        std::ofstream corrupt(file, std::ios::binary | std::ios::trunc);
        corrupt << "{\"schemaVersion\":28,\"implementationStage\":\"13-4D\"}";
    }
    CloudAppearanceSettings corruptResult = sentinel;
    if (LoadCustomCloudAppearance(file, corruptResult, status) ||
        !CloudAppearanceSettingsEqual(corruptResult, sentinel))
        Fail("old/corrupt Custom file must be rejected without mutation");

    if (!SaveCustomCloudAppearanceAtomic(file, dense, status))
        Fail("failed to restore valid JSON for range test");
    std::string outOfRangeText;
    {
        std::ifstream input(file, std::ios::binary);
        outOfRangeText.assign(std::istreambuf_iterator<char>(input), {});
    }
    const std::string validCoverage = "\"globalCoverage\": 0.680000";
    const std::size_t coveragePosition = outOfRangeText.find(validCoverage);
    if (coveragePosition == std::string::npos)
        Fail("saved JSON omitted globalCoverage");
    outOfRangeText.replace(coveragePosition, validCoverage.size(),
                           "\"globalCoverage\": 2.000000");
    {
        std::ofstream output(file, std::ios::binary | std::ios::trunc);
        output << outOfRangeText;
    }
    CloudAppearanceSettings rangeResult = sentinel;
    if (LoadCustomCloudAppearance(file, rangeResult, status) ||
        !CloudAppearanceSettingsEqual(rangeResult, sentinel))
        Fail("out-of-range Custom JSON must use fallback without mutation");

    const std::filesystem::path blocker = root / L"blocker";
    {
        std::ofstream output(blocker, std::ios::binary | std::ios::trunc);
        output << "not a directory";
    }
    if (SaveCustomCloudAppearanceAtomic(
            blocker / L"noise-settings.json", dense, status))
        Fail("atomic save failure path must report failure");

    std::error_code error;
    std::filesystem::remove(file, error);
    std::filesystem::remove(file.parent_path(), error);
    std::filesystem::remove(blocker, error);
    std::filesystem::remove(root, error);
    std::puts("CloudAppearance tests passed");
    return 0;
}
