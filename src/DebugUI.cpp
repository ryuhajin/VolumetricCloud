#include "DebugUI.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
constexpr size_t kParameterCategoryCount = 6;

struct ParameterCategorySettings
{
    std::array<bool, kParameterCategoryCount> open = { true, false, false, false, false, false };
};

ParameterCategorySettings g_parameterCategories;

void* ParameterSettingsReadOpen(
    ImGuiContext*, ImGuiSettingsHandler* handler, const char* name)
{
    return std::strcmp(name, "Parameters") == 0 ? handler->UserData : nullptr;
}

void ParameterSettingsReadLine(
    ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line)
{
    auto* settings = static_cast<ParameterCategorySettings*>(entry);
    static const char* keys[kParameterCategoryCount] = {
        "ShapeNoise", "Animation", "Lighting", "Sampling", "WideCloudLayer", "Presets"
    };
    for (size_t i = 0; i < kParameterCategoryCount; ++i)
    {
        const std::string prefix = std::string(keys[i]) + "=";
        if (std::strncmp(line, prefix.c_str(), prefix.size()) == 0)
        {
            settings->open[i] = std::atoi(line + prefix.size()) != 0;
            break;
        }
    }
}

void ParameterSettingsWriteAll(
    ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* out)
{
    const auto* settings = static_cast<const ParameterCategorySettings*>(handler->UserData);
    static const char* keys[kParameterCategoryCount] = {
        "ShapeNoise", "Animation", "Lighting", "Sampling", "WideCloudLayer", "Presets"
    };
    out->appendf("[CloudDebug][Parameters]\n");
    for (size_t i = 0; i < kParameterCategoryCount; ++i)
        out->appendf("%s=%d\n", keys[i], settings->open[i] ? 1 : 0);
    out->append("\n");
}

bool BeginParameterCategory(const char* label, size_t index)
{
    ImGui::SetNextItemOpen(g_parameterCategories.open[index], ImGuiCond_Always);
    const bool open = ImGui::CollapsingHeader(label);
    if (open != g_parameterCategories.open[index])
    {
        g_parameterCategories.open[index] = open;
        ImGui::MarkIniSettingsDirty();
    }
    return open;
}
}

bool DebugUI::Init(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiSettingsHandler categoryHandler;
    categoryHandler.TypeName = "CloudDebug";
    categoryHandler.TypeHash = ImHashStr("CloudDebug");
    categoryHandler.ReadOpenFn = ParameterSettingsReadOpen;
    categoryHandler.ReadLineFn = ParameterSettingsReadLine;
    categoryHandler.WriteAllFn = ParameterSettingsWriteAll;
    categoryHandler.UserData = &g_parameterCategories;
    ImGui::AddSettingsHandler(&categoryHandler);
    ImGui::StyleColorsDark();
    if (!ImGui_ImplWin32_Init(hwnd) || !ImGui_ImplDX11_Init(device, context))
        return false;

    m_initialized = true;
    LoadPresets();
    return true;
}

void DebugUI::Shutdown()
{
    if (!m_initialized) return;
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    m_initialized = false;
}

bool DebugUI::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return m_initialized && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam) != 0;
}

bool DebugUI::WantsMouseCapture() const
{
    return m_initialized && ImGui::GetIO().WantCaptureMouse;
}

bool DebugUI::WantsKeyboardCapture() const
{
    return m_initialized && ImGui::GetIO().WantCaptureKeyboard;
}

void DebugUI::ToggleVisible()
{
    m_visible = !m_visible;
}

void DebugUI::BeginFrame()
{
    if (!m_initialized) return;
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

static void DrawPreview(const char* label, ID3D11ShaderResourceView* srv)
{
    ImGui::TextUnformatted(label);
    if (srv)
        ImGui::Image(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(srv))), ImVec2(256, 256));
    else
        ImGui::Dummy(ImVec2(256, 256));
}

