#include "Renderer.h"
#include "Camera.h"

#include <d3dcompiler.h>

#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace
{
std::wstring GetExeDir()
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring full(path);
    const size_t slash = full.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? L"." : full.substr(0, slash);
}

std::wstring WidenUtf8(const char* text)
{
    const int length = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (length <= 0)
        return L"";

    std::wstring result(static_cast<size_t>(length - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, result.data(), length);
    return result;
}

bool DirectoryExists(const std::wstring& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::string CurrentLocalTimeText()
{
    SYSTEMTIME time = {};
    GetLocalTime(&time);
    char text[16] = {};
    sprintf_s(text, "%02u:%02u:%02u", time.wHour, time.wMinute, time.wSecond);
    return text;
}

std::wstring ResolveShaderDir()
{
    wchar_t overrideDirectory[32768] = {};
    const DWORD overrideLength = GetEnvironmentVariableW(
        L"VCLOUD_SHADER_OVERRIDE_DIR", overrideDirectory,
        static_cast<DWORD>(std::size(overrideDirectory)));
    if (overrideLength > 0 && overrideLength < std::size(overrideDirectory) &&
        DirectoryExists(overrideDirectory))
        return std::wstring(overrideDirectory) + L"\\";
#ifdef VCLOUD_SHADER_SOURCE_DIR
    const std::wstring sourceDir = WidenUtf8(VCLOUD_SHADER_SOURCE_DIR);
    if (DirectoryExists(sourceDir))
        return sourceDir + L"\\";
#endif
    return GetExeDir() + L"\\shaders\\";
}

void AppendBox(std::vector<DiagnosticSceneVertex>& vertices,
               std::vector<std::uint32_t>& indices,
               const XMFLOAT3& center,
               const XMFLOAT3& halfSize,
               const XMFLOAT3& color)
{
    const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
    const float x0 = center.x - halfSize.x;
    const float x1 = center.x + halfSize.x;
    const float y0 = center.y - halfSize.y;
    const float y1 = center.y + halfSize.y;
    const float z0 = center.z - halfSize.z;
    const float z1 = center.z + halfSize.z;

    const XMFLOAT3 positions[8] = {
        { x0, y0, z0 }, { x1, y0, z0 }, { x1, y1, z0 }, { x0, y1, z0 },
        { x0, y0, z1 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z1 },
    };
    for (const XMFLOAT3& position : positions)
        vertices.push_back({ position, color });

    const std::uint32_t localIndices[36] = {
        0, 2, 1, 0, 3, 2,
        4, 5, 6, 4, 6, 7,
        0, 1, 5, 0, 5, 4,
        3, 7, 6, 3, 6, 2,
        0, 4, 7, 0, 7, 3,
        1, 2, 6, 1, 6, 5,
    };
    for (const std::uint32_t index : localIndices)
        indices.push_back(base + index);
}
}

Renderer::~Renderer()
{
    m_noiseLab.Shutdown();
}

bool Renderer::Init(HWND hwnd, int width, int height)
{
    m_width = width;
    m_height = height;
    m_shaderDir = ResolveShaderDir();
    m_fullscreenShaderPath = m_shaderDir + L"Fullscreen.hlsl";
    m_cloudShaderPath = m_shaderDir + L"VolumetricClouds.hlsl";
    m_noiseLabShaderPath = m_shaderDir + L"NoiseLab.hlsl";
    m_sceneShaderPath = m_shaderDir + L"DiagnosticScene.hlsl";

    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    swapChainDesc.BufferCount = 2;
    swapChainDesc.BufferDesc.Width = width;
    swapChainDesc.BufferDesc.Height = height;
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.OutputWindow = hwnd;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.Windowed = TRUE;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    const D3D_FEATURE_LEVEL requestedLevel = D3D_FEATURE_LEVEL_11_0;
    const HRESULT createResult = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        &requestedLevel, 1, D3D11_SDK_VERSION, &swapChainDesc,
        &m_swapChain, &m_device, nullptr, &m_context);
    if (FAILED(createResult))
    {
        MessageBoxW(hwnd, L"D3D11 디바이스/스왑체인 생성 실패", L"오류", MB_OK | MB_ICONERROR);
        return false;
    }

    if (!CreateBackBufferTarget() || !CreateSceneTargets() ||
        !CreateShaders(true) || !CreateDiagnosticScene() ||
        !CreatePipelineStates() || !CreateConstantBuffers() ||
        !m_noiseLab.Init(hwnd, m_device.Get(), m_context.Get()))
    {
        MessageBoxW(hwnd, L"단계 2 렌더링 리소스 생성 실패", L"오류", MB_OK | MB_ICONERROR);
        return false;
    }

    UpdateShaderWriteTimes();
    return true;
}

