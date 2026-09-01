// ============================================================================
//  main.cpp - High 단일 VolumetricCloud 실행 및 GPU smoke 진입점
// ============================================================================
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <objbase.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "Camera.h"
#include "CloudFormationPresetStore.h"
#include "Renderer.h"
#include "Stage13CameraPresets.h"
#include "Window.h"

namespace
{
void WriteDiagnosticLine(const std::string& line)
{
    const std::string output = line + "\n";
    const HANDLE standardOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    if (standardOutput && standardOutput != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        WriteFile(standardOutput, output.data(),
                  static_cast<DWORD>(output.size()), &written, nullptr);
    }
    OutputDebugStringA(output.c_str());
}

bool HasArgument(const wchar_t* commandLine, const wchar_t* argument)
{
    return commandLine && argument && wcsstr(commandLine, argument) != nullptr;
}

void ApplyCameraPreset(Camera& camera, Stage13CameraPresetId id)
{
    const Stage13CameraPreset& preset = stage13camera::Get(id);
    camera.SetClipPlanes(
        stage13camera::kNearPlaneMeters, stage13camera::kFarPlaneMeters);
    camera.SetFovYDegrees(60.0f);
    camera.SetLookAt(preset.position, preset.target);
    camera.SetDebugName(L"자동 검증 카메라");
}

void RenderFrames(Renderer& renderer, Camera& camera,
                  std::uint32_t count, float& timeSeconds)
{
    for (std::uint32_t frame = 0; frame < count; ++frame)
    {
        renderer.Render(camera, timeSeconds);
        timeSeconds += 1.0f / 60.0f;
    }
}

template <typename T>
bool SameObjectBytes(const T& a, const T& b)
{
    return std::memcmp(&a, &b, sizeof(T)) == 0;
}

double Percentile95(std::vector<double> values)
{
    if (values.empty())
        return 0.0;
    std::sort(values.begin(), values.end());
    const std::size_t index = std::min(
        values.size() - 1,
        static_cast<std::size_t>(std::ceil(values.size() * 0.95) - 1.0));
    return values[index];
}

int RunHighCloudSmoke(Renderer& renderer, Camera& camera)
{
    renderer.SetAutomatedRenderMode(true);
    renderer.SetVSyncEnabled(false);
    renderer.EnableNoiseLabPreviews(false);
    renderer.EnableFrameHashCapture(true);
    float timeSeconds = 17.0f;

    const std::array<Stage15ConceptPreset, 3> concepts = {
        Stage15ConceptPreset::UrbanFairWeather,
        Stage15ConceptPreset::MeadowBrokenClouds,
        Stage15ConceptPreset::SnowOvercast,
    };
    const std::array<Stage13CameraPresetId, 4> cameras = {
        Stage13CameraPresetId::HeroDepth,
        Stage13CameraPresetId::GroundHorizon,
        Stage13CameraPresetId::InsideLayer,
        Stage13CameraPresetId::AboveLayer,
    };

    bool passed = true;
    for (Stage15ConceptPreset concept : concepts)
    {
        passed = renderer.ApplySceneConcept(concept) && passed;
        for (Stage13CameraPresetId cameraPreset : cameras)
        {
            ApplyCameraPreset(camera, cameraPreset);
            RenderFrames(renderer, camera, 4u, timeSeconds);
            const std::uint64_t frameHash = renderer.LastCloudFrameHash();
            std::ostringstream line;
            line << "[HighCloud][concept="
                 << stage15::ConceptName(concept) << "][camera="
                 << stage13camera::Get(cameraPreset).diagnosticName
                 << "] hash=0x" << std::hex << frameHash;
            WriteDiagnosticLine(line.str());
            passed = frameHash != 0u && passed;
        }
    }

    const std::array<CloudFormationType, 3> types = {
        CloudFormationType::Stratus,
        CloudFormationType::Cumulus,
        CloudFormationType::Mixed,
    };
    ApplyCameraPreset(camera, Stage13CameraPresetId::HeroDepth);
    for (CloudFormationType type : types)
    {
        const CloudFormationPresetTarget target = TypeFormationTarget(type);
        const bool applied = renderer.ApplyCloudType(target);
        RenderFrames(renderer, camera, 3u, timeSeconds);
        WriteDiagnosticLine(
            std::string("[HighCloud][type=") +
            CloudFormationPresetTargetName(target) + "] " +
            (applied ? "PASS" : "FAIL"));
        passed = applied && renderer.LastCloudFrameHash() != 0u && passed;
    }

    const std::array<CloudDebugMode, 4> debugModes = {
        CloudDebugMode::FinalDensity,
        CloudDebugMode::Transmittance,
        CloudDebugMode::Stage12NearOpticalDepth,
        CloudDebugMode::Stage12FarOpticalDepth,
    };
    for (CloudDebugMode mode : debugModes)
    {
        renderer.SetDebugMode(mode);
        RenderFrames(renderer, camera, 2u, timeSeconds);
        passed = renderer.LastCloudFrameHash() != 0u && passed;
    }
    renderer.SetDebugMode(CloudDebugMode::Composite);

    Stage14LutValidationResult lutResult;
    passed = renderer.ValidateStage14Luts(lutResult) &&
        lutResult.finiteNonNegative && passed;
    passed = !renderer.HasDebugLayerErrors() && passed;
    WriteDiagnosticLine(std::string("HIGH_CLOUD_SMOKE=") +
                        (passed ? "PASS" : "FAIL"));
    return passed ? 0 : 1;
}

int RunFormationSmoke(Renderer& renderer, Camera& camera)
{
    renderer.SetAutomatedRenderMode(true);
    renderer.SetVSyncEnabled(false);
    float timeSeconds = 31.0f;
    bool passed = true;
    for (std::uint32_t index = 0; index < 3u; ++index)
    {
        const CloudFormationPresetTarget target = TypeFormationTarget(
            static_cast<CloudFormationType>(index));
        passed = renderer.ApplyCloudType(target) && passed;
        const CloudFormationSettings formation =
            renderer.CurrentCloudFormation();
        PreparedCloudFormation prepared;
        std::string status;
        passed = PrepareCloudFormationSettings(
            formation, prepared, status, 200.0f) && passed;
        RenderFrames(renderer, camera, 2u, timeSeconds);
    }

    const std::filesystem::path customRoot =
        std::filesystem::temp_directory_path() /
        ("vcloud-custom-smoke-" + std::to_string(GetCurrentProcessId()));
    std::error_code fileError;
    std::filesystem::create_directories(customRoot, fileError);
    renderer.SetCloudFormationPresetRootForValidation(customRoot);
    const CloudFormationSettings savedFormation =
        renderer.CurrentCloudFormation();
    passed = !fileError && renderer.SaveCustomFormation() && passed;
    passed = renderer.ApplySceneConcept(
        Stage15ConceptPreset::SnowOvercast) && passed;
    const LightParameters sceneLight = renderer.LightSettings();
    const EnvironmentParameters sceneEnvironment =
        renderer.EnvironmentSettings();
    const AtmosphereParameters sceneAtmosphere =
        renderer.AtmosphereSettings();
    const GroundLightingParameters sceneGround =
        renderer.GroundLightingSettings();
    const ToneMappingParameters sceneTone = renderer.ToneMappingSettings();
    const Stage12ShadowParameters sceneShadow = renderer.ShadowSettings();
    passed = renderer.LoadCustomFormation() &&
        CloudFormationSettingsEqual(
            savedFormation, renderer.CurrentCloudFormation()) &&
        renderer.Stage15Concept() == Stage15ConceptPreset::SnowOvercast &&
        SameObjectBytes(sceneLight, renderer.LightSettings()) &&
        SameObjectBytes(sceneEnvironment, renderer.EnvironmentSettings()) &&
        SameObjectBytes(sceneAtmosphere, renderer.AtmosphereSettings()) &&
        SameObjectBytes(sceneGround, renderer.GroundLightingSettings()) &&
        SameObjectBytes(sceneTone, renderer.ToneMappingSettings()) &&
        sceneShadow.surfaceShadowEnabled ==
            renderer.ShadowSettings().surfaceShadowEnabled &&
        sceneShadow.surfaceShadowStrength ==
            renderer.ShadowSettings().surfaceShadowStrength &&
        sceneShadow.surfaceAmbientFloor ==
            renderer.ShadowSettings().surfaceAmbientFloor && passed;
    std::filesystem::remove_all(customRoot, fileError);
    passed = !fileError && passed;
    WriteDiagnosticLine(std::string("FORMATION_SMOKE=") +
                        (passed ? "PASS" : "FAIL"));
    return passed ? 0 : 1;
}

int RunAtmosphereSmoke(Renderer& renderer, Camera& camera)
{
    renderer.SetAutomatedRenderMode(true);
    renderer.SetVSyncEnabled(false);
    float timeSeconds = 43.0f;
    RenderFrames(renderer, camera, 6u, timeSeconds);
    Stage14LutValidationResult result;
    const bool passed = renderer.ValidateStage14Luts(result) &&
        result.finiteNonNegative && !renderer.HasDebugLayerErrors();
    std::ostringstream line;
    line << std::fixed << std::setprecision(8)
         << "ATMOSPHERE_LUT_SMOKE=" << (passed ? "PASS" : "FAIL")
         << " MAE=" << result.transmittanceMae
         << " P99=" << result.transmittanceP99;
    WriteDiagnosticLine(line.str());
    return passed ? 0 : 1;
}

int RunNoiseLabSmoke(Renderer& renderer, Camera& camera)
{
    const bool defaultVSyncEnabled = renderer.VSyncEnabled();
    renderer.SetAutomatedRenderMode(false);
    renderer.SetVSyncEnabled(false);
    renderer.SetNoiseLabVisible(true);
    renderer.EnableNoiseLabPreviews(true);
    float timeSeconds = 59.0f;
    renderer.SetCloudRuntimeForValidation(12.0f, 0.0f);
    RenderFrames(renderer, camera, 2u, timeSeconds);
    const bool stoppedTimeStable =
        renderer.CloudMovementSpeedForValidation() == 0.0f &&
        std::abs(renderer.CloudTimeForValidation() - 12.0f) <= 1.0e-6f;
    renderer.SetCloudRuntimeForValidation(12.0f, 2.0f);
    RenderFrames(renderer, camera, 2u, timeSeconds);
    const bool movementTimeAdvanced =
        renderer.CloudMovementSpeedForValidation() == 2.0f &&
        renderer.CloudTimeForValidation() > 12.0f;
    bool passed = defaultVSyncEnabled && stoppedTimeStable &&
        movementTimeAdvanced && renderer.ValidateNoiseLabUiContracts() &&
        renderer.ValidateNoiseLabPreviews() &&
        renderer.NoiseLabPreviewHash(0) != 0u &&
        renderer.NoiseLabPreviewHash(1) != 0u &&
        renderer.NoiseLabPreviewHash(2) != 0u;

    const std::filesystem::path snapshotRoot =
        std::filesystem::temp_directory_path() /
        ("vcloud-schema39-smoke-" +
         std::to_string(GetCurrentProcessId()));
    std::error_code fileError;
    std::filesystem::remove_all(snapshotRoot, fileError);
    fileError.clear();
    const bool exported = renderer.ExportNoiseLabSnapshot(snapshotRoot);
    std::filesystem::path metadataPath;
    if (exported)
    {
        for (std::filesystem::recursive_directory_iterator iterator(
                 snapshotRoot, fileError), end;
             !fileError && iterator != end; iterator.increment(fileError))
        {
            if (iterator->is_regular_file(fileError) && !fileError &&
                iterator->path().filename() == L"noise-settings.json")
            {
                metadataPath = iterator->path();
                break;
            }
        }
    }
    std::string metadata;
    if (!metadataPath.empty())
    {
        std::ifstream input(metadataPath, std::ios::binary);
        metadata.assign(std::istreambuf_iterator<char>(input),
                        std::istreambuf_iterator<char>());
    }
    const bool schema39 = exported && !fileError &&
        metadata.find("\"schemaVersion\": 39") != std::string::npos &&
        metadata.find("HighFullResolutionDirect") != std::string::npos &&
        metadata.find("\"weather\"") != std::string::npos &&
        metadata.find("\"shape\"") != std::string::npos &&
        metadata.find("\"noiseVolumes\"") != std::string::npos &&
        metadata.find(std::string("tempo") + "ral") == std::string::npos &&
        metadata.find(std::string("cir") + "rus") == std::string::npos &&
        metadata.find(std::string("upsam") + "pling") == std::string::npos;
    fileError.clear();
    std::filesystem::remove_all(snapshotRoot, fileError);
    passed = schema39 && !fileError && passed;
    std::ostringstream line;
    line << "NOISE_LAB_SMOKE=" << (passed ? "PASS" : "FAIL")
         << " default_vsync=" << (defaultVSyncEnabled ? "on" : "off")
         << " tearing=" << (renderer.TearingSupported() ? "supported" : "unavailable")
         << " zero_speed=" << (stoppedTimeStable ? "stable" : "changed")
         << " movement_speed="
         << (movementTimeAdvanced ? "advanced" : "stalled");
    WriteDiagnosticLine(line.str());
    return passed ? 0 : 1;
}

int RunShaderCacheSmoke(Renderer& renderer)
{
    const bool passed = renderer.ValidateWarmShaderCache();
    std::ostringstream line;
    line << "SHADER_CACHE_SMOKE=" << (passed ? "PASS" : "FAIL")
         << " compile_calls=" << renderer.ShaderCompileCallCount()
         << " cache_hits=" << renderer.ShaderCacheHitCount();
    WriteDiagnosticLine(line.str());
    return passed ? 0 : 1;
}

bool ReadTextFile(const std::filesystem::path& path, std::string& contents)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return false;
    contents.assign(std::istreambuf_iterator<char>(stream),
                    std::istreambuf_iterator<char>());
    return stream.good() || stream.eof();
}

