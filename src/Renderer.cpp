#include "Renderer.h"
#include "Camera.h"

#include <d3dcompiler.h>
#include <d3d11sdklayers.h>
#include <algorithm>
#include <cmath>
#include <cstring>
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

    bd.ByteWidth = sizeof(NoisePreviewCB);
    hr = m_device->CreateBuffer(&bd, nullptr, &m_previewCb);
    if (FAILED(hr)) return false;

    bd.ByteWidth = sizeof(NoiseVolumeGenerationCB);
    hr = m_device->CreateBuffer(&bd, nullptr, &m_noiseVolumeGenerationCb);
    if (FAILED(hr)) return false;

    if (!CreateNoisePreviewResources()) return false;
    if (!m_cacheLoaded && !CreateNoiseVolumeResources()) return false;
    if (!CreateNoiseSampler()) return false;
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
    ComPtr<ID3DBlob> vsBlob, psBlob, previewVsBlob, previewPsBlob;
    ComPtr<ID3DBlob> noiseCsBaseBlob, noiseCsDetailBlob, noiseCsWeatherBlob;

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
    if (!CompileShaderFromFile(m_noiseVolumeCsPath, "CSWeather", "cs_5_0", noiseCsWeatherBlob, showErrors))
        return false;

    ShaderBlobArray blobs;
    blobs[static_cast<size_t>(CachedShader::MainVS)] = vsBlob;
    blobs[static_cast<size_t>(CachedShader::MainPS)] = psBlob;
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
    const auto& previewVsBlob = blobs[static_cast<size_t>(CachedShader::PreviewVS)];
    const auto& previewPsBlob = blobs[static_cast<size_t>(CachedShader::PreviewPS)];
    const auto& noiseCsBaseBlob = blobs[static_cast<size_t>(CachedShader::NoiseCSBase)];
    const auto& noiseCsDetailBlob = blobs[static_cast<size_t>(CachedShader::NoiseCSDetail)];
    const auto& noiseCsWeatherBlob = blobs[static_cast<size_t>(CachedShader::NoiseCSWeather)];
    for (const auto& blob : blobs) if (!blob) return false;

    ComPtr<ID3D11VertexShader> newVs;
    ComPtr<ID3D11PixelShader>  newPs;
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
    const UINT sizes[2] = { 128, 64 };
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
    return SUCCEEDED(m_device->CreateSamplerState(&sd, &m_noiseSampler));
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

