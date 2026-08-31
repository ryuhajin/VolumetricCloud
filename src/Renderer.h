// ============================================================================
//  Renderer.h - Direct3D 11 단계 14 대기·지면·구름 HDR 통합 렌더링
// ============================================================================
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl/client.h>

#include <cstdint>
#include <array>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "CloudParameters.h"
#include "CloudAppearance.h"
#include "CloudFormationPresetStore.h"
#include "CloudFormationSettings.h"
#include "CloudLodParameters.h"
#include "CloudShapeParameters.h"
#include "CloudDomainParameters.h"
#include "EnvironmentParameters.h"
#include "FrameProfiler.h"
#include "LightParameters.h"
#include "NoiseLab.h"
#include "OptimizationParameters.h"
#include "Stage10UpsamplingParameters.h"
#include "Stage11TemporalParameters.h"
#include "Stage12ShadowMath.h"
#include "Stage12ShadowParameters.h"
#include "Stage13ScaleMath.h"
#include "Stage13OpenWorldMath.h"
#include "Stage13NoiseVolumeMath.h"
#include "Stage13SceneMath.h"
#include "Stage13OpticsLightingMath.h"
#include "AtmosphereParameters.h"
#include "GroundLightingParameters.h"
#include "Stage14Parameters.h"
#include "Stage15Parameters.h"
#include "ToneMappingParameters.h"
#include "WeatherMap.h"

class Camera;

struct DiagnosticSceneVertex
{
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 color;
    DirectX::XMFLOAT3 normal;
    std::uint32_t materialId = 0;
};

struct CloudDiagnosticFrame
{
    int width = 0;
    int height = 0;
    std::vector<DirectX::XMFLOAT4> pixels;
};

struct SceneDepthDiagnosticFrame
{
    int width = 0;
    int height = 0;
    std::vector<float> deviceDepth;
};

struct Stage14LutValidationResult
{
    bool finiteNonNegative = false;
    double transmittanceMae = 1.0;
    double transmittanceP99 = 1.0;
    std::size_t comparedChannelCount = 0;
};

