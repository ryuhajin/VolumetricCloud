#include "Renderer.h"
#include "Camera.h"

#include <d3dcompiler.h>

#include <cstring>
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

std::wstring ResolveShaderDir()
{
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

bool Renderer::Init(HWND hwnd, int width, int height)
{
    m_width = width;
    m_height = height;
    m_shaderDir = ResolveShaderDir();
    m_shaderPaths = {
        m_shaderDir + L"Fullscreen.hlsl",
        m_shaderDir + L"VolumetricClouds.hlsl",
        m_shaderDir + L"DiagnosticScene.hlsl",
    };

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
        !CreatePipelineStates() || !CreateConstantBuffers())
    {
        MessageBoxW(hwnd, L"단계 0 렌더링 리소스 생성 실패", L"오류", MB_OK | MB_ICONERROR);
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
    if (showErrors)
        MessageBoxA(nullptr, message.c_str(), "HLSL Error", MB_OK | MB_ICONERROR);
    else
        OutputDebugStringA(message.c_str());
    return false;
}

bool Renderer::CreateShaders(bool showErrors)
{
    ComPtr<ID3DBlob> fullscreenVsBlob;
    ComPtr<ID3DBlob> foundationPsBlob;
    ComPtr<ID3DBlob> sceneVsBlob;
    ComPtr<ID3DBlob> scenePsBlob;
    if (!CompileShaderFromFile(m_shaderPaths[0], "main", "vs_5_0", fullscreenVsBlob, showErrors) ||
        !CompileShaderFromFile(m_shaderPaths[1], "main", "ps_5_0", foundationPsBlob, showErrors) ||
        !CompileShaderFromFile(m_shaderPaths[2], "VSMain", "vs_5_0", sceneVsBlob, showErrors) ||
        !CompileShaderFromFile(m_shaderPaths[2], "PSMain", "ps_5_0", scenePsBlob, showErrors))
        return false;

    ComPtr<ID3D11VertexShader> fullscreenVs;
    ComPtr<ID3D11PixelShader> foundationPs;
    ComPtr<ID3D11VertexShader> sceneVs;
    ComPtr<ID3D11PixelShader> scenePs;
    ComPtr<ID3D11InputLayout> inputLayout;

    if (FAILED(m_device->CreateVertexShader(
            fullscreenVsBlob->GetBufferPointer(), fullscreenVsBlob->GetBufferSize(),
            nullptr, &fullscreenVs)) ||
        FAILED(m_device->CreatePixelShader(
            foundationPsBlob->GetBufferPointer(), foundationPsBlob->GetBufferSize(),
            nullptr, &foundationPs)) ||
        FAILED(m_device->CreateVertexShader(
            sceneVsBlob->GetBufferPointer(), sceneVsBlob->GetBufferSize(),
            nullptr, &sceneVs)) ||
        FAILED(m_device->CreatePixelShader(
            scenePsBlob->GetBufferPointer(), scenePsBlob->GetBufferSize(),
            nullptr, &scenePs)))
        return false;

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
        return false;

    m_fullscreenVs = fullscreenVs;
    m_foundationPs = foundationPs;
    m_sceneVs = sceneVs;
    m_scenePs = scenePs;
    m_sceneInputLayout = inputLayout;
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

void Renderer::RenderFoundationPass(const Camera& camera, float timeSeconds)
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
    m_context->PSSetShader(m_foundationPs.Get(), nullptr, 0);
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
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(m_width);
    viewport.Height = static_cast<float>(m_height);
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);

    RenderDiagnosticScene(camera);
    RenderFoundationPass(camera, timeSeconds);
    m_swapChain->Present(1, 0);
}

void Renderer::SetDebugMode(CloudDebugMode mode)
{
    m_cloudParameters.debugMode = static_cast<std::int32_t>(mode);
}

CloudDebugMode Renderer::DebugMode() const
{
    return static_cast<CloudDebugMode>(m_cloudParameters.debugMode);
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
    std::array<std::filesystem::file_time_type, 3>& writeTimes) const
{
    std::error_code error;
    for (size_t i = 0; i < m_shaderPaths.size(); ++i)
    {
        writeTimes[i] = std::filesystem::last_write_time(m_shaderPaths[i], error);
        if (error)
            return false;
    }
    return true;
}

void Renderer::UpdateShaderWriteTimes()
{
    GetShaderWriteTimes(m_shaderWriteTimes);
}

void Renderer::CheckShaderHotReload()
{
    std::array<std::filesystem::file_time_type, 3> currentTimes = {};
    if (!GetShaderWriteTimes(currentTimes) || currentTimes == m_shaderWriteTimes)
        return;

    CreateShaders(false);
    m_shaderWriteTimes = currentTimes;
}
