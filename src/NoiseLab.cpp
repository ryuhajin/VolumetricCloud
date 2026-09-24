#include "NoiseLab.h"
#include "LightingPresetStore.h"
#include "FormationParameterRanges.h"
#include "CloudShapeDomainContract.h"
#include "Fnv1a64.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "Camera.h"
#include "Stage13SceneMath.h"
#include "Stage15Parameters.h"
#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"

using Microsoft::WRL::ComPtr;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

namespace
{
// 사용자 입력만 보정한다. 창을 열거나 항목을 숨기는 것으로 설정을 쓰지 않는다.
bool FormationSliderFloat(const char* label, float* value, float minimum, float maximum,
    const char* format = "%.3f", ImGuiSliderFlags flags = 0)
{
    return ImGui::SliderFloat(label, value, minimum, maximum, format,
        flags | ImGuiSliderFlags_AlwaysClamp);
}

constexpr float kMaximumCloudTimeSeconds = 86400.0f;
constexpr float kMaximumCloudMovementSpeedMetersPerSecond = formationrange::motionSpeedMax;

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

const char* NoiseOutputName(NoiseOutputMode mode)
{
    static constexpr const char* names[] = {
        "Raw Noise", "Threshold Density", "Final Density",
        "Height Fraction", "Height Profile", "Base Density",
        "Detail Noise", "Erosion", "Detail Sample Mask",
        "Weather Coverage", "Cloud Type", "Weather Density",
        "Weather Threshold", "Typed Profile", "Weather UV",
        "Base R", "Base G", "Base B", "Base A", "Base Combined",
        "Detail R", "Detail G", "Detail B", "Detail A",
        "Detail Combined", "Thickness Potential", "Local Thickness",
        "Local Height", "Effective Coverage", "Base Support",
        "Local Base Offset"
    };
    const std::size_t index = static_cast<std::size_t>(mode);
    return index < std::size(names) ? names[index] : "Raw Noise";
}

const char* DebugName(CloudDebugMode mode)
{
    switch (mode)
    {
    case CloudDebugMode::Composite: return "Composite";
    case CloudDebugMode::FinalDensity: return "Density";
    case CloudDebugMode::Transmittance: return "Transmittance";
    case CloudDebugMode::ViewOpticalDepth: return "Optical Depth";
    case CloudDebugMode::CloudDepth: return "Cloud Depth";
    case CloudDebugMode::CloudWithoutAerial: return "Cloud without aerial perspective";
    case CloudDebugMode::AirTransmittanceAtCloud: return "Air T at cloud depth";
    case CloudDebugMode::AirRadianceAtCloud: return "Air L at cloud depth";
    case CloudDebugMode::OccupiedCloudLength: return "Occupied cloud length";
    case CloudDebugMode::OccupiedCloudMeanDensity: return "Mean density in occupied cloud";
    case CloudDebugMode::FirstOccupiedCloudDistance: return "Distance to first occupied cloud";
    case CloudDebugMode::AccumulatedDirectLighting: return "Direct";
    case CloudDebugMode::AccumulatedSkyAmbient: return "Sky";
    case CloudDebugMode::AccumulatedGroundBounce: return "Ground";
    case CloudDebugMode::AccumulatedMultipleScattering: return "Multiple";
    case CloudDebugMode::SilverLiningContribution: return "Silver Lining (part of Direct)";
    case CloudDebugMode::ShapedSunVisibility: return "Shaped Sun Visibility";
    case CloudDebugMode::AmbientVisibility: return "Ground Ambient Visibility";
    case CloudDebugMode::VisibleSunTransmittance: return "Visible Sun T (opacity weighted)";
    case CloudDebugMode::LightTransmittance: return "Sun T (segment midpoint / key 9)";
    case CloudDebugMode::DualPhaseFactor: return "Phase Factor (segment midpoint)";
    case CloudDebugMode::Stage12NearOpticalDepth: return "Near Cache";
    case CloudDebugMode::Stage12FarOpticalDepth: return "Far Cache";
    case CloudDebugMode::Stage12CascadeSelection: return "Cache Cascade";
    case CloudDebugMode::Stage12SurfaceTransmittance:
        return "Surface Transmittance";
    default: return "Unknown";
    }
}

constexpr CloudDebugMode kCloudDiagnosticModes[] = {
    CloudDebugMode::Composite,
    CloudDebugMode::FinalDensity,
    CloudDebugMode::Transmittance,
    CloudDebugMode::ViewOpticalDepth,
    CloudDebugMode::CloudDepth,
    CloudDebugMode::CloudWithoutAerial,
    CloudDebugMode::AirTransmittanceAtCloud,
    CloudDebugMode::AirRadianceAtCloud,
    CloudDebugMode::OccupiedCloudLength,
    CloudDebugMode::OccupiedCloudMeanDensity,
    CloudDebugMode::FirstOccupiedCloudDistance,
    CloudDebugMode::AccumulatedDirectLighting,
    CloudDebugMode::AccumulatedSkyAmbient,
    CloudDebugMode::AccumulatedGroundBounce,
    CloudDebugMode::AccumulatedMultipleScattering,
    CloudDebugMode::SilverLiningContribution,
    CloudDebugMode::VisibleSunTransmittance,
    CloudDebugMode::ShapedSunVisibility,
    CloudDebugMode::AmbientVisibility,
    CloudDebugMode::LightTransmittance,
    CloudDebugMode::DualPhaseFactor,
    CloudDebugMode::Stage12NearOpticalDepth,
    CloudDebugMode::Stage12FarOpticalDepth,
    CloudDebugMode::Stage12CascadeSelection,
    CloudDebugMode::Stage12SurfaceTransmittance,
};



const char* AtmosphereDebugName(Stage14DebugView view)
{
    switch (view)
    {
    case Stage14DebugView::None: return "None";
    case Stage14DebugView::Transmittance: return "Transmittance LUT";
    case Stage14DebugView::MultiScattering: return "Multi Scattering LUT";
    case Stage14DebugView::SkyView: return "Sky View LUT";
    case Stage14DebugView::SkyIrradiance: return "Sky Irradiance LUT";
    case Stage14DebugView::AerialRadiance: return "Aerial Radiance LUT";
    case Stage14DebugView::AerialTransmittance:
        return "Aerial Transmittance LUT";
    case Stage14DebugView::HdrPreTone: return "HDR Before Tone";
    default: return "Other";
    }
}
}

NoiseLab::~NoiseLab()
{
    Shutdown();
}

bool NoiseLab::Init(
    HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context,
    const std::filesystem::path& developerUiSettingsPath)
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
    io.Fonts->Clear();
    io.Fonts->AddFontDefaultVector();

    m_developerUiSettingsPath = developerUiSettingsPath;
    developerui::Load(m_developerUiSettingsPath, m_developerUiSettings,
                      m_developerUiSettingsStatus);
    m_developerUiSettings.userZoom = developerui::SanitizeUserZoom(
        m_developerUiSettings.userZoom);
    m_developerUiDpi = hwnd ? std::max(GetDpiForWindow(hwnd), 96u) : 96u;
    ApplyDeveloperUiScale();
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
    m_weatherTargets = {};
    m_microTargets = {};
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
    desc.BindFlags = D3D11_BIND_RENDER_TARGET |
                     D3D11_BIND_SHADER_RESOURCE;

    D3D11_TEXTURE2D_DESC staging = desc;
    staging.Usage = D3D11_USAGE_STAGING;
    staging.BindFlags = 0;
    staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    for (SliceTarget& target : m_targets)
    {
        if (FAILED(m_device->CreateTexture2D(
                &desc, nullptr, &target.texture)) ||
            FAILED(m_device->CreateRenderTargetView(
                target.texture.Get(), nullptr, &target.rtv)) ||
            FAILED(m_device->CreateShaderResourceView(
                target.texture.Get(), nullptr, &target.srv)) ||
            FAILED(m_device->CreateTexture2D(
                &staging, nullptr, &target.staging)))
        {
            return false;
        }
    }
    for (SliceTarget& target : m_microTargets)
    {
        if (FAILED(m_device->CreateTexture2D(&desc, nullptr, &target.texture)) ||
            FAILED(m_device->CreateRenderTargetView(target.texture.Get(), nullptr, &target.rtv)) ||
            FAILED(m_device->CreateShaderResourceView(target.texture.Get(), nullptr, &target.srv)))
            return false;
    }
    for (SliceTarget& target : m_weatherTargets)
    {
        if (FAILED(m_device->CreateTexture2D(&desc, nullptr, &target.texture)) ||
            FAILED(m_device->CreateRenderTargetView(target.texture.Get(), nullptr, &target.rtv)) ||
            FAILED(m_device->CreateShaderResourceView(target.texture.Get(), nullptr, &target.srv)))
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

void NoiseLab::SetDpi(unsigned int dpi)
{
    const unsigned int safeDpi = std::max(dpi, 96u);
    if (safeDpi != m_developerUiDpi)
    {
        m_developerUiDpi = safeDpi;
        m_scaleDirty = true;
    }
}

bool NoiseLab::SetUserZoom(float value)
{
    const float safe = developerui::SanitizeUserZoom(value);
    if (std::abs(safe - m_developerUiSettings.userZoom) <= 1.0e-5f)
        return false;
    m_developerUiSettings.userZoom = safe;
    m_scaleDirty = true;
    return true;
}

void NoiseLab::ApplyDeveloperUiScale()
{
    if (ImGui::GetCurrentContext() == nullptr)
        return;
    const float scale = developerui::EffectiveScale(
        m_developerUiDpi, m_developerUiSettings.userZoom);
    if (!m_scaleDirty && std::abs(scale - m_effectiveUiScale) <= 1.0e-5f)
        return;
    ImGuiStyle style;
    ImGui::StyleColorsDark(&style);
    style.ScaleAllSizes(scale);
    style.FontScaleMain = scale;
    ImGui::GetStyle() = style;
    m_effectiveUiScale = scale;
    m_scaleDirty = false;
}

float NoiseLab::StyleWindowPaddingXForValidation() const
{
    return ImGui::GetCurrentContext()
        ? ImGui::GetStyle().WindowPadding.x : 0.0f;
}

bool NoiseLab::SetDeveloperUiScaleForValidation(unsigned int dpi,
                                                 float userZoom)
{
    SetDpi(dpi);
    const bool changed = SetUserZoom(userZoom);
    ApplyDeveloperUiScale();
    return changed;
}

bool NoiseLab::HandleWindowMessage(
    HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_DPICHANGED)
        SetDpi(HIWORD(wParam));
    if (!m_initialized)
        return false;
    ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam);
    const ImGuiIO& io = ImGui::GetIO();
    return (IsMouseMessage(message) && io.WantCaptureMouse) ||
        (IsKeyboardMessage(message) && WantsKeyboardCapture());
}

