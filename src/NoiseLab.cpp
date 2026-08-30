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
#include "Camera.h"
#include "Stage3HeightMath.h"
#include "Stage7PhaseMath.h"
#include "Stage8AmbientMath.h"
#include "Stage13ScaleMath.h"
#include "Stage13CameraPresets.h"
#include "Stage13WeatherShapeMath.h"
#include "Stage13OpticsLightingMath.h"
#include "Stage14AtmosphereMath.h"

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

void WriteAppearanceSettingsJson(std::ostream& output,
                                 const CloudAppearanceSettings& value,
                                 const char* indent)
{
    output << indent << "{\"cloudTypeMode\": "
           << static_cast<std::uint32_t>(value.cloudTypeMode)
           << ", \"globalCoverage\": " << value.globalCoverage
           << ", \"densityMultiplier\": " << value.densityMultiplier
           << ", \"extinctionPerMeter\": " << value.extinctionPerMeter
           << ", \"detailErosion\": " << value.detailErosion
           << ", \"weatherThreshold\": " << value.weatherThreshold
           << ", \"weatherSoftness\": " << value.weatherSoftness
           << ", \"coverageBias\": " << value.coverageBias
           << ", \"coverageContrast\": " << value.coverageContrast
           << ", \"densityCoverageLink\": " << value.densityCoverageLink
           << ", \"thicknessCoverageLink\": " << value.thicknessCoverageLink
           << ", \"cloudTypeBias\": " << value.cloudTypeBias
           << ", \"stratusMinimumThicknessMeters\": "
           << value.stratusMinimumThicknessMeters
           << ", \"stratusMaximumThicknessMeters\": "
           << value.stratusMaximumThicknessMeters
           << ", \"cumulusMinimumThicknessMeters\": "
           << value.cumulusMinimumThicknessMeters
           << ", \"cumulusMaximumThicknessMeters\": "
           << value.cumulusMaximumThicknessMeters
           << ", \"stratusBottomFadeEnd\": " << value.stratusBottomFadeEnd
           << ", \"stratusTopFadeStart\": " << value.stratusTopFadeStart
           << ", \"mixedBottomFadeEnd\": " << value.mixedBottomFadeEnd
           << ", \"mixedTopFadeStart\": " << value.mixedTopFadeStart
           << ", \"cumulusBottomFadeEnd\": " << value.cumulusBottomFadeEnd
           << ", \"cumulusTopFadeStart\": " << value.cumulusTopFadeStart
           << ", \"cumulusUpperMassBottom\": " << value.cumulusUpperMassBottom
           << ", \"cumulusUpperMassStart\": " << value.cumulusUpperMassStart
           << ", \"cumulusUpperMassEnd\": " << value.cumulusUpperMassEnd
           << "}";
}

void DrawSunDirectionDiagram(const Camera& camera,
                             const DirectX::XMFLOAT3& directionToSun)
{
    // XZ 위에서 태양 위치, 실제 광선 진행, 그림자 방향과 카메라 시선을
    // 한 그림에 표시한다. Stage 14 각도와 렌더링이 같은 방향을 공유하는지
    // 이 도식 하나로 확인할 수 있다.
    constexpr float diagramSize = 220.0f;
    ImGui::InvisibleButton("##SunDirectionDiagram",
                           ImVec2(diagramSize, diagramSize));
    const ImVec2 diagramMin = ImGui::GetItemRectMin();
    const ImVec2 diagramMax = ImGui::GetItemRectMax();
    const ImVec2 center(
        (diagramMin.x + diagramMax.x) * 0.5f,
        (diagramMin.y + diagramMax.y) * 0.5f);
    const float radius = diagramSize * 0.38f;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(diagramMin, diagramMax,
                            IM_COL32(20, 25, 34, 255), 6.0f);
    drawList->AddRect(diagramMin, diagramMax,
                      IM_COL32(100, 110, 125, 255), 6.0f);
    drawList->AddLine(ImVec2(center.x - radius, center.y),
                      ImVec2(center.x + radius, center.y),
                      IM_COL32(75, 85, 100, 255));
    drawList->AddLine(ImVec2(center.x, center.y - radius),
                      ImVec2(center.x, center.y + radius),
                      IM_COL32(75, 85, 100, 255));
    drawList->AddText(ImVec2(center.x + radius - 16.0f, center.y + 3.0f),
                      IM_COL32(180, 190, 205, 255), "+X");
    drawList->AddText(ImVec2(center.x - radius, center.y + 3.0f),
                      IM_COL32(180, 190, 205, 255), "-X");
    drawList->AddText(ImVec2(center.x + 4.0f, center.y - radius),
                      IM_COL32(180, 190, 205, 255), "+Z");
    drawList->AddText(ImVec2(center.x + 4.0f, center.y + radius - 14.0f),
                      IM_COL32(180, 190, 205, 255), "-Z");

    const auto horizontalPoint = [&](float x, float z, float scale)
    {
        const float length = std::sqrt(x * x + z * z);
        const float safeLength = length > 1e-5f ? length : 1.0f;
        return ImVec2(center.x + x / safeLength * radius * scale,
                      center.y - z / safeLength * radius * scale);
    };
    const auto addArrow = [&](ImVec2 from, ImVec2 to, ImU32 color,
                              float thickness)
    {
        drawList->AddLine(from, to, color, thickness);
        const float dx = to.x - from.x;
        const float dy = to.y - from.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length <= 1e-5f)
            return;
        const float ux = dx / length;
        const float uy = dy / length;
        constexpr float head = 9.0f;
        constexpr float half = 4.5f;
        drawList->AddTriangleFilled(
            to,
            ImVec2(to.x - ux * head - uy * half,
                   to.y - uy * head + ux * half),
            ImVec2(to.x - ux * head + uy * half,
                   to.y - uy * head - ux * half), color);
    };

    const ImVec2 sunPoint = horizontalPoint(
        directionToSun.x, directionToSun.z, 0.82f);
    const ImVec2 shadowPoint = horizontalPoint(
        -directionToSun.x, -directionToSun.z, 0.82f);
    drawList->AddCircleFilled(sunPoint, 8.0f,
                              IM_COL32(255, 220, 70, 255));
    drawList->AddText(ImVec2(sunPoint.x + 8.0f, sunPoint.y - 8.0f),
                      IM_COL32(255, 230, 120, 255), "SUN");
    addArrow(sunPoint, center, IM_COL32(255, 160, 50, 255), 2.5f);
    addArrow(center, shadowPoint, IM_COL32(230, 85, 65, 255), 2.5f);

    const DirectX::XMFLOAT3 cameraPosition = camera.GetPosition();
    const DirectX::XMFLOAT3 cameraTarget = camera.GetTarget();
    const float cameraForwardX = cameraTarget.x - cameraPosition.x;
    const float cameraForwardZ = cameraTarget.z - cameraPosition.z;
    const ImVec2 cameraPoint = horizontalPoint(
        cameraForwardX, cameraForwardZ, 0.60f);
    addArrow(center, cameraPoint, IM_COL32(70, 210, 255, 255), 2.0f);
    drawList->AddText(
        ImVec2(diagramMin.x + 7.0f, diagramMax.y - 19.0f),
        IM_COL32(70, 210, 255, 255), "CAMERA");

    ImGui::Text("To Sun: X %.3f  Y %.3f  Z %.3f",
                directionToSun.x, directionToSun.y, directionToSun.z);
    ImGui::Text("Incoming light / shadow: X %.3f  Z %.3f",
                -directionToSun.x, -directionToSun.z);
    const float cameraHorizontalLength = std::sqrt(
        cameraForwardX * cameraForwardX + cameraForwardZ * cameraForwardZ);
    const float cameraRightX = cameraHorizontalLength > 1e-5f
        ? cameraForwardZ / cameraHorizontalLength : 1.0f;
    const float cameraRightZ = cameraHorizontalLength > 1e-5f
        ? -cameraForwardX / cameraHorizontalLength : 0.0f;
    const float sunScreenSide = directionToSun.x * cameraRightX +
                                directionToSun.z * cameraRightZ;
    const char* sunSide = sunScreenSide < -0.10f ? "LEFT" :
        (sunScreenSide > 0.10f ? "RIGHT" : "CENTER");
    const char* shadowSide = sunScreenSide < -0.10f ? "RIGHT" :
        (sunScreenSide > 0.10f ? "LEFT" : "CENTER");
    ImGui::Text("Current camera: Sun %s / Shadow %s", sunSide, shadowSide);
    ImGui::TextDisabled(
        "Yellow=Sun, orange=light travel, red=shadow away, cyan=camera forward.");
    ImGui::TextDisabled(
        "Screen left/right depends on the cyan camera direction.");
}

void DrawEnvironmentGroundControls(
    AtmosphereMode atmosphereMode,
    GroundLightingParameters& groundLightingParameters,
    EnvironmentParameters& environmentParameters,
    Stage8EnvironmentPreset& environmentPreset)
{
    static const char* presetNames[] = {
        "Off", "Balanced", "Strong Fill", "Ground Check",
        "Portfolio Hero", "Custom"
    };
    const int presetIndex = std::clamp(
        static_cast<int>(environmentPreset), 0, 5);
    ImGui::Text("Cloud Environment Preset: %s", presetNames[presetIndex]);
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
    if (ImGui::Button("Ground Check"))
    {
        stage8environment::ApplyPreset(
            environmentParameters, Stage8EnvironmentPreset::GroundCheck);
        environmentPreset = Stage8EnvironmentPreset::GroundCheck;
    }
    ImGui::SameLine();
    if (ImGui::Button("Portfolio Ambient"))
    {
        stage8environment::ApplyPreset(
            environmentParameters, Stage8EnvironmentPreset::PortfolioHero);
        environmentPreset = Stage8EnvironmentPreset::PortfolioHero;
    }

    if (ImGui::TreeNodeEx("Ground Reflection",
                          ImGuiTreeNodeFlags_DefaultOpen))
    {
        int groundPreset = static_cast<int>(groundLightingParameters.preset);
        const char* groundPresets[] = {
            "Concrete", "Grass", "Snow", "Desert", "Custom"
        };
        if (ImGui::Combo("Ground Preset", &groundPreset, groundPresets, 5))
            stage14ground::ApplyPreset(
                groundLightingParameters,
                static_cast<GroundMaterialPreset>(groundPreset));
        if (groundLightingParameters.preset == GroundMaterialPreset::Custom)
            ImGui::ColorEdit3("Custom Ground Albedo",
                &groundLightingParameters.albedo.x,
                ImGuiColorEditFlags_Float);
        ImGui::ColorButton("Ground Linear Albedo",
            ImVec4(groundLightingParameters.albedo.x,
                   groundLightingParameters.albedo.y,
                   groundLightingParameters.albedo.z, 1.0f));
        ImGui::SameLine();
        ImGui::Text("linear %.2f %.2f %.2f",
                    groundLightingParameters.albedo.x,
                    groundLightingParameters.albedo.y,
                    groundLightingParameters.albedo.z);
        ImGui::SliderFloat("Ground Bounce Multiplier",
            &groundLightingParameters.bounceMultiplier, 0.0f, 2.0f, "%.2f");
        ImGui::TextDisabled(
            "Physical: surface albedo also colors the cloud-bottom bounce.");
        ImGui::TreePop();
    }

    bool edited = false;
    if (ImGui::TreeNodeEx("Cloud Ambient Visibility",
                          ImGuiTreeNodeFlags_DefaultOpen))
    {
        edited |= ImGui::SliderFloat(
            "Ambient Occlusion Strength",
            &environmentParameters.ambientOcclusionStrength,
            0.0f, 8.0f, "%.3f");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##AmbientOcclusion"))
        {
            environmentParameters.ambientOcclusionStrength = 1.50f;
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
        edited |= ImGui::SliderFloat(
            "Ambient Shadow Coupling",
            &environmentParameters.ambientShadowCoupling,
            0.0f, 1.0f, "%.3f");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##AmbientShadowCoupling"))
        {
            environmentParameters.ambientShadowCoupling = 0.0f;
            edited = true;
        }
        edited |= ImGui::SliderFloat(
            "Ambient Shadow Exponent",
            &environmentParameters.ambientShadowExponent,
            0.1f, 8.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##AmbientShadowExponent"))
        {
            environmentParameters.ambientShadowExponent = 1.0f;
            edited = true;
        }

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
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Cloud Multiple Scattering",
                          ImGuiTreeNodeFlags_DefaultOpen))
    {
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
            environmentParameters.multipleScatteringAttenuation = 0.20f;
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
            environmentParameters.multipleScatteringPhaseFactor = 0.25f;
            edited = true;
        }
        edited |= ImGui::SliderFloat(
            "Multiple Interior Blend",
            &environmentParameters.multipleScatteringInteriorBlend,
            0.0f, 1.0f, "%.3f");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##MultipleInteriorBlend"))
        {
            environmentParameters.multipleScatteringInteriorBlend = 0.0f;
            edited = true;
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Manual Reference Colors",
                          ImGuiTreeNodeFlags_DefaultOpen))
    {
        const bool manualReference =
            atmosphereMode == AtmosphereMode::ManualReference;
        ImGui::BeginDisabled(!manualReference);
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
            environmentParameters.skyStrength = 0.12f;
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
            environmentParameters.groundStrength = 0.05f;
            edited = true;
        }
        ImGui::EndDisabled();
        if (!manualReference)
            ImGui::TextDisabled(
                "Physical mode samples atmosphere LUTs and Ground Reflection instead.");
        ImGui::TreePop();
    }

    if (ImGui::Button("Reset All Environment"))
    {
        stage8environment::ApplyPreset(
            environmentParameters, Stage8EnvironmentPreset::Balanced);
        environmentPreset = Stage8EnvironmentPreset::Balanced;
    }
    environmentParameters = stage8environment::Sanitize(environmentParameters);
    if (edited)
        environmentPreset = Stage8EnvironmentPreset::Custom;
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
        if (!WantsKeyboardCapture() && (lParam & (1ll << 30)) == 0)
            ToggleVisible();
        return !WantsKeyboardCapture();
    }
    if (!m_initialized ||
        std::none_of(m_panelVisible.begin(), m_panelVisible.end(),
                     [](bool visible) { return visible; }))
        return false;

    ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam);
    const ImGuiIO& io = ImGui::GetIO();
    return (IsMouseMessage(message) && io.WantCaptureMouse) ||
           (IsKeyboardMessage(message) && WantsKeyboardCapture());
}

bool NoiseLab::WantsKeyboardCapture() const
{
    if (!m_initialized || ImGui::GetCurrentContext() == nullptr)
        return false;
    const ImGuiIO& io = ImGui::GetIO();
    return stage13scene::ShouldBlockSceneKeyboard(
        io.WantTextInput, ImGui::IsAnyItemActive());
}

void NoiseLab::ToggleVisible()
{
    TogglePanel(DeveloperUiPanel::Noise);
}

void NoiseLab::TogglePanel(DeveloperUiPanel panel)
{
    const std::size_t index = static_cast<std::size_t>(panel);
    if (index < m_panelVisible.size())
        m_panelVisible[index] = !m_panelVisible[index];
}

NoiseLab::CameraSnapshot NoiseLab::CaptureCamera(
    const Camera& camera) const
{
    CameraSnapshot snapshot;
    snapshot.position = camera.GetPosition();
    snapshot.target = camera.GetTarget();
    snapshot.fovYDegrees = camera.GetFovYDegrees();
    snapshot.nearPlaneMeters = camera.GetNearPlane();
    snapshot.farPlaneMeters = camera.GetFarPlane();
    snapshot.valid = true;
    return snapshot;
}

void NoiseLab::ApplyCameraSnapshot(const CameraSnapshot& snapshot,
                                   Camera& camera,
                                   const wchar_t* debugName)
{
    if (!snapshot.valid)
        return;
    camera.SetClipPlanes(snapshot.nearPlaneMeters, snapshot.farPlaneMeters);
    camera.SetFovYDegrees(snapshot.fovYDegrees);
    camera.SetLookAt(snapshot.position, snapshot.target);
    camera.SetDebugName(debugName);
}

