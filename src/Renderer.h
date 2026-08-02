// ============================================================================
//  Renderer.h  —  Direct3D 11 렌더러
// ----------------------------------------------------------------------------
//  device / context / swapchain / render target view 를 만들고,
//  런타임에 HLSL 셰이더를 컴파일한 뒤, 매 프레임 풀스크린 삼각형 1개를 그려
//  픽셀 셰이더로 박스 볼륨을 레이마칭한다.
// ============================================================================
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl/client.h> // Microsoft::WRL::ComPtr
#include <filesystem>
#include <array>
#include <string>
#include <vector>

#include "CloudParameters.h"
#include "DebugUI.h"
#include "NoiseCacheManager.h"

class Camera;

struct BenchmarkFrameSample
{
    float gpuCloudMs = 0.0f;
    float gpuReconstructionMs = 0.0f;
    float gpuTotalMs = 0.0f;
    float cpuRenderMs = 0.0f;
};

class Renderer
{
public:
    ~Renderer();
    bool Init(HWND hwnd, int width, int height, bool forceRebuildCache = false);
    void Resize(int width, int height);
    void Render(const Camera& camera, float timeSeconds);
    bool HandleWindowMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    bool WantsMouseCapture() const;
    bool WantsKeyboardCapture() const;
    void ToggleDebugUI();
    void ToggleTelemetry();
    bool SaveDefaultNoiseCache();
    bool RunCodeTests();
    void SetBenchmarkMode(bool enabled);
    void SetTemporalEnabled(bool enabled);
    bool TemporalEnabled() const { return m_temporalEnabled; }
    void ConfigureViewStepBenchmark(int viewSteps, float cloudThickness);
    void BeginBenchmarkCollection();
    void EndBenchmarkCollection();
    const std::vector<BenchmarkFrameSample>& BenchmarkSamples() const { return m_benchmarkSamples; }
    unsigned int RuntimeCompileCount() const { return m_runtimeCompileCount; }
    unsigned int NoiseDispatchCount() const { return m_noiseDispatchCount; }
    float FrameIntervalMs() const { return m_frameIntervalMs; }
    float CpuRenderMs() const { return m_cpuRenderMs; }
    float PresentWaitMs() const { return m_presentWaitMs; }
    float GpuCloudMs() const { return m_gpuFrameMs; }
    float GpuReconstructionMs() const { return m_gpuReconstructionMs; }
    float GpuTotalMs() const { return m_gpuTotalMs; }
    const std::string& CacheStatus() const { return m_cacheStatus; }

private:
    // HLSL 파일을 런타임 컴파일 (showErrors=true면 실패 시 메시지박스 + false)
    bool CompileShaderFromFile(const std::wstring& path,
                               const char* entryPoint,
                               const char* target,
                               Microsoft::WRL::ComPtr<ID3DBlob>& outBlob,
                               bool showErrors);
    bool CreateShaders(bool showErrors);
    bool CreateShadersFromBlobs(const ShaderBlobArray& blobs);
    bool CreateRenderTarget();
    bool CreateNoisePreviewResources();
    bool CreateNoiseVolumeResources();
    bool CreateNoiseSampler();
    bool CreateTemporalResources();
    void ResetTemporalHistory();
    void RenderFullscreenTriangle();
    void GenerateNoiseVolumes();
    void RenderNoisePreview(float previewTime);
    bool CreateGpuTimerResources();
    void BeginGpuTimer();
    void EndGpuTimer();
    void BeginReconstructionTimer();
    void EndReconstructionTimer();
    void BeginGpuTotalTimer();
    void EndGpuTotalTimer();
    void ResolveGpuTotalTimer();
    void CheckShaderHotReload();
    void UpdateShaderWriteTimes();

