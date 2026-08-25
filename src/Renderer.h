// ============================================================================
//  Renderer.h - Direct3D 11 단계 8 환경광·다중 산란 렌더링
// ============================================================================
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl/client.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "CloudParameters.h"
#include "CloudAppearance.h"
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
#include "WeatherMap.h"

class Camera;

struct DiagnosticSceneVertex
{
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 color;
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
        return m_temporalParameters.temporalEnabled != 0u
            ? Stage11TemporalMode::Stable4Phase : Stage11TemporalMode::Off;
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
    bool ExportNoiseLabSnapshot(const std::filesystem::path& root);
    void SetNoiseLabOutputMode(NoiseOutputMode mode) { m_noiseLab.SetOutputMode(mode); }
    std::uint64_t NoiseLabPreviewHash(std::size_t targetIndex);
    std::uint64_t ShaderGeneration() const { return m_shaderGeneration; }
    void EnableFrameHashCapture(bool enabled) { m_captureFrameHashes = enabled; }
    void EnableNoiseLabPreviews(bool enabled) { m_renderNoiseLabPreviews = enabled; }
    void SetNoiseLabVisible(bool visible) { m_noiseLab.SetVisible(visible); }
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

    static_assert(sizeof(CameraCB) == 224, "CameraCB must match cbCamera");
    static_assert(sizeof(SceneCB) == 64, "SceneCB must match cbScene");

    bool CompileShaderFromFile(const std::wstring& path,
                               const char* entryPoint,
                               const char* target,
                               ComPtr<ID3DBlob>& outBlob,
                               bool showErrors);
    bool CreateShaders(bool showErrors);
    bool CreateBackBufferTarget();
    bool CreateSceneTargets();
    bool CreateCloudTargets();
    bool CreateTemporalHistoryTargets();
    bool CreateDeepShadowResources(Stage12ShadowPreset preset);
    bool CreateDiagnosticScene();
    bool CreatePipelineStates();
    bool CreateConstantBuffers();
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
        const WeatherMapGeneratorSettings& settings);
    bool ApplyCloudAppearanceSettings(
        const CloudAppearanceSettings& settings,
        CloudAppearancePreset preset);
    void MarkCloudAppearanceDirty();
    void ReleaseSizeDependentResources();
    void RenderDiagnosticScene(const Camera& camera);
    void UpdateStage12ShadowParameters(const Camera& camera);
    void RenderDeepShadowCaches(const Camera& camera, float timeSeconds);
    void RenderCloudPass(const Camera& camera, float timeSeconds,
                         ID3D11RenderTargetView* targetOverride = nullptr);
    void UpdateCloudConstantBuffers(const Camera& camera, float timeSeconds,
                                    int renderWidth, int renderHeight);
    void BindCloudRaymarchResources(ID3D11PixelShader* pixelShader);
    void UnbindCloudShaderResources(UINT count);
    void RenderCloudDataPass(const Camera& camera, float timeSeconds);
    void RenderCloudUpsamplePass(const Camera& camera, float timeSeconds,
                                 ID3D11RenderTargetView* targetOverride = nullptr);
    bool RenderCloudTemporalPass(const Camera& camera, float timeSeconds,
                                 ID3D11RenderTargetView* targetOverride = nullptr);
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
    ComPtr<ID3D11PixelShader> m_cloudUpsamplePs;
    ComPtr<ID3D11PixelShader> m_cloudTemporalResolvePs;
    ComPtr<ID3D11PixelShader> m_noiseLabPs;
    ComPtr<ID3D11VertexShader> m_sceneVs;
    ComPtr<ID3D11PixelShader> m_scenePs;
    ComPtr<ID3D11ComputeShader> m_noiseBaseCs;
    ComPtr<ID3D11ComputeShader> m_noiseDetailCs;
    ComPtr<ID3D11ComputeShader> m_deepShadowCs;
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
    ComPtr<ID3D11Buffer> m_shadowCb;
    ComPtr<ID3D11Buffer> m_sceneCb;
    ComPtr<ID3D11Buffer> m_sceneVertexBuffer;
    ComPtr<ID3D11Buffer> m_sceneIndexBuffer;
    std::uint32_t m_sceneIndexCount = 0;

    ComPtr<ID3D11DepthStencilState> m_depthState;
    ComPtr<ID3D11RasterizerState> m_rasterizerState;
    ComPtr<ID3D11SamplerState> m_pointClampSampler;
    ComPtr<ID3D11SamplerState> m_linearClampSampler;
    ComPtr<ID3D11SamplerState> m_weatherLinearWrapSampler;
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
    bool m_temporalHistoryValid = false;
    bool m_temporalPreviousFrameValid = false;
    std::uint32_t m_temporalHistoryReadIndex = 0;
    std::uint32_t m_temporalAccumulatedFrames = 0;
    float m_previousTemporalTimeSeconds = 0.0f;
    float m_previousTemporalFovYDegrees = 60.0f;
    std::uint64_t m_baseNoiseVolumeHash = 0;
    std::uint64_t m_detailNoiseVolumeHash = 0;
    double m_noiseVolumeGenerationMilliseconds = 0.0;
    WeatherMapGeneratorSettings m_weatherGeneratorSettings;
    std::uint64_t m_weatherMapHash = 0;
    std::string m_weatherMapStatus = "Not generated";

    std::wstring m_shaderDir;
    std::wstring m_fullscreenShaderPath;
    std::wstring m_cloudShaderPath;
    std::wstring m_cloudUpsampleShaderPath;
    std::wstring m_cloudTemporalResolveShaderPath;
    std::wstring m_noiseLabShaderPath;
    std::wstring m_sceneShaderPath;
    std::wstring m_noiseVolumeShaderPath;
    std::wstring m_deepShadowShaderPath;
    bool m_noiseVolumesEnabled = true;
    std::map<std::wstring, std::filesystem::file_time_type> m_shaderWriteTimes;
    std::uint64_t m_shaderGeneration = 0;
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
