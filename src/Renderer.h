// ============================================================================
//  Renderer.h - Direct3D 11 단계 0 렌더링 기반
// ============================================================================
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

#include "CloudParameters.h"

class Camera;

struct DiagnosticSceneVertex
{
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 color;
};

class Renderer
{
public:
    bool Init(HWND hwnd, int width, int height);
    void Resize(int width, int height);
    void Render(const Camera& camera, float timeSeconds);
    void SetDebugMode(CloudDebugMode mode);
    CloudDebugMode DebugMode() const;
    bool HasDebugLayerErrors() const;

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
    void RenderFoundationPass(const Camera& camera, float timeSeconds);
    void CheckShaderHotReload();
    void UpdateShaderWriteTimes();
    bool GetShaderWriteTimes(
        std::array<std::filesystem::file_time_type, 3>& writeTimes) const;

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
    ComPtr<ID3D11PixelShader> m_foundationPs;
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

    std::wstring m_shaderDir;
    std::array<std::wstring, 3> m_shaderPaths;
    std::array<std::filesystem::file_time_type, 3> m_shaderWriteTimes = {};
};
