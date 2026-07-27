#include "DebugUI.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

bool DebugUI::Init(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
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
                   bool previewDirty,
                   float cpuFrameMs,
                   const std::string& cacheStatus,
                   NoiseCacheUiActions& cacheActions)
{
    if (!m_initialized || !m_visible) return false;

    bool changed = false;
    ImGui::SetNextWindowSize(ImVec2(620, 720), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Cloud Debug", &m_visible))
    {
        if (ImGui::BeginTabBar("CloudDebugTabs"))
        {
            if (ImGui::BeginTabItem("Render"))
            {
                const char* modes[] = { "Lit Cloud", "Final Density", "Base Shape", "Detail Noise", "Height Mask", "Transmittance", "Cache Difference", "Seam Difference" };
                changed |= ImGui::Combo("Render mode", &p.renderMode, modes, IM_ARRAYSIZE(modes));
                bool useCache = p.useTextureCache != 0;
                if (ImGui::Checkbox("Use 3D texture cache", &useCache)) { p.useTextureCache = useCache ? 1 : 0; changed = true; }
                bool showBounds = p.showBounds != 0;
                if (ImGui::Checkbox("Show AABB bounds", &showBounds)) { p.showBounds = showBounds ? 1 : 0; changed = true; }
                changed |= ImGui::Checkbox("Freeze animation", &preview.freeze);
                if (preview.freeze)
                    changed |= ImGui::SliderFloat("Preview time", &preview.previewTime, 0.0f, 120.0f, "%.2f s");
                if (ImGui::Button("Reset defaults")) { p = DefaultCloudParameters(); changed = true; }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Noise Inspector"))
            {
                const char* axes[] = { "XY", "XZ", "YZ" };
                changed |= ImGui::Combo("Slice axis", &preview.axis, axes, IM_ARRAYSIZE(axes));
                changed |= ImGui::SliderFloat("Slice", &preview.slice, 0.0f, 1.0f, "%.3f");
                ImGui::Text("Preview: %s", previewDirty ? "updating" : "ready");

                if (ImGui::BeginTable("NoisePreviews", 2))
                {
                    ImGui::TableNextColumn(); DrawPreview("Base Perlin-Worley", previewSrvs[0]);
                    ImGui::TableNextColumn(); DrawPreview("Detail Worley", previewSrvs[1]);
                    ImGui::TableNextColumn(); DrawPreview("Height mask", previewSrvs[2]);
                    ImGui::TableNextColumn(); DrawPreview("Final density", previewSrvs[3]);
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Parameters"))
            {
                changed |= ImGui::SliderFloat("Tile repeat", &p.noiseWorldScale, 1.0f, 4.0f, "%.0f");
                p.noiseWorldScale = std::round(p.noiseWorldScale);
                changed |= ImGui::SliderInt("Base period", &p.basePeriod, 2, 12);
                changed |= ImGui::SliderInt("Base octaves", &p.baseOctaves, 1, 5);
                changed |= ImGui::SliderInt("Detail period", &p.detailPeriod, 4, 32);
                changed |= ImGui::SliderInt("Detail octaves", &p.detailOctaves, 1, 4);
                changed |= ImGui::SliderFloat("Noise cutoff threshold", &p.noiseCutoffThreshold,
                                              0.0f, 0.95f, "%.3f");
                changed |= ImGui::SliderFloat("Density", &p.densityMultiplier, 0.0f, 5.0f, "%.2f");
                changed |= ImGui::SliderFloat("Erosion", &p.erosionStrength, 0.0f, 1.0f, "%.3f");
                changed |= ImGui::SliderFloat("Bottom fade", &p.bottomFade, 0.01f, 0.49f, "%.3f");
                changed |= ImGui::SliderFloat("Top fade", &p.topFade, 0.01f, 0.49f, "%.3f");
                changed |= ImGui::InputFloat("Seed", &p.seed, 1.0f, 10.0f, "%.0f");
                changed |= ImGui::SliderFloat2("Wind direction", &p.windDirection.x, -1.0f, 1.0f, "%.2f");
                changed |= ImGui::SliderFloat("Wind speed", &p.windSpeed, 0.0f, 0.25f, "%.3f");
                ImGui::SeparatorText("Lighting");
                changed |= ImGui::SliderFloat("Sun azimuth", &p.sunAzimuth, -180.0f, 180.0f, "%.1f deg");
                changed |= ImGui::SliderFloat("Sun elevation", &p.sunElevation, -10.0f, 90.0f, "%.1f deg");
                changed |= ImGui::SliderFloat("Sun intensity", &p.sunIntensity, 0.0f, 5.0f, "%.2f");
                changed |= ImGui::SliderFloat("Ambient", &p.ambientIntensity, 0.0f, 1.0f, "%.2f");
                changed |= ImGui::SliderFloat("HG eccentricity", &p.phaseG, -0.9f, 0.9f, "%.2f");
                changed |= ImGui::SliderFloat("Light absorption", &p.lightAbsorption, 0.1f, 4.0f, "%.2f");
                changed |= ImGui::SliderInt("Light steps", &p.lightSteps, 1, 12);
                ImGui::SeparatorText("Preset");
                ImGui::InputText("Name", m_presetName, IM_ARRAYSIZE(m_presetName));
                if (ImGui::Button("Save"))
                {
                    m_status = SavePreset(m_presetName, p) ? "saved" : "save failed";
                }
                ImGui::SameLine();
                if (ImGui::Button("Load"))
                {
                    auto it = m_userPresets.find(m_presetName);
                    if (it != m_userPresets.end()) { p = it->second; changed = true; m_status = "loaded"; }
                    else m_status = "preset not found";
                }
                ImGui::SameLine();
                if (ImGui::Button("Delete"))
                    m_status = DeletePreset(m_presetName) ? "deleted" : "delete failed";

                if (ImGui::Button("Default")) { p = DefaultCloudParameters(); changed = true; }
                ImGui::SameLine();
                if (ImGui::Button("Cumulus")) { p = CumulusCloudParameters(); changed = true; }
                ImGui::SameLine();
                if (ImGui::Button("Stratus")) { p = StratusCloudParameters(); changed = true; }
                if (!m_status.empty()) ImGui::TextUnformatted(m_status.c_str());
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Stats"))
            {
                const ImGuiIO& io = ImGui::GetIO();
                ImGui::Text("FPS: %.1f", io.Framerate);
                ImGui::Text("CPU frame: %.3f ms", cpuFrameMs);
                ImGui::TextUnformatted("Preview: 256 x 256 x 4 MRT");
                ImGui::Text("Noise source: %s", p.useTextureCache ? "3D texture cache" : "procedural HLSL");
                ImGui::TextUnformatted("Base cache: 128^3 RGBA8 (8.0 MiB)");
                ImGui::TextUnformatted("Detail cache: 64^3 RGBA8 (1.0 MiB)");
                ImGui::SeparatorText("Persistent cache");
                ImGui::Text("Status: %s", cacheStatus.c_str());
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
    return changed;
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
        else if (key == "noiseCutoffThreshold" || key == "coverage")
            p.noiseCutoffThreshold = value; // coverage는 기존 사용자 프리셋 호환용
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
             << "lightSteps=" << v.lightSteps << "\n\n";
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
             << "lightSteps=" << v.lightSteps << "\n\n";
    }
    return true;
}