bool WriteVersionedText(
    const std::filesystem::path& path, const std::string& contents,
    std::filesystem::file_time_type timestamp)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream)
        return false;
    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    stream.close();
    if (!stream)
        return false;
    std::error_code error;
    std::filesystem::last_write_time(path, timestamp, error);
    return !error;
}

bool ReplaceExactlyOnce(std::string& text, const std::string& from,
                        const std::string& to)
{
    const std::size_t first = text.find(from);
    if (first == std::string::npos ||
        text.find(from, first + from.size()) != std::string::npos)
        return false;
    text.replace(first, from.size(), to);
    return true;
}

void WriteReloadReport(const char* label,
                       const shaderreload::ReloadReport& report)
{
    std::ostringstream line;
    line << label
         << " success=" << (report.succeeded ? "true" : "false")
         << " files=" << report.changedFiles.size()
         << " programs=" << report.affectedPrograms
         << " compile=" << report.compileCount
         << " cache=" << report.cacheHits
         << " elapsed_ms=" << std::fixed << std::setprecision(2)
         << report.elapsedMilliseconds
         << " changed=";
    for (std::size_t index = 0; index < report.changedFiles.size(); ++index)
    {
        if (index > 0)
            line << ',';
        line << report.changedFiles[index];
    }
    WriteDiagnosticLine(line.str());
}

