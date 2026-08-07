// ============================================================================
//  NoiseLab.h - 단계 6 Weather 밀도 단면과 태양광 개발용 ImGui UI
// ============================================================================
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

#include "WeatherMap.h"
#include "LightParameters.h"
#include "FrameProfiler.h"

enum class NoiseSliceAxis : std::uint32_t
{
    XY = 0,
    XZ = 1,
    YZ = 2,
};

enum class NoiseOutputMode : std::uint32_t
{
    RawNoise = 0,
    ThresholdDensity = 1,
    FinalDensity = 2,
    HeightFraction = 3,
    HeightProfile = 4,
    BaseDensity = 5,
    DetailNoise = 6,
    Erosion = 7,
    DetailSampleMask = 8,
    WeatherCoverage = 9,
    CloudType = 10,
    WeatherDensityModifier = 11,
    WeatherThresholdDensity = 12,
    TypedHeightProfile = 13,
    WeatherUv = 14,
};

struct alignas(16) NoiseLabParameters
{
    DirectX::XMFLOAT3 normalizedSlicePosition = { 0.5f, 0.5f, 0.5f };
    std::uint32_t outputMode = static_cast<std::uint32_t>(NoiseOutputMode::RawNoise);
    std::uint32_t sliceAxis = static_cast<std::uint32_t>(NoiseSliceAxis::XY);
    float effectiveTime = 0.0f;
    float padding[2] = {};
};

static_assert(sizeof(NoiseLabParameters) == 32,
              "NoiseLabParameters must match NoiseLabCB");

class NoiseLab
{
public:
    NoiseLab() = default;
    ~NoiseLab();

    bool Init(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context);
    void Shutdown();
    bool HandleWindowMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    void ToggleVisible();
    void SetVisible(bool visible) { m_visible = visible; }
    bool IsVisible() const { return m_visible; }

    // UI 명령을 먼저 만든 뒤 동일 프레임에서 preview texture를 갱신한다.
    void BeginFrame(float applicationTime,
                    CloudParameters& cloudParameters,
                    LightParameters& lightParameters,
                    Stage6SunPreset& sunPreset,
                    Stage5WeatherPreset weatherPreset,
                    const WeatherMapGeneratorSettings& weatherGeneratorSettings,
                    ID3D11ShaderResourceView* weatherMapSrv,
                    const std::string& weatherMapStatus,
                    const FrameTimingSnapshot& timing,
                    bool& vsyncEnabled,
                    std::uint64_t shaderGeneration,
                    const std::string& shaderStatus,
                    const std::string& shaderError);
    void RenderPreviews(ID3D11VertexShader* fullscreenVs,
                        ID3D11PixelShader* noiseLabPs,
                        ID3D11Buffer* cloudCb,
                        ID3D11ShaderResourceView* weatherMapSrv,
                        ID3D11SamplerState* weatherSampler);
    void EndFrame(ID3D11RenderTargetView* backBufferRtv);

    float EffectiveTime() const { return m_effectiveTime; }
    bool ConsumeParametersChanged();
    bool ConsumeWeatherPresetRequest(Stage5WeatherPreset& preset);
    bool ConsumeWeatherGeneratorRequest(WeatherMapGeneratorSettings& settings);
    void SetOutputMode(NoiseOutputMode mode)
    {
        m_parameters.outputMode = static_cast<std::uint32_t>(mode);
    }
    // 현재 출력 모드의 축 특성까지 고려해 GPU readback이 유효한지 검사한다.
    bool ValidatePreviewData();
    std::uint64_t PreviewHash(std::size_t targetIndex);
    bool ExportSnapshot(const std::filesystem::path& root,
                        const CloudParameters& cloudParameters,
                        const LightParameters& lightParameters,
                        Stage6SunPreset sunPreset,
                        Stage4DetailPreset detailPreset,
                        Stage5WeatherPreset weatherPreset,
                        const WeatherMapGeneratorSettings& weatherGeneratorSettings,
                        std::uint64_t weatherMapHash,
                        ID3D11Texture2D* weatherMapTexture,
                        const std::filesystem::path& noiseSourcePath);
    bool ConsumeExportRequest();
    const std::string& LastExportStatus() const { return m_exportStatus; }

private:
    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    struct SliceTarget
    {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11RenderTargetView> rtv;
        ComPtr<ID3D11ShaderResourceView> srv;
        ComPtr<ID3D11Texture2D> staging;
    };

