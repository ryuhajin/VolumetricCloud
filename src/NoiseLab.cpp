#include "NoiseLab.h"

#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"
#include "Stage3HeightMath.h"
#include "Stage7PhaseMath.h"
#include "Stage8AmbientMath.h"

using Microsoft::WRL::ComPtr;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

namespace
{
bool IsMouseMessage(UINT message)
{
    return (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) ||
           message == WM_NCMOUSEMOVE || message == WM_NCMOUSELEAVE;
}

bool IsKeyboardMessage(UINT message)
{
    return (message >= WM_KEYFIRST && message <= WM_KEYLAST) ||
           message == WM_CHAR;
}

std::string NarrowUtf8(const std::filesystem::path& path)
{
    const std::wstring wide = path.wstring();
    const int size = WideCharToMultiByte(
        CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1)
        return {};
    std::string result(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1,
                        result.data(), size, nullptr, nullptr);
    return result;
}

std::filesystem::path TimestampDirectory(const std::filesystem::path& root)
{
    SYSTEMTIME time = {};
    GetLocalTime(&time);
    wchar_t name[32] = {};
    swprintf_s(name, L"%04u%02u%02u-%02u%02u%02u",
               time.wYear, time.wMonth, time.wDay,
               time.wHour, time.wMinute, time.wSecond);
    return root / name;
}
}

NoiseLab::~NoiseLab()
{
    Shutdown();
}

bool NoiseLab::Init(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context)
{
    m_hwnd = hwnd;
    m_device = device;
    m_context = context;
    if (!CreatePreviewTargets() || !CreateConstantBuffer())
        return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    if (!ImGui_ImplWin32_Init(hwnd) || !ImGui_ImplDX11_Init(device, context))
    {
        Shutdown();
        return false;
    }
    m_initialized = true;
    return true;
}

void NoiseLab::Shutdown()
{
    if (m_initialized)
    {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
    m_initialized = false;
    m_noiseLabCb.Reset();
    m_targets = {};
    m_context = nullptr;
    m_device = nullptr;
    m_hwnd = nullptr;
}

bool NoiseLab::CreatePreviewTargets()
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = kPreviewSize;
    desc.Height = kPreviewSize;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    for (SliceTarget& target : m_targets)
    {
        if (FAILED(m_device->CreateTexture2D(&desc, nullptr, &target.texture)) ||
            FAILED(m_device->CreateRenderTargetView(
                target.texture.Get(), nullptr, &target.rtv)) ||
            FAILED(m_device->CreateShaderResourceView(
                target.texture.Get(), nullptr, &target.srv)) ||
            FAILED(m_device->CreateTexture2D(
                &stagingDesc, nullptr, &target.staging)))
            return false;
    }
    return true;
}

bool NoiseLab::CreateConstantBuffer()
{
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(NoiseLabParameters);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    return SUCCEEDED(m_device->CreateBuffer(&desc, nullptr, &m_noiseLabCb));
}

bool NoiseLab::HandleWindowMessage(
    HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_KEYDOWN && wParam == VK_F1)
    {
        if ((lParam & (1ll << 30)) == 0)
            ToggleVisible();
        return true;
    }
    if (!m_initialized || !m_visible)
        return false;

    ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam);
    const ImGuiIO& io = ImGui::GetIO();
    return (IsMouseMessage(message) && io.WantCaptureMouse) ||
           (IsKeyboardMessage(message) && io.WantCaptureKeyboard);
}

void NoiseLab::ToggleVisible()
{
    m_visible = !m_visible;
}

void NoiseLab::UpdateEffectiveTime(float applicationTime)
{
    if (!m_hasApplicationTime)
    {
        m_lastApplicationTime = applicationTime;
        m_hasApplicationTime = true;
    }
    const float delta = std::clamp(
        applicationTime - m_lastApplicationTime, 0.0f, 0.25f);
    m_lastApplicationTime = applicationTime;
    if (!m_timePaused)
        m_effectiveTime = std::max(0.0f, m_effectiveTime + delta * m_timeScale);

    if (m_slicePlaying)
    {
        float* values = &m_parameters.normalizedSlicePosition.x;
        values[m_playAxis] += delta * m_slicePlaySpeed * m_slicePlayDirection;
        if (values[m_playAxis] >= 1.0f)
        {
            values[m_playAxis] = 1.0f;
            m_slicePlayDirection = -1.0f;
        }
        else if (values[m_playAxis] <= 0.0f)
        {
            values[m_playAxis] = 0.0f;
            m_slicePlayDirection = 1.0f;
        }
    }
    m_parameters.effectiveTime = m_effectiveTime;
}

void NoiseLab::BeginFrame(float applicationTime,
                          CloudParameters& cloudParameters,
                          LightParameters& lightParameters,
                          Stage6SunPreset& sunPreset,
                          Stage7PhasePreset& phasePreset,
                          EnvironmentParameters& environmentParameters,
                          Stage8EnvironmentPreset& environmentPreset,
                          OptimizationParameters& optimizationParameters,
                          Stage9OptimizationPreset& optimizationPreset,
                          Stage5WeatherPreset weatherPreset,
                          const WeatherMapGeneratorSettings& weatherGeneratorSettings,
                          ID3D11ShaderResourceView* weatherMapSrv,
                          const std::string& weatherMapStatus,
                          const FrameTimingSnapshot& timing,
                          bool& vsyncEnabled,
                          std::uint64_t shaderGeneration,
                          const std::string& shaderStatus,
                          const std::string& shaderError)
{
    if (!m_initialized)
        return;
    m_currentApplicationTime = applicationTime;
    if (!m_weatherGeneratorDraftInitialized)
    {
        m_weatherGeneratorDraft = weatherGeneratorSettings;
        m_weatherGeneratorDraftInitialized = true;
    }
    UpdateEffectiveTime(applicationTime);
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    if (m_visible)
        DrawControlWindow(cloudParameters, lightParameters, sunPreset, phasePreset,
                           environmentParameters, environmentPreset,
                           optimizationParameters, optimizationPreset,
                          weatherPreset, weatherGeneratorSettings,
                          weatherMapSrv, weatherMapStatus, vsyncEnabled,
                          shaderGeneration,
                          shaderStatus, shaderError);
    DrawPerformanceOverlay(timing, cloudParameters, lightParameters,
                           vsyncEnabled);
}

