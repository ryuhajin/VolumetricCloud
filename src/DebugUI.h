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

    void BeginFrame();
    bool Draw(CloudParameters& params,
              NoisePreviewSettings& preview,
              const std::array<ID3D11ShaderResourceView*, 4>& previewSrvs,
              ID3D11ShaderResourceView* weatherSrv,
              bool previewDirty,
              float cpuFrameMs,
              float gpuFrameMs,
              const std::string& cacheStatus,
              NoiseCacheUiActions& cacheActions);
    void EndFrame();

private:
    void LoadPresets();
    bool SavePreset(const std::string& name, const CloudParameters& params);
    bool DeletePreset(const std::string& name);
    std::wstring PresetPath() const;

    bool m_initialized = false;
    bool m_visible = false;
    char m_presetName[64] = "MyCloud";
    std::map<std::string, CloudParameters> m_userPresets;
    std::string m_status;
};