bool DebugUI::Draw(CloudParameters& p,
                   NoisePreviewSettings& preview,
                   const std::array<ID3D11ShaderResourceView*, 4>& previewSrvs,
                   ID3D11ShaderResourceView* weatherSrv,
                   bool previewDirty,
                   NoiseCacheUiActions& cacheActions)
{
    if (!m_initialized || !m_visible) return false;

    bool changed = false;
    bool presetSelected = false;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 maxWindowSize(
        (std::max)(360.0f, viewport->WorkSize.x),
        (std::max)(280.0f, viewport->WorkSize.y));
    ImGui::SetNextWindowSize(ImVec2(480, 560), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(360, 280), maxWindowSize);
    if (ImGui::Begin("Cloud Debug", &m_visible))
    {
        if (ImGui::BeginTabBar("CloudDebugTabs"))
        {
            if (ImGui::BeginTabItem("Render"))
            {
                const char* modes[] = {
                    "Lit Cloud", "Final Density", "Base Shape", "Detail Noise",
                    "Height Mask", "Transmittance", "Cache Difference", "Seam Difference",
                    "Base R: Perlin-Worley", "Base G: Worley Low",
                    "Base B: Worley Mid", "Base A: Worley High",
                    "Light Visibility", "Dual-Lobe Phase", "Ambient Lighting", "Direct Lighting",
                    "Weather Coverage", "Weather Cloud Type",
                    "Weather Base Height", "Weather Thickness"
                };
                changed |= ImGui::Combo("Render mode", &p.renderMode, modes, IM_ARRAYSIZE(modes));
                bool useCache = p.useTextureCache != 0;
                if (ImGui::Checkbox("Inspector uses 3D cache", &useCache)) { p.useTextureCache = useCache ? 1 : 0; changed = true; }
                bool showBounds = p.showBounds != 0;
                if (ImGui::Checkbox("Show layer bounds", &showBounds)) { p.showBounds = showBounds ? 1 : 0; changed = true; }
                changed |= ImGui::Checkbox("Freeze animation", &preview.freeze);
                if (preview.freeze)
                    changed |= ImGui::SliderFloat("Preview time", &preview.previewTime, 0.0f, 120.0f, "%.2f s");
                if (ImGui::Button("Reset defaults"))
                {
                    p = DefaultCloudParameters();
                    changed = presetSelected = true;
                    m_activePreset = "Default";
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Noise Inspector"))
            {
                const char* axes[] = { "XY", "XZ", "YZ" };
                changed |= ImGui::Combo("Slice axis", &preview.axis, axes, IM_ARRAYSIZE(axes));
                changed |= ImGui::SliderFloat("Slice", &preview.slice, 0.0f, 1.0f, "%.3f");
                ImGui::Text("Preview: %s", previewDirty ? "updating" : "ready");

                const int columns = ImGui::GetContentRegionAvail().x < 540.0f ? 1 : 2;
                if (ImGui::BeginTable("NoisePreviews", columns))
                {
                    ImGui::TableNextColumn(); DrawPreview("Base Perlin-Worley", previewSrvs[0]);
                    ImGui::TableNextColumn(); DrawPreview("Detail Worley", previewSrvs[1]);
                    ImGui::TableNextColumn(); DrawPreview("Height mask", previewSrvs[2]);
                    ImGui::TableNextColumn(); DrawPreview("Final density", previewSrvs[3]);
                    ImGui::EndTable();
                }
                DrawPreview("Weather map (RGBA)", weatherSrv);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Parameters"))
            {
                if (BeginParameterCategory("Shape & Noise", 0))
                {
                    changed |= ImGui::SliderFloat("Tile repeat", &p.noiseWorldScale, 1.0f, 4.0f, "%.0f");
                    p.noiseWorldScale = std::round(p.noiseWorldScale);
                    changed |= ImGui::SliderInt("Base period", &p.basePeriod, 2, 12);
                    changed |= ImGui::SliderInt("Base octaves", &p.baseOctaves, 1, 5);
                    changed |= ImGui::SliderInt("Detail period", &p.detailPeriod, 4, 32);
                    changed |= ImGui::SliderInt("Detail octaves", &p.detailOctaves, 1, 4);
                    changed |= ImGui::SliderFloat("Noise cutoff threshold", &p.noiseCutoffThreshold,
                                                  0.0f, 0.95f, "%.3f");
                    changed |= ImGui::SliderFloat("Coverage", &p.coverage, 0.05f, 0.95f, "%.3f");
                    changed |= ImGui::SliderFloat("Base erosion", &p.baseErosion, 0.0f, 0.75f, "%.3f");
                    changed |= ImGui::SliderFloat("Density", &p.densityMultiplier, 0.0f, 5.0f, "%.2f");
                    changed |= ImGui::SliderFloat("Erosion", &p.erosionStrength, 0.0f, 1.0f, "%.3f");
                    changed |= ImGui::SliderFloat("Bottom fade", &p.bottomFade, 0.01f, 0.49f, "%.3f");
                    changed |= ImGui::SliderFloat("Top fade", &p.topFade, 0.01f, 0.49f, "%.3f");
                    changed |= ImGui::InputFloat("Seed", &p.seed, 1.0f, 10.0f, "%.0f");
                }

                if (BeginParameterCategory("Animation", 1))
                {
                    changed |= ImGui::SliderFloat2("Wind direction", &p.windDirection.x, -1.0f, 1.0f, "%.2f");
                    changed |= ImGui::SliderFloat("Wind speed", &p.windSpeed, 0.0f, 0.25f, "%.3f");
                }

                if (BeginParameterCategory("Lighting", 2))
                {
                    changed |= ImGui::SliderFloat("Sun azimuth", &p.sunAzimuth, -180.0f, 180.0f, "%.1f deg");
                    changed |= ImGui::SliderFloat("Sun elevation", &p.sunElevation, -10.0f, 90.0f, "%.1f deg");
                    changed |= ImGui::SliderFloat("Sun intensity", &p.sunIntensity, 0.0f, 12.0f, "%.2f");
                    changed |= ImGui::SliderFloat("Ambient", &p.ambientIntensity, 0.0f, 2.0f, "%.2f");
                    changed |= ImGui::SliderFloat("HG eccentricity", &p.phaseG, -0.9f, 0.9f, "%.2f");
                    changed |= ImGui::SliderFloat("Light absorption", &p.lightAbsorption, 0.1f, 4.0f, "%.2f");
                    changed |= ImGui::SliderInt("Light steps", &p.lightSteps, 1, 12);
                    changed |= ImGui::SliderFloat("Powder", &p.powderStrength, 0.0f, 1.5f, "%.2f");
                    changed |= ImGui::SliderFloat("Multi scattering", &p.multiScatterStrength, 0.0f, 1.0f, "%.2f");
                    changed |= ImGui::SliderFloat("Silver lining", &p.silverLiningStrength, 0.0f, 2.0f, "%.2f");
                    changed |= ImGui::SliderFloat("Sky exposure", &p.skyExposure, 0.25f, 2.5f, "%.2f");
                }

                if (BeginParameterCategory("Sampling", 3))
                {
                    changed |= ImGui::SliderInt("View steps", &p.viewSteps, 48, 128);
                    changed |= ImGui::SliderFloat("Ray jitter", &p.jitterStrength, 0.0f, 1.0f, "%.2f");
                }

                if (BeginParameterCategory("Wide Cloud Layer", 4))
                {
                    changed |= ImGui::SliderFloat("Cloud base height", &p.cloudBaseHeight, -2.0f, 10.0f, "%.2f km");
                    changed |= ImGui::SliderFloat("Cloud thickness", &p.cloudThickness, 3.0f, 16.0f, "%.2f km");
                    ImGui::TextWrapped("8 km 초과 시 128 view step의 샘플 간격이 넓어지고, "
                                       "밀도 구간의 light march가 늘어 GPU 시간이 증가할 수 있습니다.");
                    changed |= ImGui::SliderFloat("3D noise world size", &p.cloudNoiseWorldSize, 2.0f, 50.0f, "%.1f km");
                    changed |= ImGui::SliderFloat("Max march distance", &p.maxMarchDistance, 20.0f, 250.0f, "%.1f km");
                    changed |= ImGui::SliderFloat("Weather world size", &p.weatherWorldSize, 20.0f, 300.0f, "%.1f km");
                    changed |= ImGui::SliderFloat("Weather coverage", &p.weatherCoverageStrength, 0.0f, 1.5f, "%.2f");
                    changed |= ImGui::SliderFloat("Weather type bias", &p.weatherTypeBias, 0.0f, 1.0f, "%.2f");
                    changed |= ImGui::SliderFloat("Base height variation", &p.heightVariation, 0.0f, 2.0f, "%.2f km");
                    changed |= ImGui::SliderFloat("Thickness variation", &p.thicknessVariation, 0.0f, 0.8f, "%.2f");
                    changed |= ImGui::SliderFloat("Horizon fade start", &p.horizonFadeStart, 10.0f, 200.0f, "%.1f km");
                    changed |= ImGui::SliderFloat("Horizon fade end", &p.horizonFadeEnd, 20.0f, 250.0f, "%.1f km");
                    changed |= ImGui::InputFloat("Weather seed", &p.weatherSeed, 1.0f, 10.0f, "%.0f");
                }

                if (BeginParameterCategory("Presets", 5))
                {
                    ImGui::InputText("Name", m_presetName, IM_ARRAYSIZE(m_presetName));
                    if (ImGui::Button("Save"))
                        m_status = SavePreset(m_presetName, p) ? "saved" : "save failed";
                    ImGui::SameLine();
                    if (ImGui::Button("Load"))
                    {
                        auto it = m_userPresets.find(m_presetName);
                        if (it != m_userPresets.end())
                        {
                            p = it->second;
                            changed = presetSelected = true;
                            m_activePreset = m_presetName;
                            m_status = "loaded";
                        }
                        else m_status = "preset not found";
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Delete"))
                        m_status = DeletePreset(m_presetName) ? "deleted" : "delete failed";

                    if (ImGui::Button("Default")) { p = DefaultCloudParameters(); changed = presetSelected = true; m_activePreset = "Default"; }
                    ImGui::SameLine();
                    if (ImGui::Button("Cumulus")) { p = CumulusCloudParameters(); changed = presetSelected = true; m_activePreset = "Cumulus"; }
                    ImGui::SameLine();
                    if (ImGui::Button("Stratus")) { p = StratusCloudParameters(); changed = presetSelected = true; m_activePreset = "Stratus"; }
                    ImGui::SameLine();
                    if (ImGui::Button("Showcase")) { p = CumulusShowcaseCloudParameters(); changed = presetSelected = true; m_activePreset = "Cumulus Showcase"; }
                    ImGui::SameLine();
                    if (ImGui::Button("Wide")) { p = CumulusWideShowcaseCloudParameters(); changed = presetSelected = true; m_activePreset = "Cumulus Wide"; }
                    if (!m_status.empty()) ImGui::TextUnformatted(m_status.c_str());
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Stats"))
            {
                ImGui::TextUnformatted("Preview: 256 x 256 x 4 MRT");
                ImGui::Text("Inspector source: %s", p.useTextureCache ? "3D texture cache" : "procedural HLSL");
                ImGui::TextUnformatted("Main ray march source: 3D texture cache");
                ImGui::TextUnformatted("Base cache: 128^3 RGBA8 (8.0 MiB, shape bands)");
                ImGui::TextUnformatted("Detail cache: 64^3 RGBA8 (1.0 MiB)");
                ImGui::TextUnformatted("Weather cache: 512^2 RGBA8 (1.0 MiB)");
                ImGui::SeparatorText("Persistent cache");
                if (ImGui::Button("Save Noise Cache")) cacheActions.save = true;
                ImGui::SameLine();
                if (ImGui::Button("Revert to Saved")) cacheActions.revert = true;
                ImGui::SameLine();
                if (ImGui::Button("Rebuild")) cacheActions.rebuild = true;
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
    if (changed && !presetSelected) m_activePreset = "Custom";
    return changed;
}

void DebugUI::DrawTelemetry(const CloudParameters& p, const TelemetrySnapshot& t)
{
    if (!m_initialized || !m_telemetryVisible) return;

    static const char* modeNames[] = {
        "Lit", "Density", "Base Shape", "Detail", "Height", "Transmittance",
        "Cache Diff", "Seam Diff", "Base R", "Base G", "Base B", "Base A",
        "Light Visibility", "Phase", "Ambient", "Direct",
        "Weather Coverage", "Weather Type", "Weather Base", "Weather Thickness"
    };
    const int modeCount = static_cast<int>(IM_ARRAYSIZE(modeNames));
    const char* mode = p.renderMode >= 0 && p.renderMode < modeCount ? modeNames[p.renderMode] : "Unknown";

    const float compassAzimuth = std::fmod(90.0f - p.sunAzimuth + 720.0f, 360.0f);
    static const char* directions[] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
    const int directionIndex = static_cast<int>(std::floor((compassAzimuth + 22.5f) / 45.0f)) & 7;
    const float layerBottom = p.cloudBaseHeight - (std::max)(p.heightVariation, 0.0f);
    const float layerTop = p.cloudBaseHeight + (std::max)(p.heightVariation, 0.0f) +
        (std::max)(p.cloudThickness, 0.1f) *
        (1.0f + (std::clamp)(p.thicknessVariation, 0.0f, 1.0f));

    const ImGuiIO& io = ImGui::GetIO();
    std::array<std::string, 8> lines;
    char text[256] = {};
    std::snprintf(text, sizeof(text), "%s | %s", m_activePreset.c_str(), mode);
    lines[0] = text;
    std::snprintf(text, sizeof(text), "FPS %.0f | Frame %.1f ms", io.Framerate, t.frameIntervalMs);
    lines[1] = text;
    std::snprintf(text, sizeof(text), "CPU %.2f ms | GPU %.2f ms (cloud %.2f)",
                  t.cpuRenderMs, t.gpuTotalMs, t.gpuCloudMs);
    lines[2] = text;
    std::snprintf(text, sizeof(text), "Sun %s | Az %03.0f deg | El %.0f deg",
                  directions[directionIndex], compassAzimuth, p.sunElevation);
    lines[3] = text;
    std::snprintf(text, sizeof(text), "Cloud %.1f-%.1f km | Coverage %.2f",
                  layerBottom, layerTop, p.coverage);
    lines[4] = text;
    std::snprintf(text, sizeof(text), "March V%d L%d | Max %.0f km",
                  p.viewSteps, p.lightSteps, p.maxMarchDistance);
    lines[5] = text;
    lines[6] = "Cache " + t.cacheStatus;
    lines[7] = "F1 editor | F2 HUD";

    constexpr float padding = 10.0f;
    const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
    // Win32 DPI 배율이 있는 환경에서는 draw-list 좌표를 논리 폭으로 환산한다.
    const float dpiScale = (std::max)(io.DisplayFramebufferScale.x, 1.0f);
    const float displayWidth = io.DisplaySize.x / dpiScale;
    constexpr float boxWidth = 330.0f;
    const ImVec2 boxMin((std::max)(12.0f, displayWidth - boxWidth - 12.0f), 12.0f);
    const ImVec2 boxMax(displayWidth - 12.0f,
                        boxMin.y + padding * 2.0f + lineHeight * static_cast<float>(lines.size()));
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    drawList->AddRectFilled(boxMin, boxMax, IM_COL32(10, 14, 20, 165), 5.0f);
    drawList->AddRect(boxMin, boxMax, IM_COL32(150, 175, 205, 120), 5.0f);
    ImVec2 cursor(boxMin.x + padding, boxMin.y + padding);
    for (size_t i = 0; i < lines.size(); ++i)
    {
        ImU32 color = IM_COL32(235, 240, 245, 255);
        if (i == 6)
        {
            color = t.cacheStatus.find("Error") != std::string::npos
                ? IM_COL32(255, 90, 75, 255)
                : t.cacheStatus == "Saved"
                    ? IM_COL32(115, 230, 140, 255)
                    : IM_COL32(255, 200, 65, 255);
        }
        else if (i == 7)
            color = IM_COL32(155, 165, 180, 255);
        drawList->AddText(cursor, color, lines[i].c_str());
        cursor.y += lineHeight;
    }
}

void DebugUI::EndFrame()
{
    if (!m_initialized) return;
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

std::wstring DebugUI::PresetPath() const
{
    const wchar_t* localAppData = _wgetenv(L"LOCALAPPDATA");
    std::filesystem::path dir = localAppData ? localAppData : L".";
    dir /= L"VolumetricCloud";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return (dir / L"cloud-presets.ini").wstring();
}

void DebugUI::LoadPresets()
{
    m_userPresets.clear();
    std::ifstream file(PresetPath());
    std::string line, name;
    while (std::getline(file, line))
    {
        if (line.size() > 2 && line.front() == '[' && line.back() == ']')
        {
            name = line.substr(1, line.size() - 2);
            m_userPresets[name] = DefaultCloudParameters();
            continue;
        }
        const size_t eq = line.find('=');
        if (name.empty() || eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const float value = std::strtof(line.c_str() + eq + 1, nullptr);
        CloudParameters& p = m_userPresets[name];
        if (key == "noiseWorldScale") p.noiseWorldScale = value;
        else if (key == "basePeriod") p.basePeriod = static_cast<int>(value);
        else if (key == "detailPeriod") p.detailPeriod = static_cast<int>(value);
        else if (key == "noiseCutoffThreshold") p.noiseCutoffThreshold = value;
        else if (key == "coverage") p.coverage = value;
        else if (key == "baseErosion") p.baseErosion = value;
        else if (key == "density") p.densityMultiplier = value;
        else if (key == "erosion") p.erosionStrength = value;
        else if (key == "bottomFade") p.bottomFade = value;
        else if (key == "topFade") p.topFade = value;
        else if (key == "windSpeed") p.windSpeed = value;
        else if (key == "windX") p.windDirection.x = value;
        else if (key == "windY") p.windDirection.y = value;
        else if (key == "seed") p.seed = value;
        else if (key == "baseOctaves") p.baseOctaves = static_cast<int>(value);
        else if (key == "detailOctaves") p.detailOctaves = static_cast<int>(value);
        else if (key == "sunAzimuth") p.sunAzimuth = value;
        else if (key == "sunElevation") p.sunElevation = value;
        else if (key == "sunIntensity") p.sunIntensity = value;
        else if (key == "ambientIntensity") p.ambientIntensity = value;
        else if (key == "phaseG") p.phaseG = value;
        else if (key == "lightAbsorption") p.lightAbsorption = value;
        else if (key == "lightSteps") p.lightSteps = static_cast<int>(value);
        else if (key == "powderStrength") p.powderStrength = value;
        else if (key == "multiScatterStrength") p.multiScatterStrength = value;
        else if (key == "silverLiningStrength") p.silverLiningStrength = value;
        else if (key == "jitterStrength") p.jitterStrength = value;
        else if (key == "viewSteps") p.viewSteps = static_cast<int>(value);
        else if (key == "skyExposure") p.skyExposure = value;
        else if (key == "cloudBaseHeight") p.cloudBaseHeight = value;
        else if (key == "cloudThickness") p.cloudThickness = value;
        else if (key == "cloudNoiseWorldSize") p.cloudNoiseWorldSize = value;
        else if (key == "maxMarchDistance") p.maxMarchDistance = value;
        else if (key == "weatherWorldSize") p.weatherWorldSize = value;
        else if (key == "weatherCoverageStrength") p.weatherCoverageStrength = value;
        else if (key == "weatherTypeBias") p.weatherTypeBias = value;
        else if (key == "heightVariation") p.heightVariation = value;
        else if (key == "thicknessVariation") p.thicknessVariation = value;
        else if (key == "horizonFadeStart") p.horizonFadeStart = value;
        else if (key == "horizonFadeEnd") p.horizonFadeEnd = value;
        else if (key == "weatherSeed") p.weatherSeed = value;
    }
}

bool DebugUI::SavePreset(const std::string& name, const CloudParameters& p)
{
    if (name.empty()) return false;
    m_userPresets[name] = p;
    std::ofstream file(PresetPath(), std::ios::trunc);
    if (!file) return false;
    file << std::setprecision(9);
    for (const auto& [presetName, v] : m_userPresets)
    {
        file << '[' << presetName << "]\n"
             << "noiseWorldScale=" << v.noiseWorldScale << "\n"
             << "basePeriod=" << v.basePeriod << "\n"
             << "detailPeriod=" << v.detailPeriod << "\n"
             << "noiseCutoffThreshold=" << v.noiseCutoffThreshold << "\n"
             << "coverage=" << v.coverage << "\n"
             << "baseErosion=" << v.baseErosion << "\n"
             << "density=" << v.densityMultiplier << "\n"
             << "erosion=" << v.erosionStrength << "\n"
             << "bottomFade=" << v.bottomFade << "\n"
             << "topFade=" << v.topFade << "\n"
             << "windSpeed=" << v.windSpeed << "\n"
             << "windX=" << v.windDirection.x << "\n"
             << "windY=" << v.windDirection.y << "\n"
             << "seed=" << v.seed << "\n"
             << "baseOctaves=" << v.baseOctaves << "\n"
             << "detailOctaves=" << v.detailOctaves << "\n"
             << "sunAzimuth=" << v.sunAzimuth << "\n"
             << "sunElevation=" << v.sunElevation << "\n"
             << "sunIntensity=" << v.sunIntensity << "\n"
             << "ambientIntensity=" << v.ambientIntensity << "\n"
             << "phaseG=" << v.phaseG << "\n"
             << "lightAbsorption=" << v.lightAbsorption << "\n"
             << "lightSteps=" << v.lightSteps << "\n"
             << "powderStrength=" << v.powderStrength << "\n"
             << "multiScatterStrength=" << v.multiScatterStrength << "\n"
             << "silverLiningStrength=" << v.silverLiningStrength << "\n"
             << "jitterStrength=" << v.jitterStrength << "\n"
             << "viewSteps=" << v.viewSteps << "\n"
             << "skyExposure=" << v.skyExposure << "\n"
             << "cloudBaseHeight=" << v.cloudBaseHeight << "\n"
             << "cloudThickness=" << v.cloudThickness << "\n"
             << "cloudNoiseWorldSize=" << v.cloudNoiseWorldSize << "\n"
             << "maxMarchDistance=" << v.maxMarchDistance << "\n"
             << "weatherWorldSize=" << v.weatherWorldSize << "\n"
             << "weatherCoverageStrength=" << v.weatherCoverageStrength << "\n"
             << "weatherTypeBias=" << v.weatherTypeBias << "\n"
             << "heightVariation=" << v.heightVariation << "\n"
             << "thicknessVariation=" << v.thicknessVariation << "\n"
             << "horizonFadeStart=" << v.horizonFadeStart << "\n"
             << "horizonFadeEnd=" << v.horizonFadeEnd << "\n"
             << "weatherSeed=" << v.weatherSeed << "\n\n";
    }
    return true;
}

bool DebugUI::DeletePreset(const std::string& name)
{
    if (m_userPresets.erase(name) == 0) return false;
    std::ofstream file(PresetPath(), std::ios::trunc);
    if (!file) return false;
    file << std::setprecision(9);
    for (const auto& [presetName, v] : m_userPresets)
    {
        file << '[' << presetName << "]\n"
             << "noiseWorldScale=" << v.noiseWorldScale << "\n"
             << "basePeriod=" << v.basePeriod << "\n"
             << "detailPeriod=" << v.detailPeriod << "\n"
             << "noiseCutoffThreshold=" << v.noiseCutoffThreshold << "\n"
             << "coverage=" << v.coverage << "\n"
             << "baseErosion=" << v.baseErosion << "\n"
             << "density=" << v.densityMultiplier << "\n"
             << "erosion=" << v.erosionStrength << "\n"
             << "bottomFade=" << v.bottomFade << "\n"
             << "topFade=" << v.topFade << "\n"
             << "windSpeed=" << v.windSpeed << "\n"
             << "windX=" << v.windDirection.x << "\n"
             << "windY=" << v.windDirection.y << "\n"
             << "seed=" << v.seed << "\n"
             << "baseOctaves=" << v.baseOctaves << "\n"
             << "detailOctaves=" << v.detailOctaves << "\n"
             << "sunAzimuth=" << v.sunAzimuth << "\n"
             << "sunElevation=" << v.sunElevation << "\n"
             << "sunIntensity=" << v.sunIntensity << "\n"
             << "ambientIntensity=" << v.ambientIntensity << "\n"
             << "phaseG=" << v.phaseG << "\n"
             << "lightAbsorption=" << v.lightAbsorption << "\n"
             << "lightSteps=" << v.lightSteps << "\n"
             << "powderStrength=" << v.powderStrength << "\n"
             << "multiScatterStrength=" << v.multiScatterStrength << "\n"
             << "silverLiningStrength=" << v.silverLiningStrength << "\n"
             << "jitterStrength=" << v.jitterStrength << "\n"
             << "viewSteps=" << v.viewSteps << "\n"
             << "skyExposure=" << v.skyExposure << "\n"
             << "cloudBaseHeight=" << v.cloudBaseHeight << "\n"
             << "cloudThickness=" << v.cloudThickness << "\n"
             << "cloudNoiseWorldSize=" << v.cloudNoiseWorldSize << "\n"
             << "maxMarchDistance=" << v.maxMarchDistance << "\n"
             << "weatherWorldSize=" << v.weatherWorldSize << "\n"
             << "weatherCoverageStrength=" << v.weatherCoverageStrength << "\n"
             << "weatherTypeBias=" << v.weatherTypeBias << "\n"
             << "heightVariation=" << v.heightVariation << "\n"
             << "thicknessVariation=" << v.thicknessVariation << "\n"
             << "horizonFadeStart=" << v.horizonFadeStart << "\n"
             << "horizonFadeEnd=" << v.horizonFadeEnd << "\n"
             << "weatherSeed=" << v.weatherSeed << "\n\n";
    }
    return true;
}