bool Renderer::CreateBackBufferTarget()
{
    ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))))
        return false;
    return SUCCEEDED(m_device->CreateRenderTargetView(
        backBuffer.Get(), nullptr, &m_backBufferRtv));
}

bool Renderer::CreateSceneTargets()
{
    D3D11_TEXTURE2D_DESC colorDesc = {};
    colorDesc.Width = static_cast<UINT>(m_width);
    colorDesc.Height = static_cast<UINT>(m_height);
    colorDesc.MipLevels = 1;
    colorDesc.ArraySize = 1;
    colorDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    colorDesc.SampleDesc.Count = 1;
    colorDesc.Usage = D3D11_USAGE_DEFAULT;
    colorDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(m_device->CreateTexture2D(&colorDesc, nullptr, &m_sceneColor)) ||
        FAILED(m_device->CreateRenderTargetView(
            m_sceneColor.Get(), nullptr, &m_sceneColorRtv)) ||
        FAILED(m_device->CreateShaderResourceView(
            m_sceneColor.Get(), nullptr, &m_sceneColorSrv)))
        return false;

    D3D11_TEXTURE2D_DESC depthDesc = colorDesc;
    depthDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(m_device->CreateTexture2D(&depthDesc, nullptr, &m_sceneDepth)))
        return false;

    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    if (FAILED(m_device->CreateDepthStencilView(
        m_sceneDepth.Get(), &dsvDesc, &m_sceneDepthDsv)))
        return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    return SUCCEEDED(m_device->CreateShaderResourceView(
        m_sceneDepth.Get(), &srvDesc, &m_sceneDepthSrv));
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
    const HRESULT result = D3DCompileFromFile(
        path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entryPoint, target, compileFlags, 0, &outBlob, &errors);
    if (SUCCEEDED(result))
        return true;

    std::string message = "셰이더 컴파일 실패:\n";
    if (errors)
        message += static_cast<const char*>(errors->GetBufferPointer());
    m_shaderError = message;
    if (showErrors)
        MessageBoxA(nullptr, message.c_str(), "HLSL Error", MB_OK | MB_ICONERROR);
    else
        OutputDebugStringA(message.c_str());
    return false;
}

