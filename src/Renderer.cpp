#include "Renderer.h"
#include "Camera.h"

#include <d3dcompiler.h>
#include <cstring>
#include <vector>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

// 실행 파일이 있는 폴더 경로를 구한다 (셰이더는 그 아래 shaders/ 에 복사됨)
static std::wstring GetExeDir()
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring full(path);
    size_t slash = full.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? L"." : full.substr(0, slash);
}

static std::wstring WidenUtf8(const char* text)
{
    int length = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (length <= 0)
        return L"";

    std::wstring result(static_cast<size_t>(length - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, result.data(), length);
    return result;
}

static bool DirectoryExists(const std::wstring& path)
{
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

static std::wstring ResolveShaderDir()
{
#ifdef VCLOUD_SHADER_SOURCE_DIR
    std::wstring sourceDir = WidenUtf8(VCLOUD_SHADER_SOURCE_DIR);
    if (DirectoryExists(sourceDir))
        return sourceDir + L"\\";
#endif
    return GetExeDir() + L"\\shaders\\";
}

bool Renderer::Init(HWND hwnd, int width, int height)
{
    m_width  = width;
    m_height = height;
    m_shaderDir = ResolveShaderDir();
    m_vsPath = m_shaderDir + L"Fullscreen.hlsl";
    m_psPath = m_shaderDir + L"VolumetricClouds.hlsl";
    m_rayLibPath = m_shaderDir + L"Ray.hlsli";

    // ---- 스왑체인 + 디바이스 + 컨텍스트 생성 ----
    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount       = 2;
    scd.BufferDesc.Width  = width;
    scd.BufferDesc.Height = height;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow      = hwnd;
    scd.SampleDesc.Count  = 1;
    scd.Windowed          = TRUE;
    scd.SwapEffect        = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG; // 디버그 레이어 (검증 메시지)
#endif

    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        &featureLevel, 1, D3D11_SDK_VERSION,
        &scd, &m_swapChain, &m_device, nullptr, &m_context);

    if (FAILED(hr))
    {
        MessageBoxW(hwnd, L"D3D11 디바이스/스왑체인 생성 실패", L"오류", MB_OK | MB_ICONERROR);
        return false;
    }

    if (!CreateRenderTarget()) return false;
    if (!CreateShaders(true))  return false;
    UpdateShaderWriteTimes();

    // ---- 상수버퍼 생성 (매 프레임 갱신할 dynamic 버퍼) ----
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth      = sizeof(CameraCB);
    bd.Usage          = D3D11_USAGE_DYNAMIC;
    bd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = m_device->CreateBuffer(&bd, nullptr, &m_cb);
    if (FAILED(hr))
    {
        MessageBoxW(hwnd, L"상수버퍼 생성 실패", L"오류", MB_OK | MB_ICONERROR);
        return false;
    }

    return true;
}

bool Renderer::CreateRenderTarget()
{
    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) return false;

    hr = m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_rtv);
    return SUCCEEDED(hr);
}

bool Renderer::CompileShaderFromFile(const std::wstring& path,
                                     const char* entryPoint,
                                     const char* target,
                                     ComPtr<ID3DBlob>& outBlob,
                                     bool showErrors)
{
    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> errors;
    HRESULT hr = D3DCompileFromFile(
        path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entryPoint, target, compileFlags, 0, &outBlob, &errors);

    if (FAILED(hr))
    {
        // 컴파일 에러 메시지를 그대로 보여줌 (HLSL 디버깅용)
        std::string msg = "셰이더 컴파일 실패:\n";
        if (errors)
            msg += static_cast<const char*>(errors->GetBufferPointer());
        if (showErrors)
            MessageBoxA(nullptr, msg.c_str(), "HLSL Error", MB_OK | MB_ICONERROR);
        else
            OutputDebugStringA(msg.c_str());
        return false;
    }
    return true;
}

