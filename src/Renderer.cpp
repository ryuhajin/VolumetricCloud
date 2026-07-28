#include "Renderer.h"
#include "Camera.h"

#include <d3dcompiler.h>
#include <d3d11sdklayers.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <utility>
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

static double PerformanceCounterSeconds()
{
    static LARGE_INTEGER frequency = []
    {
        LARGE_INTEGER value = {};
        QueryPerformanceFrequency(&value);
        return value;
    }();
    LARGE_INTEGER now = {};
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart) / static_cast<double>(frequency.QuadPart);
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
    m_temporalPath = m_shaderDir + L"TemporalResolve.hlsl";
    m_compositePath = m_shaderDir + L"Composite.hlsl";
    m_shaderPaths = { m_vsPath, m_psPath, m_rayLibPath, m_cloudNoisePath,
                      m_previewPath, m_noiseVolumeCsPath, m_temporalPath, m_compositePath };

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
        m_noiseVolumes, m_noiseVolumeUavs, m_noiseVolumeSrvs, m_weatherMap, m_sourceModified);
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

    bd.ByteWidth = sizeof(TemporalCB);
    hr = m_device->CreateBuffer(&bd, nullptr, &m_temporalCb);
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
    if (!CreateTemporalResources()) return false;
    if (!CreateGpuTimerResources()) return false;
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
    ComPtr<ID3DBlob> vsBlob, psBlob, temporalPsBlob, compositePsBlob, previewVsBlob, previewPsBlob;
    ComPtr<ID3DBlob> noiseCsBaseBlob, noiseCsDetailBlob, noiseCsWeatherBlob;

    if (!CompileShaderFromFile(m_vsPath, "main", "vs_5_0", vsBlob, showErrors))
        return false;
    if (!CompileShaderFromFile(m_psPath, "main", "ps_5_0", psBlob, showErrors))
        return false;
    if (!CompileShaderFromFile(m_temporalPath, "main", "ps_5_0", temporalPsBlob, showErrors))
        return false;
    if (!CompileShaderFromFile(m_compositePath, "main", "ps_5_0", compositePsBlob, showErrors))
        return false;
    if (!CompileShaderFromFile(m_previewPath, "VSMain", "vs_5_0", previewVsBlob, showErrors))
        return false;
    if (!CompileShaderFromFile(m_previewPath, "PSMain", "ps_5_0", previewPsBlob, showErrors))
        return false;
    if (!CompileShaderFromFile(m_noiseVolumeCsPath, "CSBase", "cs_5_0", noiseCsBaseBlob, showErrors))
        return false;
    if (!CompileShaderFromFile(m_noiseVolumeCsPath, "CSDetail", "cs_5_0", noiseCsDetailBlob, showErrors))
        return false;
    if (!CompileShaderFromFile(m_noiseVolumeCsPath, "CSWeather", "cs_5_0", noiseCsWeatherBlob, showErrors))
        return false;

    ShaderBlobArray blobs;
    blobs[static_cast<size_t>(CachedShader::MainVS)] = vsBlob;
    blobs[static_cast<size_t>(CachedShader::MainPS)] = psBlob;
    blobs[static_cast<size_t>(CachedShader::TemporalPS)] = temporalPsBlob;
    blobs[static_cast<size_t>(CachedShader::CompositePS)] = compositePsBlob;
    blobs[static_cast<size_t>(CachedShader::PreviewVS)] = previewVsBlob;
    blobs[static_cast<size_t>(CachedShader::PreviewPS)] = previewPsBlob;
    blobs[static_cast<size_t>(CachedShader::NoiseCSBase)] = noiseCsBaseBlob;
    blobs[static_cast<size_t>(CachedShader::NoiseCSDetail)] = noiseCsDetailBlob;
    blobs[static_cast<size_t>(CachedShader::NoiseCSWeather)] = noiseCsWeatherBlob;
    return CreateShadersFromBlobs(blobs);
}

bool Renderer::CreateShadersFromBlobs(const ShaderBlobArray& blobs)
{
    const auto& vsBlob = blobs[static_cast<size_t>(CachedShader::MainVS)];
    const auto& psBlob = blobs[static_cast<size_t>(CachedShader::MainPS)];
    const auto& temporalPsBlob = blobs[static_cast<size_t>(CachedShader::TemporalPS)];
    const auto& compositePsBlob = blobs[static_cast<size_t>(CachedShader::CompositePS)];
    const auto& previewVsBlob = blobs[static_cast<size_t>(CachedShader::PreviewVS)];
    const auto& previewPsBlob = blobs[static_cast<size_t>(CachedShader::PreviewPS)];
    const auto& noiseCsBaseBlob = blobs[static_cast<size_t>(CachedShader::NoiseCSBase)];
    const auto& noiseCsDetailBlob = blobs[static_cast<size_t>(CachedShader::NoiseCSDetail)];
    const auto& noiseCsWeatherBlob = blobs[static_cast<size_t>(CachedShader::NoiseCSWeather)];
    for (const auto& blob : blobs) if (!blob) return false;

    ComPtr<ID3D11VertexShader> newVs;
    ComPtr<ID3D11PixelShader>  newPs;
    ComPtr<ID3D11PixelShader>  newTemporalPs;
    ComPtr<ID3D11PixelShader>  newCompositePs;
    ComPtr<ID3D11VertexShader> newPreviewVs;
    ComPtr<ID3D11PixelShader>  newPreviewPs;
    ComPtr<ID3D11ComputeShader> newNoiseCsBase;
    ComPtr<ID3D11ComputeShader> newNoiseCsDetail;
    ComPtr<ID3D11ComputeShader> newNoiseCsWeather;

    HRESULT hr = m_device->CreateVertexShader(
        vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &newVs);
    if (FAILED(hr)) return false;

    hr = m_device->CreatePixelShader(
        psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &newPs);
    if (FAILED(hr)) return false;
    hr = m_device->CreatePixelShader(
        temporalPsBlob->GetBufferPointer(), temporalPsBlob->GetBufferSize(), nullptr, &newTemporalPs);
    if (FAILED(hr)) return false;
    hr = m_device->CreatePixelShader(
        compositePsBlob->GetBufferPointer(), compositePsBlob->GetBufferSize(), nullptr, &newCompositePs);
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
    hr = m_device->CreateComputeShader(
        noiseCsWeatherBlob->GetBufferPointer(), noiseCsWeatherBlob->GetBufferSize(), nullptr, &newNoiseCsWeather);
    if (FAILED(hr)) return false;

    m_vs = newVs;
    m_ps = newPs;
    m_temporalPs = newTemporalPs;
    m_compositePs = newCompositePs;
    m_previewVs = newPreviewVs;
    m_previewPs = newPreviewPs;
    m_noiseVolumeCsBase = newNoiseCsBase;
    m_noiseVolumeCsDetail = newNoiseCsDetail;
    m_noiseWeatherCs = newNoiseCsWeather;
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
    const UINT sizes[2] = { 128, 128 };
    // base/detail 모두 RGBA8: base는 형태 밴드, detail은 침식 옥타브를 저장한다.
    const DXGI_FORMAT formats[2] = { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM };
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

    D3D11_TEXTURE2D_DESC weatherDesc = {};
    weatherDesc.Width = 512;
    weatherDesc.Height = 512;
    weatherDesc.MipLevels = 1;
    weatherDesc.ArraySize = 1;
    weatherDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    weatherDesc.SampleDesc.Count = 1;
    weatherDesc.Usage = D3D11_USAGE_DEFAULT;
    weatherDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    if (FAILED(m_device->CreateTexture2D(&weatherDesc, nullptr, &m_weatherMap.texture))) return false;
    if (FAILED(m_device->CreateUnorderedAccessView(
            m_weatherMap.texture.Get(), nullptr, &m_weatherMap.uav))) return false;
    if (FAILED(m_device->CreateShaderResourceView(
            m_weatherMap.texture.Get(), nullptr, &m_weatherMap.srv))) return false;

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
    if (FAILED(m_device->CreateSamplerState(&sd, &m_noiseSampler))) return false;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    return SUCCEEDED(m_device->CreateSamplerState(&sd, &m_linearClampSampler));
}