void NoiseLab::UpdateEffectiveTime(float applicationTime)
{
    if (!m_hasApplicationTime)
    {
        m_lastApplicationTime = applicationTime;
        m_hasApplicationTime = true;
    }
    const float delta = applicationTime - m_lastApplicationTime;
    m_lastApplicationTime = applicationTime;
    m_effectiveTime = static_cast<float>(stage13shape::AdvanceEffectiveTime(
        m_effectiveTime, delta, m_timeScale, m_timePaused));

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
                          Camera& camera,
                          CloudParameters& cloudParameters,
                          CloudShapeParameters& cloudShapeParameters,
                          CloudDomainParameters& cloudDomainParameters,
                          CloudLodParameters& cloudLodParameters,
                          OptimizationParameters& optimizationParameters,
                          Stage9OptimizationPreset& optimizationPreset,
                          Stage10UpsamplingParameters& upsamplingParameters,
                          Stage10ResolutionPreset& resolutionPreset,
                          Stage11TemporalParameters& temporalParameters,
                          bool temporalHistoryValid,
                          std::uint32_t temporalAccumulatedFrames,
                          Stage12ShadowParameters& shadowParameters,
                          LightParameters& lightParameters,
                          Stage6SunPreset& sunPreset,
                          Stage7PhasePreset& phasePreset,
                          EnvironmentParameters& environmentParameters,
                          Stage8EnvironmentPreset& environmentPreset,
                          AtmosphereParameters& atmosphereParameters,
                          GroundLightingParameters& groundLightingParameters,
                          ToneMappingParameters& toneMappingParameters,
                          Stage15QualityPreset stage15QualityPreset,
                          Stage15ConceptPreset stage15ConceptPreset,
                          Stage15DiagnosticMode stage15DiagnosticMode,
                          bool stage15TemporalOverrideActive,
                          const Stage15OutputExtentSnapshot& outputExtent,
                          Stage15CaptureState stage15CaptureState,
                          std::uint32_t stage15CaptureCompletedSamples,
                          const std::string& stage15CaptureStatus,
                          std::uint32_t temporalResetCountLast60Frames,
                          Stage11HistoryResetReason lastTemporalResetReason,
                          bool temporalStatisticsValid,
                          float temporalHistoryValidPercent,
                          float temporalAverageHistoryWeight,
                          bool& stage15StatusOverlayVisible,
                          bool& performanceOverlayVisible,
                          const std::array<ID3D11ShaderResourceView*, 6>& atmosphereLutSrvs,
                          const std::array<std::uint64_t, 6>& atmosphereLutGenerations,
                          const std::array<std::uint64_t, 6>& atmosphereLutHashes,
                          const std::string& atmosphereStatus,
                          Stage5WeatherPreset weatherPreset,
                          const WeatherMapGeneratorSettings& weatherGeneratorSettings,
                          CloudTypeMode cloudTypeMode,
                          CloudAppearancePreset cloudAppearancePreset,
                          bool cloudAppearanceDirty,
                          bool hasSavedCustomAppearance,
                          const std::string& cloudAppearanceStatus,
                          float& cameraMoveSpeedMetersPerSecond,
                          NoiseVolumeParameters& noiseVolumeParameters,
                          std::uint64_t baseNoiseVolumeHash,
                          std::uint64_t detailNoiseVolumeHash,
                          double noiseVolumeGenerationMilliseconds,
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
    m_atmosphereSnapshot = atmosphereParameters;
    m_groundLightingSnapshot = groundLightingParameters;
    m_toneMappingSnapshot = toneMappingParameters;
    SynchronizeStage15Snapshot(
        stage15QualityPreset, stage15ConceptPreset,
        stage15DiagnosticMode, stage15TemporalOverrideActive);
    m_atmosphereLutGenerations = atmosphereLutGenerations;
    m_atmosphereLutHashes = atmosphereLutHashes;
    m_currentApplicationTime = applicationTime;
    m_cameraMoveSpeedMetersPerSecond =
        stage13scene::SanitizeMoveSpeed(
            cameraMoveSpeedMetersPerSecond);
    if (!m_weatherGeneratorDraftInitialized)
    {
        m_weatherGeneratorDraft = weatherGeneratorSettings;
        m_weatherGeneratorDraftInitialized = true;
    }
    UpdateEffectiveTime(applicationTime);
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    m_currentCamera = CaptureCamera(camera);
    for (std::size_t index = 0; index < m_panelVisible.size(); ++index)
    {
        if (!m_panelVisible[index])
            continue;
        DrawControlWindow(static_cast<DeveloperUiPanel>(index), camera,
                          cloudParameters,
                          cloudShapeParameters, cloudDomainParameters,
                          cloudLodParameters,
                          optimizationParameters, optimizationPreset,
                          upsamplingParameters, resolutionPreset,
                          temporalParameters, temporalHistoryValid,
                          temporalAccumulatedFrames,
                          shadowParameters,
                          lightParameters, sunPreset, phasePreset,
                          environmentParameters, environmentPreset,
                          atmosphereParameters, groundLightingParameters,
                          toneMappingParameters,
                          stage15QualityPreset, stage15ConceptPreset,
                          stage15DiagnosticMode, stage15TemporalOverrideActive,
                          outputExtent, stage15CaptureState,
                          stage15CaptureCompletedSamples,
                          stage15CaptureStatus,
                          temporalResetCountLast60Frames,
                          lastTemporalResetReason,
                          temporalStatisticsValid,
                          temporalHistoryValidPercent,
                          temporalAverageHistoryWeight,
                          stage15StatusOverlayVisible,
                          performanceOverlayVisible, atmosphereLutSrvs,
                          atmosphereLutGenerations, atmosphereLutHashes,
                          atmosphereStatus,
                          weatherPreset, weatherGeneratorSettings,
                          cloudTypeMode, cloudAppearancePreset,
                          cloudAppearanceDirty, hasSavedCustomAppearance,
                          cloudAppearanceStatus,
                           cameraMoveSpeedMetersPerSecond,
                           noiseVolumeParameters, baseNoiseVolumeHash,
                          detailNoiseVolumeHash,
                          noiseVolumeGenerationMilliseconds,
                          weatherMapSrv, weatherMapStatus, vsyncEnabled,
                          shaderGeneration, shaderStatus, shaderError);
    }
    m_currentCamera = CaptureCamera(camera);
    if (stage15StatusOverlayVisible)
    {
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav;
        ImGui::SetNextWindowBgAlpha(0.72f);
        ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_Always);
        if (ImGui::Begin("Stage 15 Status", nullptr, flags))
        {
            ImGui::Text("Concept: %s", stage15::ConceptName(stage15ConceptPreset));
            ImGui::Text("Quality: %s", stage15::QualityName(stage15QualityPreset));
            const Stage11TemporalMode temporalMode =
                temporalParameters.temporalEnabled == 0u
                    ? Stage11TemporalMode::Off
                    : (temporalParameters.jitterEnabled != 0u
                        ? Stage11TemporalMode::Stable4Phase
                        : Stage11TemporalMode::FullResolution);
            ImGui::Text("Temporal: %s%s",
                stage11temporal::ModeName(temporalMode),
                stage15TemporalOverrideActive ? " (override)" : "");
            ImGui::Text("Physical Client: %d x %d | DPI %u (%.0f%%)",
                outputExtent.physicalClientWidth,
                outputExtent.physicalClientHeight, outputExtent.dpi,
                outputExtent.dpiScale * 100.0f);
            ImGui::Text("SwapChain / Viewport: %d x %d / %d x %d",
                outputExtent.swapChainWidth, outputExtent.swapChainHeight,
                outputExtent.viewportWidth, outputExtent.viewportHeight);
            ImGui::Text("Scene Color / Depth: %d x %d / %d x %d",
                outputExtent.sceneColorWidth, outputExtent.sceneColorHeight,
                outputExtent.sceneDepthWidth, outputExtent.sceneDepthHeight);
            ImGui::Text("Cloud RT / History: %d x %d / %d x %d",
                outputExtent.cloudWidth, outputExtent.cloudHeight,
                outputExtent.historyWidth, outputExtent.historyHeight);
            ImGui::Text("PMv2: %s | Native 1080p: %s",
                outputExtent.perMonitorV2 ? "yes" : "NO",
                outputExtent.native1080p ? "yes" : "no");
            if (temporalMode == Stage11TemporalMode::Stable4Phase)
                ImGui::Text("Phase: %u / 4 | History age: %u (%s)",
                    temporalParameters.frameIndex & 3u,
                    temporalAccumulatedFrames,
                    temporalHistoryValid ? "valid" : "invalid");
            else
                ImGui::Text("Phase: N/A | History age: %u (%s)",
                    temporalAccumulatedFrames,
                    temporalHistoryValid ? "valid" : "invalid");
            ImGui::Text("Temporal resets/60f: %u | last: %s",
                temporalResetCountLast60Frames,
                stage11temporal::ResetReasonName(lastTemporalResetReason));
            if (temporalStatisticsValid)
                ImGui::Text("History accepted: %.1f%% | avg weight: %.3f",
                    temporalHistoryValidPercent,
                    temporalAverageHistoryWeight);
            else
                ImGui::Text("History accepted / avg weight: N/A");
            ImGui::Text("View: %.1f m / %u | Cone %u",
                cloudParameters.stepSize, cloudParameters.maxViewSteps,
                optimizationParameters.coneSampleCount);
            ImGui::Text("Shadow: %s | Diagnostic: %s",
                stage12shadow::PresetName(static_cast<Stage12ShadowPreset>(
                    shadowParameters.shadowPreset)),
                stage15::DiagnosticName(stage15DiagnosticMode));
            if (stage15DiagnosticMode == Stage15DiagnosticMode::CaptureStill)
            {
                ImGui::Text("Capture: %s %u/%u",
                    stage15::CaptureStateName(stage15CaptureState),
                    stage15CaptureCompletedSamples,
                    stage15::kCaptureSampleCount);
                ImGui::TextWrapped("%s", stage15CaptureStatus.c_str());
            }
        }
        ImGui::End();
    }
    if (performanceOverlayVisible)
        DrawPerformanceOverlay(timing, cloudParameters, lightParameters,
                               vsyncEnabled);
}