bool Renderer::CreateShaders(bool showErrors)
{
    ComPtr<ID3DBlob> vsBlob, psBlob;

    if (!CompileShaderFromFile(m_vsPath, "main", "vs_5_0", vsBlob, showErrors))
        return false;
    if (!CompileShaderFromFile(m_psPath, "main", "ps_5_0", psBlob, showErrors))
        return false;

    ComPtr<ID3D11VertexShader> newVs;
    ComPtr<ID3D11PixelShader>  newPs;

    HRESULT hr = m_device->CreateVertexShader(
        vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &newVs);
    if (FAILED(hr)) return false;

    hr = m_device->CreatePixelShader(
        psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &newPs);
    if (FAILED(hr)) return false;

    m_vs = newVs;
    m_ps = newPs;

    // 입력 레이아웃 없음: 정점은 SV_VertexID 로 셰이더 내부에서 생성
    return true;
}

bool Renderer::GetShaderWriteTimes(std::filesystem::file_time_type& vsTime,
                                   std::filesystem::file_time_type& psTime,
                                   std::filesystem::file_time_type& rayLibTime) const
{
    std::error_code ec;
    vsTime = std::filesystem::last_write_time(m_vsPath, ec);
    if (ec) return false;

    psTime = std::filesystem::last_write_time(m_psPath, ec);
    if (ec) return false;

    rayLibTime = std::filesystem::last_write_time(m_rayLibPath, ec);
    return !ec;
}

void Renderer::UpdateShaderWriteTimes()
{
    GetShaderWriteTimes(m_vsWriteTime, m_psWriteTime, m_rayLibWriteTime);
}

void Renderer::CheckShaderHotReload()
{
    std::filesystem::file_time_type vsTime;
    std::filesystem::file_time_type psTime;
    std::filesystem::file_time_type rayLibTime;
    if (!GetShaderWriteTimes(vsTime, psTime, rayLibTime))
        return;

    if (vsTime == m_vsWriteTime &&
        psTime == m_psWriteTime &&
        rayLibTime == m_rayLibWriteTime)
        return;

    // 실패해도 기존 셰이더는 유지한다. 타임스탬프는 갱신해 같은 오류를 매 프레임 반복하지 않는다.
    CreateShaders(false);
    m_vsWriteTime = vsTime;
    m_psWriteTime = psTime;
    m_rayLibWriteTime = rayLibTime;
}

void Renderer::Resize(int width, int height)
{
    if (!m_swapChain || width <= 0 || height <= 0) return;

    m_width  = width;
    m_height = height;

    // RTV를 풀고 백버퍼 리사이즈 후 재생성
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    m_rtv.Reset();

    m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    CreateRenderTarget();
}

void Renderer::Render(const Camera& camera, float timeSeconds)
{
    if (!m_rtv) return;
    CheckShaderHotReload();

    // ---- 상수버퍼 갱신 ----
    CameraCB cb = {};
    // HLSL은 mul(vector, matrix) 규약 → DirectXMath 행렬을 transpose 해서 업로드
    XMStoreFloat4x4(&cb.invViewProj, XMMatrixTranspose(camera.GetInvViewProj()));
    cb.cameraPos      = camera.GetPosition();
    cb.time           = timeSeconds;
    cb.volumeCenter   = XMFLOAT3(0.0f, 0.0f, 0.0f); // 원점에 놓인 박스 볼륨
    cb.densityScale   = 1.2f;                        // 밀도 (불투명도)
    cb.volumeHalfSize = XMFLOAT3(2.0f, 1.2f, 1.2f);  // AABB 절반 크기
    cb._pad           = 0.0f;

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(m_context->Map(m_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        memcpy(mapped.pData, &cb, sizeof(cb));
        m_context->Unmap(m_cb.Get(), 0);
    }

    // ---- 뷰포트 ----
    D3D11_VIEWPORT vp = {};
    vp.Width    = static_cast<float>(m_width);
    vp.Height   = static_cast<float>(m_height);
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);

    // ---- 렌더 타깃 클리어 ----
    const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    m_context->OMSetRenderTargets(1, m_rtv.GetAddressOf(), nullptr);
    m_context->ClearRenderTargetView(m_rtv.Get(), clearColor);

    // ---- 파이프라인 설정 후 풀스크린 삼각형 1개 그리기 ----
    m_context->IASetInputLayout(nullptr);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_vs.Get(), nullptr, 0);
    m_context->PSSetShader(m_ps.Get(), nullptr, 0);
    m_context->PSSetConstantBuffers(0, 1, m_cb.GetAddressOf());

    m_context->Draw(3, 0); // 정점 3개 = 화면을 덮는 삼각형

    m_swapChain->Present(1, 0); // vsync
}
