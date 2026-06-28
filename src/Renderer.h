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
#include <string>

class Camera;

class Renderer
{
public:
    bool Init(HWND hwnd, int width, int height);
    void Resize(int width, int height);
    void Render(const Camera& camera, float timeSeconds);

private:
    // HLSL 파일을 런타임 컴파일 (showErrors=true면 실패 시 메시지박스 + false)
    bool CompileShaderFromFile(const std::wstring& path,
                               const char* entryPoint,
                               const char* target,
                               Microsoft::WRL::ComPtr<ID3DBlob>& outBlob,
                               bool showErrors);
    bool CreateShaders(bool showErrors);
    bool CreateRenderTarget();
    void CheckShaderHotReload();
    void UpdateShaderWriteTimes();
    bool GetShaderWriteTimes(std::filesystem::file_time_type& vsTime,
                             std::filesystem::file_time_type& psTime,
                             std::filesystem::file_time_type& intersectionsTime) const;

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

    std::wstring m_shaderDir; // 개발 중 소스 shaders/ 우선, 없으면 실행 파일 옆 shaders/ 경로
    std::wstring m_vsPath;
    std::wstring m_psPath;
    std::wstring m_intersectionsPath;
    std::filesystem::file_time_type m_vsWriteTime = {};
    std::filesystem::file_time_type m_psWriteTime = {};
    std::filesystem::file_time_type m_intersectionsWriteTime = {};
};
