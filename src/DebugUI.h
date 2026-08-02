// ============================================================================
//  DebugUI.h  —  Dear ImGui 기반 구름/노이즈 디버그 패널
// ============================================================================
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3d11.h>

#include <array>
#include <map>
#include <string>

#include "CloudParameters.h"

struct NoiseCacheUiActions
{
    bool save = false;
    bool revert = false;
    bool rebuild = false;
};

struct TelemetrySnapshot
{
    float frameIntervalMs = 0.0f;
    float cpuRenderMs = 0.0f;
    float gpuCloudMs = 0.0f;
    float gpuReconstructionMs = 0.0f;
    float gpuTotalMs = 0.0f;
    bool temporalActive = false;
    std::string cacheStatus;
};

class DebugUI
{
public:
    bool Init(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context);
    void Shutdown();

    bool HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    bool WantsMouseCapture() const;
    bool WantsKeyboardCapture() const;
    void ToggleVisible();
    bool IsVisible() const { return m_visible; }
    void ToggleTelemetry() { m_telemetryVisible = !m_telemetryVisible; }
    void SetTelemetryVisible(bool visible) { m_telemetryVisible = visible; }

    void BeginFrame();
    bool Draw(CloudParameters& params,
              NoisePreviewSettings& preview,
              const std::array<ID3D11ShaderResourceView*, 4>& previewSrvs,
              ID3D11ShaderResourceView* weatherSrv,
              ID3D11ShaderResourceView* placementSrv,
              bool previewDirty,
              bool& temporalEnabled,
              NoiseCacheUiActions& cacheActions);
    void DrawTelemetry(const CloudParameters& params, const TelemetrySnapshot& telemetry);
    void EndFrame();
    static bool RunWorldSpacePresetRoundTripTest();

private:
    void LoadPresets();
    bool SavePreset(const std::string& name, const CloudParameters& params);
    bool DeletePreset(const std::string& name);
    std::wstring PresetPath() const;

    bool m_initialized = false;
    bool m_visible = false;
    bool m_telemetryVisible = true;
    char m_presetName[64] = "MyCloud";
    std::string m_activePreset = "Cumulus Wide";
    std::map<std::string, CloudParameters> m_userPresets;
    std::string m_status;
};