bool Renderer::CreateShaders(bool showErrors)
{
    m_shaderError.clear();
    ComPtr<ID3DBlob> fullscreenVsBlob;
    ComPtr<ID3DBlob> cloudPsBlob;
    ComPtr<ID3DBlob> noiseLabPsBlob;
    ComPtr<ID3DBlob> sceneVsBlob;
    ComPtr<ID3DBlob> scenePsBlob;
    if (!CompileShaderFromFile(m_fullscreenShaderPath, "main", "vs_5_0", fullscreenVsBlob, showErrors) ||
        !CompileShaderFromFile(m_cloudShaderPath, "main", "ps_5_0", cloudPsBlob, showErrors) ||
        !CompileShaderFromFile(m_noiseLabShaderPath, "main", "ps_5_0", noiseLabPsBlob, showErrors) ||
        !CompileShaderFromFile(m_sceneShaderPath, "VSMain", "vs_5_0", sceneVsBlob, showErrors) ||
        !CompileShaderFromFile(m_sceneShaderPath, "PSMain", "ps_5_0", scenePsBlob, showErrors))
    {
        m_shaderStatus = "Reload failed; previous generation kept";
        return false;
    }

    ComPtr<ID3D11VertexShader> fullscreenVs;
    ComPtr<ID3D11PixelShader> cloudPs;
    ComPtr<ID3D11PixelShader> noiseLabPs;
    ComPtr<ID3D11VertexShader> sceneVs;
    ComPtr<ID3D11PixelShader> scenePs;
    ComPtr<ID3D11InputLayout> inputLayout;

    if (FAILED(m_device->CreateVertexShader(
            fullscreenVsBlob->GetBufferPointer(), fullscreenVsBlob->GetBufferSize(),
            nullptr, &fullscreenVs)) ||
        FAILED(m_device->CreatePixelShader(
            cloudPsBlob->GetBufferPointer(), cloudPsBlob->GetBufferSize(),
            nullptr, &cloudPs)) ||
        FAILED(m_device->CreatePixelShader(
            noiseLabPsBlob->GetBufferPointer(), noiseLabPsBlob->GetBufferSize(),
            nullptr, &noiseLabPs)) ||
        FAILED(m_device->CreateVertexShader(
            sceneVsBlob->GetBufferPointer(), sceneVsBlob->GetBufferSize(),
            nullptr, &sceneVs)) ||
        FAILED(m_device->CreatePixelShader(
            scenePsBlob->GetBufferPointer(), scenePsBlob->GetBufferSize(),
            nullptr, &scenePs)))
    {
        m_shaderError = "D3D11 shader object creation failed";
        m_shaderStatus = "Reload failed; previous generation kept";
        return false;
    }

    const D3D11_INPUT_ELEMENT_DESC elements[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
          D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
          D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    if (FAILED(m_device->CreateInputLayout(
            elements, static_cast<UINT>(std::size(elements)),
            sceneVsBlob->GetBufferPointer(), sceneVsBlob->GetBufferSize(),
            &inputLayout)))
    {
        m_shaderError = "Diagnostic scene input layout creation failed";
        m_shaderStatus = "Reload failed; previous generation kept";
        return false;
    }

    m_fullscreenVs = fullscreenVs;
    m_cloudPs = cloudPs;
    m_noiseLabPs = noiseLabPs;
    m_sceneVs = sceneVs;
    m_scenePs = scenePs;
    m_sceneInputLayout = inputLayout;
    ++m_shaderGeneration;
    m_shaderStatus = "Reload succeeded @ " + CurrentLocalTimeText();
    m_shaderError.clear();
    return true;
}

bool Renderer::CreateDiagnosticScene()
{
    std::vector<DiagnosticSceneVertex> vertices;
    std::vector<std::uint32_t> indices;
    AppendBox(vertices, indices, { 0.0f, -1.75f, 0.0f }, { 7.5f, 0.25f, 7.5f },
              { 0.22f, 0.28f, 0.24f });
    AppendBox(vertices, indices, { -1.6f, -0.25f, 0.0f }, { 1.0f, 1.25f, 1.0f },
              { 0.85f, 0.30f, 0.12f });
    AppendBox(vertices, indices, { 1.8f, -0.65f, -1.8f }, { 0.8f, 0.85f, 0.8f },
              { 0.10f, 0.42f, 0.82f });

    D3D11_BUFFER_DESC vertexDesc = {};
    vertexDesc.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(DiagnosticSceneVertex));
    vertexDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vertexDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vertexData = {};
    vertexData.pSysMem = vertices.data();
    if (FAILED(m_device->CreateBuffer(&vertexDesc, &vertexData, &m_sceneVertexBuffer)))
        return false;

    D3D11_BUFFER_DESC indexDesc = {};
    indexDesc.ByteWidth = static_cast<UINT>(indices.size() * sizeof(std::uint32_t));
    indexDesc.Usage = D3D11_USAGE_IMMUTABLE;
    indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA indexData = {};
    indexData.pSysMem = indices.data();
    if (FAILED(m_device->CreateBuffer(&indexDesc, &indexData, &m_sceneIndexBuffer)))
        return false;

    m_sceneIndexCount = static_cast<std::uint32_t>(indices.size());
    return true;
}