class Renderer
{
public:
    ~Renderer();
    bool Init(HWND hwnd, int width, int height, bool enableNoiseVolumes = true);
    void Resize(int width, int height);
    void Render(Camera& camera, float timeSeconds);
    bool CaptureCloudDiagnosticFrame(const Camera& camera, float timeSeconds,
                                     CloudDebugMode mode,
                                     CloudDiagnosticFrame& frame,
                                     bool forceDirectComposite = false);
    bool CaptureSceneDepthDiagnosticFrame(
        const Camera& camera, SceneDepthDiagnosticFrame& frame);
    // 자동 GPU 회귀가 구름 패스만 비교할 때 사용하는 테스트 전용 fixture다.
    void SetOpaqueSceneForTest(bool enabled)
    {
        m_renderOpaqueSceneForTest = enabled;
    }
    void SetDebugMode(CloudDebugMode mode);
    CloudDebugMode DebugMode() const;
    void ConfigureVolumeForTest(DirectX::XMFLOAT3 boundsMin,
                                DirectX::XMFLOAT3 boundsMax,
                                float stepSize);
    void ConfigureNoiseForTest(float baseScale, float coverage,
                               float densityMultiplier, float windSpeed,
                               float noiseOffset);
    void SetHeightProfile(float bottomFadeEnd, float topFadeStart);
    void ConfigureDetailForTest(float detailScale, float erosionStrength,
                                float windSpeed, float noiseOffset);
    bool ApplyStage5WeatherPreset(Stage5WeatherPreset preset);
    bool ApplyWeatherGeneratorSettings(
        const WeatherMapGeneratorSettings& settings);
    Stage5WeatherPreset WeatherPreset() const { return m_weatherPreset; }
    void ApplyStage6SunPreset(Stage6SunPreset preset);
    void ApplyStage7PhasePreset(Stage7PhasePreset preset);
    void ApplyStage8EnvironmentPreset(Stage8EnvironmentPreset preset);
    void ApplyPortfolioHeroLighting();
    void SetLightSampling(std::uint32_t maxSteps, float stepSize);
    void SetCloudLodForValidation(bool enabled, float startMeters,
                                  float endMeters);
    void SetViewSamplingForSmoke(std::uint32_t maxSteps, float stepSize);
    void SetCloudWindSpeedsForValidation(float bulkSpeed,
                                         float weatherSpeed,
                                         float detailSpeed);
    void SetCloudDomainType(CloudDomainType type);
    bool ApplyStage13SimilarityScale(float scale);
    bool ApplyStage13OpenWorldPreset();
    void ApplyStage9OptimizationPreset(Stage9OptimizationPreset preset);
    void ConfigureStage9ConeForValidation(std::uint32_t taps,
                                          float angleDegrees,
                                          float farSampleFraction = 0.85f);
    const OptimizationParameters& OptimizationSettings() const
    {
        return m_optimizationParameters;
    }
    Stage9OptimizationPreset OptimizationPreset() const
    {
        return m_optimizationPreset;
    }
    void ApplyStage10ResolutionPreset(Stage10ResolutionPreset preset);
    void SetStage10UpsampleFilter(Stage10UpsampleFilter filter);
    const Stage10UpsamplingParameters& UpsamplingSettings() const
    {
        return m_upsamplingParameters;
    }
    Stage10UpsamplingParameters& MutableUpsamplingSettings()
    {
        return m_upsamplingParameters;
    }
    Stage10ResolutionPreset ResolutionPreset() const
    {
        return m_resolutionPreset;
    }
    Stage10UpsampleFilter UpsampleFilter() const
    {
        return static_cast<Stage10UpsampleFilter>(
            m_upsamplingParameters.filterMode);
    }
    int CloudRenderWidth() const { return m_cloudRenderWidth; }
    int CloudRenderHeight() const { return m_cloudRenderHeight; }
    void SetStage11TemporalMode(Stage11TemporalMode mode);
    Stage11TemporalMode TemporalMode() const
    {
        if (m_temporalParameters.temporalEnabled == 0u)
            return Stage11TemporalMode::Off;
        return m_temporalParameters.jitterEnabled != 0u
            ? Stage11TemporalMode::Stable4Phase
            : Stage11TemporalMode::FullResolution;
    }
    const Stage11TemporalParameters& TemporalSettings() const
    {
        return m_temporalParameters;
    }
    Stage11TemporalParameters& MutableTemporalSettings()
    {
        return m_temporalParameters;
    }
    void ResetTemporalHistory(Stage11HistoryResetReason reason =
        Stage11HistoryResetReason::Manual);
    bool TemporalHistoryValid() const { return m_temporalHistoryValid; }
    std::uint32_t TemporalAccumulatedFrames() const
    {
        return m_temporalAccumulatedFrames;
    }
    void SetStage12ShadowMode(Stage12ShadowMode mode);
    bool SetStage12ShadowPreset(Stage12ShadowPreset preset);
    Stage12ShadowMode ShadowMode() const
    {
        return static_cast<Stage12ShadowMode>(m_shadowParameters.shadowMode);
    }
    Stage12ShadowPreset ShadowPreset() const
    {
        return static_cast<Stage12ShadowPreset>(m_shadowParameters.shadowPreset);
    }
    const Stage12ShadowParameters& ShadowSettings() const
    {
        return m_shadowParameters;
    }
    Stage12ShadowParameters& MutableShadowSettings()
    {
        return m_shadowParameters;
    }
    std::uint64_t ShadowCacheBytes() const
    {
        return stage12shadow::CacheBytes(ShadowPreset());
    }
    std::uintptr_t NearShadowCacheIdentity() const
    {
        return reinterpret_cast<std::uintptr_t>(m_shadowNearTexture.Get());
    }
    bool ApplyOpenWorldPipelinePreset(OpenWorldPipelinePreset preset);
    bool ApplyCloudAppearancePreset(CloudAppearancePreset preset);
    CloudFormationSettings CurrentCloudFormation() const;
    bool ApplyCloudFormationPresetForValidation(
        const CloudFormationPresetTarget& target,
        bool allowUserOverrides = false);
    bool ApplyCloudFormationSettingsForValidation(
        const CloudFormationSettings& settings);
    bool SaveCurrentCloudFormationPresetForValidation();
    bool SaveCurrentCloudFormationAsCustomForValidation();
    bool RestoreCurrentCloudFormationBuiltInForValidation();
    void InjectCloudFormationApplyFailureForValidation()
    {
        m_failNextCloudFormationApplyForValidation = true;
    }
    const std::filesystem::path& CloudFormationPresetRootForValidation() const
    {
        return m_cloudFormationPresetRoot;
    }
    void SetCloudFormationPresetRootForValidation(
        const std::filesystem::path& root);
    const CloudFormationPresetTarget& FormationTarget() const
    {
        return m_cloudFormationTarget;
    }
    bool FormationTargetValid() const { return m_cloudFormationTargetValid; }
    CloudFormationPresetSource FormationSource() const
    {
        return m_cloudFormationSource;
    }
    bool FormationCloudDirty() const { return m_cloudFormationCloudDirty; }
    bool FormationSceneDirty() const { return m_cloudFormationSceneDirty; }
    bool SaveCurrentCloudAppearance();
    CloudAppearanceSettings CaptureCurrentCloudAppearance() const;
    CloudAppearancePreset AppearancePreset() const
    {
        return m_cloudAppearancePreset;
    }
    bool AppearanceDirty() const { return m_cloudAppearanceDirty; }
    bool HasSavedCustomAppearance() const
    {
        return m_hasSavedCustomAppearance;
    }
    const CloudAppearanceSettings& SavedCustomAppearance() const
    {
        return m_savedCustomAppearance;
    }
    const std::string& CloudAppearanceStatus() const
    {
        return m_cloudAppearanceStatus;
    }
    bool PipelineComparisonActive() const
    {
        return m_pipelineComparisonActive;
    }
    bool SetCloudTypeMode(CloudTypeMode type);
    CloudTypeMode CurrentCloudTypeMode() const
    {
        return m_cloudTypeMode;
    }
    bool RegenerateNoiseVolumes();
    void SetNoiseSource(NoiseSource source);
    NoiseSource CurrentNoiseSource() const
    {
        return static_cast<NoiseSource>(m_noiseVolumeParameters.noiseSource);
    }
    const NoiseVolumeParameters& NoiseVolumeSettings() const
    {
        return m_noiseVolumeParameters;
    }
    std::uint64_t BaseNoiseVolumeHash() const { return m_baseNoiseVolumeHash; }
    std::uint64_t DetailNoiseVolumeHash() const { return m_detailNoiseVolumeHash; }
    double NoiseVolumeGenerationMilliseconds() const
    {
        return m_noiseVolumeGenerationMilliseconds;
    }
    bool ReadNoiseVolumeBytes(bool base, std::vector<std::uint8_t>& bytes) const;
    OpenWorldPipelinePreset PipelinePreset() const
    {
        return m_openWorldPipelinePreset;
    }
    CloudDomainType DomainType() const
    {
        return static_cast<CloudDomainType>(m_cloudDomainParameters.domainType);
    }
    const CloudDomainParameters& DomainSettings() const
    {
        return m_cloudDomainParameters;
    }
    const CloudParameters& CloudSettings() const { return m_cloudParameters; }
    const CloudShapeParameters& ShapeSettings() const { return m_cloudShapeParameters; }
    Stage6SunPreset SunPreset() const { return m_sunPreset; }
    Stage7PhasePreset PhasePreset() const { return m_phasePreset; }
    Stage8EnvironmentPreset EnvironmentPreset() const { return m_environmentPreset; }
    const LightParameters& LightSettings() const { return m_lightParameters; }
    const EnvironmentParameters& EnvironmentSettings() const { return m_environmentParameters; }
    EnvironmentParameters& MutableEnvironmentSettings()
    {
        return m_environmentParameters;
    }
    const CloudRimParameters& RimSettings() const { return m_cloudRimParameters; }
    // 자동 검증 전용: Composite-only 변경이 history reset을 일으키지 않는지
    // 다른 preset 소유권을 건드리지 않고 확인한다.
    void SetRimEnabledForValidation(bool enabled)
    {
        m_cloudRimParameters.enabled = enabled ? 1u : 0u;
    }
    bool LastEffectiveRimEnabled() const { return m_lastEffectiveRimEnabled; }
    bool LastRimUploadSucceeded() const { return m_lastRimUploadSucceeded; }
    const AtmosphereParameters& AtmosphereSettings() const
    {
        return m_atmosphereParameters;
    }
    AtmosphereParameters& MutableAtmosphereSettings()
    {
        return m_atmosphereParameters;
    }
    const GroundLightingParameters& GroundLightingSettings() const
    {
        return m_groundLightingParameters;
    }
    GroundLightingParameters& MutableGroundLightingSettings()
    {
        return m_groundLightingParameters;
    }
    const ToneMappingParameters& ToneMappingSettings() const
    {
        return m_toneMappingParameters;
    }
    ToneMappingParameters& MutableToneMappingSettings()
    {
        return m_toneMappingParameters;
    }
    std::uint64_t AtmosphereLutGeneration(std::size_t index) const
    {
        return index < 6 ? m_atmosphereLutGenerations[index] : 0u;
    }
    std::uint64_t AtmosphereLutHash(std::size_t index) const
    {
        return index < 6 ? m_atmosphereLutHashes[index] : 0u;
    }
    bool ValidateStage14Luts(Stage14LutValidationResult& result);
    const CloudLodParameters& LodSettings() const { return m_cloudLodParameters; }
    std::uint64_t WeatherMapHash() const { return m_weatherMapHash; }
    const WeatherMapGeneratorSettings& WeatherGeneratorSettings() const
    {
        return m_weatherGeneratorSettings;
    }
    std::uintptr_t WeatherTextureIdentity() const
    {
        return reinterpret_cast<std::uintptr_t>(m_weatherMapTexture.Get());
    }
    std::uintptr_t WeatherSrvIdentity() const
    {
        return reinterpret_cast<std::uintptr_t>(m_weatherMapSrv.Get());
    }
    bool HasDebugLayerErrors() const;
    bool HandleWindowMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    bool DeveloperUiWantsKeyboard() const;
    float CameraMoveSpeed() const { return m_cameraMoveSpeedMetersPerSecond; }
    void SetCameraMoveSpeed(float value)
    {
        m_cameraMoveSpeedMetersPerSecond =
            stage13scene::SanitizeMoveSpeed(value);
    }
    bool ValidateNoiseLabPreviews();
    bool ExportNoiseLabSnapshot(const std::filesystem::path& root,
                                bool includePng = true);
    void SetNoiseLabOutputMode(NoiseOutputMode mode) { m_noiseLab.SetOutputMode(mode); }
    std::uint64_t NoiseLabPreviewHash(std::size_t targetIndex);
    std::uint64_t ShaderGeneration() const { return m_shaderGeneration; }
    void EnableFrameHashCapture(bool enabled) { m_captureFrameHashes = enabled; }
    void EnableNoiseLabPreviews(bool enabled) { m_renderNoiseLabPreviews = enabled; }
    void SetNoiseLabVisible(bool visible) { m_noiseLab.SetVisible(visible); }
    void SetDeveloperUiScaleForValidation(unsigned int dpi, float userZoom)
    {
        m_noiseLab.SetDeveloperUiScaleForValidation(dpi, userZoom);
    }
    float DeveloperUiUserZoom() const { return m_noiseLab.UserZoom(); }
    float DeveloperUiEffectiveScale() const
    {
        return m_noiseLab.EffectiveUiScale();
    }
    float DeveloperUiWindowPaddingXForValidation() const
    {
        return m_noiseLab.StyleWindowPaddingXForValidation();
    }
    float DeveloperUiPanelWidthForValidation(std::size_t panelIndex) const
    {
        return m_noiseLab.PanelWidthForValidation(panelIndex);
    }
    bool DeveloperUiPanelVisibleForValidation(std::size_t panelIndex) const
    {
        return m_noiseLab.PanelVisibleForValidation(panelIndex);
    }
    DirectX::XMFLOAT4 DeveloperUiPanelRectForValidation(
        std::size_t panelIndex) const
    {
        return m_noiseLab.PanelRectForValidation(panelIndex);
    }
    DirectX::XMFLOAT4 Stage15OverlayRectForValidation() const
    {
        return m_noiseLab.Stage15OverlayRectForValidation();
    }
    DirectX::XMFLOAT4 PerformanceOverlayRectForValidation() const
    {
        return m_noiseLab.PerformanceOverlayRectForValidation();
    }
    void SetOverlayVisibilityForValidation(bool status, bool performance)
    {
        m_stage15StatusOverlayVisible = status;
        m_performanceOverlayVisible = performance;
    }
    // 자동 성능 fixture는 ImGui/overlay/preview/export를 GPU frame에서 완전히 제외한다.
    void SetAutomatedRenderMode(bool enabled) { m_automatedRenderMode = enabled; }
    // UI 자체를 검증하는 자동 smoke도 사용자 디스크 preset에는 의존하면 안 된다.
    // 렌더 자동화와 저장 override 정책을 분리해 그 두 계약을 동시에 지킨다.
    void SetIgnoreCloudFormationPresetOverrides(bool ignored);
    void ToggleDeveloperUiPanel(DeveloperUiPanel panel)
    {
        m_noiseLab.TogglePanel(panel);
    }
    void SetVSyncEnabled(bool enabled) { m_vsyncEnabled = enabled; }
    bool VSyncEnabled() const { return m_vsyncEnabled; }
    const FrameTimingSnapshot& TimingSnapshot() const
    {
        return m_frameProfiler.Snapshot();
    }
    std::uint64_t LastCloudFrameHash() const { return m_lastCloudFrameHash; }
    const std::string& AdapterName() const { return m_adapterName; }
    const std::string& DriverVersion() const { return m_driverVersion; }
    std::uint64_t Stage15StateFingerprint() const;
    std::uint64_t Stage15GpuResourceIdentityFingerprint() const;
    std::uint64_t NonCirrusRaymarchShaderHash() const
    {
        return m_nonCirrusRaymarchShaderHash;
    }
    std::uint64_t NonCirrusDeepShadowShaderHash() const
    {
        return m_nonCirrusDeepShadowShaderHash;
    }
    std::uint64_t CirrusDeepShadowShaderHash() const
    {
        return m_cirrusDeepShadowShaderHash;
    }
    std::uint64_t CloudCompositeShaderHash() const
    {
        return m_cloudCompositeShaderHash;
    }
    std::uint64_t WeatherUploadCount() const { return m_weatherUploadCount; }
    std::uint64_t Stage15TransitionCommitCount() const
    {
        return m_stage15TransitionCommitCount;
    }
    std::uint64_t ConstantBufferUploadCount() const
    {
        return m_constantBufferUploadCount;
    }
    std::uint64_t ShaderCompileCallCount() const
    {
        return m_shaderCompileCallCount;
    }
    std::uint64_t ShaderCacheHitCount() const
    {
        return m_shaderCacheHitCount;
    }
    bool ValidateWarmShaderCache();
    bool ValidateNoiseSamplerContract() const;
    void InjectStage15TransitionFailureForTest(
        Stage15TransitionFailurePoint point);
    bool Stage15TransitionFailed() const
    {
        return m_stage15TransitionStatus.find("failed") != std::string::npos;
    }
    bool ApplyStage15Defaults();
    void RequestStage15QualityPreset(Stage15QualityPreset preset)
    {
        if (m_stage15DiagnosticMode != Stage15DiagnosticMode::None)
            return;
        m_pendingStage15Transition.qualityPreset = static_cast<int>(preset);
    }
    void RequestStage15ConceptPreset(Stage15ConceptPreset preset)
    {
        if (m_stage15DiagnosticMode == Stage15DiagnosticMode::CaptureStill)
            return;
        m_pendingStage15Transition.conceptPreset = static_cast<int>(preset);
    }
    void RequestStage15DiagnosticMode(Stage15DiagnosticMode mode)
    {
        m_pendingStage15Transition.diagnosticMode = static_cast<int>(mode);
    }
    void CycleStage15QualityPreset();
    void ToggleStage15TemporalOverride();
    Stage15QualityPreset Stage15Quality() const
    {
        return m_stage15QualityPreset;
    }
    Stage15ConceptPreset Stage15Concept() const
    {
        return m_stage15ConceptPreset;
    }
    Stage15DiagnosticMode Stage15Diagnostic() const
    {
        return m_stage15DiagnosticMode;
    }
    bool Stage15TemporalOverrideActive() const
    {
        return m_stage15TemporalOverrideActive;
    }
    bool Stage15StatusOverlayVisible() const
    {
        return m_stage15StatusOverlayVisible;
    }
    bool PerformanceOverlayVisible() const
    {
        return m_performanceOverlayVisible;
    }
    void UpdateWindowMetrics(int physicalWidth, int physicalHeight,
                             unsigned int dpi, float dpiScale,
                             bool perMonitorV2, bool native1080p);
    Stage15OutputExtentSnapshot OutputExtentSnapshot() const;
    bool SizeDependentResourcesValid() const
    {
        return m_sizeDependentResourcesValid;
    }
    bool ConsumeNative1080pRequest(bool& enable);
    void NotifyNative1080pResult(bool requestedEnable, bool succeeded,
                                 bool active, const std::string& status);
    bool SceneInputLocked() const;
    Stage15CaptureState CaptureState() const { return m_stage15CaptureState; }
    std::uint32_t CaptureCompletedSamples() const
    {
        return m_stage15CaptureCompletedSamples;
    }
    const std::string& CaptureStatusText() const
    {
        return m_stage15CaptureStatus;
    }
    std::uint32_t TemporalResetCountLast60Frames() const;
    Stage11HistoryResetReason LastTemporalResetReason() const
    {
        return m_lastTemporalResetReason;
    }
    bool TemporalStatisticsValid() const
    {
        return m_temporalStatisticsValid;
    }
    float TemporalHistoryValidPercent() const
    {
        return m_temporalHistoryValidPercent;
    }
    float TemporalAverageHistoryWeight() const
    {
        return m_temporalAverageHistoryWeight;
    }

private:
    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    struct CameraCB
    {
        DirectX::XMFLOAT4X4 invViewProj;
        DirectX::XMFLOAT4X4 invProjection;
        DirectX::XMFLOAT4X4 invViewRotation;
        DirectX::XMFLOAT3 cameraPos;
        float time;
        DirectX::XMFLOAT2 renderSize;
        float nearPlane;
        float farPlane;
    };

