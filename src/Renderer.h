// ============================================================================
//  Renderer.h - Direct3D 11 단계 9 최적화·성능 계측 렌더링
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

#include "CloudParameters.h"
#include "EnvironmentParameters.h"
#include "FrameProfiler.h"
#include "LightParameters.h"
#include "NoiseLab.h"
#include "OptimizationParameters.h"
#include "WeatherMap.h"

class Camera;

struct DiagnosticSceneVertex
{
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 color;
};

class Renderer
{
public:
    ~Renderer();
    bool Init(HWND hwnd, int width, int height);
    void Resize(int width, int height);
    void Render(const Camera& camera, float timeSeconds);
    void SetDebugMode(CloudDebugMode mode);
    CloudDebugMode DebugMode() const;
    void ApplyStage1ValidationPreset(Stage1ValidationPreset preset);
    Stage1ValidationPreset ValidationPreset() const;
    void ApplyStage2NoisePreset(Stage2NoisePreset preset);
    Stage2NoisePreset NoisePreset() const;
    void SetHeightProfile(float bottomFadeEnd, float topFadeStart);
    void ApplyStage4DetailPreset(Stage4DetailPreset preset);
    Stage4DetailPreset DetailPreset() const;
    bool ApplyStage5WeatherPreset(Stage5WeatherPreset preset);
    bool ApplyWeatherGeneratorSettings(
        const WeatherMapGeneratorSettings& settings);
    Stage5WeatherPreset WeatherPreset() const { return m_weatherPreset; }
    void ApplyStage6SunPreset(Stage6SunPreset preset);
    void ApplyStage7PhasePreset(Stage7PhasePreset preset);
    void ApplyStage8EnvironmentPreset(Stage8EnvironmentPreset preset);
    void ApplyStage9OptimizationPreset(Stage9OptimizationPreset preset);
    void SetLightSampling(std::uint32_t maxSteps, float stepSize);
    void SetViewSamplingForSmoke(std::uint32_t maxSteps, float stepSize);
    Stage6SunPreset SunPreset() const { return m_sunPreset; }
    Stage7PhasePreset PhasePreset() const { return m_phasePreset; }
    Stage8EnvironmentPreset EnvironmentPreset() const { return m_environmentPreset; }
    Stage9OptimizationPreset OptimizationPreset() const { return m_optimizationPreset; }
    const OptimizationParameters& OptimizationSettings() const { return m_optimizationParameters; }
    const LightParameters& LightSettings() const { return m_lightParameters; }
    const EnvironmentParameters& EnvironmentSettings() const { return m_environmentParameters; }
    std::uint64_t WeatherMapHash() const { return m_weatherMapHash; }
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
    bool ValidateNoiseLabPreviews();
    bool ExportNoiseLabSnapshot(const std::filesystem::path& root);
    void SetNoiseLabOutputMode(NoiseOutputMode mode) { m_noiseLab.SetOutputMode(mode); }
    std::uint64_t NoiseLabPreviewHash(std::size_t targetIndex);
    std::uint64_t ShaderGeneration() const { return m_shaderGeneration; }
    void EnableFrameHashCapture(bool enabled) { m_captureFrameHashes = enabled; }
    void EnableNoiseLabPreviews(bool enabled) { m_renderNoiseLabPreviews = enabled; }
    void SetNoiseLabVisible(bool visible) { m_noiseLab.SetVisible(visible); }
    void SetVSyncEnabled(bool enabled) { m_vsyncEnabled = enabled; }
    bool VSyncEnabled() const { return m_vsyncEnabled; }
    const FrameTimingSnapshot& TimingSnapshot() const
    {
        return m_frameProfiler.Snapshot();
    }
    std::uint64_t LastCloudFrameHash() const { return m_lastCloudFrameHash; }

private:
    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    struct CameraCB
    {
        DirectX::XMFLOAT4X4 invViewProj;
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

    static_assert(sizeof(CameraCB) == 96, "CameraCB must match cbCamera");
    static_assert(sizeof(SceneCB) == 64, "SceneCB must match cbScene");

    bool CompileShaderFromFile(const std::wstring& path,
                               const char* entryPoint,
                               const char* target,
                               ComPtr<ID3DBlob>& outBlob,
                               bool showErrors);
    bool CreateShaders(bool showErrors);
    bool CreateBackBufferTarget();
    bool CreateSceneTargets();
    bool CreateDiagnosticScene();
    bool CreatePipelineStates();
    bool CreateConstantBuffers();
    bool CreateWeatherMapTexture(Stage5WeatherPreset preset);
    bool UpdateWeatherMapTexture(
        Stage5WeatherPreset preset,
        const WeatherMapGeneratorSettings& settings);
    void ReleaseSizeDependentResources();
    void RenderDiagnosticScene(const Camera& camera);
    void RenderCloudPass(const Camera& camera, float timeSeconds);
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

    ComPtr<ID3D11VertexShader> m_fullscreenVs;
    ComPtr<ID3D11PixelShader> m_cloudLegacyPs;
    ComPtr<ID3D11PixelShader> m_cloudOptimizedPs;
    ComPtr<ID3D11PixelShader> m_noiseLabPs;
    ComPtr<ID3D11VertexShader> m_sceneVs;
    ComPtr<ID3D11PixelShader> m_scenePs;
    ComPtr<ID3D11InputLayout> m_sceneInputLayout;

    ComPtr<ID3D11Buffer> m_cameraCb;
    ComPtr<ID3D11Buffer> m_cloudCb;
    ComPtr<ID3D11Buffer> m_lightCb;
    ComPtr<ID3D11Buffer> m_environmentCb;
    ComPtr<ID3D11Buffer> m_optimizationCb;
    ComPtr<ID3D11Buffer> m_sceneCb;
    ComPtr<ID3D11Buffer> m_sceneVertexBuffer;
    ComPtr<ID3D11Buffer> m_sceneIndexBuffer;
    std::uint32_t m_sceneIndexCount = 0;

    ComPtr<ID3D11DepthStencilState> m_depthState;
    ComPtr<ID3D11RasterizerState> m_rasterizerState;
    ComPtr<ID3D11SamplerState> m_pointClampSampler;
    ComPtr<ID3D11SamplerState> m_weatherLinearWrapSampler;
    ComPtr<ID3D11Texture2D> m_weatherMapTexture;
    ComPtr<ID3D11ShaderResourceView> m_weatherMapSrv;

    CloudParameters m_cloudParameters;
    LightParameters m_lightParameters;
    EnvironmentParameters m_environmentParameters;
    OptimizationParameters m_optimizationParameters;
    Stage1ValidationPreset m_validationPreset = Stage1ValidationPreset::DefaultVolume;
    Stage2NoisePreset m_noisePreset = Stage2NoisePreset::DefaultNoise;
    Stage4DetailPreset m_detailPreset = Stage4DetailPreset::DefaultDetail;
    Stage5WeatherPreset m_weatherPreset = Stage5WeatherPreset::ChannelDebug;
    Stage6SunPreset m_sunPreset = Stage6SunPreset::Custom;
    Stage7PhasePreset m_phasePreset = Stage7PhasePreset::Off;
    Stage8EnvironmentPreset m_environmentPreset = Stage8EnvironmentPreset::Balanced;
    Stage9OptimizationPreset m_optimizationPreset = Stage9OptimizationPreset::Balanced;
    WeatherMapGeneratorSettings m_weatherGeneratorSettings;
    std::uint64_t m_weatherMapHash = 0;
    std::string m_weatherMapStatus = "Not generated";

    std::wstring m_shaderDir;
    std::wstring m_fullscreenShaderPath;
    std::wstring m_cloudShaderPath;
    std::wstring m_noiseLabShaderPath;
    std::wstring m_sceneShaderPath;
    std::map<std::wstring, std::filesystem::file_time_type> m_shaderWriteTimes;
    std::uint64_t m_shaderGeneration = 0;
    std::string m_shaderStatus = "Not compiled";
    std::string m_shaderError;
    bool m_captureFrameHashes = false;
    bool m_renderNoiseLabPreviews = true;
    bool m_vsyncEnabled = true;
    std::uint64_t m_lastCloudFrameHash = 0;
    FrameProfiler m_frameProfiler;
    NoiseLab m_noiseLab;
};