    // HLSL cbCamera와 정확히 일치하는 112바이트 상수 버퍼.
    struct CameraCB
    {
        DirectX::XMFLOAT4X4 invViewProj;
        DirectX::XMFLOAT3 cameraPos;
        float time;
        DirectX::XMFLOAT2 rayJitterNdc;
        DirectX::XMFLOAT2 renderSize;
        unsigned int temporalOutput;
        unsigned int _pad[3];
    };

    struct TemporalCB
    {
        DirectX::XMFLOAT4X4 previousViewProj;
        DirectX::XMFLOAT3 previousCameraPos;
        float previousTime;
        DirectX::XMFLOAT2 windDeltaWorld;
        float historyWeight;
        unsigned int historyValid;
        unsigned int temporalDebugMode;
        unsigned int _pad[3];
    };

    struct NoisePreviewCB
    {
        int   axis;
        float slice;
        float previewTime;
        int   source;
    };

    struct NoiseVolumeGenerationCB
    {
        unsigned int volumeSize;
        unsigned int generationKind;
        unsigned int _pad[2];
    };

    static_assert(sizeof(NoisePreviewCB) == 16);
    static_assert(sizeof(NoiseVolumeGenerationCB) == 16);
    static_assert(sizeof(CameraCB) == 112);
    static_assert(sizeof(TemporalCB) == 112);

    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    int m_width  = 0;
    int m_height = 0;

    ComPtr<ID3D11Device>           m_device;
    ComPtr<ID3D11DeviceContext>    m_context;
    ComPtr<IDXGISwapChain>         m_swapChain;
    ComPtr<ID3D11RenderTargetView> m_rtv;

    ComPtr<ID3D11VertexShader>     m_vs;
    ComPtr<ID3D11PixelShader>      m_ps;
    ComPtr<ID3D11PixelShader>      m_temporalPs;
    ComPtr<ID3D11PixelShader>      m_compositePs;
    ComPtr<ID3D11Buffer>           m_cb;
    ComPtr<ID3D11Buffer>           m_cloudCb;
    ComPtr<ID3D11Buffer>           m_temporalCb;
    ComPtr<ID3D11Buffer>           m_previewCb;

    ComPtr<ID3D11VertexShader>     m_previewVs;
    ComPtr<ID3D11PixelShader>      m_previewPs;
    ComPtr<ID3D11ComputeShader>    m_noiseVolumeCsBase;   // RGBA8 base 채널 굽기
    ComPtr<ID3D11ComputeShader>    m_noiseVolumeCsDetail; // RGBA8 옥타브 굽기
    ComPtr<ID3D11ComputeShader>    m_noiseWeatherCs;      // RGBA8 weather map 굽기
    ComPtr<ID3D11SamplerState>     m_noiseSampler;
    ComPtr<ID3D11SamplerState>     m_linearClampSampler;
    ComPtr<ID3D11Buffer>           m_noiseVolumeGenerationCb;
    std::array<ComPtr<ID3D11Texture2D>, 4> m_previewTextures;
    std::array<ComPtr<ID3D11RenderTargetView>, 4> m_previewRtvs;
    std::array<ComPtr<ID3D11ShaderResourceView>, 4> m_previewSrvs;
    std::array<ComPtr<ID3D11Texture3D>, 2> m_noiseVolumes;
    std::array<ComPtr<ID3D11UnorderedAccessView>, 2> m_noiseVolumeUavs;
    std::array<ComPtr<ID3D11ShaderResourceView>, 2> m_noiseVolumeSrvs;
    WeatherMapResources m_weatherMap;
    PlacementMapResources m_placementMap;