    struct SceneCB
    {
        DirectX::XMFLOAT4X4 viewProj;
    };

    struct Stage15TransitionRequest
    {
        int qualityPreset = -1;
        int conceptPreset = -1;
        int diagnosticMode = -1;
        bool toggleTemporalOverride = false;

        bool Empty() const
        {
            return qualityPreset < 0 && conceptPreset < 0 &&
                diagnosticMode < 0 && !toggleTemporalOverride;
        }
    };

    static_assert(sizeof(CameraCB) == 224, "CameraCB must match cbCamera");
    static_assert(sizeof(SceneCB) == 64, "SceneCB must match cbScene");

    struct AtmosphereLut2D
    {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> srv;
        ComPtr<ID3D11UnorderedAccessView> uav;
    };

    struct AtmosphereLut3D
    {
        ComPtr<ID3D11Texture3D> texture;
        ComPtr<ID3D11ShaderResourceView> srv;
        ComPtr<ID3D11UnorderedAccessView> uav;
    };

    struct AtmosphereLutSet
    {
        AtmosphereLut2D transmittance;
        AtmosphereLut2D multiScattering;
        AtmosphereLut2D skyView;
        AtmosphereLut2D skyIrradiance;
        AtmosphereLut3D aerialRadiance;
        AtmosphereLut3D aerialTransmittance;
    };

