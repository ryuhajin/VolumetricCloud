// ============================================================================
//  Renderer.h - High 단일 경로 Direct3D 11 볼류메트릭 클라우드 렌더러
// ============================================================================
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wrl/client.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "AtmosphereParameters.h"
#include "CloudDomainParameters.h"
#include "CloudFormationPresetStore.h"
#include "CloudFormationSettings.h"
#include "CloudParameters.h"
#include "CloudShapeParameters.h"
#include "EnvironmentParameters.h"
#include "FrameProfiler.h"
#include "GroundLightingParameters.h"
#include "LightParameters.h"
#include "NoiseLab.h"
#include "PresentationMath.h"
#include "ShaderManifest.h"
#include "Stage12ShadowMath.h"
#include "Stage12ShadowParameters.h"
#include "Stage13NoiseVolumeMath.h"
#include "Stage13SceneMath.h"
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

    bool Init(HWND hwnd, int width, int height,
              bool enableNoiseVolumes = true,
              bool showInitializationErrors = true);
    void Resize(int width, int height);
    void Render(Camera& camera, float timeSeconds);

    bool ApplyStage15Defaults();
    bool ApplySceneConcept(Stage15ConceptPreset preset);
    bool ApplyCloudType(const CloudFormationPresetTarget& target);
    bool SaveCustomFormation();
    bool LoadCustomFormation();
    CloudFormationSettings CurrentCloudFormation() const;

    bool ApplyCloudFormationPresetForValidation(
        const CloudFormationPresetTarget& target,
        bool allowUserOverrides = false);
    bool ApplyCloudFormationSettingsForValidation(
        const CloudFormationSettings& settings);
    bool SaveCurrentCloudFormationAsCustomForValidation();
    void InjectCloudFormationApplyFailureForValidation()
    {
        m_failNextCloudFormationApplyForValidation = true;
    }
    void SetCloudFormationPresetRootForValidation(
        const std::filesystem::path& root);
    const std::filesystem::path& CloudFormationPresetRootForValidation() const
    {
        return m_cloudFormationPresetRoot;
    }

    const CloudFormationPresetTarget& FormationTarget() const
    {
        return m_cloudFormationTarget;
    }
    bool FormationTargetValid() const { return m_cloudFormationTargetValid; }
    CloudFormationPresetSource FormationSource() const
    {
        return m_cloudFormationSource;
    }
    bool HasSavedCustomFormation() const { return m_hasSavedCustomFormation; }
    const std::string& FormationStatus() const
    {
        return m_cloudFormationStatus;
    }

    bool ApplyWeatherGeneratorSettings(
        const WeatherMapGeneratorSettings& settings);
    Stage5WeatherPreset WeatherPreset() const { return m_weatherPreset; }
    const WeatherMapGeneratorSettings& WeatherGeneratorSettings() const
    {
        return m_weatherGeneratorSettings;
    }
    std::uint64_t WeatherMapHash() const { return m_weatherMapHash; }
    std::uint64_t WeatherUploadCount() const { return m_weatherUploadCount; }
    std::uintptr_t WeatherTextureIdentity() const
    {
        return reinterpret_cast<std::uintptr_t>(m_weatherMapTexture.Get());
    }
    std::uintptr_t WeatherSrvIdentity() const
    {
        return reinterpret_cast<std::uintptr_t>(m_weatherMapSrv.Get());
    }

    bool RegenerateNoiseVolumes();
    const NoiseVolumeParameters& NoiseVolumeSettings() const
    {
        return m_noiseVolumeParameters;
    }
    std::uint64_t BaseNoiseVolumeHash() const { return m_baseNoiseVolumeHash; }
    std::uint64_t DetailNoiseVolumeHash() const
    {
        return m_detailNoiseVolumeHash;
    }
    double NoiseVolumeGenerationMilliseconds() const
    {
        return m_noiseVolumeGenerationMilliseconds;
    }
    bool ReadNoiseVolumeBytes(
        bool base, std::vector<std::uint8_t>& bytes) const;

    const CloudParameters& CloudSettings() const { return m_cloudParameters; }
    const CloudShapeParameters& ShapeSettings() const
    {
        return m_cloudShapeParameters;
    }
    const CloudDomainParameters& DomainSettings() const
    {
        return m_cloudDomainParameters;
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
        return stage12shadow::kCacheBytes;
    }
    std::uintptr_t NearShadowCacheIdentity() const
    {
        return reinterpret_cast<std::uintptr_t>(m_shadowNearTexture.Get());
    }

    Stage6SunPreset SunPreset() const { return m_sunPreset; }
    Stage7PhasePreset PhasePreset() const { return m_phasePreset; }
    Stage8EnvironmentPreset EnvironmentPreset() const
    {
        return m_environmentPreset;
    }
    const LightParameters& LightSettings() const { return m_lightParameters; }
    const EnvironmentParameters& EnvironmentSettings() const
    {
        return m_environmentParameters;
    }
    EnvironmentParameters& MutableEnvironmentSettings()
    {
        return m_environmentParameters;
    }
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

    void SetOpaqueSceneForTest(bool enabled)
    {
        m_renderOpaqueSceneForTest = enabled;
    }
    void SetDebugMode(CloudDebugMode mode);
    CloudDebugMode DebugMode() const;
    bool HasDebugLayerErrors() const;

    bool HandleWindowMessage(HWND hwnd, UINT message,
                             WPARAM wParam, LPARAM lParam);
    bool DeveloperUiWantsKeyboard() const;
    void ToggleDeveloperUiPanel(DeveloperUiPanel panel)
    {
        m_noiseLab.TogglePanel(panel);
    }
    void SetNoiseLabVisible(bool visible) { m_noiseLab.SetVisible(visible); }
    void SetNoiseLabOutputMode(NoiseOutputMode mode)
    {
        m_noiseLab.SetOutputMode(mode);
    }
    void EnableNoiseLabPreviews(bool enabled)
    {
        m_renderNoiseLabPreviews = enabled;
    }
    bool ValidateNoiseLabUiContracts() const
    {
        return m_noiseLab.ValidateUiContracts();
    }
    void SetCloudRuntimeForValidation(float timeSeconds, float movementSpeed)
    {
        m_noiseLab.SetCloudRuntimeForValidation(timeSeconds, movementSpeed);
    }
    float CloudTimeForValidation() const
    {
        return m_noiseLab.EffectiveTime();
    }
    float CloudMovementSpeedForValidation() const
    {
        return m_noiseLab.CloudMovementSpeedForValidation();
    }
    bool ValidateNoiseLabPreviews();
    bool ExportNoiseLabSnapshot(const std::filesystem::path& root);
    std::uint64_t NoiseLabPreviewHash(std::size_t targetIndex);
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

    float CameraMoveSpeed() const { return m_cameraMoveSpeedMetersPerSecond; }
    void SetCameraMoveSpeed(float value)
    {
        m_cameraMoveSpeedMetersPerSecond =
            stage13scene::SanitizeMoveSpeed(value);
    }
    void SetAutomatedRenderMode(bool enabled)
    {
        m_automatedRenderMode = enabled;
    }
    void SetVSyncEnabled(bool enabled) { m_vsyncEnabled = enabled; }
    bool VSyncEnabled() const { return m_vsyncEnabled; }
    bool TearingSupported() const { return m_tearingSupported; }
    const FrameTimingSnapshot& TimingSnapshot() const
    {
        return m_frameProfiler.Snapshot();
    }
    void EnableFrameHashCapture(bool enabled)
    {
        m_captureFrameHashes = enabled;
    }
    std::uint64_t LastCloudFrameHash() const { return m_lastCloudFrameHash; }
    const std::string& AdapterName() const { return m_adapterName; }
    const std::string& DriverVersion() const { return m_driverVersion; }
    bool SizeDependentResourcesValid() const
    {
        return m_sizeDependentResourcesValid;
    }
    Stage15ConceptPreset Stage15Concept() const
    {
        return m_stage15ConceptPreset;
    }

    std::uint64_t RuntimeStateHash() const;
    std::uint64_t GpuResourceIdentityHash() const;
    std::uint64_t ShaderObjectIdentityHash() const;
    std::uint64_t CloudShaderHash() const { return m_cloudShaderHash; }
    std::uint64_t DeepShadowShaderHash() const
    {
        return m_deepShadowShaderHash;
    }
    std::uint64_t ConstantBufferUploadCount() const
    {
        return m_constantBufferUploadCount;
    }
    std::uint64_t ShaderGeneration() const { return m_shaderGeneration; }
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
    bool ForceShaderReloadScan();
    const shaderreload::ReloadReport& LastShaderReloadReport() const
    {
        return m_lastShaderReloadReport;
    }
    const std::string& ShaderStatus() const { return m_shaderStatus; }
    const std::string& ShaderError() const { return m_shaderError; }
    std::filesystem::path ShaderDirectoryForValidation() const
    {
        return std::filesystem::path(m_shaderDir);
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

    bool CompileShaderFromFile(
        const std::wstring& path, const char* entryPoint, const char* target,
        ComPtr<ID3DBlob>& outBlob, bool showErrors,
        const D3D_SHADER_MACRO* defines = nullptr);
    bool CreateShaders(bool showErrors);
    bool InitializeShaderManifest();
    bool ReloadShaderPrograms(
        const std::vector<std::size_t>& programIndices,
        const std::vector<std::string>& changedFiles,
        bool showErrors);
    bool ScanShaderChanges(bool forced);
    bool CreateBackBufferTarget();
    bool CreateSceneTargets();
    bool CreateDeepShadowResources();
    bool CreateDiagnosticScene();
    bool CreatePipelineStates();
    bool CreateConstantBuffers();
    bool EnsureAtmosphereLuts(const Camera& camera);
    bool CreateAtmosphereLut2D(
        UINT width, UINT height, AtmosphereLut2D& target) const;
    bool CreateAtmosphereLut3D(UINT size, AtmosphereLut3D& target) const;
    stage14::GpuParameters BuildStage14GpuParameters(
        const Camera& camera) const;
    void BindAtmosphereResources();
    void RenderToneMapPass();

    bool CreateWeatherMapTexture(Stage5WeatherPreset preset);
    bool UpdateWeatherMapTexture(
        Stage5WeatherPreset preset,
        const WeatherMapGeneratorSettings& settings,
        const WeatherMapData* prebuiltMap = nullptr);
    bool GenerateNoiseVolumes(
        ID3D11ComputeShader* baseShader,
        ID3D11ComputeShader* detailShader,
        ComPtr<ID3D11Texture3D>& baseTexture,
        ComPtr<ID3D11ShaderResourceView>& baseSrv,
        ComPtr<ID3D11Texture3D>& detailTexture,
        ComPtr<ID3D11ShaderResourceView>& detailSrv,
        std::uint64_t& baseHash,
        std::uint64_t& detailHash,
        float& detailNeutralValue,
        double& generationMilliseconds);
    bool HashNoiseVolume(ID3D11Texture3D* texture,
                         std::uint64_t& hash) const;
    bool ReadNoiseVolumeBytesFromTexture(
        ID3D11Texture3D* texture,
        std::vector<std::uint8_t>& bytes) const;
    bool InitializeStage15Presets();

    bool ApplyCloudFormationAtomic(
        const CloudFormationSettings& settings);
    bool ApplyCloudFormationPresetTarget(
        const CloudFormationPresetTarget& target,
        bool allowUserOverrides);
    bool SaveCurrentCloudFormationToTarget(
        const CloudFormationPresetTarget& target,
        bool switchTargetAfterSave);
    void RefreshSavedCustomFormationState();
    void MarkCloudFormationDirty();

    void ReleaseSizeDependentResources();
    void RenderDiagnosticScene(const Camera& camera);
    void UpdateStage12ShadowParameters(const Camera& camera);
    void RenderDeepShadowCaches(const Camera& camera, float timeSeconds);
    void RenderCloudPass();
    void UpdateCloudConstantBuffers(
        const Camera& camera, float timeSeconds);
    void UnbindCloudShaderResources(UINT count);
    void CaptureCloudFrameHash();

    void CheckShaderHotReload();
    void UpdateShaderWriteTimes();
    bool GetShaderWriteTimes(
        std::map<std::wstring, std::filesystem::file_time_type>& writeTimes)
        const;

    int m_width = 0;
    int m_height = 0;
    bool m_sizeDependentResourcesValid = false;
    HWND m_hwnd = nullptr;
    std::uint64_t m_renderFrameSerial = 0;

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
    ComPtr<ID3D11Texture2D> m_hdrCloud;
    ComPtr<ID3D11RenderTargetView> m_hdrCloudRtv;
    ComPtr<ID3D11ShaderResourceView> m_hdrCloudSrv;

    ComPtr<ID3D11Texture2D> m_shadowNearTexture;
    ComPtr<ID3D11ShaderResourceView> m_shadowNearSrv;
    ComPtr<ID3D11UnorderedAccessView> m_shadowNearUav;
    ComPtr<ID3D11Texture2D> m_shadowFarTexture;
    ComPtr<ID3D11ShaderResourceView> m_shadowFarSrv;
    ComPtr<ID3D11UnorderedAccessView> m_shadowFarUav;

    ComPtr<ID3D11VertexShader> m_fullscreenVs;
    ComPtr<ID3D11PixelShader> m_cloudPs;
    ComPtr<ID3D11PixelShader> m_noiseLabPs;
    ComPtr<ID3D11VertexShader> m_sceneVs;
    ComPtr<ID3D11PixelShader> m_scenePs;
    ComPtr<ID3D11PixelShader> m_toneMapPs;
    ComPtr<ID3D11ComputeShader> m_noiseBaseCs;
    ComPtr<ID3D11ComputeShader> m_noiseDetailCs;
    ComPtr<ID3D11ComputeShader> m_deepShadowCs;
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
    ComPtr<ID3D11Buffer> m_shadowCb;
    ComPtr<ID3D11Buffer> m_stage14Cb;
    ComPtr<ID3D11Buffer> m_sceneCb;
    ComPtr<ID3D11Buffer> m_sceneVertexBuffer;
    ComPtr<ID3D11Buffer> m_sceneIndexBuffer;
    std::uint32_t m_sceneIndexCount = 0;
    std::array<std::uint64_t, 8> m_constantBufferUploadHashes = {};
    std::array<bool, 8> m_constantBufferUploadValid = {};
    std::uint64_t m_constantBufferUploadCount = 0;

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
    AtmosphereParameters m_atmosphereParameters;
    GroundLightingParameters m_groundLightingParameters;
    ToneMappingParameters m_toneMappingParameters;
    stage14::GpuParameters m_stage14GpuParameters = {};
    AtmosphereLutSet m_atmosphereLuts;
    std::uint64_t m_atmosphereLutHashes[6] = {};
    std::uint64_t m_atmosphereLutGenerations[6] = {};
    bool m_atmosphereLutsValid = false;
    std::string m_atmosphereStatus = "Not generated";

    Stage5WeatherPreset m_weatherPreset = Stage5WeatherPreset::ChannelDebug;
    Stage6SunPreset m_sunPreset = Stage6SunPreset::Custom;
    Stage7PhasePreset m_phasePreset = Stage7PhasePreset::Off;
    Stage8EnvironmentPreset m_environmentPreset =
        Stage8EnvironmentPreset::Balanced;
    CloudTypeMode m_cloudTypeMode = CloudTypeMode::WeatherMap;
    std::filesystem::path m_cloudFormationPresetRoot;
    CloudFormationPresetTarget m_cloudFormationTarget =
        ConceptFormationTarget(CloudFormationConcept::UrbanFairWeather);
    bool m_cloudFormationTargetValid = false;
    CloudFormationPresetSource m_cloudFormationSource =
        CloudFormationPresetSource::BuiltIn;
    bool m_hasSavedCustomFormation = false;
    std::string m_cloudFormationStatus = "No formation target";

    float m_cameraMoveSpeedMetersPerSecond =
        stage13scene::kMoveSpeedMetersPerSecond;
    NoiseVolumeParameters m_noiseVolumeParameters;
    CloudShapeParameters m_cloudShapeParameters;
    Stage12ShadowParameters m_shadowParameters;
    WeatherMapGeneratorSettings m_weatherGeneratorSettings;
    WeatherMapData m_currentWeatherMapData;
    float m_previousAtmosphereTimeSeconds = 0.0f;
    bool m_previousAtmosphereTimeValid = false;
    std::uint64_t m_baseNoiseVolumeHash = 0;
    std::uint64_t m_detailNoiseVolumeHash = 0;
    double m_noiseVolumeGenerationMilliseconds = 0.0;
    std::uint64_t m_weatherMapHash = 0;
    std::uint64_t m_weatherUploadCount = 0;
    std::string m_weatherMapStatus = "Not generated";
    Stage15ConceptPreset m_stage15ConceptPreset =
        Stage15ConceptPreset::UrbanFairWeather;

    std::wstring m_shaderDir;
    std::wstring m_fullscreenShaderPath;
    std::wstring m_cloudShaderPath;
    std::wstring m_noiseLabShaderPath;
    std::wstring m_sceneShaderPath;
    std::wstring m_noiseVolumeShaderPath;
    std::wstring m_deepShadowShaderPath;
    std::wstring m_atmosphereLutShaderPath;
    std::wstring m_toneMapShaderPath;
    bool m_noiseVolumesEnabled = true;
    bool m_automatedRenderMode = false;
    bool m_failNextCloudFormationApplyForValidation = false;
    std::uint64_t m_shaderCompileCallCount = 0;
    std::uint64_t m_shaderCacheHitCount = 0;
    std::map<std::wstring, std::filesystem::file_time_type> m_shaderWriteTimes;
    std::vector<shaderreload::Program> m_shaderManifest;
    shaderreload::ReloadReport m_lastShaderReloadReport;
    std::chrono::steady_clock::time_point m_lastShaderScanTime = {};
    std::uint64_t m_shaderGeneration = 0;
    std::uint64_t m_cloudShaderHash = 0;
    std::uint64_t m_deepShadowShaderHash = 0;
    std::string m_shaderStatus = "Not compiled";
    std::string m_shaderError;

    bool m_captureFrameHashes = false;
    bool m_renderNoiseLabPreviews = true;
    bool m_vsyncEnabled = true;
    bool m_tearingSupported = false;
    bool m_renderOpaqueSceneForTest = true;
    std::uint64_t m_lastCloudFrameHash = 0;
    std::string m_adapterName = "Unknown adapter";
    std::string m_driverVersion = "Unavailable";
    FrameProfiler m_frameProfiler;
    NoiseLab m_noiseLab;
};