    ComPtr<ID3D11Texture2D> m_halfCloudColor;
    ComPtr<ID3D11RenderTargetView> m_halfCloudColorRtv;
    ComPtr<ID3D11ShaderResourceView> m_halfCloudColorSrv;
    ComPtr<ID3D11Texture2D> m_halfCloudDepth;
    ComPtr<ID3D11RenderTargetView> m_halfCloudDepthRtv;
    ComPtr<ID3D11ShaderResourceView> m_halfCloudDepthSrv;
    std::array<ComPtr<ID3D11Texture2D>, 2> m_historyColor;
    std::array<ComPtr<ID3D11RenderTargetView>, 2> m_historyColorRtv;
    std::array<ComPtr<ID3D11ShaderResourceView>, 2> m_historyColorSrv;
    std::array<ComPtr<ID3D11Texture2D>, 2> m_historyDepth;
    std::array<ComPtr<ID3D11RenderTargetView>, 2> m_historyDepthRtv;
    std::array<ComPtr<ID3D11ShaderResourceView>, 2> m_historyDepthSrv;

    std::array<ComPtr<ID3D11Query>, 2> m_gpuBeginQueries;
    std::array<ComPtr<ID3D11Query>, 2> m_gpuEndQueries;
    std::array<ComPtr<ID3D11Query>, 2> m_gpuReconstructionBeginQueries;
    std::array<ComPtr<ID3D11Query>, 2> m_gpuReconstructionEndQueries;
    bool m_gpuTimerActive = false;
    bool m_gpuReconstructionTimerActive = false;
    float m_gpuFrameMs = 0.0f;
    float m_gpuReconstructionMs = 0.0f;
    std::array<ComPtr<ID3D11Query>, 2> m_gpuTotalDisjointQueries;
    std::array<ComPtr<ID3D11Query>, 2> m_gpuTotalBeginQueries;
    std::array<ComPtr<ID3D11Query>, 2> m_gpuTotalEndQueries;
    std::array<bool, 2> m_gpuTotalQueryIssued = { false, false };
    std::array<bool, 2> m_gpuTotalQueryBenchmark = { false, false };
    std::array<float, 2> m_gpuTotalQueryCpuMs = { 0.0f, 0.0f };
    unsigned int m_gpuTotalQueryIndex = 0;
    int m_lastIssuedGpuQueryIndex = -1;
    bool m_gpuTotalTimerActive = false;
    float m_gpuTotalMs = 0.0f;
    float m_cpuRenderMs = 0.0f;
    float m_presentWaitMs = 0.0f;
    float m_frameIntervalMs = 0.0f;
    bool m_benchmarkMode = false;
    bool m_benchmarkCollecting = false;
    std::vector<BenchmarkFrameSample> m_benchmarkSamples;

    CloudParameters m_cloudParams = CumulusWideShowcaseCloudParameters();
    NoisePreviewSettings m_previewSettings;
    DebugUI m_debugUI;
    bool m_previewDirty = true;
    bool m_noiseCacheDirty = true;
    bool m_cacheLoaded = false;
    bool m_sourceModified = false;
    bool m_hasPresented = false;
    bool m_temporalEnabled = true;
    bool m_historyValid = false;
    unsigned int m_historyIndex = 0;
    unsigned int m_temporalFrameIndex = 0;
    DirectX::XMFLOAT4X4 m_previousViewProj = {};
    DirectX::XMFLOAT3 m_previousCameraPos = {};
    float m_previousCloudTime = 0.0f;
    unsigned int m_runtimeCompileCount = 0;
    unsigned int m_noiseDispatchCount = 0;
    float m_lastFrameTime = 0.0f;
    std::string m_cacheStatus = "No cache";
    NoiseCacheManager m_noiseCacheManager;
    ShaderBlobArray m_shaderBlobs;

    std::wstring m_shaderDir; // 개발 중 소스 shaders/ 우선, 없으면 실행 파일 옆 shaders/ 경로
    std::wstring m_vsPath;
    std::wstring m_psPath;
    std::wstring m_rayLibPath;
    std::wstring m_cloudNoisePath;
    std::wstring m_cloudAtmospherePath;
    std::wstring m_previewPath;
    std::wstring m_noiseVolumeCsPath;
    std::wstring m_temporalPath;
    std::wstring m_compositePath;
    std::vector<std::wstring> m_shaderPaths;
    std::vector<std::filesystem::file_time_type> m_shaderWriteTimes;
};