    bool CompileShaderFromFile(const std::wstring& path,
                               const char* entryPoint,
                               const char* target,
                               ComPtr<ID3DBlob>& outBlob,
                               bool showErrors,
                               const D3D_SHADER_MACRO* defines = nullptr);
    bool CreateShaders(bool showErrors);
    bool CreateBackBufferTarget();
    bool CreateSceneTargets();
    bool CreateCloudTargets();
    bool CreateTemporalHistoryTargets();
    bool CreateDeepShadowResources(Stage12ShadowPreset preset);
    bool CreateDiagnosticScene();
    bool CreatePipelineStates();
    bool CreateConstantBuffers();
    bool EnsureAtmosphereLuts(const Camera& camera);
    bool CreateAtmosphereLut2D(UINT width, UINT height,
                               AtmosphereLut2D& target) const;
    bool CreateAtmosphereLut3D(UINT size, AtmosphereLut3D& target) const;
    stage14::GpuParameters BuildStage14GpuParameters(
        const Camera& camera) const;
    void BindAtmosphereResources();
    void RenderToneMapPass(ID3D11ShaderResourceView* sourceOverride = nullptr);
    bool CreateStage15CaptureTarget();
    void ReleaseStage15CaptureTarget();
    bool AccumulateStage15CaptureSample();
    void BeginStage15CaptureIfReady(float effectiveTime);
    void ResetStage15CaptureAccumulation(const char* reason);
    bool CreateWeatherMapTexture(Stage5WeatherPreset preset);
    bool GenerateNoiseVolumes(
        ID3D11ComputeShader* baseShader, ID3D11ComputeShader* detailShader,
        ComPtr<ID3D11Texture3D>& baseTexture,
        ComPtr<ID3D11ShaderResourceView>& baseSrv,
        ComPtr<ID3D11Texture3D>& detailTexture,
        ComPtr<ID3D11ShaderResourceView>& detailSrv,
        std::uint64_t& baseHash, std::uint64_t& detailHash,
        float& detailNeutralValue,
        double& generationMilliseconds);
    bool HashNoiseVolume(ID3D11Texture3D* texture, std::uint64_t& hash) const;
    bool ReadNoiseVolumeBytesFromTexture(
        ID3D11Texture3D* texture, std::vector<std::uint8_t>& bytes) const;
    bool UpdateWeatherMapTexture(
        Stage5WeatherPreset preset,
        const WeatherMapGeneratorSettings& settings,
        const WeatherMapData* prebuiltMap = nullptr);
    bool InitializeStage15Presets();
    Stage15QualityDescriptor CaptureCurrentStage15Quality() const;
    bool ApplyStage15QualityDescriptor(
        const Stage15QualityDescriptor& descriptor,
        Stage15QualityPreset selection, bool updateSelection,
        bool resetState = true);
    bool ApplyStage15QualityPresetImmediate(
        Stage15QualityPreset preset, bool resetState = true);
    bool ApplyStage15ConceptPresetImmediate(
        Stage15ConceptPreset preset, bool resetState = true);
    bool ApplyStage15DiagnosticModeImmediate(
        Stage15DiagnosticMode mode, bool resetState = true);
    void ApplyPendingStage15Requests();
    bool ApplyCloudAppearanceSettings(
        const CloudAppearanceSettings& settings,
        CloudAppearancePreset preset);
    bool ApplyCloudFormationAtomic(
        const CloudFormationSettings& settings,
        bool resetState = true);
    bool ApplyCloudFormationPresetTarget(
        const CloudFormationPresetTarget& target,
        bool allowUserOverrides,
        bool resetState = true);
    bool SaveCurrentCloudFormationToTarget(
        const CloudFormationPresetTarget& target,
        bool switchTargetAfterSave);
    bool RestoreCurrentCloudFormationBuiltIn();
    void RefreshSavedCustomFormationState();
    void MarkCloudFormationDirty();
    void MarkCloudFormationSceneDirty();
    void MarkCloudAppearanceDirty();
    void ReleaseSizeDependentResources();
    void RenderDiagnosticScene(const Camera& camera,
                               DirectX::XMFLOAT2 jitterPixels = {});
    void UpdateStage12ShadowParameters(const Camera& camera);
    void RenderDeepShadowCaches(const Camera& camera, float timeSeconds);
    void RenderCloudPass(const Camera& camera, float timeSeconds,
                         ID3D11RenderTargetView* targetOverride = nullptr);
    void UpdateCloudConstantBuffers(const Camera& camera, float timeSeconds,
                                    int renderWidth, int renderHeight,
                                    DirectX::XMFLOAT2 jitterPixels = {});
    void BindCloudRaymarchResources(ID3D11PixelShader* pixelShader);
    void UnbindCloudShaderResources(UINT count);
    void RenderCloudDataPass(const Camera& camera, float timeSeconds,
                             DirectX::XMFLOAT2 jitterPixels = {});
    void RenderCloudUpsamplePass(const Camera& camera, float timeSeconds,
                                 ID3D11RenderTargetView* targetOverride = nullptr,
                                 DirectX::XMFLOAT2 jitterPixels = {});
    void RenderCloudCompositePass(
        const Camera& camera, float timeSeconds,
        ID3D11ShaderResourceView* resolvedCloud,
        ID3D11ShaderResourceView* resolvedAux,
        ID3D11RenderTargetView* targetOverride = nullptr);
    bool RenderCloudTemporalPass(const Camera& camera, float timeSeconds,
                                 ID3D11RenderTargetView* targetOverride = nullptr);
    void UpdateTemporalStatisticsReadback();
    void PrepareTemporalFrame(const Camera& camera, float timeSeconds);
    void CommitTemporalFrame(const Camera& camera, float timeSeconds);
    bool EnsureCloudTargets();
    void CaptureCloudFrameHash();
    void CheckShaderHotReload();
    void UpdateShaderWriteTimes();
    bool GetShaderWriteTimes(
        std::map<std::wstring, std::filesystem::file_time_type>& writeTimes) const;

