// ============================================================================
//  NoiseLab.h - High 단일 렌더러의 네 영역 개발 UI
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

#include "AtmosphereParameters.h"
#include "CloudDomainParameters.h"
#include "CloudFormationPresetStore.h"
#include "CloudParameters.h"
#include "CloudShapeParameters.h"
#include "DeveloperUiSettings.h"
#include "EnvironmentParameters.h"
#include "FrameProfiler.h"
#include "GroundLightingParameters.h"
#include "LightParameters.h"
#include "ShaderManifest.h"
#include "Stage12ShadowParameters.h"
#include "Stage13NoiseVolumeMath.h"
#include "Stage15Parameters.h"
#include "ToneMappingParameters.h"
#include "WeatherMap.h"

class Camera;

enum class DeveloperUiPanel : std::size_t
{
    Formation = 0,
    NoiseWeather = 1,
    Lighting = 2,
    Diagnostics = 3,
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
    std::uint32_t outputMode =
        static_cast<std::uint32_t>(NoiseOutputMode::RawNoise);
    std::uint32_t sliceAxis = static_cast<std::uint32_t>(NoiseSliceAxis::XY);
    float effectiveTime = 0.0f;
    float padding[2] = {};
};

static_assert(sizeof(NoiseLabParameters) == 32,
              "NoiseLabParameters must match NoiseLabCB");

class NoiseLab
{
public:
    ~NoiseLab();

    bool Init(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context,
              const std::filesystem::path& developerUiSettingsPath);
    void Shutdown();
    bool HandleWindowMessage(HWND hwnd, UINT message, WPARAM wParam,
                             LPARAM lParam);
    bool WantsKeyboardCapture() const;
    void SetDpi(unsigned int dpi);
    void TogglePanel(DeveloperUiPanel panel);
    void SetVisible(bool visible) { m_panelVisible[0] = visible; }

    void BeginFrame(
        float applicationTime,
        Camera& camera,
        CloudParameters& cloud,
        CloudShapeParameters& shape,
        CloudDomainParameters& domain,
        WeatherMapGeneratorSettings& weather,
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
        ID3D11ShaderResourceView* weatherMapSrv,
        const std::array<ID3D11ShaderResourceView*, 6>& atmosphereLutSrvs,
        const FrameTimingSnapshot& timing,
        bool& vsyncEnabled,
        bool tearingSupported,
        std::uint64_t shaderGeneration,
        const std::string& shaderStatus,
        const std::string& shaderError,
        const shaderreload::ReloadReport& reloadReport);

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
    void SetCloudRuntimeForValidation(float timeSeconds, float movementSpeed);
    float CloudMovementSpeedForValidation() const { return m_timeScale; }
    bool ConsumeFormationEdited();
    bool ConsumeFormationPresetRequest(CloudFormationPresetTarget& target);
    bool ConsumeSaveCustomRequest();
    bool ConsumeLoadCustomRequest();
    bool ConsumeSceneConceptRequest(Stage15ConceptPreset& concept);
    bool ConsumeNoiseVolumeRegenerateRequest();
    bool ConsumeExportRequest();

    void SynchronizeWeatherGeneratorSettings(
        const WeatherMapGeneratorSettings& settings)
    {
        m_weatherDraft = settings;
        m_weatherDraftInitialized = true;
    }
    void SetOutputMode(NoiseOutputMode mode)
    {
        m_parameters.outputMode = static_cast<std::uint32_t>(mode);
    }
    bool ValidateUiContracts() const;
    bool ValidatePreviewData();
    std::uint64_t PreviewHash(std::size_t targetIndex);
    bool ExportSnapshot(
        const std::filesystem::path& root,
        const CloudFormationSettings& formation,
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
        std::uint64_t shaderGeneration);

    float UserZoom() const { return m_developerUiSettings.userZoom; }
    float EffectiveUiScale() const { return m_effectiveUiScale; }
    float StyleWindowPaddingXForValidation() const;
    bool SetDeveloperUiScaleForValidation(unsigned int dpi, float userZoom);

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

    static constexpr UINT kPreviewSize = 512u;
    float Ui(float logicalPixels) const
    {
        return logicalPixels * m_effectiveUiScale;
    }

    bool CreatePreviewTargets();
    bool CreateConstantBuffer();
    bool SetUserZoom(float value);
    void ApplyDeveloperUiScale();
    void UpdateEffectiveTime(float applicationTime);
    void DrawFormationPanel(CloudParameters& cloud,
                            CloudShapeParameters& shape,
                            CloudDomainParameters& domain,
                            WeatherMapGeneratorSettings& weather,
                            const CloudFormationPresetTarget& target,
                            CloudFormationPresetSource source,
                            bool hasCustom,
                            const std::string& status,
                            bool& vsyncEnabled,
                            bool tearingSupported);
    void DrawWeatherMapPanel(CloudParameters& cloud,
                             WeatherMapGeneratorSettings& weather,
                             NoiseVolumeParameters& noiseVolume,
                             std::uint64_t baseHash,
                             std::uint64_t detailHash,
                             double generationMilliseconds,
                             ID3D11ShaderResourceView* weatherMapSrv);
    void DrawLightingPanel(Stage12ShadowParameters& shadow,
                           LightParameters& light,
                           Stage6SunPreset& sunPreset,
                           Stage7PhasePreset& phasePreset,
                           EnvironmentParameters& environment,
                           Stage8EnvironmentPreset& environmentPreset,
                           AtmosphereParameters& atmosphere,
                           GroundLightingParameters& ground,
                           ToneMappingParameters& tone);
    void DrawDiagnosticsPanel(
        Camera& camera,
        CloudParameters& cloud,
        AtmosphereParameters& atmosphere,
        Stage15ConceptPreset concept,
        float& cameraMoveSpeedMetersPerSecond,
        const std::array<ID3D11ShaderResourceView*, 6>& atmosphereLutSrvs,
        std::uint64_t shaderGeneration,
        const std::string& shaderStatus,
        const std::string& shaderError,
        const shaderreload::ReloadReport& reloadReport);
    void DrawProfilerOverlay(const FrameTimingSnapshot& timing);
    void DrawSlice(const char* label, NoiseSliceAxis axis,
                   SliceTarget& target);
    bool DrawPeriodicChannelFields(const char* label,
                                   PeriodicChannelSettings& settings);
    bool WriteMetadata(
        const std::filesystem::path& path,
        const CloudFormationSettings& formation,
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
        std::uint64_t shaderGeneration) const;

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
    bool m_scaleDirty = true;
    std::array<bool, 4> m_panelVisible = { true, false, false, false };

    NoiseLabParameters m_parameters;
    WeatherMapGeneratorSettings m_weatherDraft;
    bool m_weatherDraftInitialized = false;
    float m_lastApplicationTime = 0.0f;
    float m_effectiveTime = 0.0f;
    bool m_hasApplicationTime = false;
    float m_timeScale = 1.0f;

    bool m_formationEdited = false;
    bool m_formationPresetPending = false;
    CloudFormationPresetTarget m_formationPresetRequest =
        TypeFormationTarget(CloudFormationType::Mixed);
    bool m_saveCustomPending = false;
    bool m_loadCustomPending = false;
    int m_sceneConceptRequest = -1;
    bool m_noiseVolumeRegeneratePending = false;
    bool m_exportPending = false;
    std::string m_exportStatus;
};
