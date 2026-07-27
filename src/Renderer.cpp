#include "Renderer.h"
#include "Camera.h"

#include <d3dcompiler.h>
#include <d3d11sdklayers.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

Renderer::~Renderer()
{
    m_debugUI.Shutdown();
}

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

static std::wstring ResolveDefaultCacheDir()
{
#ifdef VCLOUD_CACHE_SOURCE_DIR
    std::wstring sourceDir = WidenUtf8(VCLOUD_CACHE_SOURCE_DIR);
    if (DirectoryExists(sourceDir)) return sourceDir;
#endif
    return GetExeDir() + L"\\assets\\noise-cache";
}

bool Renderer::Init(HWND hwnd, int width, int height, bool forceRebuildCache)
{
    m_width  = width;
    m_height = height;
    m_shaderDir = ResolveShaderDir();
    m_vsPath = m_shaderDir + L"Fullscreen.hlsl";
    m_psPath = m_shaderDir + L"VolumetricClouds.hlsl";
    m_rayLibPath = m_shaderDir + L"Ray.hlsli";
    m_cloudNoisePath = m_shaderDir + L"CloudNoise.hlsli";
    m_previewPath = m_shaderDir + L"NoisePreview.hlsl";
    m_noiseVolumeCsPath = m_shaderDir + L"NoiseVolumeCS.hlsl";
    m_shaderPaths = { m_vsPath, m_psPath, m_rayLibPath, m_cloudNoisePath, m_previewPath, m_noiseVolumeCsPath };

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

    m_noiseCacheManager.Init(ResolveDefaultCacheDir(), m_shaderPaths);
    m_cacheLoaded = !forceRebuildCache && m_noiseCacheManager.LoadPreferred(
        m_device.Get(), m_cloudParams, m_shaderBlobs,
        m_noiseVolumes, m_noiseVolumeUavs, m_noiseVolumeSrvs, m_sourceModified);
    if (m_cacheLoaded)
    {
        if (!CreateShadersFromBlobs(m_shaderBlobs)) return false;
        m_noiseCacheDirty = false;
        m_cacheStatus = m_sourceModified ? "Source Modified" : "Saved";
    }
    else
    {
        if (!CreateShaders(true)) return false;
        m_cacheStatus = "Generating";
    }
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

    bd.ByteWidth = sizeof(CloudParameters);
    hr = m_device->CreateBuffer(&bd, nullptr, &m_cloudCb);
    if (FAILED(hr)) return false;

    bd.ByteWidth = sizeof(NoisePreviewCB);
    hr = m_device->CreateBuffer(&bd, nullptr, &m_previewCb);
    if (FAILED(hr)) return false;

    bd.ByteWidth = sizeof(NoiseVolumeGenerationCB);
    hr = m_device->CreateBuffer(&bd, nullptr, &m_noiseVolumeGenerationCb);
    if (FAILED(hr)) return false;

    if (!CreateNoisePreviewResources()) return false;
    if (!m_cacheLoaded && !CreateNoiseVolumeResources()) return false;
    if (!CreateNoiseSampler()) return false;
    if (!m_debugUI.Init(hwnd, m_device.Get(), m_context.Get()))
    {
        MessageBoxW(hwnd, L"ImGui 디버그 UI 초기화 실패", L"오류", MB_OK | MB_ICONERROR);
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
    ++m_runtimeCompileCount;
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
    ComPtr<ID3DBlob> vsBlob, psBlob, previewVsBlob, previewPsBlob, noiseCsBaseBlob, noiseCsDetailBlob;

    if (!CompileShaderFromFile(m_vsPath, "main", "vs_5_0", vsBlob, showErrors))
        return false;
    if (!CompileShaderFromFile(m_psPath, "main", "ps_5_0", psBlob, showErrors))
        return false;
    if (!CompileShaderFromFile(m_previewPath, "VSMain", "vs_5_0", previewVsBlob, showErrors))
        return false;
    if (!CompileShaderFromFile(m_previewPath, "PSMain", "ps_5_0", previewPsBlob, showErrors))
        return false;
    if (!CompileShaderFromFile(m_noiseVolumeCsPath, "CSBase", "cs_5_0", noiseCsBaseBlob, showErrors))
        return false;
    if (!CompileShaderFromFile(m_noiseVolumeCsPath, "CSDetail", "cs_5_0", noiseCsDetailBlob, showErrors))
        return false;

    ShaderBlobArray blobs;
    blobs[static_cast<size_t>(CachedShader::MainVS)] = vsBlob;
    blobs[static_cast<size_t>(CachedShader::MainPS)] = psBlob;
    blobs[static_cast<size_t>(CachedShader::PreviewVS)] = previewVsBlob;
    blobs[static_cast<size_t>(CachedShader::PreviewPS)] = previewPsBlob;
    blobs[static_cast<size_t>(CachedShader::NoiseCSBase)] = noiseCsBaseBlob;
    blobs[static_cast<size_t>(CachedShader::NoiseCSDetail)] = noiseCsDetailBlob;
    return CreateShadersFromBlobs(blobs);
}

bool Renderer::CreateShadersFromBlobs(const ShaderBlobArray& blobs)
{
    const auto& vsBlob = blobs[static_cast<size_t>(CachedShader::MainVS)];
    const auto& psBlob = blobs[static_cast<size_t>(CachedShader::MainPS)];
    const auto& previewVsBlob = blobs[static_cast<size_t>(CachedShader::PreviewVS)];
    const auto& previewPsBlob = blobs[static_cast<size_t>(CachedShader::PreviewPS)];
    const auto& noiseCsBaseBlob = blobs[static_cast<size_t>(CachedShader::NoiseCSBase)];
    const auto& noiseCsDetailBlob = blobs[static_cast<size_t>(CachedShader::NoiseCSDetail)];
    for (const auto& blob : blobs) if (!blob) return false;

    ComPtr<ID3D11VertexShader> newVs;
    ComPtr<ID3D11PixelShader>  newPs;
    ComPtr<ID3D11VertexShader> newPreviewVs;
    ComPtr<ID3D11PixelShader>  newPreviewPs;
    ComPtr<ID3D11ComputeShader> newNoiseCsBase;
    ComPtr<ID3D11ComputeShader> newNoiseCsDetail;

    HRESULT hr = m_device->CreateVertexShader(
        vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &newVs);
    if (FAILED(hr)) return false;

    hr = m_device->CreatePixelShader(
        psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &newPs);
    if (FAILED(hr)) return false;

    hr = m_device->CreateVertexShader(
        previewVsBlob->GetBufferPointer(), previewVsBlob->GetBufferSize(), nullptr, &newPreviewVs);
    if (FAILED(hr)) return false;

    hr = m_device->CreatePixelShader(
        previewPsBlob->GetBufferPointer(), previewPsBlob->GetBufferSize(), nullptr, &newPreviewPs);
    if (FAILED(hr)) return false;

    hr = m_device->CreateComputeShader(
        noiseCsBaseBlob->GetBufferPointer(), noiseCsBaseBlob->GetBufferSize(), nullptr, &newNoiseCsBase);
    if (FAILED(hr)) return false;

    hr = m_device->CreateComputeShader(
        noiseCsDetailBlob->GetBufferPointer(), noiseCsDetailBlob->GetBufferSize(), nullptr, &newNoiseCsDetail);
    if (FAILED(hr)) return false;

    m_vs = newVs;
    m_ps = newPs;
    m_previewVs = newPreviewVs;
    m_previewPs = newPreviewPs;
    m_noiseVolumeCsBase = newNoiseCsBase;
    m_noiseVolumeCsDetail = newNoiseCsDetail;
    m_shaderBlobs = blobs;
    m_previewDirty = true;
    m_noiseCacheDirty = true;

    // 입력 레이아웃 없음: 정점은 SV_VertexID 로 셰이더 내부에서 생성
    return true;
}

void Renderer::UpdateShaderWriteTimes()
{
    m_shaderWriteTimes.clear();
    for (const std::wstring& path : m_shaderPaths)
    {
        std::error_code ec;
        m_shaderWriteTimes.push_back(std::filesystem::last_write_time(path, ec));
    }
}

void Renderer::CheckShaderHotReload()
{
    std::vector<std::filesystem::file_time_type> current;
    bool changed = m_sourceModified || m_shaderWriteTimes.size() != m_shaderPaths.size();
    for (size_t i = 0; i < m_shaderPaths.size(); ++i)
    {
        std::error_code ec;
        auto time = std::filesystem::last_write_time(m_shaderPaths[i], ec);
        if (ec) return;
        current.push_back(time);
        if (i >= m_shaderWriteTimes.size() || time != m_shaderWriteTimes[i]) changed = true;
    }
    if (!changed) return;

    // 실패해도 기존 셰이더는 유지하고, 같은 오류를 매 프레임 반복하지 않는다.
    m_cacheStatus = "Compiling";
    if (CreateShaders(false))
    {
        m_sourceModified = false;
        m_cacheStatus = "Unsaved";
    }
    else
    {
        m_cacheStatus = "Error";
    }
    m_shaderWriteTimes = current;
}

bool Renderer::CreateNoisePreviewResources()
{
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = 256;
    td.Height = 256;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    for (size_t i = 0; i < m_previewTextures.size(); ++i)
    {
        if (FAILED(m_device->CreateTexture2D(&td, nullptr, &m_previewTextures[i]))) return false;
        if (FAILED(m_device->CreateRenderTargetView(m_previewTextures[i].Get(), nullptr, &m_previewRtvs[i]))) return false;
        if (FAILED(m_device->CreateShaderResourceView(m_previewTextures[i].Get(), nullptr, &m_previewSrvs[i]))) return false;
    }
    return true;
}

bool Renderer::CreateNoiseVolumeResources()
{
    const UINT sizes[2] = { 128, 64 };
    // base=R8(실루엣 단일값), detail=RGBA8(Worley 옥타브 4채널)
    const DXGI_FORMAT formats[2] = { DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM };
    for (size_t i = 0; i < 2; ++i)
    {
        D3D11_TEXTURE3D_DESC td = {};
        td.Width = sizes[i];
        td.Height = sizes[i];
        td.Depth = sizes[i];
        td.MipLevels = 1;
        td.Format = formats[i];
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        if (FAILED(m_device->CreateTexture3D(&td, nullptr, &m_noiseVolumes[i]))) return false;
        if (FAILED(m_device->CreateUnorderedAccessView(m_noiseVolumes[i].Get(), nullptr, &m_noiseVolumeUavs[i]))) return false;
        if (FAILED(m_device->CreateShaderResourceView(m_noiseVolumes[i].Get(), nullptr, &m_noiseVolumeSrvs[i]))) return false;
    }

    return true;
}

bool Renderer::CreateNoiseSampler()
{
    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    return SUCCEEDED(m_device->CreateSamplerState(&sd, &m_noiseSampler));
}

void Renderer::GenerateNoiseVolumes()
{
    ID3D11ShaderResourceView* nullSrvs[2] = { nullptr, nullptr };
    m_context->PSSetShaderResources(0, 2, nullSrvs);
    m_context->CSSetConstantBuffers(1, 1, m_cloudCb.GetAddressOf());
    m_context->CSSetConstantBuffers(2, 1, m_noiseVolumeGenerationCb.GetAddressOf());

    // base(R8)와 detail(RGBA8)은 UAV 타입이 달라 셰이더/레지스터도 나뉜다.
    // base → CSBase, outputBase(u0) / detail → CSDetail, outputDetail(u2). (u1은 CSSeamTest 전용)
    const UINT sizes[2] = { 128, 64 };
    ID3D11ComputeShader* shaders[2] = { m_noiseVolumeCsBase.Get(), m_noiseVolumeCsDetail.Get() };
    const UINT uavSlots[2] = { 0, 2 };
    for (UINT i = 0; i < 2; ++i)
    {
        NoiseVolumeGenerationCB cb = { sizes[i], i, { 0, 0 } };
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (SUCCEEDED(m_context->Map(m_noiseVolumeGenerationCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            memcpy(mapped.pData, &cb, sizeof(cb));
            m_context->Unmap(m_noiseVolumeGenerationCb.Get(), 0);
        }
        m_context->CSSetShader(shaders[i], nullptr, 0);
        ID3D11UnorderedAccessView* uav = m_noiseVolumeUavs[i].Get();
        m_context->CSSetUnorderedAccessViews(uavSlots[i], 1, &uav, nullptr);
        const UINT groups = (sizes[i] + 3) / 4;
        m_context->Dispatch(groups, groups, groups);
        ++m_noiseDispatchCount;
        ID3D11UnorderedAccessView* nullUav = nullptr;
        m_context->CSSetUnorderedAccessViews(uavSlots[i], 1, &nullUav, nullptr);
    }
    m_context->CSSetShader(nullptr, nullptr, 0);
    m_noiseCacheDirty = false;
    m_previewDirty = true;
}

void Renderer::RenderNoisePreview(float previewTime)
{
    NoisePreviewCB cb = { m_previewSettings.axis, m_previewSettings.slice, previewTime, 0 };
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(m_context->Map(m_previewCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        memcpy(mapped.pData, &cb, sizeof(cb));
        m_context->Unmap(m_previewCb.Get(), 0);
    }

    ID3D11RenderTargetView* rtvs[4] = {};
    for (size_t i = 0; i < 4; ++i) rtvs[i] = m_previewRtvs[i].Get();
    m_context->OMSetRenderTargets(4, rtvs, nullptr);

    D3D11_VIEWPORT vp = {};
    vp.Width = 256.0f;
    vp.Height = 256.0f;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);
    m_context->IASetInputLayout(nullptr);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_previewVs.Get(), nullptr, 0);
    m_context->PSSetShader(m_previewPs.Get(), nullptr, 0);
    m_context->PSSetConstantBuffers(1, 1, m_cloudCb.GetAddressOf());
    m_context->PSSetConstantBuffers(2, 1, m_previewCb.GetAddressOf());
    ID3D11ShaderResourceView* noiseSrvs[2] = { m_noiseVolumeSrvs[0].Get(), m_noiseVolumeSrvs[1].Get() };
    m_context->PSSetShaderResources(0, 2, noiseSrvs);
    m_context->PSSetSamplers(0, 1, m_noiseSampler.GetAddressOf());
    m_context->Draw(3, 0);
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    m_previewDirty = false;
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

bool Renderer::HandleWindowMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return m_debugUI.HandleMessage(hwnd, msg, wParam, lParam);
}

bool Renderer::WantsMouseCapture() const
{
    return m_debugUI.WantsMouseCapture();
}

bool Renderer::WantsKeyboardCapture() const
{
    return m_debugUI.WantsKeyboardCapture();
}

void Renderer::ToggleDebugUI()
{
    m_debugUI.ToggleVisible();
}

void Renderer::Render(const Camera& camera, float timeSeconds)
{
    if (!m_rtv) return;

    const float cpuFrameMs = m_lastFrameTime > 0.0f ? (timeSeconds - m_lastFrameTime) * 1000.0f : 0.0f;
    m_lastFrameTime = timeSeconds;
    m_debugUI.BeginFrame();

    std::array<ID3D11ShaderResourceView*, 4> previewViews = {};
    for (size_t i = 0; i < previewViews.size(); ++i) previewViews[i] = m_previewSrvs[i].Get();
    const CloudParameters previousParams = m_cloudParams;
    NoiseCacheUiActions cacheActions;
    if (m_debugUI.Draw(m_cloudParams, m_previewSettings, previewViews, m_previewDirty, cpuFrameMs,
                       m_cacheStatus, cacheActions))
    {
        m_previewDirty = true;
        if (previousParams.noiseWorldScale != m_cloudParams.noiseWorldScale ||
            previousParams.basePeriod != m_cloudParams.basePeriod ||
            previousParams.detailPeriod != m_cloudParams.detailPeriod ||
            previousParams.seed != m_cloudParams.seed ||
            previousParams.baseOctaves != m_cloudParams.baseOctaves ||
            previousParams.detailOctaves != m_cloudParams.detailOctaves)
        {
            m_noiseCacheDirty = true;
            m_cacheStatus = "Unsaved";
        }
    }

    if (cacheActions.rebuild)
    {
        m_noiseCacheDirty = true;
        m_cacheStatus = "Generating";
    }
    if (cacheActions.revert)
    {
        bool modified = false;
        CloudParameters loadedParams;
        ShaderBlobArray loadedBlobs;
        std::array<ComPtr<ID3D11Texture3D>, 2> loadedVolumes;
        std::array<ComPtr<ID3D11UnorderedAccessView>, 2> loadedUavs;
        std::array<ComPtr<ID3D11ShaderResourceView>, 2> loadedSrvs;
        if (m_noiseCacheManager.LoadPreferred(m_device.Get(), loadedParams, loadedBlobs,
                                               loadedVolumes, loadedUavs, loadedSrvs, modified) &&
            CreateShadersFromBlobs(loadedBlobs))
        {
            m_cloudParams = loadedParams;
            m_noiseVolumes = loadedVolumes;
            m_noiseVolumeUavs = loadedUavs;
            m_noiseVolumeSrvs = loadedSrvs;
            m_noiseCacheDirty = false;
            m_sourceModified = modified;
            m_cacheStatus = modified ? "Source Modified" : "Saved";
        }
        else m_cacheStatus = "Error";
    }

    const float cloudTime = m_previewSettings.freeze ? m_previewSettings.previewTime : timeSeconds;
    if (!m_previewSettings.freeze) m_previewDirty = true;

    // ---- 상수버퍼 갱신 ----
    CameraCB cb = {};
    // HLSL은 mul(vector, matrix) 규약 → DirectXMath 행렬을 transpose 해서 업로드
    XMStoreFloat4x4(&cb.invViewProj, XMMatrixTranspose(camera.GetInvViewProj()));
    cb.cameraPos      = camera.GetPosition();
    cb.time           = cloudTime;
    cb.volumeCenter   = XMFLOAT3(0.0f, 0.0f, 0.0f); // 원점에 놓인 박스 볼륨
    cb.densityScale   = 1.0f;                        // 최종 배율은 CloudCB가 제어
    cb.volumeHalfSize = XMFLOAT3(2.0f, 1.2f, 1.2f);  // AABB 절반 크기
    cb._pad           = 0.0f;

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(m_context->Map(m_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        memcpy(mapped.pData, &cb, sizeof(cb));
        m_context->Unmap(m_cb.Get(), 0);
    }

    if (SUCCEEDED(m_context->Map(m_cloudCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        memcpy(mapped.pData, &m_cloudParams, sizeof(m_cloudParams));
        m_context->Unmap(m_cloudCb.Get(), 0);
    }

    if (m_noiseCacheDirty)
    {
        m_cacheStatus = "Generating";
        GenerateNoiseVolumes();
        m_cacheStatus = "Unsaved";
    }
    if (cacheActions.save)
    {
        m_cacheStatus = "Saving";
        m_cacheStatus = m_noiseCacheManager.SaveUser(
            m_device.Get(), m_context.Get(), m_cloudParams, m_shaderBlobs, m_noiseVolumes)
            ? "Saved" : "Error";
    }
    // 패널이 기본 숨김인 동안에는 비싼 4-MRT 절차식 미리보기를 만들지 않는다.
    // F1로 처음 열면 유지된 dirty 플래그에 의해 즉시 한 번 생성된다.
    if (m_previewDirty && m_debugUI.IsVisible())
        RenderNoisePreview(cloudTime);

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
    m_context->PSSetConstantBuffers(1, 1, m_cloudCb.GetAddressOf());
    ID3D11ShaderResourceView* noiseSrvs[2] = { m_noiseVolumeSrvs[0].Get(), m_noiseVolumeSrvs[1].Get() };
    m_context->PSSetShaderResources(0, 2, noiseSrvs);
    m_context->PSSetSamplers(0, 1, m_noiseSampler.GetAddressOf());

    m_context->Draw(3, 0); // 정점 3개 = 화면을 덮는 삼각형

    // 구름 위에 디버그 UI를 합성한 뒤 Present.
    m_debugUI.EndFrame();
    m_swapChain->Present(1, 0); // vsync
    m_hasPresented = true;
    CheckShaderHotReload();
}

bool Renderer::SaveDefaultNoiseCache()
{
    if (m_noiseCacheDirty) GenerateNoiseVolumes();
    return m_noiseCacheManager.SaveDefault(
        m_device.Get(), m_context.Get(), m_cloudParams, m_shaderBlobs, m_noiseVolumes);
}

bool Renderer::RunCodeTests()
{
    ComPtr<ID3DBlob> testBlob;
    if (!CompileShaderFromFile(m_noiseVolumeCsPath, "CSSeamTest", "cs_5_0", testBlob, false))
        return false;
    ComPtr<ID3D11ComputeShader> testShader;
    if (FAILED(m_device->CreateComputeShader(testBlob->GetBufferPointer(), testBlob->GetBufferSize(),
                                              nullptr, &testShader))) return false;

    const UINT zero = 0;
    D3D11_BUFFER_DESC resultDesc = {};
    resultDesc.ByteWidth = sizeof(UINT);
    resultDesc.Usage = D3D11_USAGE_DEFAULT;
    resultDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    resultDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    resultDesc.StructureByteStride = sizeof(UINT);
    D3D11_SUBRESOURCE_DATA initial = { &zero, 0, 0 };
    ComPtr<ID3D11Buffer> result;
    if (FAILED(m_device->CreateBuffer(&resultDesc, &initial, &result))) return false;

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.NumElements = 1;
    ComPtr<ID3D11UnorderedAccessView> resultUav;
    if (FAILED(m_device->CreateUnorderedAccessView(result.Get(), &uavDesc, &resultUav))) return false;

    m_context->CSSetShader(testShader.Get(), nullptr, 0);
    m_context->CSSetConstantBuffers(1, 1, m_cloudCb.GetAddressOf());
    ID3D11UnorderedAccessView* uav = resultUav.Get();
    m_context->CSSetUnorderedAccessViews(1, 1, &uav, nullptr);
    m_context->Dispatch(16, 1, 1); // 1024 deterministic samples
    ID3D11UnorderedAccessView* nullUav = nullptr;
    m_context->CSSetUnorderedAccessViews(1, 1, &nullUav, nullptr);
    m_context->CSSetShader(nullptr, nullptr, 0);

    D3D11_BUFFER_DESC stagingDesc = resultDesc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;
    ComPtr<ID3D11Buffer> staging;
    if (FAILED(m_device->CreateBuffer(&stagingDesc, nullptr, &staging))) return false;
    m_context->CopyResource(staging.Get(), result.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(m_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
    float maxError = 0.0f;
    const UINT errorBits = *static_cast<const UINT*>(mapped.pData);
    std::memcpy(&maxError, &errorBits, sizeof(maxError));
    m_context->Unmap(staging.Get(), 0);

    D3D11_TEXTURE3D_DESC baseDesc = {};
    D3D11_TEXTURE3D_DESC detailDesc = {};
    m_noiseVolumes[0]->GetDesc(&baseDesc);
    m_noiseVolumes[1]->GetDesc(&detailDesc);
    const bool volumeLayout =
        baseDesc.Width == 128 && baseDesc.Height == 128 && baseDesc.Depth == 128 &&
        detailDesc.Width == 64 && detailDesc.Height == 64 && detailDesc.Depth == 64 &&
        baseDesc.Format == DXGI_FORMAT_R8_UNORM &&        // 실루엣 단일값
        detailDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM && // Worley 옥타브 4채널
        static_cast<uint64_t>(baseDesc.Width) * baseDesc.Height * baseDesc.Depth * 1 == 2097152ull &&
        static_cast<uint64_t>(detailDesc.Width) * detailDesc.Height * detailDesc.Depth * 4 == 1048576ull;

    // Beer-Lambert와 HG의 CPU 기준값. 셰이더와 같은 범위에서 수치 성질을 검사한다.
    const float density = 0.8f;
    const float nearT = std::exp(-density * 0.5f);
    const float farT = std::exp(-density * 1.0f);
    const float g = 0.55f;
    const auto hg = [g](float cosine)
    {
        const float g2 = g * g;
        return (1.0f - g2) / std::pow((std::max)(1.0f + g2 - 2.0f * g * cosine, 0.001f), 1.5f);
    };
    const bool lightingMath = farT <= nearT && nearT <= 1.0f && farT >= 0.0f &&
                              hg(-1.0f) > 0.0f && hg(1.0f) > hg(0.0f) && hg(0.0f) > hg(-1.0f);
    const bool seamless = maxError <= 1.0e-5f;
    const bool roundTrip = m_noiseCacheManager.RunRoundTripTest(
        m_device.Get(), m_context.Get(), m_cloudParams, m_shaderBlobs, m_noiseVolumes);
    bool debugLayerClean = true;
#ifdef _DEBUG
    ComPtr<ID3D11InfoQueue> infoQueue;
    if (SUCCEEDED(m_device.As(&infoQueue)))
    {
        const UINT64 count = infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
        for (UINT64 i = 0; i < count; ++i)
        {
            SIZE_T size = 0;
            infoQueue->GetMessage(i, nullptr, &size);
            std::vector<unsigned char> storage(size);
            auto* message = reinterpret_cast<D3D11_MESSAGE*>(storage.data());
            if (SUCCEEDED(infoQueue->GetMessage(i, message, &size)) &&
                message->Severity <= D3D11_MESSAGE_SEVERITY_WARNING)
            {
                debugLayerClean = false;
                break;
            }
        }
    }
#endif
    return seamless && roundTrip && volumeLayout && lightingMath && debugLayerClean;
}