    int m_width = 0;
    int m_height = 0;
    bool m_sizeDependentResourcesValid = false;
    HWND m_hwnd = nullptr;
    Stage15OutputExtentSnapshot m_outputExtent = {};
    std::uint64_t m_renderFrameSerial = 0;
    std::array<std::uint64_t, 64> m_temporalResetFrames = {};
    std::size_t m_temporalResetFrameCursor = 0;
    Stage11HistoryResetReason m_lastTemporalResetReason =
        Stage11HistoryResetReason::FirstFrame;

    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<IDXGISwapChain> m_swapChain;
    ComPtr<ID3D11RenderTargetView> m_backBufferRtv;

    ComPtr<ID3D11Texture2D> m_sceneColor;
    ComPtr<ID3D11RenderTargetView> m_sceneColorRtv;
    ComPtr<ID3D11ShaderResourceView> m_sceneColorSrv;
    ComPtr<ID3D11Texture2D> m_sceneDepth;
    ComPtr<ID3D11DepthStencilView> m_sceneDepthDsv;
    ComPtr<ID3D11ShaderResourceView> m_sceneDepthSrv;
    ComPtr<ID3D11Texture2D> m_hdrComposite;
    ComPtr<ID3D11RenderTargetView> m_hdrCompositeRtv;
    ComPtr<ID3D11ShaderResourceView> m_hdrCompositeSrv;
    ComPtr<ID3D11Texture2D> m_stage15CaptureAccumulator;
    ComPtr<ID3D11RenderTargetView> m_stage15CaptureAccumulatorRtv;
    ComPtr<ID3D11ShaderResourceView> m_stage15CaptureAccumulatorSrv;