void NoiseLab::DrawControlWindow(DeveloperUiPanel panel,
                                 Camera& camera,
                                 CloudParameters& cloudParameters,
                                 CloudShapeParameters& cloudShapeParameters,
                                 CloudDomainParameters& cloudDomainParameters,
                                 CloudLodParameters& cloudLodParameters,
                                 OptimizationParameters& optimizationParameters,
                                 Stage9OptimizationPreset& optimizationPreset,
                                 Stage10UpsamplingParameters& upsamplingParameters,
                                 Stage10ResolutionPreset& resolutionPreset,
                                 Stage11TemporalParameters& temporalParameters,
                                 bool temporalHistoryValid,
                                 std::uint32_t temporalAccumulatedFrames,
                                 Stage12ShadowParameters& shadowParameters,
                                 LightParameters& lightParameters,
                                 Stage6SunPreset& sunPreset,
                                 Stage7PhasePreset& phasePreset,
                                 EnvironmentParameters& environmentParameters,
                                 Stage8EnvironmentPreset& environmentPreset,
                                 AtmosphereParameters& atmosphereParameters,
                                 GroundLightingParameters& groundLightingParameters,
                                 ToneMappingParameters& toneMappingParameters,
                                 Stage15QualityPreset stage15QualityPreset,
                                 Stage15ConceptPreset stage15ConceptPreset,
                                 Stage15DiagnosticMode stage15DiagnosticMode,
                                 bool stage15TemporalOverrideActive,
                                 const Stage15OutputExtentSnapshot& outputExtent,
                                 Stage15CaptureState stage15CaptureState,
                                 std::uint32_t stage15CaptureCompletedSamples,
                                 const std::string& stage15CaptureStatus,
                                 std::uint32_t temporalResetCountLast60Frames,
                                 Stage11HistoryResetReason lastTemporalResetReason,
                                 bool temporalStatisticsValid,
                                 float temporalHistoryValidPercent,
                                 float temporalAverageHistoryWeight,
                                 bool& stage15StatusOverlayVisible,
                                 bool& performanceOverlayVisible,
                                 const std::array<ID3D11ShaderResourceView*, 6>& atmosphereLutSrvs,
                                 const std::array<std::uint64_t, 6>& atmosphereLutGenerations,
                                 const std::array<std::uint64_t, 6>& atmosphereLutHashes,
                                 const std::string& atmosphereStatus,
                                 Stage5WeatherPreset weatherPreset,
                                 const WeatherMapGeneratorSettings& weatherGeneratorSettings,
                                 CloudTypeMode cloudTypeMode,
                                 CloudAppearancePreset cloudAppearancePreset,
                                 bool cloudAppearanceDirty,
                                 bool hasSavedCustomAppearance,
                                 const std::string& cloudAppearanceStatus,
                                 float& cameraMoveSpeedMetersPerSecond,
                                 NoiseVolumeParameters& noiseVolumeParameters,
                                 std::uint64_t baseNoiseVolumeHash,
                                 std::uint64_t detailNoiseVolumeHash,
                                 double noiseVolumeGenerationMilliseconds,
                                 ID3D11ShaderResourceView* weatherMapSrv,
                                 const std::string& weatherMapStatus,
                                 bool& vsyncEnabled,
                                 std::uint64_t shaderGeneration,
                                 const std::string& shaderStatus,
                                 const std::string& shaderError)
{
    const std::size_t panelIndex = static_cast<std::size_t>(panel);
    const char* panelTitles[] = {
        "Noise & Density (F1)", "Weather Map (F2)",
        "Lighting (F3)", "Camera (F4)"
    };
    const bool noisePanel = panel == DeveloperUiPanel::Noise;
    const bool weatherPanel = panel == DeveloperUiPanel::Weather;
    const bool lightingPanel = panel == DeveloperUiPanel::Lighting;
    const bool cameraPanel = panel == DeveloperUiPanel::Camera;
    ImGui::SetNextWindowPos(
        ImVec2(20.0f + 36.0f * static_cast<float>(panelIndex),
               20.0f + 36.0f * static_cast<float>(panelIndex)),
        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(
        noisePanel ? ImVec2(620.0f, 680.0f) : ImVec2(520.0f, 640.0f),
        ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(panelTitles[panelIndex], &m_panelVisible[panelIndex]))
    {
        ImGui::End();
        return;
    }

    if (cameraPanel)
    {
        ImGui::SeparatorText("Stage 15 Presets");
        ImGui::Text("Concept: %s", stage15::ConceptName(stage15ConceptPreset));
        const bool diagnosticActive =
            stage15DiagnosticMode != Stage15DiagnosticMode::None;
        const Stage15ConceptPreset concepts[] = {
            Stage15ConceptPreset::UrbanFairWeather,
            Stage15ConceptPreset::MeadowBrokenClouds,
            Stage15ConceptPreset::DesertCirrus,
            Stage15ConceptPreset::SnowOvercast,
        };
        ImGui::BeginDisabled(diagnosticActive);
        for (std::size_t index = 0; index < std::size(concepts); ++index)
        {
            if (ImGui::Button(stage15::ConceptName(concepts[index])))
                m_stage15ConceptRequest = static_cast<int>(concepts[index]);
            if (index + 1u != std::size(concepts))
                ImGui::SameLine();
        }
        ImGui::Text("Quality: %s | Q cycles Low / Medium / High",
                    stage15::QualityName(stage15QualityPreset));
        const Stage15QualityPreset qualities[] = {
            Stage15QualityPreset::Low,
            Stage15QualityPreset::Medium,
            Stage15QualityPreset::High,
        };
        for (std::size_t index = 0; index < std::size(qualities); ++index)
        {
            if (ImGui::Button(stage15::QualityName(qualities[index])))
                m_stage15QualityRequest = static_cast<int>(qualities[index]);
            if (index + 1u != std::size(qualities))
                ImGui::SameLine();
        }
        ImGui::EndDisabled();
        if (diagnosticActive)
            ImGui::TextDisabled(
                "Concept, Quality and Q/T are locked until Restore Realtime.");
        const Stage11TemporalMode currentTemporalMode =
            temporalParameters.temporalEnabled == 0u
                ? Stage11TemporalMode::Off
                : (temporalParameters.jitterEnabled != 0u
                    ? Stage11TemporalMode::Stable4Phase
                    : Stage11TemporalMode::FullResolution);
        ImGui::Text("Temporal: %s%s | T toggles without changing RT size",
            stage11temporal::ModeName(currentTemporalMode),
            stage15TemporalOverrideActive ? " (override)" : "");
        ImGui::Text("Output %d x %d | Cloud %d x %d | History %d x %d",
            outputExtent.physicalClientWidth,
            outputExtent.physicalClientHeight,
            outputExtent.cloudWidth, outputExtent.cloudHeight,
            outputExtent.historyWidth, outputExtent.historyHeight);
        ImGui::Text("DPI %u (%.0f%%) | PMv2 %s | Native 1080p %s",
            outputExtent.dpi, outputExtent.dpiScale * 100.0f,
            outputExtent.perMonitorV2 ? "yes" : "NO",
            outputExtent.native1080p ? "yes" : "no");
        ImGui::Checkbox("Compact Stage 15 Overlay",
                        &stage15StatusOverlayVisible);
        ImGui::Checkbox("Performance Overlay", &performanceOverlayVisible);
        ImGui::TextDisabled(
            "Turn both overlays off, then close F4, for a clean manual capture.");
        if (ImGui::TreeNode("Advanced Capture / Reference"))
        {
            ImGui::BeginDisabled(
                stage15DiagnosticMode == Stage15DiagnosticMode::CaptureStill);
            if (ImGui::Button("Capture Still"))
                m_stage15DiagnosticRequest = static_cast<int>(
                    Stage15DiagnosticMode::CaptureStill);
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Reference"))
                m_stage15DiagnosticRequest = static_cast<int>(
                    Stage15DiagnosticMode::Reference);
            ImGui::SameLine();
            if (ImGui::Button("Restore Realtime"))
                m_stage15DiagnosticRequest = static_cast<int>(
                    Stage15DiagnosticMode::None);
            ImGui::Text("Current: %s",
                        stage15::DiagnosticName(stage15DiagnosticMode));
            if (stage15DiagnosticMode == Stage15DiagnosticMode::CaptureStill)
            {
                ImGui::Text("Capture: %s | %u/%u HDR samples",
                    stage15::CaptureStateName(stage15CaptureState),
                    stage15CaptureCompletedSamples,
                    stage15::kCaptureSampleCount);
                ImGui::TextWrapped("%s", stage15CaptureStatus.c_str());
            }
            ImGui::TextDisabled(
                "Capture requests physical Native 1920x1080, freezes the scene, and averages 4 HDR samples.");
            ImGui::TreePop();
        }
        ImGui::Separator();
    }

    const bool captureControlsLocked =
        stage15DiagnosticMode == Stage15DiagnosticMode::CaptureStill &&
        stage15CaptureState != Stage15CaptureState::Inactive &&
        stage15CaptureState != Stage15CaptureState::Failed;
    if (captureControlsLocked)
        ImGui::BeginDisabled();

    if (noisePanel)
    {
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

    if (ImGui::CollapsingHeader(
            "Cloud Type Settings", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("Current: %s",
                    CloudAppearancePresetName(cloudAppearancePreset));
        if (cloudAppearanceDirty)
            ImGui::TextColored(ImVec4(1.0f, 0.68f, 0.22f, 1.0f),
                "Unsaved changes will be replaced by another preset.");
        if (!cloudAppearanceStatus.empty())
            ImGui::TextWrapped("%s", cloudAppearanceStatus.c_str());
        if (ImGui::Button("Dense Mixed Default"))
            m_cloudAppearancePresetRequest = static_cast<int>(
                CloudAppearancePreset::DenseMixedDefault);
        ImGui::SameLine();
        if (ImGui::Button("Stratus (층운)"))
            m_cloudAppearancePresetRequest = static_cast<int>(
                CloudAppearancePreset::Stratus);
        ImGui::SameLine();
        if (ImGui::Button("Cumulus (적운)"))
            m_cloudAppearancePresetRequest = static_cast<int>(
                CloudAppearancePreset::Cumulus);
        ImGui::SameLine();
        if (ImGui::Button("Custom"))
            m_cloudAppearancePresetRequest = static_cast<int>(
                CloudAppearancePreset::Custom);
        if (ImGui::Button("Save Current as Custom"))
        {
            QueueWeatherGeneratorRequest(true);
            m_cloudAppearanceSaveRequest = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Saved slot: %s",
                            hasSavedCustomAppearance ? "available" : "empty");
        ImGui::TextDisabled(
            "Startup uses Stratus; Full Open World restores deterministic Dense Mixed Default.");
    }

    const char* outputs[] = {
        "Raw Noise", "Threshold Density", "Final Density",
        "Height Fraction", "Height Profile", "Base Density",
        "Detail Noise", "Erosion", "Detail Sample Mask",
        "Weather Coverage", "Cloud Type", "Weather Density Modifier",
        "Weather Threshold Density", "Typed Shape Profile", "Weather UV",
        "Base Volume R", "Base Volume G", "Base Volume B", "Base Volume A",
        "Base Volume Combined", "Detail Volume R", "Detail Volume G",
        "Detail Volume B", "Detail Volume A", "Detail Volume Combined",
        "Weather Thickness Potential", "Local Thickness", "Local Height Fraction",
        "Effective Shape Coverage", "Base Support Before Density"
    };
    int output = static_cast<int>(m_parameters.outputMode);
    if (ImGui::Combo("Output", &output, outputs, 30))
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
    }

    const CloudParameters before = cloudParameters;
    const CloudShapeParameters shapeBefore = cloudShapeParameters;
    const CloudDomainParameters domainBefore = cloudDomainParameters;
    const CloudLodParameters lodBefore = cloudLodParameters;
    const OptimizationParameters optimizationBefore = optimizationParameters;
    const Stage10UpsamplingParameters upsamplingBefore = upsamplingParameters;
    const Stage11TemporalParameters temporalBefore = temporalParameters;
    const Stage12ShadowParameters shadowBefore = shadowParameters;
    const NoiseVolumeParameters noiseVolumeBefore = noiseVolumeParameters;
    const LightParameters lightBefore = lightParameters;
    const EnvironmentParameters environmentBefore = environmentParameters;
    const AtmosphereParameters atmosphereBefore = atmosphereParameters;
    const GroundLightingParameters groundBefore = groundLightingParameters;
    const CloudAppearanceSettings appearanceBefore = CaptureCloudAppearance(
        cloudParameters, cloudShapeParameters, m_weatherGeneratorDraft);
    const CloudShapeMode shapeMode = cloudshape::Mode(
        cloudShapeParameters.shapeMode);
    const bool weatherPhysicalShape =
        shapeMode == CloudShapeMode::WeatherPhysicalThickness;
    const bool cirrusPhysicalShape =
        shapeMode == CloudShapeMode::CirrusPhysicalLayer;
    const bool bulkAdvectedShape = cloudshape::UsesPhysicalBulkAdvection(
        cloudShapeParameters.shapeMode);
    const bool stage15QualityControlsLocked =
        stage15DiagnosticMode != Stage15DiagnosticMode::None;

    if (noisePanel && ImGui::CollapsingHeader(
            "Open World Render Pipeline Compare"))
    {
        ImGui::BeginDisabled(stage15QualityControlsLocked);
        ImGui::TextDisabled(
            "Keeps the unified scene geometry and camera. Each step is deterministic and cumulative.");
        const char* labels[] = {
            "1 Legacy 1000x Baseline", "2 + Texture3D",
            "3 + Periodic Weather", "4 + Physical Shape",
            "5 Full Open World"
        };
        for (int index = 0; index < 5; ++index)
        {
            if (ImGui::Button(labels[index]))
                m_openWorldPipelinePresetRequest = index + 1;
        }
        ImGui::Text("Current pipeline: %s",
            stage13openworld::PipelinePresetName(
                m_openWorldPipelinePreset));
        ImGui::EndDisabled();
        if (stage15QualityControlsLocked)
            ImGui::TextDisabled(
                "Pipeline compare is locked until Restore Realtime.");
    }

    if (noisePanel && ImGui::CollapsingHeader(
            "Optimization", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::BeginDisabled(stage15QualityControlsLocked);
        ImGui::TextDisabled(
            "Every row is ordered left to right: lower GPU cost -> higher GPU cost.");
        auto applyMaster = [&](Stage9OptimizationPreset preset)
        {
            stage9optimization::ApplyPreset(
                optimizationParameters, preset,
                cloudParameters.maxViewSteps, cloudParameters.stepSize,
                lightParameters.maxLightSteps, lightParameters.lightStepSize,
                cloudParameters.transmittanceThreshold);
            optimizationPreset = preset;
        };
        const Stage9OptimizationPreset masterPresets[] = {
            Stage9OptimizationPreset::Balanced,
            Stage9OptimizationPreset::Conservative,
            Stage9OptimizationPreset::ApprovedReference,
            Stage9OptimizationPreset::FineReference,
        };
        ImGui::Text("Master: %s",
                    stage9optimization::PresetName(optimizationPreset));
        for (Stage9OptimizationPreset preset : masterPresets)
        {
            if (ImGui::Button(stage9optimization::PresetName(preset)))
                applyMaster(preset);
            if (preset != Stage9OptimizationPreset::FineReference)
                ImGui::SameLine();
        }

        auto markCustom = [&]() { optimizationPreset = Stage9OptimizationPreset::Custom; };
        ImGui::TextUnformatted("Empty Search:"); ImGui::SameLine();
        if (ImGui::Button("2x##Empty")) { optimizationParameters.emptySpaceSkippingEnabled = 1u; optimizationParameters.coarseStepMultiplier = 2.0f; markCustom(); }
        ImGui::SameLine();
        if (ImGui::Button("Off##Empty")) { optimizationParameters.emptySpaceSkippingEnabled = 0u; markCustom(); }

        ImGui::TextUnformatted("Early Exit:"); ImGui::SameLine();
        if (ImGui::Button("2%##Exit")) { optimizationParameters.viewEarlyExitEnabled = 1u; cloudParameters.transmittanceThreshold = 0.02f; markCustom(); }
        ImGui::SameLine();
        if (ImGui::Button("1%##Exit")) { optimizationParameters.viewEarlyExitEnabled = 1u; cloudParameters.transmittanceThreshold = 0.01f; markCustom(); }
        ImGui::SameLine();
        if (ImGui::Button("0.5%##Exit")) { optimizationParameters.viewEarlyExitEnabled = 1u; cloudParameters.transmittanceThreshold = 0.005f; markCustom(); }
        ImGui::SameLine();
        if (ImGui::Button("Off##Exit")) { optimizationParameters.viewEarlyExitEnabled = 0u; markCustom(); }

        ImGui::TextUnformatted("View:"); ImGui::SameLine();
        if (ImGui::Button("100->200m")) { cloudParameters.stepSize = 100.0f; cloudParameters.maxViewSteps = 512u; optimizationParameters.distanceStepEnabled = 1u; optimizationParameters.farStepMultiplier = 2.0f; markCustom(); }
        ImGui::SameLine();
        if (ImGui::Button("100->150m")) { cloudParameters.stepSize = 100.0f; cloudParameters.maxViewSteps = 512u; optimizationParameters.distanceStepEnabled = 1u; optimizationParameters.farStepMultiplier = 1.5f; markCustom(); }
        ImGui::SameLine();
        if (ImGui::Button("100m Fixed")) { cloudParameters.stepSize = 100.0f; cloudParameters.maxViewSteps = 512u; optimizationParameters.distanceStepEnabled = 0u; markCustom(); }
        ImGui::SameLine();
        if (ImGui::Button("50m Fine")) { cloudParameters.stepSize = 50.0f; cloudParameters.maxViewSteps = 1024u; optimizationParameters.distanceStepEnabled = 0u; markCustom(); }

        ImGui::TextUnformatted("Light:"); ImGui::SameLine();
        for (std::uint32_t taps : { 5u, 6u, 8u, 12u })
        {
            std::string label = "Cone " + std::to_string(taps) + "##Light";
            if (ImGui::Button(label.c_str())) { optimizationParameters.lightSamplingMode = static_cast<std::uint32_t>(Stage9LightSamplingMode::DeterministicCone); optimizationParameters.coneSampleCount = taps; markCustom(); }
            ImGui::SameLine();
        }
        if (ImGui::Button("Straight 80")) { optimizationParameters.lightSamplingMode = static_cast<std::uint32_t>(Stage9LightSamplingMode::StraightRay); lightParameters.maxLightSteps = 80u; lightParameters.lightStepSize = 250.0f; markCustom(); }
        ImGui::SameLine();
        if (ImGui::Button("Straight 320 Fine")) { optimizationParameters.lightSamplingMode = static_cast<std::uint32_t>(Stage9LightSamplingMode::StraightRay); lightParameters.maxLightSteps = 320u; lightParameters.lightStepSize = 62.5f; markCustom(); }

        ImGui::TextUnformatted("Support Precheck:"); ImGui::SameLine();
        if (ImGui::Button("On##Precheck")) { optimizationParameters.supportPrecheckEnabled = 1u; markCustom(); }
        ImGui::SameLine();
        if (ImGui::Button("Off##Precheck")) { optimizationParameters.supportPrecheckEnabled = 0u; markCustom(); }

        ImGui::TextUnformatted("Cone Angle (same cost):"); ImGui::SameLine();
        for (float angle : { 4.0f, 3.0f, 2.0f, 1.0f })
        {
            std::string label = std::to_string(static_cast<int>(angle)) +
                " deg##ConeAngle";
            if (ImGui::Button(label.c_str())) { optimizationParameters.coneAngleDegrees = angle; markCustom(); }
            if (angle != 1.0f) ImGui::SameLine();
        }
        ImGui::TextUnformatted("Cone Far Fraction (same cost):"); ImGui::SameLine();
        for (float fraction : { 0.75f, 0.77f, 0.85f, 0.95f })
        {
            std::string label = std::to_string(static_cast<int>(fraction * 100.0f)) +
                "%##ConeFar";
            if (ImGui::Button(label.c_str())) { optimizationParameters.lightFarSampleFraction = fraction; markCustom(); }
            if (fraction != 0.95f) ImGui::SameLine();
        }
        ImGui::EndDisabled();
        if (stage15QualityControlsLocked)
            ImGui::TextDisabled(
                "Optimization controls are locked until Restore Realtime.");
    }

    if (noisePanel && ImGui::CollapsingHeader(
            "Low Resolution / Upsampling", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::BeginDisabled(stage15QualityControlsLocked);
        ImGui::TextDisabled(
            "Every row is ordered left to right: lower GPU cost -> higher GPU cost.");
        ImGui::Text("Resolution: %s",
                    stage10upsampling::ResolutionPresetName(resolutionPreset));
        for (Stage10ResolutionPreset preset :
             stage10upsampling::kRuntimeResolutionCandidates)
        {
            if (ImGui::Button(stage10upsampling::ResolutionPresetName(preset)))
            {
                stage10upsampling::ApplyResolutionPreset(
                    upsamplingParameters, preset);
                resolutionPreset = preset;
            }
            if (preset != Stage10ResolutionPreset::Full)
                ImGui::SameLine();
        }

        ImGui::Text("Filter: %s", stage10upsampling::FilterName(
            static_cast<Stage10UpsampleFilter>(upsamplingParameters.filterMode)));
        for (Stage10UpsampleFilter filter :
             stage10upsampling::kRuntimeFilterCandidates)
        {
            if (ImGui::Button(stage10upsampling::FilterName(filter)))
                upsamplingParameters.filterMode =
                    static_cast<std::uint32_t>(filter);
            if (filter != Stage10UpsampleFilter::Joint4)
                ImGui::SameLine();
        }

        ImGui::TextUnformatted("Scene Depth (same cost):"); ImGui::SameLine();
        for (float value : { 0.00125f, 0.0025f, 0.005f, 0.01f })
        {
            std::string label = std::to_string(value * 100.0f) +
                "%##SceneDepth";
            if (ImGui::Button(label.c_str()))
                upsamplingParameters.sceneDepthRelativeSigma = value;
            if (value != 0.01f) ImGui::SameLine();
        }
        ImGui::TextUnformatted("Cloud Depth (same cost):"); ImGui::SameLine();
        for (float value : { 0.005f, 0.01f, 0.02f, 0.04f })
        {
            std::string label = std::to_string(value * 100.0f) +
                "%##CloudDepth";
            if (ImGui::Button(label.c_str()))
                upsamplingParameters.cloudDepthRelativeSigma = value;
            if (value != 0.04f) ImGui::SameLine();
        }
        ImGui::TextUnformatted("Transmittance Sigma (same cost):"); ImGui::SameLine();
        for (float value : { 0.05f, 0.10f, 0.15f, 0.20f })
        {
            std::string label = std::to_string(value) + "##TSigma";
            if (ImGui::Button(label.c_str()))
                upsamplingParameters.transmittanceSigma = value;
            if (value != 0.20f) ImGui::SameLine();
        }
        ImGui::TextUnformatted("Minimum Weight (same cost):"); ImGui::SameLine();
        for (float value : { 1.0e-5f, 1.0e-4f, 1.0e-3f })
        {
            std::string label = std::to_string(value) + "##MinimumWeight";
            if (ImGui::Button(label.c_str()))
                upsamplingParameters.minimumWeight = value;
            if (value != 1.0e-3f) ImGui::SameLine();
        }
        ImGui::Text("Current scale %.2f | minimum weight %.1e",
                    upsamplingParameters.resolutionScale,
                    upsamplingParameters.minimumWeight);
        ImGui::TextDisabled(
            "Stage 10 starts at Full until automatic and user quality gates approve a lower resolution.");
        ImGui::EndDisabled();
        if (stage15QualityControlsLocked)
            ImGui::TextDisabled(
                "Resolution and filter controls are locked until Restore Realtime.");
    }

    if (noisePanel && ImGui::CollapsingHeader(
            "Temporal", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::BeginDisabled(stage15QualityControlsLocked);
        const Stage11TemporalMode temporalMode =
            temporalParameters.temporalEnabled == 0u
                ? Stage11TemporalMode::Off
                : (temporalParameters.jitterEnabled != 0u
                    ? Stage11TemporalMode::Stable4Phase
                    : Stage11TemporalMode::FullResolution);
        ImGui::Text("Mode: %s", stage11temporal::ModeName(temporalMode));
        if (ImGui::Button("Off##Temporal"))
            stage11temporal::ApplyMode(
                temporalParameters, Stage11TemporalMode::Off);
        ImGui::SameLine();
        if (ImGui::Button("Stable 4-Phase##Temporal"))
            stage11temporal::ApplyMode(
                temporalParameters, Stage11TemporalMode::Stable4Phase);
        ImGui::SameLine();
        if (ImGui::Button("Full Resolution##Temporal"))
            stage11temporal::ApplyMode(
                temporalParameters, Stage11TemporalMode::FullResolution);
        ImGui::SameLine();
        if (ImGui::Button("Reset History"))
            m_temporalResetRequested = true;

        if (temporalMode == Stage11TemporalMode::Stable4Phase)
            ImGui::Text("Phase %u | history %s | age %u",
                        temporalParameters.frameIndex & 3u,
                        temporalHistoryValid ? "valid" : "invalid",
                        temporalAccumulatedFrames);
        else
            ImGui::Text("Phase N/A | history %s | age %u",
                        temporalHistoryValid ? "valid" : "invalid",
                        temporalAccumulatedFrames);
        ImGui::Text("Resets last 60 frames: %u | Last: %s",
                    temporalResetCountLast60Frames,
                    stage11temporal::ResetReasonName(
                        lastTemporalResetReason));
        if (temporalStatisticsValid)
            ImGui::Text("Accepted history: %.1f%% | Mip-mean weight: %.3f",
                        temporalHistoryValidPercent,
                        temporalAverageHistoryWeight);
        else
            ImGui::Text("Accepted history / Mip-mean weight: N/A");
        ImGui::TextDisabled(
            "GPU mip reduction is a low-cost whole-screen estimate; reset discards pending older samples.");

        ImGui::TextUnformatted("History Weight:"); ImGui::SameLine();
        for (float value : { 0.80f, 0.90f, 0.95f })
        {
            std::string label = std::to_string(value) + "##HistoryWeight";
            if (ImGui::Button(label.c_str()))
                temporalParameters.historyWeight = value;
            if (value != 0.95f) ImGui::SameLine();
        }
        ImGui::TextUnformatted("Near Cloud History:"); ImGui::SameLine();
        if (ImGui::Button("Off##NearHistory"))
        {
            temporalParameters.nearHistoryFadeStartMeters = 0.0f;
            temporalParameters.nearHistoryFadeEndMeters = 1.0f;
        }
        ImGui::SameLine();
        if (ImGui::Button("1 km##NearHistory"))
        {
            temporalParameters.nearHistoryFadeStartMeters = 250.0f;
            temporalParameters.nearHistoryFadeEndMeters = 1000.0f;
        }
        ImGui::SameLine();
        if (ImGui::Button("3 km##NearHistory"))
        {
            temporalParameters.nearHistoryFadeStartMeters = 1000.0f;
            temporalParameters.nearHistoryFadeEndMeters = 3000.0f;
        }
        bool neighborhoodClamp =
            temporalParameters.neighborhoodClampingEnabled != 0u;
        if (ImGui::Checkbox("Neighborhood Clamp", &neighborhoodClamp))
            temporalParameters.neighborhoodClampingEnabled =
                neighborhoodClamp ? 1u : 0u;
        ImGui::EndDisabled();
        if (stage15QualityControlsLocked)
            ImGui::TextDisabled(
                "Temporal controls are locked until Restore Realtime.");
        ImGui::TextDisabled(
            "Low/Medium use Stable 4-Phase; High Full uses Full Resolution without 4-phase jitter.");
    }

    if (noisePanel && ImGui::CollapsingHeader(
            "Noise / Weather / Shape / Sampling Debug View",
            ImGuiTreeNodeFlags_DefaultOpen))
    {
        struct DebugViewOption { const char* name; CloudDebugMode mode; };
        static constexpr DebugViewOption options[] = {
            { "Composite", CloudDebugMode::Composite },
            { "Raw Noise", CloudDebugMode::RawNoise },
            { "Threshold Density", CloudDebugMode::ThresholdDensity },
            { "Weather Coverage", CloudDebugMode::WeatherCoverage },
            { "Cloud Type", CloudDebugMode::CloudType },
            { "Weather Threshold", CloudDebugMode::WeatherThresholdDensity },
            { "Typed Shape Profile", CloudDebugMode::TypedShapeProfile },
            { "Base Density", CloudDebugMode::BaseDensity },
            { "Detail Noise", CloudDebugMode::DetailNoise },
            { "Erosion", CloudDebugMode::Erosion },
            { "Detail Sample Mask", CloudDebugMode::DetailSampleMask },
            { "Final Density", CloudDebugMode::FinalDensity },
            { "Noise UVW", CloudDebugMode::NoiseUvw },
            { "Height Fraction", CloudDebugMode::HeightFraction },
            { "Height Profile", CloudDebugMode::HeightProfile },
            { "Cloud Segment Length", CloudDebugMode::CloudSegmentLength },
            { "Actual View Step", CloudDebugMode::ActualViewStepLength },
            { "Cloud Hit Mask", CloudDebugMode::CloudHitMask },
            { "Base Volume R", CloudDebugMode::BaseVolumeR },
            { "Base Volume G", CloudDebugMode::BaseVolumeG },
            { "Base Volume B", CloudDebugMode::BaseVolumeB },
            { "Base Volume A", CloudDebugMode::BaseVolumeA },
            { "Base Volume Combined", CloudDebugMode::BaseVolumeCombined },
            { "Detail Volume R", CloudDebugMode::DetailVolumeR },
            { "Detail Volume G", CloudDebugMode::DetailVolumeG },
            { "Detail Volume B", CloudDebugMode::DetailVolumeB },
            { "Detail Volume A", CloudDebugMode::DetailVolumeA },
            { "Detail Volume Combined", CloudDebugMode::DetailVolumeCombined },
            { "Texture Wrap Difference", CloudDebugMode::TextureWrapDifference },
            { "Weather Thickness Potential", CloudDebugMode::WeatherThicknessPotential },
            { "Local Thickness", CloudDebugMode::LocalThickness },
            { "Local Height", CloudDebugMode::LocalHeightFraction },
            { "Effective Shape Coverage", CloudDebugMode::EffectiveShapeCoverage },
            { "Base Support Before Density", CloudDebugMode::BaseSupportBeforeDensity },
            { "Executed View Samples", CloudDebugMode::ExecutedViewSamples },
            { "Skipped Distance", CloudDebugMode::SkippedDistance },
            { "Early Exit Savings", CloudDebugMode::EarlyExitSavings },
            { "Support Precheck Skip", CloudDebugMode::SupportPrecheckSkip },
            { "Low-resolution Grid", CloudDebugMode::LowResolutionGrid },
            { "Scene Rejection", CloudDebugMode::UpsampleSceneRejection },
            { "Cloud Depth Weight", CloudDebugMode::UpsampleCloudDepthWeight },
            { "Transmittance Weight", CloudDebugMode::UpsampleTransmittanceWeight },
            { "Accepted Joint Tap Count", CloudDebugMode::UpsampleAcceptedTapCount },
            { "Temporal Jitter Phase", CloudDebugMode::TemporalJitterPhase },
            { "Temporal Reprojection Motion", CloudDebugMode::TemporalReprojectionMotion },
            { "Temporal History Validity", CloudDebugMode::TemporalHistoryValidity },
            { "Temporal History Weight", CloudDebugMode::TemporalHistoryWeight },
            { "Temporal Current/History Difference", CloudDebugMode::TemporalCurrentHistoryDifference },
            { "Temporal Current Source Validity", CloudDebugMode::TemporalCurrentSourceValidity },
        };
        int selected = 0;
        const auto current = static_cast<CloudDebugMode>(cloudParameters.debugMode);
        for (int index = 0; index < static_cast<int>(std::size(options)); ++index)
            if (options[index].mode == current)
                selected = index;
        if (ImGui::BeginCombo("Main View Debug", options[selected].name))
        {
            for (int index = 0; index < static_cast<int>(std::size(options)); ++index)
            {
                const bool isSelected = index == selected;
                if (ImGui::Selectable(options[index].name, isSelected))
                    cloudParameters.debugMode = static_cast<std::int32_t>(
                        options[index].mode);
                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (current == CloudDebugMode::UpsampleAcceptedTapCount)
        {
            ImGui::TextWrapped(
                "Accepted Joint taps: black=0, 25/50/75/100%% gray=1/2/3/4 hard-valid taps. Pure sky should average at least 2; geometry/sky boundaries may reject more. Flat dark gray means Full RT (spatial resolve not applicable).");
        }
        else if (current == CloudDebugMode::TemporalJitterPhase)
        {
            ImGui::TextWrapped(
                "4-phase source lattice: the bright phase color marks the one Full pixel sampled by each low texel; four frames must visit every pixel of its 2x2 once. Gray means Full RT (not applicable).");
        }
        else if (current == CloudDebugMode::TemporalReprojectionMotion)
        {
            ImGui::TextWrapped(
                "Motion: neutral gray = 0 px, RG = signed +/-16 px, blue = behind/outside, dark gray = no cloud/history.");
        }
        else if (current == CloudDebugMode::TemporalHistoryValidity)
        {
            ImGui::TextWrapped(
                "Validity: green accepted, gray no history, magenta clip.w, blue UV bounds, cyan max motion.");
            ImGui::TextWrapped(
                "Orange scene, yellow cloud depth, red T, purple near fade, pink non-finite.");
        }
        else if (current == CloudDebugMode::TemporalCurrentHistoryDifference)
        {
            ImGui::TextWrapped(
                "Raw difference x4: red scattering, green T, yellow both, blue = current/history unavailable.");
            ImGui::TextWrapped(
                "Different jitter phases can remain non-black after convergence; judge stability in Composite.");
        }
        else if (current == CloudDebugMode::TemporalCurrentSourceValidity)
        {
            ImGui::TextWrapped(
                "Current source: green valid, red scene class, yellow geometry surface/plane, blue no finite/guide/source.");
            ImGui::TextWrapped(
                "Gray = invalid current skipped while accepted history is held.");
        }
        ImGui::TextDisabled("0-9 selects the nine frequent pipeline views.");
    }

    if (noisePanel && ImGui::CollapsingHeader(
            "3D Noise Volumes (Stage 13-4B)", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const bool textureSource = noiseVolumeParameters.noiseSource ==
            static_cast<std::uint32_t>(NoiseSource::Texture3D);
        ImGui::Text("Noise source: %s",
                    textureSource ? "Texture3D" : "Procedural Legacy");
        if (!textureSource)
            ImGui::TextDisabled("Procedural Legacy is controlled by Pipeline Compare.");
        if (ImGui::Button("Regenerate 3D Noise"))
            m_noiseVolumeRegenerateRequest = true;
        {
            float baseXyzWorldSize = noiseVolumeParameters.baseWorldSizeMeters;
            if (ImGui::SliderFloat("Base XYZ World Size", &baseXyzWorldSize,
                                   1.0f, 16000.0f, "%.0f m",
                                   ImGuiSliderFlags_Logarithmic))
            {
                noiseVolumeParameters.baseWorldSizeMeters = baseXyzWorldSize;
                noiseVolumeParameters.baseVerticalWorldSizeMeters = baseXyzWorldSize;
            }
            ImGui::TextDisabled("Shared Base X/Y/Z physical scale for both domains.");
        }
        ImGui::Text("Base %u^3 RGBA8 | %.0f m | %.2f min samples/wavelength",
                    noiseVolumeParameters.baseResolution,
                    noiseVolumeParameters.baseWorldSizeMeters,
                    stage13noise::SamplesPerWavelength(
                        noiseVolumeParameters.baseWorldSizeMeters,
                        noiseVolumeParameters.baseFrequencies.w,
                        cloudParameters.stepSize));
        ImGui::Text("  Y world size %.0f m | %.2f min vertical samples/wavelength",
                    noiseVolumeParameters.baseVerticalWorldSizeMeters,
                    stage13noise::SamplesPerWavelength(
                        noiseVolumeParameters.baseVerticalWorldSizeMeters,
                        noiseVolumeParameters.baseFrequencies.w,
                        cloudParameters.stepSize));
        ImGui::Text("  frequencies %u/%u/%u/%u | 8.000 MiB | hash %016llx",
                    noiseVolumeParameters.baseFrequencies.x,
                    noiseVolumeParameters.baseFrequencies.y,
                    noiseVolumeParameters.baseFrequencies.z,
                    noiseVolumeParameters.baseFrequencies.w,
                    static_cast<unsigned long long>(baseNoiseVolumeHash));
        ImGui::Text("Detail %u^3 RGBA8 | %.0f m | %.2f samples/wavelength",
                    noiseVolumeParameters.detailResolution,
                    noiseVolumeParameters.detailWorldSizeMeters,
                    stage13noise::SamplesPerWavelength(
                        noiseVolumeParameters.detailWorldSizeMeters,
                        noiseVolumeParameters.detailFrequencies.x,
                        cloudParameters.stepSize));
        ImGui::Text("  frequencies %u/%u/%u/%u | 0.125 MiB | hash %016llx",
                    noiseVolumeParameters.detailFrequencies.x,
                    noiseVolumeParameters.detailFrequencies.y,
                    noiseVolumeParameters.detailFrequencies.z,
                    noiseVolumeParameters.detailFrequencies.w,
                    static_cast<unsigned long long>(detailNoiseVolumeHash));
        ImGui::Text("Generation/readback: %.3f ms",
                    noiseVolumeGenerationMilliseconds);
        ImGui::TextDisabled(
            "Weather places 64 km cloud regions; Base shapes mass; Detail erodes boundaries.");
    }

    cloudDomainParameters = SanitizeCloudDomainParameters(
        cloudDomainParameters);
    m_weatherPreset = weatherPreset;
    m_weatherMapPreviewSrv = weatherMapSrv;
    if (weatherPanel && ImGui::CollapsingHeader(
            "Weather map", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const char* presets[] = { "Periodic Perlin", "Channel Debug" };
        int presetIndex = weatherPreset == Stage5WeatherPreset::ChannelDebug ? 1 : 0;
        if (ImGui::Combo("Weather Preset", &presetIndex, presets, 2))
        {
            m_weatherPresetRequest = static_cast<int>(presetIndex == 0
                ? Stage5WeatherPreset::PeriodicPerlin
                : Stage5WeatherPreset::ChannelDebug);
            m_weatherPreset = static_cast<Stage5WeatherPreset>(
                m_weatherPresetRequest);
        }
        const char* typeName = "Weather Map";
        switch (cloudTypeMode)
        {
        case CloudTypeMode::Stratus: typeName = "Stratus (fixed G=0)"; break;
        case CloudTypeMode::Mixed: typeName = "Mixed (fixed G=0.5)"; break;
        case CloudTypeMode::Cumulus: typeName = "Cumulus (fixed G=1)"; break;
        case CloudTypeMode::WeatherMap: typeName = "Weather Map G"; break;
        }
        ImGui::Text("Cloud Type G mode (read only): %s", typeName);
        ImGui::TextDisabled("Change cloud type from F1 Cloud Type Settings.");
        if (m_weatherMapPreviewSrv)
        {
            ImGui::TextUnformatted(
                "Actual RGBA texture (R coverage / G type / B density / A local thickness)");
            ImGui::Image(ImTextureRef(static_cast<ImTextureID>(
                reinterpret_cast<std::uintptr_t>(m_weatherMapPreviewSrv))),
                ImVec2(256.0f, 256.0f));
        }
        ImGui::SliderFloat("Weather World Size", &cloudParameters.weatherMapWorldSize,
                           0.1f, 100000.0f, "%.3f m", ImGuiSliderFlags_Logarithmic);
        ImGui::BeginDisabled(bulkAdvectedShape);
        ImGui::SliderFloat("Weather Wind Speed", &cloudParameters.weatherMapWindSpeed,
                           0.0f, 1000.0f, "%.2f m/s");
        ImGui::EndDisabled();
        if (bulkAdvectedShape)
            ImGui::TextDisabled(
                "Legacy Shape only; physical shapes use Bulk Wind.");
        ImGui::DragFloat2("Weather Offset", &cloudParameters.weatherMapOffset.x,
                          0.01f, -10.0f, 10.0f, "%.2f cycle");
        if (ImGui::Button("Reset Weather Transform"))
        {
            cloudParameters.weatherMapWorldSize = 16.0f;
            cloudParameters.weatherMapWindSpeed = 0.10f;
            cloudParameters.weatherMapOffset = { 0.0f, 0.0f };
        }
    }

    const bool generatorEnabled =
        m_weatherPreset == Stage5WeatherPreset::PeriodicPerlin;
    bool generatorChanged = false;
    if (weatherPanel && ImGui::CollapsingHeader(
            "Periodic Perlin Generator", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::TextWrapped(
            "CPU RGBA map -> DEFAULT texture UpdateSubresource (max 10 Hz)");
        ImGui::TextWrapped("%s", weatherMapStatus.c_str());
        if (!generatorEnabled)
        {
            ImGui::TextColored(
                ImVec4(0.45f, 0.75f, 1.0f, 1.0f),
                "Settings are editable; select Periodic Perlin above to preview them.");
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
        ImGui::TextDisabled(
            "A Local Thickness uses fixed seed/period/weight: %u, %u/%u, %.2f.",
            m_weatherGeneratorDraft.localThickness.seed,
            m_weatherGeneratorDraft.localThickness.macroPeriod,
            m_weatherGeneratorDraft.localThickness.detailPeriod,
            m_weatherGeneratorDraft.localThickness.detailWeight);
        generatorChanged |= ImGui::SliderFloat(
            "Thickness-Coverage Link",
            &m_weatherGeneratorDraft.thicknessCoverageInfluence,
            0.0f, 1.0f, "%.3f");

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
            m_weatherGeneratorDraft.localThickness.seed =
                nextSeed(m_weatherGeneratorDraft.localThickness.seed);
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

    if (noisePanel && ImGui::CollapsingHeader(
            "Shared cloud parameters", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SliderFloat("Noise Scale", &cloudParameters.baseNoiseScale,
                           0.0001f, 2.0f, "%.6f cycle/m", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Coverage", &cloudParameters.coverage, 0.0f, 1.0f);
        ImGui::SliderFloat("Density", &cloudParameters.densityMultiplier, 0.0f, 4.0f);
        ImGui::SliderFloat("Extinction / m",
                           &cloudParameters.extinctionCoefficient,
                           0.00001f, 0.002f, "%.6f",
                           ImGuiSliderFlags_Logarithmic);
        ImGui::DragFloat("Noise Offset", &cloudParameters.noiseOffset,
                         0.01f, -20.0f, 20.0f);
        ImGui::DragFloat3("Wind Direction", &cloudParameters.windDirection.x,
                          0.01f, -1.0f, 1.0f);
        ImGui::SliderFloat(bulkAdvectedShape
                               ? "Cloud Wind Speed (Bulk)"
                               : "Wind Speed",
                           &cloudParameters.windSpeed,
                           0.0f, 1000.0f, "%.2f m/s");
    }

    if (noisePanel && ImGui::CollapsingHeader(
            "Height profile", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (!cirrusPhysicalShape)
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
        const float selectedWorldY = cloudParameters.cloudBoundsMin.y +
            (cloudParameters.cloudBoundsMax.y - cloudParameters.cloudBoundsMin.y) *
            m_parameters.normalizedSlicePosition.y;
        const float selectedHeight = stage3::EvaluateHeightFraction(
            selectedWorldY, cloudParameters.cloudBoundsMin.y, cloudParameters.cloudBoundsMax.y);
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
        ImGui::Text("Shape mode: %s", cloudshape::ModeDisplayName(
            cloudShapeParameters.shapeMode));
        if (weatherPhysicalShape)
        {
            const float minimumThickness = 1.0f;
            const float maximumThickness = 6000.0f;
            ImGui::SliderFloat("Stratus Min Thickness",
                &cloudShapeParameters.stratusMinimumThicknessMeters,
                minimumThickness, maximumThickness, "%.0f m");
            ImGui::SliderFloat("Stratus Max Thickness",
                &cloudShapeParameters.stratusMaximumThicknessMeters,
                minimumThickness, maximumThickness, "%.0f m");
            ImGui::SliderFloat("Cumulus Min Thickness",
                &cloudShapeParameters.cumulusMinimumThicknessMeters,
                minimumThickness, maximumThickness, "%.0f m");
            ImGui::SliderFloat("Cumulus Max Thickness",
                &cloudShapeParameters.cumulusMaximumThicknessMeters,
                minimumThickness, maximumThickness, "%.0f m");
            ImGui::SliderFloat("Stratus Bottom Fade End", &cloudShapeParameters.stratusBottomFadeEnd,
                0.01f, 0.99f, "bottom %.2f");
            ImGui::SliderFloat("Stratus Top Fade", &cloudShapeParameters.stratusTopFadeStart,
                0.01f, 0.99f, "top %.2f");
            ImGui::SliderFloat("Mixed Bottom Fade", &cloudShapeParameters.mixedBottomFadeEnd,
                0.01f, 0.99f, "%.2f");
            ImGui::SliderFloat("Mixed Top Fade", &cloudShapeParameters.mixedTopFadeStart,
                0.01f, 0.99f, "%.2f");
            ImGui::SliderFloat("Cumulus Bottom Fade", &cloudShapeParameters.cumulusBottomFadeEnd,
                0.01f, 0.99f, "%.2f");
            ImGui::SliderFloat("Cumulus Top Fade", &cloudShapeParameters.cumulusTopFadeStart,
                0.01f, 0.99f, "%.2f");
            ImGui::SliderFloat("Cumulus Lower Mass", &cloudShapeParameters.cumulusUpperMassBottom,
                0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Cumulus Upper Mass Start",
                &cloudShapeParameters.cumulusUpperMassStart,
                0.0f, 0.99f, "%.2f");
            ImGui::SliderFloat("Cumulus Upper Mass End",
                &cloudShapeParameters.cumulusUpperMassEnd,
                0.01f, 1.0f, "%.2f");
            if (ImGui::Button("Reset Shared Shape"))
            {
                CloudParameters unusedCloud = cloudParameters;
                WeatherMapGeneratorSettings unusedWeather =
                    m_weatherGeneratorDraft;
                ApplyCloudAppearance(DenseMixedAppearance(), unusedCloud,
                                     cloudShapeParameters, unusedWeather);
                cloudShapeParameters.shapeMode = static_cast<std::uint32_t>(
                    CloudShapeMode::WeatherPhysicalThickness);
            }
        }
        else if (cirrusPhysicalShape)
        {
            ImGui::Text("Flow XZ: %.6f, %.6f",
                cloudShapeParameters.cirrusFlowDirectionXZ.x,
                cloudShapeParameters.cirrusFlowDirectionXZ.y);
            ImGui::Text("Base scale: along %.0f m | across %.0f m | vertical %.0f m",
                cloudShapeParameters.cirrusBaseAlongScaleMeters,
                cloudShapeParameters.cirrusBaseAcrossScaleMeters,
                cloudShapeParameters.cirrusBaseVerticalScaleMeters);
            ImGui::Text("Detail scale: along %.0f m | across %.0f m | vertical %.0f m",
                cloudShapeParameters.cirrusDetailAlongScaleMeters,
                cloudShapeParameters.cirrusDetailAcrossScaleMeters,
                cloudShapeParameters.cirrusDetailVerticalScaleMeters);
            ImGui::Text("Local thickness: %.0f - %.0f m",
                cloudShapeParameters.cirrusMinimumThicknessMeters,
                cloudShapeParameters.cirrusMaximumThicknessMeters);
            ImGui::Text("Vertical profile: center %.3f | half-width %.3f",
                cloudShapeParameters.cirrusVerticalProfileCenter,
                cloudShapeParameters.cirrusVerticalProfileHalfWidth);
            ImGui::TextDisabled(
                "Stage 15 Cirrus values are read-only here; edit the concept descriptor in code.");
        }
        else
        {
            ImGui::SliderFloat("Minimum Local Thickness",
                               &cloudParameters.minimumLocalThicknessFraction,
                               0.1f, 1.0f, "%.2f layer fraction");
            ImGui::SliderFloat("Local Height Variation",
                               &cloudParameters.localHeightVariation,
                               0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Cumulus Top Boost",
                               &cloudParameters.cumulusTopBoost,
                               0.0f, 1.0f, "%.2f");
        }
        ImGui::TextDisabled(
            "Weather Thickness Potential: white selects a thicker typed column.");
        if (cirrusPhysicalShape)
            ImGui::TextDisabled(
                "Cirrus Local Thickness maps Weather A to 0.5-1.5 km; Local Height is 0-1 inside that layer.");
        else
        {
            ImGui::TextDisabled("Local Thickness: black=0 km, white=6 km.");
            ImGui::TextDisabled(
                "Local Height: local bottom 0, local top 1; outside is black.");
        }
    }
    cloudParameters.bottomFadeEnd = std::clamp(cloudParameters.bottomFadeEnd, 0.01f, 0.99f);
    cloudParameters.topFadeStart = std::clamp(cloudParameters.topFadeStart, 0.01f, 0.99f);
    cloudParameters.minimumLocalThicknessFraction = std::clamp(
        cloudParameters.minimumLocalThicknessFraction, 0.1f, 1.0f);
    cloudParameters.localHeightVariation = std::clamp(
        cloudParameters.localHeightVariation, 0.0f, 1.0f);
    cloudParameters.cumulusTopBoost = std::clamp(
        cloudParameters.cumulusTopBoost, 0.0f, 1.0f);

    if (noisePanel && ImGui::CollapsingHeader(
            "Detail erosion", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SliderFloat("Detail Scale", &cloudParameters.detailNoiseScale,
                           0.0001f, 16.0f, "%.6f cycle/m", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Erosion Strength", &cloudParameters.detailErosionStrength,
                           0.0f, 1.0f, "%.3f");
        ImGui::BeginDisabled(bulkAdvectedShape);
        ImGui::SliderFloat("Detail Wind Speed", &cloudParameters.detailWindSpeed,
                           0.0f, 1000.0f, "%.2f m/s");
        ImGui::EndDisabled();
        if (bulkAdvectedShape)
            ImGui::TextDisabled(
                "Legacy Shape only; physical shapes use Bulk Wind.");
        ImGui::DragFloat("Detail Offset", &cloudParameters.detailNoiseOffset,
                         0.01f, -50.0f, 50.0f, "%.2f cycle");
        if (ImGui::Button("Reset Detail"))
        {
            cloudParameters.detailNoiseScale = 2.5f;
            cloudParameters.detailErosionStrength = 0.25f;
            cloudParameters.detailWindSpeed = 0.45f;
            cloudParameters.detailNoiseOffset = 17.3f;
        }
    }
    cloudParameters.detailNoiseScale = std::max(cloudParameters.detailNoiseScale, 1e-4f);
    cloudParameters.detailErosionStrength = std::clamp(
        cloudParameters.detailErosionStrength, 0.0f, 1.0f);
    cloudParameters.detailWindSpeed = std::max(cloudParameters.detailWindSpeed, 0.0f);

    if (lightingPanel && ImGui::CollapsingHeader("Atmosphere Debug"))
    {
        static const char* debugViews[] = {
            "None", "Transmittance", "1-T", "Optical Depth",
            "Multi Scattering", "Sky View", "Sky Irradiance",
            "Aerial Radiance", "Aerial Transmittance", "Atmosphere Sun",
            "Surface Direct", "Surface Sky", "Aerial Only", "HDR Pre-Tone"
        };
        int debugView = static_cast<int>(atmosphereParameters.debugView);
        if (ImGui::Combo("Atmosphere Debug View", &debugView,
                         debugViews, static_cast<int>(std::size(debugViews))))
            atmosphereParameters.debugView = static_cast<Stage14DebugView>(
                debugView);
        const char* channels[] = { "RGB", "R", "G", "B" };
        int channel = static_cast<int>(atmosphereParameters.debugChannel);
        if (ImGui::Combo("LUT Channel", &channel, channels, 4))
            atmosphereParameters.debugChannel =
                static_cast<Stage14DebugChannel>(channel);
        ImGui::SliderFloat("LUT Debug Exposure",
            &atmosphereParameters.debugExposure,
            0.01f, 64.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
        int aerialSlice = static_cast<int>(atmosphereParameters.aerialSlice);
        if (ImGui::SliderInt("Aerial Slice", &aerialSlice, 0, 31))
            atmosphereParameters.aerialSlice = static_cast<float>(aerialSlice);

        const char* lutNames[4] = {
            "Transmittance 256x64 RGBA16F (height / zenith)",
            "Multi Scattering 32x32 RGBA16F (sun zenith / height)",
            "Sky View 192x108 RGBA16F (sun-relative azimuth / view zenith)",
            "Sky Irradiance 64x16 RGBA16F (sun zenith / height)"
        };
        for (int index = 0; index < 4; ++index)
        {
            ImGui::Text("%s | generation %llu", lutNames[index],
                static_cast<unsigned long long>(atmosphereLutGenerations[index]));
            if (atmosphereLutSrvs[index])
                ImGui::Image(ImTextureRef(static_cast<ImTextureID>(
                    reinterpret_cast<std::uintptr_t>(atmosphereLutSrvs[index]))),
                    ImVec2(256.0f, index == 2 ? 144.0f : 64.0f));
        }
        ImGui::Text("Aerial Radiance/T 32^3 RGBA16F | generations %llu/%llu",
            static_cast<unsigned long long>(atmosphereLutGenerations[4]),
            static_cast<unsigned long long>(atmosphereLutGenerations[5]));
        ImGui::TextDisabled(
            "3D LUT slice is shown fullscreen by the Atmosphere Debug View.");
    }

    if (lightingPanel && ImGui::CollapsingHeader(
            "Tone Mapping", ImGuiTreeNodeFlags_DefaultOpen))
    {
        bool acesEnabled = toneMappingParameters.mode ==
                           ToneMappingMode::AcesFitted;
        if (ImGui::Checkbox("ACES", &acesEnabled))
            toneMappingParameters.mode = acesEnabled
                ? ToneMappingMode::AcesFitted : ToneMappingMode::LinearDebug;
        ImGui::SliderFloat("Exposure EV", &toneMappingParameters.exposureEv,
                           -8.0f, 8.0f, "%+.2f EV");
        ImGui::SliderFloat("White Balance",
            &toneMappingParameters.whiteBalanceKelvin,
            3500.0f, 10000.0f, "%.0f K");

        ImGui::SeparatorText("Temporal Status (read-only)");
        ImGui::Text("History: %s | accumulated %u",
                    temporalHistoryValid ? "valid" : "invalid",
                    temporalAccumulatedFrames);
        ImGui::Text("Phase %u | last reset reason %u",
                    temporalParameters.frameIndex & 3u,
                    temporalParameters.resetReason);
        ImGui::TextDisabled(
            "Exposure/White Balance do not reset history. Controls remain in F1 > Temporal.");
    }

    if (lightingPanel && ImGui::CollapsingHeader(
            "Lighting & Atmosphere", ImGuiTreeNodeFlags_DefaultOpen))
    {
        int atmosphereMode = static_cast<int>(atmosphereParameters.mode);
        const char* atmosphereModes[] = { "Physical", "Manual Reference" };
        if (ImGui::Combo("Atmosphere Mode", &atmosphereMode,
                         atmosphereModes, 2))
            atmosphereParameters.mode = static_cast<AtmosphereMode>(
                atmosphereMode);
        ImGui::TextWrapped("%s", atmosphereStatus.c_str());

        ImGui::SeparatorText("Sun Direction & Time");
        const bool timePlaying = atmosphereParameters.timePlaybackEnabled;
        if (ImGui::TreeNodeEx("Angle & Directional Light",
                              ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::BeginDisabled(timePlaying);
            bool angleChanged = false;
            if (ImGui::Button("Morning 18 deg"))
            {
                atmosphereParameters.sunAzimuthDegrees = -60.0f;
                atmosphereParameters.sunElevationDegrees = 18.0f;
                sunPreset = Stage6SunPreset::LowEast;
                angleChanged = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Noon 70 deg"))
            {
                atmosphereParameters.sunAzimuthDegrees = 30.0f;
                atmosphereParameters.sunElevationDegrees = 70.0f;
                sunPreset = Stage6SunPreset::Custom;
                angleChanged = true;
            }
            if (ImGui::Button("Sunset 3 deg"))
            {
                atmosphereParameters.sunAzimuthDegrees = 105.0f;
                atmosphereParameters.sunElevationDegrees = 3.0f;
                sunPreset = Stage6SunPreset::Custom;
                angleChanged = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Twilight -4 deg"))
            {
                atmosphereParameters.sunAzimuthDegrees = 115.0f;
                atmosphereParameters.sunElevationDegrees = -4.0f;
                sunPreset = Stage6SunPreset::Custom;
                angleChanged = true;
            }
            angleChanged |= ImGui::SliderFloat("Sun Azimuth",
                &atmosphereParameters.sunAzimuthDegrees,
                -180.0f, 180.0f, "%.1f deg");
            angleChanged |= ImGui::SliderFloat("Sun Elevation",
                &atmosphereParameters.sunElevationDegrees,
                -6.0f, 90.0f, "%.1f deg");
            ImGui::EndDisabled();
            if (angleChanged)
            {
                atmosphereParameters.timePlaybackEnabled = false;
                atmosphereParameters.sunControlMode = SunControlMode::Angles;
                if (sunPreset != Stage6SunPreset::LowEast)
                    sunPreset = Stage6SunPreset::Custom;
            }
            if (timePlaying)
                ImGui::TextDisabled(
                    "Angle controls are locked while Time of Day is playing.");
            const stage14math::Float3 sun = stage14math::DirectionFromAngles(
                atmosphereParameters.sunAzimuthDegrees,
                atmosphereParameters.sunElevationDegrees);
            DrawSunDirectionDiagram(camera, { sun.x, sun.y, sun.z });
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Time of Day",
                              ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::SliderFloat("Time of Day",
                &atmosphereParameters.timeOfDayHours,
                5.5f, 19.5f, "%.2f h");
            if (!atmosphereParameters.timePlaybackEnabled)
            {
                if (ImGui::Button("Play Time"))
                {
                    atmosphereParameters.timePlaybackEnabled = true;
                    atmosphereParameters.sunControlMode =
                        SunControlMode::TimeOfDay;
                    const stage14math::SunAngles path =
                        stage14math::TimeOfDayPath(
                            atmosphereParameters.timeOfDayHours);
                    atmosphereParameters.sunAzimuthDegrees =
                        path.azimuthDegrees;
                    atmosphereParameters.sunElevationDegrees =
                        path.elevationDegrees;
                }
            }
            else if (ImGui::Button("Pause Time"))
            {
                // Renderer가 이번 프레임에 계산한 시간 방향을 Angle 원본에
                // 남긴 채 제어권만 넘겨 정지 순간 화면 점프를 막는다.
                const stage14math::SunAngles path = stage14math::TimeOfDayPath(
                    atmosphereParameters.timeOfDayHours);
                atmosphereParameters.sunAzimuthDegrees = path.azimuthDegrees;
                atmosphereParameters.sunElevationDegrees = path.elevationDegrees;
                atmosphereParameters.timePlaybackEnabled = false;
                atmosphereParameters.sunControlMode = SunControlMode::Angles;
                sunPreset = Stage6SunPreset::Custom;
            }
            ImGui::SliderFloat("Playback Speed",
                &atmosphereParameters.timePlaybackMinutesPerSecond,
                30.0f, 120.0f, "%.1f simulated min/s");
            const stage14math::SunAngles path = stage14math::TimeOfDayPath(
                atmosphereParameters.timeOfDayHours);
            ImGui::Text("Path result: azimuth %.1f, elevation %.1f",
                        path.azimuthDegrees, path.elevationDegrees);
            ImGui::TextDisabled(
                "Playback always loops from 19:30 back to 05:30.");
            ImGui::TextDisabled(
                "While paused, the time slider selects the next play start only.");
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Sun Appearance",
                              ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::ColorEdit3("Sun Tint", &lightParameters.sunColor.x,
                              ImGuiColorEditFlags_Float);
            ImGui::SliderFloat("Sun Multiplier", &lightParameters.sunIntensity,
                               0.0f, 5.0f, "%.2f");
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Direct Light Sampling",
                              ImGuiTreeNodeFlags_DefaultOpen))
        {
            bool lightChanged = false;
            lightChanged |= ImGui::SliderFloat(
                "Single Scattering Albedo (omega)",
                &lightParameters.singleScatteringAlbedo,
                0.0f, 1.0f, "%.2f");
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset##Scatter"))
            {
                lightParameters.singleScatteringAlbedo = 1.0f;
                lightChanged = true;
            }
            ImGui::TextDisabled(
                "Extinguished light converted to scattering (sigma_s / sigma_t).");
            ImGui::BeginDisabled(stage15QualityControlsLocked);
            int lightSteps = static_cast<int>(lightParameters.maxLightSteps);
            if (ImGui::SliderInt("Max Light Steps", &lightSteps, 1, 512))
            {
                lightParameters.maxLightSteps =
                    static_cast<std::uint32_t>(lightSteps);
                lightChanged = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset##LightSteps"))
            {
                lightParameters.maxLightSteps = 80u;
                lightChanged = true;
            }
            lightChanged |= ImGui::SliderFloat(
                "Light Step Size", &lightParameters.lightStepSize,
                0.01f, 1000.0f, "%.3f m", ImGuiSliderFlags_Logarithmic);
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset##LightStepSize"))
            {
                lightParameters.lightStepSize = 250.0f;
                lightChanged = true;
            }
            ImGui::EndDisabled();
            if (stage15QualityControlsLocked)
                ImGui::TextDisabled(
                    "Light step budget is locked until Restore Realtime.");
            lightChanged |= ImGui::SliderFloat(
                "Light Ray Bias", &lightParameters.lightRayBias,
                0.001f, 100.0f, "%.3f m", ImGuiSliderFlags_Logarithmic);
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset##LightBias"))
            {
                lightParameters.lightRayBias = 1.0f;
                lightChanged = true;
            }
            if (lightChanged)
                sunPreset = Stage6SunPreset::Custom;
            ImGui::TextDisabled(
                "Light Ray samples Base Density only; Detail is omitted.");
            ImGui::TreePop();
        }
        lightParameters = stage6light::Sanitize(lightParameters);
        const stage14math::Float3 canonicalSun =
            stage14math::DirectionFromAngles(
                atmosphereParameters.sunAzimuthDegrees,
                atmosphereParameters.sunElevationDegrees);
        lightParameters.directionToSun = {
            canonicalSun.x, canonicalSun.y, canonicalSun.z
        };

        ImGui::SeparatorText("Atmosphere Model");
        int atmospherePreset = static_cast<int>(atmosphereParameters.preset);
        const char* atmospherePresets[] = {
            "Earth Clear", "Earth Hazy", "Custom"
        };
        if (ImGui::Combo("Atmosphere Preset", &atmospherePreset,
                         atmospherePresets, 3))
            stage14atmosphere::ApplyPreset(
                atmosphereParameters,
                static_cast<AtmospherePreset>(atmospherePreset));
        if (ImGui::SliderFloat("Turbidity", &atmosphereParameters.turbidity,
                               0.25f, 4.0f, "%.2f"))
            atmosphereParameters.preset = AtmospherePreset::Custom;
        if (ImGui::TreeNode("Atmosphere Advanced"))
        {
            bool advancedChanged = false;
            advancedChanged |= ImGui::SliderFloat("Rayleigh Scale",
                &atmosphereParameters.rayleighScale, 0.25f, 4.0f, "%.2f");
            advancedChanged |= ImGui::SliderFloat("Mie Absorption",
                &atmosphereParameters.mieAbsorptionScale, 0.0f, 4.0f, "%.2f");
            advancedChanged |= ImGui::SliderFloat("Mie g",
                &atmosphereParameters.mieG, 0.0f, 0.95f, "%.3f");
            advancedChanged |= ImGui::SliderFloat("Ozone Scale",
                &atmosphereParameters.ozoneScale, 0.0f, 4.0f, "%.2f");
            advancedChanged |= ImGui::SliderFloat("Rayleigh Height",
                &atmosphereParameters.rayleighScaleHeightKm,
                4.0f, 16.0f, "%.1f km");
            advancedChanged |= ImGui::SliderFloat("Mie Height",
                &atmosphereParameters.mieScaleHeightKm,
                0.5f, 4.0f, "%.2f km");
            if (advancedChanged)
                atmosphereParameters.preset = AtmospherePreset::Custom;
            ImGui::TreePop();
        }

        ImGui::SeparatorText(
            "Environment, Ground & Cloud Multiple Scattering");
        DrawEnvironmentGroundControls(
            atmosphereParameters.mode, groundLightingParameters,
            environmentParameters, environmentPreset);

    }

    if (lightingPanel && ImGui::CollapsingHeader(
            "Stage 12 Cloud Shadow / Deep Cache",
            ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::BeginDisabled(stage15QualityControlsLocked);
        int mode = static_cast<int>(shadowParameters.shadowMode);
        const char* modes[] = { "Direct Reference", "Deep Cache" };
        if (ImGui::Combo("Shadow Mode", &mode, modes,
                         static_cast<int>(std::size(modes))))
            shadowParameters.shadowMode = static_cast<std::uint32_t>(mode);

        int preset = static_cast<int>(shadowParameters.shadowPreset);
        const char* presets[] = { "Fast 256", "Balanced 512" };
        if (ImGui::Combo("Cache Preset", &preset, presets,
                         static_cast<int>(std::size(presets))))
            shadowParameters.shadowPreset = static_cast<std::uint32_t>(preset);
        ImGui::EndDisabled();
        if (stage15QualityControlsLocked)
            ImGui::TextDisabled(
                "Shadow mode and cache preset are locked until Restore Realtime.");

        bool surfaceEnabled = shadowParameters.surfaceShadowEnabled != 0u;
        if (ImGui::Checkbox("Surface Cloud Shadow", &surfaceEnabled))
            shadowParameters.surfaceShadowEnabled = surfaceEnabled ? 1u : 0u;
        ImGui::SliderFloat("Surface Strength",
                           &shadowParameters.surfaceShadowStrength,
                           0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Surface Ambient Floor",
                           &shadowParameters.surfaceAmbientFloor,
                           0.0f, 1.0f, "%.2f");
        int nearSlice = static_cast<int>(shadowParameters.debugNearSlice);
        if (ImGui::SliderInt("Near Cache Debug Slice", &nearSlice, 0,
                             static_cast<int>(shadowParameters.nearSliceCount - 1u)))
            shadowParameters.debugNearSlice = static_cast<std::uint32_t>(nearSlice);
        int farSlice = static_cast<int>(shadowParameters.debugFarSlice);
        if (ImGui::SliderInt("Far Cache Debug Slice", &farSlice, 0,
                             static_cast<int>(shadowParameters.farSliceCount - 1u)))
            shadowParameters.debugFarSlice = static_cast<std::uint32_t>(farSlice);
        ImGui::SliderFloat("Cache Debug Exposure",
                           &shadowParameters.cacheDebugExposure,
                           0.1f, 20.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
        shadowParameters = stage12shadow::Sanitize(shadowParameters);
        const float layerHeight = shadowParameters.cloudTopMeters -
            shadowParameters.cloudBottomMeters;
        const float nearDebugHeight = shadowParameters.cloudBottomMeters +
            layerHeight * static_cast<float>(shadowParameters.debugNearSlice) /
            static_cast<float>(std::max(shadowParameters.nearSliceCount - 1u, 1u));
        const float farDebugHeight = shadowParameters.cloudBottomMeters +
            layerHeight * static_cast<float>(shadowParameters.debugFarSlice) /
            static_cast<float>(std::max(shadowParameters.farSliceCount - 1u, 1u));
        ImGui::Text("Debug slice height: Near %.0f m | Far %.0f m",
                    nearDebugHeight, farDebugHeight);
        ImGui::Text("Near: %ux%ux%u, %.3f m/texel",
                    shadowParameters.nearResolution,
                    shadowParameters.nearResolution,
                    shadowParameters.nearSliceCount,
                    shadowParameters.nearWidthMeters /
                        static_cast<float>(shadowParameters.nearResolution));
        ImGui::Text("Far: %ux%ux%u, %.3f m/texel",
                    shadowParameters.farResolution,
                    shadowParameters.farResolution,
                    shadowParameters.farSliceCount,
                    shadowParameters.farWidthMeters /
                        static_cast<float>(shadowParameters.farResolution));
        ImGui::Text("Cache memory: %.1f MiB | Ready: %s",
                    static_cast<double>(stage12shadow::CacheBytes(
                        static_cast<Stage12ShadowPreset>(
                            shadowParameters.shadowPreset))) /
                        (1024.0 * 1024.0),
                    shadowParameters.cacheReady != 0u ? "yes" : "fallback");
        ImGui::TextDisabled(
            "World cache is independent of Full/50%% and window resize.");
        ImGui::TextDisabled(
            "Cache Texture: black=empty, white=optical depth; slice 0=surface shadow.");
        ImGui::TextDisabled(
            "Cascade: red=Near, blue=Far, magenta=blend, black=outside/unavailable.");
    }

    if (lightingPanel && ImGui::CollapsingHeader(
            "Lighting / Phase / Environment Debug View",
            ImGuiTreeNodeFlags_DefaultOpen))
    {
        struct DebugViewOption { const char* name; CloudDebugMode mode; };
        static constexpr DebugViewOption options[] = {
            { "Composite", CloudDebugMode::Composite },
            { "View Transmittance", CloudDebugMode::Transmittance },
            { "View Optical Depth", CloudDebugMode::ViewOpticalDepth },
            { "Light Transmittance", CloudDebugMode::LightTransmittance },
            { "Light Optical Depth", CloudDebugMode::LightOpticalDepth },
            { "Total Light Samples", CloudDebugMode::TotalLightSamples },
            { "Direct Single Scattering", CloudDebugMode::DirectSingleScattering },
            { "Phase CosTheta", CloudDebugMode::PhaseCosTheta },
            { "Forward Phase", CloudDebugMode::ForwardPhaseLobe },
            { "Backward Phase", CloudDebugMode::BackwardPhaseLobe },
            { "Dual-lobe Phase", CloudDebugMode::DualPhaseFactor },
            { "Accumulated Direct", CloudDebugMode::AccumulatedDirectLighting },
            { "Sky Ambient", CloudDebugMode::AccumulatedSkyAmbient },
            { "Ground Bounce", CloudDebugMode::AccumulatedGroundBounce },
            { "Multiple Scattering", CloudDebugMode::AccumulatedMultipleScattering },
            { "Detail LOD Factor", CloudDebugMode::DetailLodFactor },
            { "Silver Lining Contribution", CloudDebugMode::SilverLiningContribution },
            { "Shaped Sun Visibility", CloudDebugMode::ShapedSunVisibility },
            { "Ambient Visibility", CloudDebugMode::AmbientVisibility },
            { "Stage 12 Near Cache Texture", CloudDebugMode::Stage12NearOpticalDepth },
            { "Stage 12 Far Cache Texture", CloudDebugMode::Stage12FarOpticalDepth },
            { "Stage 12 Cascade World Lookup", CloudDebugMode::Stage12CascadeSelection },
            { "Stage 12 Surface T", CloudDebugMode::Stage12SurfaceTransmittance },
            { "Stage 12 Direct/Cache Error x10", CloudDebugMode::Stage12DirectCacheError },
        };
        int selected = 0;
        const auto current = static_cast<CloudDebugMode>(cloudParameters.debugMode);
        for (int index = 0; index < static_cast<int>(std::size(options)); ++index)
            if (options[index].mode == current)
                selected = index;
        if (ImGui::BeginCombo("Main View Debug", options[selected].name))
        {
            for (int index = 0; index < static_cast<int>(std::size(options)); ++index)
            {
                const bool isSelected = index == selected;
                if (ImGui::Selectable(options[index].name, isSelected))
                    cloudParameters.debugMode = static_cast<std::int32_t>(
                        options[index].mode);
                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    if (noisePanel && ImGui::CollapsingHeader(
            "Performance", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Checkbox("VSync", &vsyncEnabled);
        ImGui::TextDisabled("CPU Frame includes Present/VSync wait.");
        ImGui::TextDisabled("GPU Frame excludes Present; compare ray cost with GPU Cloud.");
    }

    if (lightingPanel && ImGui::CollapsingHeader(
            "Stage 13-5 km Optics & Detail LOD",
            ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::BeginDisabled(stage15QualityControlsLocked);
        ImGui::TextDisabled(
            "Unified 50km scene: reference 62.5m, previous 125m, default 250m.");
        if (ImGui::Button("Reference 62.5m / 320"))
        {
            lightParameters.lightStepSize = 62.5f;
            lightParameters.maxLightSteps = 320u;
        }
        ImGui::SameLine();
        if (ImGui::Button("Previous Quality 125m / 160"))
        {
            lightParameters.lightStepSize = 125.0f;
            lightParameters.maxLightSteps = 160u;
        }
        if (ImGui::Button("Default 250m / 80"))
        {
            lightParameters.lightStepSize = 250.0f;
            lightParameters.maxLightSteps = 80u;
        }

        bool lodEnabled = cloudLodParameters.detailLodEnabled != 0u;
        if (ImGui::Checkbox("Enable Detail Distance LOD", &lodEnabled))
            cloudLodParameters.detailLodEnabled = lodEnabled ? 1u : 0u;
        ImGui::SliderFloat("Detail LOD Start",
                           &cloudLodParameters.detailLodStartMeters,
                           0.0f, 50000.0f, "%.0f m");
        ImGui::SliderFloat("Detail LOD End",
                           &cloudLodParameters.detailLodEndMeters,
                           1000.0f, 50000.0f, "%.0f m");
        if (ImGui::Button("Approved 32–48km"))
        {
            cloudLodParameters.detailLodEnabled = 1u;
            cloudLodParameters.detailLodStartMeters = 32000.0f;
            cloudLodParameters.detailLodEndMeters = 48000.0f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Aggressive 24–40km"))
        {
            cloudLodParameters.detailLodEnabled = 1u;
            cloudLodParameters.detailLodStartMeters = 24000.0f;
            cloudLodParameters.detailLodEndMeters = 40000.0f;
        }
        ImGui::Text("Measured Detail neutral mean: %.6f",
                    cloudLodParameters.detailNeutralValue);
        cloudLodParameters = stage13lod::Sanitize(cloudLodParameters);
        ImGui::EndDisabled();
        if (stage15QualityControlsLocked)
            ImGui::TextDisabled(
                "Light sampling and Detail LOD are locked until Restore Realtime.");

        ImGui::SeparatorText("Lighting validation setup");
        if (ImGui::Button("Shadow Isolation (Phase + Environment Off)"))
        {
            stage6light::ApplyPhasePreset(
                lightParameters, Stage7PhasePreset::Off);
            stage8environment::ApplyPreset(
                environmentParameters, Stage8EnvironmentPreset::Off);
            lightParameters.sunIntensity = 1.0f;
            phasePreset = Stage7PhasePreset::Off;
            environmentPreset = Stage8EnvironmentPreset::Off;
        }
        if (ImGui::Button("Restore Balanced Lighting"))
        {
            stage6light::ApplyPhasePreset(
                lightParameters, Stage7PhasePreset::Balanced);
            stage8environment::ApplyPreset(
                environmentParameters, Stage8EnvironmentPreset::Balanced);
            lightParameters.sunIntensity = 1.0f;
            phasePreset = Stage7PhasePreset::Balanced;
            environmentPreset = Stage8EnvironmentPreset::Balanced;
        }
        if (ImGui::Button("Portfolio Hero"))
        {
            atmosphereParameters.sunAzimuthDegrees = -60.0f;
            atmosphereParameters.sunElevationDegrees = 18.0f;
            atmosphereParameters.timePlaybackEnabled = false;
            atmosphereParameters.sunControlMode = SunControlMode::Angles;
            lightParameters.sunColor = { 1.0f, 0.78f, 0.62f };
            lightParameters.sunIntensity = 1.15f;
            stage6light::ApplyPhasePreset(
                lightParameters, Stage7PhasePreset::SilverLining);
            stage8environment::ApplyPreset(
                environmentParameters, Stage8EnvironmentPreset::PortfolioHero);
            sunPreset = Stage6SunPreset::LowEast;
            phasePreset = Stage7PhasePreset::SilverLining;
            environmentPreset = Stage8EnvironmentPreset::PortfolioHero;
        }
        ImGui::TextDisabled(
            "Shadow Isolation shows direct self-shadow only; Balanced adds softer fill.");

    }

    if (lightingPanel && ImGui::CollapsingHeader(
            "Phase Function", ImGuiTreeNodeFlags_DefaultOpen))
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
        phaseEditedManually |= ImGui::SliderFloat(
            "Edge Influence", &lightParameters.edgeInfluence,
            0.0f, 1.0f, "%.3f");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##EdgeInfluence"))
        {
            lightParameters.edgeInfluence = 0.0f;
            phaseEditedManually = true;
        }
        phaseEditedManually |= ImGui::SliderFloat(
            "Edge Optical Depth", &lightParameters.edgeOpticalDepthScale,
            0.25f, 8.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##EdgeOpticalDepth"))
        {
            lightParameters.edgeOpticalDepthScale = 1.0f;
            phaseEditedManually = true;
        }
        phaseEditedManually |= ImGui::SliderFloat(
            "Shadow Contrast", &lightParameters.shadowExponent,
            0.5f, 4.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##ShadowContrast"))
        {
            lightParameters.shadowExponent = 1.0f;
            phaseEditedManually = true;
        }
        ImGui::TextDisabled(
            "Edge Influence confines Phase to sun-exposed volume; Shadow Contrast reshapes Tsun.");
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
            appliedCurve[index] = std::clamp(
                appliedCurve[index], 0.0f, stage7::kMaxAppliedPhaseFactor);
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
                         "1 = isotropic / 2.5 = LDR safety cap", 0.0f,
                         stage7::kMaxAppliedPhaseFactor,
                         ImVec2(0.0f, 70.0f));
        ImGui::TextDisabled(
            "cosTheta +1: camera looks toward sun / -1: opposite direction");
    }

    if (cameraPanel && ImGui::CollapsingHeader(
            "Camera Transform", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const DirectX::XMFLOAT3 position = camera.GetPosition();
        const DirectX::XMFLOAT3 forward = camera.GetForward();
        const DirectX::XMFLOAT3 target = camera.GetTarget();
        ImGui::Text("Position:  X %.3f  Y %.3f  Z %.3f m",
                    position.x, position.y, position.z);
        ImGui::Text("Forward:   X %.3f  Y %.3f  Z %.3f",
                    forward.x, forward.y, forward.z);
        ImGui::Text("Yaw/Pitch: %.2f / %.2f deg",
                    camera.GetYawDegrees(), camera.GetPitchDegrees());
        ImGui::Text("Reference Target: X %.3f  Y %.3f  Z %.3f m",
                    target.x, target.y, target.z);
        ImGui::Text("Reference Distance: %.3f m", camera.GetDistance());
        ImGui::Text("Clip: %.3f m - %.1f m",
                    camera.GetNearPlane(), camera.GetFarPlane());
        float fovYDegrees = camera.GetFovYDegrees();
        if (ImGui::SliderFloat("Vertical FOV", &fovYDegrees,
                               20.0f, 120.0f, "%.1f deg"))
        {
            camera.SetFovYDegrees(fovYDegrees);
            camera.MarkManuallyAdjusted();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##CameraFov"))
        {
            camera.SetFovYDegrees(60.0f);
            camera.MarkManuallyAdjusted();
        }
        cameraMoveSpeedMetersPerSecond =
            stage13scene::SanitizeMoveSpeed(cameraMoveSpeedMetersPerSecond);
        if (ImGui::SliderFloat(
                "Move Speed", &cameraMoveSpeedMetersPerSecond,
                stage13scene::kMinimumMoveSpeedMetersPerSecond,
                stage13scene::kMaximumMoveSpeedMetersPerSecond,
                "%.1f m/s", ImGuiSliderFlags_Logarithmic))
        {
            cameraMoveSpeedMetersPerSecond =
                stage13scene::SanitizeMoveSpeed(
                    cameraMoveSpeedMetersPerSecond);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##CameraMoveSpeed"))
            cameraMoveSpeedMetersPerSecond = stage13scene::kMoveSpeedMetersPerSecond;
        ImGui::TextDisabled(
            "LMB drag: free look | Wheel: forward/back | WASD: free fly | Shift: 4x.");
    }

    if (cameraPanel && ImGui::CollapsingHeader(
            "View Presets", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const auto applyPreset = [&](const Stage13CameraPreset& preset,
                                     const wchar_t* debugName)
        {
            CameraSnapshot snapshot;
            snapshot.position = preset.position;
            snapshot.target = preset.target;
            snapshot.fovYDegrees = 60.0f;
            snapshot.nearPlaneMeters = stage13camera::kNearPlaneMeters;
            snapshot.farPlaneMeters = stage13camera::kFarPlaneMeters;
            snapshot.valid = true;
            ApplyCameraSnapshot(snapshot, camera, debugName);
        };
        if (ImGui::Button("F5 Hero / Building Depth"))
            applyPreset(stage13camera::Get(Stage13CameraPresetId::HeroDepth),
                        L"Hero / Building Depth(F5/UI)");
        ImGui::SameLine();
        if (ImGui::Button("F6 Ground Horizon"))
            applyPreset(stage13camera::Get(Stage13CameraPresetId::GroundHorizon),
                        L"Ground Horizon(F6/UI)");
        if (ImGui::Button("F7 Inside Cloud"))
            applyPreset(stage13camera::Get(Stage13CameraPresetId::InsideLayer),
                        L"Inside Cloud(F7/UI)");
        ImGui::SameLine();
        if (ImGui::Button("F8 Above / Down"))
            applyPreset(stage13camera::Get(Stage13CameraPresetId::AboveLayer),
                        L"Above / Down(F8/UI)");
        ImGui::TextDisabled("The four buttons exactly match the global F5-F8 presets.");
    }

    if (cameraPanel && ImGui::CollapsingHeader(
        "Saved Position", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::Button("Save Current Position"))
            m_savedCamera = CaptureCamera(camera);
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_savedCamera.valid);
        if (ImGui::Button("Restore Saved Position"))
            ApplyCameraSnapshot(m_savedCamera, camera, L"저장 카메라(F4)");
        ImGui::EndDisabled();
        if (m_savedCamera.valid)
        {
            ImGui::Text("Saved: (%.3f, %.3f, %.3f) m | FOV %.1f deg",
                        m_savedCamera.position.x, m_savedCamera.position.y,
                        m_savedCamera.position.z,
                        m_savedCamera.fovYDegrees);
        }
        else
        {
            ImGui::TextDisabled("No in-memory camera position is saved yet.");
        }
        ImGui::TextWrapped(
            "Export writes both the current camera and this saved position to noise-settings.json.");
        if (ImGui::Button("Export 4 PNG + JSON##Camera"))
            m_exportRequested = true;
        if (!m_exportStatus.empty())
            ImGui::TextWrapped("%s", m_exportStatus.c_str());
    }

    if (noisePanel && ImGui::CollapsingHeader(
            "Animation", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Checkbox("Pause Time", &m_timePaused);
        ImGui::SliderFloat("Time Scale", &m_timeScale, 0.0f, 4.0f);
        ImGui::Text("Effective Time: %.3f s", m_effectiveTime);
        if (bulkAdvectedShape)
        {
            const stage13shape::HorizontalOffset offset =
                stage13shape::EvaluatePhysicalCloudAdvectionOffset(
                    cloudParameters.windDirection.x,
                    cloudParameters.windDirection.z,
                    cloudParameters.windSpeed, m_effectiveTime);
            const double distanceMeters = std::hypot(offset.x, offset.z);
            const double weatherTexelMeters = std::max(
                static_cast<double>(cloudParameters.weatherMapWorldSize) / 256.0,
                1e-6);
            const double directionLength = std::hypot(
                static_cast<double>(cloudParameters.windDirection.x),
                static_cast<double>(cloudParameters.windDirection.z));
            const double activeSpeed = !m_timePaused && directionLength > 1e-6
                ? std::max(static_cast<double>(cloudParameters.windSpeed), 0.0) *
                    std::max(static_cast<double>(m_timeScale), 0.0)
                : 0.0;
            ImGui::Text("Bulk travel: %.1f m", distanceMeters);
            ImGui::Text("Weather motion: %.3f texel/s (%.1f m/s effective)",
                        activeSpeed / weatherTexelMeters, activeSpeed);
        }
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
            cloudParameters.baseNoiseScale = 0.35f;
            cloudParameters.coverage = 0.55f;
            cloudParameters.densityMultiplier = 1.0f;
            cloudParameters.windDirection = { 0.9701425f, 0.0f, 0.2425356f };
            cloudParameters.windSpeed = 0.25f;
            cloudParameters.noiseOffset = 0.0f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Export 4 PNG + JSON"))
            m_exportRequested = true;
        if (!m_exportStatus.empty())
            ImGui::TextWrapped("%s", m_exportStatus.c_str());
    }

    if (captureControlsLocked)
        ImGui::EndDisabled();

    optimizationParameters = stage9optimization::Sanitize(
        optimizationParameters);
    upsamplingParameters = stage10upsampling::Sanitize(
        upsamplingParameters);
    temporalParameters = stage11temporal::Sanitize(temporalParameters);
    atmosphereParameters = stage14atmosphere::Sanitize(atmosphereParameters);
    groundLightingParameters = stage14ground::Sanitize(
        groundLightingParameters);
    toneMappingParameters = stage14tone::Sanitize(toneMappingParameters);
    m_atmosphereSnapshot = atmosphereParameters;
    m_groundLightingSnapshot = groundLightingParameters;
    m_toneMappingSnapshot = toneMappingParameters;
    const CloudAppearanceSettings appearanceAfter = CaptureCloudAppearance(
        cloudParameters, cloudShapeParameters, m_weatherGeneratorDraft);
    if (!CloudAppearanceSettingsEqual(appearanceBefore, appearanceAfter))
        m_cloudAppearanceEdited = true;

    Stage12ShadowParameters historyShadowBefore = shadowBefore;
    Stage12ShadowParameters historyShadowAfter = shadowParameters;
    // Cache preview 조작은 실제 렌더 계약을 바꾸지 않으므로 Temporal history를
    // 지우지 않는다. mode/preset/surface 등 나머지 필드는 계속 reset 대상이다.
    historyShadowBefore.cacheDebugExposure = 0.0f;
    historyShadowBefore.debugNearSlice = 0u;
    historyShadowBefore.debugFarSlice = 0u;
    historyShadowAfter.cacheDebugExposure = 0.0f;
    historyShadowAfter.debugNearSlice = 0u;
    historyShadowAfter.debugFarSlice = 0u;
    LightParameters uiLightBefore = lightBefore;
    LightParameters uiLightAfter = lightParameters;
    // directionToSun은 Atmosphere 각도에서 매 UI 프레임 파생된다. 실제
    // Sun/Time 입력은 Atmosphere 비교가 잡으므로 여기서는 파생 차이를 제외한다.
    uiLightBefore.directionToSun = uiLightAfter.directionToSun;
    uiLightBefore.lightPadding = 0.0f;
    uiLightAfter.lightPadding = 0.0f;
    m_parametersChanged = m_parametersChanged ||
        std::memcmp(&before, &cloudParameters, sizeof(CloudParameters)) != 0 ||
        std::memcmp(&shapeBefore, &cloudShapeParameters,
                    sizeof(CloudShapeParameters)) != 0 ||
        std::memcmp(&domainBefore, &cloudDomainParameters,
                    sizeof(CloudDomainParameters)) != 0 ||
        std::memcmp(&lodBefore, &cloudLodParameters,
                    sizeof(CloudLodParameters)) != 0 ||
        std::memcmp(&noiseVolumeBefore, &noiseVolumeParameters,
                    sizeof(NoiseVolumeParameters)) != 0 ||
        std::memcmp(&optimizationBefore, &optimizationParameters,
                    sizeof(OptimizationParameters)) != 0 ||
        std::memcmp(&upsamplingBefore, &upsamplingParameters,
                    sizeof(Stage10UpsamplingParameters)) != 0 ||
        std::memcmp(&temporalBefore, &temporalParameters,
                    sizeof(Stage11TemporalParameters)) != 0 ||
        std::memcmp(&historyShadowBefore, &historyShadowAfter,
                    sizeof(Stage12ShadowParameters)) != 0 ||
        std::memcmp(&uiLightBefore, &uiLightAfter,
                    sizeof(LightParameters)) != 0 ||
        std::memcmp(&environmentBefore, &environmentParameters,
                    sizeof(EnvironmentParameters)) != 0 ||
        groundBefore.preset != groundLightingParameters.preset ||
        groundBefore.albedo.x != groundLightingParameters.albedo.x ||
        groundBefore.albedo.y != groundLightingParameters.albedo.y ||
        groundBefore.albedo.z != groundLightingParameters.albedo.z ||
        groundBefore.bounceMultiplier !=
            groundLightingParameters.bounceMultiplier ||
        !stage15::ConceptAtmosphereEqual(
            atmosphereBefore, atmosphereParameters);
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
                              ID3D11Buffer* noiseVolumeCb,
                              ID3D11Buffer* cloudShapeCb,
                              ID3D11ShaderResourceView* weatherMapSrv,
                              ID3D11ShaderResourceView* baseNoiseVolumeSrv,
                              ID3D11ShaderResourceView* detailNoiseVolumeSrv,
                              ID3D11SamplerState* weatherSampler)
{
    if (!m_initialized || !m_panelVisible[0] || !fullscreenVs || !noiseLabPs)
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
    m_context->PSSetConstantBuffers(6, 1, &noiseVolumeCb);
    m_context->PSSetConstantBuffers(7, 1, &cloudShapeCb);
    ID3D11ShaderResourceView* resources[3] = {
        weatherMapSrv, baseNoiseVolumeSrv, detailNoiseVolumeSrv
    };
    m_context->PSSetShaderResources(2, 3, resources);
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
    ID3D11ShaderResourceView* nullSrvs[3] = {};
    m_context->PSSetShaderResources(2, 3, nullSrvs);
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
            ImGui::Text("Atmosphere LUT      %7.3f ms",
                        timing.gpuAtmosphereLutMs);
            ImGui::Text("GPU Shadow Cache    %7.3f ms",
                        timing.gpuShadowCacheMs);
            ImGui::Text("Opaque Scene        %7.3f ms",
                        timing.gpuOpaqueSceneMs);
            ImGui::Text("Cloud Raymarch      %7.3f ms",
                        timing.gpuCloudRaymarchMs);
            ImGui::Text("Spatial/Temporal Resolve %7.3f ms",
                        timing.gpuUpsampleCompositeMs);
            ImGui::Text("GPU Cloud Total     %7.3f ms", timing.gpuCloudMs);
            ImGui::Text("Tone Map            %7.3f ms", timing.gpuToneMapMs);
        }
        else
        {
            ImGui::TextUnformatted("GPU        warming up");
        }

        ImGui::Separator();
        ImGui::Text("View   %u @ %.3f m",
                    cloudParameters.maxViewSteps, cloudParameters.stepSize);
        ImGui::Text("Light  %u @ %.3f m",
                    lightParameters.maxLightSteps, lightParameters.lightStepSize);
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

bool NoiseLab::ConsumeStage15QualityRequest(Stage15QualityPreset& preset)
{
    if (m_stage15QualityRequest < 0)
        return false;
    preset = static_cast<Stage15QualityPreset>(m_stage15QualityRequest);
    m_stage15QualityRequest = -1;
    return true;
}

bool NoiseLab::ConsumeStage15ConceptRequest(Stage15ConceptPreset& preset)
{
    if (m_stage15ConceptRequest < 0)
        return false;
    preset = static_cast<Stage15ConceptPreset>(m_stage15ConceptRequest);
    m_stage15ConceptRequest = -1;
    return true;
}

bool NoiseLab::ConsumeStage15DiagnosticRequest(Stage15DiagnosticMode& mode)
{
    if (m_stage15DiagnosticRequest < 0)
        return false;
    mode = static_cast<Stage15DiagnosticMode>(m_stage15DiagnosticRequest);
    m_stage15DiagnosticRequest = -1;
    return true;
}

bool NoiseLab::ConsumeWeatherPresetRequest(Stage5WeatherPreset& preset)
{
    if (m_weatherPresetRequest < 0 || m_weatherPresetRequest > 3)
        return false;
    preset = static_cast<Stage5WeatherPreset>(m_weatherPresetRequest);
    m_weatherPresetRequest = -1;
    return true;
}

bool NoiseLab::ConsumeOpenWorldPipelinePresetRequest(
    OpenWorldPipelinePreset& preset)
{
    if (m_openWorldPipelinePresetRequest < 1 ||
        m_openWorldPipelinePresetRequest > 5)
        return false;
    preset = static_cast<OpenWorldPipelinePreset>(
        m_openWorldPipelinePresetRequest);
    m_openWorldPipelinePresetRequest = -1;
    return true;
}

bool NoiseLab::ConsumeCloudAppearancePresetRequest(
    CloudAppearancePreset& preset)
{
    const int request = m_cloudAppearancePresetRequest;
    m_cloudAppearancePresetRequest = -1;
    return TryDecodeCloudAppearancePresetRequest(request, preset);
}

bool NoiseLab::ConsumeCloudAppearanceSaveRequest()
{
    const bool requested = m_cloudAppearanceSaveRequest;
    m_cloudAppearanceSaveRequest = false;
    return requested;
}

bool NoiseLab::ConsumeCloudAppearanceEdited()
{
    const bool edited = m_cloudAppearanceEdited;
    m_cloudAppearanceEdited = false;
    return edited;
}

bool NoiseLab::ConsumeNoiseVolumeRegenerateRequest()
{
    const bool requested = m_noiseVolumeRegenerateRequest;
    m_noiseVolumeRegenerateRequest = false;
    return requested;
}

bool NoiseLab::ConsumeTemporalResetRequest()
{
    const bool requested = m_temporalResetRequested;
    m_temporalResetRequested = false;
    return requested;
}

bool NoiseLab::ConsumeNoiseSourceRequest(NoiseSource& source)
{
    if (m_noiseSourceRequest < 0)
        return false;
    source = static_cast<NoiseSource>(m_noiseSourceRequest);
    m_noiseSourceRequest = -1;
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

void NoiseLab::SynchronizeWeatherGeneratorSettings(
    const WeatherMapGeneratorSettings& settings)
{
    m_weatherGeneratorDraft = SanitizeWeatherMapGeneratorSettings(settings);
    m_weatherGeneratorRequest = m_weatherGeneratorDraft;
    m_weatherGeneratorDraftInitialized = true;
    m_weatherGeneratorDirty = false;
    m_weatherGeneratorRequestPending = false;
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
    const bool localHeightField =
        outputMode == NoiseOutputMode::WeatherThicknessPotential ||
        outputMode == NoiseOutputMode::LocalThickness ||
        outputMode == NoiseOutputMode::LocalHeightFraction;
    const bool weatherMayEmptySlice = outputMode == NoiseOutputMode::FinalDensity ||
        outputMode == NoiseOutputMode::BaseDensity ||
        outputMode == NoiseOutputMode::DetailNoise ||
        outputMode == NoiseOutputMode::Erosion || sampleMask ||
        outputMode == NoiseOutputMode::WeatherCoverage ||
        outputMode == NoiseOutputMode::WeatherThresholdDensity ||
        outputMode == NoiseOutputMode::WeatherThicknessPotential ||
        outputMode == NoiseOutputMode::LocalThickness ||
        outputMode == NoiseOutputMode::LocalHeightFraction ||
        outputMode == NoiseOutputMode::EffectiveShapeCoverage ||
        outputMode == NoiseOutputMode::BaseSupportBeforeDensity;
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
        else if (maximum <= minimum &&
                 !(weatherMayEmptySlice && maximum == 0) &&
                 !localHeightField)
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
                             const CloudShapeParameters& cloudShape,
                             const CloudDomainParameters& domain,
                             const CloudLodParameters& lod,
                             const OptimizationParameters& optimization,
                             Stage9OptimizationPreset optimizationPreset,
                             const Stage10UpsamplingParameters& upsampling,
                             Stage10ResolutionPreset resolutionPreset,
                             const Stage11TemporalParameters& temporal,
                             const Stage12ShadowParameters& shadow,
                             int cloudRenderWidth,
                             int cloudRenderHeight,
                             const LightParameters& light,
                             Stage6SunPreset sunPreset,
                             Stage7PhasePreset phasePreset,
                             const EnvironmentParameters& environment,
                             Stage8EnvironmentPreset environmentPreset,
                             Stage5WeatherPreset weatherPreset,
                             CloudTypeMode cloudTypeMode,
                             CloudAppearancePreset cloudAppearancePreset,
                             bool cloudAppearanceDirty,
                             bool hasSavedCustomAppearance,
                             const CloudAppearanceSettings& savedCustomAppearance,
                             const NoiseVolumeParameters& noiseVolumeParameters,
                             std::uint64_t baseNoiseVolumeHash,
                             std::uint64_t detailNoiseVolumeHash,
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
        "weatherThresholdDensity", "typedShapeProfile", "weatherUv",
        "baseVolumeR", "baseVolumeG", "baseVolumeB", "baseVolumeA",
        "baseVolumeCombined", "detailVolumeR", "detailVolumeG",
        "detailVolumeB", "detailVolumeA", "detailVolumeCombined",
        "weatherThicknessPotential", "localThickness", "localHeightFraction",
        "effectiveShapeCoverage", "baseSupportBeforeDensity"
    };
    static const char* weatherPresetNames[] = {
        "uniformLegacy", "periodicPerlin", "channelDebug"
    };
    static const char* cloudTypeModeNames[] = {
        "stratus", "mixed", "cumulus", "weatherMap"
    };
    static const char* sunPresetNames[] = {
        "noon", "lowEast", "lowWest", "customSun"
    };
    static const char* phasePresetNames[] = {
        "off", "balanced", "silverLining", "backscatterCheck", "custom"
    };
    static const char* environmentPresetNames[] = {
        "off", "balanced", "strongFill", "groundCheck",
        "portfolioHero", "custom"
    };
    static const char* domainNames[] = { "aabbReference", "planarLayer" };
    const int weatherPresetIndex = std::clamp(static_cast<int>(weatherPreset), 0, 2);
    const int cloudTypeIndex = std::clamp(
        static_cast<int>(cloudTypeMode), 0, 3);
    const int sunPresetIndex = std::clamp(static_cast<int>(sunPreset), 0, 3);
    const int phasePresetIndex = std::clamp(static_cast<int>(phasePreset), 0, 4);
    const int environmentPresetIndex = std::clamp(
        static_cast<int>(environmentPreset), 0, 5);
    const bool portfolioHeroLighting =
        sunPreset == Stage6SunPreset::LowEast &&
        phasePreset == Stage7PhasePreset::SilverLining &&
        environmentPreset == Stage8EnvironmentPreset::PortfolioHero;
    const std::uint32_t domainIndex = std::min(domain.domainType, 1u);
    const std::uint32_t outputIndex = std::min(m_parameters.outputMode, 29u);
    const WeatherMapGeneratorSettings generator =
        SanitizeWeatherMapGeneratorSettings(weatherGeneratorSettings);
    const bool bulkAdvectedShape = cloudshape::UsesPhysicalBulkAdvection(
        cloudShape.shapeMode);
    const stage13shape::HorizontalOffset physicalOffset =
        stage13shape::EvaluatePhysicalCloudAdvectionOffset(
            cloud.windDirection.x, cloud.windDirection.z,
            cloud.windSpeed, m_effectiveTime);
    const double physicalDistanceMeters = std::hypot(
        physicalOffset.x, physicalOffset.z);
    const double configuredBulkSpeed = bulkAdvectedShape
        ? std::max(static_cast<double>(cloud.windSpeed), 0.0) : 0.0;
    const double effectiveBulkSpeed = bulkAdvectedShape && !m_timePaused
        ? configuredBulkSpeed * std::max(static_cast<double>(m_timeScale), 0.0)
        : 0.0;
    output << std::fixed << std::setprecision(6)
           << "{\n"
           << "  \"schemaVersion\": 37,\n"
           << "  \"implementationStage\": \"15\",\n"
           << "  \"developerUiLayout\": \"F1Noise_F2Weather_F3LightingAtmosphere_F4Stage15Camera\",\n"
           << "  \"stage15\": {\"quality\": \""
           << stage15::QualityName(m_stage15QualitySnapshot)
           << "\", \"concept\": \""
           << stage15::ConceptName(m_stage15ConceptSnapshot)
           << "\", \"diagnosticMode\": \""
           << stage15::DiagnosticName(m_stage15DiagnosticSnapshot)
           << "\", \"temporalOverrideActive\": "
           << (m_stage15TemporalOverrideSnapshot ? "true" : "false")
           << "},\n"
           << "  \"legacyRestorePolicy\": \"schema34OrOlder_ManualReference_LegacyShoulder\",\n"
           << "  \"atmosphere\": {\"mode\": \""
           << (m_atmosphereSnapshot.mode == AtmosphereMode::Physical
                   ? "physical" : "manualReference")
           << "\", \"preset\": "
           << static_cast<std::uint32_t>(m_atmosphereSnapshot.preset)
           << ", \"sunControl\": "
           << static_cast<std::uint32_t>(m_atmosphereSnapshot.sunControlMode)
           << ", \"sunAnglesDegrees\": ["
           << m_atmosphereSnapshot.sunAzimuthDegrees << ", "
           << m_atmosphereSnapshot.sunElevationDegrees
           << "], \"timeOfDayHours\": "
           << m_atmosphereSnapshot.timeOfDayHours
           << ", \"timePlaybackMinutesPerSecond\": "
           << m_atmosphereSnapshot.timePlaybackMinutesPerSecond
           << ", \"timePlaybackEnabled\": "
           << (m_atmosphereSnapshot.timePlaybackEnabled ? "true" : "false")
           << ", \"timeLoopEnabled\": "
           << (m_atmosphereSnapshot.timeLoopEnabled ? "true" : "false")
           << ", \"radiiKm\": [" << m_atmosphereSnapshot.bottomRadiusKm
           << ", " << m_atmosphereSnapshot.topRadiusKm
           << "], \"scaleHeightsKm\": ["
           << m_atmosphereSnapshot.rayleighScaleHeightKm << ", "
           << m_atmosphereSnapshot.mieScaleHeightKm
           << "], \"rayleighScale\": " << m_atmosphereSnapshot.rayleighScale
           << ", \"mieAbsorptionScale\": "
           << m_atmosphereSnapshot.mieAbsorptionScale
           << ", \"mieG\": " << m_atmosphereSnapshot.mieG
           << ", \"ozoneScale\": " << m_atmosphereSnapshot.ozoneScale
           << ", \"turbidity\": " << m_atmosphereSnapshot.turbidity
           << "},\n"
           << "  \"groundLighting\": {\"preset\": "
           << static_cast<std::uint32_t>(m_groundLightingSnapshot.preset)
           << ", \"albedoLinear\": [" << m_groundLightingSnapshot.albedo.x
           << ", " << m_groundLightingSnapshot.albedo.y << ", "
           << m_groundLightingSnapshot.albedo.z
           << "], \"bounceMultiplier\": "
           << m_groundLightingSnapshot.bounceMultiplier << "},\n"
           << "  \"toneMapping\": {\"mode\": "
           << static_cast<std::uint32_t>(m_toneMappingSnapshot.mode)
           << ", \"exposureEv\": " << m_toneMappingSnapshot.exposureEv
           << ", \"whiteBalanceKelvin\": "
           << m_toneMappingSnapshot.whiteBalanceKelvin << "},\n"
           << "  \"atmosphereLuts\": {\"format\": \"RGBA16_FLOAT\", "
              "\"sizes\": [[256,64],[32,32],[192,108],[64,16],[32,32,32],[32,32,32]], "
              "\"generations\": ["
           << m_atmosphereLutGenerations[0] << ", "
           << m_atmosphereLutGenerations[1] << ", "
           << m_atmosphereLutGenerations[2] << ", "
           << m_atmosphereLutGenerations[3] << ", "
           << m_atmosphereLutGenerations[4] << ", "
           << m_atmosphereLutGenerations[5] << "], \"hashFnv1a64\": [\""
           << std::hex << m_atmosphereLutHashes[0] << "\", \""
           << m_atmosphereLutHashes[1] << "\", \""
           << m_atmosphereLutHashes[2] << "\", \""
           << m_atmosphereLutHashes[3] << "\", \""
           << m_atmosphereLutHashes[4] << "\", \""
           << m_atmosphereLutHashes[5] << "\"]}" << std::dec << ",\n"
           << "  \"physicalAdvectionMode\": \""
           << (bulkAdvectedShape ? "rigidSharedWindSpeed"
                                 : "legacyIndependentSpeeds") << "\",\n"
           << "  \"effectiveBulkWindSpeedMetersPerSecond\": ";
    if (bulkAdvectedShape)
        output << effectiveBulkSpeed;
    else
        output << "null";
    output << ",\n"
           << "  \"bulkWindSpeedMetersPerEffectiveSecond\": ";
    if (bulkAdvectedShape)
        output << configuredBulkSpeed;
    else
        output << "null";
    output << ",\n"
           << "  \"bulkAdvectionDistanceMeters\": ";
    if (bulkAdvectedShape)
        output << physicalDistanceMeters;
    else
        output << "null";
    output << ",\n"
           << "  \"weatherMapWindSpeedUsage\": \""
           << (bulkAdvectedShape ? "legacyOnlyIgnored" : "absolute") << "\",\n"
           << "  \"detailWindSpeedUsage\": \""
           << (bulkAdvectedShape ? "legacyOnlyIgnored" : "absolute") << "\",\n"
           << "  \"timePaused\": " << (m_timePaused ? "true" : "false") << ",\n"
           << "  \"timeScale\": " << m_timeScale << ",\n"
           << "  \"openWorldPipelinePreset\": \""
           << stage13openworld::PipelinePresetName(
                  m_openWorldPipelinePreset) << "\",\n"
           << "  \"temporal\": {\"mode\": \""
           << stage11temporal::ModeName(
                  temporal.temporalEnabled == 0u
                      ? Stage11TemporalMode::Off
                      : (temporal.jitterEnabled != 0u
                          ? Stage11TemporalMode::Stable4Phase
                          : Stage11TemporalMode::FullResolution))
           << "\", \"historyWeight\": " << temporal.historyWeight
           << ", \"sceneDepthRelativeThreshold\": "
           << temporal.sceneDepthRelativeThreshold
           << ", \"cloudDepthRelativeThreshold\": "
           << temporal.cloudDepthRelativeThreshold
           << ", \"transmittanceThreshold\": "
           << temporal.transmittanceThreshold
           << ", \"nearFadeMeters\": ["
           << temporal.nearHistoryFadeStartMeters << ", "
           << temporal.nearHistoryFadeEndMeters
           << "], \"neighborhoodClamp\": "
           << (temporal.neighborhoodClampingEnabled != 0u ? "true" : "false")
           << "},\n"
           << "  \"stage12Shadow\": {\"mode\": \""
           << stage12shadow::ModeName(static_cast<Stage12ShadowMode>(
                  shadow.shadowMode))
           << "\", \"preset\": \""
           << stage12shadow::PresetName(static_cast<Stage12ShadowPreset>(
                  shadow.shadowPreset))
           << "\", \"near\": {\"size\": [" << shadow.nearResolution
           << ", " << shadow.nearResolution << ", "
           << shadow.nearSliceCount << "], \"widthMeters\": "
           << shadow.nearWidthMeters
           << "}, \"far\": {\"size\": [" << shadow.farResolution
           << ", " << shadow.farResolution << ", "
           << shadow.farSliceCount << "], \"widthMeters\": "
           << shadow.farWidthMeters << "}, \"memoryMiB\": "
           << (static_cast<double>(stage12shadow::CacheBytes(
                  static_cast<Stage12ShadowPreset>(shadow.shadowPreset))) /
               (1024.0 * 1024.0))
           << ", \"surfaceEnabled\": "
           << (shadow.surfaceShadowEnabled != 0u ? "true" : "false")
           << ", \"surfaceStrength\": " << shadow.surfaceShadowStrength
           << ", \"surfaceAmbientFloor\": " << shadow.surfaceAmbientFloor
           << ", \"debugNearSlice\": " << shadow.debugNearSlice
           << ", \"debugFarSlice\": " << shadow.debugFarSlice
           << ", \"debugExposure\": " << shadow.cacheDebugExposure
           << "},\n"
           << "  \"cloudTypeMode\": \""
           << cloudTypeModeNames[cloudTypeIndex] << "\",\n";
    const CloudAppearanceSettings currentAppearance = CaptureCloudAppearance(
        cloud, cloudShape, generator);
    output << "  \"cloudAppearance\": {\n"
           << "    \"activePreset\": \""
           << CloudAppearancePresetName(cloudAppearancePreset) << "\",\n"
           << "    \"dirty\": "
           << (cloudAppearanceDirty ? "true" : "false") << ",\n"
           << "    \"current\": ";
    WriteAppearanceSettingsJson(output, currentAppearance, "");
    output << ",\n    \"savedCustom\": ";
    if (hasSavedCustomAppearance)
        WriteAppearanceSettingsJson(output, savedCustomAppearance, "");
    else
        output << "null";
    output << "\n  },\n"
           << "  \"sceneContract\": {\"name\": \"singlePortfolioDebugScene\", "
              "\"groundSizeMeters\": [10000.000000, 10000.000000], "
              "\"buildingSizeMeters\": [20.000000, 60.000000, 20.000000], "
              "\"supportedRadiusMeters\": 50000.000000},\n"
           << "  \"cameraMovement\": {\"speedMetersPerSecond\": "
           << m_cameraMoveSpeedMetersPerSecond
           << ", \"fastMultiplier\": 4.000000},\n"
           << "  \"sharedRenderState\": {\"weatherPreset\": \""
           << weatherPresetNames[weatherPresetIndex]
           << "\", \"viewStepMeters\": " << cloud.stepSize
           << ", \"maxViewSteps\": " << cloud.maxViewSteps
           << ", \"extinctionPerMeter\": " << cloud.extinctionCoefficient
           << ", \"baseWorldSizeMeters\": "
           << noiseVolumeParameters.baseWorldSizeMeters
           << ", \"detailWorldSizeMeters\": "
           << noiseVolumeParameters.detailWorldSizeMeters
           << ", \"maxLightSteps\": " << light.maxLightSteps
           << ", \"lightStepMeters\": " << light.lightStepSize
           << ", \"detailLodEnabled\": "
           << (lod.detailLodEnabled != 0u ? "true" : "false")
           << "},\n"
           << "  \"optimization\": {\"preset\": \""
           << stage9optimization::PresetName(optimizationPreset)
           << "\", \"supportPrecheck\": "
           << (optimization.supportPrecheckEnabled != 0u ? "true" : "false")
           << ", \"emptySearch\": "
           << (optimization.emptySpaceSkippingEnabled != 0u ? "true" : "false")
           << ", \"earlyExit\": "
           << (optimization.viewEarlyExitEnabled != 0u ? "true" : "false")
           << ", \"distanceStep\": "
           << (optimization.distanceStepEnabled != 0u ? "true" : "false")
           << ", \"emptySamplesBeforeCoarse\": "
           << optimization.emptySamplesBeforeCoarse
           << ", \"baseDensityEpsilon\": " << optimization.baseDensityEpsilon
           << ", \"coarseStepMultiplier\": " << optimization.coarseStepMultiplier
           << ", \"maxSearchStepMeters\": " << optimization.maxSearchStepMeters
           << ", \"distanceStepRangeMeters\": ["
           << optimization.distanceStepStartMeters << ", "
           << optimization.distanceStepEndMeters
           << "], \"farStepMultiplier\": " << optimization.farStepMultiplier
           << ", \"lightMode\": \""
           << (optimization.lightSamplingMode == static_cast<std::uint32_t>(
                   Stage9LightSamplingMode::DeterministicCone)
                   ? "deterministicCone" : "straightRay")
           << "\", \"coneSampleCount\": " << optimization.coneSampleCount
           << ", \"coneAngleDegrees\": " << optimization.coneAngleDegrees
           << ", \"lightFarSampleFraction\": "
           << optimization.lightFarSampleFraction << "},\n"
           << "  \"upsampling\": {\"resolutionPreset\": \""
           << stage10upsampling::ResolutionPresetName(resolutionPreset)
           << "\", \"resolutionScale\": " << upsampling.resolutionScale
           << ", \"filter\": \""
           << stage10upsampling::FilterName(
                  static_cast<Stage10UpsampleFilter>(upsampling.filterMode))
           << "\", \"sceneDepthRelativeSigma\": "
           << upsampling.sceneDepthRelativeSigma
           << ", \"cloudDepthRelativeSigma\": "
           << upsampling.cloudDepthRelativeSigma
           << ", \"transmittanceSigma\": "
           << upsampling.transmittanceSigma
           << ", \"minimumWeight\": " << upsampling.minimumWeight
           << ", \"targetSize\": [" << std::max(cloudRenderWidth, 1)
           << ", " << std::max(cloudRenderHeight, 1) << "]},\n"
           << "  \"camera\": {\n"
           << "    \"current\": ";
    if (m_currentCamera.valid)
    {
        output << "{\"positionMeters\": ["
               << m_currentCamera.position.x << ", "
               << m_currentCamera.position.y << ", "
               << m_currentCamera.position.z << "], \"targetMeters\": ["
               << m_currentCamera.target.x << ", "
               << m_currentCamera.target.y << ", "
               << m_currentCamera.target.z << "], \"verticalFovDegrees\": "
               << m_currentCamera.fovYDegrees
               << ", \"nearPlaneMeters\": "
               << m_currentCamera.nearPlaneMeters
               << ", \"farPlaneMeters\": "
               << m_currentCamera.farPlaneMeters << "}";
    }
    else
    {
        output << "null";
    }
    output << ",\n"
           << "    \"savedPosition\": ";
    if (m_savedCamera.valid)
    {
        output << "{\"positionMeters\": ["
               << m_savedCamera.position.x << ", "
               << m_savedCamera.position.y << ", "
               << m_savedCamera.position.z << "], \"targetMeters\": ["
               << m_savedCamera.target.x << ", "
               << m_savedCamera.target.y << ", "
               << m_savedCamera.target.z << "], \"verticalFovDegrees\": "
               << m_savedCamera.fovYDegrees
               << ", \"nearPlaneMeters\": "
               << m_savedCamera.nearPlaneMeters
               << ", \"farPlaneMeters\": "
               << m_savedCamera.farPlaneMeters << "}";
    }
    else
    {
        output << "null";
    }
    output << "\n  },\n"
           << "  \"noiseSource\": \""
           << (noiseVolumeParameters.noiseSource ==
                   static_cast<std::uint32_t>(NoiseSource::Texture3D)
                   ? "texture3D" : "proceduralLegacy") << "\",\n"
           << "  \"noiseVolumes\": {\n"
           << "    \"seed\": " << noiseVolumeParameters.seed << ",\n"
           << "    \"basePerlinOctaveSeedStride\": "
           << stage13noise::kBasePerlinOctaveSeedStride << ",\n"
           << "    \"base\": {\"size\": "
           << noiseVolumeParameters.baseResolution
           << ", \"format\": \"RGBA8_UNORM\", \"worldSizeMeters\": "
           << noiseVolumeParameters.baseWorldSizeMeters
           << ", \"verticalWorldSizeMeters\": "
           << noiseVolumeParameters.baseVerticalWorldSizeMeters
           << ", \"frequencies\": ["
           << noiseVolumeParameters.baseFrequencies.x << ", "
           << noiseVolumeParameters.baseFrequencies.y << ", "
           << noiseVolumeParameters.baseFrequencies.z << ", "
           << noiseVolumeParameters.baseFrequencies.w << "]"
           << ", \"hashFnv1a64\": \"" << std::hex << baseNoiseVolumeHash
           << std::dec << "\"},\n"
           << "    \"detail\": {\"size\": "
           << noiseVolumeParameters.detailResolution
           << ", \"format\": \"RGBA8_UNORM\", \"worldSizeMeters\": "
           << noiseVolumeParameters.detailWorldSizeMeters
           << ", \"frequencies\": ["
           << noiseVolumeParameters.detailFrequencies.x << ", "
           << noiseVolumeParameters.detailFrequencies.y << ", "
           << noiseVolumeParameters.detailFrequencies.z << ", "
           << noiseVolumeParameters.detailFrequencies.w << "]"
           << ", \"hashFnv1a64\": \"" << std::hex << detailNoiseVolumeHash
           << std::dec << "\"}\n"
           << "  },\n"
           << "  \"cloudDomain\": \"" << domainNames[domainIndex] << "\",\n"
           << "  \"cloudBottomAltitudeMeters\": "
           << domain.cloudBottomAltitude << ",\n"
           << "  \"cloudLayerThicknessMeters\": "
           << domain.cloudLayerThickness << ",\n"
           << "  \"cloudShape\": {\"mode\": \""
           << cloudshape::ModeName(cloudShape.shapeMode)
           << "\", \"stratusThicknessMeters\": ["
           << cloudShape.stratusMinimumThicknessMeters << ", "
           << cloudShape.stratusMaximumThicknessMeters
           << "], \"cumulusThicknessMeters\": ["
           << cloudShape.cumulusMinimumThicknessMeters << ", "
           << cloudShape.cumulusMaximumThicknessMeters
           << "], \"stratusProfile\": ["
           << cloudShape.stratusBottomFadeEnd << ", "
           << cloudShape.stratusTopFadeStart
           << "], \"mixedProfile\": ["
           << cloudShape.mixedBottomFadeEnd << ", "
           << cloudShape.mixedTopFadeStart
           << "], \"cumulusProfile\": ["
           << cloudShape.cumulusBottomFadeEnd << ", "
           << cloudShape.cumulusTopFadeStart << ", "
           << cloudShape.cumulusUpperMassBottom << ", "
           << cloudShape.cumulusUpperMassStart << ", "
           << cloudShape.cumulusUpperMassEnd
           << "], \"cirrus\": {\"flowDirectionXZ\": ["
           << cloudShape.cirrusFlowDirectionXZ.x << ", "
           << cloudShape.cirrusFlowDirectionXZ.y
           << "], \"baseScaleMeters\": ["
           << cloudShape.cirrusBaseAlongScaleMeters << ", "
           << cloudShape.cirrusBaseAcrossScaleMeters << ", "
           << cloudShape.cirrusBaseVerticalScaleMeters
           << "], \"detailScaleMeters\": ["
           << cloudShape.cirrusDetailAlongScaleMeters << ", "
           << cloudShape.cirrusDetailAcrossScaleMeters << ", "
           << cloudShape.cirrusDetailVerticalScaleMeters
           << "], \"thicknessMeters\": ["
           << cloudShape.cirrusMinimumThicknessMeters << ", "
           << cloudShape.cirrusMaximumThicknessMeters
           << "], \"verticalProfile\": ["
           << cloudShape.cirrusVerticalProfileCenter << ", "
           << cloudShape.cirrusVerticalProfileHalfWidth << "]}},\n"
           << "  \"maxViewTraceDistanceMeters\": "
           << domain.maxViewTraceDistance << ",\n"
           << "  \"viewTraceFadeStartDistanceMeters\": "
           << domain.viewTraceFadeStartDistance << ",\n"
           << "  \"maxLightTraceDistanceMeters\": "
           << domain.maxLightTraceDistance << ",\n"
           << "  \"detailDistanceLod\": {\"enabled\": "
           << (lod.detailLodEnabled != 0u ? "true" : "false")
           << ", \"startMeters\": " << lod.detailLodStartMeters
           << ", \"endMeters\": " << lod.detailLodEndMeters
           << ", \"neutralValue\": " << lod.detailNeutralValue
           << ", \"neutralSource\": \"measuredWeightedDetailVolumeMean\"},\n"
           << "  \"output\": \"" << outputNames[outputIndex] << "\",\n"
           << "  \"weatherPreset\": \"" << weatherPresetNames[weatherPresetIndex] << "\",\n"
           << "  \"sunPreset\": \"" << sunPresetNames[sunPresetIndex] << "\",\n"
           << "  \"phasePreset\": \"" << phasePresetNames[phasePresetIndex] << "\",\n"
           << "  \"environmentPreset\": \""
           << environmentPresetNames[environmentPresetIndex] << "\",\n"
           << "  \"lightingLook\": \""
           << (portfolioHeroLighting ? "portfolioHero" : "custom")
           << "\",\n"
           << "  \"environmentSource\": \"analyticColorsNoExternalTexture\",\n"
           << "  \"ambientModel\": \"heightDensityAoWithSunVisibilityCoupling\",\n"
           << "  \"directionConvention\": \"sampleToSunWorldDirection\",\n"
           << "  \"phaseDirectionConvention\": "
              "\"cosTheta=dot(cameraToSample,sampleToSun)\",\n"
           << "  \"directionToSun\": [" << light.directionToSun.x << ", "
           << light.directionToSun.y << ", " << light.directionToSun.z << "],\n"
           << "  \"sunColorLinear\": [" << light.sunColor.x << ", "
           << light.sunColor.y << ", " << light.sunColor.z << "],\n"
           << "  \"sunIntensity\": " << light.sunIntensity << ",\n"
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
           << "  \"edgeInfluence\": " << light.edgeInfluence << ",\n"
           << "  \"edgeOpticalDepthScale\": "
           << light.edgeOpticalDepthScale << ",\n"
           << "  \"shadowExponent\": " << light.shadowExponent << ",\n"
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
           << "  \"ambientShadowCoupling\": "
           << environment.ambientShadowCoupling << ",\n"
           << "  \"ambientShadowExponent\": "
           << environment.ambientShadowExponent << ",\n"
           << "  \"multipleScatteringModel\": \"reusedLightDepthInteriorWeightedOctaves\",\n"
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
           << "  \"multipleScatteringInteriorBlend\": "
           << environment.multipleScatteringInteriorBlend << ",\n"
           << "  \"weatherMapResolution\": [256, 256],\n"
           << "  \"weatherChannels\": {\"R\": \"coverage\", \"G\": \"cloudType\", "
              "\"B\": \"densityModifierSource\", \"A\": \"localThicknessPotential\"},\n"
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
           << "    \"localThickness\": {\"seed\": " << generator.localThickness.seed
           << ", \"macroPeriod\": " << generator.localThickness.macroPeriod
           << ", \"detailPeriod\": " << generator.localThickness.detailPeriod
           << ", \"detailWeight\": " << generator.localThickness.detailWeight
           << ", \"bias\": " << generator.localThickness.bias
           << ", \"contrast\": " << generator.localThickness.contrast << "},\n"
           << "    \"coverageThreshold\": " << generator.coverageThreshold << ",\n"
           << "    \"coverageSoftness\": " << generator.coverageSoftness << ",\n"
           << "    \"densityCoverageInfluence\": "
           << generator.densityCoverageInfluence << ",\n"
           << "    \"thicknessCoverageInfluence\": "
           << generator.thicknessCoverageInfluence << "\n"
           << "  },\n"
           << "  \"resolution\": [512, 512],\n"
           << "  \"slicePosition\": [" << m_parameters.normalizedSlicePosition.x << ", "
           << m_parameters.normalizedSlicePosition.y << ", "
           << m_parameters.normalizedSlicePosition.z << "],\n"
           << "  \"effectiveTime\": " << m_effectiveTime << ",\n"
           << "  \"baseNoiseScale\": " << cloud.baseNoiseScale << ",\n"
           << "  \"coverage\": " << cloud.coverage << ",\n"
           << "  \"densityMultiplier\": " << cloud.densityMultiplier << ",\n"
           << "  \"bottomFadeEnd\": " << cloud.bottomFadeEnd << ",\n"
           << "  \"topFadeStart\": " << cloud.topFadeStart << ",\n"
           << "  \"minimumLocalThicknessFraction\": "
           << cloud.minimumLocalThicknessFraction << ",\n"
           << "  \"localHeightVariation\": "
           << cloud.localHeightVariation << ",\n"
           << "  \"cumulusTopBoost\": " << cloud.cumulusTopBoost << ",\n"
           << "  \"detailNoiseScale\": " << cloud.detailNoiseScale << ",\n"
           << "  \"detailErosionStrength\": " << cloud.detailErosionStrength << ",\n"
           << "  \"detailWindSpeed\": " << cloud.detailWindSpeed << ",\n"
           << "  \"detailNoiseOffset\": " << cloud.detailNoiseOffset << ",\n"
           << "  \"weatherMapWorldSize\": " << cloud.weatherMapWorldSize << ",\n"
           << "  \"weatherMapWindSpeed\": " << cloud.weatherMapWindSpeed << ",\n"
           << "  \"weatherMapOffset\": [" << cloud.weatherMapOffset.x << ", "
           << cloud.weatherMapOffset.y << "],\n"
           << "  \"noiseOffset\": " << cloud.noiseOffset << ",\n"
           << "  \"windDirection\": [" << cloud.windDirection.x << ", "
           << cloud.windDirection.y << ", " << cloud.windDirection.z << "],\n"
           << "  \"windSpeed\": " << cloud.windSpeed << ",\n"
           << "  \"noiseImplementationSource\": \"Noise.hlsli\",\n"
           << "  \"noiseSourceHashFnv1a64\": \"" << std::hex
           << HashFile(noiseSourcePath) << "\"\n"
           << "}\n";
    return output.good();
}

bool NoiseLab::ExportSnapshot(const std::filesystem::path& root,
                              const CloudParameters& cloudParameters,
                              const CloudShapeParameters& cloudShapeParameters,
                              const CloudDomainParameters& cloudDomainParameters,
                              const CloudLodParameters& cloudLodParameters,
                              const OptimizationParameters& optimizationParameters,
                              Stage9OptimizationPreset optimizationPreset,
                              const Stage10UpsamplingParameters& upsamplingParameters,
                              Stage10ResolutionPreset resolutionPreset,
                              const Stage11TemporalParameters& temporalParameters,
                              const Stage12ShadowParameters& shadowParameters,
                              int cloudRenderWidth,
                              int cloudRenderHeight,
                              const LightParameters& lightParameters,
                              Stage6SunPreset sunPreset,
                              Stage7PhasePreset phasePreset,
                              const EnvironmentParameters& environmentParameters,
                              Stage8EnvironmentPreset environmentPreset,
                              Stage5WeatherPreset weatherPreset,
                              CloudTypeMode cloudTypeMode,
                              CloudAppearancePreset cloudAppearancePreset,
                              bool cloudAppearanceDirty,
                              bool hasSavedCustomAppearance,
                              const CloudAppearanceSettings& savedCustomAppearance,
                              const NoiseVolumeParameters& noiseVolumeParameters,
                              std::uint64_t baseNoiseVolumeHash,
                              std::uint64_t detailNoiseVolumeHash,
                              const WeatherMapGeneratorSettings& weatherGeneratorSettings,
                              std::uint64_t weatherMapHash,
                              ID3D11Texture2D* weatherMapTexture,
                              const std::filesystem::path& noiseSourcePath,
                              bool includePng)
{
    std::error_code error;
    const std::filesystem::path directory = TimestampDirectory(root);
    std::filesystem::create_directories(directory, error);
    if (error)
    {
        m_exportStatus = "Export failed: " + error.message();
        return false;
    }

    const bool pngSuccess = !includePng ||
                            (SaveTargetPng(directory / L"xy.png", m_targets[0]) &&
                             SaveTargetPng(directory / L"xz.png", m_targets[1]) &&
                             SaveTargetPng(directory / L"yz.png", m_targets[2]) &&
                             SaveTexturePng(
                                 directory / L"weather-map.png",
                                 weatherMapTexture));
    const bool success = pngSuccess &&
                         WriteMetadata(directory / L"noise-settings.json",
                                       cloudParameters, cloudShapeParameters,
                                       cloudDomainParameters,
                                       cloudLodParameters,
                                       optimizationParameters,
                                       optimizationPreset,
                                       upsamplingParameters,
                                       resolutionPreset,
                                       temporalParameters,
                                       shadowParameters,
                                       cloudRenderWidth,
                                       cloudRenderHeight,
                                       lightParameters, sunPreset,
                                       phasePreset, environmentParameters,
                                       environmentPreset, weatherPreset,
                                       cloudTypeMode,
                                       cloudAppearancePreset,
                                       cloudAppearanceDirty,
                                       hasSavedCustomAppearance,
                                       savedCustomAppearance,
                                       noiseVolumeParameters,
                                       baseNoiseVolumeHash,
                                       detailNoiseVolumeHash,
                                       weatherGeneratorSettings, weatherMapHash,
                                       noiseSourcePath);
    if (success)
    {
        m_exportStatus = std::string(includePng
            ? "Exported: " : "Metadata exported: ") + NarrowUtf8(directory);
    }
    else
    {
        m_exportStatus = includePng
            ? "Export failed while writing PNG or JSON"
            : "Export failed while writing JSON";
    }
    return success;
}