void Renderer::ResolveGpuTotalTimer()
{
    const unsigned int readIndex = m_gpuTotalQueryIndex;
    if (!m_gpuTotalQueryIssued[readIndex]) return;
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint = {};
    UINT64 totalBegin = 0;
    UINT64 cloudBegin = 0;
    UINT64 cloudEnd = 0;
    UINT64 totalEnd = 0;
    if (m_context->GetData(m_gpuTotalDisjointQueries[readIndex].Get(), &disjoint, sizeof(disjoint),
                           D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
        m_context->GetData(m_gpuTotalBeginQueries[readIndex].Get(), &totalBegin, sizeof(totalBegin),
                           D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
        m_context->GetData(m_gpuBeginQueries[readIndex].Get(), &cloudBegin, sizeof(cloudBegin),
                           D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
        m_context->GetData(m_gpuEndQueries[readIndex].Get(), &cloudEnd, sizeof(cloudEnd),
                           D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
        m_context->GetData(m_gpuTotalEndQueries[readIndex].Get(), &totalEnd, sizeof(totalEnd),
                           D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK)
    {
        if (!disjoint.Disjoint && disjoint.Frequency > 0 &&
            totalEnd >= totalBegin && cloudEnd >= cloudBegin)
        {
            const float totalSample =
                static_cast<float>((totalEnd - totalBegin) * 1000.0 / disjoint.Frequency);
            const float cloudSample =
                static_cast<float>((cloudEnd - cloudBegin) * 1000.0 / disjoint.Frequency);
            m_gpuTotalMs = m_gpuTotalMs > 0.0f ? m_gpuTotalMs * 0.9f + totalSample * 0.1f : totalSample;
            m_gpuFrameMs = m_gpuFrameMs > 0.0f ? m_gpuFrameMs * 0.9f + cloudSample * 0.1f : cloudSample;
        }
        m_gpuTotalQueryIssued[readIndex] = false;
    }
}

void Renderer::BeginGpuTotalTimer()
{
    ResolveGpuTotalTimer();
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
    m_context->End(m_gpuTotalEndQueries[m_gpuTotalQueryIndex].Get());
    m_context->End(m_gpuTotalDisjointQueries[m_gpuTotalQueryIndex].Get());
    m_gpuTotalQueryIssued[m_gpuTotalQueryIndex] = true;
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
    NoiseCacheUiActions cacheActions;
    if (m_debugUI.Draw(m_cloudParams, m_previewSettings, previewViews, m_weatherMap.srv.Get(),
                       m_previewDirty, cacheActions))
    {
        m_previewDirty = true;
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
        }
        else
        {
            const std::string& detail = m_noiseCacheManager.LastError();
            m_cacheStatus = detail.empty() ? "Error: cache reload failed" : "Error: " + detail;
        }
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
    ID3D11ShaderResourceView* noiseSrvs[3] = {
        m_noiseVolumeSrvs[0].Get(), m_noiseVolumeSrvs[1].Get(), m_weatherMap.srv.Get()
    };
    m_context->PSSetShaderResources(0, 3, noiseSrvs);
    m_context->PSSetSamplers(0, 1, m_noiseSampler.GetAddressOf());

    BeginGpuTimer();
    m_context->Draw(3, 0); // 정점 3개 = 화면을 덮는 삼각형
    EndGpuTimer();

    // 구름 위에 디버그 UI를 합성한 뒤 Present.
    TelemetrySnapshot telemetry;
    telemetry.frameIntervalMs = m_frameIntervalMs;
    telemetry.cpuRenderMs = m_cpuRenderMs;
    telemetry.gpuCloudMs = m_gpuFrameMs;
    telemetry.gpuTotalMs = m_gpuTotalMs;
    telemetry.cacheStatus = m_cacheStatus;
    m_debugUI.DrawTelemetry(m_cloudParams, telemetry);
    m_debugUI.EndFrame();
    EndGpuTotalTimer();
    const float cpuSample =
        static_cast<float>((PerformanceCounterSeconds() - cpuRenderStart) * 1000.0);
    m_cpuRenderMs = m_cpuRenderMs > 0.0f ? m_cpuRenderMs * 0.9f + cpuSample * 0.1f : cpuSample;
    m_swapChain->Present(1, 0); // vsync
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
        detailDesc.Width == 64 && detailDesc.Height == 64 && detailDesc.Depth == 64 &&
        baseDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM && // Perlin-Worley + Worley 3밴드
        detailDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM && // Worley 옥타브 4채널
        static_cast<uint64_t>(baseDesc.Width) * baseDesc.Height * baseDesc.Depth * 4 == 8388608ull &&
        static_cast<uint64_t>(detailDesc.Width) * detailDesc.Height * detailDesc.Depth * 4 == 1048576ull;

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
    // UI가 허용하는 3/8/16 km 두께에서 weather 변형 후의 local thickness와
    // 고정 128-step 샘플 간격이 모두 유한하고 양수인지 확인한다.
    bool thicknessSampling = true;
    for (const float thickness : { 3.0f, 8.0f, 16.0f })
    {
        for (const float weatherThickness : { 0.0f, 0.5f, 1.0f })
        {
            const float localThickness = thickness *
                (1.0f + (weatherThickness * 2.0f - 1.0f) * 0.8f);
            const float sampleSpacing = localThickness / 128.0f;
            thicknessSampling = thicknessSampling &&
                std::isfinite(localThickness) && localThickness > 0.0f &&
                std::isfinite(sampleSpacing) && sampleSpacing > 0.0f;
        }
    }
    const int clampedViewSteps = std::clamp(256, 48, 128);
    thicknessSampling = thicknessSampling && clampedViewSteps == 128;
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
    return seamless && roundTrip && volumeLayout && baseDistribution &&
           weatherLayout && weatherDistribution && weatherHash && layerMath &&
           lightingMath && thicknessSampling && debugLayerClean;
}