    ComPtr<ID3D11Texture2D> m_cloudScatteringTransmittance;
    ComPtr<ID3D11RenderTargetView> m_cloudScatteringTransmittanceRtv;
    ComPtr<ID3D11ShaderResourceView> m_cloudScatteringTransmittanceSrv;
    ComPtr<ID3D11Texture2D> m_cloudDepthSceneLimit;
    ComPtr<ID3D11RenderTargetView> m_cloudDepthSceneLimitRtv;
    ComPtr<ID3D11ShaderResourceView> m_cloudDepthSceneLimitSrv;
    int m_cloudRenderWidth = 0;
    int m_cloudRenderHeight = 0;

    ComPtr<ID3D11Texture2D> m_temporalHistoryCloud[2];
    ComPtr<ID3D11RenderTargetView> m_temporalHistoryCloudRtv[2];
    ComPtr<ID3D11ShaderResourceView> m_temporalHistoryCloudSrv[2];
    ComPtr<ID3D11Texture2D> m_temporalHistoryAux[2];
    ComPtr<ID3D11RenderTargetView> m_temporalHistoryAuxRtv[2];
    ComPtr<ID3D11ShaderResourceView> m_temporalHistoryAuxSrv[2];
    ComPtr<ID3D11Texture2D> m_temporalStatistics;
    ComPtr<ID3D11RenderTargetView> m_temporalStatisticsRtv;
    ComPtr<ID3D11ShaderResourceView> m_temporalStatisticsSrv;
    std::array<ComPtr<ID3D11Texture2D>, 3> m_temporalStatisticsStaging;
    std::array<bool, 3> m_temporalStatisticsPending = {};
    std::uint32_t m_temporalStatisticsMipLevels = 0;
    std::uint32_t m_temporalStatisticsWriteIndex = 0;
    bool m_temporalStatisticsValid = false;
    float m_temporalHistoryValidPercent = 0.0f;
    float m_temporalAverageHistoryWeight = 0.0f;

    ComPtr<ID3D11Texture2D> m_shadowNearTexture;
    ComPtr<ID3D11ShaderResourceView> m_shadowNearSrv;
    ComPtr<ID3D11UnorderedAccessView> m_shadowNearUav;
    ComPtr<ID3D11Texture2D> m_shadowFarTexture;
    ComPtr<ID3D11ShaderResourceView> m_shadowFarSrv;
    ComPtr<ID3D11UnorderedAccessView> m_shadowFarUav;

    ComPtr<ID3D11VertexShader> m_fullscreenVs;
    ComPtr<ID3D11PixelShader> m_cloudReferencePs;
    ComPtr<ID3D11PixelShader> m_cloudOptimizedPs;
    ComPtr<ID3D11PixelShader> m_cloudReferenceDataPs;
    ComPtr<ID3D11PixelShader> m_cloudOptimizedDataPs;
    ComPtr<ID3D11PixelShader> m_cirrusReferencePs;
    ComPtr<ID3D11PixelShader> m_cirrusOptimizedPs;
    ComPtr<ID3D11PixelShader> m_cirrusReferenceDataPs;
    ComPtr<ID3D11PixelShader> m_cirrusOptimizedDataPs;
    ComPtr<ID3D11PixelShader> m_cloudUpsamplePs;
    ComPtr<ID3D11PixelShader> m_cloudTemporalResolvePs;
    ComPtr<ID3D11PixelShader> m_cloudCompositePs;
    ComPtr<ID3D11PixelShader> m_cloudCompositeNoRimPs;
    ComPtr<ID3D11PixelShader> m_noiseLabPs;
    ComPtr<ID3D11VertexShader> m_sceneVs;
    ComPtr<ID3D11PixelShader> m_scenePs;
    ComPtr<ID3D11PixelShader> m_toneMapPs;
    ComPtr<ID3D11PixelShader> m_stage15CaptureAccumulatePs;
    ComPtr<ID3D11ComputeShader> m_noiseBaseCs;
    ComPtr<ID3D11ComputeShader> m_noiseDetailCs;
    ComPtr<ID3D11ComputeShader> m_nonCirrusDeepShadowCs;
    ComPtr<ID3D11ComputeShader> m_cirrusDeepShadowCs;
    ComPtr<ID3D11ComputeShader> m_atmosphereTransmittanceCs;
    ComPtr<ID3D11ComputeShader> m_atmosphereMultiScatteringCs;
    ComPtr<ID3D11ComputeShader> m_atmosphereSkyViewCs;
    ComPtr<ID3D11ComputeShader> m_atmosphereSkyIrradianceCs;
    ComPtr<ID3D11ComputeShader> m_atmosphereAerialCs;
    ComPtr<ID3D11InputLayout> m_sceneInputLayout;