bool ReloadReportMatches(const shaderreload::ReloadReport& report,
                         const char* expectedFile,
                         std::size_t expectedPrograms)
{
    return report.attempted &&
        report.changedFiles.size() == 1u &&
        report.changedFiles.front() == expectedFile &&
        report.affectedPrograms == expectedPrograms &&
        report.compileCount + report.cacheHits == expectedPrograms;
}

bool IsMarkedReloadSmokeRoot(const std::filesystem::path& root)
{
    std::error_code error;
    return std::filesystem::is_regular_file(
        root / ".vcloud-shader-smoke-root", error) && !error;
}

int RunToneReloadSmoke(Renderer& renderer)
{
    const std::filesystem::path root =
        renderer.ShaderDirectoryForValidation();
    const std::filesystem::path shader = root / "Stage14ToneMap.hlsl";
    if (!IsMarkedReloadSmokeRoot(root))
    {
        WriteDiagnosticLine(
            "HOT_RELOAD_TONE=FAIL unmarked shader override directory");
        return 1;
    }

    std::string original;
    std::error_code timeError;
    const auto originalTime = std::filesystem::last_write_time(
        shader, timeError);
    if (timeError || !ReadTextFile(shader, original))
    {
        WriteDiagnosticLine("HOT_RELOAD_TONE=FAIL unable to read shader");
        return 1;
    }
    std::string changed = original;
    if (!ReplaceExactlyOnce(
            changed, "#define VCLOUD_TONE_TEST_BIAS 0.0",
            "#define VCLOUD_TONE_TEST_BIAS 0.000123"))
    {
        WriteDiagnosticLine("HOT_RELOAD_TONE=FAIL test hook missing");
        return 1;
    }

    const auto started = std::chrono::steady_clock::now();
    bool passed = true;
    const std::uint64_t initialGeneration = renderer.ShaderGeneration();
    const std::uint64_t initialObjects = renderer.ShaderObjectIdentityHash();
    const bool changedWritten = WriteVersionedText(
        shader, changed, originalTime + std::chrono::seconds(1));
    const bool changedReloaded = changedWritten &&
        renderer.ForceShaderReloadScan();
    const shaderreload::ReloadReport changedReport =
        renderer.LastShaderReloadReport();
    WriteReloadReport("[HotReload][ToneChanged]", changedReport);
    passed = changedReloaded && changedReport.succeeded &&
        ReloadReportMatches(changedReport, "stage14tonemap.hlsl", 1u) &&
        renderer.ShaderGeneration() == initialGeneration + 1u &&
        renderer.ShaderObjectIdentityHash() != initialObjects && passed;

    const std::uint64_t acceptedGeneration = renderer.ShaderGeneration();
    const std::uint64_t acceptedObjects = renderer.ShaderObjectIdentityHash();
    const std::string invalid = changed +
        "\n#error VCLOUD_FORCED_COMPILE_ERROR\n";
    const bool invalidWritten = WriteVersionedText(
        shader, invalid, originalTime + std::chrono::seconds(2));
    const bool invalidReloaded = invalidWritten &&
        renderer.ForceShaderReloadScan();
    const shaderreload::ReloadReport invalidReport =
        renderer.LastShaderReloadReport();
    WriteReloadReport("[HotReload][ToneInvalid]", invalidReport);
    passed = invalidWritten && !invalidReloaded &&
        !invalidReport.succeeded &&
        ReloadReportMatches(invalidReport, "stage14tonemap.hlsl", 1u) &&
        renderer.ShaderGeneration() == acceptedGeneration &&
        renderer.ShaderObjectIdentityHash() == acceptedObjects && passed;

    const bool originalWritten = WriteVersionedText(
        shader, original, originalTime + std::chrono::seconds(3));
    const bool originalReloaded = originalWritten &&
        renderer.ForceShaderReloadScan();
    const shaderreload::ReloadReport restoreReport =
        renderer.LastShaderReloadReport();
    WriteReloadReport("[HotReload][ToneRestored]", restoreReport);
    passed = originalReloaded && restoreReport.succeeded &&
        ReloadReportMatches(restoreReport, "stage14tonemap.hlsl", 1u) &&
        restoreReport.compileCount == 0u && restoreReport.cacheHits == 1u &&
        renderer.ShaderGeneration() == acceptedGeneration + 1u && passed;

    const double elapsedMilliseconds =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
    passed = elapsedMilliseconds <= 15000.0 && passed;
    std::ostringstream result;
    result << "HOT_RELOAD_TONE=" << (passed ? "PASS" : "FAIL")
           << " elapsed_ms=" << std::fixed << std::setprecision(2)
           << elapsedMilliseconds;
    WriteDiagnosticLine(result.str());
    return passed ? 0 : 1;
}