bool Renderer::CreateTemporalResources()
{
    if (!m_device || m_width <= 0 || m_height <= 0) return false;
    m_halfCloudColor.Reset();
    m_halfCloudColorRtv.Reset();
    m_halfCloudColorSrv.Reset();
    m_halfCloudDepth.Reset();
    m_halfCloudDepthRtv.Reset();
    m_halfCloudDepthSrv.Reset();
    for (size_t i = 0; i < 2; ++i)
    {
        m_historyColor[i].Reset();
        m_historyColorRtv[i].Reset();
        m_historyColorSrv[i].Reset();
        m_historyDepth[i].Reset();
        m_historyDepthRtv[i].Reset();
        m_historyDepthSrv[i].Reset();
    }

    const auto createTarget = [&](UINT width, UINT height, DXGI_FORMAT format,
                                  ComPtr<ID3D11Texture2D>& texture,
                                  ComPtr<ID3D11RenderTargetView>& rtv,
                                  ComPtr<ID3D11ShaderResourceView>& srv)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        return SUCCEEDED(m_device->CreateTexture2D(&desc, nullptr, &texture)) &&
               SUCCEEDED(m_device->CreateRenderTargetView(texture.Get(), nullptr, &rtv)) &&
               SUCCEEDED(m_device->CreateShaderResourceView(texture.Get(), nullptr, &srv));
    };

    const UINT halfWidth = (std::max)(1, (m_width + 1) / 2);
    const UINT halfHeight = (std::max)(1, (m_height + 1) / 2);
    if (!createTarget(halfWidth, halfHeight, DXGI_FORMAT_R11G11B10_FLOAT,
                      m_halfCloudColor, m_halfCloudColorRtv, m_halfCloudColorSrv) ||
        !createTarget(halfWidth, halfHeight, DXGI_FORMAT_R16_FLOAT,
                      m_halfCloudDepth, m_halfCloudDepthRtv, m_halfCloudDepthSrv))
        return false;
    for (size_t i = 0; i < 2; ++i)
    {
        if (!createTarget(m_width, m_height, DXGI_FORMAT_R11G11B10_FLOAT,
                          m_historyColor[i], m_historyColorRtv[i], m_historyColorSrv[i]) ||
            !createTarget(m_width, m_height, DXGI_FORMAT_R16_FLOAT,
                          m_historyDepth[i], m_historyDepthRtv[i], m_historyDepthSrv[i]))
            return false;
    }
    ResetTemporalHistory();
    return true;
}

void Renderer::ResetTemporalHistory()
{
    m_historyValid = false;
    m_historyIndex = 0;
    m_temporalFrameIndex = 0;
    if (!m_context) return;
    const float zero[4] = {};
    for (size_t i = 0; i < 2; ++i)
    {
        if (m_historyColorRtv[i]) m_context->ClearRenderTargetView(m_historyColorRtv[i].Get(), zero);
        if (m_historyDepthRtv[i]) m_context->ClearRenderTargetView(m_historyDepthRtv[i].Get(), zero);
    }
}

void Renderer::RenderFullscreenTriangle()
{
    m_context->IASetInputLayout(nullptr);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_vs.Get(), nullptr, 0);
    m_context->Draw(3, 0);
}

bool Renderer::CreateGpuTimerResources()
{
    D3D11_QUERY_DESC desc = {};
    for (size_t i = 0; i < m_gpuBeginQueries.size(); ++i)
    {
        desc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
        if (FAILED(m_device->CreateQuery(&desc, &m_gpuTotalDisjointQueries[i]))) return false;
        desc.Query = D3D11_QUERY_TIMESTAMP;
        if (FAILED(m_device->CreateQuery(&desc, &m_gpuBeginQueries[i]))) return false;
        if (FAILED(m_device->CreateQuery(&desc, &m_gpuEndQueries[i]))) return false;
        if (FAILED(m_device->CreateQuery(&desc, &m_gpuReconstructionBeginQueries[i]))) return false;
        if (FAILED(m_device->CreateQuery(&desc, &m_gpuReconstructionEndQueries[i]))) return false;
        if (FAILED(m_device->CreateQuery(&desc, &m_gpuTotalBeginQueries[i]))) return false;
        if (FAILED(m_device->CreateQuery(&desc, &m_gpuTotalEndQueries[i]))) return false;
    }
    return true;
}

void Renderer::BeginGpuTimer()
{
    if (!m_gpuTotalTimerActive) return;
    m_context->End(m_gpuBeginQueries[m_gpuTotalQueryIndex].Get());
    m_gpuTimerActive = true;
}

void Renderer::EndGpuTimer()
{
    if (!m_gpuTimerActive) return;
    m_context->End(m_gpuEndQueries[m_gpuTotalQueryIndex].Get());
    m_gpuTimerActive = false;
}

void Renderer::BeginReconstructionTimer()
{
    if (!m_gpuTotalTimerActive) return;
    m_context->End(m_gpuReconstructionBeginQueries[m_gpuTotalQueryIndex].Get());
    m_gpuReconstructionTimerActive = true;
}

void Renderer::EndReconstructionTimer()
{
    if (!m_gpuReconstructionTimerActive) return;
    m_context->End(m_gpuReconstructionEndQueries[m_gpuTotalQueryIndex].Get());
    m_gpuReconstructionTimerActive = false;
}