    ComPtr<ID3D11Buffer> m_cameraCb;
    ComPtr<ID3D11Buffer> m_cloudCb;
    ComPtr<ID3D11Buffer> m_lightCb;
    ComPtr<ID3D11Buffer> m_environmentCb;
    ComPtr<ID3D11Buffer> m_cloudDomainCb;
    ComPtr<ID3D11Buffer> m_noiseVolumeCb;
    ComPtr<ID3D11Buffer> m_cloudShapeCb;
    ComPtr<ID3D11Buffer> m_cloudLodCb;
    ComPtr<ID3D11Buffer> m_optimizationCb;
    ComPtr<ID3D11Buffer> m_upsamplingCb;
    ComPtr<ID3D11Buffer> m_temporalCb;
    ComPtr<ID3D11Buffer> m_cloudRimCb;
    ComPtr<ID3D11Buffer> m_shadowCb;
    ComPtr<ID3D11Buffer> m_stage14Cb;
    ComPtr<ID3D11Buffer> m_sceneCb;
    ComPtr<ID3D11Buffer> m_sceneVertexBuffer;
    ComPtr<ID3D11Buffer> m_sceneIndexBuffer;
    std::uint32_t m_sceneIndexCount = 0;
    std::array<std::uint64_t, 12> m_constantBufferUploadHashes = {};
    std::array<bool, 12> m_constantBufferUploadValid = {};
    std::uint64_t m_constantBufferUploadCount = 0;

    ComPtr<ID3D11DepthStencilState> m_depthState;
    ComPtr<ID3D11RasterizerState> m_rasterizerState;
    ComPtr<ID3D11SamplerState> m_pointClampSampler;
    ComPtr<ID3D11SamplerState> m_linearClampSampler;
    ComPtr<ID3D11SamplerState> m_weatherLinearWrapSampler;
    ComPtr<ID3D11BlendState> m_stage15CaptureBlendState;
    ComPtr<ID3D11Texture2D> m_weatherMapTexture;
    ComPtr<ID3D11ShaderResourceView> m_weatherMapSrv;
    ComPtr<ID3D11Texture3D> m_baseNoiseVolume;
    ComPtr<ID3D11ShaderResourceView> m_baseNoiseVolumeSrv;
    ComPtr<ID3D11Texture3D> m_detailNoiseVolume;
    ComPtr<ID3D11ShaderResourceView> m_detailNoiseVolumeSrv;