void NoiseLab::DrawControlWindow(CloudParameters& cloudParameters,
                                 LightParameters& lightParameters,
                                 Stage6SunPreset& sunPreset,
                                 Stage7PhasePreset& phasePreset,
                                 EnvironmentParameters& environmentParameters,
                                 Stage8EnvironmentPreset& environmentPreset,
                                 OptimizationParameters& optimizationParameters,
                                 Stage9OptimizationPreset& optimizationPreset,
                                 Stage5WeatherPreset weatherPreset,
                                 const WeatherMapGeneratorSettings& weatherGeneratorSettings,
                                 ID3D11ShaderResourceView* weatherMapSrv,
                                 const std::string& weatherMapStatus,
                                 bool& vsyncEnabled,
                                 std::uint64_t shaderGeneration,
                                 const std::string& shaderStatus,
                                 const std::string& shaderError)
{
    ImGui::SetNextWindowSize(ImVec2(620.0f, 680.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Noise Lab (F1)", &m_visible))
    {
        ImGui::End();
        return;
    }

    ImGui::Text("Shader generation: %llu",
                static_cast<unsigned long long>(shaderGeneration));
    ImGui::SameLine();
    ImGui::TextColored(shaderError.empty() ? ImVec4(0.4f, 1.0f, 0.5f, 1.0f)
                                            : ImVec4(1.0f, 0.35f, 0.3f, 1.0f),
                       "%s", shaderStatus.c_str());
    if (!shaderError.empty())
    {
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted(shaderError.c_str());
        ImGui::PopTextWrapPos();
    }

    const char* outputs[] = {
        "Raw Noise", "Threshold Density", "Final Density",
        "Height Fraction", "Height Profile", "Base Density",
        "Detail Noise", "Erosion", "Detail Sample Mask",
        "Weather Coverage", "Cloud Type", "Weather Density Modifier",
        "Weather Threshold Density", "Typed Height Profile", "Weather UV"
    };
    int output = static_cast<int>(m_parameters.outputMode);
    if (ImGui::Combo("Output", &output, outputs, 15))
        m_parameters.outputMode = static_cast<std::uint32_t>(output);

    float* slice = &m_parameters.normalizedSlicePosition.x;
    ImGui::SliderFloat3("Slice XYZ", slice, 0.0f, 1.0f, "%.3f");
    const char* axes[] = { "X", "Y", "Z" };
    ImGui::Combo("Play Axis", &m_playAxis, axes, 3);
    ImGui::Checkbox("Slice Play", &m_slicePlaying);
    ImGui::SameLine();
    ImGui::SliderFloat("Slice Speed", &m_slicePlaySpeed, 0.01f, 1.0f, "%.2f cycle/s");

    ImGui::SeparatorText("Synchronized 3D slices");
    DrawSlice("XY (fixed Z)", NoiseSliceAxis::XY, m_targets[0]);
    ImGui::SameLine();
    DrawSlice("XZ (fixed Y)", NoiseSliceAxis::XZ, m_targets[1]);
    ImGui::SameLine();
    DrawSlice("YZ (fixed X)", NoiseSliceAxis::YZ, m_targets[2]);

    const CloudParameters before = cloudParameters;
    const LightParameters lightBefore = lightParameters;
    m_weatherPreset = weatherPreset;
    m_weatherMapPreviewSrv = weatherMapSrv;
    if (ImGui::CollapsingHeader("Stage 13 Cloud Layer",
                                ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SliderFloat("Layer Bottom", &cloudParameters.cloudBottomAltitude,
                           0.0f, 10000.0f, "%.2f m");
        ImGui::SliderFloat("Layer Thickness", &cloudParameters.cloudLayerThickness,
                           100.0f, 10000.0f, "%.2f m");
        ImGui::SliderFloat("View Trace Max", &cloudParameters.maxViewTraceDistance,
                           1000.0f, 100000.0f, "%.2f m", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("View Fade Start", &cloudParameters.viewTraceFadeStartDistance,
                           0.0f, cloudParameters.maxViewTraceDistance, "%.2f m");
        int viewSteps = static_cast<int>(cloudParameters.maxViewSteps);
        if (ImGui::SliderInt("Max View Steps", &viewSteps, 1, 1024))
            cloudParameters.maxViewSteps = static_cast<std::uint32_t>(viewSteps);
        if (ImGui::Button("View 256 Steps"))
            cloudParameters.maxViewSteps = 256u;
        ImGui::SameLine();
        if (ImGui::Button("View 512 Steps"))
            cloudParameters.maxViewSteps = 512u;
        ImGui::SliderFloat("Light Trace Max", &cloudParameters.maxLightTraceDistance,
                           1000.0f, 50000.0f, "%.2f m", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Noise Lab Width", &cloudParameters.noiseLabPreviewWorldSize,
                           100.0f, 64000.0f, "%.2f m", ImGuiSliderFlags_Logarithmic);
        if (ImGui::Button("Equal Axis Diagnostic"))
            cloudParameters.noiseLabPreviewWorldSize =
                cloudParameters.cloudLayerThickness;
        ImGui::SameLine();
        if (ImGui::Button("Restore 32km Preview"))
            cloudParameters.noiseLabPreviewWorldSize = 32000.0f;

        const float previewWidth = std::max(
            cloudParameters.noiseLabPreviewWorldSize, 1.0f);
        const float previewHeight = std::max(
            cloudParameters.cloudLayerThickness, 1.0f);
        const float verticalDisplayStretch = previewWidth / previewHeight;
        ImGui::Text("XY: %.2f x %.2f km | XZ: %.2f x %.2f km",
                    previewWidth / 1000.0f, previewHeight / 1000.0f,
                    previewWidth / 1000.0f, previewWidth / 1000.0f);
        ImGui::Text("YZ: %.2f x %.2f km | XY/YZ Y stretch: %.2fx",
                    previewWidth / 1000.0f, previewHeight / 1000.0f,
                    verticalDisplayStretch);
        ImGui::TextDisabled(
            "Preview only: these buttons do not change cloud rendering or world noise.");
        ImGui::TextDisabled(
            "Equal Axis matches Layer Thickness, not Layer Bottom (cloud altitude).");

        const float targetViewStep = std::max(cloudParameters.stepSize, 1e-4f);
        const float requestedViewSteps = std::ceil(
            cloudParameters.maxViewTraceDistance / targetViewStep);
        const float executedWorstCaseSteps = std::max(
            std::min(requestedViewSteps,
                     static_cast<float>(std::max(cloudParameters.maxViewSteps, 1u))),
            1.0f);
        const float worstCaseActualStep =
            cloudParameters.maxViewTraceDistance / executedWorstCaseSteps;
        const float baseWavelength = 1.0f /
            std::max(cloudParameters.baseNoiseScale, 1e-6f);
        const float detailWavelength = 1.0f /
            std::max(cloudParameters.detailNoiseScale, 1e-6f);
        const bool viewStepCapped = requestedViewSteps >
            static_cast<float>(std::max(cloudParameters.maxViewSteps, 1u));
        ImGui::Text("Max-trace step: %.1f m | target %.1f m | %s",
                    worstCaseActualStep, targetViewStep,
                    viewStepCapped ? "MAX STEPS CAPPED" : "target preserved");
        ImGui::Text("Base %.0f m: %.1f samples | Detail %.0f m: %.1f samples",
                    baseWavelength, baseWavelength / worstCaseActualStep,
                    detailWavelength, detailWavelength / worstCaseActualStep);
        ImGui::SliderFloat("Extinction", &cloudParameters.extinctionCoefficient,
                           0.0001f, 0.005f, "%.6f /m", ImGuiSliderFlags_Logarithmic);
        const float referenceOpticalDepth = 0.5f *
            cloudParameters.densityMultiplier * cloudParameters.extinctionCoefficient *
            3000.0f;
        ImGui::Text("3km reference: optical depth %.3f | T %.3f",
                    referenceOpticalDepth, std::exp(-referenceOpticalDepth));
        ImGui::Text("Layer: %.2f - %.2f km | View: %.1f km",
                    cloudParameters.cloudBottomAltitude / 1000.0f,
                    (cloudParameters.cloudBottomAltitude +
                     cloudParameters.cloudLayerThickness) / 1000.0f,
                    cloudParameters.maxViewTraceDistance / 1000.0f);
    }
    cloudParameters.cloudBottomAltitude = std::max(
        cloudParameters.cloudBottomAltitude, 0.0f);
    cloudParameters.cloudLayerThickness = std::max(
        cloudParameters.cloudLayerThickness, 100.0f);
    cloudParameters.maxViewTraceDistance = std::max(
        cloudParameters.maxViewTraceDistance, 1000.0f);
    cloudParameters.maxViewSteps = std::max(cloudParameters.maxViewSteps, 1u);
    cloudParameters.viewTraceFadeStartDistance = std::clamp(
        cloudParameters.viewTraceFadeStartDistance, 0.0f,
        cloudParameters.maxViewTraceDistance);
    cloudParameters.maxLightTraceDistance = std::max(
        cloudParameters.maxLightTraceDistance, 1000.0f);
    cloudParameters.noiseLabPreviewWorldSize = std::max(
        cloudParameters.noiseLabPreviewWorldSize, 100.0f);
    cloudParameters.extinctionCoefficient = std::clamp(
        cloudParameters.extinctionCoefficient, 0.0001f, 0.005f);
    if (ImGui::CollapsingHeader("Weather map", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const char* presets[] = {
            "F2 Uniform Legacy", "F3 Periodic Perlin", "F4 Channel Debug"
        };
        int presetIndex = static_cast<int>(weatherPreset);
        if (ImGui::Combo("Weather Preset", &presetIndex, presets, 3))
        {
            m_weatherPresetRequest = presetIndex;
            m_weatherPreset = static_cast<Stage5WeatherPreset>(presetIndex);
        }
        if (m_weatherMapPreviewSrv)
        {
            ImGui::TextUnformatted("Actual RGBA texture (R coverage / G type / B density)");
            ImGui::Image(ImTextureRef(static_cast<ImTextureID>(
                reinterpret_cast<std::uintptr_t>(m_weatherMapPreviewSrv))),
                ImVec2(256.0f, 256.0f));
        }
        ImGui::SliderFloat("Weather World Size", &cloudParameters.weatherMapWorldSize,
                           1000.0f, 64000.0f, "%.1f m", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Weather Wind Speed", &cloudParameters.weatherMapWindSpeed,
                           0.0f, 40.0f, "%.2f m/s");
        ImGui::DragFloat2("Weather Offset", &cloudParameters.weatherMapOffset.x,
                          0.01f, -10.0f, 10.0f, "%.2f cycle");
        if (ImGui::Button("Reset Weather Transform"))
        {
            cloudParameters.weatherMapWorldSize = 32000.0f;
            cloudParameters.weatherMapWindSpeed = 8.0f;
            cloudParameters.weatherMapOffset = { 0.0f, 0.0f };
        }
    }

    const bool generatorEnabled =
        m_weatherPreset == Stage5WeatherPreset::PeriodicPerlin;
    bool generatorChanged = false;
    if (ImGui::CollapsingHeader(
            "Periodic Perlin Generator", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::TextWrapped(
            "CPU RGBA map -> DEFAULT texture UpdateSubresource (max 10 Hz)");
        ImGui::TextWrapped("%s", weatherMapStatus.c_str());
        if (!generatorEnabled)
        {
            ImGui::TextColored(
                ImVec4(0.45f, 0.75f, 1.0f, 1.0f),
                "Settings are editable; switch to F3 to preview Periodic Perlin.");
        }

        if (ImGui::CollapsingHeader("R Coverage"))
        {
            generatorChanged |= DrawPeriodicChannelFields(
                "R Coverage", m_weatherGeneratorDraft.coverage);
            generatorChanged |= ImGui::SliderFloat(
                "Coverage Threshold", &m_weatherGeneratorDraft.coverageThreshold,
                0.0f, 1.0f, "%.3f");
            generatorChanged |= ImGui::SliderFloat(
                "Coverage Softness", &m_weatherGeneratorDraft.coverageSoftness,
                0.02f, 0.8f, "%.3f");
        }
        if (ImGui::CollapsingHeader("G Cloud Type"))
        {
            generatorChanged |= DrawPeriodicChannelFields(
                "G Cloud Type", m_weatherGeneratorDraft.cloudType);
        }
        if (ImGui::CollapsingHeader("B Density"))
        {
            generatorChanged |= DrawPeriodicChannelFields(
                "B Density", m_weatherGeneratorDraft.density);
            generatorChanged |= ImGui::SliderFloat(
                "Density Coverage Influence",
                &m_weatherGeneratorDraft.densityCoverageInfluence,
                0.0f, 1.0f, "%.3f");
        }

        ImGui::Checkbox("Live Update", &m_weatherGeneratorLiveUpdate);
        if (ImGui::Button("Apply Now"))
            QueueWeatherGeneratorRequest(true);
        ImGui::SameLine();
        if (ImGui::Button("Reset Generator"))
        {
            m_weatherGeneratorDraft = WeatherMapGeneratorSettings{};
            m_weatherGeneratorDirty = true;
            QueueWeatherGeneratorRequest(true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Next Seeds"))
        {
            const auto nextSeed = [](std::uint32_t seed)
            {
                return seed * 1664525u + 1013904223u;
            };
            m_weatherGeneratorDraft.coverage.seed =
                nextSeed(m_weatherGeneratorDraft.coverage.seed);
            m_weatherGeneratorDraft.cloudType.seed =
                nextSeed(m_weatherGeneratorDraft.cloudType.seed);
            m_weatherGeneratorDraft.density.seed =
                nextSeed(m_weatherGeneratorDraft.density.seed);
            m_weatherGeneratorDirty = true;
            QueueWeatherGeneratorRequest(true);
        }

        if (!WeatherMapGeneratorSettingsEqual(
                weatherGeneratorSettings, m_weatherGeneratorDraft))
        {
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f),
                               "Generator has unapplied values.");
        }
    }

    if (generatorChanged)
        m_weatherGeneratorDirty = true;
    m_weatherGeneratorDraft = SanitizeWeatherMapGeneratorSettings(
        m_weatherGeneratorDraft);
    if (generatorEnabled && m_weatherGeneratorDirty &&
        m_weatherGeneratorLiveUpdate)
    {
        QueueWeatherGeneratorRequest(!ImGui::IsAnyItemActive());
    }
    cloudParameters.weatherMapWorldSize = std::max(
        cloudParameters.weatherMapWorldSize, 1e-4f);
    cloudParameters.weatherMapWindSpeed = std::max(
        cloudParameters.weatherMapWindSpeed, 0.0f);

    if (ImGui::CollapsingHeader(
            "Shared cloud parameters", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SliderFloat("Noise Scale", &cloudParameters.baseNoiseScale,
                           0.0001f, 0.02f, "%.5f cycle/m", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Coverage", &cloudParameters.coverage, 0.0f, 1.0f);
        ImGui::SliderFloat("Density", &cloudParameters.densityMultiplier, 0.0f, 2.0f);
        ImGui::DragFloat("Noise Offset", &cloudParameters.noiseOffset,
                         0.01f, -20.0f, 20.0f);
        ImGui::DragFloat3("Wind Direction", &cloudParameters.windDirection.x,
                          0.01f, -1.0f, 1.0f);
        ImGui::SliderFloat("Wind Speed", &cloudParameters.windSpeed,
                           0.0f, 40.0f, "%.2f m/s");
    }

    if (ImGui::CollapsingHeader("Height profile", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SliderFloat("Bottom Fade End", &cloudParameters.bottomFadeEnd,
                           0.01f, 0.99f, "%.2f normalized height");
        ImGui::SliderFloat("Top Fade Start", &cloudParameters.topFadeStart,
                           0.01f, 0.99f, "%.2f normalized height");
        if (cloudParameters.bottomFadeEnd > cloudParameters.topFadeStart)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f),
                               "Fade ranges overlap: the full-density plateau disappears.");
        }

        std::array<float, 64> profileCurve = {};
        for (std::size_t index = 0; index < profileCurve.size(); ++index)
        {
            const float height = static_cast<float>(index) /
                static_cast<float>(profileCurve.size() - 1);
            profileCurve[index] = stage3::EvaluateHeightProfileFromFraction(
                height, cloudParameters.bottomFadeEnd, cloudParameters.topFadeStart);
        }
        ImGui::PlotLines("Profile curve", profileCurve.data(),
                         static_cast<int>(profileCurve.size()), 0,
                         "bottom 0 -> top 1", 0.0f, 1.0f, ImVec2(0.0f, 80.0f));
        const float selectedWorldY = cloudParameters.cloudBottomAltitude +
            cloudParameters.cloudLayerThickness *
            m_parameters.normalizedSlicePosition.y;
        const float selectedHeight = stage3::EvaluateHeightFraction(
            selectedWorldY, cloudParameters.cloudBottomAltitude,
            cloudParameters.cloudBottomAltitude + cloudParameters.cloudLayerThickness);
        const float selectedProfile = stage3::EvaluateHeightProfileFromFraction(
            selectedHeight, cloudParameters.bottomFadeEnd, cloudParameters.topFadeStart);
        ImGui::Text("Selected Y: fraction %.3f, profile %.3f",
                    selectedHeight, selectedProfile);
        if (ImGui::Button("Reset Bottom Fade"))
            cloudParameters.bottomFadeEnd = 0.20f;
        ImGui::SameLine();
        if (ImGui::Button("Reset Top Fade"))
            cloudParameters.topFadeStart = 0.80f;
    }
    cloudParameters.bottomFadeEnd = std::clamp(cloudParameters.bottomFadeEnd, 0.01f, 0.99f);
    cloudParameters.topFadeStart = std::clamp(cloudParameters.topFadeStart, 0.01f, 0.99f);

    if (ImGui::CollapsingHeader("Detail erosion", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SliderFloat("Detail Scale", &cloudParameters.detailNoiseScale,
                           0.001f, 0.1f, "%.4f cycle/m", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Erosion Strength", &cloudParameters.detailErosionStrength,
                           0.0f, 1.0f, "%.3f");
        ImGui::SliderFloat("Detail Wind Speed", &cloudParameters.detailWindSpeed,
                           0.0f, 60.0f, "%.2f m/s");
        ImGui::DragFloat("Detail Offset", &cloudParameters.detailNoiseOffset,
                         0.01f, -50.0f, 50.0f, "%.2f cycle");
        ImGui::SliderFloat("Detail LOD Full Until",
                           &cloudParameters.detailLodFadeStartDistance,
                           0.0f, cloudParameters.maxViewTraceDistance,
                           "%.0f m");
        ImGui::SliderFloat("Detail LOD Skip After",
                           &cloudParameters.detailLodFadeEndDistance,
                           cloudParameters.detailLodFadeStartDistance,
                           cloudParameters.maxViewTraceDistance, "%.0f m");
        if (ImGui::Button("Reset Detail"))
        {
            cloudParameters.detailNoiseScale = 0.0025f;
            cloudParameters.detailErosionStrength = 0.25f;
            cloudParameters.detailWindSpeed = 18.0f;
            cloudParameters.detailNoiseOffset = 17.3f;
            cloudParameters.detailLodFadeStartDistance = 8000.0f;
            cloudParameters.detailLodFadeEndDistance = 20000.0f;
        }
        ImGui::TextDisabled("F9 Off / F10 Default / F11 Fine / F12 Strong");
    }
    cloudParameters.detailNoiseScale = std::max(cloudParameters.detailNoiseScale, 0.001f);
    cloudParameters.detailErosionStrength = std::clamp(
        cloudParameters.detailErosionStrength, 0.0f, 1.0f);
    cloudParameters.detailWindSpeed = std::max(cloudParameters.detailWindSpeed, 0.0f);
    cloudParameters.detailLodFadeStartDistance = std::clamp(
        cloudParameters.detailLodFadeStartDistance, 0.0f,
        cloudParameters.maxViewTraceDistance);
    cloudParameters.detailLodFadeEndDistance = std::clamp(
        cloudParameters.detailLodFadeEndDistance,
        cloudParameters.detailLodFadeStartDistance,
        cloudParameters.maxViewTraceDistance);

    if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Checkbox("VSync", &vsyncEnabled);
        ImGui::TextDisabled("CPU Frame includes Present/VSync wait.");
        ImGui::TextDisabled("GPU Frame excludes Present; compare ray cost with GPU Cloud.");
    }

    if (ImGui::CollapsingHeader("Stage 9 Optimization",
                                ImGuiTreeNodeFlags_DefaultOpen))
    {
        static const char* presetNames[] = {
            "Off (Stage 8)", "Early Exit Only", "Empty Space Only",
            "Balanced", "Custom"
        };
        const int presetIndex = std::clamp(
            static_cast<int>(optimizationPreset), 0, 4);
        ImGui::Text("Current: %s", presetNames[presetIndex]);
        if (ImGui::Button("Optimization Off"))
        {
            stage9optimization::ApplyPreset(
                optimizationParameters, Stage9OptimizationPreset::Off);
            optimizationPreset = Stage9OptimizationPreset::Off;
        }
        ImGui::SameLine();
        if (ImGui::Button("Early Exit Only"))
        {
            stage9optimization::ApplyPreset(
                optimizationParameters, Stage9OptimizationPreset::EarlyExitOnly);
            optimizationPreset = Stage9OptimizationPreset::EarlyExitOnly;
        }
        if (ImGui::Button("Empty Space Only"))
        {
            stage9optimization::ApplyPreset(
                optimizationParameters, Stage9OptimizationPreset::EmptySpaceOnly);
            optimizationPreset = Stage9OptimizationPreset::EmptySpaceOnly;
        }
        ImGui::SameLine();
        if (ImGui::Button("Balanced Optimization"))
        {
            stage9optimization::ApplyPreset(
                optimizationParameters, Stage9OptimizationPreset::Balanced);
            optimizationPreset = Stage9OptimizationPreset::Balanced;
        }

        bool changed = false;
        bool earlyExit = optimizationParameters.earlyExitEnabled != 0;
        bool supportPrecheck = optimizationParameters.supportPrecheckEnabled != 0;
        bool emptySkipping = optimizationParameters.emptySpaceSkippingEnabled != 0;
        changed |= ImGui::Checkbox("View Early Exit", &earlyExit);
        changed |= ImGui::Checkbox("Height/Weather Precheck", &supportPrecheck);
        changed |= ImGui::Checkbox("Adaptive Empty-space", &emptySkipping);
        changed |= ImGui::SliderFloat(
            "Transmittance Threshold", &cloudParameters.transmittanceThreshold,
            0.0f, 0.1f, "%.4f");
        changed |= ImGui::SliderFloat(
            "Base Density Epsilon", &optimizationParameters.baseDensityEpsilon,
            0.000001f, 0.05f, "%.5f", ImGuiSliderFlags_Logarithmic);
        changed |= ImGui::SliderFloat(
            "Coarse Step Multiplier", &optimizationParameters.coarseStepMultiplier,
            1.0f, 8.0f, "%.1fx");
        int emptySamples = static_cast<int>(
            optimizationParameters.emptySamplesBeforeCoarse);
        if (ImGui::SliderInt("Empty Samples Before Coarse", &emptySamples, 1, 8))
        {
            optimizationParameters.emptySamplesBeforeCoarse =
                static_cast<std::uint32_t>(emptySamples);
            changed = true;
        }
        optimizationParameters.earlyExitEnabled = earlyExit ? 1u : 0u;
        optimizationParameters.supportPrecheckEnabled = supportPrecheck ? 1u : 0u;
        optimizationParameters.emptySpaceSkippingEnabled = emptySkipping ? 1u : 0u;
        cloudParameters.transmittanceThreshold = std::clamp(
            cloudParameters.transmittanceThreshold, 0.0f, 0.1f);
        optimizationParameters = stage9optimization::Sanitize(
            optimizationParameters);
        if (changed)
            optimizationPreset = Stage9OptimizationPreset::Custom;
        ImGui::TextDisabled("Composite+On uses mainOptimized; debug/Off uses mainLegacy.");
    }

    if (ImGui::CollapsingHeader("Directional light", ImGuiTreeNodeFlags_DefaultOpen))
    {
        static const char* sunPresetNames[] = {
            "Noon", "Low East", "Low West", "Custom Sun"
        };
        const int sunPresetIndex = std::clamp(static_cast<int>(sunPreset), 0, 3);
        ImGui::Text("Current: %s", sunPresetNames[sunPresetIndex]);
        if (ImGui::Button("Noon"))
        {
            lightParameters.directionToSun =
                stage6light::Preset(Stage6SunPreset::Noon).directionToSun;
            sunPreset = Stage6SunPreset::Noon;
        }
        ImGui::SameLine();
        if (ImGui::Button("Low East"))
        {
            lightParameters.directionToSun =
                stage6light::Preset(Stage6SunPreset::LowEast).directionToSun;
            sunPreset = Stage6SunPreset::LowEast;
        }
        ImGui::SameLine();
        if (ImGui::Button("Low West"))
        {
            lightParameters.directionToSun =
                stage6light::Preset(Stage6SunPreset::LowWest).directionToSun;
            sunPreset = Stage6SunPreset::LowWest;
        }

        float azimuth = 0.0f;
        float elevation = 0.0f;
        stage6light::AnglesFromDirection(
            lightParameters.directionToSun, azimuth, elevation);
        bool lightChanged = false;
        lightChanged |= ImGui::SliderFloat("Sun Azimuth", &azimuth,
                                            -180.0f, 180.0f, "%.1f deg");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##Azimuth"))
        {
            azimuth = 45.0f;
            lightChanged = true;
        }
        lightChanged |= ImGui::SliderFloat("Sun Elevation", &elevation,
                                            0.0f, 90.0f, "%.1f deg");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##Elevation"))
        {
            elevation = 70.0f;
            lightChanged = true;
        }
        if (lightChanged)
            lightParameters.directionToSun =
                stage6light::DirectionFromAngles(azimuth, elevation);

        lightChanged |= ImGui::ColorEdit3("Sun Color", &lightParameters.sunColor.x,
                                           ImGuiColorEditFlags_Float);
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##SunColor"))
        {
            lightParameters.sunColor = { 1.0f, 0.95f, 0.85f };
            lightChanged = true;
        }
        lightChanged |= ImGui::SliderFloat("Sun Intensity", &lightParameters.sunIntensity,
                                            0.0f, 5.0f, "%.2f");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##SunIntensity"))
        {
            lightParameters.sunIntensity = 1.0f;
            lightChanged = true;
        }
        lightChanged |= ImGui::SliderFloat("Single Scattering Albedo",
                                            &lightParameters.singleScatteringAlbedo,
                                            0.0f, 1.0f, "%.3f");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##Scatter"))
        {
            lightParameters.singleScatteringAlbedo = 0.90f;
            lightChanged = true;
        }
        int lightSteps = static_cast<int>(lightParameters.maxLightSteps);
        if (ImGui::SliderInt("Max Light Steps", &lightSteps, 1, 64))
        {
            lightParameters.maxLightSteps = static_cast<std::uint32_t>(lightSteps);
            lightChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##LightSteps"))
        {
            lightParameters.maxLightSteps = 32;
            lightChanged = true;
        }
        lightChanged |= ImGui::SliderFloat("Light Step Size", &lightParameters.lightStepSize,
                                            10.0f, 2000.0f, "%.1f m",
                                            ImGuiSliderFlags_Logarithmic);
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##LightStepSize"))
        {
            lightParameters.lightStepSize = 250.0f;
            lightChanged = true;
        }
        lightChanged |= ImGui::SliderFloat("Light Ray Bias", &lightParameters.lightRayBias,
                                            0.01f, 10.0f, "%.2f m");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##LightBias"))
        {
            lightParameters.lightRayBias = 1.0f;
            lightChanged = true;
        }
        if (lightChanged)
            sunPreset = Stage6SunPreset::Custom;
        lightParameters = stage6light::Sanitize(lightParameters);
        ImGui::TextDisabled("Light Ray samples Base Density only; Detail is omitted.");
    }

    if (ImGui::CollapsingHeader("Phase Function", ImGuiTreeNodeFlags_DefaultOpen))
    {
        static const char* phasePresetNames[] = {
            "Off", "Balanced", "Silver Lining", "Backscatter Check", "Custom"
        };
        const int phasePresetIndex = std::clamp(
            static_cast<int>(phasePreset), 0, 4);
        ImGui::Text("Current: %s", phasePresetNames[phasePresetIndex]);

        if (ImGui::Button("Phase Off"))
        {
            stage6light::ApplyPhasePreset(
                lightParameters, Stage7PhasePreset::Off);
            phasePreset = Stage7PhasePreset::Off;
        }
        ImGui::SameLine();
        if (ImGui::Button("Balanced"))
        {
            stage6light::ApplyPhasePreset(
                lightParameters, Stage7PhasePreset::Balanced);
            phasePreset = Stage7PhasePreset::Balanced;
        }
        ImGui::SameLine();
        if (ImGui::Button("Silver Lining"))
        {
            stage6light::ApplyPhasePreset(
                lightParameters, Stage7PhasePreset::SilverLining);
            phasePreset = Stage7PhasePreset::SilverLining;
        }
        ImGui::SameLine();
        if (ImGui::Button("Backscatter Check"))
        {
            stage6light::ApplyPhasePreset(
                lightParameters, Stage7PhasePreset::BackscatterCheck);
            phasePreset = Stage7PhasePreset::BackscatterCheck;
        }

        bool phaseEditedManually = false;
        bool phaseEnabled = lightParameters.phaseEnabled >= 0.5f;
        if (ImGui::Checkbox("Enable Phase Function", &phaseEnabled))
        {
            lightParameters.phaseEnabled = phaseEnabled ? 1.0f : 0.0f;
            phaseEditedManually = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##PhaseEnabled"))
        {
            lightParameters.phaseEnabled = 0.0f;
            phaseEditedManually = true;
        }

        phaseEditedManually |= ImGui::SliderFloat(
            "Forward Scattering G", &lightParameters.forwardScatteringG,
            0.0f, 0.95f, "%.3f");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##ForwardG"))
        {
            lightParameters.forwardScatteringG = 0.65f;
            phaseEditedManually = true;
        }
        phaseEditedManually |= ImGui::SliderFloat(
            "Backward Scattering G", &lightParameters.backwardScatteringG,
            -0.95f, 0.0f, "%.3f");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##BackwardG"))
        {
            lightParameters.backwardScatteringG = -0.25f;
            phaseEditedManually = true;
        }
        phaseEditedManually |= ImGui::SliderFloat(
            "Phase Blend", &lightParameters.phaseBlend, 0.0f, 1.0f, "%.3f");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##PhaseBlend"))
        {
            lightParameters.phaseBlend = 0.80f;
            phaseEditedManually = true;
        }
        phaseEditedManually |= ImGui::SliderFloat(
            "Phase Intensity", &lightParameters.phaseIntensity,
            0.0f, 1.0f, "%.3f");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##PhaseIntensity"))
        {
            lightParameters.phaseIntensity = 0.25f;
            phaseEditedManually = true;
        }
        if (ImGui::Button("Reset All Phase"))
        {
            stage6light::ApplyPhasePreset(
                lightParameters, Stage7PhasePreset::Off);
            phasePreset = Stage7PhasePreset::Off;
        }

        lightParameters = stage6light::Sanitize(lightParameters);
        if (phaseEditedManually)
            phasePreset = Stage7PhasePreset::Custom;

        std::array<float, 128> forwardCurve = {};
        std::array<float, 128> backwardCurve = {};
        std::array<float, 128> dualCurve = {};
        std::array<float, 128> appliedCurve = {};
        for (std::size_t index = 0; index < forwardCurve.size(); ++index)
        {
            const float cosTheta = -1.0f + 2.0f * static_cast<float>(index) /
                static_cast<float>(forwardCurve.size() - 1);
            forwardCurve[index] = stage7::HenyeyGreenstein(
                cosTheta, lightParameters.forwardScatteringG);
            backwardCurve[index] = stage7::HenyeyGreenstein(
                cosTheta, lightParameters.backwardScatteringG);
            dualCurve[index] = backwardCurve[index] +
                (forwardCurve[index] - backwardCurve[index]) *
                lightParameters.phaseBlend;
            const float boundedDual = std::clamp(
                dualCurve[index], 0.0f, stage7::kMaxPhaseFactor);
            appliedCurve[index] = lightParameters.phaseEnabled >= 0.5f
                ? 1.0f + (boundedDual - 1.0f) * lightParameters.phaseIntensity
                : 1.0f;
        }
        ImGui::PlotLines("Forward HG", forwardCurve.data(),
                         static_cast<int>(forwardCurve.size()), 0,
                         "-1 opposite  ->  +1 toward sun", 0.0f, 16.0f,
                         ImVec2(0.0f, 70.0f));
        ImGui::PlotLines("Backward HG", backwardCurve.data(),
                         static_cast<int>(backwardCurve.size()), 0,
                         "-1 opposite  ->  +1 toward sun", 0.0f, 16.0f,
                         ImVec2(0.0f, 70.0f));
        ImGui::PlotLines("Dual Lobe", dualCurve.data(),
                         static_cast<int>(dualCurve.size()), 0,
                         "Backward/Forward mixed by Phase Blend", 0.0f, 16.0f,
                         ImVec2(0.0f, 70.0f));
        ImGui::PlotLines("Applied Factor", appliedCurve.data(),
                         static_cast<int>(appliedCurve.size()), 0,
                         "1 = Stage 6 isotropic", 0.0f, 16.0f,
                         ImVec2(0.0f, 70.0f));
        ImGui::TextDisabled(
            "cosTheta +1: camera looks toward sun / -1: opposite direction");
    }

    if (ImGui::CollapsingHeader(
            "Environment & Multiple Scattering", ImGuiTreeNodeFlags_DefaultOpen))
    {
        static const char* presetNames[] = {
            "Off", "Balanced", "Strong Fill", "Ground Check", "Custom"
        };
        const int presetIndex = std::clamp(
            static_cast<int>(environmentPreset), 0, 4);
        ImGui::Text("Current: %s", presetNames[presetIndex]);

        if (ImGui::Button("Environment Off"))
        {
            stage8environment::ApplyPreset(
                environmentParameters, Stage8EnvironmentPreset::Off);
            environmentPreset = Stage8EnvironmentPreset::Off;
        }
        ImGui::SameLine();
        if (ImGui::Button("Balanced Ambient"))
        {
            stage8environment::ApplyPreset(
                environmentParameters, Stage8EnvironmentPreset::Balanced);
            environmentPreset = Stage8EnvironmentPreset::Balanced;
        }
        ImGui::SameLine();
        if (ImGui::Button("Strong Fill"))
        {
            stage8environment::ApplyPreset(
                environmentParameters, Stage8EnvironmentPreset::StrongFill);
            environmentPreset = Stage8EnvironmentPreset::StrongFill;
        }
        ImGui::SameLine();
        if (ImGui::Button("Ground Check"))
        {
            stage8environment::ApplyPreset(
                environmentParameters, Stage8EnvironmentPreset::GroundCheck);
            environmentPreset = Stage8EnvironmentPreset::GroundCheck;
        }

        bool edited = false;
        edited |= ImGui::ColorEdit3(
            "Sky Color", &environmentParameters.skyColor.x,
            ImGuiColorEditFlags_Float);
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##SkyColor"))
        {
            environmentParameters.skyColor = { 0.35f, 0.50f, 0.75f };
            edited = true;
        }
        edited |= ImGui::SliderFloat(
            "Sky Strength", &environmentParameters.skyStrength,
            0.0f, 1.0f, "%.3f");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##SkyStrength"))
        {
            environmentParameters.skyStrength = 0.20f;
            edited = true;
        }
        edited |= ImGui::ColorEdit3(
            "Ground Color", &environmentParameters.groundColor.x,
            ImGuiColorEditFlags_Float);
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##GroundColor"))
        {
            environmentParameters.groundColor = { 0.18f, 0.12f, 0.08f };
            edited = true;
        }
        edited |= ImGui::SliderFloat(
            "Ground Strength", &environmentParameters.groundStrength,
            0.0f, 1.0f, "%.3f");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##GroundStrength"))
        {
            environmentParameters.groundStrength = 0.08f;
            edited = true;
        }
        edited |= ImGui::SliderFloat(
            "Ambient Occlusion Strength",
            &environmentParameters.ambientOcclusionStrength,
            0.0f, 8.0f, "%.3f");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##AmbientOcclusion"))
        {
            environmentParameters.ambientOcclusionStrength = 1.25f;
            edited = true;
        }
        edited |= ImGui::SliderFloat(
            "Ambient Height Influence",
            &environmentParameters.ambientHeightInfluence,
            0.0f, 1.0f, "%.3f");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##AmbientHeight"))
        {
            environmentParameters.ambientHeightInfluence = 0.65f;
            edited = true;
        }

        bool multipleEnabled =
            environmentParameters.multipleScatteringEnabled >= 0.5f;
        if (ImGui::Checkbox("Enable Multiple Scattering", &multipleEnabled))
        {
            environmentParameters.multipleScatteringEnabled =
                multipleEnabled ? 1.0f : 0.0f;
            edited = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##MultipleEnabled"))
        {
            environmentParameters.multipleScatteringEnabled = 1.0f;
            edited = true;
        }
        int octaves = static_cast<int>(
            environmentParameters.multipleScatteringOctaves);
        if (ImGui::SliderInt("Scattering Octaves", &octaves, 0, 4))
        {
            environmentParameters.multipleScatteringOctaves =
                static_cast<std::uint32_t>(octaves);
            edited = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##ScatteringOctaves"))
        {
            environmentParameters.multipleScatteringOctaves = 2u;
            edited = true;
        }
        edited |= ImGui::SliderFloat(
            "Scattering Attenuation",
            &environmentParameters.multipleScatteringAttenuation,
            0.0f, 1.0f, "%.3f");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##ScatteringAttenuation"))
        {
            environmentParameters.multipleScatteringAttenuation = 0.35f;
            edited = true;
        }
        edited |= ImGui::SliderFloat(
            "Scattering Extinction Factor",
            &environmentParameters.multipleScatteringExtinctionFactor,
            0.0f, 1.0f, "%.3f");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##ScatteringExtinction"))
        {
            environmentParameters.multipleScatteringExtinctionFactor = 0.50f;
            edited = true;
        }
        edited |= ImGui::SliderFloat(
            "Scattering Phase Factor",
            &environmentParameters.multipleScatteringPhaseFactor,
            0.0f, 1.0f, "%.3f");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##ScatteringPhase"))
        {
            environmentParameters.multipleScatteringPhaseFactor = 0.50f;
            edited = true;
        }
        if (ImGui::Button("Reset All Environment"))
        {
            stage8environment::ApplyPreset(
                environmentParameters, Stage8EnvironmentPreset::Balanced);
            environmentPreset = Stage8EnvironmentPreset::Balanced;
        }

        environmentParameters =
            stage8environment::Sanitize(environmentParameters);
        if (edited)
            environmentPreset = Stage8EnvironmentPreset::Custom;

        std::array<float, 64> skyWeights = {};
        std::array<float, 64> groundWeights = {};
        for (std::size_t index = 0; index < skyWeights.size(); ++index)
        {
            const float height = static_cast<float>(index) /
                static_cast<float>(skyWeights.size() - 1);
            const stage8::Weights weights = stage8::EvaluateWeights(
                height, 0.0f, environmentParameters);
            skyWeights[index] = weights.sky;
            groundWeights[index] = weights.ground;
        }
        ImGui::PlotLines("Sky Height Weight", skyWeights.data(),
                         static_cast<int>(skyWeights.size()), 0,
                         "bottom 0 -> top 1", 0.0f, 1.0f,
                         ImVec2(0.0f, 60.0f));
        ImGui::PlotLines("Ground Height Weight", groundWeights.data(),
                         static_cast<int>(groundWeights.size()), 0,
                         "bottom 0 -> top 1", 0.0f, 1.0f,
                         ImVec2(0.0f, 60.0f));
        ImGui::TextDisabled(
            "Analytic colors only; no Cube Map or indirect-light texture.");
    }

    if (ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Checkbox("Pause Time", &m_timePaused);
        ImGui::SliderFloat("Time Scale", &m_timeScale, 0.0f, 4.0f);
        if (m_timePaused)
        {
            if (ImGui::DragFloat("Manual Time", &m_effectiveTime,
                                 0.02f, 0.0f, 10000.0f))
                m_parameters.effectiveTime = m_effectiveTime;
        }
        if (ImGui::Button("Reset Time"))
        {
            m_effectiveTime = 0.0f;
            m_parameters.effectiveTime = 0.0f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset Noise"))
        {
            cloudParameters.baseNoiseScale = 0.00035f;
            cloudParameters.coverage = 0.55f;
            cloudParameters.densityMultiplier = 0.65f;
            cloudParameters.windDirection = { 0.9701425f, 0.0f, 0.2425356f };
            cloudParameters.windSpeed = 12.0f;
            cloudParameters.noiseOffset = 0.0f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Export 4 PNG + JSON"))
            m_exportRequested = true;
        if (!m_exportStatus.empty())
            ImGui::TextWrapped("%s", m_exportStatus.c_str());
    }

    m_parametersChanged = m_parametersChanged ||
        std::memcmp(&before, &cloudParameters, sizeof(CloudParameters)) != 0;
    const bool sunSettingsChanged =
        lightBefore.directionToSun.x != lightParameters.directionToSun.x ||
        lightBefore.directionToSun.y != lightParameters.directionToSun.y ||
        lightBefore.directionToSun.z != lightParameters.directionToSun.z ||
        lightBefore.sunIntensity != lightParameters.sunIntensity ||
        lightBefore.sunColor.x != lightParameters.sunColor.x ||
        lightBefore.sunColor.y != lightParameters.sunColor.y ||
        lightBefore.sunColor.z != lightParameters.sunColor.z ||
        lightBefore.singleScatteringAlbedo != lightParameters.singleScatteringAlbedo ||
        lightBefore.maxLightSteps != lightParameters.maxLightSteps ||
        lightBefore.lightStepSize != lightParameters.lightStepSize ||
        lightBefore.lightRayBias != lightParameters.lightRayBias;
    if (sunSettingsChanged &&
        sunPreset != Stage6SunPreset::Noon &&
        sunPreset != Stage6SunPreset::LowEast &&
        sunPreset != Stage6SunPreset::LowWest)
        sunPreset = Stage6SunPreset::Custom;

    ImGui::End();
}

bool NoiseLab::DrawPeriodicChannelFields(
    const char* label, PeriodicChannelSettings& settings)
{
    ImGui::PushID(label);
    bool changed = false;
    changed |= ImGui::InputScalar(
        "Seed", ImGuiDataType_U32, &settings.seed, nullptr, nullptr, "%u");
    int macroPeriod = static_cast<int>(settings.macroPeriod);
    int detailPeriod = static_cast<int>(settings.detailPeriod);
    if (ImGui::SliderInt("Macro Period", &macroPeriod, 1, 8))
    {
        settings.macroPeriod = static_cast<std::uint32_t>(macroPeriod);
        changed = true;
    }
    if (ImGui::SliderInt("Detail Period", &detailPeriod, 2, 16))
    {
        settings.detailPeriod = static_cast<std::uint32_t>(detailPeriod);
        changed = true;
    }
    changed |= ImGui::SliderFloat(
        "Detail Weight", &settings.detailWeight, 0.0f, 1.0f, "%.3f");
    changed |= ImGui::SliderFloat(
        "Bias", &settings.bias, -0.5f, 0.5f, "%.3f");
    changed |= ImGui::SliderFloat(
        "Contrast", &settings.contrast, 0.25f, 3.0f, "%.3f");
    ImGui::PopID();
    return changed;
}

void NoiseLab::QueueWeatherGeneratorRequest(bool force)
{
    if (!force)
    {
        if (!m_weatherGeneratorLiveUpdate ||
            m_currentApplicationTime - m_weatherGeneratorLastRequestTime < 0.1f)
            return;
    }
    m_weatherGeneratorDraft = SanitizeWeatherMapGeneratorSettings(
        m_weatherGeneratorDraft);
    m_weatherGeneratorRequest = m_weatherGeneratorDraft;
    m_weatherGeneratorRequestPending = true;
    m_weatherGeneratorDirty = false;
    m_weatherGeneratorLastRequestTime = m_currentApplicationTime;
}

void NoiseLab::DrawSlice(const char* label, NoiseSliceAxis axis, SliceTarget& target)
{
    ImGui::BeginGroup();
    ImGui::TextUnformatted(label);
    const ImVec2 size(180.0f, 180.0f);
    ImGui::Image(ImTextureRef(static_cast<ImTextureID>(
                     reinterpret_cast<std::uintptr_t>(target.srv.Get()))), size);
    const ImVec2 imageMin = ImGui::GetItemRectMin();
    const ImVec2 imageMax = ImGui::GetItemRectMax();

    float horizontal = 0.5f;
    float vertical = 0.5f;
    if (axis == NoiseSliceAxis::XY)
    {
        horizontal = m_parameters.normalizedSlicePosition.x;
        vertical = m_parameters.normalizedSlicePosition.y;
    }
    else if (axis == NoiseSliceAxis::XZ)
    {
        horizontal = m_parameters.normalizedSlicePosition.x;
        vertical = m_parameters.normalizedSlicePosition.z;
    }
    else
    {
        horizontal = m_parameters.normalizedSlicePosition.z;
        vertical = m_parameters.normalizedSlicePosition.y;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 color = IM_COL32(255, 70, 70, 220);
    const float x = imageMin.x + horizontal * (imageMax.x - imageMin.x);
    const float y = imageMin.y + (1.0f - vertical) * (imageMax.y - imageMin.y);
    drawList->AddLine(ImVec2(x, imageMin.y), ImVec2(x, imageMax.y), color);
    drawList->AddLine(ImVec2(imageMin.x, y), ImVec2(imageMax.x, y), color);

    if (ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        const ImVec2 mouse = ImGui::GetMousePos();
        const float u = std::clamp((mouse.x - imageMin.x) / size.x, 0.0f, 1.0f);
        const float v = 1.0f - std::clamp((mouse.y - imageMin.y) / size.y, 0.0f, 1.0f);
        if (axis == NoiseSliceAxis::XY)
        {
            m_parameters.normalizedSlicePosition.x = u;
            m_parameters.normalizedSlicePosition.y = v;
        }
        else if (axis == NoiseSliceAxis::XZ)
        {
            m_parameters.normalizedSlicePosition.x = u;
            m_parameters.normalizedSlicePosition.z = v;
        }
        else
        {
            m_parameters.normalizedSlicePosition.z = u;
            m_parameters.normalizedSlicePosition.y = v;
        }
    }
    ImGui::EndGroup();
}

void NoiseLab::RenderPreviews(ID3D11VertexShader* fullscreenVs,
                              ID3D11PixelShader* noiseLabPs,
                              ID3D11Buffer* cloudCb,
                              ID3D11ShaderResourceView* weatherMapSrv,
                              ID3D11SamplerState* weatherSampler)
{
    if (!m_initialized || !m_visible || !fullscreenVs || !noiseLabPs)
        return;

    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(kPreviewSize);
    viewport.Height = static_cast<float>(kPreviewSize);
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);
    m_context->OMSetDepthStencilState(nullptr, 0);
    m_context->RSSetState(nullptr);
    m_context->IASetInputLayout(nullptr);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(fullscreenVs, nullptr, 0);
    m_context->PSSetShader(noiseLabPs, nullptr, 0);
    m_context->PSSetConstantBuffers(1, 1, &cloudCb);
    m_context->PSSetShaderResources(2, 1, &weatherMapSrv);
    m_context->PSSetSamplers(1, 1, &weatherSampler);

    for (std::uint32_t axis = 0; axis < m_targets.size(); ++axis)
    {
        m_parameters.sliceAxis = axis;
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(m_context->Map(
                m_noiseLabCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            continue;
        std::memcpy(mapped.pData, &m_parameters, sizeof(m_parameters));
        m_context->Unmap(m_noiseLabCb.Get(), 0);
        ID3D11Buffer* labCb = m_noiseLabCb.Get();
        m_context->PSSetConstantBuffers(2, 1, &labCb);

        const float clear[4] = {};
        ID3D11RenderTargetView* rtv = m_targets[axis].rtv.Get();
        m_context->OMSetRenderTargets(1, &rtv, nullptr);
        m_context->ClearRenderTargetView(rtv, clear);
        m_context->Draw(3, 0);
    }
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    ID3D11ShaderResourceView* nullSrv = nullptr;
    m_context->PSSetShaderResources(2, 1, &nullSrv);
}

void NoiseLab::DrawPerformanceOverlay(const FrameTimingSnapshot& timing,
                                      const CloudParameters& cloudParameters,
                                      const LightParameters& lightParameters,
                                      bool vsyncEnabled)
{
    // Win32 DPI 가상화가 켜진 환경에서는 viewport WorkSize가 물리 픽셀 기준으로
    // 남을 수 있다. ImGui가 실제 레이아웃에 사용하는 DisplaySize를 기준으로 잡아야
    // 우측 가장자리가 고배율 모니터에서도 화면 밖으로 밀려나지 않는다.
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 position(
        io.DisplaySize.x - 12.0f,
        12.0f);
    ImGui::SetNextWindowPos(position, ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.72f);

    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoInputs;

    if (ImGui::Begin("Performance Overlay", nullptr, flags))
    {
        ImGui::Text("Frame # %llu",
                    static_cast<unsigned long long>(timing.frameIndex));
        if (timing.cpuValid)
        {
            ImGui::Text("FPS        %7.1f", timing.fps);
            ImGui::Text("CPU Frame  %7.3f ms", timing.cpuFrameMs);
        }
        else
        {
            ImGui::TextUnformatted("FPS / CPU  warming up");
        }

        if (timing.gpuValid)
        {
            ImGui::Text("GPU Frame  %7.3f ms", timing.gpuFrameMs);
            ImGui::Text("GPU Cloud  %7.3f ms", timing.gpuCloudMs);
        }
        else
        {
            ImGui::TextUnformatted("GPU        warming up");
        }

        ImGui::Separator();
        ImGui::Text("View   %u @ %.1f m / %.1f km",
                    cloudParameters.maxViewSteps, cloudParameters.stepSize,
                    cloudParameters.maxViewTraceDistance / 1000.0f);
        ImGui::Text("Light  %u @ %.1f m / %.1f km",
                    lightParameters.maxLightSteps, lightParameters.lightStepSize,
                    cloudParameters.maxLightTraceDistance / 1000.0f);
        ImGui::Text("VSync  %s", vsyncEnabled ? "On" : "Off");
    }
    ImGui::End();
}

void NoiseLab::EndFrame(ID3D11RenderTargetView* backBufferRtv)
{
    if (!m_initialized)
        return;
    m_context->OMSetRenderTargets(1, &backBufferRtv, nullptr);
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

bool NoiseLab::ConsumeParametersChanged()
{
    const bool result = m_parametersChanged;
    m_parametersChanged = false;
    return result;
}

bool NoiseLab::ConsumeWeatherPresetRequest(Stage5WeatherPreset& preset)
{
    if (m_weatherPresetRequest < 0 || m_weatherPresetRequest > 2)
        return false;
    preset = static_cast<Stage5WeatherPreset>(m_weatherPresetRequest);
    m_weatherPresetRequest = -1;
    return true;
}

bool NoiseLab::ConsumeWeatherGeneratorRequest(
    WeatherMapGeneratorSettings& settings)
{
    if (!m_weatherGeneratorRequestPending)
        return false;
    settings = m_weatherGeneratorRequest;
    m_weatherGeneratorRequestPending = false;
    return true;
}

bool NoiseLab::ConsumeExportRequest()
{
    const bool result = m_exportRequested;
    m_exportRequested = false;
    return result;
}

bool NoiseLab::ValidatePreviewData()
{
    const auto outputMode = static_cast<NoiseOutputMode>(m_parameters.outputMode);
    const bool heightOnly = outputMode == NoiseOutputMode::HeightFraction ||
                            outputMode == NoiseOutputMode::HeightProfile;
    const bool sampleMask = outputMode == NoiseOutputMode::DetailSampleMask;
    const bool weatherMayEmptySlice = outputMode == NoiseOutputMode::FinalDensity ||
        outputMode == NoiseOutputMode::BaseDensity ||
        outputMode == NoiseOutputMode::DetailNoise ||
        outputMode == NoiseOutputMode::Erosion || sampleMask ||
        outputMode == NoiseOutputMode::WeatherCoverage ||
        outputMode == NoiseOutputMode::WeatherThresholdDensity;
    const bool cloudTypeOnly = outputMode == NoiseOutputMode::CloudType;
    const bool weatherXOnly = outputMode == NoiseOutputMode::WeatherDensityModifier ||
                              outputMode == NoiseOutputMode::WeatherUv;
    unsigned char globalMinimum = 255;
    unsigned char globalMaximum = 0;
    for (std::size_t targetIndex = 0; targetIndex < m_targets.size(); ++targetIndex)
    {
        SliceTarget& target = m_targets[targetIndex];
        m_context->CopyResource(target.staging.Get(), target.texture.Get());
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(m_context->Map(target.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            return false;
        unsigned char minimum = 255;
        unsigned char maximum = 0;
        for (UINT y = 0; y < kPreviewSize; ++y)
        {
            const auto* row = static_cast<const unsigned char*>(mapped.pData) +
                              static_cast<size_t>(y) * mapped.RowPitch;
            for (UINT x = 0; x < kPreviewSize; ++x)
            {
                const unsigned char value = row[x * 4];
                minimum = std::min(minimum, value);
                maximum = std::max(maximum, value);
            }
        }
        m_context->Unmap(target.staging.Get(), 0);
        globalMinimum = std::min(globalMinimum, minimum);
        globalMaximum = std::max(globalMaximum, maximum);
        // XZ는 Y를 고정하므로 height-only 출력이 단색인 것이 정상이다.
        // XY와 YZ는 세로축에 월드 Y가 들어가므로 위아래 변화가 반드시 있어야 한다.
        // Cloud Type 프리셋은 Z 띠이므로 Z 고정 XY가 단색이고, Density/UV의
        // 첫 채널은 X 기준이므로 X 고정 YZ가 단색이다.
        const bool expectedUniform = (heightOnly && targetIndex == 1) ||
            (cloudTypeOnly && targetIndex == 0) ||
            (weatherXOnly && targetIndex == 2);
        if (expectedUniform)
        {
            if (maximum != minimum)
                return false;
        }
        else if (maximum <= minimum && !(weatherMayEmptySlice && maximum == 0))
        {
            return false;
        }
    }
    // Weather R이 한 단면 전체를 비우는 것은 정상이다. 대신 세 단면 전체가
    // 모두 비어 자동 검사가 무의미해지는 경우와 sample mask 양 끝 누락은 막는다.
    if (weatherMayEmptySlice && globalMaximum <= globalMinimum)
        return false;
    if (sampleMask && (globalMinimum != 0 || globalMaximum != 255))
        return false;
    return true;
}

std::uint64_t NoiseLab::PreviewHash(std::size_t targetIndex)
{
    if (targetIndex >= m_targets.size())
        return 0;
    SliceTarget& target = m_targets[targetIndex];
    m_context->CopyResource(target.staging.Get(), target.texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(m_context->Map(target.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return 0;
    std::uint64_t hash = 1469598103934665603ull;
    for (UINT y = 0; y < kPreviewSize; ++y)
    {
        const auto* row = static_cast<const unsigned char*>(mapped.pData) +
                          static_cast<size_t>(y) * mapped.RowPitch;
        for (UINT x = 0; x < kPreviewSize * 4; ++x)
        {
            hash ^= row[x];
            hash *= 1099511628211ull;
        }
    }
    m_context->Unmap(target.staging.Get(), 0);
    return hash;
}

bool NoiseLab::SaveTargetPng(const std::filesystem::path& path, SliceTarget& target)
{
    m_context->CopyResource(target.staging.Get(), target.texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(m_context->Map(target.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return false;

    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    HRESULT result = CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (SUCCEEDED(result)) result = factory->CreateStream(&stream);
    if (SUCCEEDED(result)) result = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(result)) result = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(result)) result = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (SUCCEEDED(result)) result = encoder->CreateNewFrame(&frame, &properties);
    if (SUCCEEDED(result)) result = frame->Initialize(properties.Get());
    if (SUCCEEDED(result)) result = frame->SetSize(kPreviewSize, kPreviewSize);
    // D3D texture는 회색조라 R/B 순서가 같으며 PNG encoder가 기본 지원하는
    // 32bpp BGRA를 사용하면 별도 format converter 없이 안전하게 기록할 수 있다.
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(result)) result = frame->SetPixelFormat(&format);
    if (SUCCEEDED(result) && format != GUID_WICPixelFormat32bppBGRA)
        result = E_FAIL;
    if (SUCCEEDED(result))
        result = frame->WritePixels(kPreviewSize, mapped.RowPitch,
                                    mapped.RowPitch * kPreviewSize,
                                    static_cast<BYTE*>(mapped.pData));
    if (SUCCEEDED(result)) result = frame->Commit();
    if (SUCCEEDED(result)) result = encoder->Commit();
    m_context->Unmap(target.staging.Get(), 0);
    return SUCCEEDED(result);
}

bool NoiseLab::SaveTexturePng(const std::filesystem::path& path,
                              ID3D11Texture2D* texture)
{
    if (!texture)
        return false;
    D3D11_TEXTURE2D_DESC desc = {};
    texture->GetDesc(&desc);
    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(m_device->CreateTexture2D(&stagingDesc, nullptr, &staging)))
        return false;
    m_context->CopyResource(staging.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(m_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return false;

    // D3D R8G8B8A8의 메모리 순서는 RGBA지만 WIC PNG encoder가 안정적으로
    // 받는 형식은 BGRA다. R/B를 명시적으로 바꾼 packed buffer를 만들어
    // coverage(R)와 density(B)가 저장 파일에서 뒤바뀌지 않게 한다.
    const UINT packedPitch = desc.Width * 4u;
    std::vector<BYTE> bgra(static_cast<std::size_t>(packedPitch) * desc.Height);
    for (UINT y = 0; y < desc.Height; ++y)
    {
        const BYTE* source = static_cast<const BYTE*>(mapped.pData) +
            static_cast<std::size_t>(y) * mapped.RowPitch;
        BYTE* destination = bgra.data() + static_cast<std::size_t>(y) * packedPitch;
        for (UINT x = 0; x < desc.Width; ++x)
        {
            destination[x * 4u + 0u] = source[x * 4u + 2u];
            destination[x * 4u + 1u] = source[x * 4u + 1u];
            destination[x * 4u + 2u] = source[x * 4u + 0u];
            destination[x * 4u + 3u] = source[x * 4u + 3u];
        }
    }

    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    HRESULT result = CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (SUCCEEDED(result)) result = factory->CreateStream(&stream);
    if (SUCCEEDED(result)) result = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(result)) result = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(result)) result = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (SUCCEEDED(result)) result = encoder->CreateNewFrame(&frame, &properties);
    if (SUCCEEDED(result)) result = frame->Initialize(properties.Get());
    if (SUCCEEDED(result)) result = frame->SetSize(desc.Width, desc.Height);
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(result)) result = frame->SetPixelFormat(&format);
    if (SUCCEEDED(result) && format != GUID_WICPixelFormat32bppBGRA)
        result = E_FAIL;
    if (SUCCEEDED(result))
        result = frame->WritePixels(desc.Height, packedPitch,
                                    packedPitch * desc.Height, bgra.data());
    if (SUCCEEDED(result)) result = frame->Commit();
    if (SUCCEEDED(result)) result = encoder->Commit();
    m_context->Unmap(staging.Get(), 0);
    return SUCCEEDED(result);
}

std::uint64_t NoiseLab::HashFile(const std::filesystem::path& path) const
{
    std::ifstream input(path, std::ios::binary);
    std::uint64_t hash = 1469598103934665603ull;
    char byte = 0;
    while (input.get(byte))
    {
        hash ^= static_cast<unsigned char>(byte);
        hash *= 1099511628211ull;
    }
    return hash;
}

bool NoiseLab::WriteMetadata(const std::filesystem::path& path,
                             const CloudParameters& cloud,
                             const LightParameters& light,
                             Stage6SunPreset sunPreset,
                             Stage7PhasePreset phasePreset,
                              const EnvironmentParameters& environment,
                              Stage8EnvironmentPreset environmentPreset,
                              const OptimizationParameters& optimization,
                              Stage9OptimizationPreset optimizationPreset,
                             Stage4DetailPreset detailPreset,
                             Stage5WeatherPreset weatherPreset,
                             const WeatherMapGeneratorSettings& weatherGeneratorSettings,
                             std::uint64_t weatherMapHash,
                             const std::filesystem::path& noiseSourcePath) const
{
    std::ofstream output(path, std::ios::binary);
    if (!output)
        return false;
    static const char* outputNames[] = {
        "rawNoise", "thresholdDensity", "finalDensity",
        "heightFraction", "heightProfile", "baseDensity",
        "detailNoise", "erosion", "detailSampleMask",
        "weatherCoverage", "cloudType", "weatherDensityModifier",
        "weatherThresholdDensity", "typedHeightProfile", "weatherUv"
    };
    static const char* detailPresetNames[] = {
        "detailOff", "defaultDetail", "fineDetail", "strongErosion", "custom"
    };
    static const char* weatherPresetNames[] = {
        "uniformLegacy", "periodicPerlin", "channelDebug"
    };
    static const char* sunPresetNames[] = {
        "noon", "lowEast", "lowWest", "customSun"
    };
    static const char* phasePresetNames[] = {
        "off", "balanced", "silverLining", "backscatterCheck", "custom"
    };
    static const char* environmentPresetNames[] = {
        "off", "balanced", "strongFill", "groundCheck", "custom"
    };
    static const char* optimizationPresetNames[] = {
        "off", "earlyExitOnly", "emptySpaceOnly", "balanced", "custom"
    };
    const int presetIndex = std::clamp(static_cast<int>(detailPreset), 0, 4);
    const int weatherPresetIndex = std::clamp(static_cast<int>(weatherPreset), 0, 2);
    const int sunPresetIndex = std::clamp(static_cast<int>(sunPreset), 0, 3);
    const int phasePresetIndex = std::clamp(static_cast<int>(phasePreset), 0, 4);
    const int environmentPresetIndex = std::clamp(
        static_cast<int>(environmentPreset), 0, 4);
    const int optimizationPresetIndex = std::clamp(
        static_cast<int>(optimizationPreset), 0, 4);
    const std::uint32_t outputIndex = std::min(m_parameters.outputMode, 14u);
    const WeatherMapGeneratorSettings generator =
        SanitizeWeatherMapGeneratorSettings(weatherGeneratorSettings);
    output << std::fixed << std::setprecision(6)
           << "{\n"
           << "  \"schemaVersion\": 13,\n"
           << "  \"cloudDomain\": \"cameraCenteredPlanarLayer\",\n"
           << "  \"internalDistanceUnit\": \"meter\",\n"
           << "  \"cloudBottomAltitudeMeters\": "
           << cloud.cloudBottomAltitude << ",\n"
           << "  \"cloudLayerThicknessMeters\": "
           << cloud.cloudLayerThickness << ",\n"
           << "  \"cloudTopAltitudeMeters\": "
           << cloud.cloudBottomAltitude + cloud.cloudLayerThickness << ",\n"
           << "  \"maxViewTraceDistanceMeters\": "
           << cloud.maxViewTraceDistance << ",\n"
           << "  \"maxViewSteps\": " << cloud.maxViewSteps << ",\n"
           << "  \"viewStepSizeMeters\": " << cloud.stepSize << ",\n"
           << "  \"viewTraceFadeStartDistanceMeters\": "
           << cloud.viewTraceFadeStartDistance << ",\n"
           << "  \"maxLightTraceDistanceMeters\": "
           << cloud.maxLightTraceDistance << ",\n"
           << "  \"noiseLabPreviewWorldSizeMeters\": "
           << cloud.noiseLabPreviewWorldSize << ",\n"
           << "  \"noiseLabPreviewCenterXZMeters\": ["
           << m_parameters.previewCenterXZ.x << ", "
           << m_parameters.previewCenterXZ.y << "],\n"
           << "  \"output\": \"" << outputNames[outputIndex] << "\",\n"
           << "  \"detailPreset\": \"" << detailPresetNames[presetIndex] << "\",\n"
           << "  \"weatherPreset\": \"" << weatherPresetNames[weatherPresetIndex] << "\",\n"
           << "  \"sunPreset\": \"" << sunPresetNames[sunPresetIndex] << "\",\n"
           << "  \"phasePreset\": \"" << phasePresetNames[phasePresetIndex] << "\",\n"
           << "  \"environmentPreset\": \""
           << environmentPresetNames[environmentPresetIndex] << "\",\n"
           << "  \"optimizationPreset\": \""
           << optimizationPresetNames[optimizationPresetIndex] << "\",\n"
           << "  \"optimizationShaderPath\": \"legacyOrCompositeOptimized\",\n"
           << "  \"earlyExitEnabled\": "
           << (optimization.earlyExitEnabled ? "true" : "false") << ",\n"
           << "  \"supportPrecheckEnabled\": "
           << (optimization.supportPrecheckEnabled ? "true" : "false") << ",\n"
           << "  \"emptySpaceSkippingEnabled\": "
           << (optimization.emptySpaceSkippingEnabled ? "true" : "false") << ",\n"
           << "  \"transmittanceThreshold\": "
           << cloud.transmittanceThreshold << ",\n"
           << "  \"baseDensityEpsilon\": "
           << optimization.baseDensityEpsilon << ",\n"
           << "  \"coarseStepMultiplier\": "
           << optimization.coarseStepMultiplier << ",\n"
           << "  \"emptySamplesBeforeCoarse\": "
           << optimization.emptySamplesBeforeCoarse << ",\n"
           << "  \"environmentSource\": \"analyticColorsNoExternalTexture\",\n"
           << "  \"ambientModel\": \"heightWeightedSkyGroundDensityAo\",\n"
           << "  \"directionConvention\": \"sampleToSunWorldDirection\",\n"
           << "  \"phaseDirectionConvention\": "
              "\"cosTheta=dot(cameraToSample,sampleToSun)\",\n"
           << "  \"directionToSun\": [" << light.directionToSun.x << ", "
           << light.directionToSun.y << ", " << light.directionToSun.z << "],\n"
           << "  \"sunColorLinear\": [" << light.sunColor.x << ", "
           << light.sunColor.y << ", " << light.sunColor.z << "],\n"
           << "  \"sunIntensity\": " << light.sunIntensity << ",\n"
           << "  \"singleScatteringModel\": \"energyConservingAlbedo\",\n"
           << "  \"singleScatteringAlbedo\": "
           << light.singleScatteringAlbedo << ",\n"
           << "  \"maxLightSteps\": " << light.maxLightSteps << ",\n"
           << "  \"lightStepSizeMeters\": " << light.lightStepSize << ",\n"
           << "  \"lightRayBiasMeters\": " << light.lightRayBias << ",\n"
           << "  \"lightRayDensitySource\": \"baseDensityWithoutDetailErosion\",\n"
           << "  \"phaseFunction\": \"dualLobeHenyeyGreensteinIsotropicRelative\",\n"
           << "  \"phaseEnabled\": " << (light.phaseEnabled >= 0.5f ? "true" : "false") << ",\n"
           << "  \"forwardScatteringG\": " << light.forwardScatteringG << ",\n"
           << "  \"backwardScatteringG\": " << light.backwardScatteringG << ",\n"
           << "  \"phaseBlend\": " << light.phaseBlend << ",\n"
           << "  \"phaseIntensity\": " << light.phaseIntensity << ",\n"
           << "  \"maxPhaseFactor\": 16.000000,\n"
           << "  \"skyColorLinear\": [" << environment.skyColor.x << ", "
           << environment.skyColor.y << ", " << environment.skyColor.z << "],\n"
           << "  \"skyStrength\": " << environment.skyStrength << ",\n"
           << "  \"groundColorLinear\": [" << environment.groundColor.x << ", "
           << environment.groundColor.y << ", " << environment.groundColor.z << "],\n"
           << "  \"groundStrength\": " << environment.groundStrength << ",\n"
           << "  \"ambientOcclusionStrength\": "
           << environment.ambientOcclusionStrength << ",\n"
           << "  \"ambientHeightInfluence\": "
           << environment.ambientHeightInfluence << ",\n"
           << "  \"multipleScatteringModel\": \"reusedLightOpticalDepthOctaves\",\n"
           << "  \"multipleScatteringEnabled\": "
           << (environment.multipleScatteringEnabled >= 0.5f ? "true" : "false") << ",\n"
           << "  \"multipleScatteringOctaves\": "
           << environment.multipleScatteringOctaves << ",\n"
           << "  \"multipleScatteringAttenuation\": "
           << environment.multipleScatteringAttenuation << ",\n"
           << "  \"multipleScatteringExtinctionFactor\": "
           << environment.multipleScatteringExtinctionFactor << ",\n"
           << "  \"multipleScatteringPhaseFactor\": "
           << environment.multipleScatteringPhaseFactor << ",\n"
           << "  \"weatherMapResolution\": [256, 256],\n"
           << "  \"weatherChannels\": {\"R\": \"coverage\", \"G\": \"cloudType\", "
              "\"B\": \"densityModifierSource\", \"A\": \"reserved\"},\n"
           << "  \"weatherMapHashFnv1a64\": \"" << std::hex << weatherMapHash
           << std::dec << "\",\n"
           << "  \"weatherGenerator\": {\n"
           << "    \"coverage\": {\"seed\": " << generator.coverage.seed
           << ", \"macroPeriod\": " << generator.coverage.macroPeriod
           << ", \"detailPeriod\": " << generator.coverage.detailPeriod
           << ", \"detailWeight\": " << generator.coverage.detailWeight
           << ", \"bias\": " << generator.coverage.bias
           << ", \"contrast\": " << generator.coverage.contrast << "},\n"
           << "    \"cloudType\": {\"seed\": " << generator.cloudType.seed
           << ", \"macroPeriod\": " << generator.cloudType.macroPeriod
           << ", \"detailPeriod\": " << generator.cloudType.detailPeriod
           << ", \"detailWeight\": " << generator.cloudType.detailWeight
           << ", \"bias\": " << generator.cloudType.bias
           << ", \"contrast\": " << generator.cloudType.contrast << "},\n"
           << "    \"density\": {\"seed\": " << generator.density.seed
           << ", \"macroPeriod\": " << generator.density.macroPeriod
           << ", \"detailPeriod\": " << generator.density.detailPeriod
           << ", \"detailWeight\": " << generator.density.detailWeight
           << ", \"bias\": " << generator.density.bias
           << ", \"contrast\": " << generator.density.contrast << "},\n"
           << "    \"coverageThreshold\": " << generator.coverageThreshold << ",\n"
           << "    \"coverageSoftness\": " << generator.coverageSoftness << ",\n"
           << "    \"densityCoverageInfluence\": "
           << generator.densityCoverageInfluence << "\n"
           << "  },\n"
           << "  \"resolution\": [512, 512],\n"
           << "  \"slicePosition\": [" << m_parameters.normalizedSlicePosition.x << ", "
           << m_parameters.normalizedSlicePosition.y << ", "
           << m_parameters.normalizedSlicePosition.z << "],\n"
           << "  \"effectiveTime\": " << m_effectiveTime << ",\n"
           << "  \"baseNoiseScale\": " << cloud.baseNoiseScale << ",\n"
           << "  \"coverage\": " << cloud.coverage << ",\n"
           << "  \"densityMultiplier\": " << cloud.densityMultiplier << ",\n"
           << "  \"extinctionCoefficientPerMeter\": "
           << cloud.extinctionCoefficient << ",\n"
           << "  \"bottomFadeEnd\": " << cloud.bottomFadeEnd << ",\n"
           << "  \"topFadeStart\": " << cloud.topFadeStart << ",\n"
           << "  \"detailNoiseScale\": " << cloud.detailNoiseScale << ",\n"
           << "  \"detailErosionStrength\": " << cloud.detailErosionStrength << ",\n"
           << "  \"detailWindSpeed\": " << cloud.detailWindSpeed << ",\n"
           << "  \"detailNoiseOffset\": " << cloud.detailNoiseOffset << ",\n"
           << "  \"detailLodFadeStartDistanceMeters\": "
           << cloud.detailLodFadeStartDistance << ",\n"
           << "  \"detailLodFadeEndDistanceMeters\": "
           << cloud.detailLodFadeEndDistance << ",\n"
           << "  \"detailLodFilter\": \"lerpMean0.5ThenSkip\",\n"
           << "  \"weatherMapWorldSize\": " << cloud.weatherMapWorldSize << ",\n"
           << "  \"weatherMapWindSpeed\": " << cloud.weatherMapWindSpeed << ",\n"
           << "  \"weatherMapOffset\": [" << cloud.weatherMapOffset.x << ", "
           << cloud.weatherMapOffset.y << "],\n"
           << "  \"noiseOffset\": " << cloud.noiseOffset << ",\n"
           << "  \"windDirection\": [" << cloud.windDirection.x << ", "
           << cloud.windDirection.y << ", " << cloud.windDirection.z << "],\n"
           << "  \"windSpeed\": " << cloud.windSpeed << ",\n"
           << "  \"noiseSource\": \"Noise.hlsli\",\n"
           << "  \"noiseSourceHashFnv1a64\": \"" << std::hex
           << HashFile(noiseSourcePath) << "\"\n"
           << "}\n";
    return output.good();
}

bool NoiseLab::ExportSnapshot(const std::filesystem::path& root,
                              const CloudParameters& cloudParameters,
                              const LightParameters& lightParameters,
                              Stage6SunPreset sunPreset,
                              Stage7PhasePreset phasePreset,
                               const EnvironmentParameters& environmentParameters,
                               Stage8EnvironmentPreset environmentPreset,
                               const OptimizationParameters& optimizationParameters,
                               Stage9OptimizationPreset optimizationPreset,
                              Stage4DetailPreset detailPreset,
                              Stage5WeatherPreset weatherPreset,
                              const WeatherMapGeneratorSettings& weatherGeneratorSettings,
                              std::uint64_t weatherMapHash,
                              ID3D11Texture2D* weatherMapTexture,
                              const std::filesystem::path& noiseSourcePath)
{
    std::error_code error;
    const std::filesystem::path directory = TimestampDirectory(root);
    std::filesystem::create_directories(directory, error);
    if (error)
    {
        m_exportStatus = "Export failed: " + error.message();
        return false;
    }

    const bool success = SaveTargetPng(directory / L"xy.png", m_targets[0]) &&
                         SaveTargetPng(directory / L"xz.png", m_targets[1]) &&
                         SaveTargetPng(directory / L"yz.png", m_targets[2]) &&
                         SaveTexturePng(directory / L"weather-map.png", weatherMapTexture) &&
                         WriteMetadata(directory / L"noise-settings.json",
                                       cloudParameters, lightParameters, sunPreset,
                                        phasePreset, environmentParameters,
                                        environmentPreset, optimizationParameters,
                                        optimizationPreset, detailPreset, weatherPreset,
                                       weatherGeneratorSettings, weatherMapHash,
                                       noiseSourcePath);
    m_exportStatus = success
        ? "Exported: " + NarrowUtf8(directory)
        : "Export failed while writing PNG or JSON";
    return success;
}
