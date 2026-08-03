// ============================================================================
//  main.cpp  —  진입점 (WinMain)
// ----------------------------------------------------------------------------
//  창(Window) · 카메라(Camera) · 렌더러(Renderer)를 생성·연결하고,
//  메인 루프에서 진단 장면과 단계 3 높이 프로파일 구름 패스를 그린다.
// ============================================================================
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <objbase.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "Window.h"
#include "Camera.h"
#include "Renderer.h"

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
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR commandLine, int)
{
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    struct ComScope
    {
        HRESULT result;
        ~ComScope() { if (SUCCEEDED(result)) CoUninitialize(); }
    } comScope{ comResult };

    const int kWidth  = 1280;
    const int kHeight = 720;

    const bool smokeTest = commandLine &&
        wcsstr(commandLine, L"--foundation-smoke-test") != nullptr;
    const bool stage1SmokeTest = commandLine &&
        wcsstr(commandLine, L"--stage1-smoke-test") != nullptr;
    const bool stage2SmokeTest = commandLine &&
        wcsstr(commandLine, L"--stage2-smoke-test") != nullptr;
    const bool stage3SmokeTest = commandLine &&
        wcsstr(commandLine, L"--stage3-smoke-test") != nullptr;
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
                    L"VolumetricCloud - Stage 3 | 0 합성 | Y 넓은 볼륨 | N 기본 Noise | 외부 기본(F5)",
                    !smokeTest && !stage1SmokeTest && !stage2SmokeTest &&
                    !stage3SmokeTest && !noiseLabSmokeTest && !shaderHotReloadSmokeTest);
    Camera   camera;
    Renderer renderer;

    camera.SetAspect(static_cast<float>(kWidth) / kHeight);

    if (!renderer.Init(window.GetHandle(), kWidth, kHeight))
        return -1; // 초기화 실패 (오류 메시지는 Renderer가 표시)

    // 입력/리사이즈 연결
    window.SetCamera(&camera);
    window.SetRenderer(&renderer);

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

    if (noiseLabSmokeTest)
    {
        for (int mode = static_cast<int>(NoiseOutputMode::RawNoise);
             mode <= static_cast<int>(NoiseOutputMode::HeightProfile); ++mode)
        {
            renderer.SetNoiseLabOutputMode(static_cast<NoiseOutputMode>(mode));
            renderer.Render(camera, 0.0f);
            if (!renderer.ValidateNoiseLabPreviews())
                return 3;
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
    }

    return 0;
}