    CloudParameters m_cloudParameters;
    CloudDomainParameters m_cloudDomainParameters;
    LightParameters m_lightParameters;
    EnvironmentParameters m_environmentParameters;
    AtmosphereParameters m_atmosphereParameters;
    GroundLightingParameters m_groundLightingParameters;
    ToneMappingParameters m_toneMappingParameters;
    stage14::GpuParameters m_stage14GpuParameters = {};
    AtmosphereLutSet m_atmosphereLuts;
    std::uint64_t m_atmosphereLutHashes[6] = {};
    std::uint64_t m_atmosphereLutGenerations[6] = {};
    bool m_atmosphereLutsValid = false;
    bool m_stage14SunDirectionInitialized = false;
    std::string m_atmosphereStatus = "Not generated";
    Stage5WeatherPreset m_weatherPreset = Stage5WeatherPreset::ChannelDebug;
    Stage6SunPreset m_sunPreset = Stage6SunPreset::Custom;
    Stage7PhasePreset m_phasePreset = Stage7PhasePreset::Off;
    Stage8EnvironmentPreset m_environmentPreset = Stage8EnvironmentPreset::Balanced;
    OpenWorldPipelinePreset m_openWorldPipelinePreset =
        OpenWorldPipelinePreset::Custom;
    CloudTypeMode m_cloudTypeMode = CloudTypeMode::WeatherMap;
    CloudAppearancePreset m_cloudAppearancePreset =
        CloudAppearancePreset::DenseMixedDefault;
    CloudAppearanceSettings m_savedCustomAppearance = DenseMixedAppearance();
    bool m_hasSavedCustomAppearance = false;
    bool m_cloudAppearanceDirty = false;
    std::string m_cloudAppearanceStatus;
    std::filesystem::path m_customAppearancePath;
    std::filesystem::path m_cloudFormationPresetRoot;
    CloudFormationPresetTarget m_cloudFormationTarget =
        CustomFormationTarget();
    bool m_cloudFormationTargetValid = false;
    CloudFormationPresetSource m_cloudFormationSource =
        CloudFormationPresetSource::BuiltIn;
    bool m_cloudFormationCloudDirty = false;
    bool m_cloudFormationSceneDirty = false;
    bool m_hasSavedCustomFormation = false;
    std::string m_cloudFormationStatus = "No formation target";
    bool m_pipelineComparisonActive = false;
    float m_cameraMoveSpeedMetersPerSecond =
        stage13scene::kMoveSpeedMetersPerSecond;
    NoiseVolumeParameters m_noiseVolumeParameters;
    CloudShapeParameters m_cloudShapeParameters;
    CloudLodParameters m_cloudLodParameters;
    OptimizationParameters m_optimizationParameters;
    Stage9OptimizationPreset m_optimizationPreset =
        Stage9OptimizationPreset::Balanced;
    Stage10UpsamplingParameters m_upsamplingParameters;
    Stage10ResolutionPreset m_resolutionPreset =
        Stage10ResolutionPreset::Full;
    Stage11TemporalParameters m_temporalParameters;
    Stage12ShadowParameters m_shadowParameters;
    CloudRimParameters m_cloudRimParameters;
    bool m_lastEffectiveRimEnabled = false;
    bool m_lastRimUploadSucceeded = false;
    bool m_temporalHistoryValid = false;
    bool m_temporalPreviousFrameValid = false;
    std::uint32_t m_temporalHistoryReadIndex = 0;
    std::uint32_t m_temporalAccumulatedFrames = 0;
    float m_previousTemporalTimeSeconds = 0.0f;
    float m_previousTemporalFovYDegrees = 60.0f;
    float m_previousAtmosphereTimeSeconds = 0.0f;
    bool m_previousAtmosphereTimeValid = false;
    std::uint64_t m_baseNoiseVolumeHash = 0;
    std::uint64_t m_detailNoiseVolumeHash = 0;
    double m_noiseVolumeGenerationMilliseconds = 0.0;
    WeatherMapGeneratorSettings m_weatherGeneratorSettings;
    WeatherMapData m_currentWeatherMapData;
    std::uint64_t m_weatherMapHash = 0;
    std::uint64_t m_weatherUploadCount = 0;
    std::string m_weatherMapStatus = "Not generated";
    Stage15QualityPreset m_stage15QualityPreset =
        Stage15QualityPreset::Medium;
    Stage15ConceptPreset m_stage15ConceptPreset =
        Stage15ConceptPreset::UrbanFairWeather;
    Stage15DiagnosticMode m_stage15DiagnosticMode =
        Stage15DiagnosticMode::None;
    bool m_stage15TemporalOverrideActive = false;
    Stage11TemporalMode m_stage15TemporalOverrideMode =
        Stage11TemporalMode::Stable4Phase;
    Stage11TemporalMode m_stage15TemporalOverrideRestoreMode =
        Stage11TemporalMode::Stable4Phase;
    bool m_stage15StatusOverlayVisible = true;
    bool m_performanceOverlayVisible = true;
    Stage15QualityDescriptor m_stage15SavedRealtimeQuality = {};
    Stage15QualityPreset m_stage15SavedRealtimeQualityPreset =
        Stage15QualityPreset::Medium;
    bool m_stage15SavedRealtimeQualityValid = false;
    bool m_stage15ConceptApplied = false;
    Stage15TransitionRequest m_pendingStage15Transition;
    std::string m_stage15TransitionStatus = "Idle";
    std::uint64_t m_stage15TransitionCommitCount = 0;
    bool m_stage15FailCloudTargetPreflight = false;
    bool m_stage15FailShadowResourcePreflight = false;
    bool m_stage15FailWeatherPreflight = false;
    std::array<WeatherMapData, 4> m_stage15WeatherMaps;
    std::array<std::uint64_t, 4> m_stage15WeatherHashes = {};
    int m_native1080pRequest = -1;
    bool m_stage15CaptureOwnsNative1080p = false;
    Stage15CaptureState m_stage15CaptureState =
        Stage15CaptureState::Inactive;
    std::uint32_t m_stage15CaptureCompletedSamples = 0;
    float m_stage15CaptureFrozenTime = 0.0f;
    bool m_stage15CaptureFreezeValid = false;
    std::uint64_t m_stage15CaptureShaderGeneration = 0;
    std::string m_stage15CaptureStatus = "Inactive";
    CloudDebugMode m_stage15CaptureSavedDebugMode =
        CloudDebugMode::Composite;
    bool m_stage15CaptureSavedDebugModeValid = false;

    std::wstring m_shaderDir;
    std::wstring m_fullscreenShaderPath;
    std::wstring m_cloudShaderPath;
    std::wstring m_cloudUpsampleShaderPath;
    std::wstring m_cloudTemporalResolveShaderPath;
    std::wstring m_cloudCompositeShaderPath;
    std::wstring m_noiseLabShaderPath;
    std::wstring m_sceneShaderPath;
    std::wstring m_noiseVolumeShaderPath;
    std::wstring m_deepShadowShaderPath;
    std::wstring m_atmosphereLutShaderPath;
    std::wstring m_toneMapShaderPath;
    std::wstring m_stage15CaptureAccumulateShaderPath;
    bool m_noiseVolumesEnabled = true;
    bool m_automatedRenderMode = false;
    bool m_ignoreCloudFormationPresetOverrides = false;
    bool m_failNextCloudFormationApplyForValidation = false;
    std::uint64_t m_shaderCompileCallCount = 0;
    std::uint64_t m_shaderCacheHitCount = 0;
    std::map<std::wstring, std::filesystem::file_time_type> m_shaderWriteTimes;
    std::uint64_t m_shaderGeneration = 0;
    std::uint64_t m_nonCirrusRaymarchShaderHash = 0;
    std::uint64_t m_nonCirrusDeepShadowShaderHash = 0;
    std::uint64_t m_cirrusDeepShadowShaderHash = 0;
    std::uint64_t m_cloudCompositeShaderHash = 0;
    std::string m_shaderStatus = "Not compiled";
    std::string m_shaderError;
    bool m_captureFrameHashes = false;
    bool m_renderNoiseLabPreviews = true;
    bool m_vsyncEnabled = true;
    bool m_renderOpaqueSceneForTest = true;
    std::uint64_t m_lastCloudFrameHash = 0;
    std::string m_adapterName = "Unknown adapter";
    std::string m_driverVersion = "Unavailable";
    FrameProfiler m_frameProfiler;
    NoiseLab m_noiseLab;
};