bool Renderer::CreatePipelineStates()
{
    D3D11_DEPTH_STENCIL_DESC depthDesc = {};
    depthDesc.DepthEnable = TRUE;
    depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depthDesc.DepthFunc = D3D11_COMPARISON_LESS;
    if (FAILED(m_device->CreateDepthStencilState(&depthDesc, &m_depthState)))
        return false;

    D3D11_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.DepthClipEnable = TRUE;
    if (FAILED(m_device->CreateRasterizerState(&rasterizerDesc, &m_rasterizerState)))
        return false;

    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    return SUCCEEDED(m_device->CreateSamplerState(
        &samplerDesc, &m_pointClampSampler));
}

bool Renderer::CreateConstantBuffers()
{
    const auto createDynamicBuffer = [&](UINT byteWidth, ID3D11Buffer** buffer)
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = byteWidth;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        return SUCCEEDED(m_device->CreateBuffer(&desc, nullptr, buffer));
    };

    return createDynamicBuffer(sizeof(CameraCB), &m_cameraCb) &&
           createDynamicBuffer(sizeof(CloudParameters), &m_cloudCb) &&
           createDynamicBuffer(sizeof(SceneCB), &m_sceneCb);
}

void Renderer::ReleaseSizeDependentResources()
{
    m_backBufferRtv.Reset();
    m_sceneColorSrv.Reset();
    m_sceneColorRtv.Reset();
    m_sceneColor.Reset();
    m_sceneDepthSrv.Reset();
    m_sceneDepthDsv.Reset();
    m_sceneDepth.Reset();
}

void Renderer::Resize(int width, int height)
{
    if (!m_swapChain || width <= 0 || height <= 0)
        return;

    m_width = width;
    m_height = height;
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    ID3D11ShaderResourceView* nullSrvs[2] = { nullptr, nullptr };
    m_context->PSSetShaderResources(0, 2, nullSrvs);
    ReleaseSizeDependentResources();

    if (SUCCEEDED(m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0)))
    {
        CreateBackBufferTarget();
        CreateSceneTargets();
    }
}

