// ============================================================================
//  Renderer.h - Direct3D 11 단계 4 Base/Detail Erosion 렌더링
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
#include "NoiseLab.h"

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
    bool HasDebugLayerErrors() const;
    bool HandleWindowMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    bool ValidateNoiseLabPreviews();
    bool ExportNoiseLabSnapshot(const std::filesystem::path& root);
    void SetNoiseLabOutputMode(NoiseOutputMode mode) { m_noiseLab.SetOutputMode(mode); }
    std::uint64_t NoiseLabPreviewHash(std::size_t targetIndex);
    std::uint64_t ShaderGeneration() const { return m_shaderGeneration; }
    void EnableFrameHashCapture(bool enabled) { m_captureFrameHashes = enabled; }
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
    ComPtr<ID3D11PixelShader> m_cloudPs;
    ComPtr<ID3D11PixelShader> m_noiseLabPs;
    ComPtr<ID3D11VertexShader> m_sceneVs;
    ComPtr<ID3D11PixelShader> m_scenePs;
    ComPtr<ID3D11InputLayout> m_sceneInputLayout;

    ComPtr<ID3D11Buffer> m_cameraCb;
    ComPtr<ID3D11Buffer> m_cloudCb;
    ComPtr<ID3D11Buffer> m_sceneCb;
    ComPtr<ID3D11Buffer> m_sceneVertexBuffer;
    ComPtr<ID3D11Buffer> m_sceneIndexBuffer;
    std::uint32_t m_sceneIndexCount = 0;

    ComPtr<ID3D11DepthStencilState> m_depthState;
    ComPtr<ID3D11RasterizerState> m_rasterizerState;
    ComPtr<ID3D11SamplerState> m_pointClampSampler;

    CloudParameters m_cloudParameters;
    Stage1ValidationPreset m_validationPreset = Stage1ValidationPreset::WideVolume;
    Stage2NoisePreset m_noisePreset = Stage2NoisePreset::DefaultNoise;
    Stage4DetailPreset m_detailPreset = Stage4DetailPreset::DefaultDetail;

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
    std::uint64_t m_lastCloudFrameHash = 0;
    NoiseLab m_noiseLab;
};