    static constexpr UINT kPreviewSize = 512;

    bool CreatePreviewTargets();
    bool CreateConstantBuffer();
    void DrawControlWindow(CloudParameters& cloudParameters,
                           LightParameters& lightParameters,
                           Stage6SunPreset& sunPreset,
                           Stage5WeatherPreset weatherPreset,
                           const WeatherMapGeneratorSettings& weatherGeneratorSettings,
                           ID3D11ShaderResourceView* weatherMapSrv,
                           const std::string& weatherMapStatus,
                           bool& vsyncEnabled,
                           std::uint64_t shaderGeneration,
                           const std::string& shaderStatus,
                           const std::string& shaderError);
    void DrawSlice(const char* label, NoiseSliceAxis axis, SliceTarget& target);
    void DrawPerformanceOverlay(const FrameTimingSnapshot& timing,
                                const CloudParameters& cloudParameters,
                                const LightParameters& lightParameters,
                                bool vsyncEnabled);
    bool DrawPeriodicChannelFields(const char* label,
                                   PeriodicChannelSettings& settings);
    void QueueWeatherGeneratorRequest(bool force);
    void UpdateEffectiveTime(float applicationTime);
    bool SaveTargetPng(const std::filesystem::path& path, SliceTarget& target);
    bool SaveTexturePng(const std::filesystem::path& path,
                        ID3D11Texture2D* texture);
    std::uint64_t HashFile(const std::filesystem::path& path) const;
    bool WriteMetadata(const std::filesystem::path& path,
                       const CloudParameters& cloudParameters,
                       const LightParameters& lightParameters,
                       Stage6SunPreset sunPreset,
                       Stage4DetailPreset detailPreset,
                       Stage5WeatherPreset weatherPreset,
                       const WeatherMapGeneratorSettings& weatherGeneratorSettings,
                       std::uint64_t weatherMapHash,
                       const std::filesystem::path& noiseSourcePath) const;

    HWND m_hwnd = nullptr;
    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    std::array<SliceTarget, 3> m_targets;
    ComPtr<ID3D11Buffer> m_noiseLabCb;

    bool m_initialized = false;
    bool m_visible = true;
    bool m_parametersChanged = false;
    bool m_exportRequested = false;
    int m_weatherPresetRequest = -1;
    bool m_weatherGeneratorRequestPending = false;
    bool m_weatherGeneratorDraftInitialized = false;
    bool m_weatherGeneratorDirty = false;
    bool m_weatherGeneratorLiveUpdate = true;
    float m_weatherGeneratorLastRequestTime = -1.0f;
    float m_currentApplicationTime = 0.0f;
    WeatherMapGeneratorSettings m_weatherGeneratorDraft;
    WeatherMapGeneratorSettings m_weatherGeneratorRequest;
    ID3D11ShaderResourceView* m_weatherMapPreviewSrv = nullptr;
    Stage5WeatherPreset m_weatherPreset = Stage5WeatherPreset::ChannelDebug;
    bool m_slicePlaying = false;
    bool m_timePaused = false;
    int m_playAxis = 2;
    float m_slicePlaySpeed = 0.20f;
    float m_slicePlayDirection = 1.0f;
    float m_timeScale = 1.0f;
    float m_lastApplicationTime = 0.0f;
    float m_effectiveTime = 0.0f;
    bool m_hasApplicationTime = false;
    NoiseLabParameters m_parameters;
    std::string m_exportStatus;
};