void Renderer::RenderDiagnosticScene(const Camera& camera)
{
    SceneCB scene = {};
    XMStoreFloat4x4(&scene.viewProj, XMMatrixTranspose(camera.GetViewProj()));
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(m_context->Map(m_sceneCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        std::memcpy(mapped.pData, &scene, sizeof(scene));
        m_context->Unmap(m_sceneCb.Get(), 0);
    }

    const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    m_context->OMSetRenderTargets(1, m_sceneColorRtv.GetAddressOf(), m_sceneDepthDsv.Get());
    m_context->ClearRenderTargetView(m_sceneColorRtv.Get(), clearColor);
    m_context->ClearDepthStencilView(m_sceneDepthDsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    m_context->OMSetDepthStencilState(m_depthState.Get(), 0);
    m_context->RSSetState(m_rasterizerState.Get());

    const UINT stride = sizeof(DiagnosticSceneVertex);
    const UINT offset = 0;
    m_context->IASetInputLayout(m_sceneInputLayout.Get());
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->IASetVertexBuffers(0, 1, m_sceneVertexBuffer.GetAddressOf(), &stride, &offset);
    m_context->IASetIndexBuffer(m_sceneIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    m_context->VSSetShader(m_sceneVs.Get(), nullptr, 0);
    m_context->VSSetConstantBuffers(0, 1, m_sceneCb.GetAddressOf());
    m_context->PSSetShader(m_scenePs.Get(), nullptr, 0);
    m_context->DrawIndexed(m_sceneIndexCount, 0, 0);

    m_context->OMSetRenderTargets(0, nullptr, nullptr);
}

void Renderer::RenderCloudPass(const Camera& camera, float timeSeconds)
{
    CameraCB cameraData = {};
    XMStoreFloat4x4(&cameraData.invViewProj, XMMatrixTranspose(camera.GetInvViewProj()));
    cameraData.cameraPos = camera.GetPosition();
    cameraData.time = timeSeconds;
    cameraData.renderSize = {
        static_cast<float>(m_width), static_cast<float>(m_height)
    };
    cameraData.nearPlane = camera.GetNearPlane();
    cameraData.farPlane = camera.GetFarPlane();

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(m_context->Map(m_cameraCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        std::memcpy(mapped.pData, &cameraData, sizeof(cameraData));
        m_context->Unmap(m_cameraCb.Get(), 0);
    }
    if (SUCCEEDED(m_context->Map(m_cloudCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        std::memcpy(mapped.pData, &m_cloudParameters, sizeof(m_cloudParameters));
        m_context->Unmap(m_cloudCb.Get(), 0);
    }

    const float clearColor[4] = { 0.02f, 0.03f, 0.05f, 1.0f };
    m_context->OMSetRenderTargets(1, m_backBufferRtv.GetAddressOf(), nullptr);
    m_context->ClearRenderTargetView(m_backBufferRtv.Get(), clearColor);
    m_context->OMSetDepthStencilState(nullptr, 0);
    m_context->RSSetState(nullptr);

    m_context->IASetInputLayout(nullptr);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_fullscreenVs.Get(), nullptr, 0);
    m_context->PSSetShader(m_cloudPs.Get(), nullptr, 0);
    ID3D11Buffer* constantBuffers[2] = { m_cameraCb.Get(), m_cloudCb.Get() };
    m_context->PSSetConstantBuffers(0, 2, constantBuffers);
    ID3D11ShaderResourceView* resources[2] = {
        m_sceneColorSrv.Get(), m_sceneDepthSrv.Get()
    };
    m_context->PSSetShaderResources(0, 2, resources);
    m_context->PSSetSamplers(0, 1, m_pointClampSampler.GetAddressOf());
    m_context->Draw(3, 0);

    ID3D11ShaderResourceView* nullResources[2] = { nullptr, nullptr };
    m_context->PSSetShaderResources(0, 2, nullResources);
}

void Renderer::Render(const Camera& camera, float timeSeconds)
{
    if (!m_backBufferRtv || !m_sceneColorRtv || !m_sceneDepthDsv)
        return;

    CheckShaderHotReload();
    m_noiseLab.BeginFrame(timeSeconds, m_cloudParameters,
                          m_shaderGeneration, m_shaderStatus, m_shaderError);
    if (m_noiseLab.ConsumeParametersChanged())
        m_noisePreset = Stage2NoisePreset::Custom;
    const float effectiveTime = m_noiseLab.EffectiveTime();
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(m_width);
    viewport.Height = static_cast<float>(m_height);
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);

    RenderDiagnosticScene(camera);
    RenderCloudPass(camera, effectiveTime);
    if (m_captureFrameHashes)
        CaptureCloudFrameHash();
    m_noiseLab.RenderPreviews(
        m_fullscreenVs.Get(), m_noiseLabPs.Get(), m_cloudCb.Get());
    if (m_noiseLab.ConsumeExportRequest())
    {
        std::filesystem::path shaderDirectory(m_shaderDir);
        if (shaderDirectory.filename().empty())
            shaderDirectory = shaderDirectory.parent_path();
        m_noiseLab.ExportSnapshot(shaderDirectory.parent_path() / L"captures" / L"noise-lab",
                                  m_cloudParameters,
                                  shaderDirectory / L"Noise.hlsli");
    }
    m_noiseLab.EndFrame(m_backBufferRtv.Get());
    m_swapChain->Present(1, 0);
}

void Renderer::CaptureCloudFrameHash()
{
    ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))))
        return;
    D3D11_TEXTURE2D_DESC desc = {};
    backBuffer->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(m_device->CreateTexture2D(&desc, nullptr, &staging)))
        return;
    m_context->CopyResource(staging.Get(), backBuffer.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(m_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return;
    std::uint64_t hash = 1469598103934665603ull;
    for (UINT y = 0; y < desc.Height; ++y)
    {
        const auto* row = static_cast<const unsigned char*>(mapped.pData) +
                          static_cast<size_t>(y) * mapped.RowPitch;
        for (UINT x = 0; x < desc.Width * 4; ++x)
        {
            hash ^= row[x];
            hash *= 1099511628211ull;
        }
    }
    m_context->Unmap(staging.Get(), 0);
    m_lastCloudFrameHash = hash;
}

void Renderer::SetDebugMode(CloudDebugMode mode)
{
    m_cloudParameters.debugMode = static_cast<std::int32_t>(mode);
}

CloudDebugMode Renderer::DebugMode() const
{
    return static_cast<CloudDebugMode>(m_cloudParameters.debugMode);
}

void Renderer::ApplyStage1ValidationPreset(Stage1ValidationPreset preset)
{
    // 각 키는 다른 키의 잔여 상태가 결과를 흐리지 않도록 단계 1 기본값에서 시작한다.
    m_cloudParameters.cloudBoundsMin = { -2.0f, -1.0f, -2.0f };
    m_cloudParameters.cloudBoundsMax = { 2.0f, 2.0f, 2.0f };
    m_cloudParameters.densityMultiplier = 1.0f;
    m_cloudParameters.stepSize = 0.10f;
    m_cloudParameters.maxViewSteps = 128;
    m_cloudParameters.extinctionCoefficient = 1.0f;
    m_cloudParameters.transmittanceThreshold = 0.01f;

    switch (preset)
    {
    case Stage1ValidationPreset::ThinVolume:
        m_cloudParameters.cloudBoundsMin.z = -0.5f;
        m_cloudParameters.cloudBoundsMax.z = 0.5f;
        break;
    case Stage1ValidationPreset::ThickVolume:
        m_cloudParameters.cloudBoundsMin.z = -4.0f;
        m_cloudParameters.cloudBoundsMax.z = 4.0f;
        break;
    case Stage1ValidationPreset::FineStep:
        m_cloudParameters.stepSize = 0.025f;
        break;
    case Stage1ValidationPreset::CoarseStep:
        m_cloudParameters.stepSize = 0.5f;
        break;
    case Stage1ValidationPreset::DefaultVolume:
    default:
        break;
    }
    m_validationPreset = preset;
}

Stage1ValidationPreset Renderer::ValidationPreset() const
{
    return m_validationPreset;
}

void Renderer::ApplyStage2NoisePreset(Stage2NoisePreset preset)
{
    // 프리셋을 누르는 순서와 무관하게 비교할 수 있도록 noise 관련 값만 기본화한다.
    // AABB와 step 프리셋은 유지되어 두 종류의 검증을 조합할 수 있다.
    m_cloudParameters.baseNoiseScale = 0.35f;
    m_cloudParameters.coverage = 0.55f;
    m_cloudParameters.densityMultiplier = 1.0f;
    m_cloudParameters.windDirection = { 0.9701425f, 0.0f, 0.2425356f };
    m_cloudParameters.windSpeed = 0.25f;
    m_cloudParameters.noiseOffset = 0.0f;

    switch (preset)
    {
    case Stage2NoisePreset::SparseCoverage:
        m_cloudParameters.coverage = 0.35f;
        break;
    case Stage2NoisePreset::DenseCoverage:
        m_cloudParameters.coverage = 0.75f;
        break;
    case Stage2NoisePreset::LargeBlobs:
        m_cloudParameters.baseNoiseScale = 0.18f;
        break;
    case Stage2NoisePreset::SmallBlobs:
        m_cloudParameters.baseNoiseScale = 0.70f;
        break;
    case Stage2NoisePreset::StoppedWind:
        m_cloudParameters.windSpeed = 0.0f;
        break;
    case Stage2NoisePreset::FastWind:
        m_cloudParameters.windSpeed = 0.8f;
        break;
    case Stage2NoisePreset::OffsetNoise:
        m_cloudParameters.noiseOffset = 0.73f;
        break;
    case Stage2NoisePreset::DefaultNoise:
    default:
        break;
    }
    m_noisePreset = preset;
}

Stage2NoisePreset Renderer::NoisePreset() const
{
    return m_noisePreset;
}

bool Renderer::HasDebugLayerErrors() const
{
#ifdef _DEBUG
    ComPtr<ID3D11InfoQueue> infoQueue;
    if (FAILED(m_device.As(&infoQueue)))
        return false;

    const UINT64 messageCount = infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
    for (UINT64 i = 0; i < messageCount; ++i)
    {
        SIZE_T messageSize = 0;
        if (FAILED(infoQueue->GetMessage(i, nullptr, &messageSize)))
            continue;
        std::vector<unsigned char> storage(messageSize);
        D3D11_MESSAGE* message = reinterpret_cast<D3D11_MESSAGE*>(storage.data());
        if (SUCCEEDED(infoQueue->GetMessage(i, message, &messageSize)) &&
            (message->Severity == D3D11_MESSAGE_SEVERITY_ERROR ||
             message->Severity == D3D11_MESSAGE_SEVERITY_CORRUPTION))
            return true;
    }
#endif
    return false;
}

bool Renderer::GetShaderWriteTimes(
    std::map<std::wstring, std::filesystem::file_time_type>& writeTimes) const
{
    writeTimes.clear();
    std::error_code error;
    const std::filesystem::path root(m_shaderDir);
    std::filesystem::recursive_directory_iterator iterator(root, error);
    const std::filesystem::recursive_directory_iterator end;
    if (error)
        return false;
    for (; iterator != end; iterator.increment(error))
    {
        if (error)
            return false;
        if (!iterator->is_regular_file(error) || error)
            continue;
        const std::wstring extension = iterator->path().extension().wstring();
        if (extension != L".hlsl" && extension != L".hlsli")
            continue;
        const auto time = std::filesystem::last_write_time(iterator->path(), error);
        if (error)
            return false;
        writeTimes.emplace(iterator->path().wstring(), time);
    }
    return !writeTimes.empty();
}

void Renderer::UpdateShaderWriteTimes()
{
    GetShaderWriteTimes(m_shaderWriteTimes);
}

void Renderer::CheckShaderHotReload()
{
    std::map<std::wstring, std::filesystem::file_time_type> currentTimes;
    if (!GetShaderWriteTimes(currentTimes) || currentTimes == m_shaderWriteTimes)
        return;

    std::ostringstream changed;
    bool first = true;
    for (const auto& [path, time] : currentTimes)
    {
        const auto previous = m_shaderWriteTimes.find(path);
        if (previous == m_shaderWriteTimes.end() || previous->second != time)
        {
            if (!first)
                changed << ", ";
            changed << std::filesystem::path(path).filename().string();
            first = false;
        }
    }
    for (const auto& [path, time] : m_shaderWriteTimes)
    {
        if (currentTimes.find(path) == currentTimes.end())
        {
            if (!first)
                changed << ", ";
            changed << std::filesystem::path(path).filename().string() << " (removed)";
            first = false;
        }
    }
    const bool succeeded = CreateShaders(false);
    if (succeeded)
        m_shaderStatus = "Reloaded: " + changed.str() + " @ " + CurrentLocalTimeText();
    else
        m_shaderStatus = "Reload failed: " + changed.str();
    m_shaderWriteTimes = currentTimes;
}

bool Renderer::HandleWindowMessage(
    HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    return m_noiseLab.HandleWindowMessage(hwnd, message, wParam, lParam);
}

bool Renderer::ValidateNoiseLabPreviews()
{
    return m_noiseLab.ValidatePreviewData();
}

bool Renderer::ExportNoiseLabSnapshot(const std::filesystem::path& root)
{
    return m_noiseLab.ExportSnapshot(
        root, m_cloudParameters,
        std::filesystem::path(m_shaderDir) / L"Noise.hlsli");
}

std::uint64_t Renderer::NoiseLabPreviewHash(std::size_t targetIndex)
{
    return m_noiseLab.PreviewHash(targetIndex);
}