bool NoiseLab::WantsKeyboardCapture() const
{
    if (!m_initialized || !ImGui::GetCurrentContext())
        return false;
    const ImGuiIO& io = ImGui::GetIO();
    return stage13scene::ShouldBlockSceneKeyboard(
        io.WantTextInput, ImGui::IsAnyItemActive());
}

void NoiseLab::TogglePanel(DeveloperUiPanel panel)
{
    const std::size_t index = static_cast<std::size_t>(panel);
    if (index >= m_panelVisible.size())
        return;
    const bool close = m_panelVisible[index];
    m_panelVisible.fill(false);
    if (!close)
        m_panelVisible[index] = true;
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
    m_effectiveTime += delta;
    if (m_effectiveTime > kMaximumCloudTimeSeconds)
        m_effectiveTime = std::fmod(m_effectiveTime, kMaximumCloudTimeSeconds);
    m_parameters.effectiveTime = m_effectiveTime;
}

void NoiseLab::SetCloudTimeForValidation(float timeSeconds)
{
    m_effectiveTime = std::clamp(
        std::isfinite(timeSeconds) ? timeSeconds : 0.0f,
        0.0f, kMaximumCloudTimeSeconds);
    m_parameters.effectiveTime = m_effectiveTime;
}

void NoiseLab::BeginFrame(
    float applicationTime,
    Camera& camera,
    CloudParameters& cloud,
    CloudShapeParameters& shape,
    CloudDomainParameters& domain,
    WeatherMapDefinition& weather,
    CloudTypeSelection& typeSelection,
    CloudMotionParameters& motion,
    Stage12ShadowParameters& shadow,
    LightParameters& light,
    Stage6SunPreset& sunPreset,
    Stage7PhasePreset& phasePreset,
    EnvironmentParameters& environment,
    Stage8EnvironmentPreset& environmentPreset,
    AtmosphereParameters& atmosphere,
    GroundLightingParameters& ground,
    ToneMappingParameters& tone,
    Stage15ConceptPreset concept,
    const CloudFormationPresetTarget& formationTarget,
    CloudFormationPresetSource formationSource,
    bool hasCustomFormation,
    const std::string& formationStatus,
    float& cameraMoveSpeedMetersPerSecond,
    NoiseVolumeParameters& noiseVolume,
    std::uint64_t baseNoiseVolumeHash,
    std::uint64_t detailNoiseVolumeHash,
    double noiseVolumeGenerationMilliseconds,
    double weatherMapGenerationMilliseconds,
    std::uint64_t weatherMapGeneration,
    ID3D11ShaderResourceView* weatherMapSrv,
    const std::array<ID3D11ShaderResourceView*, 6>& atmosphereLutSrvs,
    const FrameTimingSnapshot& timing,
    bool& vsyncEnabled,
    bool tearingSupported,
    std::uint64_t shaderGeneration,
    const std::string& shaderStatus,
    const std::string& shaderError,
    const shaderreload::ReloadReport& reloadReport)
{
    if (!m_initialized)
        return;
    UpdateEffectiveTime(applicationTime);
    ApplyDeveloperUiScale();
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    if (m_panelVisible[0])
        DrawFormationPanel(cloud, shape, domain, weather, typeSelection, motion,
                           formationTarget, formationSource,
                           hasCustomFormation, formationStatus,
                           vsyncEnabled, tearingSupported);
    if (m_panelVisible[1])
        DrawWeatherMapPanel(cloud, shape, weather.generator, noiseVolume,
                            baseNoiseVolumeHash, detailNoiseVolumeHash,
                            noiseVolumeGenerationMilliseconds,
                            weatherMapGenerationMilliseconds,
                            weatherMapGeneration,
                            weatherMapSrv);
    if (m_panelVisible[2])
        DrawLightingPanel(shadow, light, sunPreset, phasePreset,
                          environment, environmentPreset,
                          atmosphere, ground, tone);
    if (m_panelVisible[3])
        DrawDiagnosticsPanel(camera, cloud, atmosphere, concept,
                             cameraMoveSpeedMetersPerSecond,
                             atmosphereLutSrvs,
                             shaderGeneration, shaderStatus, shaderError,
                             reloadReport);
    DrawProfilerOverlay(timing, weatherMapGenerationMilliseconds,
                        weatherMapGeneration);
}