int RunNoiseDependencyReloadSmoke(Renderer& renderer)
{
    const std::filesystem::path root =
        renderer.ShaderDirectoryForValidation();
    const std::filesystem::path shader = root / "Noise.hlsli";
    if (!IsMarkedReloadSmokeRoot(root))
    {
        WriteDiagnosticLine(
            "HOT_RELOAD_NOISE=FAIL unmarked shader override directory");
        return 1;
    }

    std::string original;
    std::error_code timeError;
    const auto originalTime = std::filesystem::last_write_time(
        shader, timeError);
    if (timeError || !ReadTextFile(shader, original))
    {
        WriteDiagnosticLine("HOT_RELOAD_NOISE=FAIL unable to read shader");
        return 1;
    }
    std::string changed = original;
    if (!ReplaceExactlyOnce(
            changed, "#define VCLOUD_NOISE_TEST_BIAS 0.0",
            "#define VCLOUD_NOISE_TEST_BIAS 0.000123"))
    {
        WriteDiagnosticLine("HOT_RELOAD_NOISE=FAIL test hook missing");
        return 1;
    }

    const auto started = std::chrono::steady_clock::now();
    bool passed = true;
    const std::uint64_t initialGeneration = renderer.ShaderGeneration();
    const std::uint64_t initialObjects = renderer.ShaderObjectIdentityHash();
    const std::uint64_t initialCloudHash = renderer.CloudShaderHash();
    const std::uint64_t initialShadowHash = renderer.DeepShadowShaderHash();
    const bool changedWritten = WriteVersionedText(
        shader, changed, originalTime + std::chrono::seconds(1));
    const bool changedReloaded = changedWritten &&
        renderer.ForceShaderReloadScan();
    const shaderreload::ReloadReport changedReport =
        renderer.LastShaderReloadReport();
    WriteReloadReport("[HotReload][NoiseChanged]", changedReport);
    passed = changedReloaded && changedReport.succeeded &&
        ReloadReportMatches(changedReport, "noise.hlsli", 3u) &&
        renderer.ShaderGeneration() == initialGeneration + 1u &&
        renderer.ShaderObjectIdentityHash() != initialObjects &&
        renderer.CloudShaderHash() != initialCloudHash &&
        renderer.DeepShadowShaderHash() != initialShadowHash && passed;

    const bool originalWritten = WriteVersionedText(
        shader, original, originalTime + std::chrono::seconds(2));
    const bool originalReloaded = originalWritten &&
        renderer.ForceShaderReloadScan();
    const shaderreload::ReloadReport restoreReport =
        renderer.LastShaderReloadReport();
    WriteReloadReport("[HotReload][NoiseRestored]", restoreReport);
    passed = originalReloaded && restoreReport.succeeded &&
        ReloadReportMatches(restoreReport, "noise.hlsli", 3u) &&
        restoreReport.compileCount == 0u && restoreReport.cacheHits == 3u &&
        renderer.ShaderGeneration() == initialGeneration + 2u &&
        renderer.CloudShaderHash() == initialCloudHash &&
        renderer.DeepShadowShaderHash() == initialShadowHash && passed;

    const double elapsedMilliseconds =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
    passed = elapsedMilliseconds <= 30000.0 && passed;
    std::ostringstream result;
    result << "HOT_RELOAD_NOISE=" << (passed ? "PASS" : "FAIL")
           << " elapsed_ms=" << std::fixed << std::setprecision(2)
           << elapsedMilliseconds;
    WriteDiagnosticLine(result.str());
    return passed ? 0 : 1;
}

