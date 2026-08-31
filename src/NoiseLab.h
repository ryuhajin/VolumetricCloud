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
#include "DeveloperUiSettings.h"
#include "CloudRimParameters.h"
#include "CloudFormationPresetStore.h"

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
    LocalBaseOffset = 30,
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

    bool Init(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context,
              const std::filesystem::path& developerUiSettingsPath);
    void Shutdown();
    bool HandleWindowMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    // 패널이 닫혀 있어도 PMv2 DPI 변경을 다음 NewFrame 전에 반영한다.
    void SetDpi(unsigned int dpi);
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
                    CloudRimParameters& cloudRimParameters,
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
                    const CloudFormationPresetTarget& formationTarget,
                    bool formationTargetValid,
                    CloudFormationPresetSource formationSource,
                    bool formationCloudDirty,
                    bool formationSceneDirty,
                    bool hasSavedCustomFormation,
                    const std::string& formationStatus,
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
    bool ConsumeCloudFormationPresetRequest(
        CloudFormationPresetTarget& target);
    bool ConsumeCloudFormationSaveToPresetRequest();
    bool ConsumeCloudFormationSaveAsCustomRequest();
    bool ConsumeCloudFormationRestoreBuiltInRequest();
    bool ConsumeNoiseVolumeRegenerateRequest();
    bool ConsumeTemporalResetRequest();
    bool ConsumeStage15QualityRequest(Stage15QualityPreset& preset);
    bool ConsumeStage15ConceptRequest(Stage15ConceptPreset& preset);
    bool ConsumeStage15DiagnosticRequest(Stage15DiagnosticMode& mode);
    // Resolve 이후에만 쓰는 rim 편집은 Concept 소유권만 바꾸고 history를
    // 지우지 않는다.
    bool ConsumeCompositeOnlyParametersChanged();
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
                        const CloudRimParameters& cloudRimParameters,
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
    float UserZoom() const { return m_developerUiSettings.userZoom; }
    float EffectiveUiScale() const { return m_effectiveUiScale; }
    float StyleWindowPaddingXForValidation() const;
    float PanelWidthForValidation(std::size_t panelIndex) const
    {
        return panelIndex < m_panelSize.size()
            ? m_panelSize[panelIndex].x : 0.0f;
    }
    bool PanelVisibleForValidation(std::size_t panelIndex) const
    {
        return panelIndex < m_panelVisible.size() &&
            m_panelVisible[panelIndex];
    }
    DirectX::XMFLOAT4 PanelRectForValidation(std::size_t panelIndex) const
    {
        if (panelIndex >= m_panelSize.size())
            return {};
        return {
            m_panelPosition[panelIndex].x,
            m_panelPosition[panelIndex].y,
            m_panelPosition[panelIndex].x + m_panelSize[panelIndex].x,
            m_panelPosition[panelIndex].y + m_panelSize[panelIndex].y
        };
    }
    DirectX::XMFLOAT4 Stage15OverlayRectForValidation() const
    {
        return { m_stage15OverlayMin.x, m_stage15OverlayMin.y,
                 m_stage15OverlayMax.x, m_stage15OverlayMax.y };
    }
    DirectX::XMFLOAT4 PerformanceOverlayRectForValidation() const
    {
        return { m_performanceOverlayMin.x, m_performanceOverlayMin.y,
                 m_performanceOverlayMax.x, m_performanceOverlayMax.y };
    }
    bool SetDeveloperUiScaleForValidation(unsigned int dpi, float userZoom)
    {
        SetDpi(dpi);
        const bool changed = SetUserZoom(userZoom);
        ApplyDeveloperUiScale();
        return changed;
    }

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
    void ApplyDeveloperUiScale();
    bool SetUserZoom(float value);
    float Ui(float logicalPixels) const
    {
        return logicalPixels * m_effectiveUiScale;
    }
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
                           CloudRimParameters& cloudRimParameters,
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
                           const CloudFormationPresetTarget& formationTarget,
                           bool formationTargetValid,
                           CloudFormationPresetSource formationSource,
                           bool formationCloudDirty,
                           bool formationSceneDirty,
                           bool hasSavedCustomFormation,
                           const std::string& formationStatus,
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
                       const CloudRimParameters& cloudRimParameters,
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
    developerui::Settings m_developerUiSettings;
    std::filesystem::path m_developerUiSettingsPath;
    std::string m_developerUiSettingsStatus;
    unsigned int m_developerUiDpi = 96u;
    float m_effectiveUiScale = 1.0f;
    bool m_developerUiScaleDirty = true;
    std::array<float, 4> m_panelLayoutScale = {};
    std::array<DirectX::XMFLOAT2, 4> m_panelPosition = {};
    std::array<DirectX::XMFLOAT2, 4> m_panelSize = {};
    std::array<bool, 4> m_panelVisible = { true, false, false, false };
    // 새 패널을 열거나 DPI/Zoom이 바뀔 때만 우측 하단 anchor를 다시 적용한다.
    // 열린 동안에는 사용자가 옮긴 위치를 존중한다.
    std::array<bool, 4> m_panelAnchorPending = { true, true, true, true };
    DirectX::XMFLOAT2 m_stage15OverlayMin = {};
    DirectX::XMFLOAT2 m_stage15OverlayMax = {};
    DirectX::XMFLOAT2 m_performanceOverlayMin = {};
    DirectX::XMFLOAT2 m_performanceOverlayMax = {};
    bool m_previousStage15OverlayVisible = true;
    bool m_previousPerformanceOverlayVisible = true;
    bool m_parametersChanged = false;
    bool m_compositeOnlyParametersChanged = false;
    bool m_exportRequested = false;
    int m_weatherPresetRequest = -1;
    int m_openWorldPipelinePresetRequest = -1;
    int m_cloudAppearancePresetRequest = -1;
    bool m_cloudAppearanceSaveRequest = false;
    bool m_cloudAppearanceEdited = false;
    CloudFormationPresetTarget m_cloudFormationPresetRequest =
        CustomFormationTarget();
    bool m_cloudFormationPresetRequestPending = false;
    bool m_cloudFormationSaveToPresetRequest = false;
    bool m_cloudFormationSaveAsCustomRequest = false;
    bool m_cloudFormationRestoreBuiltInRequest = false;
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