void NoiseLab::DrawFormationPanel(
    CloudParameters& cloud,
    CloudShapeParameters& shape,
    CloudDomainParameters& domain,
    WeatherMapDefinition& weather,
    CloudTypeSelection& typeSelection,
    CloudMotionParameters& motion,
    const CloudFormationPresetTarget& target,
    CloudFormationPresetSource source,
    bool hasCustom,
    const std::string& status,
    bool& vsyncEnabled,
    bool tearingSupported)
{
    ImGui::SetNextWindowSize(ImVec2(Ui(520.0f), Ui(760.0f)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("F1 Cloud Formation", &m_panelVisible[0]))
    {
        ImGui::End();
        return;
    }
    const struct TypeButton
    {
        const char* label;
        CloudFormationType type;
    } buttons[] = {
        { "Stratus", CloudFormationType::Stratus },
        { "Cumulus", CloudFormationType::Cumulus },
        { "Altocumulus", CloudFormationType::Mixed },
    };
    for (std::size_t index = 0; index < std::size(buttons); ++index)
    {
        if (index > 0)
            ImGui::SameLine();
        if (ImGui::Button(buttons[index].label))
        {
            m_formationPresetRequest =
                TypeFormationTarget(buttons[index].type);
            m_formationPresetPending = true;
        }
    }
    ImGui::SameLine();

    if (ImGui::Button("Custom"))
        m_loadCustomPending = true;

    ImGui::Text("Active: %s (%s)",
                CloudFormationPresetTargetName(target),
                CloudFormationPresetSourceName(source));
    ImGui::TextWrapped("%s", status.c_str());
    ImGui::TextWrapped("%s", CloudFormationPresetPath(m_presetRoot, target).generic_string().c_str());
    ImGui::TextWrapped("Selecting a slot reloads it and discards unsaved edits.");
    if (ImGui::Button("Save Preset"))
        m_saveCustomPending = true;

    ImGui::SeparatorText("Presentation");
    ImGui::Checkbox("VSync", &vsyncEnabled);
    ImGui::SameLine();
    ImGui::TextDisabled("Default: On");
    if (vsyncEnabled)
        ImGui::TextDisabled("Present: interval 1 (display synchronized)");
    else if (tearingSupported)
        ImGui::TextDisabled("Present: immediate + tearing allowed");
    else
        ImGui::TextDisabled(
            "Present: immediate; Windows/driver may still limit FPS");

    ImGui::SeparatorText("Formation");
    bool edited = false;
    edited |= FormationSliderFloat("Coverage", &cloud.coverage,
                                 0.0f, 1.0f, "%.3f");
    edited |= FormationSliderFloat("Density", &cloud.densityMultiplier,
                                 0.0f, formationrange::densityMax, "%.3f");
    edited |= FormationSliderFloat("Extinction / m",
                                 &cloud.extinctionCoefficient,
                                 formationrange::extinctionMin, formationrange::extinctionMax, "%.6f", ImGuiSliderFlags_Logarithmic);
    edited |= FormationSliderFloat("Detail erosion",
                                 &cloud.detailErosionStrength,
                                 0.0f, 1.0f, "%.3f");


    edited |= FormationSliderFloat("Density shaping",
        &shape.densityShaping, 0.0f, 1.0f, "%.2f");

    ImGui::SeparatorText("Cloud Local");
    auto& column = weather.column;
    edited |= FormationSliderFloat("Cloud min thickness", &column.minimumThicknessMeters,
        1.0f, column.maximumThicknessMeters, "%.0f m", ImGuiSliderFlags_Logarithmic);
    edited |= FormationSliderFloat("Cloud max thickness", &column.maximumThicknessMeters,
        column.minimumThicknessMeters, 6000.0f, "%.0f m", ImGuiSliderFlags_Logarithmic);
    const float availableLift = std::clamp(domain.cloudLayerThickness -
        cloudshapedomain::ActiveMaximumThicknessMeters(column, typeSelection) - 200.0f,
        0.0f, formationrange::liftMax);
    edited |= FormationSliderFloat("Max base lift", &column.maximumBaseLiftMeters,
        0.0f, availableLift, "%.0f m");
    edited |= FormationSliderFloat("Height-based narrowing", &shape.footprintCoverageInfluence, 0.0f, 1.0f, "%.3f");

    ImGui::SeparatorText("Vertical Profile");
    const auto heightSlider = [&](const char* label, float& ratio, float lo, float hi) {
        float percent=ratio*100.0f;
        if (!FormationSliderFloat(label,&percent,lo*100.0f,hi*100.0f,"%.1f %%")) return false;
        ratio=percent*0.01f;
        return true;
    };
    edited |= heightSlider("Bottom fade end",shape.bottomFadeEnd,0.01f,shape.topFadeStart);
    edited |= heightSlider("Top fade start",shape.topFadeStart,shape.bottomFadeEnd,0.99f);
    edited |= FormationSliderFloat("Lower density", &shape.lowerDensityScale, 0.0f, 1.0f);
    edited |= heightSlider("Density rise start",shape.upperTransitionStart,0.0f,shape.upperTransitionEnd-0.01f);
    edited |= heightSlider("Density rise end",shape.upperTransitionEnd,shape.upperTransitionStart+0.01f,1.0f);
    float profile[65];
    for (int i=0;i<65;++i) profile[i]=EvaluateCommonVerticalProfile(i/64.0f,shape);
    ImGui::PlotLines("Density vs height", profile, 65, 0, "Bottom 0 -> Top 1", 0.f, 1.f, ImVec2(0,Ui(80.f)));

    ImGui::SeparatorText("Ray Domain");
    edited |= FormationSliderFloat("Domain bottom altitude", &domain.cloudBottomAltitude,
        0.0f, 7000.0f, "%.0f m");
    const float minimumDomainHeight = cloudshapedomain::EvaluateFit(column, typeSelection, domain, 200.0f).requiredLayerThicknessMeters;
    edited |= FormationSliderFloat("Domain height", &domain.cloudLayerThickness,
        minimumDomainHeight, std::max(5000.0f, minimumDomainHeight), "%.0f m", ImGuiSliderFlags_Logarithmic);

    ImGui::SeparatorText("Cloud Movement");
    bool motionEdited = false;
    float directionXZ[2] = { motion.direction.x, motion.direction.z };
    if (ImGui::SliderFloat2(
            "Wind direction XZ", directionXZ, -1.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp))
    {
        motion.direction = { directionXZ[0], 0.0f, directionXZ[1] };
        motionEdited = true;
    }
    motionEdited |= FormationSliderFloat("Cloud movement speed", &motion.speedMetersPerSecond,
        0.0f, kMaximumCloudMovementSpeedMetersPerSecond, "%.1f m/s",
        ImGuiSliderFlags_AlwaysClamp);
    ImGui::SeparatorText("Preview");
    const int maximumMode = static_cast<int>(NoiseOutputMode::LocalBaseOffset);
    int output = static_cast<int>(m_parameters.outputMode);
    if (ImGui::BeginCombo("Preview field", NoiseOutputName(
            static_cast<NoiseOutputMode>(output))))
    {
        for (int modeIndex = 0; modeIndex <= maximumMode; ++modeIndex)
        {
            const NoiseOutputMode mode =
                static_cast<NoiseOutputMode>(modeIndex);
            const bool selected = modeIndex == output;
            ImGui::PushID(modeIndex);
            if (ImGui::Selectable(NoiseOutputName(mode), selected))
                m_parameters.outputMode = static_cast<std::uint32_t>(mode);
            ImGui::PopID();
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    DrawSlice("XY", NoiseSliceAxis::XY, m_targets[0]);
    ImGui::SameLine();
    DrawSlice("XZ", NoiseSliceAxis::XZ, m_targets[1]);
    DrawSlice("YZ", NoiseSliceAxis::YZ, m_targets[2]);
    if (edited)
        m_formationEdited = true;
    if (motionEdited)
        motion = SanitizeCloudMotionParameters(motion);
    ImGui::End();
}

bool NoiseLab::DrawPeriodicChannelFields(
    const char* label, PeriodicChannelSettings& settings)
{
    bool edited = false;
    if (ImGui::TreeNode(label))
    {
        ImGui::PushID(label);
        edited |= ImGui::InputScalar(
            "Seed", ImGuiDataType_U32, &settings.seed);
        int macro = static_cast<int>(settings.macroPeriod);
        int detail = static_cast<int>(settings.detailPeriod);
        if (ImGui::SliderInt("Macro period", &macro, formationrange::macroMin, formationrange::macroMax, "%d", ImGuiSliderFlags_AlwaysClamp)) {
            settings.macroPeriod = static_cast<std::uint32_t>(macro); edited = true;
        }
        if (ImGui::SliderInt("Detail period", &detail, formationrange::detailMin, formationrange::detailMax, "%d", ImGuiSliderFlags_AlwaysClamp)) {
            settings.detailPeriod = static_cast<std::uint32_t>(detail); edited = true;
        }
        edited |= FormationSliderFloat(
            "Detail weight", &settings.detailWeight, 0.0f, 1.0f);
        edited |= FormationSliderFloat("Bias", &settings.bias, formationrange::biasMin, formationrange::biasMax);
        edited |= FormationSliderFloat(
            "Contrast", &settings.contrast, formationrange::contrastMin, formationrange::contrastMax);
        ImGui::PopID();
        ImGui::TreePop();
    }
    return edited;
}

void NoiseLab::DrawWeatherMapPanel(
    CloudParameters& cloud,
    CloudShapeParameters& shape,
    WeatherMapGeneratorSettings& weather,
    NoiseVolumeParameters& noiseVolume,
    std::uint64_t baseHash,
    std::uint64_t detailHash,
    double generationMilliseconds,
    double weatherGenerationMilliseconds,
    std::uint64_t weatherGeneration,
    ID3D11ShaderResourceView* weatherMapSrv)
{
    ImGui::SetNextWindowSize(ImVec2(Ui(520.0f), Ui(720.0f)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("F2 Weather Map", &m_panelVisible[1]))
    {
        ImGui::End();
        return;
    }
    ImGui::SeparatorText("Base / Detail Noise Scale");
    bool edited = false;
    edited |= FormationSliderFloat("Base world size",
        &noiseVolume.baseWorldSizeMeters, 3000.0f, 40000.0f, "%.0f m", ImGuiSliderFlags_Logarithmic);
    edited |= FormationSliderFloat("Base vertical size",
        &noiseVolume.baseVerticalWorldSizeMeters,
        3000.0f, 40000.0f, "%.0f m", ImGuiSliderFlags_Logarithmic);
    edited |= FormationSliderFloat("Detail world size",
        &noiseVolume.detailWorldSizeMeters, 700.0f, 6000.0f, "%.0f m", ImGuiSliderFlags_Logarithmic);
    if (ImGui::Button("Regenerate Base and Detail"))
        m_noiseVolumeRegeneratePending = true;
    ImGui::Text("Base hash: %016llx",
        static_cast<unsigned long long>(baseHash));
    ImGui::Text("Detail hash: %016llx",
        static_cast<unsigned long long>(detailHash));
    ImGui::Text("Generation: %.2f ms", generationMilliseconds);

    // 세션 전용 Base 비교. Original과 중간 옥타브 x2.5만 남긴다(2026-09-24 정리).
    ImGui::SeparatorText("Temporary Base comparison");
    const char* baseCandidates[]={"Original", "Mid octaves x2.5"};
    int baseCandidate=m_baseCandidate;
    if(ImGui::Combo("Base candidate", &baseCandidate, baseCandidates, 2))
        m_baseCandidateRequest=baseCandidate;
    if(ImGui::Button("Restore original Base"))m_baseCandidateRequest=0;
    ImGui::TextDisabled("Session only; Save Preset does not save this candidate.");
    if(!m_baseCandidateStatus.empty())ImGui::TextWrapped("%s",m_baseCandidateStatus.c_str());
    ImGui::TextDisabled("Base mid octaves: %.2fx%s", m_baseCandidate==1?2.5f:1.0f+noiseVolume.baseMidOctaveExtra,
        m_baseCandidate==1?" (temporary)":" (original)");

    // 근경 미세 Detail(E19). 선택한 타입의 Formation 값이며 F1 Save Preset으로 타입별 JSON에 저장된다.
    ImGui::SeparatorText("Near micro detail");
    edited |= FormationSliderFloat("Near micro tile", &noiseVolume.nearMicroTileMeters,
        formationrange::microTileMin, formationrange::microTileMax, "%.0f m", ImGuiSliderFlags_Logarithmic);
    edited |= FormationSliderFloat("Near micro strength", &shape.nearMicroStrength,
        0.0f, formationrange::microStrengthMax, "%.2f");
    edited |= FormationSliderFloat("Near micro mean", &shape.nearMicroMean,
        formationrange::microMeanMin, formationrange::microMeanMax, "%.4f");
    edited |= FormationSliderFloat("Near micro warp", &shape.nearMicroWarp,
        0.0f, formationrange::microWarpMax, "%.2f");
    edited |= FormationSliderFloat("Near micro warp freq", &shape.nearMicroWarpFrequency,
        formationrange::microWarpFrequencyMin, formationrange::microWarpFrequencyMax, "%.2f");
    if (ImGui::Button("Reset near micro"))
    {
        noiseVolume.nearMicroTileMeters = nearmicro::kDefaultTileMeters;
        shape.nearMicroStrength = nearmicro::kDefaultStrength;
        shape.nearMicroMean = nearmicro::kDefaultMean;
        shape.nearMicroWarp = nearmicro::kDefaultWarp;
        shape.nearMicroWarpFrequency = nearmicro::kDefaultWarpFrequency;
        edited = true;
    }
    ImGui::TextDisabled("Strength 0 = off. Mean %.4f = texture mean (lower: more erosion).", nearmicro::kDefaultMean);
    ImGui::TextDisabled("Fades from ~%.1f km to ~%.1f km at 1080p/FOV60. Saved per type (F1 Save Preset).",
        noiseVolume.nearMicroTileMeters * 14.6f * 0.001f, noiseVolume.nearMicroTileMeters * 29.3f * 0.001f);
    ImGui::SliderFloat("Preview extent", &m_parameters.microPreviewExtentMeters,
        500.0f, 8000.0f, "%.0f m", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
    {
        const char* names[] = {"Detail (world)", "Near micro texture", "Near micro applied"};
        for (int i = 0; i < 3; ++i) {
            if (i > 0) ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::TextUnformatted(names[i]);
            ImGui::Image(ImTextureRef(static_cast<ImTextureID>(
                reinterpret_cast<std::uintptr_t>(m_microTargets[i].srv.Get()))),
                ImVec2(Ui(160.0f), Ui(160.0f)));
            ImGui::EndGroup();
        }
        ImGui::TextDisabled("Same horizontal world square, mid-layer height, no wind.");
        ImGui::TextDisabled("Each image: 0.5 gray = mean, black/white = -/+3 std. Applied = dual lookup + warp, before strength.");
    }
    ImGui::SeparatorText("Weather Map");
    edited |= FormationSliderFloat("Weather map scale", &cloud.weatherMapWorldSize,
        10000.0f, 64000.0f, "%.0f m", ImGuiSliderFlags_Logarithmic);
    edited |= FormationSliderFloat("Density coverage link",
        &weather.densityCoverageInfluence, 0.0f, 1.0f);
    edited |= FormationSliderFloat("Thickness coverage link",
        &weather.thicknessCoverageInfluence, 0.0f, 1.0f);

    ImGui::Text("Last GPU generation: %.3f ms (#%llu)",
        weatherGenerationMilliseconds,
        static_cast<unsigned long long>(weatherGeneration));
    if (weatherMapSrv)
    {
        const char* names[] = {"R/B/A composite", "R - Coverage", "B - Density", "A - Thickness potential"};
        for (int channel = 0; channel < 4; ++channel) {
            if (channel == 1 || channel == 3) ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::TextUnformatted(names[channel]);
            ImGui::Image(ImTextureRef(static_cast<ImTextureID>(
                reinterpret_cast<std::uintptr_t>(m_weatherTargets[channel].srv.Get()))),
                ImVec2(Ui(200.0f), Ui(200.0f)));
            ImGui::EndGroup();
        }
    }
    ImGui::SeparatorText("Weather Generator");
    edited |= FormationSliderFloat("Coverage threshold (R)",
        &weather.coverageThreshold, 0.0f, 1.0f, "%.3f");
    edited |= FormationSliderFloat("Coverage softness (R)",
        &weather.coverageSoftness, formationrange::softnessMin,
        formationrange::softnessMax, "%.3f");
    edited |= DrawPeriodicChannelFields("Coverage channel", weather.coverage);
    edited |= DrawPeriodicChannelFields("Density channel", weather.density);
    edited |= DrawPeriodicChannelFields(
        "Thickness channel", weather.localThickness);
    if (edited)
        m_formationEdited = true;
    ImGui::End();
}

void NoiseLab::DrawLightingPanel(
    Stage12ShadowParameters& shadow,
    LightParameters& light,
    Stage6SunPreset& sunPreset,
    Stage7PhasePreset& phasePreset,
    EnvironmentParameters& environment,
    Stage8EnvironmentPreset& environmentPreset,
    AtmosphereParameters& atmosphere,
    GroundLightingParameters& ground,
    ToneMappingParameters& tone)
{
    ImGui::SetNextWindowSize(ImVec2(Ui(520.0f), Ui(760.0f)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("F3 Lighting Atmosphere Tone", &m_panelVisible[2]))
    {
        ImGui::End();
        return;
    }
    bool edited = false;
    ImGui::SeparatorText("Sun and Cloud Lighting");
    edited |= ImGui::SliderFloat("Sun azimuth",
        &atmosphere.sunAzimuthDegrees, -180.0f, 180.0f, "%.1f deg");
    edited |= ImGui::SliderFloat("Sun altitude",
        &atmosphere.sunElevationDegrees, 0.0f, 90.0f, "%.1f deg");
    edited |= ImGui::SliderFloat("Sun intensity",
        &light.sunIntensity, 0.0f, 8.0f, "%.2f");
    edited |= ImGui::ColorEdit3("Sun color", &light.sunColor.x,
                                ImGuiColorEditFlags_Float);
    edited |= ImGui::SliderFloat("Forward scattering",
        &light.forwardScatteringG, 0.0f, 0.95f, "%.3f");
    edited |= ImGui::SliderFloat("Phase intensity",
        &light.phaseIntensity, 0.0f, 1.0f, "%.3f");
    edited |= ImGui::SliderFloat("Edge influence",
        &light.edgeInfluence, 0.0f, 1.0f, "%.3f");
    edited |= ImGui::SliderFloat("Shadow exponent",
        &light.shadowExponent, 0.5f, 4.0f, "%.3f");
    edited |= ImGui::SliderFloat("Edge optical depth scale",
        &light.edgeOpticalDepthScale, 0.25f, 8.0f, "%.3f");

    ImGui::SeparatorText("Cloud Rim");
    edited |= ImGui::SliderFloat("Rim intensity", &light.rimIntensity, 0.0f, 4.0f, "%.2f");
    edited |= ImGui::SliderFloat("Rim depth scale", &light.rimDepthScale, 0.5f, 2.0f, "%.2f");
    ImGui::TextDisabled("Rim phase cap: %.1f%s", m_rimComparisonCap,
        m_rimComparisonCap == 2.5f ? "" : " (comparison)");
    ImGui::TextWrapped("Rim intensity 0 removes the positive phase contribution only. Larger depth scale concentrates light in sun-exposed samples.");
    ImGui::SeparatorText("Environment");
    edited |= ImGui::SliderFloat("Sky fill",
        &environment.physicalSkyFillScale, 0.0f, 2.0f, "%.4f");
    edited |= ImGui::SliderFloat("Ground fill",
        &environment.physicalGroundFillScale, 0.0f, 2.0f, "%.4f");
    edited |= ImGui::SliderFloat("Interior scattering",
        &environment.multipleScatteringInteriorBlend, 0.0f, 1.0f, "%.3f");
    edited |= ImGui::SliderFloat("Multiple attenuation",
        &environment.multipleScatteringAttenuation, 0.0f, 1.0f, "%.4f");
    if (ImGui::Button("Reset approved Urban lighting"))
    {
        // 비교 대상 여섯 명암 항목과 두 림 배율을 복원한다. 형상/태양 각도/노출은 유지한다.
        const auto base = stage15::ResolveSceneConcept(Stage15ConceptPreset::UrbanFairWeather);
        light.shadowExponent = base.light.shadowExponent;
        light.edgeOpticalDepthScale = base.light.edgeOpticalDepthScale;
        light.phaseIntensity = base.light.phaseIntensity;
        light.rimIntensity = base.light.rimIntensity;
        light.rimDepthScale = base.light.rimDepthScale;
        environment.physicalSkyFillScale = base.environment.physicalSkyFillScale;
        environment.physicalGroundFillScale = base.environment.physicalGroundFillScale;
        environment.multipleScatteringAttenuation = base.environment.multipleScatteringAttenuation;
        edited = true;
    }
    ImGui::TextWrapped("Approved Urban: Shadow 1.35; Sky/Ground .85; Multiple .15; Phase .20; Edge 2.");

    ImGui::SeparatorText("Atmosphere");
    edited |= ImGui::SliderFloat("Turbidity",
        &atmosphere.turbidity, 0.25f, 4.0f, "%.3f");
    edited |= ImGui::SliderFloat("Mie scale height (km)",
        &atmosphere.mieScaleHeightKm, 0.5f, 4.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(
        "Higher values retain more haze at cloud altitude. Affects clouds, sky, ground and the sun halo.");
    edited |= ImGui::SliderFloat("Mie anisotropy (g)",
        &atmosphere.mieG, 0.0f, 0.95f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(
        "Lower values reduce the concentration of atmospheric light around the sun. Does not change air extinction or cloud phase.");
    edited |= ImGui::SliderFloat("Rayleigh scale",
        &atmosphere.rayleighScale, 0.25f, 4.0f, "%.3f");
    edited |= ImGui::SliderFloat("Ozone scale",
        &atmosphere.ozoneScale, 0.0f, 4.0f, "%.3f");

    ImGui::SeparatorText("Ground and Deep Cache");
    edited |= ImGui::ColorEdit3("Ground albedo", &ground.albedo.x,
                                ImGuiColorEditFlags_Float);
    edited |= ImGui::SliderFloat("Ground bounce",
        &ground.bounceMultiplier, 0.0f, 2.0f, "%.3f");
    edited |= ImGui::SliderFloat("Surface shadow",
        &shadow.surfaceShadowStrength, 0.0f, 1.0f, "%.3f");
    edited |= ImGui::SliderFloat("Ambient floor",
        &shadow.surfaceAmbientFloor, 0.0f, 1.0f, "%.3f");
    ImGui::Text("Deep Cache: 512, %u + %u height slices%s",
                shadow.nearSliceCount, shadow.farSliceCount,
                shadow.nearSliceCount > 80 ? " (05-B comparison)" : "");
    ImGui::TextDisabled("Sun shadow transition: 3-5 deg");

    ImGui::SeparatorText("Tone Mapping");
    edited |= ImGui::SliderFloat("Exposure EV", &tone.exposureEv,
                                 -8.0f, 8.0f, "%.2f");
    edited |= ImGui::SliderFloat("White balance",
        &tone.whiteBalanceKelvin, 3500.0f, 10000.0f, "%.0f K");
    if (edited)
    {
        light.directionToSun = stage6light::DirectionFromAngles(
            atmosphere.sunAzimuthDegrees, atmosphere.sunElevationDegrees);
        light = stage6light::Sanitize(light);
        environment = stage8environment::Sanitize(environment);
        atmosphere = stage14atmosphere::Sanitize(atmosphere);
        ground = stage14ground::Sanitize(ground);
        tone = stage14tone::Sanitize(tone);
        shadow = stage12shadow::Sanitize(shadow);
        sunPreset = Stage6SunPreset::Custom;
        phasePreset = Stage7PhasePreset::Custom;
        environmentPreset = Stage8EnvironmentPreset::Custom;
    }
    ImGui::End();
}

void NoiseLab::DrawDiagnosticsPanel(
    Camera& camera,
    CloudParameters& cloud,
    AtmosphereParameters& atmosphere,
    Stage15ConceptPreset concept,
    float& cameraMoveSpeedMetersPerSecond,
    const std::array<ID3D11ShaderResourceView*, 6>& atmosphereLutSrvs,
    std::uint64_t shaderGeneration,
    const std::string& shaderStatus,
    const std::string& shaderError,
    const shaderreload::ReloadReport& reloadReport)
{
    ImGui::SetNextWindowSize(ImVec2(Ui(540.0f), Ui(760.0f)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("F4 Lighting & Environment", &m_panelVisible[3]))
    {
        ImGui::End();
        return;
    }
    const Stage15ConceptPreset concepts[] = {
        Stage15ConceptPreset::AutumnMorning,
        Stage15ConceptPreset::BeachSunset,
        Stage15ConceptPreset::BrightNoon,
        Stage15ConceptPreset::PastelDream,
    };
    for (std::size_t index = 0; index < std::size(concepts); ++index)
    {
        if (index > 0)
            ImGui::SameLine();
        if (ImGui::Button(stage15::ConceptName(concepts[index])))
            m_sceneConceptRequest = static_cast<int>(concepts[index]);
        const char* descriptions[] = {"Autumn morning", "Beach sunset", "Bright noon", "Pink / lavender dream"};
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", descriptions[index]);
    }
    ImGui::Text("Lighting preset: %s (%s)", stage15::ConceptName(concept),
        CloudFormationPresetSourceName(m_lightingSource));
    ImGui::TextWrapped("%s", LightingPresetPath(m_presetRoot, concept).generic_string().c_str());
    ImGui::TextWrapped("%s", m_lightingStatus.c_str());
    ImGui::TextWrapped("Selecting a slot reloads it and discards unsaved edits.");
    if (ImGui::Button("Save Preset")) m_saveLightingPending = true;

    ImGui::SeparatorText("Cloud and Cache Diagnostics");
    int currentCloudMode = cloud.debugMode;
    if (ImGui::BeginCombo("Cloud view", DebugName(
            static_cast<CloudDebugMode>(currentCloudMode))))
    {
        for (CloudDebugMode mode : kCloudDiagnosticModes)
        {
            const bool selected = currentCloudMode == static_cast<int>(mode);
            ImGui::PushID(static_cast<int>(mode));
            if (ImGui::Selectable(DebugName(mode), selected))
                cloud.debugMode = static_cast<int>(mode);
            ImGui::PopID();
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    const auto selectedCloudMode = static_cast<CloudDebugMode>(cloud.debugMode);
    if(cloud.debugMode>=93 && cloud.debugMode<=95)
        ImGui::TextWrapped("Diagnostic only: fixed 100m samples, no early exit/empty skip; rho > 0.001. All occupied segments summed, including hidden clouds. Cyan=no occupied cloud. Length/distance: black=0, white=10km or more; mean density: black=0, white=1 or more. Physical density before far fade. Tone bypassed; not a near/far classification.");
    if (selectedCloudMode == CloudDebugMode::CloudWithoutAerial)
        ImGui::TextWrapped("Cloud air attenuation/scattering removed; cloud lighting, sky, ground air and tone remain. Compare with Composite at the same camera.");
    else if (selectedCloudMode == CloudDebugMode::AirTransmittanceAtCloud ||
             selectedCloudMode == CloudDebugMode::AirRadianceAtCloud)
        ImGui::TextWrapped("Cloud air lookup at 2x opacity-weighted depth (same as Composite). Cloud Depth still shows actual distance. Air T: white=clear, dark=attenuated. Air L: added air light (F4 Debug exposure/channel). Gray=no cloud opacity. Exposure EV/white balance/ACES bypassed.");
    if (selectedCloudMode == CloudDebugMode::LightTransmittance)
        ImGui::TextWrapped("Key 9 samples ONE segment midpoint, which may be empty. Use Visible Sun T for the clouds you can see.");
    else if (selectedCloudMode == CloudDebugMode::VisibleSunTransmittance)
        ImGui::TextWrapped("Sun T weighted by Tview * (1 - Tstep). Bright: sunlight passes through. Dark: blocked OR no cloud contribution; compare View Transmittance.");
    else if (selectedCloudMode == CloudDebugMode::AmbientVisibility)
        ImGui::TextWrapped("Ground bounce visibility: local AO times sun coupling. Sky uses local AO only; this is not sky visibility. Dark also means no cloud contribution.");
    else if (selectedCloudMode == CloudDebugMode::ShapedSunVisibility)
        ImGui::TextWrapped("Visibility after lighting shaping, weighted by scattering interaction. Bright: visible; dark: occluded OR no contribution.");
    else if (selectedCloudMode == CloudDebugMode::AccumulatedDirectLighting ||
             selectedCloudMode == CloudDebugMode::AccumulatedSkyAmbient ||
             selectedCloudMode == CloudDebugMode::AccumulatedGroundBounce ||
             selectedCloudMode == CloudDebugMode::AccumulatedMultipleScattering ||
             selectedCloudMode == CloudDebugMode::SilverLiningContribution)
        ImGui::TextWrapped("Integrated cloud light only: L / (1 + L), then current Tone settings. Silver Lining is already part of Direct. Displayed colors cannot be added numerically.");

    const Stage14DebugView atmosphereViews[] = {
        Stage14DebugView::None,
        Stage14DebugView::Transmittance,
        Stage14DebugView::MultiScattering,
        Stage14DebugView::SkyView,
        Stage14DebugView::SkyIrradiance,
        Stage14DebugView::AerialRadiance,
        Stage14DebugView::AerialTransmittance,
        Stage14DebugView::HdrPreTone,
    };
    if (ImGui::BeginCombo("Atmosphere view",
                          AtmosphereDebugName(atmosphere.debugView)))
    {
        for (Stage14DebugView view : atmosphereViews)
        {
            const bool selected = atmosphere.debugView == view;
            if (ImGui::Selectable(AtmosphereDebugName(view), selected))
                atmosphere.debugView = view;
        }
        ImGui::EndCombo();
    }
    if (ImGui::TreeNode("LUT previews"))
    {
        static constexpr const char* names[] = {
            "Transmittance", "Multi scattering", "Sky view",
            "Sky irradiance", "Aerial radiance", "Aerial transmittance"
        };
        for (std::size_t index = 0; index < atmosphereLutSrvs.size(); ++index)
        {
            ImGui::TextUnformatted(names[index]);
            if (atmosphereLutSrvs[index])
            {
                ImGui::Image(ImTextureRef(static_cast<ImTextureID>(
                    reinterpret_cast<std::uintptr_t>(
                        atmosphereLutSrvs[index]))),
                    ImVec2(Ui(180.0f), Ui(90.0f)));
            }
        }
        ImGui::TreePop();
    }

    ImGui::SeparatorText("Camera and Runtime");
    ImGui::Text("Camera: %.0f %.0f %.0f m",
                camera.GetPosition().x, camera.GetPosition().y,
                camera.GetPosition().z);
    ImGui::SliderFloat("Move speed", &cameraMoveSpeedMetersPerSecond,
                       1.0f, 5000.0f, "%.0f m/s",
                       ImGuiSliderFlags_Logarithmic);
    float zoom = m_developerUiSettings.userZoom;
    if (ImGui::SliderFloat("UI zoom", &zoom,
            developerui::kMinimumUserZoom,
            developerui::kMaximumUserZoom, "%.2fx"))
    {
        if (SetUserZoom(zoom))
            developerui::SaveAtomic(
                m_developerUiSettingsPath, m_developerUiSettings,
                m_developerUiSettingsStatus);
    }
    ImGui::TextWrapped("%s", m_developerUiSettingsStatus.c_str());

    ImGui::SeparatorText("Developer Runtime");
    ImGui::Text("Shader generation: %llu",
                static_cast<unsigned long long>(shaderGeneration));
    ImGui::TextWrapped("%s", shaderStatus.c_str());
    if (reloadReport.attempted)
    {
        ImGui::Text("Last reload: %s | programs %zu | compile %llu | cache %llu | %.2f ms",
            reloadReport.succeeded ? "success" : "failed",
            reloadReport.affectedPrograms,
            static_cast<unsigned long long>(reloadReport.compileCount),
            static_cast<unsigned long long>(reloadReport.cacheHits),
            reloadReport.elapsedMilliseconds);
        for (const std::string& changedFile : reloadReport.changedFiles)
            ImGui::BulletText("%s", changedFile.c_str());
    }
    if (!shaderError.empty())
        ImGui::TextWrapped("Error: %s", shaderError.c_str());
    if (ImGui::Button("Export schema 42 snapshot"))
        m_exportPending = true;
    if (!m_exportStatus.empty())
        ImGui::TextWrapped("%s", m_exportStatus.c_str());
    ImGui::End();
}

bool NoiseLab::ValidateUiContracts() const
{
    for (std::size_t left = 0; left < std::size(kCloudDiagnosticModes); ++left)
    {
        if (std::strcmp(DebugName(kCloudDiagnosticModes[left]), "Unknown") == 0)
            return false;
        for (std::size_t right = left + 1;
             right < std::size(kCloudDiagnosticModes); ++right)
        {
            if (std::strcmp(DebugName(kCloudDiagnosticModes[left]),
                            DebugName(kCloudDiagnosticModes[right])) == 0)
                return false;
        }
    }
    const int maximumMode = static_cast<int>(NoiseOutputMode::LocalBaseOffset);
    for (int left = 0; left <= maximumMode; ++left)
    {
        for (int right = left + 1; right <= maximumMode; ++right)
        {
            if (std::strcmp(NoiseOutputName(
                    static_cast<NoiseOutputMode>(left)), NoiseOutputName(
                    static_cast<NoiseOutputMode>(right))) == 0)
                return false;
        }
    }
    return true;
}

void NoiseLab::DrawProfilerOverlay(
    const FrameTimingSnapshot& timing,
    double weatherGenerationMilliseconds,
    std::uint64_t weatherGeneration)
{
    ImGui::SetNextWindowPos(ImVec2(Ui(10.0f), Ui(10.0f)), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.86f);
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing;
    if (!ImGui::Begin("Performance##ProfilerOverlay", nullptr, flags))
    {
        ImGui::End();
        return;
    }
    ImGui::Text("Time      %7.2f s", m_effectiveTime);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Cloud simulation time in seconds; not frame duration.");
    if (timing.cpuValid)
    {
        ImGui::Text("FPS       %7.1f", timing.fps);
        ImGui::Text("CPU frame %7.3f ms", timing.cpuFrameMs);
    }
    else
    {
        ImGui::TextUnformatted("CPU timing warming up");
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Smoothed CPU render duration, including Present / VSync wait.\nFPS = 1000 / CPU frame milliseconds.");
    if (timing.gpuValid)
    {
        ImGui::Text("GPU frame %7.3f ms", timing.gpuFrameMs);
        ImGui::Separator();
        ImGui::TextDisabled("GPU pass times (smoothed)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Exponential moving average: 90%% previous average + 10%% latest sample.\nGPU frame uses the same smoothing. Results arrive asynchronously.");
        ImGui::Text("Weather Map   %7.3f ms", timing.gpuWeatherMapMs);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Latest GPU interval: %.3f ms (before smoothing).\nAn unchanged Weather Map is reused; its frame interval can round to zero.",
                              timing.rawGpuWeatherMapMs);
        ImGui::Text("Atmosphere LUT %7.3f ms", timing.gpuAtmosphereLutMs);
        ImGui::Text("Shadow     %7.3f ms", timing.gpuShadowCacheMs);
        ImGui::Text("Opaque     %7.3f ms", timing.gpuOpaqueSceneMs);
        ImGui::Text("Cloud      %7.3f ms", timing.gpuCloudMs);
        ImGui::Text("Tone       %7.3f ms", timing.gpuToneMapMs);
    }
    else
    {
        ImGui::TextUnformatted("GPU timing warming up");
    }
    ImGui::Separator();
    ImGui::TextDisabled("Last Weather rebuild (CPU elapsed)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Wall-clock time from compute submission through GPU readback.\nIncludes staging creation, GPU queue wait and CPU copy; excludes hashing.\nThis is not the Weather shader's GPU execution time or a per-frame cost.");
    if (weatherGeneration > 0u)
    {
        ImGui::Text("Elapsed       %7.3f ms", weatherGenerationMilliseconds);
        ImGui::Text("Rebuild count %llu",
                    static_cast<unsigned long long>(weatherGeneration));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Successful Weather generations in this renderer session.\nA generated map with unchanged content still counts; a skipped generator key does not.");
    }
    else
        ImGui::TextUnformatted("No completed rebuild");
    ImGui::End();
}

void NoiseLab::DrawSlice(
    const char* label, NoiseSliceAxis axis, SliceTarget& target)
{
    ImGui::BeginGroup();
    ImGui::TextUnformatted(label);
    const ImVec2 size(Ui(145.0f), Ui(145.0f));
    ImGui::Image(ImTextureRef(static_cast<ImTextureID>(
        reinterpret_cast<std::uintptr_t>(target.srv.Get()))), size);
    if (ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        const ImVec2 minimum = ImGui::GetItemRectMin();
        const ImVec2 mouse = ImGui::GetMousePos();
        const float u = std::clamp((mouse.x - minimum.x) / size.x, 0.0f, 1.0f);
        const float v = 1.0f - std::clamp(
            (mouse.y - minimum.y) / size.y, 0.0f, 1.0f);
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

void NoiseLab::RenderPreviews(
    ID3D11VertexShader* fullscreenVs,
    ID3D11PixelShader* noiseLabPs,
    ID3D11Buffer* cloudCb,
    ID3D11Buffer* noiseVolumeCb,
    ID3D11Buffer* cloudShapeCb,
    ID3D11Buffer* weatherColumnCb,
    ID3D11ShaderResourceView* weatherMapSrv,
    ID3D11ShaderResourceView* baseNoiseVolumeSrv,
    ID3D11ShaderResourceView* detailNoiseVolumeSrv,
    ID3D11SamplerState* weatherSampler,
    ID3D11ShaderResourceView* nearMicroVolumeSrv)
{
    if (!m_initialized || !fullscreenVs || !noiseLabPs ||
        std::none_of(m_panelVisible.begin(), m_panelVisible.end(),
            [](bool value) { return value; }))
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
    m_context->PSSetConstantBuffers(10, 1, &weatherColumnCb);
    ID3D11ShaderResourceView* resources[] = {
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
        ID3D11RenderTargetView* target = m_targets[axis].rtv.Get();
        const float clear[] = { 0.0f, 0.0f, 0.0f, 1.0f };
        m_context->OMSetRenderTargets(1, &target, nullptr);
        m_context->ClearRenderTargetView(target, clear);
        m_context->Draw(3, 0);
    }
    // F2의 Weather 원본 UV 전체를 불투명하게 표시한다. A는 두께 데이터다.
    if (m_panelVisible[1] && weatherMapSrv) {
        NoiseLabParameters weatherParameters = m_parameters;
        for (std::uint32_t channel = 0; channel < m_weatherTargets.size(); ++channel) {
            const unsigned modes[] = {100u,101u,103u,104u};
            weatherParameters.outputMode = modes[channel];
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(m_context->Map(m_noiseLabCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) continue;
            std::memcpy(mapped.pData, &weatherParameters, sizeof(weatherParameters));
            m_context->Unmap(m_noiseLabCb.Get(), 0);
            ID3D11Buffer* labCb = m_noiseLabCb.Get();
            m_context->PSSetConstantBuffers(2, 1, &labCb);
            ID3D11RenderTargetView* target = m_weatherTargets[channel].rtv.Get();
            m_context->OMSetRenderTargets(1, &target, nullptr);
            m_context->Draw(3, 0);
        }
    }
    // [시험 전용 E19] 105=Detail, 106=미세 텍스처 원본, 107=실제 적용 섭동. t14=미세 64³.
    if (m_panelVisible[1]) {
        m_context->PSSetShaderResources(14, 1, &nearMicroVolumeSrv);
        NoiseLabParameters microParameters = m_parameters;
        for (std::uint32_t i = 0; i < m_microTargets.size(); ++i) {
            microParameters.outputMode = 105u + i;
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(m_context->Map(m_noiseLabCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) continue;
            std::memcpy(mapped.pData, &microParameters, sizeof(microParameters));
            m_context->Unmap(m_noiseLabCb.Get(), 0);
            ID3D11Buffer* labCb = m_noiseLabCb.Get();
            m_context->PSSetConstantBuffers(2, 1, &labCb);
            ID3D11RenderTargetView* target = m_microTargets[i].rtv.Get();
            m_context->OMSetRenderTargets(1, &target, nullptr);
            m_context->Draw(3, 0);
        }
        ID3D11ShaderResourceView* nullMicro = nullptr;
        m_context->PSSetShaderResources(14, 1, &nullMicro);
    }
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    ID3D11ShaderResourceView* nullResources[3] = {};
    m_context->PSSetShaderResources(2, 3, nullResources);
}

void NoiseLab::EndFrame(ID3D11RenderTargetView* backBufferRtv)
{
    if (!m_initialized)
        return;
    m_context->OMSetRenderTargets(1, &backBufferRtv, nullptr);
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

bool NoiseLab::ConsumeFormationEdited()
{
    const bool result = m_formationEdited;
    m_formationEdited = false;
    return result;
}

bool NoiseLab::ConsumeFormationPresetRequest(
    CloudFormationPresetTarget& target)
{
    if (!m_formationPresetPending)
        return false;
    target = m_formationPresetRequest;
    m_formationPresetPending = false;
    return true;
}

bool NoiseLab::ConsumeSaveCustomRequest()
{
    const bool result = m_saveCustomPending;
    m_saveCustomPending = false;
    return result;
}

bool NoiseLab::ConsumeLoadCustomRequest()
{
    const bool result = m_loadCustomPending;
    m_loadCustomPending = false;
    return result;
}

bool NoiseLab::ConsumeSceneConceptRequest(Stage15ConceptPreset& concept)
{
    if (m_sceneConceptRequest < 0)
        return false;
    concept = static_cast<Stage15ConceptPreset>(m_sceneConceptRequest);
    m_sceneConceptRequest = -1;
    return true;
}

bool NoiseLab::ConsumeNoiseVolumeRegenerateRequest()
{
    const bool result = m_noiseVolumeRegeneratePending;
    m_noiseVolumeRegeneratePending = false;
    return result;
}

bool NoiseLab::ConsumeExportRequest()
{
    const bool result = m_exportPending;
    m_exportPending = false;
    return result;
}

bool NoiseLab::ValidatePreviewData()
{
    std::uint8_t minimum = 255u;
    std::uint8_t maximum = 0u;
    for (SliceTarget& target : m_targets)
    {
        m_context->CopyResource(target.staging.Get(), target.texture.Get());
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(m_context->Map(
                target.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            return false;
        for (UINT y = 0; y < kPreviewSize; ++y)
        {
            const auto* row = static_cast<const std::uint8_t*>(mapped.pData) +
                static_cast<std::size_t>(y) * mapped.RowPitch;
            for (UINT x = 0; x < kPreviewSize; ++x)
            {
                minimum = std::min(minimum, row[x * 4u]);
                maximum = std::max(maximum, row[x * 4u]);
            }
        }
        m_context->Unmap(target.staging.Get(), 0);
    }
    return maximum > minimum;
}

std::uint64_t NoiseLab::PreviewHash(std::size_t targetIndex)
{
    if (targetIndex >= m_targets.size())
        return 0u;
    SliceTarget& target = m_targets[targetIndex];
    m_context->CopyResource(target.staging.Get(), target.texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(m_context->Map(
            target.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return 0u;
    std::uint64_t hash = fnv1a64::kOffsetBasis;
    for (UINT y = 0; y < kPreviewSize; ++y)
    {
        const auto* row = static_cast<const std::uint8_t*>(mapped.pData) +
            static_cast<std::size_t>(y) * mapped.RowPitch;
        fnv1a64::Append(hash, row, kPreviewSize * 4u);
    }
    m_context->Unmap(target.staging.Get(), 0);
    return hash;
}

bool NoiseLab::WriteMetadata(
    const std::filesystem::path& path,
    const CloudFormationSettings& formation,
    const CloudMotionParameters& motion,
    const Stage12ShadowParameters& shadow,
    const LightParameters& light,
    const EnvironmentParameters& environment,
    const AtmosphereParameters& atmosphere,
    const GroundLightingParameters& ground,
    const ToneMappingParameters& tone,
    Stage15ConceptPreset concept,
    const NoiseVolumeParameters& noiseVolume,
    std::uint64_t baseNoiseVolumeHash,
    std::uint64_t detailNoiseVolumeHash,
    std::uint64_t weatherMapHash,
    std::uint64_t shaderGeneration) const
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        return false;
    output << std::setprecision(9)
        << "{\n"
        << "  \"schemaVersion\": 44,\n"
        << "  \"renderPath\": \"HighFullResolutionDirect\",\n"
        << "  \"sceneConcept\": \"" << stage15::ConceptName(concept)
        << "\",\n"
        << "  \"formationSlot\": \"" << CloudFormationPresetTargetName(m_snapshotFormationTarget) << "\",\n"
        << "  \"formationSource\": \"" << CloudFormationPresetSourceName(m_snapshotFormationSource) << "\",\n"
        << "  \"lightingSlot\": " << LightingPresetSlot(concept) << ",\n"
        << "  \"lightingSource\": \"" << CloudFormationPresetSourceName(m_lightingSource) << "\",\n"
        << "  \"formation\": {\n"
        << "    \"coverage\": " << formation.coverage << ",\n"
        << "    \"densityMultiplier\": " << formation.densityMultiplier
        << ",\n"
        << "    \"extinctionPerMeter\": " << formation.extinctionPerMeter
        << ",\n"
        << "    \"detailErosion\": " << formation.detailErosion << ",\n"
        << "    \"weatherPreset\": "
        << static_cast<std::uint32_t>(formation.weatherPreset) << ",\n"
        << "    \"cloudTypeSelection\": "
        << static_cast<std::uint32_t>(formation.typeSelection.mode)
        << ",\n"
        << "    \"weather\": {\n"
        << "      \"coverageThreshold\": "
        << formation.weather.generator.coverageThreshold << ",\n"
        << "      \"coverageSoftness\": "
        << formation.weather.generator.coverageSoftness << ",\n"
        << "      \"densityCoverageInfluence\": "
        << formation.weather.generator.densityCoverageInfluence << ",\n"
        << "      \"thicknessCoverageInfluence\": "
        << formation.weather.generator.thicknessCoverageInfluence << ",\n"
        << "      \"coverageSeed\": "
        << formation.weather.generator.coverage.seed << ",\n"
        << "      \"densitySeed\": "
        << formation.weather.generator.density.seed << ",\n"
        << "      \"thicknessSeed\": "
        << formation.weather.generator.localThickness.seed << "\n"
        << "    },\n"
        << "    \"shape\": {\n"
        << "      \"thicknessMeters\": [" << formation.weather.column.minimumThicknessMeters << ", "
        << formation.weather.column.maximumThicknessMeters << "],\n"
        << "      \"verticalProfile\": [" << formation.shape.bottomFadeEnd << ", "
        << formation.shape.topFadeStart << ", " << formation.shape.lowerDensityScale << ", "
        << formation.shape.upperTransitionStart << ", " << formation.shape.upperTransitionEnd << "],\n"
        << "      \"localBaseLiftMaxMeters\": "
        << formation.weather.column.maximumBaseLiftMeters << ",\n"
        << "      \"footprintCoverageInfluence\": "
        << formation.shape.footprintCoverageInfluence << ",\n"
        << "      \"densityShaping\": " << formation.shape.densityShaping << "\n"
        << "    },\n"
        << "    \"domainBottomMeters\": " << formation.domainBottomMeters
        << ",\n"
        << "    \"domainThicknessMeters\": "
        << formation.domainThicknessMeters << ",\n"
        << "    \"maximumViewTraceDistanceMeters\": "
        << formation.maximumViewTraceDistanceMeters << ",\n"
        << "    \"viewTraceFadeStartDistanceMeters\": "
        << formation.viewTraceFadeStartDistanceMeters << ",\n"
        << "    \"maximumLightTraceDistanceMeters\": "
        << formation.maximumLightTraceDistanceMeters << ",\n"
        << "    \"weatherWorldSizeMeters\": "
        << formation.weather.worldSizeMeters << ",\n"
        << "    \"baseNoiseWorldSizeMeters\": "
        << formation.baseNoiseWorldSizeMeters << ",\n"
        << "    \"baseNoiseVerticalWorldSizeMeters\": "
        << formation.baseNoiseVerticalWorldSizeMeters << ",\n"
        << "    \"detailNoiseWorldSizeMeters\": "
        << formation.detailNoiseWorldSizeMeters << "\n"
        << "  },\n"
        << "  \"runtimeMotion\": [" << motion.direction.x << ", "
        << motion.direction.y << ", " << motion.direction.z << ", "
        << motion.speedMetersPerSecond << "],\n"
        << "  \"deepCache\": {\"resolution\": 512, \"nearSlices\": "
        << shadow.nearSliceCount << ", \"farSlices\": "
        << shadow.farSliceCount << ", \"projection\": \"layer-projected-up\", \"maxIntegrationStepMeters\": 250, \"sunTransitionDegrees\": [3,5], \"cloudTransitionBlend\": \"optical-depth\"},\n"
        << "  \"lighting\": {\"sunIntensity\": " << light.sunIntensity
        << ", \"forwardScatteringG\": " << light.forwardScatteringG
        << ", \"shadowExponent\": " << light.shadowExponent
        << ", \"edgeOpticalDepthScale\": " << light.edgeOpticalDepthScale
        << ", \"phaseIntensity\": " << light.phaseIntensity
        << ", \"phaseCap\": 2.5"
        << ", \"rimIntensity\": " << light.rimIntensity
        << ", \"rimDepthScale\": " << light.rimDepthScale
        << ", \"rimPhaseCap\": " << m_rimComparisonCap
        << ", \"temporaryBaseCandidate\": " << m_baseCandidate
        << ", \"multipleAttenuation\": " << environment.multipleScatteringAttenuation
        << ", \"skyFill\": " << environment.physicalSkyFillScale
        << ", \"groundFill\": " << environment.physicalGroundFillScale
        << "},\n"
        << "  \"atmosphere\": {\"preset\": "
        << static_cast<std::uint32_t>(atmosphere.preset)
        << ", \"sunAzimuthDegrees\": " << atmosphere.sunAzimuthDegrees
        << ", \"sunElevationDegrees\": " << atmosphere.sunElevationDegrees
        << ", \"timeOfDayHours\": " << atmosphere.timeOfDayHours << "},\n"
        << "  \"ground\": {\"albedo\": [" << ground.albedo.x << ", "
        << ground.albedo.y << ", " << ground.albedo.z
        << "], \"bounceMultiplier\": " << ground.bounceMultiplier << "},\n"
        << "  \"tone\": {\"mode\": "
        << static_cast<std::uint32_t>(tone.mode)
        << ", \"exposureEv\": " << tone.exposureEv
        << ", \"whiteBalanceKelvin\": " << tone.whiteBalanceKelvin << "},\n"
        << "  \"noiseVolumes\": {\"source\": \"Texture3D\""
        << ", \"baseResolution\": " << noiseVolume.baseResolution
        << ", \"detailResolution\": " << noiseVolume.detailResolution
        << ", \"seed\": " << noiseVolume.seed
        << ", \"baseFrequencies\": ["
        << noiseVolume.baseFrequencies.x << ", "
        << noiseVolume.baseFrequencies.y << ", "
        << noiseVolume.baseFrequencies.z << ", "
        << noiseVolume.baseFrequencies.w << "]"
        << ", \"detailFrequencies\": ["
        << noiseVolume.detailFrequencies.x << ", "
        << noiseVolume.detailFrequencies.y << ", "
        << noiseVolume.detailFrequencies.z << ", "
        << noiseVolume.detailFrequencies.w << "]"
        << ", \"baseWeights\": ["
        << noiseVolume.baseWeights.x << ", " << noiseVolume.baseWeights.y
        << ", " << noiseVolume.baseWeights.z << ", "
        << noiseVolume.baseWeights.w << "]"
        << ", \"detailWeights\": ["
        << noiseVolume.detailWeights.x << ", "
        << noiseVolume.detailWeights.y << ", "
        << noiseVolume.detailWeights.z << ", "
        << noiseVolume.detailWeights.w << "]"
        << ", \"baseMidOctaveExtra\": " << noiseVolume.baseMidOctaveExtra
        << ", \"baseHash\": " << baseNoiseVolumeHash
        << ", \"detailHash\": " << detailNoiseVolumeHash
        << ", \"weatherHash\": " << weatherMapHash << "},\n"
        << "  \"shaderGeneration\": " << shaderGeneration << "\n"
        << "}\n";
    return static_cast<bool>(output);
}

bool NoiseLab::ExportSnapshot(
    const std::filesystem::path& root,
    const CloudFormationSettings& formation,
    const CloudMotionParameters& motion,
    const Stage12ShadowParameters& shadow,
    const LightParameters& light,
    const EnvironmentParameters& environment,
    const AtmosphereParameters& atmosphere,
    const GroundLightingParameters& ground,
    const ToneMappingParameters& tone,
    Stage15ConceptPreset concept,
    const NoiseVolumeParameters& noiseVolume,
    std::uint64_t baseNoiseVolumeHash,
    std::uint64_t detailNoiseVolumeHash,
    std::uint64_t weatherMapHash,
    std::uint64_t shaderGeneration)
{
    const std::filesystem::path directory = TimestampDirectory(root);
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error || !WriteMetadata(
            directory / L"noise-settings.json", formation, motion, shadow,
            light, environment, atmosphere, ground, tone, concept,
            noiseVolume, baseNoiseVolumeHash, detailNoiseVolumeHash,
            weatherMapHash, shaderGeneration))
    {
        m_exportStatus = "Snapshot export failed";
        return false;
    }
    m_exportStatus = "Schema 44 snapshot exported";
    return true;
}
