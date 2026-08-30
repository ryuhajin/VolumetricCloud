// ============================================================================
//  NoiseLab.h - 단계 8 밀도·태양광·환경광 개발용 ImGui UI
// ============================================================================
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#include "WeatherMap.h"
#include "Stage13OpenWorldMath.h"
#include "Stage13NoiseVolumeMath.h"
#include "Stage13SceneMath.h"
#include "CloudDomainParameters.h"
#include "CloudLodParameters.h"
#include "CloudShapeParameters.h"
#include "CloudAppearance.h"
#include "LightParameters.h"
#include "EnvironmentParameters.h"
#include "FrameProfiler.h"
#include "OptimizationParameters.h"
#include "Stage10UpsamplingParameters.h"
#include "Stage11TemporalParameters.h"
#include "Stage12ShadowParameters.h"
#include "AtmosphereParameters.h"
#include "GroundLightingParameters.h"
#include "ToneMappingParameters.h"
#include "Stage15Parameters.h"

class Camera;

enum class DeveloperUiPanel : std::size_t
{
    Noise = 0,
    Weather = 1,
    Lighting = 2,
    Camera = 3,
};

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
    TypedShapeProfile = 13,
    WeatherUv = 14,
    BaseVolumeR = 15,
    BaseVolumeG = 16,
    BaseVolumeB = 17,
    BaseVolumeA = 18,
    BaseVolumeCombined = 19,
    DetailVolumeR = 20,
    DetailVolumeG = 21,
    DetailVolumeB = 22,
    DetailVolumeA = 23,
    DetailVolumeCombined = 24,
    WeatherThicknessPotential = 25,
    LocalThickness = 26,
    LocalHeightFraction = 27,
    EffectiveShapeCoverage = 28,
    BaseSupportBeforeDensity = 29,
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
    bool WantsKeyboardCapture() const;
    void ToggleVisible();
    void TogglePanel(DeveloperUiPanel panel);
    void SetVisible(bool visible) { m_panelVisible[0] = visible; }
    bool IsVisible() const { return m_panelVisible[0]; }

    // UI 명령을 먼저 만든 뒤 동일 프레임에서 preview texture를 갱신한다.
    void BeginFrame(float applicationTime,
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
                    const std::string& shaderError);
    void RenderPreviews(ID3D11VertexShader* fullscreenVs,
                        ID3D11PixelShader* noiseLabPs,
                        ID3D11Buffer* cloudCb,
                        ID3D11Buffer* noiseVolumeCb,
                        ID3D11Buffer* cloudShapeCb,
                        ID3D11ShaderResourceView* weatherMapSrv,
                        ID3D11ShaderResourceView* baseNoiseVolumeSrv,
                        ID3D11ShaderResourceView* detailNoiseVolumeSrv,
                        ID3D11SamplerState* weatherSampler);
    void EndFrame(ID3D11RenderTargetView* backBufferRtv);

    float EffectiveTime() const { return m_effectiveTime; }
    bool ConsumeParametersChanged();
    bool ConsumeWeatherPresetRequest(Stage5WeatherPreset& preset);
    bool ConsumeOpenWorldPipelinePresetRequest(OpenWorldPipelinePreset& preset);
    bool ConsumeCloudAppearancePresetRequest(CloudAppearancePreset& preset);
    bool ConsumeCloudAppearanceSaveRequest();
    bool ConsumeCloudAppearanceEdited();
    bool ConsumeNoiseVolumeRegenerateRequest();
    bool ConsumeTemporalResetRequest();
    bool ConsumeStage15QualityRequest(Stage15QualityPreset& preset);
    bool ConsumeStage15ConceptRequest(Stage15ConceptPreset& preset);
    bool ConsumeStage15DiagnosticRequest(Stage15DiagnosticMode& mode);
    void SynchronizeStage15Snapshot(
        Stage15QualityPreset quality, Stage15ConceptPreset concept,
        Stage15DiagnosticMode diagnostic, bool temporalOverrideActive)
    {
        m_stage15QualitySnapshot = quality;
        m_stage15ConceptSnapshot = concept;
        m_stage15DiagnosticSnapshot = diagnostic;
        m_stage15TemporalOverrideSnapshot = temporalOverrideActive;
    }
    bool ConsumeNoiseSourceRequest(NoiseSource& source);
    bool ConsumeWeatherGeneratorRequest(WeatherMapGeneratorSettings& settings);
    void SynchronizeWeatherGeneratorSettings(
        const WeatherMapGeneratorSettings& settings);
    void SetOpenWorldPipelinePreset(OpenWorldPipelinePreset preset)
    {
        m_openWorldPipelinePreset = preset;
    }
    void SetOutputMode(NoiseOutputMode mode)
    {
        m_parameters.outputMode = static_cast<std::uint32_t>(mode);
    }
    // 현재 출력 모드의 축 특성까지 고려해 GPU readback이 유효한지 검사한다.
    bool ValidatePreviewData();
    std::uint64_t PreviewHash(std::size_t targetIndex);
    bool ExportSnapshot(const std::filesystem::path& root,
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
                        bool includePng = true);
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

    struct CameraSnapshot
    {
        DirectX::XMFLOAT3 position = {};
        DirectX::XMFLOAT3 target = {};
        float fovYDegrees = 60.0f;
        float nearPlaneMeters = 0.1f;
        float farPlaneMeters = 60000.0f;
        bool valid = false;
    };

    static constexpr UINT kPreviewSize = 512;

    bool CreatePreviewTargets();
    bool CreateConstantBuffer();
    void DrawControlWindow(DeveloperUiPanel panel,
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
    CameraSnapshot CaptureCamera(const Camera& camera) const;
    void ApplyCameraSnapshot(const CameraSnapshot& snapshot,
                             Camera& camera,
                             const wchar_t* debugName);
    bool SaveTargetPng(const std::filesystem::path& path, SliceTarget& target);
    bool SaveTexturePng(const std::filesystem::path& path,
                        ID3D11Texture2D* texture);
    std::uint64_t HashFile(const std::filesystem::path& path) const;
    bool WriteMetadata(const std::filesystem::path& path,
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
                       const std::filesystem::path& noiseSourcePath) const;

    HWND m_hwnd = nullptr;
    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    std::array<SliceTarget, 3> m_targets;
    ComPtr<ID3D11Buffer> m_noiseLabCb;

    bool m_initialized = false;
    std::array<bool, 4> m_panelVisible = { true, false, false, false };
    bool m_parametersChanged = false;
    bool m_exportRequested = false;
    int m_weatherPresetRequest = -1;
    int m_openWorldPipelinePresetRequest = -1;
    int m_cloudAppearancePresetRequest = -1;
    bool m_cloudAppearanceSaveRequest = false;
    bool m_cloudAppearanceEdited = false;
    bool m_temporalResetRequested = false;
    int m_stage15QualityRequest = -1;
    int m_stage15ConceptRequest = -1;
    int m_stage15DiagnosticRequest = -1;
    bool m_noiseVolumeRegenerateRequest = false;
    int m_noiseSourceRequest = -1;
    bool m_weatherGeneratorRequestPending = false;
    bool m_weatherGeneratorDraftInitialized = false;
    bool m_weatherGeneratorDirty = false;
    bool m_weatherGeneratorLiveUpdate = true;
    float m_weatherGeneratorLastRequestTime = -1.0f;
    float m_currentApplicationTime = 0.0f;
    float m_cameraMoveSpeedMetersPerSecond =
        stage13scene::kMoveSpeedMetersPerSecond;
    OpenWorldPipelinePreset m_openWorldPipelinePreset =
        OpenWorldPipelinePreset::Custom;
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
    CameraSnapshot m_currentCamera;
    CameraSnapshot m_savedCamera;
    std::string m_exportStatus;
    AtmosphereParameters m_atmosphereSnapshot;
    GroundLightingParameters m_groundLightingSnapshot;
    ToneMappingParameters m_toneMappingSnapshot;
    std::array<std::uint64_t, 6> m_atmosphereLutGenerations = {};
    std::array<std::uint64_t, 6> m_atmosphereLutHashes = {};
    Stage15QualityPreset m_stage15QualitySnapshot =
        Stage15QualityPreset::Medium;
    Stage15ConceptPreset m_stage15ConceptSnapshot =
        Stage15ConceptPreset::UrbanFairWeather;
    Stage15DiagnosticMode m_stage15DiagnosticSnapshot =
        Stage15DiagnosticMode::None;
    bool m_stage15TemporalOverrideSnapshot = false;
};
