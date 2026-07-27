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
    unsigned int RuntimeCompileCount() const { return m_runtimeCompileCount; }
    unsigned int NoiseDispatchCount() const { return m_noiseDispatchCount; }

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
    void GenerateNoiseVolumes();
    void RenderNoisePreview(float previewTime);
    bool CreateGpuTimerResources();
    void BeginGpuTimer();
    void EndGpuTimer();
    void BeginGpuTotalTimer();
    void EndGpuTotalTimer();
    void ResolveGpuTotalTimer();
    void CheckShaderHotReload();
    void UpdateShaderWriteTimes();

    // 셰이더 상수버퍼 — HLSL cbCamera 와 레이아웃이 정확히 일치해야 함 (112 바이트)
    struct CameraCB
    {
        DirectX::XMFLOAT4X4 invViewProj;  // 64
        DirectX::XMFLOAT3   cameraPos;    // 12
        float               time;         //  4
        DirectX::XMFLOAT3   volumeCenter; // 12
        float               densityScale; //  4
        DirectX::XMFLOAT3   volumeHalfSize;// 12
        float               _pad;         //  4
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
    ComPtr<ID3D11Buffer>           m_cb;
    ComPtr<ID3D11Buffer>           m_cloudCb;
    ComPtr<ID3D11Buffer>           m_previewCb;

    ComPtr<ID3D11VertexShader>     m_previewVs;
    ComPtr<ID3D11PixelShader>      m_previewPs;
    ComPtr<ID3D11ComputeShader>    m_noiseVolumeCsBase;   // RGBA8 base 채널 굽기
    ComPtr<ID3D11ComputeShader>    m_noiseVolumeCsDetail; // RGBA8 옥타브 굽기
    ComPtr<ID3D11ComputeShader>    m_noiseWeatherCs;      // RGBA8 weather map 굽기
    ComPtr<ID3D11SamplerState>     m_noiseSampler;
    ComPtr<ID3D11Buffer>           m_noiseVolumeGenerationCb;
    std::array<ComPtr<ID3D11Texture2D>, 4> m_previewTextures;
    std::array<ComPtr<ID3D11RenderTargetView>, 4> m_previewRtvs;
    std::array<ComPtr<ID3D11ShaderResourceView>, 4> m_previewSrvs;
    std::array<ComPtr<ID3D11Texture3D>, 2> m_noiseVolumes;
    std::array<ComPtr<ID3D11UnorderedAccessView>, 2> m_noiseVolumeUavs;
    std::array<ComPtr<ID3D11ShaderResourceView>, 2> m_noiseVolumeSrvs;
    WeatherMapResources m_weatherMap;

    std::array<ComPtr<ID3D11Query>, 2> m_gpuBeginQueries;
    std::array<ComPtr<ID3D11Query>, 2> m_gpuEndQueries;
    bool m_gpuTimerActive = false;
    float m_gpuFrameMs = 0.0f;
    std::array<ComPtr<ID3D11Query>, 2> m_gpuTotalDisjointQueries;
    std::array<ComPtr<ID3D11Query>, 2> m_gpuTotalBeginQueries;
    std::array<ComPtr<ID3D11Query>, 2> m_gpuTotalEndQueries;
    std::array<bool, 2> m_gpuTotalQueryIssued = { false, false };
    unsigned int m_gpuTotalQueryIndex = 0;
    bool m_gpuTotalTimerActive = false;
    float m_gpuTotalMs = 0.0f;
    float m_cpuRenderMs = 0.0f;
    float m_frameIntervalMs = 0.0f;

    CloudParameters m_cloudParams = CumulusWideShowcaseCloudParameters();
    NoisePreviewSettings m_previewSettings;
    DebugUI m_debugUI;
    bool m_previewDirty = true;
    bool m_noiseCacheDirty = true;
    bool m_cacheLoaded = false;
    bool m_sourceModified = false;
    bool m_hasPresented = false;
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
    std::wstring m_previewPath;
    std::wstring m_noiseVolumeCsPath;
    std::vector<std::wstring> m_shaderPaths;
    std::vector<std::filesystem::file_time_type> m_shaderWriteTimes;
};
