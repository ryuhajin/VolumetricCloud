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

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "AtmosphereParameters.h"
#include "CloudDomainParameters.h"
#include "CloudFormationPresetStore.h"
#include "LightingPresetStore.h"
#include "CloudFormationSettings.h"
#include "CloudMotionParameters.h"
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
#include "WeatherColumnParameters.h"

class Camera;
struct DirectionalLightingFrame;

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
    bool SaveCustomFormation(); // 역사적 검증용 Custom 복사
    bool SaveSelectedFormation();
    bool SaveSelectedLighting();
    LightingPresetSettings CurrentLightingPreset() const;
    void LoadUserPresetDefaults();
    bool RunPresetSlotDiagnostics(Camera& camera, bool capture);
    bool RunCloudClarityDiagnostics(Camera& camera);

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
        return m_weatherDefinition.generator;
    }
    std::uint64_t WeatherMapHash() const { return m_weatherMapHash; }
    std::uint64_t WeatherUploadCount() const { return m_weatherUploadCount; }
    std::uint64_t WeatherGenerationCount() const { return m_weatherMapGeneration; }
    double WeatherGenerationMilliseconds() const
    { return m_weatherMapGenerationMilliseconds; }
    const CloudMotionParameters& CloudMotion() const { return m_cloudMotion; }
    const CloudTypeSelection& CloudTypeSelectionState() const
    { return m_cloudTypeSelection; }
    bool ValidateWeatherMapCpuParity() const;
    std::size_t WeatherMapCpuMismatchCount() const;
    std::string WeatherMapCpuParitySummary() const;
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
    void SetCloudTimeForValidation(float timeSeconds)
    {
        m_noiseLab.SetCloudTimeForValidation(timeSeconds);
    }
    float CloudTimeForValidation() const
    {
        return m_noiseLab.EffectiveTime();
    }
    float CloudMovementSpeedForValidation() const
    {
        return m_cloudMotion.speedMetersPerSecond;
    }
    void SetCloudMovementSpeedForValidation(float metersPerSecond)
    {
        m_cloudMotion.speedMetersPerSecond = std::clamp(
            std::isfinite(metersPerSecond) ? metersPerSecond : 0.0f,
            0.0f, 400.0f);
    }
    void SetCloudMotionForValidation(const CloudMotionParameters& motion)
    {
        m_cloudMotion = SanitizeCloudMotionParameters(motion);
    }
    bool ValidateNoiseLabPreviews();
    bool ExportNoiseLabSnapshot(const std::filesystem::path& root);
    // 명시적 테스트 실행에서만 사용. 일반 UI/렌더 preset에는 촬영 경로를 추가하지 않는다.
    bool RunDirectionalLightingBaseline(Camera& camera,
        const std::filesystem::path& root, bool verifyOnly, bool finalApproved = false);
    bool RunDirectionalLightingDiagnostics(Camera& camera);
    bool RunDensityShapingDiagnostics(Camera& camera);
    bool RunBaseOctaveDiagnostics(Camera& camera);
    bool RunLightingTuningDiagnostics(Camera& camera);
    bool RunSolarOcclusionDiagnostics(Camera& camera);
    bool RunSolarBandingDiagnostics(Camera& camera);
    bool RunSolarTransitionDiagnostics(Camera& camera);
    bool RunRimLightingDiagnostics(Camera& camera);
    bool RunRimBoundaryDiagnostics(Camera& camera);
    bool RunFarHeightStability(Camera& camera, const std::filesystem::path& root);
    bool SetRimPhaseCapForValidation(float cap);
    bool SetShadowHeightRefinementForValidation(bool enabled, bool farOnly = false);
    bool SetBaseOctaveExtraForValidation(float extra)
    {
        const float previous = m_noiseVolumeParameters.baseMidOctaveExtra;
        m_noiseVolumeParameters.baseMidOctaveExtra = SanitizeBaseMidOctaveExtra(extra);
        if (RegenerateNoiseVolumes()) return true;
        m_noiseVolumeParameters.baseMidOctaveExtra = previous;
        return false;
    }
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
    // 테스트 전용. Init 이전에 켜야 compile/cache provenance도 수집한다.
    void EnableDeterminismValidation(bool detailed = false)
    { m_determinismValidation = true; m_determinismDetailed = detailed; }
    const std::string& DeterminismFrameReport() const { return m_determinismFrameReport; }
    const std::string& DeterminismShaderReport() const { return m_determinismShaderReport; }
    bool DeterminismCaptureValid() const { return m_determinismCaptureValid; }
    bool ValidateDeterminismTransitions(Camera& camera);
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
        // [파생 값] CPU transpose된 역 view-projection. 화면 UV/device depth [0,1] → 월드 위치(m). 직접 수정하면 깊이와 구름 가림이 어긋난다.
        DirectX::XMFLOAT4X4 invViewProj;
        // [파생 값] 역 투영 행렬. NDC 방향 → view ray; 큰 월드에서 translation 없이 정밀한 레이를 복원.
        DirectX::XMFLOAT4X4 invProjection;
        // [파생 값] 역 view 회전 행렬. view 방향 → 월드 방향, w=0으로 translation 제외.
        DirectX::XMFLOAT4X4 invViewRotation;
        // [파생 값] xyz 카메라 월드 m, ray 시작점. F5~F8/이동에서 생성.
        DirectX::XMFLOAT3 cameraPos;
        // [파생 값] 유효 구름 시간 s. 일반 실행은 실제 delta 누적, 테스트는 고정 입력; Weather/Base/Shadow에 동일 값.
        float time;
        // [파생 값] xy=전체 화면 가로/세로 pixel, 각각 >=1. Full-resolution ray/LUT 계약.
        DirectX::XMFLOAT2 renderSize;
        // [파생 값] 카메라 near clip 거리 m, 양수. 깊이/투영 계약에서 생성.
        float nearPlane;
        // [파생 값] 카메라 far clip 거리 m, near보다 큼. 하늘 ray 외부 한계; 구름 최대 거리는 b5도 제한.
        float farPlane;
    };

    struct SceneCB
    {
        // [파생 값] CPU transpose된 view-projection. 월드 m → clip 좌표; 같은 b0라도 CameraCB와 다른 SceneCB 64B.
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
    float m_rimComparisonCap = 2.5f; // 명시적인 비교 실행만 변경, 일반 기본값2.5.
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
        const WeatherMapGeneratorSettings& settings);
    bool GenerateWeatherMapTexture(
        ID3D11ComputeShader* shader, Stage5WeatherPreset preset,
        const WeatherMapGeneratorSettings& settings,
        ComPtr<ID3D11Texture2D>& generatedTexture,
        ComPtr<ID3D11UnorderedAccessView>& generatedUav,
        WeatherMapData& generatedData, std::uint64_t& generatedHash,
        double& generationMilliseconds);
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
    void DispatchDeepShadowCaches(); // 준비된 CB로 적분. 진단은 입력을 고정해 재사용한다.
    // true는 테스트 타일 Draw용 binding만 준비한다. 호출자가 Draw/Unbind를 완료해야 한다.
    void RenderCloudPass(bool prepareOnlyForValidation = false);
    void UpdateCloudConstantBuffers(
        const Camera& camera, float timeSeconds);
    void UnbindCloudShaderResources(UINT count);
    void CaptureCloudFrameHash();
    void CaptureDirectionalLightingFrame();
    void CaptureDeterminismFrame();
    void RecordDeterminismShader(const std::wstring& path, const char* entry,
        const char* target, UINT flags, const std::string& key, ID3DBlob* blob);

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
    ComPtr<ID3D11ComputeShader> m_weatherMapCs;
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
    ComPtr<ID3D11Buffer> m_weatherColumnCb;
    ComPtr<ID3D11Buffer> m_shadowCb;
    ComPtr<ID3D11Buffer> m_stage14Cb;
    ComPtr<ID3D11Buffer> m_sceneCb;
    ComPtr<ID3D11Buffer> m_sceneVertexBuffer;
    ComPtr<ID3D11Buffer> m_sceneIndexBuffer;
    std::uint32_t m_sceneIndexCount = 0;
    std::array<std::uint64_t, 9> m_constantBufferUploadHashes = {};
    std::array<bool, 9> m_constantBufferUploadValid = {};
    std::uint64_t m_constantBufferUploadCount = 0;

    ComPtr<ID3D11DepthStencilState> m_depthState;
    ComPtr<ID3D11RasterizerState> m_rasterizerState;
    ComPtr<ID3D11SamplerState> m_pointClampSampler;
    ComPtr<ID3D11SamplerState> m_linearClampSampler;
    ComPtr<ID3D11SamplerState> m_weatherLinearWrapSampler;

    ComPtr<ID3D11Texture2D> m_weatherMapTexture;
    ComPtr<ID3D11ShaderResourceView> m_weatherMapSrv;
    ComPtr<ID3D11Texture2D> m_weatherMapScratchTexture;
    ComPtr<ID3D11UnorderedAccessView> m_weatherMapScratchUav;
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
    CloudTypeSelection m_cloudTypeSelection = {};
    CloudMotionParameters m_cloudMotion = {};
    std::filesystem::path m_cloudFormationPresetRoot;
    CloudFormationPresetTarget m_cloudFormationTarget =
        ConceptFormationTarget(CloudFormationConcept::UrbanFairWeather);
    bool m_cloudFormationTargetValid = false;
    CloudFormationPresetSource m_cloudFormationSource =
        CloudFormationPresetSource::BuiltIn;
    CloudFormationPresetSource m_lightingSource = CloudFormationPresetSource::BuiltIn;
    std::string m_lightingStatus = "Built-in";
    bool m_useSavedPresets = false;
    bool m_hasSavedCustomFormation = false;
    std::string m_cloudFormationStatus = "No formation target";

    float m_cameraMoveSpeedMetersPerSecond =
        stage13scene::kMoveSpeedMetersPerSecond;
    NoiseVolumeParameters m_noiseVolumeParameters;
    CloudShapeParameters m_cloudShapeParameters;
    Stage12ShadowParameters m_shadowParameters;
    // 05-B 비교 실행에서만 사용. 일반 Balanced512 기본 규격/Custom과 분리한다.
    bool m_shadowHeightRefinementForValidation = false;
    bool m_shadowFarOnlyRefinementForValidation = false;
    WeatherMapDefinition m_weatherDefinition;
    WeatherColumnParameters m_weatherColumnParameters;
    WeatherMapData m_currentWeatherMapData;
    float m_previousAtmosphereTimeSeconds = 0.0f;
    bool m_previousAtmosphereTimeValid = false;
    std::uint64_t m_baseNoiseVolumeHash = 0;
    std::uint64_t m_detailNoiseVolumeHash = 0;
    double m_noiseVolumeGenerationMilliseconds = 0.0;
    double m_weatherMapGenerationMilliseconds = 0.0;
    std::uint64_t m_weatherMapGeneration = 0;
    std::uint64_t m_weatherMapHash = 0;
    std::uint64_t m_weatherGenerationKey = 0;
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
    std::wstring m_weatherMapShaderPath;
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
    DirectionalLightingFrame* m_directionalCaptureTarget = nullptr;
    bool m_determinismValidation = false;
    bool m_determinismDetailed = false;
    bool m_determinismCaptureValid = false;
    std::string m_determinismFrameReport;
    std::string m_determinismShaderReport;
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
