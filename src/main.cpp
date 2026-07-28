// ============================================================================
//  main.cpp  —  진입점 (WinMain)
// ----------------------------------------------------------------------------
//  창(Window) · 카메라(Camera) · 렌더러(Renderer)를 생성·연결하고,
//  메인 루프에서 매 프레임 박스 볼륨을 레이마칭으로 그린다.
// ============================================================================
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <vector>

#include "Window.h"
#include "Camera.h"
#include "Renderer.h"

namespace
{
struct MetricSummary
{
    double mean = 0.0;
    double median = 0.0;
    double p95 = 0.0;
};

struct ViewStepBenchmarkRow
{
    float thickness = 0.0f;
    int viewSteps = 0;
    size_t sampleCount = 0;
    MetricSummary gpuCloud;
    MetricSummary gpuReconstruction;
    MetricSummary gpuTotal;
    MetricSummary cpuRender;
};

MetricSummary Summarize(std::vector<float> values)
{
    std::sort(values.begin(), values.end());
    MetricSummary result;
    result.mean = std::accumulate(values.begin(), values.end(), 0.0) / values.size();
    const size_t middle = values.size() / 2;
    result.median = values.size() % 2 == 0
        ? (static_cast<double>(values[middle - 1]) + values[middle]) * 0.5
        : values[middle];
    const size_t p95Index = static_cast<size_t>(
        std::ceil(static_cast<double>(values.size()) * 0.95)) - 1;
    result.p95 = values[(std::min)(p95Index, values.size() - 1)];
    return result;
}

std::filesystem::path BenchmarkDirectory()
{
    const wchar_t* localAppData = _wgetenv(L"LOCALAPPDATA");
    if (!localAppData || localAppData[0] == L'\0') return {};
    std::filesystem::path path = localAppData;
    return path / L"VolumetricCloud" / L"benchmarks";
}

std::filesystem::path ViewStepBenchmarkPath(bool temporal)
{
    const auto directory = BenchmarkDirectory();
    if (directory.empty()) return {};
    return directory /
        (temporal ? L"view-step-quality-temporal.csv" : L"view-step-quality-reference.csv");
}

bool ReplaceFile(const std::filesystem::path& temporaryPath,
                 const std::filesystem::path& outputPath)
{
    return MoveFileExW(temporaryPath.c_str(), outputPath.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

int RunViewStepBenchmark(Window& window, Camera& camera, Renderer& renderer, bool temporal)
{
    constexpr int kWarmupFrames = 30;
    constexpr size_t kRequiredSamples = 120;
    constexpr int kMaximumFrames = 600;
    constexpr int kSteps[] = { 128, 160, 192, 256 };
    constexpr float kThicknesses[] = { 3.8f, 16.0f };

    const std::filesystem::path outputPath = ViewStepBenchmarkPath(temporal);
    if (outputPath.empty()) return 8;
    std::error_code ec;
    std::filesystem::create_directories(outputPath.parent_path(), ec);
    if (ec) return 8;
    renderer.SetBenchmarkMode(true);
    renderer.SetTemporalEnabled(temporal);
    std::vector<ViewStepBenchmarkRow> rows;
    rows.reserve(8);

    for (const float thickness : kThicknesses)
    {
        for (const int steps : kSteps)
        {
            renderer.ConfigureViewStepBenchmark(steps, thickness);
            renderer.EndBenchmarkCollection();
            for (int frame = 0; frame < kWarmupFrames; ++frame)
            {
                if (!window.ProcessMessages()) return 6;
                renderer.Render(camera, 0.0f);
            }

            renderer.BeginBenchmarkCollection();
            for (int frame = 0;
                 frame < kMaximumFrames &&
                 renderer.BenchmarkSamples().size() < kRequiredSamples;
                 ++frame)
            {
                if (!window.ProcessMessages()) return 6;
                renderer.Render(camera, 0.0f);
            }
            renderer.EndBenchmarkCollection();

            const auto& samples = renderer.BenchmarkSamples();
            if (samples.size() < kRequiredSamples) return 5;

            std::vector<float> gpuCloud;
            std::vector<float> gpuReconstruction;
            std::vector<float> gpuTotal;
            std::vector<float> cpuRender;
            gpuCloud.reserve(kRequiredSamples);
            gpuReconstruction.reserve(kRequiredSamples);
            gpuTotal.reserve(kRequiredSamples);
            cpuRender.reserve(kRequiredSamples);
            for (size_t i = 0; i < kRequiredSamples; ++i)
            {
                const BenchmarkFrameSample& sample = samples[i];
                if (!std::isfinite(sample.gpuCloudMs) || sample.gpuCloudMs <= 0.0f ||
                    !std::isfinite(sample.gpuTotalMs) || sample.gpuTotalMs <= 0.0f ||
                    !std::isfinite(sample.gpuReconstructionMs) ||
                    sample.gpuReconstructionMs < 0.0f ||
                    !std::isfinite(sample.cpuRenderMs) || sample.cpuRenderMs <= 0.0f ||
                    sample.gpuTotalMs + 1.0e-4f < sample.gpuCloudMs)
                {
                    return 7;
                }
                gpuCloud.push_back(sample.gpuCloudMs);
                gpuReconstruction.push_back(sample.gpuReconstructionMs);
                gpuTotal.push_back(sample.gpuTotalMs);
                cpuRender.push_back(sample.cpuRenderMs);
            }
            rows.push_back({
                thickness,
                steps,
                kRequiredSamples,
                Summarize(std::move(gpuCloud)),
                Summarize(std::move(gpuReconstruction)),
                Summarize(std::move(gpuTotal)),
                Summarize(std::move(cpuRender)) });
        }
    }

    std::filesystem::path temporaryPath = outputPath;
    temporaryPath += L".tmp";
    std::filesystem::remove(temporaryPath, ec);
    if (ec) return 8;
    std::ofstream output(temporaryPath, std::ios::trunc);
    if (!output) return 8;
    output << "thickness_km,view_steps,sample_count,"
              "gpu_cloud_mean_ms,gpu_cloud_median_ms,gpu_cloud_p95_ms,"
              "gpu_reconstruction_mean_ms,gpu_reconstruction_median_ms,gpu_reconstruction_p95_ms,"
              "gpu_total_mean_ms,gpu_total_median_ms,gpu_total_p95_ms,"
              "cpu_render_mean_ms,cpu_render_median_ms,cpu_render_p95_ms,"
              "theoretical_gpu_fps\n";
    output << std::fixed << std::setprecision(4);
    for (const ViewStepBenchmarkRow& row : rows)
    {
        output << row.thickness << ',' << row.viewSteps << ',' << row.sampleCount << ','
               << row.gpuCloud.mean << ',' << row.gpuCloud.median << ',' << row.gpuCloud.p95 << ','
               << row.gpuReconstruction.mean << ',' << row.gpuReconstruction.median << ','
               << row.gpuReconstruction.p95 << ','
               << row.gpuTotal.mean << ',' << row.gpuTotal.median << ',' << row.gpuTotal.p95 << ','
               << row.cpuRender.mean << ',' << row.cpuRender.median << ',' << row.cpuRender.p95 << ','
               << 1000.0 / row.gpuTotal.median << '\n';
    }
    output.close();
    if (!output || rows.size() != 8) return 8;
    return ReplaceFile(temporaryPath, outputPath) ? 0 : 8;
}

int RunStartupDiagnostic(Window& window, Camera& camera, Renderer& renderer, bool temporal)
{
    constexpr double kDurationSeconds = 120.0;
    const auto directory = BenchmarkDirectory();
    if (directory.empty()) return 9;
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) return 9;
    const auto outputPath = directory /
        (temporal ? L"startup-temporal.csv" : L"startup-reference.csv");
    auto temporaryPath = outputPath;
    temporaryPath += L".tmp";
    std::filesystem::remove(temporaryPath, ec);
    if (ec) return 9;
    std::ofstream output(temporaryPath, std::ios::trunc);
    if (!output) return 9;
    output << "elapsed_ms,frame,frame_interval_ms,cpu_submit_ms,present_wait_ms,"
              "gpu_raymarch_ms,gpu_reconstruction_ms,gpu_total_ms,"
              "runtime_compile_count,noise_dispatch_count,cache_status\n";
    output << std::fixed << std::setprecision(4);

    renderer.SetTemporalEnabled(temporal);
    renderer.ToggleTelemetry();
    LARGE_INTEGER frequency = {}, start = {}, now = {};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    unsigned int frame = 0;
    double elapsed = 0.0;
    while (elapsed < kDurationSeconds)
    {
        if (!window.ProcessMessages()) return 6;
        QueryPerformanceCounter(&now);
        elapsed = static_cast<double>(now.QuadPart - start.QuadPart) / frequency.QuadPart;
        renderer.Render(camera, static_cast<float>(elapsed));
        output << elapsed * 1000.0 << ',' << frame++ << ','
               << renderer.FrameIntervalMs() << ',' << renderer.CpuRenderMs() << ','
               << renderer.PresentWaitMs() << ',' << renderer.GpuCloudMs() << ','
               << renderer.GpuReconstructionMs() << ',' << renderer.GpuTotalMs() << ','
               << renderer.RuntimeCompileCount() << ',' << renderer.NoiseDispatchCount() << ','
               << renderer.CacheStatus() << '\n';
    }
    output.close();
    if (!output) return 9;
    return ReplaceFile(temporaryPath, outputPath) ? 0 : 9;
}
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int)
{
    const int kWidth  = 1280;
    const int kHeight = 720;

    // ---- 객체 생성 ----
    Window   window(hInstance, kWidth, kHeight, L"VolumetricCloud - F1: Cloud Debug");
    Camera   camera;
    Renderer renderer;
    const bool buildDefaultCache = wcsstr(GetCommandLineW(), L"--build-default-cache") != nullptr;
    const bool cacheSmokeTest = wcsstr(GetCommandLineW(), L"--cache-smoke-test") != nullptr;
    const bool runCodeTests = wcsstr(GetCommandLineW(), L"--run-code-tests") != nullptr;
    const bool benchmarkViewSteps =
        wcsstr(GetCommandLineW(), L"--benchmark-view-steps") != nullptr;
    const bool benchmarkReference =
        wcsstr(GetCommandLineW(), L"--benchmark-view-steps-reference") != nullptr;
    const bool diagnoseStartupReference =
        wcsstr(GetCommandLineW(), L"--diagnose-startup-reference") != nullptr;
    const bool diagnoseStartupTemporal =
        wcsstr(GetCommandLineW(), L"--diagnose-startup-temporal") != nullptr;

    camera.SetAspect(static_cast<float>(kWidth) / kHeight);

    if (!renderer.Init(window.GetHandle(), kWidth, kHeight, buildDefaultCache))
        return -1; // 초기화 실패 (오류 메시지는 Renderer가 표시)

    // 입력/리사이즈 연결
    window.SetCamera(&camera);
    window.SetRenderer(&renderer);

    if (buildDefaultCache)
    {
        ShowWindow(window.GetHandle(), SW_HIDE);
        renderer.Render(camera, 0.0f);
        return renderer.SaveDefaultNoiseCache() ? 0 : 2;
    }
    if (cacheSmokeTest)
    {
        ShowWindow(window.GetHandle(), SW_HIDE);
        // 캐시 히트 여부는 Init에서 이미 결정된다. 1280×720 cloud draw/Present를
        // 수행하지 않아 CI의 GPU 속도와 무관하게 시작 경로만 검사한다.
        return renderer.RuntimeCompileCount() == 0 && renderer.NoiseDispatchCount() == 0 ? 0 : 3;
    }
    if (runCodeTests)
    {
        ShowWindow(window.GetHandle(), SW_HIDE);
        renderer.Render(camera, 0.0f);
        return renderer.RunCodeTests() ? 0 : 4;
    }
    if (benchmarkReference)
        return RunViewStepBenchmark(window, camera, renderer, false);
    if (benchmarkViewSteps)
        return RunViewStepBenchmark(window, camera, renderer, true);
    if (diagnoseStartupReference)
        return RunStartupDiagnostic(window, camera, renderer, false);
    if (diagnoseStartupTemporal)
        return RunStartupDiagnostic(window, camera, renderer, true);

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
    }

    return 0;
}