int RunPerformanceTest(Renderer& renderer, Camera& camera)
{
    renderer.SetAutomatedRenderMode(true);
    renderer.SetVSyncEnabled(false);
    renderer.EnableNoiseLabPreviews(false);
    float timeSeconds = 71.0f;
    std::vector<double> cloudSamples;
    std::vector<double> frameSamples;
    bool passed = true;

    const std::array<Stage15ConceptPreset, 3> concepts = {
        Stage15ConceptPreset::UrbanFairWeather,
        Stage15ConceptPreset::MeadowBrokenClouds,
        Stage15ConceptPreset::SnowOvercast,
    };
    const std::array<Stage13CameraPresetId, 4> cameras = {
        Stage13CameraPresetId::HeroDepth,
        Stage13CameraPresetId::GroundHorizon,
        Stage13CameraPresetId::InsideLayer,
        Stage13CameraPresetId::AboveLayer,
    };

    for (Stage15ConceptPreset concept : concepts)
    {
        if (!renderer.ApplySceneConcept(concept))
            return 1;
        for (Stage13CameraPresetId cameraPreset : cameras)
        {
            ApplyCameraPreset(camera, cameraPreset);
            RenderFrames(renderer, camera, 60u, timeSeconds);
            std::vector<double> caseCloudSamples;
            std::vector<double> caseFrameSamples;
            for (std::uint32_t frame = 0;
                 frame < 180u && caseCloudSamples.size() < 120u; ++frame)
            {
                renderer.Render(camera, timeSeconds);
                timeSeconds += 1.0f / 60.0f;
                const FrameTimingSnapshot& timing = renderer.TimingSnapshot();
                if (timing.gpuValid && timing.rawGpuFrameMs > 0.0)
                {
                    caseCloudSamples.push_back(timing.rawGpuCloudMs);
                    caseFrameSamples.push_back(timing.rawGpuFrameMs);
                    cloudSamples.push_back(timing.rawGpuCloudMs);
                    frameSamples.push_back(timing.rawGpuFrameMs);
                }
            }
            const double caseCloudP95 = Percentile95(caseCloudSamples);
            const double caseFrameP95 = Percentile95(caseFrameSamples);
            const bool casePassed = caseCloudSamples.size() == 120u &&
                caseCloudP95 <= 10.0 && caseFrameP95 <= 16.67;
            passed = casePassed && passed;
            std::ostringstream caseLine;
            caseLine << std::fixed << std::setprecision(3)
                     << "[HighPerformance][concept="
                     << stage15::ConceptName(concept) << "][camera="
                     << stage13camera::Get(cameraPreset).diagnosticName
                     << "] " << (casePassed ? "PASS" : "FAIL")
                     << " samples=" << caseCloudSamples.size()
                     << " cloud_p95_ms=" << caseCloudP95
                     << " frame_p95_ms=" << caseFrameP95;
            WriteDiagnosticLine(caseLine.str());
        }
    }

    const double cloudP95 = Percentile95(cloudSamples);
    const double frameP95 = Percentile95(frameSamples);
    passed = cloudSamples.size() == 1440u &&
        cloudP95 <= 10.0 && frameP95 <= 16.67 &&
        !renderer.HasDebugLayerErrors() && passed;
    std::ostringstream line;
    line << std::fixed << std::setprecision(3)
         << "HIGH_PERFORMANCE=" << (passed ? "PASS" : "FAIL")
         << " adapter=\"" << renderer.AdapterName() << "\""
         << " driver=" << renderer.DriverVersion()
         << " samples=" << cloudSamples.size()
         << " cloud_p95_ms=" << cloudP95
         << " frame_p95_ms=" << frameP95;
    WriteDiagnosticLine(line.str());
    return passed ? 0 : 1;
}
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR commandLine, int)
{
    const bool highSmoke = HasArgument(
        commandLine, L"--high-cloud-smoke-test");
    const bool formationSmoke = HasArgument(
        commandLine, L"--formation-smoke-test");
    const bool atmosphereSmoke = HasArgument(
        commandLine, L"--atmosphere-smoke-test");
    const bool noiseLabSmoke = HasArgument(
        commandLine, L"--noise-lab-smoke-test");
    const bool shaderCacheSmoke = HasArgument(
        commandLine, L"--shader-cache-smoke-test");
    const bool toneReloadSmoke = HasArgument(
        commandLine, L"--hot-reload-smoke-test");
    const bool noiseReloadSmoke = HasArgument(
        commandLine, L"--hot-reload-dependency-smoke-test");
    const bool performanceTest = HasArgument(
        commandLine, L"--high-performance-test");
    const bool automated = highSmoke || formationSmoke || atmosphereSmoke ||
        noiseLabSmoke || shaderCacheSmoke || toneReloadSmoke ||
        noiseReloadSmoke || performanceTest;

    const int initialWidth = performanceTest ? 1920 :
        (highSmoke ? 320 : (automated ? 640 : 1280));
    const int initialHeight = performanceTest ? 1080 :
        (highSmoke ? 180 : (automated ? 360 : 720));

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Window window(
        hInstance, initialWidth, initialHeight,
        L"VolumetricCloud - High", !automated, !automated);
    if (!window.GetHandle())
    {
        CoUninitialize();
        return 1;
    }

    Camera camera;
    camera.SetAspect(static_cast<float>(window.GetWidth()) /
                     static_cast<float>(window.GetHeight()));
    ApplyCameraPreset(camera, Stage13CameraPresetId::HeroDepth);

    Renderer renderer;
    const bool needsNoiseVolumes = !toneReloadSmoke && !noiseReloadSmoke;
    if (!renderer.Init(
            window.GetHandle(), window.GetWidth(), window.GetHeight(),
            needsNoiseVolumes,
            !automated))
    {
        WriteDiagnosticLine(
            std::string("RENDERER_INIT=FAIL status=") +
            renderer.ShaderStatus() + " error=" + renderer.ShaderError());
        CoUninitialize();
        return 1;
    }
    if (!renderer.ApplyStage15Defaults())
    {
        WriteDiagnosticLine("STAGE15_DEFAULTS=FAIL");
        CoUninitialize();
        return 1;
    }
    window.SetCamera(&camera);
    window.SetRenderer(&renderer);
    if (!automated)
        window.ApplyInitialPortfolioCamera();

    int result = 0;
    if (highSmoke)
        result = RunHighCloudSmoke(renderer, camera);
    else if (formationSmoke)
        result = RunFormationSmoke(renderer, camera);
    else if (atmosphereSmoke)
        result = RunAtmosphereSmoke(renderer, camera);
    else if (noiseLabSmoke)
        result = RunNoiseLabSmoke(renderer, camera);
    else if (shaderCacheSmoke)
        result = RunShaderCacheSmoke(renderer);
    else if (toneReloadSmoke)
        result = RunToneReloadSmoke(renderer);
    else if (noiseReloadSmoke)
        result = RunNoiseDependencyReloadSmoke(renderer);
    else if (performanceTest)
        result = RunPerformanceTest(renderer, camera);
    else
    {
        LARGE_INTEGER frequency = {};
        LARGE_INTEGER previous = {};
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&previous);
        float timeSeconds = 0.0f;
        while (window.ProcessMessages())
        {
            LARGE_INTEGER current = {};
            QueryPerformanceCounter(&current);
            const float deltaSeconds = static_cast<float>(
                static_cast<double>(current.QuadPart - previous.QuadPart) /
                static_cast<double>(frequency.QuadPart));
            previous = current;
            window.UpdateCameraMovement(deltaSeconds);
            timeSeconds += std::clamp(deltaSeconds, 0.0f, 0.25f);
            renderer.Render(camera, timeSeconds);
        }
    }

    CoUninitialize();
    return result;
}
