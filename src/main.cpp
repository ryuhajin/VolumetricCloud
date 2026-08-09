// ============================================================================
//  main.cpp  —  진입점 (WinMain)
// ----------------------------------------------------------------------------
//  창(Window) · 카메라(Camera) · 렌더러(Renderer)를 생성·연결하고,
//  메인 루프에서 진단 장면과 단계 8 환경광·다중 산란 구름 패스를 그린다.
// ============================================================================
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <objbase.h>

#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "Window.h"
#include "Camera.h"
#include "Renderer.h"
#include "Stage9OptimizationMath.h"

namespace
{
bool WriteTextFile(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    return output.good();
}

bool ReadTextFile(const std::filesystem::path& path, std::string& text)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return false;
    text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return input.good() || input.eof();
}

std::wstring CommandOption(const wchar_t* commandLine, const wchar_t* option)
{
    if (!commandLine) return {};
    std::wstring line(commandLine), key(option);
    const auto begin = line.find(key);
    if (begin == std::wstring::npos) return {};
    auto valueBegin = line.find_first_not_of(L" \t", begin + key.size());
    if (valueBegin == std::wstring::npos) return {};
    if (line[valueBegin] == L'\"')
    {
        const auto end = line.find(L'\"', ++valueBegin);
        return line.substr(valueBegin, end - valueBegin);
    }
    const auto end = line.find_first_of(L" \t", valueBegin);
    return line.substr(valueBegin, end - valueBegin);
}
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR commandLine, int)
{
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    struct ComScope
    {
        HRESULT result;
        ~ComScope() { if (SUCCEEDED(result)) CoUninitialize(); }
    } comScope{ comResult };

    // Stage6Smoke는 중첩 View/Light Ray를 확인하므로 D3D 경로 검증에 충분한 작은 타깃을 쓴다.
    // 일반 실행과 이전 단계 smoke의 해상도·프레임 해시는 바꾸지 않는다.
    const bool requestedStage6Smoke = commandLine &&
        wcsstr(commandLine, L"--stage6-smoke-test") != nullptr;
    const bool requestedStage7Smoke = commandLine &&
        wcsstr(commandLine, L"--stage7-smoke-test") != nullptr;
    const bool requestedStage8Smoke = commandLine &&
        wcsstr(commandLine, L"--stage8-smoke-test") != nullptr;
    const bool requestedStage9Smoke = commandLine &&
        wcsstr(commandLine, L"--stage9-smoke-test") != nullptr;
    const bool requestedStage13Smoke = commandLine &&
        wcsstr(commandLine, L"--stage13-smoke-test") != nullptr;
    const bool stage9Benchmark = commandLine &&
        wcsstr(commandLine, L"--stage9-benchmark") != nullptr;
    const bool requestedPerformanceOverlaySmoke = commandLine &&
        wcsstr(commandLine, L"--performance-overlay-smoke-test") != nullptr;
    const bool requestedSmallGpuSmoke = requestedStage6Smoke || requestedStage7Smoke ||
        requestedStage8Smoke || requestedStage9Smoke || requestedStage13Smoke ||
        requestedPerformanceOverlaySmoke;
    const int kWidth  = stage9Benchmark ? 1920 : (requestedSmallGpuSmoke ? 96 : 1280);
    const int kHeight = stage9Benchmark ? 1080 : (requestedSmallGpuSmoke ? 54 : 720);

    const std::wstring shaderRoot = CommandOption(commandLine, L"--shader-root");
    if (!shaderRoot.empty())
        SetEnvironmentVariableW(L"VCLOUD_SHADER_OVERRIDE_DIR", shaderRoot.c_str());

    const bool smokeTest = commandLine &&
        wcsstr(commandLine, L"--foundation-smoke-test") != nullptr;
    const bool stage1SmokeTest = commandLine &&
        wcsstr(commandLine, L"--stage1-smoke-test") != nullptr;
    const bool stage2SmokeTest = commandLine &&
        wcsstr(commandLine, L"--stage2-smoke-test") != nullptr;
    const bool stage3SmokeTest = commandLine &&
        wcsstr(commandLine, L"--stage3-smoke-test") != nullptr;
    const bool stage4SmokeTest = commandLine &&
        wcsstr(commandLine, L"--stage4-smoke-test") != nullptr;
    const bool stage5SmokeTest = commandLine &&
        wcsstr(commandLine, L"--stage5-smoke-test") != nullptr;
    const bool stage6SmokeTest = requestedStage6Smoke;
    const bool stage7SmokeTest = requestedStage7Smoke;
    const bool stage8SmokeTest = requestedStage8Smoke;
    const bool stage9SmokeTest = requestedStage9Smoke;
    const bool stage13SmokeTest = requestedStage13Smoke;
    const bool performanceOverlaySmokeTest = requestedPerformanceOverlaySmoke;
    const bool noiseLabSmokeTest = commandLine &&
        wcsstr(commandLine, L"--noise-lab-smoke-test") != nullptr;
    const bool shaderHotReloadSmokeTest = commandLine &&
        wcsstr(commandLine, L"--shader-hot-reload-smoke-test") != nullptr;

    std::filesystem::path hotReloadShaderDirectory;
    if (shaderHotReloadSmokeTest)
    {
        hotReloadShaderDirectory = std::filesystem::temp_directory_path() /
            (L"VolumetricCloudHotReload-" + std::to_wstring(GetCurrentProcessId()));
        std::error_code error;
        std::filesystem::create_directories(hotReloadShaderDirectory, error);
        std::filesystem::copy(
            std::filesystem::path(VCLOUD_SHADER_SOURCE_DIR), hotReloadShaderDirectory,
            std::filesystem::copy_options::recursive |
            std::filesystem::copy_options::overwrite_existing, error);
        if (error || !SetEnvironmentVariableW(
                L"VCLOUD_SHADER_OVERRIDE_DIR", hotReloadShaderDirectory.c_str()))
            return 5;
    }

    // ---- 객체 생성 ----
    Window   window(hInstance, kWidth, kHeight,
                    L"VolumetricCloud - Stage 13 | 0 합성 | Q 1.5-4.5km 평면층 | N 기본 Noise | F10 기본 Detail | F3 Periodic Weather | Balanced Light/Ambient | Stage 9 보류 | 지상 상향(F5)",
                    !smokeTest && !stage1SmokeTest && !stage2SmokeTest &&
                    !stage3SmokeTest && !stage4SmokeTest && !stage5SmokeTest &&
                    !stage6SmokeTest && !stage7SmokeTest && !stage8SmokeTest &&
                    !stage9SmokeTest &&
                    !stage13SmokeTest &&
                    !performanceOverlaySmokeTest &&
                    !noiseLabSmokeTest && !shaderHotReloadSmokeTest && !stage9Benchmark);
    Camera   camera;
    Renderer renderer;

    camera.SetAspect(static_cast<float>(kWidth) / kHeight);
    camera.SetOrbit(0.0f, -0.73f, 3000.0f, { 0.0f, 2000.0f, 0.0f });

    if (!renderer.Init(window.GetHandle(), kWidth, kHeight))
        return -1; // 초기화 실패 (오류 메시지는 Renderer가 표시)

    // 숨김 자동 검증은 D3D 분기 확인이 목적이다. 합성 모드를 사용하는 Foundation,
    // Noise Lab, Hot Reload smoke가 단계 6의 기본 128×16 중첩 비용을 그대로 쓰지
    // 않도록 낮은 표본 수를 적용한다. 일반 사용자 실행에는 영향을 주지 않는다.
    if (smokeTest || stage1SmokeTest || stage2SmokeTest || stage3SmokeTest ||
        stage4SmokeTest || stage5SmokeTest || stage6SmokeTest || stage7SmokeTest ||
        stage8SmokeTest || stage9SmokeTest || stage13SmokeTest ||
        performanceOverlaySmokeTest ||
        noiseLabSmokeTest || shaderHotReloadSmokeTest)
    {
        renderer.SetViewSamplingForSmoke(16u, 250.0f);
        renderer.SetLightSampling(4u, 500.0f);
    }

    // 입력/리사이즈 연결
    window.SetCamera(&camera);
    window.SetRenderer(&renderer);

    if (stage9Benchmark)
    {
        renderer.SetNoiseLabVisible(false);
        renderer.EnableNoiseLabPreviews(false);
        renderer.EnableFrameHashCapture(false);
        renderer.SetVSyncEnabled(false);
        renderer.SetDebugMode(CloudDebugMode::Composite);
        renderer.SetViewSamplingForSmoke(256u, 100.0f);
        renderer.SetLightSampling(32u, 250.0f);
        renderer.ApplyStage4DetailPreset(Stage4DetailPreset::DefaultDetail);
        renderer.ApplyStage6SunPreset(Stage6SunPreset::Noon);
        renderer.ApplyStage7PhasePreset(Stage7PhasePreset::Balanced);
        renderer.ApplyStage8EnvironmentPreset(Stage8EnvironmentPreset::Balanced);

        const std::wstring optimizationOption = CommandOption(commandLine, L"--optimization");
        Stage9OptimizationPreset benchmarkPreset = Stage9OptimizationPreset::Balanced;
        const char* benchmarkPresetName = "Balanced";
        if (optimizationOption == L"off")
        {
            benchmarkPreset = Stage9OptimizationPreset::Off;
            benchmarkPresetName = "Off";
        }
        else if (optimizationOption == L"early-exit")
        {
            benchmarkPreset = Stage9OptimizationPreset::EarlyExitOnly;
            benchmarkPresetName = "EarlyExitOnly";
        }
        else if (optimizationOption == L"empty-space")
        {
            benchmarkPreset = Stage9OptimizationPreset::EmptySpaceOnly;
            benchmarkPresetName = "EmptySpaceOnly";
        }
        renderer.ApplyStage9OptimizationPreset(benchmarkPreset);

        std::filesystem::path outputRoot = CommandOption(commandLine, L"--output");
        if (outputRoot.empty()) outputRoot = L"captures/performance/stage9/current";
        std::error_code directoryError;
        std::filesystem::create_directories(outputRoot, directoryError);
        if (directoryError) return 20;
        std::ofstream raw(outputRoot / L"raw.csv", std::ios::trunc);
        raw << "scenario,repeat,sample,gpu_cloud_ms,gpu_frame_ms,cpu_frame_ms\n";
        std::ostringstream summary;
        summary << std::fixed << std::setprecision(6)
                << "{\n  \"schemaVersion\": 1,\n  \"optimization\": \""
                << benchmarkPresetName << "\",\n  \"scenarios\": [\n";

        enum class BenchmarkView { GroundZenith, GroundHorizon, InsideLayer };
        struct Scenario { const char* name; Stage5WeatherPreset weather; Stage2NoisePreset noise; BenchmarkView view; };
        const Scenario scenarios[] = {
            {"GroundZenithDense", Stage5WeatherPreset::UniformLegacy, Stage2NoisePreset::DenseCoverage, BenchmarkView::GroundZenith},
            {"GroundHorizonDense", Stage5WeatherPreset::UniformLegacy, Stage2NoisePreset::DenseCoverage, BenchmarkView::GroundHorizon},
            {"SparseHorizon", Stage5WeatherPreset::PeriodicPerlin, Stage2NoisePreset::SparseCoverage, BenchmarkView::GroundHorizon},
            {"DepthOccluded", Stage5WeatherPreset::ChannelDebug, Stage2NoisePreset::DefaultNoise, BenchmarkView::GroundZenith},
            {"InsideLayer", Stage5WeatherPreset::UniformLegacy, Stage2NoisePreset::DenseCoverage, BenchmarkView::InsideLayer}};
        const std::wstring selectedScenario = CommandOption(commandLine, L"--scenario");
        bool firstScenario = true;
        for (const Scenario& scenario : scenarios)
        {
            std::wstring wideScenarioName;
            for (const char* character = scenario.name; *character; ++character)
                wideScenarioName.push_back(static_cast<wchar_t>(*character));
            if (!selectedScenario.empty() && selectedScenario != L"all" &&
                wideScenarioName != selectedScenario)
                continue;
            renderer.ApplyStage1ValidationPreset(Stage1ValidationPreset::DefaultVolume);
            renderer.ApplyStage5WeatherPreset(scenario.weather);
            renderer.ApplyStage2NoisePreset(scenario.noise);
            if (scenario.view == BenchmarkView::GroundHorizon)
                camera.SetOrbit(0.0f, -0.15f, 10000.0f, {0.0f, 1500.0f, 0.0f});
            else if (scenario.view == BenchmarkView::InsideLayer)
                camera.SetOrbit(0.0f, 0.0f, 100.0f, {0.0f, 3000.0f, 0.0f});
            else
                camera.SetOrbit(0.0f, -0.73f, 3000.0f, {0.0f, 2000.0f, 0.0f});

            std::vector<double> allCloud;
            std::vector<double> allFrame;
            for (int repeat = 0; repeat < 3; ++repeat)
            {
                const auto warmupStart = std::chrono::steady_clock::now();
                int warmupFrames = 0;
                while (warmupFrames < 120 || std::chrono::steady_clock::now() - warmupStart < std::chrono::seconds(2))
                { renderer.Render(camera, 0.0f); ++warmupFrames; }
                std::uint64_t lastSample = renderer.TimingSnapshot().gpuSampleIndex;
                int collected = 0;
                while (collected < 300)
                {
                    renderer.Render(camera, 0.0f);
                    const auto timing = renderer.TimingSnapshot();
                    if (!timing.gpuValid || timing.gpuSampleIndex == lastSample) continue;
                    lastSample = timing.gpuSampleIndex;
                    raw << scenario.name << ',' << repeat << ',' << collected << ','
                        << timing.rawGpuCloudMs << ',' << timing.rawGpuFrameMs << ','
                        << timing.rawCpuFrameMs << '\n';
                    allCloud.push_back(timing.rawGpuCloudMs);
                    allFrame.push_back(timing.rawGpuFrameMs);
                    ++collected;
                }
            }
            const auto cloud = stage9::ComputeStatistics(allCloud);
            const auto frame = stage9::ComputeStatistics(allFrame);
            if (!firstScenario) summary << ",\n";
            firstScenario = false;
            summary << "    {\"name\":\"" << scenario.name << "\",\"samples\":" << cloud.validSamples
                    << ",\"gpuCloud\":{\"min\":" << cloud.minimum << ",\"mean\":" << cloud.mean
                    << ",\"p50\":" << cloud.p50 << ",\"p95\":" << cloud.p95 << ",\"max\":" << cloud.maximum
                    << ",\"stddev\":" << cloud.standardDeviation << "},\"gpuFrameP95\":" << frame.p95 << "}";
        }
        summary << "\n  ]\n}\n";
        if (!raw.good() || !WriteTextFile(outputRoot / L"summary.json", summary.str())) return 21;
        return renderer.HasDebugLayerErrors() ? 22 : 0;
    }

    if (smokeTest)
    {
        for (int frame = 0; frame < 3; ++frame)
            renderer.Render(camera, static_cast<float>(frame) / 60.0f);
        return renderer.HasDebugLayerErrors() ? 2 : 0;
    }

    if (stage1SmokeTest)
    {
        // 단계 1의 모든 분기 셰이더와 볼륨 프리셋이 실제 D3D11 draw에서
        // 오류 없이 실행되는지 검사한다.
        for (int mode = 0; mode <= 9; ++mode)
        {
            renderer.SetDebugMode(static_cast<CloudDebugMode>(mode));
            for (int preset = static_cast<int>(Stage1ValidationPreset::DefaultVolume);
                 preset <= static_cast<int>(Stage1ValidationPreset::CoarseStep); ++preset)
            {
                renderer.ApplyStage1ValidationPreset(
                    static_cast<Stage1ValidationPreset>(preset));
                renderer.SetViewSamplingForSmoke(16u, 0.5f);
                renderer.Render(camera, static_cast<float>(mode + preset) / 60.0f);
            }
        }
        return renderer.HasDebugLayerErrors() ? 2 : 0;
    }

    if (stage2SmokeTest)
    {
        // 단계 2의 네 디버그 출력과 여덟 noise 프리셋을 실제 draw로 실행한다.
        for (int mode = static_cast<int>(CloudDebugMode::RawNoise);
             mode <= static_cast<int>(CloudDebugMode::NoiseUvw); ++mode)
        {
            renderer.SetDebugMode(static_cast<CloudDebugMode>(mode));
            for (int preset = static_cast<int>(Stage2NoisePreset::DefaultNoise);
                 preset <= static_cast<int>(Stage2NoisePreset::OffsetNoise); ++preset)
            {
                renderer.ApplyStage2NoisePreset(static_cast<Stage2NoisePreset>(preset));
                renderer.Render(camera, static_cast<float>(mode + preset) / 60.0f);
            }
        }
        return renderer.HasDebugLayerErrors() ? 2 : 0;
    }

    if (stage3SmokeTest)
    {
        // 단계 3의 두 진단 모드와 기본/좁은/넓은/겹친 fade 조합을 GPU draw로 검사한다.
        const float profiles[][2] = {
            { 0.20f, 0.80f }, { 0.05f, 0.95f },
            { 0.35f, 0.65f }, { 0.80f, 0.20f },
        };
        for (int mode = static_cast<int>(CloudDebugMode::HeightFraction);
             mode <= static_cast<int>(CloudDebugMode::HeightProfile); ++mode)
        {
            renderer.SetDebugMode(static_cast<CloudDebugMode>(mode));
            for (const auto& profile : profiles)
            {
                renderer.SetHeightProfile(profile[0], profile[1]);
                renderer.Render(camera, static_cast<float>(mode) / 60.0f);
            }
        }
        return renderer.HasDebugLayerErrors() ? 2 : 0;
    }

    if (stage4SmokeTest)
    {
        // Base/Detail 중간값 네 종류와 네 프리셋을 작은/넓은 볼륨에서 모두 draw한다.
        const Stage1ValidationPreset volumes[] = {
            Stage1ValidationPreset::DefaultVolume,
            Stage1ValidationPreset::WideVolume,
        };
        for (int mode = static_cast<int>(CloudDebugMode::BaseDensity);
             mode <= static_cast<int>(CloudDebugMode::DetailSampleMask); ++mode)
        {
            renderer.SetDebugMode(static_cast<CloudDebugMode>(mode));
            for (int preset = static_cast<int>(Stage4DetailPreset::DetailOff);
                 preset <= static_cast<int>(Stage4DetailPreset::StrongErosion); ++preset)
            {
                renderer.ApplyStage4DetailPreset(static_cast<Stage4DetailPreset>(preset));
                for (Stage1ValidationPreset volume : volumes)
                {
                    renderer.ApplyStage1ValidationPreset(volume);
                    renderer.Render(camera, static_cast<float>(mode + preset) / 60.0f);
                }
            }
        }
        return renderer.HasDebugLayerErrors() ? 2 : 0;
    }

    if (stage5SmokeTest)
    {
        // Weather 중간값 네 종류를 세 CPU 맵, Q/Y 볼륨, 이동 전/후에 draw한다.
        const Stage1ValidationPreset volumes[] = {
            Stage1ValidationPreset::DefaultVolume,
            Stage1ValidationPreset::WideVolume,
        };
        const std::uintptr_t weatherTextureIdentity =
            renderer.WeatherTextureIdentity();
        const std::uintptr_t weatherSrvIdentity = renderer.WeatherSrvIdentity();
        if (weatherTextureIdentity == 0 || weatherSrvIdentity == 0)
            return 3;
        for (int mode = static_cast<int>(CloudDebugMode::WeatherCoverage);
             mode <= static_cast<int>(CloudDebugMode::TypedHeightProfile); ++mode)
        {
            renderer.SetDebugMode(static_cast<CloudDebugMode>(mode));
            for (int preset = static_cast<int>(Stage5WeatherPreset::UniformLegacy);
                 preset <= static_cast<int>(Stage5WeatherPreset::ChannelDebug); ++preset)
            {
                if (!renderer.ApplyStage5WeatherPreset(
                        static_cast<Stage5WeatherPreset>(preset)))
                    return 3;
                if (renderer.WeatherTextureIdentity() != weatherTextureIdentity ||
                    renderer.WeatherSrvIdentity() != weatherSrvIdentity)
                    return 4;
                for (Stage1ValidationPreset volume : volumes)
                {
                    renderer.ApplyStage1ValidationPreset(volume);
                    renderer.Render(camera, 0.0f);
                    renderer.Render(camera, 4.0f);
                }
            }
        }
        if (!renderer.ApplyStage5WeatherPreset(Stage5WeatherPreset::PeriodicPerlin))
            return 5;
        const std::uint64_t defaultHash = renderer.WeatherMapHash();
        WeatherMapGeneratorSettings changedGenerator;
        changedGenerator.coverage.seed += 1u;
        if (!renderer.ApplyWeatherGeneratorSettings(changedGenerator) ||
            renderer.WeatherMapHash() == defaultHash)
            return 6;
        if (renderer.WeatherTextureIdentity() != weatherTextureIdentity ||
            renderer.WeatherSrvIdentity() != weatherSrvIdentity)
            return 7;
        renderer.Render(camera, 0.0f);
        return renderer.HasDebugLayerErrors() ? 2 : 0;
    }

    if (stage6SmokeTest)
    {
        // 조명 진단 4종과 태양·Weather·Q/Y 값을 pairwise 실제 draw한다.
        const Stage1ValidationPreset volumes[] = {
            Stage1ValidationPreset::DefaultVolume,
            Stage1ValidationPreset::WideVolume,
        };
        const Stage6SunPreset suns[] = {
            Stage6SunPreset::Noon,
            Stage6SunPreset::LowEast,
            Stage6SunPreset::LowWest,
        };
        // 자동 smoke 조합은 바인딩/분기 검증이 목적이므로 4-step으로 실행한다.
        // 아래에서 8/16/32 설정을 각각 별도 draw해 실제 품질 경로도 확인한다.
        renderer.SetLightSampling(4u, 1.0f);
        renderer.EnableNoiseLabPreviews(false);
        for (int mode = static_cast<int>(CloudDebugMode::LightTransmittance);
             mode <= static_cast<int>(CloudDebugMode::DirectSingleScattering); ++mode)
        {
            renderer.SetDebugMode(static_cast<CloudDebugMode>(mode));
            // 네 pairwise draw가 모든 mode를 실행하며 sun/weather/volume 값도
            // 순환 배치해 각각 최소 한 번 실제 b1/b3 및 t2 경로를 지난다.
            const int combination = mode -
                static_cast<int>(CloudDebugMode::LightTransmittance);
            renderer.ApplyStage6SunPreset(suns[combination % 3]);
            if (!renderer.ApplyStage5WeatherPreset(
                    static_cast<Stage5WeatherPreset>(combination % 3)))
                return 3;
            renderer.ApplyStage1ValidationPreset(volumes[combination % 2]);
            renderer.SetViewSamplingForSmoke(1u, 32.0f);
            renderer.Render(camera, 0.0f);
        }

        // 품질/비용 비교용 8/16/32 step 설정도 같은 b3 경로로 전달한다.
        const std::uint32_t lightSteps[] = { 8u, 16u, 32u };
        for (std::uint32_t steps : lightSteps)
        {
            renderer.SetLightSampling(steps, 0.25f);
            renderer.SetViewSamplingForSmoke(1u, 32.0f);
            renderer.SetDebugMode(CloudDebugMode::TotalLightSamples);
            renderer.Render(camera, 0.0f);
            if (renderer.LightSettings().maxLightSteps != steps)
                return 4;
        }

        const std::filesystem::path exportRoot =
            std::filesystem::temp_directory_path() / L"VolumetricCloudStage6Smoke";
        if (!renderer.ExportNoiseLabSnapshot(exportRoot))
            return 5;
        bool foundCurrentSchema = false;
        std::error_code exportError;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 exportRoot, exportError))
        {
            if (exportError)
                return 6;
            if (entry.path().filename() != L"noise-settings.json")
                continue;
            std::string metadata;
            if (ReadTextFile(entry.path(), metadata) &&
                metadata.find("\"schemaVersion\": 13") != std::string::npos &&
                metadata.find("\"lightRayDensitySource\": \"baseDensityWithoutDetailErosion\"") != std::string::npos)
                foundCurrentSchema = true;
        }
        if (!foundCurrentSchema)
            return 7;
        return renderer.HasDebugLayerErrors() ? 2 : 0;
    }

    if (stage7SmokeTest)
    {
        const Stage1ValidationPreset volumes[] = {
            Stage1ValidationPreset::DefaultVolume,
            Stage1ValidationPreset::WideVolume,
        };
        const Stage6SunPreset suns[] = {
            Stage6SunPreset::Noon,
            Stage6SunPreset::LowEast,
            Stage6SunPreset::LowWest,
        };
        const Stage7PhasePreset phases[] = {
            Stage7PhasePreset::Off,
            Stage7PhasePreset::Balanced,
            Stage7PhasePreset::SilverLining,
            Stage7PhasePreset::BackscatterCheck,
        };

        renderer.EnableNoiseLabPreviews(false);
        renderer.SetViewSamplingForSmoke(1u, 32.0f);
        renderer.SetLightSampling(4u, 1.0f);
        for (int mode = static_cast<int>(CloudDebugMode::PhaseCosTheta);
             mode <= static_cast<int>(CloudDebugMode::DualPhaseFactor); ++mode)
        {
            const int combination = mode -
                static_cast<int>(CloudDebugMode::PhaseCosTheta);
            renderer.SetDebugMode(static_cast<CloudDebugMode>(mode));
            renderer.ApplyStage6SunPreset(suns[combination % 3]);
            renderer.ApplyStage7PhasePreset(phases[combination % 4]);
            renderer.ApplyStage1ValidationPreset(volumes[combination % 2]);
            renderer.Render(camera, 0.0f);
        }

        // Phase 프리셋을 바꿔도 태양 방향 프리셋 이름과 Light Ray 품질은 유지되어야 한다.
        renderer.ApplyStage6SunPreset(Stage6SunPreset::Noon);
        renderer.SetLightSampling(4u, 1.0f);
        renderer.ApplyStage6SunPreset(Stage6SunPreset::Noon);
        renderer.ApplyStage7PhasePreset(Stage7PhasePreset::Balanced);
        if (renderer.SunPreset() != Stage6SunPreset::Noon ||
            renderer.PhasePreset() != Stage7PhasePreset::Balanced ||
            renderer.LightSettings().maxLightSteps != 4u ||
            sizeof(LightParameters) != 64u)
            return 3;

        // 합성 경로에서 Off와 방향성 Phase가 실제로 다른 프레임을 만드는지 확인한다.
        renderer.EnableFrameHashCapture(true);
        // km 평면층에서도 상단의 직접광 표본을 충분히 잡아 Phase 차이가 해시에 나타나게 한다.
        renderer.ApplyStage1ValidationPreset(Stage1ValidationPreset::ThinVolume);
        renderer.ApplyStage5WeatherPreset(Stage5WeatherPreset::UniformLegacy);
        renderer.ApplyStage2NoisePreset(Stage2NoisePreset::DenseCoverage);
        renderer.SetViewSamplingForSmoke(32u, 100.0f);
        renderer.SetLightSampling(16u, 250.0f);
        renderer.SetDebugMode(CloudDebugMode::Composite);
        renderer.ApplyStage7PhasePreset(Stage7PhasePreset::Off);
        renderer.Render(camera, 0.0f);
        const std::uint64_t offHash = renderer.LastCloudFrameHash();
        renderer.ApplyStage7PhasePreset(Stage7PhasePreset::SilverLining);
        renderer.Render(camera, 0.0f);
        const std::uint64_t onHash = renderer.LastCloudFrameHash();
        if (offHash == 0 || onHash == 0 || offHash == onHash)
            return 4;

        renderer.SetDebugMode(CloudDebugMode::DirectSingleScattering);
        renderer.Render(camera, 0.0f);

        const std::filesystem::path exportRoot =
            std::filesystem::temp_directory_path() / L"VolumetricCloudStage7Smoke";
        if (!renderer.ExportNoiseLabSnapshot(exportRoot))
            return 5;
        bool foundSchema7 = false;
        std::error_code exportError;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 exportRoot, exportError))
        {
            if (exportError)
                return 6;
            if (entry.path().filename() != L"noise-settings.json")
                continue;
            std::string metadata;
            if (ReadTextFile(entry.path(), metadata) &&
                metadata.find("\"schemaVersion\": 13") != std::string::npos &&
                metadata.find("\"phaseFunction\": \"dualLobeHenyeyGreensteinIsotropicRelative\"") != std::string::npos &&
                metadata.find("\"phaseDirectionConvention\": \"cosTheta=dot(cameraToSample,sampleToSun)\"") != std::string::npos)
                foundSchema7 = true;
        }
        if (!foundSchema7)
            return 7;
        return renderer.HasDebugLayerErrors() ? 2 : 0;
    }

    if (stage8SmokeTest)
    {
        renderer.EnableNoiseLabPreviews(false);
        renderer.SetViewSamplingForSmoke(8u, 2.0f);
        renderer.SetLightSampling(4u, 1.0f);
        renderer.SetDebugMode(CloudDebugMode::AccumulatedDirectLighting);
        renderer.ApplyStage8EnvironmentPreset(Stage8EnvironmentPreset::Balanced);
        renderer.ApplyStage6SunPreset(Stage6SunPreset::Noon);
        renderer.ApplyStage1ValidationPreset(Stage1ValidationPreset::WideVolume);
        renderer.Render(camera, 0.0f);

        // Environment 프리셋은 Phase와 태양 프리셋을 바꾸지 않는다.
        renderer.ApplyStage6SunPreset(Stage6SunPreset::Noon);
        renderer.ApplyStage7PhasePreset(Stage7PhasePreset::Balanced);
        renderer.ApplyStage8EnvironmentPreset(Stage8EnvironmentPreset::StrongFill);
        if (renderer.SunPreset() != Stage6SunPreset::Noon ||
            renderer.PhasePreset() != Stage7PhasePreset::Balanced ||
            renderer.EnvironmentPreset() != Stage8EnvironmentPreset::StrongFill ||
            sizeof(EnvironmentParameters) != 64u)
            return 3;

        renderer.EnableFrameHashCapture(true);
        renderer.ApplyStage1ValidationPreset(Stage1ValidationPreset::WideVolume);
        renderer.SetDebugMode(CloudDebugMode::Composite);
        renderer.ApplyStage8EnvironmentPreset(Stage8EnvironmentPreset::Off);
        renderer.Render(camera, 0.0f);
        const std::uint64_t offHash = renderer.LastCloudFrameHash();
        renderer.ApplyStage8EnvironmentPreset(Stage8EnvironmentPreset::Balanced);
        renderer.Render(camera, 0.0f);
        const std::uint64_t balancedHash = renderer.LastCloudFrameHash();
        if (offHash == 0 || balancedHash == 0 || offHash == balancedHash)
            return 4;

        // 다중 산란은 기존 Light Ray를 재사용하므로 비용 출력 해시가 같아야 한다.
        renderer.SetDebugMode(CloudDebugMode::TotalLightSamples);
        renderer.ApplyStage8EnvironmentPreset(Stage8EnvironmentPreset::Off);
        renderer.Render(camera, 0.0f);
        const std::uint64_t samplesOffHash = renderer.LastCloudFrameHash();
        renderer.ApplyStage8EnvironmentPreset(Stage8EnvironmentPreset::Balanced);
        renderer.Render(camera, 0.0f);
        const std::uint64_t samplesOnHash = renderer.LastCloudFrameHash();
        if (samplesOffHash == 0 || samplesOffHash != samplesOnHash)
            return 5;

        const std::filesystem::path exportRoot =
            std::filesystem::temp_directory_path() / L"VolumetricCloudStage8Smoke";
        if (!renderer.ExportNoiseLabSnapshot(exportRoot))
            return 6;
        bool foundSchema8 = false;
        std::error_code exportError;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 exportRoot, exportError))
        {
            if (exportError)
                return 7;
            if (entry.path().filename() != L"noise-settings.json")
                continue;
            std::string metadata;
            if (ReadTextFile(entry.path(), metadata) &&
                metadata.find("\"schemaVersion\": 13") != std::string::npos &&
                metadata.find("\"environmentSource\": \"analyticColorsNoExternalTexture\"") != std::string::npos &&
                metadata.find("\"multipleScatteringModel\": \"reusedLightOpticalDepthOctaves\"") != std::string::npos)
                foundSchema8 = true;
        }
        if (!foundSchema8)
            return 8;
        return renderer.HasDebugLayerErrors() ? 2 : 0;
    }

    if (stage9SmokeTest)
    {
        renderer.SetNoiseLabVisible(false);
        renderer.EnableNoiseLabPreviews(false);
        renderer.EnableFrameHashCapture(true);
        renderer.SetDebugMode(CloudDebugMode::Composite);
        renderer.ApplyStage1ValidationPreset(Stage1ValidationPreset::WideVolume);
        renderer.ApplyStage5WeatherPreset(Stage5WeatherPreset::PeriodicPerlin);
        renderer.ApplyStage9OptimizationPreset(Stage9OptimizationPreset::Off);
        renderer.Render(camera, 0.0f);
        const std::uint64_t legacyHash = renderer.LastCloudFrameHash();

        renderer.ApplyStage9OptimizationPreset(Stage9OptimizationPreset::Balanced);
        renderer.Render(camera, 0.0f);
        const std::uint64_t optimizedHash = renderer.LastCloudFrameHash();
        if (legacyHash == 0 || optimizedHash == 0 ||
            sizeof(OptimizationParameters) != 32u)
            return 3;

        const CloudDebugMode diagnosticModes[] = {
            CloudDebugMode::ExecutedViewSteps,
            CloudDebugMode::CoarseSkippedRatio,
            CloudDebugMode::EarlyExitSavings,
            CloudDebugMode::SupportPrecheckMask,
            CloudDebugMode::MarchStateTransitions };
        for (CloudDebugMode mode : diagnosticModes)
        {
            renderer.SetDebugMode(mode);
            renderer.Render(camera, 0.0f);
            if (renderer.LastCloudFrameHash() == 0)
                return 4;
        }

        const std::filesystem::path exportRoot =
            std::filesystem::temp_directory_path() / L"VolumetricCloudStage9Smoke";
        if (!renderer.ExportNoiseLabSnapshot(exportRoot))
            return 5;
        bool foundSchema13 = false;
        std::error_code exportError;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(exportRoot, exportError))
        {
            if (exportError) return 6;
            if (entry.path().filename() != L"noise-settings.json") continue;
            std::string metadata;
            if (ReadTextFile(entry.path(), metadata) &&
                metadata.find("\"schemaVersion\": 13") != std::string::npos &&
                metadata.find("\"optimizationPreset\"") != std::string::npos)
                foundSchema13 = true;
        }
        if (!foundSchema13) return 7;
        return renderer.HasDebugLayerErrors() ? 2 : 0;
    }

    if (stage13SmokeTest)
    {
        renderer.SetNoiseLabVisible(false);
        renderer.EnableNoiseLabPreviews(false);
        renderer.EnableFrameHashCapture(true);
        renderer.ApplyStage1ValidationPreset(Stage1ValidationPreset::DefaultVolume);
        renderer.ApplyStage5WeatherPreset(Stage5WeatherPreset::PeriodicPerlin);
        renderer.ApplyStage9OptimizationPreset(Stage9OptimizationPreset::Off);

        const struct CameraCase
        {
            float yaw, pitch, distance;
            DirectX::XMFLOAT3 target;
        } cameras[] = {
            {0.0f, -0.73f, 3000.0f, {0.0f, 2000.0f, 0.0f}},
            {0.0f, -0.15f, 10000.0f, {0.0f, 1500.0f, 0.0f}},
            {0.0f, 0.0f, 100.0f, {0.0f, 3000.0f, 0.0f}},
            {0.0f, 0.75f, 3000.0f, {0.0f, 3000.0f, 0.0f}}
        };
        for (const CameraCase& cameraCase : cameras)
        {
            camera.SetOrbit(cameraCase.yaw, cameraCase.pitch,
                            cameraCase.distance, cameraCase.target);
            renderer.SetDebugMode(CloudDebugMode::Composite);
            renderer.Render(camera, 0.0f);
            if (renderer.LastCloudFrameHash() == 0)
                return 3;
        }

        renderer.SetDebugMode(CloudDebugMode::CloudLayerEntryDistance);
        renderer.Render(camera, 0.0f);
        const std::uint64_t entryHash = renderer.LastCloudFrameHash();
        renderer.SetDebugMode(CloudDebugMode::CloudLayerExitDistance);
        renderer.Render(camera, 0.0f);
        if (entryHash == 0 || renderer.LastCloudFrameHash() == 0)
            return 4;

        camera.SetOrbit(0.0f, 0.0f, 100.0f, {0.0f, 3000.0f, 0.0f});
        renderer.SetDebugMode(CloudDebugMode::DetailLodFactor);
        renderer.Render(camera, 0.0f);
        const std::uint64_t nearLodHash = renderer.LastCloudFrameHash();
        renderer.SetDebugMode(CloudDebugMode::DetailSampleMask);
        renderer.Render(camera, 0.0f);
        const std::uint64_t nearSampleHash = renderer.LastCloudFrameHash();
        camera.SetOrbit(0.0f, -0.15f, 10000.0f, {0.0f, 1500.0f, 0.0f});
        renderer.SetDebugMode(CloudDebugMode::DetailLodFactor);
        renderer.Render(camera, 0.0f);
        const std::uint64_t farLodHash = renderer.LastCloudFrameHash();
        renderer.SetDebugMode(CloudDebugMode::DetailSampleMask);
        renderer.Render(camera, 0.0f);
        const std::uint64_t farSampleHash = renderer.LastCloudFrameHash();
        if (nearLodHash == 0 || nearSampleHash == 0 || farLodHash == 0 ||
            farSampleHash == 0 || nearLodHash == farLodHash ||
            nearSampleHash == farSampleHash)
            return 8;

        // 사용자가 Z Raw Noise에서 보고한 간헐적 팝이 실제 GPU 출력 변화인지
        // 표시 과정의 모아레인지 분리한다. 시간·바람·카메라·최적화를 고정하고
        // 기본 Base와 의도적인 고주파 스트레스 값을 각각 8프레임 비교한다.
        camera.SetOrbit(0.0f, -0.15f, 10000.0f, {0.0f, 1500.0f, 0.0f});
        renderer.ApplyStage2NoisePreset(Stage2NoisePreset::StoppedWind);
        renderer.ApplyStage9OptimizationPreset(Stage9OptimizationPreset::Off);
        renderer.SetDebugMode(CloudDebugMode::RawNoise);
        const auto captureStableRawNoiseHash =
            [&](float baseNoiseScale, std::uint64_t& stableHash)
        {
            renderer.SetBaseNoiseScaleForSmoke(baseNoiseScale);
            stableHash = 0;
            for (int frame = 0; frame < 8; ++frame)
            {
                renderer.Render(camera, 0.0f);
                const std::uint64_t frameHash = renderer.LastCloudFrameHash();
                if (frameHash == 0)
                    return false;
                if (frame == 0)
                    stableHash = frameHash;
                else if (frameHash != stableHash)
                    return false;
            }
            return true;
        };

        std::uint64_t defaultRawNoiseHash = 0;
        if (!captureStableRawNoiseHash(0.00035f, defaultRawNoiseHash))
            return 9;
        std::uint64_t stressRawNoiseHash = 0;
        if (!captureStableRawNoiseHash(0.00662f, stressRawNoiseHash))
            return 10;
        if (defaultRawNoiseHash == stressRawNoiseHash)
            return 11;

        // 아래 export는 제품 기본값을 검증하므로 스트레스 값을 남기지 않는다.
        renderer.ApplyStage2NoisePreset(Stage2NoisePreset::DefaultNoise);

        const std::filesystem::path exportRoot =
            std::filesystem::temp_directory_path() / L"VolumetricCloudStage13Smoke";
        if (!renderer.ExportNoiseLabSnapshot(exportRoot))
            return 5;
        bool foundDomain = false;
        std::error_code exportError;
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator(exportRoot, exportError))
        {
            if (exportError) return 6;
            if (entry.path().filename() != L"noise-settings.json") continue;
            std::string metadata;
            if (ReadTextFile(entry.path(), metadata) &&
                metadata.find("\"schemaVersion\": 13") != std::string::npos &&
                metadata.find("\"cloudDomain\": \"cameraCenteredPlanarLayer\"") != std::string::npos &&
                metadata.find("\"weatherMapWorldSize\": 32000") != std::string::npos &&
                metadata.find("\"baseNoiseScale\": 0.00035") != std::string::npos &&
                metadata.find("\"detailNoiseScale\": 0.0025") != std::string::npos &&
                metadata.find("\"maxViewSteps\": 512") != std::string::npos &&
                metadata.find("\"viewStepSizeMeters\": 100") != std::string::npos &&
                metadata.find("\"singleScatteringModel\": \"energyConservingAlbedo\"") != std::string::npos &&
                metadata.find("\"singleScatteringAlbedo\": 0.9") != std::string::npos &&
                metadata.find("\"detailLodFadeStartDistanceMeters\": 8000") != std::string::npos &&
                metadata.find("\"detailLodFadeEndDistanceMeters\": 20000") != std::string::npos &&
                metadata.find("\"detailLodFilter\": \"lerpMean0.5ThenSkip\"") != std::string::npos &&
                metadata.find("\"scatteringCoefficient\"") == std::string::npos)
                foundDomain = true;
        }
        if (!foundDomain || sizeof(CloudParameters) != 128u ||
            sizeof(LightParameters) != 64u)
            return 7;
        return renderer.HasDebugLayerErrors() ? 2 : 0;
    }

    if (performanceOverlaySmokeTest)
    {
        // UI 창을 숨긴 상태에서도 오버레이와 timestamp query가 계속 동작해야 한다.
        // 작은 숨김 타깃에서 GPU 완료를 비동기로 기다리되, 어떤 프레임에서도
        // GetData가 GPU flush나 CPU 대기를 유발하지 않도록 profiler가 DONOTFLUSH만 쓴다.
        renderer.SetNoiseLabVisible(false);
        renderer.EnableNoiseLabPreviews(false);
        renderer.SetVSyncEnabled(false);
        renderer.SetViewSamplingForSmoke(1u, 32.0f);
        renderer.SetLightSampling(1u, 32.0f);

        bool receivedGpuTiming = false;
        for (int frame = 0; frame < 180; ++frame)
        {
            renderer.Render(camera, static_cast<float>(frame) / 60.0f);
            const FrameTimingSnapshot timing = renderer.TimingSnapshot();
            if (timing.gpuValid)
            {
                receivedGpuTiming = true;
                if (!std::isfinite(timing.gpuFrameMs) ||
                    !std::isfinite(timing.gpuCloudMs) ||
                    timing.gpuFrameMs < 0.0 || timing.gpuCloudMs < 0.0 ||
                    timing.gpuCloudMs > timing.gpuFrameMs)
                    return 3;
                break;
            }
            Sleep(1);
        }
        if (!receivedGpuTiming)
            return 4;

        const FrameTimingSnapshot timing = renderer.TimingSnapshot();
        if (!timing.cpuValid || timing.frameIndex == 0 ||
            !std::isfinite(timing.cpuFrameMs) ||
            !std::isfinite(timing.fps) || timing.cpuFrameMs <= 0.0 ||
            timing.fps <= 0.0)
            return 5;

        renderer.SetVSyncEnabled(true);
        renderer.Render(camera, 0.0f);
        if (!renderer.VSyncEnabled())
            return 6;
        renderer.SetVSyncEnabled(false);
        renderer.Render(camera, 0.0f);
        if (renderer.VSyncEnabled())
            return 7;

        return renderer.HasDebugLayerErrors() ? 2 : 0;
    }

    if (noiseLabSmokeTest)
    {
        for (int mode = static_cast<int>(NoiseOutputMode::RawNoise);
             mode <= static_cast<int>(NoiseOutputMode::WeatherUv); ++mode)
        {
            renderer.SetNoiseLabOutputMode(static_cast<NoiseOutputMode>(mode));
            renderer.Render(camera, 0.0f);
            if (!renderer.ValidateNoiseLabPreviews())
                return 30 + mode;
        }
        wchar_t temporaryPath[MAX_PATH] = {};
        if (GetTempPathW(MAX_PATH, temporaryPath) == 0 ||
            !renderer.ExportNoiseLabSnapshot(
                std::filesystem::path(temporaryPath) / L"VolumetricCloudNoiseLabSmoke"))
            return 4;
        return renderer.HasDebugLayerErrors() ? 2 : 0;
    }

    if (shaderHotReloadSmokeTest)
    {
        renderer.EnableFrameHashCapture(true);
        renderer.Render(camera, 0.0f);
        const std::uint64_t baselineGeneration = renderer.ShaderGeneration();
        const std::uint64_t baselineLabHash = renderer.NoiseLabPreviewHash(0);
        const std::uint64_t baselineCloudHash = renderer.LastCloudFrameHash();

        const std::filesystem::path noisePath =
            hotReloadShaderDirectory / L"Noise.hlsli";
        std::string original;
        if (!ReadTextFile(noisePath, original))
            return 6;
        std::string biased = original;
        const std::string from = "#define VCLOUD_NOISE_TEST_BIAS 0.0";
        const size_t biasPosition = biased.find(from);
        if (biasPosition == std::string::npos)
            return 7;
        biased.replace(biasPosition, from.size(),
                       "#define VCLOUD_NOISE_TEST_BIAS 0.25");
        if (!WriteTextFile(noisePath, biased))
            return 8;
        renderer.Render(camera, 0.0f);
        const std::uint64_t biasedGeneration = renderer.ShaderGeneration();
        const std::uint64_t biasedLabHash = renderer.NoiseLabPreviewHash(0);
        const std::uint64_t biasedCloudHash = renderer.LastCloudFrameHash();
        if (biasedGeneration != baselineGeneration + 1 ||
            biasedLabHash == baselineLabHash || biasedCloudHash == baselineCloudHash)
            return 9;

        if (!WriteTextFile(noisePath, biased + "\n#error expected hot reload failure\n"))
            return 10;
        renderer.Render(camera, 0.0f);
        if (renderer.ShaderGeneration() != biasedGeneration ||
            renderer.NoiseLabPreviewHash(0) != biasedLabHash ||
            renderer.LastCloudFrameHash() != biasedCloudHash)
            return 11;

        if (!WriteTextFile(noisePath, original))
            return 12;
        renderer.Render(camera, 0.0f);
        if (renderer.ShaderGeneration() != biasedGeneration + 1 ||
            renderer.NoiseLabPreviewHash(0) != baselineLabHash ||
            renderer.LastCloudFrameHash() != baselineCloudHash)
            return 13;
        return renderer.HasDebugLayerErrors() ? 2 : 0;
    }

    // ---- 고해상도 타이머 준비 ----
    LARGE_INTEGER freq, start;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);

    // ---- 메인 루프 ----
    while (window.ProcessMessages())
    {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        float elapsed = static_cast<float>(now.QuadPart - start.QuadPart) / freq.QuadPart;

        // 창이 리사이즈되면 종횡비를 갱신
        if (window.GetHeight() > 0)
            camera.SetAspect(static_cast<float>(window.GetWidth()) / window.GetHeight());

        renderer.Render(camera, elapsed);
        // Noise Lab에서 프리셋/파라미터를 바꾼 경우에도 다음 프레임까지 기다리지
        // 않고 창 제목이 Renderer의 현재 상태를 정확히 표시하게 한다.
        window.RefreshDebugTitle();
    }

    return 0;
}