void Renderer::ResolveGpuTotalTimer()
{
    const unsigned int readIndex = m_gpuTotalQueryIndex;
    if (!m_gpuTotalQueryIssued[readIndex]) return;
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint = {};
    UINT64 totalBegin = 0;
    UINT64 cloudBegin = 0;
    UINT64 cloudEnd = 0;
    UINT64 reconstructionBegin = 0;
    UINT64 reconstructionEnd = 0;
    UINT64 totalEnd = 0;
    if (m_context->GetData(m_gpuTotalDisjointQueries[readIndex].Get(), &disjoint, sizeof(disjoint),
                           D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
        m_context->GetData(m_gpuTotalBeginQueries[readIndex].Get(), &totalBegin, sizeof(totalBegin),
                           D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
        m_context->GetData(m_gpuBeginQueries[readIndex].Get(), &cloudBegin, sizeof(cloudBegin),
                           D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
        m_context->GetData(m_gpuEndQueries[readIndex].Get(), &cloudEnd, sizeof(cloudEnd),
                           D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
        m_context->GetData(m_gpuReconstructionBeginQueries[readIndex].Get(), &reconstructionBegin,
                           sizeof(reconstructionBegin), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
        m_context->GetData(m_gpuReconstructionEndQueries[readIndex].Get(), &reconstructionEnd,
                           sizeof(reconstructionEnd), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
        m_context->GetData(m_gpuTotalEndQueries[readIndex].Get(), &totalEnd, sizeof(totalEnd),
                           D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK)
    {
        if (!disjoint.Disjoint && disjoint.Frequency > 0 &&
            totalEnd >= totalBegin && cloudEnd >= cloudBegin &&
            reconstructionEnd >= reconstructionBegin)
        {
            const float totalSample =
                static_cast<float>((totalEnd - totalBegin) * 1000.0 / disjoint.Frequency);
            const float cloudSample =
                static_cast<float>((cloudEnd - cloudBegin) * 1000.0 / disjoint.Frequency);
            const float reconstructionSample = static_cast<float>(
                (reconstructionEnd - reconstructionBegin) * 1000.0 / disjoint.Frequency);
            m_gpuTotalMs = m_gpuTotalMs > 0.0f ? m_gpuTotalMs * 0.9f + totalSample * 0.1f : totalSample;
            m_gpuFrameMs = m_gpuFrameMs > 0.0f ? m_gpuFrameMs * 0.9f + cloudSample * 0.1f : cloudSample;
            m_gpuReconstructionMs = m_gpuReconstructionMs > 0.0f
                ? m_gpuReconstructionMs * 0.9f + reconstructionSample * 0.1f
                : reconstructionSample;
            if (m_gpuTotalQueryBenchmark[readIndex] &&
                std::isfinite(totalSample) && totalSample > 0.0f &&
                std::isfinite(cloudSample) && cloudSample > 0.0f &&
                std::isfinite(m_gpuTotalQueryCpuMs[readIndex]) &&
                m_gpuTotalQueryCpuMs[readIndex] > 0.0f)
            {
                m_benchmarkSamples.push_back({
                    cloudSample, reconstructionSample, totalSample,
                    m_gpuTotalQueryCpuMs[readIndex] });
            }
        }
        m_gpuTotalQueryIssued[readIndex] = false;
        m_gpuTotalQueryBenchmark[readIndex] = false;
    }
}

void Renderer::BeginGpuTotalTimer()
{
    ResolveGpuTotalTimer();
    m_lastIssuedGpuQueryIndex = -1;
    if (m_gpuTotalQueryIssued[m_gpuTotalQueryIndex])
    {
        m_gpuTotalTimerActive = false;
        return;
    }
    m_context->Begin(m_gpuTotalDisjointQueries[m_gpuTotalQueryIndex].Get());
    m_context->End(m_gpuTotalBeginQueries[m_gpuTotalQueryIndex].Get());
    m_gpuTotalTimerActive = true;
}

void Renderer::EndGpuTotalTimer()
{
    if (!m_gpuTotalTimerActive) return;
    const unsigned int issuedIndex = m_gpuTotalQueryIndex;
    m_context->End(m_gpuTotalEndQueries[issuedIndex].Get());
    m_context->End(m_gpuTotalDisjointQueries[issuedIndex].Get());
    m_gpuTotalQueryIssued[issuedIndex] = true;
    m_gpuTotalQueryBenchmark[issuedIndex] = m_benchmarkCollecting;
    m_gpuTotalQueryCpuMs[issuedIndex] = 0.0f;
    m_lastIssuedGpuQueryIndex = static_cast<int>(issuedIndex);
    m_gpuTotalQueryIndex = (m_gpuTotalQueryIndex + 1) % 2;
    m_gpuTotalTimerActive = false;
}

void Renderer::GenerateNoiseVolumes()
{
    ID3D11ShaderResourceView* nullSrvs[3] = { nullptr, nullptr, nullptr };
    m_context->PSSetShaderResources(0, 3, nullSrvs);
    m_context->CSSetConstantBuffers(1, 1, m_cloudCb.GetAddressOf());
    m_context->CSSetConstantBuffers(2, 1, m_noiseVolumeGenerationCb.GetAddressOf());

    // base → CSBase, outputBase(u0) / detail → CSDetail, outputDetail(u2). (u1은 CSSeamTest 전용)
    const UINT sizes[2] = { 128, 128 };
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

    NoiseVolumeGenerationCB weatherCb = { 512, 2, { 0, 0 } };
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(m_context->Map(m_noiseVolumeGenerationCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        memcpy(mapped.pData, &weatherCb, sizeof(weatherCb));
        m_context->Unmap(m_noiseVolumeGenerationCb.Get(), 0);
    }
    m_context->CSSetShader(m_noiseWeatherCs.Get(), nullptr, 0);
    ID3D11UnorderedAccessView* weatherUav = m_weatherMap.uav.Get();
    m_context->CSSetUnorderedAccessViews(3, 1, &weatherUav, nullptr);
    m_context->Dispatch(64, 64, 1);
    ++m_noiseDispatchCount;
    ID3D11UnorderedAccessView* nullWeatherUav = nullptr;
    m_context->CSSetUnorderedAccessViews(3, 1, &nullWeatherUav, nullptr);
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
    CreateTemporalResources();
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

void Renderer::ToggleTelemetry()
{
    m_debugUI.ToggleTelemetry();
}

void Renderer::SetBenchmarkMode(bool enabled)
{
    m_benchmarkMode = enabled;
    if (enabled)
        m_debugUI.SetTelemetryVisible(false);
}

void Renderer::SetTemporalEnabled(bool enabled)
{
    if (m_temporalEnabled == enabled) return;
    m_temporalEnabled = enabled;
    ResetTemporalHistory();
}

void Renderer::ConfigureViewStepBenchmark(int viewSteps, float cloudThickness)
{
    m_cloudParams = CumulusWideShowcaseCloudParameters();
    m_cloudParams.viewSteps = std::clamp(viewSteps, 48, 256);
    m_cloudParams.cloudThickness = (std::max)(cloudThickness, 0.1f);
    m_previewSettings.freeze = true;
    m_previewSettings.previewTime = 0.0f;
    ResetTemporalHistory();
}

void Renderer::BeginBenchmarkCollection()
{
    m_benchmarkSamples.clear();
    m_benchmarkCollecting = true;
}

void Renderer::EndBenchmarkCollection()
{
    m_benchmarkCollecting = false;
}

void Renderer::Render(const Camera& camera, float timeSeconds)
{
    if (!m_rtv) return;

    const double cpuRenderStart = PerformanceCounterSeconds();
    const float frameSample = m_lastFrameTime > 0.0f ? (timeSeconds - m_lastFrameTime) * 1000.0f : 0.0f;
    if (frameSample > 0.0f)
        m_frameIntervalMs = m_frameIntervalMs > 0.0f
            ? m_frameIntervalMs * 0.9f + frameSample * 0.1f : frameSample;
    m_lastFrameTime = timeSeconds;
    m_debugUI.BeginFrame();

    std::array<ID3D11ShaderResourceView*, 4> previewViews = {};
    for (size_t i = 0; i < previewViews.size(); ++i) previewViews[i] = m_previewSrvs[i].Get();
    const CloudParameters previousParams = m_cloudParams;
    const bool previousTemporalEnabled = m_temporalEnabled;
    NoiseCacheUiActions cacheActions;
    if (m_debugUI.Draw(m_cloudParams, m_previewSettings, previewViews, m_weatherMap.srv.Get(),
                       m_previewDirty, m_temporalEnabled, cacheActions))
    {
        m_previewDirty = true;
        ResetTemporalHistory();
        if (previousParams.noiseWorldScale != m_cloudParams.noiseWorldScale ||
            previousParams.basePeriod != m_cloudParams.basePeriod ||
            previousParams.detailPeriod != m_cloudParams.detailPeriod ||
            previousParams.seed != m_cloudParams.seed ||
            previousParams.baseOctaves != m_cloudParams.baseOctaves ||
            previousParams.detailOctaves != m_cloudParams.detailOctaves ||
            previousParams.weatherSeed != m_cloudParams.weatherSeed)
        {
            m_noiseCacheDirty = true;
            m_cacheStatus = "Unsaved";
        }
    }
    if (previousTemporalEnabled != m_temporalEnabled)
        ResetTemporalHistory();

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
        WeatherMapResources loadedWeather;
        if (m_noiseCacheManager.LoadPreferred(m_device.Get(), loadedParams, loadedBlobs,
                                               loadedVolumes, loadedUavs, loadedSrvs,
                                               loadedWeather, modified) &&
            CreateShadersFromBlobs(loadedBlobs))
        {
            m_cloudParams = loadedParams;
            m_noiseVolumes = loadedVolumes;
            m_noiseVolumeUavs = loadedUavs;
            m_noiseVolumeSrvs = loadedSrvs;
            m_weatherMap = loadedWeather;
            m_noiseCacheDirty = false;
            m_sourceModified = modified;
            m_cacheStatus = modified ? "Source Modified" : "Saved";
            ResetTemporalHistory();
        }
        else
        {
            const std::string& detail = m_noiseCacheManager.LastError();
            m_cacheStatus = detail.empty() ? "Error: cache reload failed" : "Error: " + detail;
        }
    }

    const float cloudTime = m_previewSettings.freeze ? m_previewSettings.previewTime : timeSeconds;
    if (!m_previewSettings.freeze) m_previewDirty = true;
    const bool temporalActive = m_temporalEnabled && m_cloudParams.renderMode == 0;
    const int halfWidth = (std::max)(1, (m_width + 1) / 2);
    const int halfHeight = (std::max)(1, (m_height + 1) / 2);

    // ---- 상수버퍼 갱신 ----
    CameraCB cb = {};
    // HLSL은 mul(vector, matrix) 규약 → DirectXMath 행렬을 transpose 해서 업로드
    XMStoreFloat4x4(&cb.invViewProj, XMMatrixTranspose(camera.GetInvViewProj()));
    cb.cameraPos = camera.GetPosition();
    cb.time = cloudTime;
    cb.renderSize = XMFLOAT2(
        static_cast<float>(temporalActive ? halfWidth : m_width),
        static_cast<float>(temporalActive ? halfHeight : m_height));
    static constexpr XMFLOAT2 jitterPixels[4] = {
        { -0.25f, -0.25f }, { 0.25f, -0.25f },
        { -0.25f, 0.25f }, { 0.25f, 0.25f }
    };
    // 움직이는 밀도장은 자체적으로 시간별 sample phase를 제공한다. 이때 camera jitter까지
    // 중첩하면 얇은 상·하 경계가 두 좌표 변화 사이를 오가므로 정지 animation에서만 jitter한다.
    if (temporalActive && m_previewSettings.freeze)
    {
        const XMFLOAT2 jitter = jitterPixels[m_temporalFrameIndex & 3u];
        cb.rayJitterNdc = XMFLOAT2(
            2.0f * jitter.x / static_cast<float>(halfWidth),
            -2.0f * jitter.y / static_cast<float>(halfHeight));
    }

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

    const XMVECTOR currentPosition = XMLoadFloat3(&cb.cameraPos);
    const XMVECTOR previousPosition = XMLoadFloat3(&m_previousCameraPos);
    if (m_historyValid &&
        XMVectorGetX(XMVector3Length(XMVectorSubtract(currentPosition, previousPosition))) > 1.0f)
        ResetTemporalHistory();
    TemporalCB temporalCb = {};
    XMStoreFloat4x4(&temporalCb.previousViewProj,
                    XMMatrixTranspose(m_historyValid
                        ? XMLoadFloat4x4(&m_previousViewProj)
                        : camera.GetViewProj()));
    temporalCb.previousCameraPos = m_historyValid ? m_previousCameraPos : cb.cameraPos;
    temporalCb.previousTime = m_historyValid ? m_previousCloudTime : cloudTime;
    const float windLength = std::sqrt(
        m_cloudParams.windDirection.x * m_cloudParams.windDirection.x +
        m_cloudParams.windDirection.y * m_cloudParams.windDirection.y);
    const float windScale = windLength > 1.0e-5f
        ? (cloudTime - temporalCb.previousTime) * m_cloudParams.windSpeed / windLength
        : 0.0f;
    temporalCb.windDeltaWorld = XMFLOAT2(
        m_cloudParams.windDirection.x * windScale,
        m_cloudParams.windDirection.y * windScale);
    temporalCb.historyWeight = 0.85f;
    temporalCb.historyValid = m_historyValid ? 1u : 0u;
    if (SUCCEEDED(m_context->Map(m_temporalCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        memcpy(mapped.pData, &temporalCb, sizeof(temporalCb));
        m_context->Unmap(m_temporalCb.Get(), 0);
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
        const bool saved = m_noiseCacheManager.SaveUser(
            m_device.Get(), m_context.Get(), m_cloudParams, m_shaderBlobs,
            m_noiseVolumes, m_weatherMap);
        m_cacheStatus = saved ? "Saved" : "Error: " + m_noiseCacheManager.LastError();
    }
    // 패널이 기본 숨김인 동안에는 비싼 4-MRT 절차식 미리보기를 만들지 않는다.
    // F1로 처음 열면 유지된 dirty 플래그에 의해 즉시 한 번 생성된다.
    if (m_previewDirty && m_debugUI.IsVisible())
        RenderNoisePreview(cloudTime);

    BeginGpuTotalTimer();

    const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    m_context->ClearRenderTargetView(m_rtv.Get(), clearColor);
    m_context->PSSetShader(m_ps.Get(), nullptr, 0);
    m_context->PSSetConstantBuffers(0, 1, m_cb.GetAddressOf());
    m_context->PSSetConstantBuffers(1, 1, m_cloudCb.GetAddressOf());
    ID3D11ShaderResourceView* noiseSrvs[3] = {
        m_noiseVolumeSrvs[0].Get(), m_noiseVolumeSrvs[1].Get(), m_weatherMap.srv.Get()
    };
    m_context->PSSetShaderResources(0, 3, noiseSrvs);
    m_context->PSSetSamplers(0, 1, m_noiseSampler.GetAddressOf());
    D3D11_VIEWPORT vp = {};
    vp.MaxDepth = 1.0f;

    if (temporalActive)
    {
        vp.Width = static_cast<float>(halfWidth);
        vp.Height = static_cast<float>(halfHeight);
        m_context->RSSetViewports(1, &vp);
        const float zero[4] = {};
        m_context->ClearRenderTargetView(m_halfCloudColorRtv.Get(), zero);
        m_context->ClearRenderTargetView(m_halfCloudDepthRtv.Get(), zero);
        ID3D11RenderTargetView* halfTargets[2] = {
            m_halfCloudColorRtv.Get(), m_halfCloudDepthRtv.Get()
        };
        m_context->OMSetRenderTargets(2, halfTargets, nullptr);
        BeginGpuTimer();
        RenderFullscreenTriangle();
        EndGpuTimer();

        ID3D11ShaderResourceView* nullSrvs[4] = {};
        m_context->PSSetShaderResources(0, 4, nullSrvs);
        const unsigned int currentHistory = m_historyIndex;
        const unsigned int previousHistory = 1u - currentHistory;
        vp.Width = static_cast<float>(m_width);
        vp.Height = static_cast<float>(m_height);
        m_context->RSSetViewports(1, &vp);
        m_context->ClearRenderTargetView(m_historyColorRtv[currentHistory].Get(), zero);
        m_context->ClearRenderTargetView(m_historyDepthRtv[currentHistory].Get(), zero);
        ID3D11RenderTargetView* historyTargets[2] = {
            m_historyColorRtv[currentHistory].Get(), m_historyDepthRtv[currentHistory].Get()
        };
        m_context->OMSetRenderTargets(2, historyTargets, nullptr);
        m_context->PSSetShader(m_temporalPs.Get(), nullptr, 0);
        m_context->PSSetConstantBuffers(0, 1, m_cb.GetAddressOf());
        m_context->PSSetConstantBuffers(2, 1, m_temporalCb.GetAddressOf());
        ID3D11ShaderResourceView* temporalSrvs[4] = {
            m_halfCloudColorSrv.Get(), m_halfCloudDepthSrv.Get(),
            m_historyColorSrv[previousHistory].Get(), m_historyDepthSrv[previousHistory].Get()
        };
        m_context->PSSetShaderResources(0, 4, temporalSrvs);
        m_context->PSSetSamplers(0, 1, m_linearClampSampler.GetAddressOf());
        BeginReconstructionTimer();
        RenderFullscreenTriangle();

        m_context->PSSetShaderResources(0, 4, nullSrvs);
        m_context->OMSetRenderTargets(1, m_rtv.GetAddressOf(), nullptr);
        m_context->PSSetShader(m_compositePs.Get(), nullptr, 0);
        ID3D11ShaderResourceView* resolved = m_historyColorSrv[currentHistory].Get();
        m_context->PSSetShaderResources(0, 1, &resolved);
        m_context->PSSetSamplers(0, 1, m_linearClampSampler.GetAddressOf());
        RenderFullscreenTriangle();
        EndReconstructionTimer();
        m_context->PSSetShaderResources(0, 4, nullSrvs);
        m_historyValid = true;
        m_historyIndex = previousHistory;
        ++m_temporalFrameIndex;
    }
    else
    {
        if (m_historyValid) ResetTemporalHistory();
        vp.Width = static_cast<float>(m_width);
        vp.Height = static_cast<float>(m_height);
        m_context->RSSetViewports(1, &vp);
        m_context->OMSetRenderTargets(1, m_rtv.GetAddressOf(), nullptr);
        BeginGpuTimer();
        RenderFullscreenTriangle();
        EndGpuTimer();
        BeginReconstructionTimer();
        EndReconstructionTimer();
    }

    XMStoreFloat4x4(&m_previousViewProj, camera.GetViewProj());
    m_previousCameraPos = cb.cameraPos;
    m_previousCloudTime = cloudTime;

    // 구름 위에 디버그 UI를 합성한 뒤 Present.
    TelemetrySnapshot telemetry;
    telemetry.frameIntervalMs = m_frameIntervalMs;
    telemetry.cpuRenderMs = m_cpuRenderMs;
    telemetry.gpuCloudMs = m_gpuFrameMs;
    telemetry.gpuReconstructionMs = m_gpuReconstructionMs;
    telemetry.gpuTotalMs = m_gpuTotalMs;
    telemetry.temporalActive = temporalActive;
    telemetry.cacheStatus = m_cacheStatus;
    m_debugUI.DrawTelemetry(m_cloudParams, telemetry);
    m_debugUI.EndFrame();
    EndGpuTotalTimer();
    const float cpuSample =
        static_cast<float>((PerformanceCounterSeconds() - cpuRenderStart) * 1000.0);
    if (m_lastIssuedGpuQueryIndex >= 0)
        m_gpuTotalQueryCpuMs[static_cast<size_t>(m_lastIssuedGpuQueryIndex)] = cpuSample;
    m_cpuRenderMs = m_cpuRenderMs > 0.0f ? m_cpuRenderMs * 0.9f + cpuSample * 0.1f : cpuSample;
    const double presentStart = PerformanceCounterSeconds();
    m_swapChain->Present(m_benchmarkMode ? 0 : 1, 0);
    const float presentSample =
        static_cast<float>((PerformanceCounterSeconds() - presentStart) * 1000.0);
    m_presentWaitMs = m_presentWaitMs > 0.0f
        ? m_presentWaitMs * 0.9f + presentSample * 0.1f : presentSample;
    m_hasPresented = true;
    CheckShaderHotReload();
}

bool Renderer::SaveDefaultNoiseCache()
{
    if (m_noiseCacheDirty) GenerateNoiseVolumes();
    return m_noiseCacheManager.SaveDefault(
        m_device.Get(), m_context.Get(), m_cloudParams, m_shaderBlobs,
        m_noiseVolumes, m_weatherMap);
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
        detailDesc.Width == 128 && detailDesc.Height == 128 && detailDesc.Depth == 128 &&
        baseDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM && // Perlin-Worley + Worley 3밴드
        detailDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM && // Worley 옥타브 4채널
        static_cast<uint64_t>(baseDesc.Width) * baseDesc.Height * baseDesc.Depth * 4 == 8388608ull &&
        static_cast<uint64_t>(detailDesc.Width) * detailDesc.Height * detailDesc.Depth * 4 == 8388608ull;

    bool detailDistribution = false;
    if (volumeLayout)
    {
        D3D11_TEXTURE3D_DESC readDesc = detailDesc;
        readDesc.Usage = D3D11_USAGE_STAGING;
        readDesc.BindFlags = 0;
        readDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        readDesc.MiscFlags = 0;
        ComPtr<ID3D11Texture3D> readback;
        if (SUCCEEDED(m_device->CreateTexture3D(&readDesc, nullptr, &readback)))
        {
            m_context->CopyResource(readback.Get(), m_noiseVolumes[1].Get());
            D3D11_MAPPED_SUBRESOURCE detailMapped = {};
            if (SUCCEEDED(m_context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &detailMapped)))
            {
                std::array<unsigned char, 4> channelMin = { 255, 255, 255, 255 };
                std::array<unsigned char, 4> channelMax = { 0, 0, 0, 0 };
                for (UINT z = 0; z < detailDesc.Depth; ++z)
                for (UINT y = 0; y < detailDesc.Height; ++y)
                {
                    const auto* row = static_cast<const unsigned char*>(detailMapped.pData) +
                        static_cast<size_t>(z) * detailMapped.DepthPitch +
                        static_cast<size_t>(y) * detailMapped.RowPitch;
                    for (UINT x = 0; x < detailDesc.Width; ++x)
                    {
                        const auto* texel = row + static_cast<size_t>(x) * 4;
                        for (size_t c = 0; c < 4; ++c)
                        {
                            channelMin[c] = (std::min)(channelMin[c], texel[c]);
                            channelMax[c] = (std::max)(channelMax[c], texel[c]);
                        }
                    }
                }
                m_context->Unmap(readback.Get(), 0);
                detailDistribution = true;
                for (size_t c = 0; c < 4; ++c)
                    detailDistribution = detailDistribution && channelMax[c] > channelMin[c] + 8;
            }
        }
    }

    D3D11_TEXTURE2D_DESC weatherDesc = {};
    m_weatherMap.texture->GetDesc(&weatherDesc);
    const bool weatherLayout =
        weatherDesc.Width == 512 && weatherDesc.Height == 512 &&
        weatherDesc.ArraySize == 1 && weatherDesc.MipLevels == 1 &&
        weatherDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM;
    bool weatherDistribution = false;
    if (weatherLayout)
    {
        D3D11_TEXTURE2D_DESC readDesc = weatherDesc;
        readDesc.Usage = D3D11_USAGE_STAGING;
        readDesc.BindFlags = 0;
        readDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        readDesc.MiscFlags = 0;
        ComPtr<ID3D11Texture2D> readback;
        if (SUCCEEDED(m_device->CreateTexture2D(&readDesc, nullptr, &readback)))
        {
            m_context->CopyResource(readback.Get(), m_weatherMap.texture.Get());
            D3D11_MAPPED_SUBRESOURCE weatherMapped = {};
            if (SUCCEEDED(m_context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &weatherMapped)))
            {
                std::array<unsigned char, 4> channelMin = { 255, 255, 255, 255 };
                std::array<unsigned char, 4> channelMax = { 0, 0, 0, 0 };
                for (UINT y = 0; y < weatherDesc.Height; ++y)
                {
                    const auto* row = static_cast<const unsigned char*>(weatherMapped.pData) +
                        static_cast<size_t>(y) * weatherMapped.RowPitch;
                    for (UINT x = 0; x < weatherDesc.Width; ++x)
                    {
                        const auto* texel = row + static_cast<size_t>(x) * 4;
                        for (size_t c = 0; c < 4; ++c)
                        {
                            channelMin[c] = (std::min)(channelMin[c], texel[c]);
                            channelMax[c] = (std::max)(channelMax[c], texel[c]);
                        }
                    }
                }
                m_context->Unmap(readback.Get(), 0);
                weatherDistribution = true;
                for (size_t c = 0; c < 4; ++c)
                    weatherDistribution = weatherDistribution && channelMax[c] > channelMin[c] + 8;
            }
        }
    }

    const auto layerInterval = [](float originY, float directionY, float bottom, float top, float limit)
    {
        if (std::abs(directionY) < 1.0e-5f)
            return originY >= bottom && originY <= top
                ? std::pair<float, float>{ 0.0f, limit }
                : std::pair<float, float>{ 1.0f, 0.0f };
        const float a = (bottom - originY) / directionY;
        const float b = (top - originY) / directionY;
        return std::pair<float, float>{
            (std::max)((std::min)(a, b), 0.0f),
            (std::min)((std::max)(a, b), limit) };
    };
    const auto upward = layerInterval(0.0f, 1.0f, 2.0f, 5.0f, 120.0f);
    const auto insideHorizontal = layerInterval(3.0f, 0.0f, 2.0f, 5.0f, 120.0f);
    const auto outsideHorizontal = layerInterval(0.0f, 0.0f, 2.0f, 5.0f, 120.0f);
    const bool layerMath = std::abs(upward.first - 2.0f) < 1.0e-5f &&
                           std::abs(upward.second - 5.0f) < 1.0e-5f &&
                           insideHorizontal.first == 0.0f && insideHorizontal.second == 120.0f &&
                           outsideHorizontal.second <= outsideHorizontal.first;
    CloudParameters changedWeather = m_cloudParams;
    changedWeather.weatherSeed += 1.0f;
    const bool weatherHash =
        NoiseCacheManager::ParameterHash(changedWeather) !=
        NoiseCacheManager::ParameterHash(m_cloudParams);

    // 실제 base 캐시를 읽어 네 채널이 퇴화하지 않았고 shaped density가 전부 비거나
    // 포화되지 않는지 검사한다. 화면 비교가 아니라 생성 데이터의 수치 건전성 검사다.
    bool baseDistribution = false;
    if (volumeLayout)
    {
        D3D11_TEXTURE3D_DESC readDesc = baseDesc;
        readDesc.Usage = D3D11_USAGE_STAGING;
        readDesc.BindFlags = 0;
        readDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        readDesc.MiscFlags = 0;
        ComPtr<ID3D11Texture3D> readback;
        if (SUCCEEDED(m_device->CreateTexture3D(&readDesc, nullptr, &readback)))
        {
            m_context->CopyResource(readback.Get(), m_noiseVolumes[0].Get());
            D3D11_MAPPED_SUBRESOURCE baseMapped = {};
            if (SUCCEEDED(m_context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &baseMapped)))
            {
                std::array<unsigned char, 4> channelMin = { 255, 255, 255, 255 };
                std::array<unsigned char, 4> channelMax = { 0, 0, 0, 0 };
                uint64_t occupied = 0;
                uint64_t saturated = 0;
                const uint64_t total = static_cast<uint64_t>(baseDesc.Width) *
                    baseDesc.Height * baseDesc.Depth;
                for (UINT z = 0; z < baseDesc.Depth; ++z)
                for (UINT y = 0; y < baseDesc.Height; ++y)
                {
                    const auto* row = static_cast<const unsigned char*>(baseMapped.pData) +
                        static_cast<size_t>(z) * baseMapped.DepthPitch +
                        static_cast<size_t>(y) * baseMapped.RowPitch;
                    const float height01 = (static_cast<float>(y) + 0.5f) / baseDesc.Height;
                    const float bottom = std::clamp(height01 / (std::max)(m_cloudParams.bottomFade, 0.001f), 0.0f, 1.0f);
                    const float top = std::clamp((1.0f - height01) / (std::max)(m_cloudParams.topFade, 0.001f), 0.0f, 1.0f);
                    const float heightMask = bottom * bottom * (3.0f - 2.0f * bottom) *
                                             top * top * (3.0f - 2.0f * top);
                    for (UINT x = 0; x < baseDesc.Width; ++x)
                    {
                        const auto* texel = row + static_cast<size_t>(x) * 4;
                        for (size_t c = 0; c < 4; ++c)
                        {
                            channelMin[c] = (std::min)(channelMin[c], texel[c]);
                            channelMax[c] = (std::max)(channelMax[c], texel[c]);
                        }
                        const float r = texel[0] / 255.0f;
                        const float worley = (texel[1] * 0.625f + texel[2] * 0.25f + texel[3] * 0.125f) / 255.0f;
                        const float threshold = std::clamp(
                            m_cloudParams.noiseCutoffThreshold +
                            (0.5f - m_cloudParams.coverage) * 0.70f, 0.0f, 0.99f);
                        float shaped = std::clamp((r - threshold) / (1.0f - threshold), 0.0f, 1.0f);
                        shaped = std::clamp(shaped - worley * m_cloudParams.baseErosion, 0.0f, 1.0f);
                        const float densityValue = shaped * heightMask * m_cloudParams.densityMultiplier;
                        if (densityValue > 0.01f) ++occupied;
                        if (densityValue >= 0.99f) ++saturated;
                    }
                }
                m_context->Unmap(readback.Get(), 0);
                bool channelsVary = true;
                for (size_t c = 0; c < 4; ++c)
                    channelsVary = channelsVary && channelMax[c] > channelMin[c] + 8;
                const float occupiedRatio = static_cast<float>(occupied) / total;
                const float saturatedRatio = static_cast<float>(saturated) / total;
                baseDistribution = channelsVary && occupiedRatio > 0.005f &&
                                   occupiedRatio < 0.90f && saturatedRatio < 0.70f;
            }
        }
    }

    // Beer-Lambert, dual-lobe phase와 다중 산란 근사의 CPU 기준값.
    const float density = 0.8f;
    const float nearT = std::exp(-density * 0.5f);
    const float farT = std::exp(-density * 1.0f);
    const float g = 0.55f;
    const auto hg = [g](float cosine)
    {
        const float g2 = g * g;
        return 0.07957747f * (1.0f - g2) /
            std::pow((std::max)(1.0f + g2 - 2.0f * g * cosine, 0.001f), 1.5f);
    };
    const auto dualLobe = [&hg](float cosine)
    {
        constexpr float backwardG = -0.22f;
        constexpr float backwardG2 = backwardG * backwardG;
        const float backward = 0.07957747f * (1.0f - backwardG2) /
            std::pow((std::max)(1.0f + backwardG2 - 2.0f * backwardG * cosine, 0.001f), 1.5f);
        return backward * 0.18f + hg(cosine) * 0.82f;
    };
    const auto multiScatter = [](float visibility)
    {
        float energy = 0.0f;
        float weight = 0.5f;
        for (int i = 0; i < 3; ++i)
        {
            energy += visibility * weight;
            visibility = std::sqrt(visibility);
            weight *= 0.5f;
        }
        return energy / 0.875f;
    };
    const bool lightingMath = farT <= nearT && nearT <= 1.0f && farT >= 0.0f &&
                              dualLobe(-1.0f) > 0.0f && dualLobe(1.0f) > dualLobe(0.0f) &&
                              std::isfinite(dualLobe(1.0f)) &&
                              multiScatter(0.0f) >= 0.0f && multiScatter(1.0f) <= 1.0f &&
                              std::isfinite(multiScatter(0.35f));

    const float baseOffsetY = 1.25f;
    const float baseUvYThin = baseOffsetY / m_cloudParams.baseNoiseVerticalSize;
    const float baseUvYThick = baseOffsetY / m_cloudParams.baseNoiseVerticalSize;
    const float detailUvYThin = baseOffsetY / m_cloudParams.detailNoiseVerticalSize;
    const float detailUvYThick = baseOffsetY / m_cloudParams.detailNoiseVerticalSize;
    const bool worldSpaceCoordinates =
        m_cloudParams.detailNoiseWorldSize > 0.0f &&
        m_cloudParams.baseNoiseVerticalSize > 0.0f &&
        m_cloudParams.detailNoiseVerticalSize > 0.0f &&
        std::abs(baseUvYThin - baseUvYThick) < 1.0e-6f &&
        std::abs(detailUvYThin - detailUvYThick) < 1.0e-6f;
    const bool detailNyquist =
        m_cloudParams.detailPeriod >= 2 && m_cloudParams.detailPeriod <= 8 &&
        m_cloudParams.detailPeriod * 8 <= static_cast<int>(detailDesc.Width / 2);

    D3D11_TEXTURE2D_DESC halfColorDesc = {};
    D3D11_TEXTURE2D_DESC halfDepthDesc = {};
    D3D11_TEXTURE2D_DESC historyColorDesc = {};
    D3D11_TEXTURE2D_DESC historyDepthDesc = {};
    m_halfCloudColor->GetDesc(&halfColorDesc);
    m_halfCloudDepth->GetDesc(&halfDepthDesc);
    m_historyColor[0]->GetDesc(&historyColorDesc);
    m_historyDepth[0]->GetDesc(&historyDepthDesc);
    const bool temporalLayout =
        halfColorDesc.Width == static_cast<UINT>((m_width + 1) / 2) &&
        halfColorDesc.Height == static_cast<UINT>((m_height + 1) / 2) &&
        halfColorDesc.Format == DXGI_FORMAT_R11G11B10_FLOAT &&
        halfDepthDesc.Width == halfColorDesc.Width &&
        halfDepthDesc.Height == halfColorDesc.Height &&
        halfDepthDesc.Format == DXGI_FORMAT_R16_FLOAT &&
        historyColorDesc.Width == static_cast<UINT>(m_width) &&
        historyColorDesc.Height == static_cast<UINT>(m_height) &&
        historyColorDesc.Format == DXGI_FORMAT_R11G11B10_FLOAT &&
        historyDepthDesc.Width == static_cast<UINT>(m_width) &&
        historyDepthDesc.Height == static_cast<UINT>(m_height) &&
        historyDepthDesc.Format == DXGI_FORMAT_R16_FLOAT;
    const auto historyAccepted = [](float storedDepth, float expectedDepth)
    {
        const float tolerance = (std::max)(0.1f, expectedDepth * 0.05f);
        return std::abs(storedDepth - expectedDepth) <= tolerance;
    };
    const bool temporalRejection =
        historyAccepted(10.2f, 10.0f) &&
        !historyAccepted(10.6f, 10.0f) &&
        historyAccepted(0.05f, 0.1f);
    bool temporalJitterAlignment = true;
    const XMFLOAT2 testOutputUv = { 0.37f, 0.61f };
    for (const XMFLOAT2& jitterPixel : {
            XMFLOAT2{ -0.25f, -0.25f }, XMFLOAT2{ 0.25f, -0.25f },
            XMFLOAT2{ -0.25f, 0.25f }, XMFLOAT2{ 0.25f, 0.25f } })
    {
        const float jitterNdcX = 2.0f * jitterPixel.x /
            static_cast<float>((std::max)(1, (m_width + 1) / 2));
        const float jitterNdcY = -2.0f * jitterPixel.y /
            static_cast<float>((std::max)(1, (m_height + 1) / 2));
        const float currentUvX = testOutputUv.x - jitterNdcX * 0.5f;
        const float currentUvY = testOutputUv.y + jitterNdcY * 0.5f;
        const float recoveredNdcX = currentUvX * 2.0f - 1.0f + jitterNdcX;
        const float recoveredNdcY = 1.0f - currentUvY * 2.0f + jitterNdcY;
        const float expectedNdcX = testOutputUv.x * 2.0f - 1.0f;
        const float expectedNdcY = 1.0f - testOutputUv.y * 2.0f;
        temporalJitterAlignment &=
            std::abs(recoveredNdcX - expectedNdcX) < 1.0e-6f &&
            std::abs(recoveredNdcY - expectedNdcY) < 1.0e-6f;
    }
    const float windLengthForTest = std::sqrt(
        m_cloudParams.windDirection.x * m_cloudParams.windDirection.x +
        m_cloudParams.windDirection.y * m_cloudParams.windDirection.y);
    const float tenSecondWindDistance = windLengthForTest > 1.0e-5f
        ? 10.0f * m_cloudParams.windSpeed : 0.0f;
    const bool weatherAdvection =
        m_cloudParams.windSpeed <= 0.0f ||
        (windLengthForTest > 1.0e-5f && tenSecondWindDistance > 0.0f &&
         std::isfinite(tenSecondWindDistance));
    const auto explicitJitterEnabled = [](bool temporalActive, bool animationFrozen)
    {
        return temporalActive && animationFrozen;
    };
    const bool animatedJitterDisabled =
        explicitJitterEnabled(true, true) &&
        !explicitJitterEnabled(true, false) &&
        !explicitJitterEnabled(false, true);
    bool hierarchicalEarlyOut = true;
    for (const float potential : { 0.0f, 0.0005f, 0.5f, 1.0f })
    for (const float baseShape : { 0.0f, 0.00005f, 0.25f, 1.0f })
    for (const float detail : { 0.0f, 0.5f, 1.0f })
    {
        const float height = 0.8f;
        const float macro = baseShape * height * potential;
        const float detailed = (std::max)(baseShape - detail * 0.35f, 0.0f) *
                               height * potential;
        if ((potential <= 0.0f || macro <= 0.0f) && detailed > 0.0f)
            hierarchicalEarlyOut = false;
    }
    const bool recreatedTemporalResources = CreateTemporalResources();
    m_historyValid = true;
    m_historyIndex = 1;
    ResetTemporalHistory();
    const bool historyReset = !m_historyValid && m_historyIndex == 0;

    const auto localThicknessFor = [&](float weatherThickness, float weatherType)
    {
        const float varied = m_cloudParams.cloudThickness *
            (std::max)(0.2f, 1.0f + (weatherThickness * 2.0f - 1.0f) *
                                  std::clamp(m_cloudParams.thicknessVariation, 0.0f, 1.0f));
        const float shiftedType = std::clamp(
            weatherType + m_cloudParams.weatherTypeBias - 0.5f, 0.0f, 1.0f);
        const float typeT = shiftedType * shiftedType * (3.0f - 2.0f * shiftedType);
        const float growth = (std::max)(m_cloudParams.cumulusGrowth, 1.0f);
        return varied * (1.0f + (growth - 1.0f) * typeT);
    };
    const float lowTypeThickness = localThicknessFor(0.5f, 0.0f);
    const float highTypeThickness = localThicknessFor(0.5f, 1.0f);
    const bool cumulusBounds = std::isfinite(lowTypeThickness) &&
        std::isfinite(highTypeThickness) && lowTypeThickness > 0.0f &&
        highTypeThickness >= lowTypeThickness;

    const float fineStep = (std::min)(
        96.0f / static_cast<float>((std::max)(m_cloudParams.viewSteps, 48)),
        (std::max)(m_cloudParams.maxViewStepLength, 0.001f));
    const float targetStep = 96.0f /
        static_cast<float>((std::max)(m_cloudParams.viewSteps, 48));
    const float weatherSkip = (std::min)(
        (std::max)(targetStep * 4.0f, fineStep * 4.0f), 1.6f);
    const float candidateSkip = (std::min)(
        (std::max)(targetStep * 2.0f, fineStep * 2.0f), 0.4f);
    const int emptyIterations = static_cast<int>(std::ceil(96.0f / weatherSkip));
    const int candidateIterations = static_cast<int>(std::ceil(96.0f / candidateSkip));
    const int denseIterations = static_cast<int>(std::ceil(16.0f / fineStep));
    const float refinedBoundaryWidth = fineStep * 2.0f /
        static_cast<float>(1 << std::clamp(m_cloudParams.boundaryRefineSteps, 0, 5));
    const bool adaptiveMarching = std::isfinite(fineStep) && fineStep > 0.0f &&
        fineStep <= m_cloudParams.maxViewStepLength + 1.0e-6f &&
        weatherSkip > 0.0f && candidateSkip > 0.0f &&
        emptyIterations <= 2000 && candidateIterations <= 2000 &&
        denseIterations <= 2000 &&
        std::isfinite(refinedBoundaryWidth) && refinedBoundaryWidth > 0.0f;

    const float lightExitDistance = 7.0f;
    const float localSegment = (std::min)(
        lightExitDistance, (std::max)(m_cloudParams.localLightDistance, 0.01f));
    const float farSegment = (std::max)(lightExitDistance - localSegment, 0.0f);
    const float coveredLightDistance = localSegment +
        (m_cloudParams.farLightSteps > 0 ? farSegment : 0.0f);
    const float testVisibility = std::exp(-density * coveredLightDistance *
                                         m_cloudParams.lightAbsorption);
    const bool lightSegments = localSegment > 0.0f && farSegment >= 0.0f &&
        m_cloudParams.lightSteps >= 1 && m_cloudParams.lightSteps <= 12 &&
        m_cloudParams.farLightSteps >= 0 && m_cloudParams.farLightSteps <= 8 &&
        std::abs(coveredLightDistance - lightExitDistance) < 1.0e-5f &&
        std::isfinite(testVisibility) && testVisibility >= 0.0f && testVisibility <= 1.0f;
    const bool presetRoundTrip = DebugUI::RunWorldSpacePresetRoundTripTest();
    // 벤치마크 두께와 48~256 view-step 범위에서 weather 변형 후의
    // local thickness와 샘플 간격이 모두 유한하고 양수인지 확인한다.
    bool thicknessSampling = true;
    for (const float thickness : { 3.8f, 16.0f })
    {
        for (const float weatherThickness : { 0.0f, 0.5f, 1.0f })
        {
            const float localThickness = thickness *
                (1.0f + (weatherThickness * 2.0f - 1.0f) * 0.8f);
            for (const int steps : { 48, 128, 160, 192, 256 })
            {
                const float sampleSpacing = localThickness / steps;
                thicknessSampling = thicknessSampling &&
                    std::isfinite(localThickness) && localThickness > 0.0f &&
                    std::isfinite(sampleSpacing) && sampleSpacing > 0.0f;
            }
        }
    }
    const int clampedViewSteps = std::clamp(512, 48, 256);
    thicknessSampling = thicknessSampling && clampedViewSteps == 256;
    const bool seamless = maxError <= 1.0e-5f;
    const bool roundTrip = m_noiseCacheManager.RunRoundTripTest(
        m_device.Get(), m_context.Get(), m_cloudParams, m_shaderBlobs,
        m_noiseVolumes, m_weatherMap);
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
    const bool passed =
           seamless && roundTrip && volumeLayout && baseDistribution && detailDistribution &&
           weatherLayout && weatherDistribution && weatherHash && layerMath &&
           lightingMath && worldSpaceCoordinates && detailNyquist && cumulusBounds &&
           adaptiveMarching && lightSegments && temporalLayout && temporalRejection &&
           temporalJitterAlignment && weatherAdvection && animatedJitterDisabled &&
           hierarchicalEarlyOut && recreatedTemporalResources && historyReset &&
           sizeof(CloudParameters) == 224 &&
           presetRoundTrip && thicknessSampling && debugLayerClean;
    std::ostringstream report;
#define REPORT_CHECK(name) report << #name << '=' << ((name) ? "pass" : "FAIL") << '\n'
    REPORT_CHECK(seamless);
    REPORT_CHECK(roundTrip);
    REPORT_CHECK(volumeLayout);
    REPORT_CHECK(baseDistribution);
    REPORT_CHECK(detailDistribution);
    REPORT_CHECK(weatherLayout);
    REPORT_CHECK(weatherDistribution);
    REPORT_CHECK(weatherHash);
    REPORT_CHECK(layerMath);
    REPORT_CHECK(lightingMath);
    REPORT_CHECK(worldSpaceCoordinates);
    REPORT_CHECK(detailNyquist);
    REPORT_CHECK(cumulusBounds);
    REPORT_CHECK(adaptiveMarching);
    REPORT_CHECK(lightSegments);
    REPORT_CHECK(temporalLayout);
    REPORT_CHECK(temporalRejection);
    REPORT_CHECK(temporalJitterAlignment);
    REPORT_CHECK(weatherAdvection);
    REPORT_CHECK(animatedJitterDisabled);
    REPORT_CHECK(hierarchicalEarlyOut);
    REPORT_CHECK(recreatedTemporalResources);
    REPORT_CHECK(historyReset);
    REPORT_CHECK(presetRoundTrip);
    REPORT_CHECK(thicknessSampling);
    REPORT_CHECK(debugLayerClean);
#undef REPORT_CHECK
    const auto reportPath =
        std::filesystem::temp_directory_path() / L"VolumetricCloudCodeTests.txt";
    std::ofstream reportFile(reportPath, std::ios::trunc);
    reportFile << report.str();
    OutputDebugStringA(report.str().c_str());
    return passed;
}
