#include "Renderer.h"
#include "Camera.h"
#include "Stage13CameraPresets.h"
#include "Stage13SceneMath.h"
#include "Stage11TemporalMath.h"

#include <d3dcompiler.h>
#include <SetupAPI.h>
#include <devguid.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace
{
std::string NarrowWide(const wchar_t* value)
{
    if (!value || value[0] == L'\0')
        return {};
    const int length = WideCharToMultiByte(
        CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1)
        return {};
    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), length,
                        nullptr, nullptr);
    result.pop_back();
    return result;
}

std::string QueryDisplayDriverVersion(const wchar_t* adapterDescription)
{
    HDEVINFO devices = SetupDiGetClassDevsW(
        &GUID_DEVCLASS_DISPLAY, nullptr, nullptr, DIGCF_PRESENT);
    if (devices == INVALID_HANDLE_VALUE)
        return {};
    std::string version;
    for (DWORD index = 0; version.empty(); ++index)
    {
        SP_DEVINFO_DATA device = {};
        device.cbSize = sizeof(device);
        if (!SetupDiEnumDeviceInfo(devices, index, &device))
            break;
        wchar_t description[256] = {};
        DWORD type = 0;
        if (!SetupDiGetDeviceRegistryPropertyW(
                devices, &device, SPDRP_DEVICEDESC, &type,
                reinterpret_cast<PBYTE>(description), sizeof(description),
                nullptr) || _wcsicmp(description, adapterDescription) != 0)
            continue;
        wchar_t driverKey[256] = {};
        if (!SetupDiGetDeviceRegistryPropertyW(
                devices, &device, SPDRP_DRIVER, &type,
                reinterpret_cast<PBYTE>(driverKey), sizeof(driverKey), nullptr))
            continue;
        const std::wstring registryPath =
            L"SYSTEM\\CurrentControlSet\\Control\\Class\\" +
            std::wstring(driverKey);
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, registryPath.c_str(), 0,
                          KEY_READ, &key) != ERROR_SUCCESS)
            continue;
        wchar_t value[128] = {};
        DWORD bytes = sizeof(value);
        if (RegGetValueW(key, nullptr, L"DriverVersion", RRF_RT_REG_SZ,
                         nullptr, value, &bytes) == ERROR_SUCCESS)
            version = NarrowWide(value);
        RegCloseKey(key);
    }
    SetupDiDestroyDeviceInfoList(devices);
    return version;
}

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

void AppendGroundPlane(std::vector<DiagnosticSceneVertex>& vertices,
                       std::vector<std::uint32_t>& indices)
{
    const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
    constexpr float h = stage13scene::kGroundHalfSizeMeters;
    constexpr XMFLOAT3 color = { 0.50f, 0.50f, 0.50f };
    vertices.push_back({ { -h, 0.0f, -h }, color });
    vertices.push_back({ {  h, 0.0f, -h }, color });
    vertices.push_back({ {  h, 0.0f,  h }, color });
    vertices.push_back({ { -h, 0.0f,  h }, color });
    const std::uint32_t planeIndices[] = {
        base + 0u, base + 2u, base + 1u,
        base + 0u, base + 3u, base + 2u,
    };
    indices.insert(indices.end(), std::begin(planeIndices),
                   std::end(planeIndices));
}
}

Renderer::~Renderer()
{
    m_noiseLab.Shutdown();
}

bool Renderer::Init(HWND hwnd, int width, int height, bool enableNoiseVolumes)
{
    m_width = width;
    m_height = height;
    m_noiseVolumesEnabled = enableNoiseVolumes;
    m_shaderDir = ResolveShaderDir();
    m_fullscreenShaderPath = m_shaderDir + L"Fullscreen.hlsl";
    m_cloudShaderPath = m_shaderDir + L"VolumetricClouds.hlsl";
    m_cloudUpsampleShaderPath = m_shaderDir + L"CloudUpsample.hlsl";
    m_cloudTemporalResolveShaderPath =
        m_shaderDir + L"CloudTemporalResolve.hlsl";
    m_noiseLabShaderPath = m_shaderDir + L"NoiseLab.hlsl";
    m_sceneShaderPath = m_shaderDir + L"DiagnosticScene.hlsl";
    m_noiseVolumeShaderPath = m_shaderDir + L"NoiseVolume.hlsl";
    m_deepShadowShaderPath = m_shaderDir + L"CloudDeepShadow.hlsl";
    std::filesystem::path shaderDirectory(m_shaderDir);
    if (shaderDirectory.filename().empty())
        shaderDirectory = shaderDirectory.parent_path();
    m_customAppearancePath = shaderDirectory.parent_path() / L"captures" /
        L"noise-lab" / L"custom" / L"noise-settings.json";
    m_hasSavedCustomAppearance = LoadCustomCloudAppearance(
        m_customAppearancePath, m_savedCustomAppearance,
        m_cloudAppearanceStatus);

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
    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    if (SUCCEEDED(m_device.As(&dxgiDevice)) &&
        SUCCEEDED(dxgiDevice->GetAdapter(&adapter)))
    {
        DXGI_ADAPTER_DESC description = {};
        if (SUCCEEDED(adapter->GetDesc(&description)))
        {
            m_adapterName = NarrowWide(description.Description);
            const std::string registryVersion = QueryDisplayDriverVersion(
                description.Description);
            if (!registryVersion.empty())
                m_driverVersion = registryVersion;
        }
        LARGE_INTEGER version = {};
        if (m_driverVersion == "Unavailable" &&
            SUCCEEDED(adapter->CheckInterfaceSupport(
                __uuidof(ID3D11Device), &version)))
        {
            std::ostringstream text;
            text << HIWORD(version.HighPart) << '.'
                 << LOWORD(version.HighPart) << '.'
                 << HIWORD(version.LowPart) << '.'
                 << LOWORD(version.LowPart);
            m_driverVersion = text.str();
        }
    }

    // GPU timestamp를 만들 수 없는 특수 환경에서도 렌더는 계속하고 오버레이에는
    // GPU timing unavailable을 표시한다. 일반 D3D11 장치에서는 8-slot ring을 쓴다.
    m_frameProfiler.Init(m_device.Get());

    if (!CreateBackBufferTarget() || !CreateSceneTargets() ||
        !CreateCloudTargets() || !CreateTemporalHistoryTargets() ||
        !CreateConstantBuffers() || !CreateShaders(true) ||
        !CreateDiagnosticScene() || !CreatePipelineStates() ||
        !CreateWeatherMapTexture(m_weatherPreset) ||
        !m_noiseLab.Init(hwnd, m_device.Get(), m_context.Get()))
    {
        MessageBoxW(hwnd, L"단계 8 렌더링 리소스 생성 실패", L"오류", MB_OK | MB_ICONERROR);
        return false;
    }

    // 단계 12 cache는 화면 크기와 독립적이다. 실패해도 DirectReference로
    // 렌더를 계속할 수 있으므로 앱 초기화 자체를 중단하지 않는다.
    CreateDeepShadowResources(ShadowPreset());

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

bool Renderer::CreateCloudTargets()
{
    if (!m_device || m_width <= 0 || m_height <= 0)
        return false;
    m_upsamplingParameters = stage10upsampling::Sanitize(
        m_upsamplingParameters);
    m_temporalParameters = stage11temporal::Sanitize(
        m_temporalParameters);
    const int targetWidth = stage10upsampling::ScaledExtent(
        m_width, m_upsamplingParameters.resolutionScale);
    const int targetHeight = stage10upsampling::ScaledExtent(
        m_height, m_upsamplingParameters.resolutionScale);

    const auto createTarget = [&](DXGI_FORMAT format,
                                  ComPtr<ID3D11Texture2D>& texture,
                                  ComPtr<ID3D11RenderTargetView>& rtv,
                                  ComPtr<ID3D11ShaderResourceView>& srv)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = static_cast<UINT>(targetWidth);
        desc.Height = static_cast<UINT>(targetHeight);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET |
                         D3D11_BIND_SHADER_RESOURCE;
        return SUCCEEDED(m_device->CreateTexture2D(
                   &desc, nullptr, &texture)) &&
               SUCCEEDED(m_device->CreateRenderTargetView(
                   texture.Get(), nullptr, &rtv)) &&
               SUCCEEDED(m_device->CreateShaderResourceView(
                   texture.Get(), nullptr, &srv));
    };

    ComPtr<ID3D11Texture2D> scattering;
    ComPtr<ID3D11RenderTargetView> scatteringRtv;
    ComPtr<ID3D11ShaderResourceView> scatteringSrv;
    ComPtr<ID3D11Texture2D> depth;
    ComPtr<ID3D11RenderTargetView> depthRtv;
    ComPtr<ID3D11ShaderResourceView> depthSrv;
    if (!createTarget(DXGI_FORMAT_R16G16B16A16_FLOAT,
                      scattering, scatteringRtv, scatteringSrv) ||
        !createTarget(DXGI_FORMAT_R32G32_FLOAT,
                      depth, depthRtv, depthSrv))
        return false;

    m_cloudScatteringTransmittance = scattering;
    m_cloudScatteringTransmittanceRtv = scatteringRtv;
    m_cloudScatteringTransmittanceSrv = scatteringSrv;
    m_cloudDepthSceneLimit = depth;
    m_cloudDepthSceneLimitRtv = depthRtv;
    m_cloudDepthSceneLimitSrv = depthSrv;
    m_cloudRenderWidth = targetWidth;
    m_cloudRenderHeight = targetHeight;
    return true;
}

bool Renderer::CreateTemporalHistoryTargets()
{
    if (!m_device || m_width <= 0 || m_height <= 0)
        return false;

    const auto createTarget = [&](DXGI_FORMAT format,
                                  ComPtr<ID3D11Texture2D>& texture,
                                  ComPtr<ID3D11RenderTargetView>& rtv,
                                  ComPtr<ID3D11ShaderResourceView>& srv)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = static_cast<UINT>(m_width);
        desc.Height = static_cast<UINT>(m_height);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET |
                         D3D11_BIND_SHADER_RESOURCE;
        return SUCCEEDED(m_device->CreateTexture2D(&desc, nullptr, &texture)) &&
               SUCCEEDED(m_device->CreateRenderTargetView(
                   texture.Get(), nullptr, &rtv)) &&
               SUCCEEDED(m_device->CreateShaderResourceView(
                   texture.Get(), nullptr, &srv));
    };

    ComPtr<ID3D11Texture2D> clouds[2];
    ComPtr<ID3D11RenderTargetView> cloudRtvs[2];
    ComPtr<ID3D11ShaderResourceView> cloudSrvs[2];
    ComPtr<ID3D11Texture2D> auxiliaries[2];
    ComPtr<ID3D11RenderTargetView> auxiliaryRtvs[2];
    ComPtr<ID3D11ShaderResourceView> auxiliarySrvs[2];
    for (int index = 0; index < 2; ++index)
    {
        if (!createTarget(DXGI_FORMAT_R16G16B16A16_FLOAT,
                          clouds[index], cloudRtvs[index], cloudSrvs[index]) ||
            !createTarget(DXGI_FORMAT_R16G16_FLOAT,
                          auxiliaries[index], auxiliaryRtvs[index],
                          auxiliarySrvs[index]))
            return false;
    }
    for (int index = 0; index < 2; ++index)
    {
        m_temporalHistoryCloud[index] = clouds[index];
        m_temporalHistoryCloudRtv[index] = cloudRtvs[index];
        m_temporalHistoryCloudSrv[index] = cloudSrvs[index];
        m_temporalHistoryAux[index] = auxiliaries[index];
        m_temporalHistoryAuxRtv[index] = auxiliaryRtvs[index];
        m_temporalHistoryAuxSrv[index] = auxiliarySrvs[index];
    }
    ResetTemporalHistory(Stage11HistoryResetReason::ResourcesRecreated);
    return true;
}

bool Renderer::CreateDeepShadowResources(Stage12ShadowPreset preset)
{
    if (!m_device)
        return false;
    const UINT resolution = stage12shadow::Resolution(preset);
    const auto createArray = [&](UINT slices,
                                 ComPtr<ID3D11Texture2D>& texture,
                                 ComPtr<ID3D11ShaderResourceView>& srv,
                                 ComPtr<ID3D11UnorderedAccessView>& uav)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = resolution;
        desc.Height = resolution;
        desc.MipLevels = 1;
        desc.ArraySize = slices;
        desc.Format = DXGI_FORMAT_R32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE |
                         D3D11_BIND_UNORDERED_ACCESS;
        ComPtr<ID3D11Texture2D> newTexture;
        if (FAILED(m_device->CreateTexture2D(&desc, nullptr, &newTexture)))
            return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        srvDesc.Texture2DArray.MostDetailedMip = 0;
        srvDesc.Texture2DArray.MipLevels = 1;
        srvDesc.Texture2DArray.FirstArraySlice = 0;
        srvDesc.Texture2DArray.ArraySize = slices;
        ComPtr<ID3D11ShaderResourceView> newSrv;
        if (FAILED(m_device->CreateShaderResourceView(
                newTexture.Get(), &srvDesc, &newSrv)))
            return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = desc.Format;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
        uavDesc.Texture2DArray.MipSlice = 0;
        uavDesc.Texture2DArray.FirstArraySlice = 0;
        uavDesc.Texture2DArray.ArraySize = slices;
        ComPtr<ID3D11UnorderedAccessView> newUav;
        if (FAILED(m_device->CreateUnorderedAccessView(
                newTexture.Get(), &uavDesc, &newUav)))
            return false;
        texture = newTexture;
        srv = newSrv;
        uav = newUav;
        return true;
    };

    ComPtr<ID3D11Texture2D> nearTexture;
    ComPtr<ID3D11ShaderResourceView> nearSrv;
    ComPtr<ID3D11UnorderedAccessView> nearUav;
    ComPtr<ID3D11Texture2D> farTexture;
    ComPtr<ID3D11ShaderResourceView> farSrv;
    ComPtr<ID3D11UnorderedAccessView> farUav;
    if (!createArray(stage12shadow::kNearSlices,
                     nearTexture, nearSrv, nearUav) ||
        !createArray(stage12shadow::kFarSlices,
                     farTexture, farSrv, farUav))
        return false;

    // 두 배열이 모두 준비된 뒤에만 기존 세트를 교체한다.
    m_shadowNearTexture = nearTexture;
    m_shadowNearSrv = nearSrv;
    m_shadowNearUav = nearUav;
    m_shadowFarTexture = farTexture;
    m_shadowFarSrv = farSrv;
    m_shadowFarUav = farUav;
    stage12shadow::ApplyPreset(m_shadowParameters, preset);
    return true;
}

bool Renderer::EnsureCloudTargets()
{
    const Stage10UpsamplingParameters sanitized =
        stage10upsampling::Sanitize(m_upsamplingParameters);
    const int expectedWidth = stage10upsampling::ScaledExtent(
        m_width, sanitized.resolutionScale);
    const int expectedHeight = stage10upsampling::ScaledExtent(
        m_height, sanitized.resolutionScale);
    m_upsamplingParameters = sanitized;
    return (m_cloudScatteringTransmittance && m_cloudDepthSceneLimit &&
            expectedWidth == m_cloudRenderWidth &&
            expectedHeight == m_cloudRenderHeight) || CreateCloudTargets();
}

bool Renderer::CompileShaderFromFile(const std::wstring& path,
                                     const char* entryPoint,
                                     const char* target,
                                     ComPtr<ID3DBlob>& outBlob,
                                     bool showErrors)
{
    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    compileFlags |= D3DCOMPILE_DEBUG;
    // 동적 단계 9 shader는 /Od에서 하드웨어 instruction 한도를 넘길 수 있다.
    // 실제 최적화 비용을 재는 경로이기도 하므로 Debug에서도 최소 O1을 사용한다.
    compileFlags |= (std::strcmp(entryPoint, "mainOptimized") == 0 ||
                     std::strcmp(entryPoint, "mainOptimizedData") == 0 ||
                     (std::strcmp(entryPoint, "main") == 0 &&
                      path == m_deepShadowShaderPath))
        ? D3DCOMPILE_OPTIMIZATION_LEVEL1
        : D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> errors;
    const HRESULT result = D3DCompileFromFile(
        path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entryPoint, target, compileFlags, 0, &outBlob, &errors);
    if (SUCCEEDED(result))
        return true;

    std::string message = "HLSL shader compile failed:\n";
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
    ComPtr<ID3DBlob> cloudReferencePsBlob;
    ComPtr<ID3DBlob> cloudOptimizedPsBlob;
    ComPtr<ID3DBlob> cloudReferenceDataPsBlob;
    ComPtr<ID3DBlob> cloudOptimizedDataPsBlob;
    ComPtr<ID3DBlob> cloudUpsamplePsBlob;
    ComPtr<ID3DBlob> cloudTemporalResolvePsBlob;
    ComPtr<ID3DBlob> noiseLabPsBlob;
    ComPtr<ID3DBlob> sceneVsBlob;
    ComPtr<ID3DBlob> scenePsBlob;
    ComPtr<ID3DBlob> noiseBaseCsBlob;
    ComPtr<ID3DBlob> noiseDetailCsBlob;
    ComPtr<ID3DBlob> deepShadowCsBlob;
    if (!CompileShaderFromFile(m_fullscreenShaderPath, "main", "vs_5_0", fullscreenVsBlob, showErrors) ||
        !CompileShaderFromFile(m_cloudShaderPath, "mainReference", "ps_5_0", cloudReferencePsBlob, showErrors) ||
        !CompileShaderFromFile(m_cloudShaderPath, "mainOptimized", "ps_5_0", cloudOptimizedPsBlob, showErrors) ||
        !CompileShaderFromFile(m_cloudShaderPath, "mainReferenceData", "ps_5_0", cloudReferenceDataPsBlob, showErrors) ||
        !CompileShaderFromFile(m_cloudShaderPath, "mainOptimizedData", "ps_5_0", cloudOptimizedDataPsBlob, showErrors) ||
        !CompileShaderFromFile(m_cloudUpsampleShaderPath, "main", "ps_5_0", cloudUpsamplePsBlob, showErrors) ||
        !CompileShaderFromFile(m_cloudTemporalResolveShaderPath, "main", "ps_5_0", cloudTemporalResolvePsBlob, showErrors) ||
        !CompileShaderFromFile(m_noiseLabShaderPath, "main", "ps_5_0", noiseLabPsBlob, showErrors) ||
        !CompileShaderFromFile(m_sceneShaderPath, "VSMain", "vs_5_0", sceneVsBlob, showErrors) ||
        !CompileShaderFromFile(m_sceneShaderPath, "PSMain", "ps_5_0", scenePsBlob, showErrors) ||
        !CompileShaderFromFile(m_deepShadowShaderPath, "main", "cs_5_0", deepShadowCsBlob, showErrors) ||
        (m_noiseVolumesEnabled &&
         (!CompileShaderFromFile(m_noiseVolumeShaderPath, "CSBase", "cs_5_0", noiseBaseCsBlob, showErrors) ||
          !CompileShaderFromFile(m_noiseVolumeShaderPath, "CSDetail", "cs_5_0", noiseDetailCsBlob, showErrors))))
    {
        m_shaderStatus = "Reload failed; previous generation kept";
        return false;
    }

    ComPtr<ID3D11VertexShader> fullscreenVs;
    ComPtr<ID3D11PixelShader> cloudReferencePs;
    ComPtr<ID3D11PixelShader> cloudOptimizedPs;
    ComPtr<ID3D11PixelShader> cloudReferenceDataPs;
    ComPtr<ID3D11PixelShader> cloudOptimizedDataPs;
    ComPtr<ID3D11PixelShader> cloudUpsamplePs;
    ComPtr<ID3D11PixelShader> cloudTemporalResolvePs;
    ComPtr<ID3D11PixelShader> noiseLabPs;
    ComPtr<ID3D11VertexShader> sceneVs;
    ComPtr<ID3D11PixelShader> scenePs;
    ComPtr<ID3D11ComputeShader> noiseBaseCs;
    ComPtr<ID3D11ComputeShader> noiseDetailCs;
    ComPtr<ID3D11ComputeShader> deepShadowCs;
    ComPtr<ID3D11InputLayout> inputLayout;

    if (FAILED(m_device->CreateVertexShader(
            fullscreenVsBlob->GetBufferPointer(), fullscreenVsBlob->GetBufferSize(),
            nullptr, &fullscreenVs)) ||
        FAILED(m_device->CreatePixelShader(
            cloudReferencePsBlob->GetBufferPointer(), cloudReferencePsBlob->GetBufferSize(),
            nullptr, &cloudReferencePs)) ||
        FAILED(m_device->CreatePixelShader(
            cloudOptimizedPsBlob->GetBufferPointer(), cloudOptimizedPsBlob->GetBufferSize(),
            nullptr, &cloudOptimizedPs)) ||
        FAILED(m_device->CreatePixelShader(
            cloudReferenceDataPsBlob->GetBufferPointer(), cloudReferenceDataPsBlob->GetBufferSize(),
            nullptr, &cloudReferenceDataPs)) ||
        FAILED(m_device->CreatePixelShader(
            cloudOptimizedDataPsBlob->GetBufferPointer(), cloudOptimizedDataPsBlob->GetBufferSize(),
            nullptr, &cloudOptimizedDataPs)) ||
        FAILED(m_device->CreatePixelShader(
            cloudUpsamplePsBlob->GetBufferPointer(), cloudUpsamplePsBlob->GetBufferSize(),
            nullptr, &cloudUpsamplePs)) ||
        FAILED(m_device->CreatePixelShader(
            cloudTemporalResolvePsBlob->GetBufferPointer(),
            cloudTemporalResolvePsBlob->GetBufferSize(), nullptr,
            &cloudTemporalResolvePs)) ||
        FAILED(m_device->CreatePixelShader(
            noiseLabPsBlob->GetBufferPointer(), noiseLabPsBlob->GetBufferSize(),
            nullptr, &noiseLabPs)) ||
        FAILED(m_device->CreateVertexShader(
            sceneVsBlob->GetBufferPointer(), sceneVsBlob->GetBufferSize(),
            nullptr, &sceneVs)) ||
        FAILED(m_device->CreatePixelShader(
            scenePsBlob->GetBufferPointer(), scenePsBlob->GetBufferSize(),
            nullptr, &scenePs)) ||
        FAILED(m_device->CreateComputeShader(
            deepShadowCsBlob->GetBufferPointer(), deepShadowCsBlob->GetBufferSize(),
            nullptr, &deepShadowCs)) ||
        (m_noiseVolumesEnabled &&
         (FAILED(m_device->CreateComputeShader(
              noiseBaseCsBlob->GetBufferPointer(), noiseBaseCsBlob->GetBufferSize(),
              nullptr, &noiseBaseCs)) ||
          FAILED(m_device->CreateComputeShader(
              noiseDetailCsBlob->GetBufferPointer(), noiseDetailCsBlob->GetBufferSize(),
              nullptr, &noiseDetailCs)))))
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

    ComPtr<ID3D11Texture3D> baseTexture;
    ComPtr<ID3D11ShaderResourceView> baseSrv;
    ComPtr<ID3D11Texture3D> detailTexture;
    ComPtr<ID3D11ShaderResourceView> detailSrv;
    std::uint64_t baseHash = 0;
    std::uint64_t detailHash = 0;
    float detailNeutralValue = 0.5f;
    double generationMilliseconds = 0.0;
    if (m_noiseVolumesEnabled && !GenerateNoiseVolumes(
            noiseBaseCs.Get(), noiseDetailCs.Get(), baseTexture, baseSrv,
            detailTexture, detailSrv, baseHash, detailHash,
            detailNeutralValue,
            generationMilliseconds))
    {
        m_shaderError = "Texture3D noise generation failed";
        m_shaderStatus = "Reload failed; previous generation kept";
        return false;
    }

    m_fullscreenVs = fullscreenVs;
    m_cloudReferencePs = cloudReferencePs;
    m_cloudOptimizedPs = cloudOptimizedPs;
    m_cloudReferenceDataPs = cloudReferenceDataPs;
    m_cloudOptimizedDataPs = cloudOptimizedDataPs;
    m_cloudUpsamplePs = cloudUpsamplePs;
    m_cloudTemporalResolvePs = cloudTemporalResolvePs;
    m_noiseLabPs = noiseLabPs;
    m_sceneVs = sceneVs;
    m_scenePs = scenePs;
    m_deepShadowCs = deepShadowCs;
    if (m_noiseVolumesEnabled)
    {
        m_noiseBaseCs = noiseBaseCs;
        m_noiseDetailCs = noiseDetailCs;
    }
    m_sceneInputLayout = inputLayout;
    m_baseNoiseVolume = baseTexture;
    m_baseNoiseVolumeSrv = baseSrv;
    m_detailNoiseVolume = detailTexture;
    m_detailNoiseVolumeSrv = detailSrv;
    m_baseNoiseVolumeHash = baseHash;
    m_detailNoiseVolumeHash = detailHash;
    m_cloudLodParameters.detailNeutralValue = detailNeutralValue;
    m_noiseVolumeGenerationMilliseconds = generationMilliseconds;
    ++m_shaderGeneration;
    m_shaderStatus = "Reload succeeded @ " + CurrentLocalTimeText();
    m_shaderError.clear();
    return true;
}

bool Renderer::CreateDiagnosticScene()
{
    std::vector<DiagnosticSceneVertex> vertices;
    std::vector<std::uint32_t> indices;
    // 13-4D 단일 씬은 10km 실제 평면과 3m×20층(60m) 건물 하나만 사용한다.
    AppendGroundPlane(vertices, indices);
    AppendBox(vertices, indices, { 0.0f, 30.0f, 0.0f }, { 10.0f, 30.0f, 10.0f },
              { 0.50f, 0.50f, 0.50f });

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
    if (FAILED(m_device->CreateSamplerState(&samplerDesc, &m_pointClampSampler)))
        return false;

    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    if (FAILED(m_device->CreateSamplerState(
            &samplerDesc, &m_linearClampSampler)))
        return false;

    // Weather Map의 연속 coverage/type 값은 bilinear로 읽고, 0/1 UV 경계는
    // 넓은 월드에서 반복되므로 wrap한다. Scene Depth의 point-clamp와 분리한다.
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    return SUCCEEDED(m_device->CreateSamplerState(
        &samplerDesc, &m_weatherLinearWrapSampler));
}

bool Renderer::CreateWeatherMapTexture(Stage5WeatherPreset preset)
{
    const WeatherMapGeneratorSettings safeSettings =
        SanitizeWeatherMapGeneratorSettings(m_weatherGeneratorSettings);
    const WeatherMapData map = BuildWeatherMap(preset, safeSettings);
    if (!IsValidWeatherMapData(map))
    {
        m_weatherMapStatus = "Initial weather map validation failed";
        return false;
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = map.width;
    desc.Height = map.height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA data = {};
    data.pSysMem = map.rgba.data();
    data.SysMemPitch = map.width * 4u;

    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(m_device->CreateTexture2D(&desc, &data, &texture)) ||
        FAILED(m_device->CreateShaderResourceView(texture.Get(), nullptr, &srv)))
    {
        m_weatherMapStatus = "Failed to create DEFAULT weather texture/SRV";
        return false;
    }

    m_weatherMapTexture = texture;
    m_weatherMapSrv = srv;
    m_weatherGeneratorSettings = safeSettings;
    m_cloudTypeMode = safeSettings.cloudTypeMode;
    m_weatherMapHash = HashWeatherMap(map);
    m_weatherPreset = preset;
    m_weatherMapStatus = "DEFAULT texture created";
    return true;
}

bool Renderer::UpdateWeatherMapTexture(
    Stage5WeatherPreset preset,
    const WeatherMapGeneratorSettings& settings)
{
    if (!m_weatherMapTexture || !m_weatherMapSrv || !m_context)
    {
        m_weatherMapStatus = "Weather texture is not initialized";
        return false;
    }

    const WeatherMapGeneratorSettings safeSettings =
        SanitizeWeatherMapGeneratorSettings(settings);
    const WeatherMapData map = BuildWeatherMap(preset, safeSettings);
    if (!IsValidWeatherMapData(map))
    {
        m_weatherMapStatus = "Weather map validation failed; previous map retained";
        return false;
    }

    // 이전 frame의 t2 바인딩을 명시적으로 해제한 뒤 같은 DEFAULT texture에
    // 새 CPU RGBA를 복사한다. texture/SRV 객체는 생성 이후 바뀌지 않는다.
    ID3D11ShaderResourceView* nullWeatherSrv = nullptr;
    m_context->PSSetShaderResources(2, 1, &nullWeatherSrv);
    m_context->UpdateSubresource(
        m_weatherMapTexture.Get(), 0, nullptr, map.rgba.data(), map.width * 4u, 0);
    const HRESULT deviceState = m_device->GetDeviceRemovedReason();
    if (FAILED(deviceState))
    {
        std::ostringstream failure;
        failure << "UpdateSubresource device failure 0x" << std::hex
                << static_cast<unsigned long>(deviceState);
        m_weatherMapStatus = failure.str();
        return false;
    }

    m_weatherGeneratorSettings = safeSettings;
    m_cloudTypeMode = safeSettings.cloudTypeMode;
    m_weatherMapHash = HashWeatherMap(map);
    m_weatherPreset = preset;
    std::ostringstream status;
    status << "UpdateSubresource OK, hash " << std::hex << m_weatherMapHash;
    m_weatherMapStatus = status.str();
    return true;
}

bool Renderer::HashNoiseVolume(ID3D11Texture3D* texture,
                               std::uint64_t& hash) const
{
    hash = 0;
    std::vector<std::uint8_t> bytes;
    if (!ReadNoiseVolumeBytesFromTexture(texture, bytes))
        return false;
    constexpr std::uint64_t offset = 1469598103934665603ull;
    constexpr std::uint64_t prime = 1099511628211ull;
    std::uint64_t value = offset;
    for (std::uint8_t byte : bytes)
    {
        value ^= byte;
        value *= prime;
    }
    hash = value;
    return true;
}

bool Renderer::ReadNoiseVolumeBytes(
    bool base, std::vector<std::uint8_t>& bytes) const
{
    ID3D11Texture3D* texture = base ? m_baseNoiseVolume.Get() :
        m_detailNoiseVolume.Get();
    return ReadNoiseVolumeBytesFromTexture(texture, bytes);
}

bool Renderer::ReadNoiseVolumeBytesFromTexture(
    ID3D11Texture3D* texture, std::vector<std::uint8_t>& bytes) const
{
    if (!texture || !m_device || !m_context)
        return false;
    D3D11_TEXTURE3D_DESC desc = {};
    texture->GetDesc(&desc);
    if (desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM || desc.MipLevels != 1)
        return false;

    D3D11_TEXTURE3D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;
    ComPtr<ID3D11Texture3D> staging;
    if (FAILED(m_device->CreateTexture3D(&stagingDesc, nullptr, &staging)))
        return false;
    m_context->CopyResource(staging.Get(), texture);

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(m_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return false;
    const std::size_t rowBytes = static_cast<std::size_t>(desc.Width) * 4u;
    const std::size_t sliceBytes = rowBytes * desc.Height;
    std::vector<std::uint8_t> loaded(sliceBytes * desc.Depth);
    for (UINT z = 0; z < desc.Depth; ++z)
    {
        for (UINT y = 0; y < desc.Height; ++y)
        {
            const auto* row = static_cast<const std::uint8_t*>(mapped.pData) +
                static_cast<std::size_t>(z) * mapped.DepthPitch +
                static_cast<std::size_t>(y) * mapped.RowPitch;
            std::memcpy(loaded.data() + static_cast<std::size_t>(z) * sliceBytes +
                            static_cast<std::size_t>(y) * rowBytes,
                        row, rowBytes);
        }
    }
    m_context->Unmap(staging.Get(), 0);
    bytes = std::move(loaded);
    return true;
}

bool Renderer::GenerateNoiseVolumes(
    ID3D11ComputeShader* baseShader, ID3D11ComputeShader* detailShader,
    ComPtr<ID3D11Texture3D>& baseTexture,
    ComPtr<ID3D11ShaderResourceView>& baseSrv,
    ComPtr<ID3D11Texture3D>& detailTexture,
    ComPtr<ID3D11ShaderResourceView>& detailSrv,
    std::uint64_t& baseHash, std::uint64_t& detailHash,
    float& detailNeutralValue,
    double& generationMilliseconds)
{
    if (!baseShader || !detailShader || !m_noiseVolumeCb)
        return false;
    const auto begin = std::chrono::steady_clock::now();
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(m_context->Map(
            m_noiseVolumeCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return false;
    std::memcpy(mapped.pData, &m_noiseVolumeParameters,
                sizeof(m_noiseVolumeParameters));
    m_context->Unmap(m_noiseVolumeCb.Get(), 0);

    const auto createAndDispatch = [&](UINT resolution,
                                       ID3D11ComputeShader* shader,
                                       ComPtr<ID3D11Texture3D>& texture,
                                       ComPtr<ID3D11ShaderResourceView>& srv)
    {
        D3D11_TEXTURE3D_DESC desc = {};
        desc.Width = resolution;
        desc.Height = resolution;
        desc.Depth = resolution;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE |
                         D3D11_BIND_UNORDERED_ACCESS;
        ComPtr<ID3D11Texture3D> newTexture;
        ComPtr<ID3D11UnorderedAccessView> uav;
        ComPtr<ID3D11ShaderResourceView> newSrv;
        if (FAILED(m_device->CreateTexture3D(&desc, nullptr, &newTexture)) ||
            FAILED(m_device->CreateUnorderedAccessView(
                newTexture.Get(), nullptr, &uav)) ||
            FAILED(m_device->CreateShaderResourceView(
                newTexture.Get(), nullptr, &newSrv)))
            return false;
        ID3D11Buffer* cb = m_noiseVolumeCb.Get();
        ID3D11UnorderedAccessView* output = uav.Get();
        m_context->CSSetShader(shader, nullptr, 0);
        m_context->CSSetConstantBuffers(6, 1, &cb);
        m_context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
        const UINT groups = (resolution + 3u) / 4u;
        m_context->Dispatch(groups, groups, groups);
        ID3D11UnorderedAccessView* nullUav = nullptr;
        ID3D11Buffer* nullCb = nullptr;
        m_context->CSSetUnorderedAccessViews(0, 1, &nullUav, nullptr);
        m_context->CSSetConstantBuffers(6, 1, &nullCb);
        m_context->CSSetShader(nullptr, nullptr, 0);
        texture = newTexture;
        srv = newSrv;
        return true;
    };

    if (!createAndDispatch(m_noiseVolumeParameters.baseResolution, baseShader,
                           baseTexture, baseSrv) ||
        !createAndDispatch(m_noiseVolumeParameters.detailResolution,
                           detailShader, detailTexture, detailSrv) ||
        !HashNoiseVolume(baseTexture.Get(), baseHash) ||
        !HashNoiseVolume(detailTexture.Get(), detailHash))
        return false;
    std::vector<std::uint8_t> detailBytes;
    if (!ReadNoiseVolumeBytesFromTexture(detailTexture.Get(), detailBytes))
        return false;
    const auto& weights = m_noiseVolumeParameters.detailWeights;
    detailNeutralValue = static_cast<float>(
        stage13optics::WeightedDetailMean(
            detailBytes, { weights.x, weights.y, weights.z, weights.w }));
    generationMilliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - begin).count();
    return true;
}

bool Renderer::RegenerateNoiseVolumes()
{
    if (!m_noiseBaseCs || !m_noiseDetailCs)
        return false;
    ComPtr<ID3D11Texture3D> baseTexture;
    ComPtr<ID3D11ShaderResourceView> baseSrv;
    ComPtr<ID3D11Texture3D> detailTexture;
    ComPtr<ID3D11ShaderResourceView> detailSrv;
    std::uint64_t baseHash = 0;
    std::uint64_t detailHash = 0;
    float detailNeutralValue = 0.5f;
    double milliseconds = 0.0;
    if (!GenerateNoiseVolumes(
            m_noiseBaseCs.Get(), m_noiseDetailCs.Get(), baseTexture, baseSrv,
            detailTexture, detailSrv, baseHash, detailHash,
            detailNeutralValue, milliseconds))
        return false;
    m_baseNoiseVolume = baseTexture;
    m_baseNoiseVolumeSrv = baseSrv;
    m_detailNoiseVolume = detailTexture;
    m_detailNoiseVolumeSrv = detailSrv;
    m_baseNoiseVolumeHash = baseHash;
    m_detailNoiseVolumeHash = detailHash;
    m_cloudLodParameters.detailNeutralValue = detailNeutralValue;
    m_noiseVolumeGenerationMilliseconds = milliseconds;
    ResetTemporalHistory(Stage11HistoryResetReason::ParametersChanged);
    return true;
}

void Renderer::SetNoiseSource(NoiseSource source)
{
    if (source != CurrentNoiseSource())
        ResetTemporalHistory(Stage11HistoryResetReason::ParametersChanged);
    m_noiseVolumeParameters.noiseSource = static_cast<std::uint32_t>(source);
    m_openWorldPipelinePreset = OpenWorldPipelinePreset::Custom;
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
           createDynamicBuffer(sizeof(LightParameters), &m_lightCb) &&
           createDynamicBuffer(sizeof(EnvironmentParameters), &m_environmentCb) &&
           createDynamicBuffer(sizeof(CloudDomainParameters), &m_cloudDomainCb) &&
           createDynamicBuffer(sizeof(NoiseVolumeParameters), &m_noiseVolumeCb) &&
           createDynamicBuffer(sizeof(CloudShapeParameters), &m_cloudShapeCb) &&
           createDynamicBuffer(sizeof(CloudLodParameters), &m_cloudLodCb) &&
           createDynamicBuffer(sizeof(OptimizationParameters), &m_optimizationCb) &&
           createDynamicBuffer(sizeof(Stage10UpsamplingParameters),
                               &m_upsamplingCb) &&
           createDynamicBuffer(sizeof(Stage11TemporalParameters),
                               &m_temporalCb) &&
           createDynamicBuffer(sizeof(Stage12ShadowParameters),
                               &m_shadowCb) &&
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
    m_cloudScatteringTransmittanceSrv.Reset();
    m_cloudScatteringTransmittanceRtv.Reset();
    m_cloudScatteringTransmittance.Reset();
    m_cloudDepthSceneLimitSrv.Reset();
    m_cloudDepthSceneLimitRtv.Reset();
    m_cloudDepthSceneLimit.Reset();
    for (int index = 0; index < 2; ++index)
    {
        m_temporalHistoryCloudSrv[index].Reset();
        m_temporalHistoryCloudRtv[index].Reset();
        m_temporalHistoryCloud[index].Reset();
        m_temporalHistoryAuxSrv[index].Reset();
        m_temporalHistoryAuxRtv[index].Reset();
        m_temporalHistoryAux[index].Reset();
    }
    m_cloudRenderWidth = 0;
    m_cloudRenderHeight = 0;
    ResetTemporalHistory(Stage11HistoryResetReason::Resize);
}

void Renderer::Resize(int width, int height)
{
    if (!m_swapChain || width <= 0 || height <= 0)
        return;

    m_width = width;
    m_height = height;
    m_frameProfiler.ResetMeasurements();
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    ID3D11ShaderResourceView* nullSrvs[6] = {};
    m_context->PSSetShaderResources(0, 6, nullSrvs);
    ReleaseSizeDependentResources();

    if (SUCCEEDED(m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0)))
    {
        CreateBackBufferTarget();
        CreateSceneTargets();
        CreateCloudTargets();
        CreateTemporalHistoryTargets();
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

    if (!m_renderOpaqueSceneForTest)
    {
        m_context->OMSetRenderTargets(0, nullptr, nullptr);
        return;
    }

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

void Renderer::RenderCloudPass(const Camera& camera, float timeSeconds,
                               ID3D11RenderTargetView* targetOverride)
{
    CameraCB cameraData = {};
    XMStoreFloat4x4(&cameraData.invViewProj, XMMatrixTranspose(camera.GetInvViewProj()));
    XMStoreFloat4x4(&cameraData.invProjection,
                    XMMatrixTranspose(camera.GetInvProjection()));
    XMStoreFloat4x4(&cameraData.invViewRotation,
                    XMMatrixTranspose(camera.GetInvViewRotation()));
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
    m_cloudDomainParameters = SanitizeCloudDomainParameters(
        m_cloudDomainParameters);
    if (SUCCEEDED(m_context->Map(
            m_cloudDomainCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        std::memcpy(mapped.pData, &m_cloudDomainParameters,
                    sizeof(m_cloudDomainParameters));
        m_context->Unmap(m_cloudDomainCb.Get(), 0);
    }
    m_lightParameters = stage6light::Sanitize(m_lightParameters);
    if (SUCCEEDED(m_context->Map(m_lightCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        std::memcpy(mapped.pData, &m_lightParameters, sizeof(m_lightParameters));
        m_context->Unmap(m_lightCb.Get(), 0);
    }
    m_environmentParameters = stage8environment::Sanitize(m_environmentParameters);
    if (SUCCEEDED(m_context->Map(m_environmentCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        std::memcpy(mapped.pData, &m_environmentParameters,
                    sizeof(m_environmentParameters));
        m_context->Unmap(m_environmentCb.Get(), 0);
    }
    m_cloudShapeParameters = SanitizeCloudShapeParameters(m_cloudShapeParameters);
    if (SUCCEEDED(m_context->Map(
            m_cloudShapeCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        std::memcpy(mapped.pData, &m_cloudShapeParameters,
                    sizeof(m_cloudShapeParameters));
        m_context->Unmap(m_cloudShapeCb.Get(), 0);
    }
    m_cloudLodParameters = stage13lod::Sanitize(m_cloudLodParameters);
    if (SUCCEEDED(m_context->Map(
            m_cloudLodCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        std::memcpy(mapped.pData, &m_cloudLodParameters,
                    sizeof(m_cloudLodParameters));
        m_context->Unmap(m_cloudLodCb.Get(), 0);
    }
    m_optimizationParameters = stage9optimization::Sanitize(
        m_optimizationParameters);
    if (SUCCEEDED(m_context->Map(
            m_optimizationCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        std::memcpy(mapped.pData, &m_optimizationParameters,
                    sizeof(m_optimizationParameters));
        m_context->Unmap(m_optimizationCb.Get(), 0);
    }
    UpdateStage12ShadowParameters(camera);
    if (SUCCEEDED(m_context->Map(
            m_shadowCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        std::memcpy(mapped.pData, &m_shadowParameters,
                    sizeof(m_shadowParameters));
        m_context->Unmap(m_shadowCb.Get(), 0);
    }

    const float clearColor[4] = { 0.02f, 0.03f, 0.05f, 1.0f };
    ID3D11RenderTargetView* cloudTarget =
        targetOverride ? targetOverride : m_backBufferRtv.Get();
    m_context->OMSetRenderTargets(1, &cloudTarget, nullptr);
    m_context->ClearRenderTargetView(cloudTarget, clearColor);
    m_context->OMSetDepthStencilState(nullptr, 0);
    m_context->RSSetState(nullptr);

    m_context->IASetInputLayout(nullptr);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_fullscreenVs.Get(), nullptr, 0);
    ID3D11PixelShader* cloudShader =
        stage9optimization::UsesReferenceShader(m_optimizationParameters)
        ? m_cloudReferencePs.Get() : m_cloudOptimizedPs.Get();
    m_context->PSSetShader(cloudShader, nullptr, 0);
    ID3D11Buffer* constantBuffers[2] = { m_cameraCb.Get(), m_cloudCb.Get() };
    m_context->PSSetConstantBuffers(0, 2, constantBuffers);
    ID3D11Buffer* lightBuffer = m_lightCb.Get();
    m_context->PSSetConstantBuffers(3, 1, &lightBuffer);
    ID3D11Buffer* environmentBuffer = m_environmentCb.Get();
    m_context->PSSetConstantBuffers(4, 1, &environmentBuffer);
    ID3D11Buffer* domainBuffer = m_cloudDomainCb.Get();
    m_context->PSSetConstantBuffers(5, 1, &domainBuffer);
    D3D11_MAPPED_SUBRESOURCE noiseVolumeMapped = {};
    if (SUCCEEDED(m_context->Map(
            m_noiseVolumeCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0,
            &noiseVolumeMapped)))
    {
        std::memcpy(noiseVolumeMapped.pData, &m_noiseVolumeParameters,
                    sizeof(m_noiseVolumeParameters));
        m_context->Unmap(m_noiseVolumeCb.Get(), 0);
    }
    ID3D11Buffer* noiseVolumeBuffer = m_noiseVolumeCb.Get();
    m_context->PSSetConstantBuffers(6, 1, &noiseVolumeBuffer);
    ID3D11Buffer* cloudShapeBuffer = m_cloudShapeCb.Get();
    m_context->PSSetConstantBuffers(7, 1, &cloudShapeBuffer);
    ID3D11Buffer* cloudLodBuffer = m_cloudLodCb.Get();
    m_context->PSSetConstantBuffers(8, 1, &cloudLodBuffer);
    ID3D11Buffer* optimizationBuffer = m_optimizationCb.Get();
    m_context->PSSetConstantBuffers(9, 1, &optimizationBuffer);
    ID3D11Buffer* shadowBuffer = m_shadowCb.Get();
    m_context->PSSetConstantBuffers(12, 1, &shadowBuffer);
    ID3D11ShaderResourceView* resources[5] = {
        m_sceneColorSrv.Get(), m_sceneDepthSrv.Get(), m_weatherMapSrv.Get(),
        m_baseNoiseVolumeSrv.Get(), m_detailNoiseVolumeSrv.Get()
    };
    m_context->PSSetShaderResources(0, 5, resources);
    ID3D11ShaderResourceView* shadowResources[2] = {
        m_shadowNearSrv.Get(), m_shadowFarSrv.Get()
    };
    m_context->PSSetShaderResources(6, 2, shadowResources);
    ID3D11SamplerState* samplers[2] = {
        m_pointClampSampler.Get(), m_weatherLinearWrapSampler.Get()
    };
    m_context->PSSetSamplers(0, 2, samplers);
    ID3D11SamplerState* shadowSampler = m_linearClampSampler.Get();
    m_context->PSSetSamplers(2, 1, &shadowSampler);
    m_context->Draw(3, 0);

    ID3D11ShaderResourceView* nullResources[8] = {};
    m_context->PSSetShaderResources(0, 8, nullResources);
}

void Renderer::UpdateStage12ShadowParameters(const Camera& camera)
{
    m_shadowParameters = stage12shadow::Sanitize(m_shadowParameters);
    const stage12shadow::LightBasis basis =
        stage12shadow::BuildLightBasis(m_lightParameters.directionToSun);
    m_shadowParameters.lightRight = basis.right;
    m_shadowParameters.lightUp = basis.up;
    m_shadowParameters.lightForward = basis.forward;
    m_shadowParameters.cloudBottomMeters =
        m_cloudDomainParameters.cloudBottomAltitude;
    m_shadowParameters.cloudTopMeters =
        m_cloudDomainParameters.cloudBottomAltitude +
        m_cloudDomainParameters.cloudLayerThickness;
    const XMFLOAT3 cameraPosition = camera.GetPosition();
    const XMFLOAT3 rawCenter{
        cameraPosition.x,
        0.5f * (m_shadowParameters.cloudBottomMeters +
                m_shadowParameters.cloudTopMeters),
        cameraPosition.z
    };
    m_shadowParameters.nearCenter = stage12shadow::SnappedCenter(
        rawCenter, basis, m_shadowParameters.nearWidthMeters,
        m_shadowParameters.nearResolution);
    m_shadowParameters.farCenter = stage12shadow::SnappedCenter(
        rawCenter, basis, m_shadowParameters.farWidthMeters,
        m_shadowParameters.farResolution);
    const bool resourcesReady = m_deepShadowCs && m_shadowNearSrv &&
        m_shadowNearUav && m_shadowFarSrv && m_shadowFarUav;
    const bool supportedDomain = m_cloudDomainParameters.domainType ==
        static_cast<std::uint32_t>(CloudDomainType::PlanarLayer);
    m_shadowParameters.cacheReady = resourcesReady && supportedDomain &&
        basis.valid && basis.forward.y >= m_shadowParameters.minimumSunY
        ? 1u : 0u;
}

void Renderer::RenderDeepShadowCaches(const Camera& camera, float timeSeconds)
{
    UpdateCloudConstantBuffers(camera, timeSeconds, m_width, m_height);
    if (m_shadowParameters.shadowMode !=
            static_cast<std::uint32_t>(Stage12ShadowMode::DeepCache) ||
        m_shadowParameters.cacheReady == 0u)
        return;

    ID3D11Buffer* cameraBuffer = m_cameraCb.Get();
    ID3D11Buffer* cloudBuffer = m_cloudCb.Get();
    ID3D11Buffer* domainBuffer = m_cloudDomainCb.Get();
    ID3D11Buffer* noiseBuffer = m_noiseVolumeCb.Get();
    ID3D11Buffer* shapeBuffer = m_cloudShapeCb.Get();
    ID3D11Buffer* lodBuffer = m_cloudLodCb.Get();
    ID3D11Buffer* optimizationBuffer = m_optimizationCb.Get();
    ID3D11Buffer* shadowBuffer = m_shadowCb.Get();
    m_context->CSSetConstantBuffers(0, 1, &cameraBuffer);
    m_context->CSSetConstantBuffers(1, 1, &cloudBuffer);
    m_context->CSSetConstantBuffers(5, 1, &domainBuffer);
    m_context->CSSetConstantBuffers(6, 1, &noiseBuffer);
    m_context->CSSetConstantBuffers(7, 1, &shapeBuffer);
    m_context->CSSetConstantBuffers(8, 1, &lodBuffer);
    m_context->CSSetConstantBuffers(9, 1, &optimizationBuffer);
    m_context->CSSetConstantBuffers(12, 1, &shadowBuffer);
    ID3D11ShaderResourceView* densityResources[3] = {
        m_weatherMapSrv.Get(), m_baseNoiseVolumeSrv.Get(),
        m_detailNoiseVolumeSrv.Get()
    };
    m_context->CSSetShaderResources(2, 3, densityResources);
    ID3D11SamplerState* weatherSampler = m_weatherLinearWrapSampler.Get();
    m_context->CSSetSamplers(1, 1, &weatherSampler);
    m_context->CSSetShader(m_deepShadowCs.Get(), nullptr, 0);

    const auto dispatch = [&](std::uint32_t cascade,
                              ID3D11UnorderedAccessView* uav,
                              std::uint32_t resolution) -> bool
    {
        m_shadowParameters.dispatchCascade = cascade;
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(m_context->Map(
                m_shadowCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            return false;
        std::memcpy(mapped.pData, &m_shadowParameters,
                    sizeof(m_shadowParameters));
        m_context->Unmap(m_shadowCb.Get(), 0);
        m_context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
        const UINT groups = (resolution + 7u) / 8u;
        m_context->Dispatch(groups, groups, 1);
        ID3D11UnorderedAccessView* nullUav = nullptr;
        m_context->CSSetUnorderedAccessViews(0, 1, &nullUav, nullptr);
        return true;
    };
    const bool nearDispatched = dispatch(
        0u, m_shadowNearUav.Get(), m_shadowParameters.nearResolution);
    const bool farDispatched = nearDispatched && dispatch(
        1u, m_shadowFarUav.Get(), m_shadowParameters.farResolution);
    if (!farDispatched)
        m_shadowParameters.cacheReady = 0u;
    m_shadowParameters.dispatchCascade = 0u;
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(m_context->Map(
            m_shadowCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        std::memcpy(mapped.pData, &m_shadowParameters,
                    sizeof(m_shadowParameters));
        m_context->Unmap(m_shadowCb.Get(), 0);
    }
    ID3D11ShaderResourceView* nullSrvs[3] = {};
    m_context->CSSetShaderResources(2, 3, nullSrvs);
    m_context->CSSetShader(nullptr, nullptr, 0);
}

void Renderer::UpdateCloudConstantBuffers(const Camera& camera,
                                          float timeSeconds,
                                          int renderWidth,
                                          int renderHeight)
{
    CameraCB cameraData = {};
    XMStoreFloat4x4(&cameraData.invViewProj,
                    XMMatrixTranspose(camera.GetInvViewProj()));
    XMStoreFloat4x4(&cameraData.invProjection,
                    XMMatrixTranspose(camera.GetInvProjection()));
    XMStoreFloat4x4(&cameraData.invViewRotation,
                    XMMatrixTranspose(camera.GetInvViewRotation()));
    cameraData.cameraPos = camera.GetPosition();
    cameraData.time = timeSeconds;
    cameraData.renderSize = {
        static_cast<float>(std::max(renderWidth, 1)),
        static_cast<float>(std::max(renderHeight, 1))
    };
    cameraData.nearPlane = camera.GetNearPlane();
    cameraData.farPlane = camera.GetFarPlane();

    m_cloudDomainParameters = SanitizeCloudDomainParameters(
        m_cloudDomainParameters);
    m_lightParameters = stage6light::Sanitize(m_lightParameters);
    m_environmentParameters = stage8environment::Sanitize(
        m_environmentParameters);
    m_cloudShapeParameters = SanitizeCloudShapeParameters(
        m_cloudShapeParameters);
    m_cloudLodParameters = stage13lod::Sanitize(m_cloudLodParameters);
    m_optimizationParameters = stage9optimization::Sanitize(
        m_optimizationParameters);
    m_upsamplingParameters = stage10upsampling::Sanitize(
        m_upsamplingParameters);
    m_temporalParameters = stage11temporal::Sanitize(
        m_temporalParameters);
    UpdateStage12ShadowParameters(camera);

    const auto update = [&](ID3D11Buffer* buffer, const void* data,
                            std::size_t size)
    {
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (buffer && SUCCEEDED(m_context->Map(
                buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            std::memcpy(mapped.pData, data, size);
            m_context->Unmap(buffer, 0);
        }
    };
    update(m_cameraCb.Get(), &cameraData, sizeof(cameraData));
    update(m_cloudCb.Get(), &m_cloudParameters, sizeof(m_cloudParameters));
    update(m_lightCb.Get(), &m_lightParameters, sizeof(m_lightParameters));
    update(m_environmentCb.Get(), &m_environmentParameters,
           sizeof(m_environmentParameters));
    update(m_cloudDomainCb.Get(), &m_cloudDomainParameters,
           sizeof(m_cloudDomainParameters));
    update(m_noiseVolumeCb.Get(), &m_noiseVolumeParameters,
           sizeof(m_noiseVolumeParameters));
    update(m_cloudShapeCb.Get(), &m_cloudShapeParameters,
           sizeof(m_cloudShapeParameters));
    update(m_cloudLodCb.Get(), &m_cloudLodParameters,
           sizeof(m_cloudLodParameters));
    update(m_optimizationCb.Get(), &m_optimizationParameters,
           sizeof(m_optimizationParameters));
    update(m_upsamplingCb.Get(), &m_upsamplingParameters,
           sizeof(m_upsamplingParameters));
    update(m_temporalCb.Get(), &m_temporalParameters,
           sizeof(m_temporalParameters));
    update(m_shadowCb.Get(), &m_shadowParameters,
           sizeof(m_shadowParameters));
}

void Renderer::BindCloudRaymarchResources(ID3D11PixelShader* pixelShader)
{
    m_context->IASetInputLayout(nullptr);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_fullscreenVs.Get(), nullptr, 0);
    m_context->PSSetShader(pixelShader, nullptr, 0);
    ID3D11Buffer* constantBuffers[2] = { m_cameraCb.Get(), m_cloudCb.Get() };
    m_context->PSSetConstantBuffers(0, 2, constantBuffers);
    ID3D11Buffer* lightBuffer = m_lightCb.Get();
    m_context->PSSetConstantBuffers(3, 1, &lightBuffer);
    ID3D11Buffer* environmentBuffer = m_environmentCb.Get();
    m_context->PSSetConstantBuffers(4, 1, &environmentBuffer);
    ID3D11Buffer* domainBuffer = m_cloudDomainCb.Get();
    m_context->PSSetConstantBuffers(5, 1, &domainBuffer);
    ID3D11Buffer* noiseVolumeBuffer = m_noiseVolumeCb.Get();
    m_context->PSSetConstantBuffers(6, 1, &noiseVolumeBuffer);
    ID3D11Buffer* shapeBuffer = m_cloudShapeCb.Get();
    m_context->PSSetConstantBuffers(7, 1, &shapeBuffer);
    ID3D11Buffer* lodBuffer = m_cloudLodCb.Get();
    m_context->PSSetConstantBuffers(8, 1, &lodBuffer);
    ID3D11Buffer* optimizationBuffer = m_optimizationCb.Get();
    m_context->PSSetConstantBuffers(9, 1, &optimizationBuffer);
    ID3D11Buffer* temporalBuffer = m_temporalCb.Get();
    m_context->PSSetConstantBuffers(11, 1, &temporalBuffer);
    ID3D11Buffer* shadowBuffer = m_shadowCb.Get();
    m_context->PSSetConstantBuffers(12, 1, &shadowBuffer);
    ID3D11ShaderResourceView* resources[5] = {
        m_sceneColorSrv.Get(), m_sceneDepthSrv.Get(), m_weatherMapSrv.Get(),
        m_baseNoiseVolumeSrv.Get(), m_detailNoiseVolumeSrv.Get()
    };
    m_context->PSSetShaderResources(0, 5, resources);
    ID3D11ShaderResourceView* shadowResources[2] = {
        m_shadowNearSrv.Get(), m_shadowFarSrv.Get()
    };
    m_context->PSSetShaderResources(6, 2, shadowResources);
    ID3D11SamplerState* samplers[2] = {
        m_pointClampSampler.Get(), m_weatherLinearWrapSampler.Get()
    };
    m_context->PSSetSamplers(0, 2, samplers);
    ID3D11SamplerState* shadowSampler = m_linearClampSampler.Get();
    m_context->PSSetSamplers(2, 1, &shadowSampler);
}

void Renderer::UnbindCloudShaderResources(UINT count)
{
    std::vector<ID3D11ShaderResourceView*> nullResources(count, nullptr);
    m_context->PSSetShaderResources(0, count, nullResources.data());
}

void Renderer::RenderCloudDataPass(const Camera& camera, float timeSeconds)
{
    if (!EnsureCloudTargets())
        return;
    const std::int32_t savedDebugMode = m_cloudParameters.debugMode;
    m_cloudParameters.debugMode = static_cast<std::int32_t>(
        CloudDebugMode::Composite);
    UpdateCloudConstantBuffers(camera, timeSeconds,
                               m_cloudRenderWidth, m_cloudRenderHeight);
    m_cloudParameters.debugMode = savedDebugMode;

    ID3D11RenderTargetView* targets[2] = {
        m_cloudScatteringTransmittanceRtv.Get(),
        m_cloudDepthSceneLimitRtv.Get()
    };
    const float clearCloud[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    const float clearDepth[4] = {
        camera.GetFarPlane(), camera.GetFarPlane(), 0.0f, 0.0f
    };
    m_context->OMSetRenderTargets(2, targets, nullptr);
    m_context->ClearRenderTargetView(targets[0], clearCloud);
    m_context->ClearRenderTargetView(targets[1], clearDepth);
    m_context->OMSetDepthStencilState(nullptr, 0);
    m_context->RSSetState(nullptr);
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(m_cloudRenderWidth);
    viewport.Height = static_cast<float>(m_cloudRenderHeight);
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);

    ID3D11PixelShader* shader =
        stage9optimization::UsesReferenceShader(m_optimizationParameters)
        ? m_cloudReferenceDataPs.Get() : m_cloudOptimizedDataPs.Get();
    BindCloudRaymarchResources(shader);
    m_context->Draw(3, 0);
    UnbindCloudShaderResources(8);
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
}

void Renderer::RenderCloudUpsamplePass(const Camera& camera,
                                       float timeSeconds,
                                       ID3D11RenderTargetView* targetOverride)
{
    UpdateCloudConstantBuffers(camera, timeSeconds, m_width, m_height);
    ID3D11RenderTargetView* target = targetOverride
        ? targetOverride : m_backBufferRtv.Get();
    const float clearColor[4] = { 0.02f, 0.03f, 0.05f, 1.0f };
    m_context->OMSetRenderTargets(1, &target, nullptr);
    m_context->ClearRenderTargetView(target, clearColor);
    m_context->OMSetDepthStencilState(nullptr, 0);
    m_context->RSSetState(nullptr);
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(m_width);
    viewport.Height = static_cast<float>(m_height);
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);
    m_context->IASetInputLayout(nullptr);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_fullscreenVs.Get(), nullptr, 0);
    m_context->PSSetShader(m_cloudUpsamplePs.Get(), nullptr, 0);
    ID3D11Buffer* constantBuffers[2] = { m_cameraCb.Get(), m_cloudCb.Get() };
    m_context->PSSetConstantBuffers(0, 2, constantBuffers);
    ID3D11Buffer* upsamplingBuffer = m_upsamplingCb.Get();
    m_context->PSSetConstantBuffers(10, 1, &upsamplingBuffer);
    ID3D11Buffer* shadowBuffer = m_shadowCb.Get();
    m_context->PSSetConstantBuffers(12, 1, &shadowBuffer);
    ID3D11ShaderResourceView* resources[4] = {
        m_sceneColorSrv.Get(), m_sceneDepthSrv.Get(),
        m_cloudScatteringTransmittanceSrv.Get(),
        m_cloudDepthSceneLimitSrv.Get()
    };
    m_context->PSSetShaderResources(0, 4, resources);
    ID3D11ShaderResourceView* shadowResources[2] = {
        m_shadowNearSrv.Get(), m_shadowFarSrv.Get()
    };
    m_context->PSSetShaderResources(6, 2, shadowResources);
    ID3D11SamplerState* shadowSampler = m_linearClampSampler.Get();
    m_context->PSSetSamplers(2, 1, &shadowSampler);
    m_context->Draw(3, 0);
    UnbindCloudShaderResources(8);
}

void Renderer::PrepareTemporalFrame(const Camera& camera, float timeSeconds)
{
    if (m_temporalPreviousFrameValid)
    {
        const XMFLOAT3 currentPosition = camera.GetPosition();
        const XMFLOAT3 delta = {
            currentPosition.x - m_temporalParameters.previousCameraPosition.x,
            currentPosition.y - m_temporalParameters.previousCameraPosition.y,
            currentPosition.z - m_temporalParameters.previousCameraPosition.z
        };
        const float distanceSquared =
            delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
        if (distanceSquared > 5000.0f * 5000.0f ||
            std::abs(camera.GetFovYDegrees() -
                     m_previousTemporalFovYDegrees) > 2.0f)
            ResetTemporalHistory(Stage11HistoryResetReason::CameraCut);
    }
    if (!m_temporalPreviousFrameValid)
    {
        XMStoreFloat4x4(&m_temporalParameters.previousViewProjection,
                        XMMatrixTranspose(camera.GetViewProj()));
        m_temporalParameters.previousCameraPosition = camera.GetPosition();
        m_previousTemporalTimeSeconds = timeSeconds;
        m_previousTemporalFovYDegrees = camera.GetFovYDegrees();
        m_temporalParameters.deltaTimeSeconds = 0.0f;
    }
    else
    {
        const float delta = timeSeconds - m_previousTemporalTimeSeconds;
        if (!std::isfinite(delta) || delta < 0.0f || delta > 0.25f)
        {
            ResetTemporalHistory(
                Stage11HistoryResetReason::TimeDiscontinuity);
            XMStoreFloat4x4(&m_temporalParameters.previousViewProjection,
                            XMMatrixTranspose(camera.GetViewProj()));
            m_temporalParameters.previousCameraPosition = camera.GetPosition();
            m_temporalParameters.deltaTimeSeconds = 0.0f;
        }
        else
        {
            m_temporalParameters.deltaTimeSeconds = delta;
        }
    }
    m_temporalParameters.historyValid = m_temporalHistoryValid ? 1u : 0u;
    m_temporalParameters.jitterOffsetLowResTexels =
        m_temporalParameters.jitterEnabled != 0u
        ? stage11temporal::JitterForFrame(m_temporalParameters.frameIndex)
        : XMFLOAT2{};
    m_temporalParameters = stage11temporal::Sanitize(m_temporalParameters);
}

void Renderer::CommitTemporalFrame(const Camera& camera, float timeSeconds)
{
    m_temporalHistoryReadIndex = 1u - m_temporalHistoryReadIndex;
    m_temporalHistoryValid = true;
    m_temporalPreviousFrameValid = true;
    m_temporalParameters.historyValid = 1u;
    XMStoreFloat4x4(&m_temporalParameters.previousViewProjection,
                    XMMatrixTranspose(camera.GetViewProj()));
    m_temporalParameters.previousCameraPosition = camera.GetPosition();
    m_previousTemporalTimeSeconds = timeSeconds;
    m_previousTemporalFovYDegrees = camera.GetFovYDegrees();
    if (m_temporalParameters.temporalEnabled != 0u)
    {
        ++m_temporalParameters.frameIndex;
        ++m_temporalAccumulatedFrames;
    }
    else
    {
        m_temporalParameters.frameIndex = 0;
        m_temporalAccumulatedFrames = 0;
    }
}

bool Renderer::RenderCloudTemporalPass(
    const Camera& camera, float timeSeconds,
    ID3D11RenderTargetView* targetOverride)
{
    const std::uint32_t writeIndex = 1u - m_temporalHistoryReadIndex;
    if (!m_cloudTemporalResolvePs || !m_temporalHistoryCloudRtv[writeIndex] ||
        !m_temporalHistoryAuxRtv[writeIndex] ||
        !m_temporalHistoryCloudSrv[m_temporalHistoryReadIndex] ||
        !m_temporalHistoryAuxSrv[m_temporalHistoryReadIndex])
    {
        ResetTemporalHistory(Stage11HistoryResetReason::ResourcesRecreated);
        return false;
    }

    UpdateCloudConstantBuffers(camera, timeSeconds, m_width, m_height);
    ID3D11RenderTargetView* outputTarget = targetOverride
        ? targetOverride : m_backBufferRtv.Get();
    ID3D11RenderTargetView* targets[3] = {
        m_temporalHistoryCloudRtv[writeIndex].Get(),
        m_temporalHistoryAuxRtv[writeIndex].Get(),
        outputTarget
    };
    const float clearColor[4] = { 0.02f, 0.03f, 0.05f, 1.0f };
    m_context->OMSetRenderTargets(3, targets, nullptr);
    m_context->ClearRenderTargetView(outputTarget, clearColor);
    m_context->OMSetDepthStencilState(nullptr, 0);
    m_context->RSSetState(nullptr);
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(m_width);
    viewport.Height = static_cast<float>(m_height);
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);
    m_context->IASetInputLayout(nullptr);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_fullscreenVs.Get(), nullptr, 0);
    m_context->PSSetShader(m_cloudTemporalResolvePs.Get(), nullptr, 0);
    ID3D11Buffer* cameraAndCloud[2] = { m_cameraCb.Get(), m_cloudCb.Get() };
    m_context->PSSetConstantBuffers(0, 2, cameraAndCloud);
    ID3D11Buffer* upsamplingBuffer = m_upsamplingCb.Get();
    ID3D11Buffer* temporalBuffer = m_temporalCb.Get();
    m_context->PSSetConstantBuffers(10, 1, &upsamplingBuffer);
    m_context->PSSetConstantBuffers(11, 1, &temporalBuffer);
    ID3D11Buffer* shadowBuffer = m_shadowCb.Get();
    m_context->PSSetConstantBuffers(12, 1, &shadowBuffer);
    ID3D11ShaderResourceView* resources[6] = {
        m_sceneColorSrv.Get(), m_sceneDepthSrv.Get(),
        m_cloudScatteringTransmittanceSrv.Get(),
        m_cloudDepthSceneLimitSrv.Get(),
        m_temporalHistoryCloudSrv[m_temporalHistoryReadIndex].Get(),
        m_temporalHistoryAuxSrv[m_temporalHistoryReadIndex].Get()
    };
    m_context->PSSetShaderResources(0, 6, resources);
    ID3D11ShaderResourceView* shadowResources[2] = {
        m_shadowNearSrv.Get(), m_shadowFarSrv.Get()
    };
    m_context->PSSetShaderResources(6, 2, shadowResources);
    ID3D11SamplerState* sampler = m_linearClampSampler.Get();
    m_context->PSSetSamplers(0, 1, &sampler);
    m_context->PSSetSamplers(2, 1, &sampler);
    m_context->Draw(3, 0);
    UnbindCloudShaderResources(8);
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    CommitTemporalFrame(camera, timeSeconds);
    return true;
}

bool Renderer::CaptureCloudDiagnosticFrame(
    const Camera& camera, float timeSeconds, CloudDebugMode mode,
    CloudDiagnosticFrame& frame, bool forceDirectComposite)
{
    frame = {};
    if (!m_device || !m_context || m_width <= 0 || m_height <= 0)
        return false;

    D3D11_TEXTURE2D_DESC targetDesc = {};
    targetDesc.Width = static_cast<UINT>(m_width);
    targetDesc.Height = static_cast<UINT>(m_height);
    targetDesc.MipLevels = 1;
    targetDesc.ArraySize = 1;
    targetDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    targetDesc.SampleDesc.Count = 1;
    targetDesc.Usage = D3D11_USAGE_DEFAULT;
    targetDesc.BindFlags = D3D11_BIND_RENDER_TARGET;

    ComPtr<ID3D11Texture2D> target;
    ComPtr<ID3D11RenderTargetView> targetRtv;
    if (FAILED(m_device->CreateTexture2D(&targetDesc, nullptr, &target)) ||
        FAILED(m_device->CreateRenderTargetView(
            target.Get(), nullptr, &targetRtv)))
        return false;

    D3D11_TEXTURE2D_DESC stagingDesc = targetDesc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(m_device->CreateTexture2D(
            &stagingDesc, nullptr, &staging)))
        return false;

    const CloudDebugMode previousMode = DebugMode();
    SetDebugMode(mode);
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(m_width);
    viewport.Height = static_cast<float>(m_height);
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);
    RenderDiagnosticScene(camera);
    RenderDeepShadowCaches(camera, timeSeconds);
    if (!forceDirectComposite && (mode == CloudDebugMode::Composite ||
        (static_cast<std::int32_t>(mode) >= 64 &&
         static_cast<std::int32_t>(mode) <= 73)) && EnsureCloudTargets())
    {
        PrepareTemporalFrame(camera, timeSeconds);
        RenderCloudDataPass(camera, timeSeconds);
        if (!RenderCloudTemporalPass(camera, timeSeconds, targetRtv.Get()))
            RenderCloudUpsamplePass(camera, timeSeconds, targetRtv.Get());
    }
    else
    {
        RenderCloudPass(camera, timeSeconds, targetRtv.Get());
    }
    SetDebugMode(previousMode);

    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    m_context->CopyResource(staging.Get(), target.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(m_context->Map(
            staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return false;

    frame.width = m_width;
    frame.height = m_height;
    frame.pixels.resize(
        static_cast<size_t>(m_width) * static_cast<size_t>(m_height));
    const size_t rowBytes = static_cast<size_t>(m_width) *
        sizeof(DirectX::XMFLOAT4);
    for (int y = 0; y < m_height; ++y)
    {
        const auto* source = static_cast<const unsigned char*>(mapped.pData) +
            static_cast<size_t>(y) * mapped.RowPitch;
        std::memcpy(frame.pixels.data() +
                        static_cast<size_t>(y) * static_cast<size_t>(m_width),
                    source, rowBytes);
    }
    m_context->Unmap(staging.Get(), 0);
    return true;
}

bool Renderer::CaptureSceneDepthDiagnosticFrame(
    const Camera& camera, SceneDepthDiagnosticFrame& frame)
{
    frame = {};
    if (!m_device || !m_context || !m_sceneDepth ||
        m_width <= 0 || m_height <= 0)
        return false;

    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(m_width);
    viewport.Height = static_cast<float>(m_height);
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);
    RenderDiagnosticScene(camera);
    D3D11_TEXTURE2D_DESC stagingDesc = {};
    m_sceneDepth->GetDesc(&stagingDesc);
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(m_device->CreateTexture2D(
            &stagingDesc, nullptr, &staging)))
        return false;

    m_context->CopyResource(staging.Get(), m_sceneDepth.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(m_context->Map(
            staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return false;

    frame.width = m_width;
    frame.height = m_height;
    frame.deviceDepth.resize(
        static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height));
    const std::size_t rowBytes = static_cast<std::size_t>(m_width) * sizeof(float);
    for (int y = 0; y < m_height; ++y)
    {
        const auto* source = static_cast<const unsigned char*>(mapped.pData) +
            static_cast<std::size_t>(y) * mapped.RowPitch;
        std::memcpy(frame.deviceDepth.data() +
                        static_cast<std::size_t>(y) *
                        static_cast<std::size_t>(m_width),
                    source, rowBytes);
    }
    m_context->Unmap(staging.Get(), 0);
    return true;
}

void Renderer::Render(Camera& camera, float timeSeconds)
{
    if (!m_backBufferRtv || !m_sceneColorRtv || !m_sceneDepthDsv)
        return;

    m_frameProfiler.BeginCpuFrame();
    CheckShaderHotReload();
    m_frameProfiler.BeginGpuFrame(m_context.Get());
    // Noise Lab은 CloudParameters를 직접 편집한다. 편집 전 값을 보관해
    // Pipeline Compare가 현재 최종값인지 판정한다.
    const CloudParameters parametersBeforeNoiseLab = m_cloudParameters;
    const CloudShapeParameters shapeBeforeNoiseLab = m_cloudShapeParameters;
    const CloudDomainParameters domainBeforeNoiseLab = m_cloudDomainParameters;
    const NoiseVolumeParameters noiseVolumeBeforeNoiseLab = m_noiseVolumeParameters;
    const LightParameters lightBeforeNoiseLab = m_lightParameters;
    const Stage12ShadowPreset shadowPresetBeforeNoiseLab = ShadowPreset();
    m_noiseLab.SetOpenWorldPipelinePreset(m_openWorldPipelinePreset);
    m_noiseLab.BeginFrame(timeSeconds, camera, m_cloudParameters,
                          m_cloudShapeParameters,
                          m_cloudDomainParameters,
                          m_cloudLodParameters,
                          m_optimizationParameters, m_optimizationPreset,
                          m_upsamplingParameters, m_resolutionPreset,
                          m_temporalParameters, m_temporalHistoryValid,
                          m_temporalAccumulatedFrames,
                          m_shadowParameters,
                          m_lightParameters, m_sunPreset, m_phasePreset,
                          m_environmentParameters, m_environmentPreset,
                          m_weatherPreset, m_weatherGeneratorSettings,
                          m_cloudTypeMode, m_cloudAppearancePreset,
                          m_cloudAppearanceDirty,
                          m_hasSavedCustomAppearance,
                          m_cloudAppearanceStatus,
                          m_cameraMoveSpeedMetersPerSecond,
                           m_noiseVolumeParameters, m_baseNoiseVolumeHash,
                          m_detailNoiseVolumeHash,
                          m_noiseVolumeGenerationMilliseconds,
                          m_weatherMapSrv.Get(), m_weatherMapStatus,
                          m_frameProfiler.Snapshot(), m_vsyncEnabled,
                          m_shaderGeneration, m_shaderStatus, m_shaderError);
    if (ShadowPreset() != shadowPresetBeforeNoiseLab &&
        !CreateDeepShadowResources(ShadowPreset()))
    {
        stage12shadow::ApplyPreset(
            m_shadowParameters, shadowPresetBeforeNoiseLab);
    }
    if (m_noiseLab.ConsumeParametersChanged())
    {
        if (m_temporalParameters.temporalEnabled != 0u)
        {
            stage10upsampling::ApplyResolutionPreset(
                m_upsamplingParameters, Stage10ResolutionPreset::Half);
            m_resolutionPreset = Stage10ResolutionPreset::Half;
        }
        ResetTemporalHistory(Stage11HistoryResetReason::ParametersChanged);
        m_pipelineComparisonActive = false;
        const bool similarityChanged =
            domainBeforeNoiseLab.domainType !=
                m_cloudDomainParameters.domainType ||
            std::memcmp(&parametersBeforeNoiseLab.cloudBoundsMin,
                        &m_cloudParameters.cloudBoundsMin,
                        sizeof(DirectX::XMFLOAT3)) != 0 ||
            std::memcmp(&parametersBeforeNoiseLab.cloudBoundsMax,
                        &m_cloudParameters.cloudBoundsMax,
                        sizeof(DirectX::XMFLOAT3)) != 0 ||
            parametersBeforeNoiseLab.stepSize != m_cloudParameters.stepSize ||
            parametersBeforeNoiseLab.maxViewSteps != m_cloudParameters.maxViewSteps ||
            parametersBeforeNoiseLab.extinctionCoefficient !=
                m_cloudParameters.extinctionCoefficient ||
            parametersBeforeNoiseLab.coverage != m_cloudParameters.coverage ||
            parametersBeforeNoiseLab.densityMultiplier !=
                m_cloudParameters.densityMultiplier ||
            parametersBeforeNoiseLab.baseNoiseScale != m_cloudParameters.baseNoiseScale ||
            parametersBeforeNoiseLab.windSpeed != m_cloudParameters.windSpeed ||
            parametersBeforeNoiseLab.detailNoiseScale !=
                m_cloudParameters.detailNoiseScale ||
            parametersBeforeNoiseLab.detailWindSpeed !=
                m_cloudParameters.detailWindSpeed ||
            parametersBeforeNoiseLab.detailErosionStrength !=
                m_cloudParameters.detailErosionStrength ||
            parametersBeforeNoiseLab.bottomFadeEnd !=
                m_cloudParameters.bottomFadeEnd ||
            parametersBeforeNoiseLab.topFadeStart !=
                m_cloudParameters.topFadeStart ||
            parametersBeforeNoiseLab.minimumLocalThicknessFraction !=
                m_cloudParameters.minimumLocalThicknessFraction ||
            parametersBeforeNoiseLab.localHeightVariation !=
                m_cloudParameters.localHeightVariation ||
            parametersBeforeNoiseLab.cumulusTopBoost !=
                m_cloudParameters.cumulusTopBoost ||
            parametersBeforeNoiseLab.weatherMapWorldSize !=
                m_cloudParameters.weatherMapWorldSize ||
            parametersBeforeNoiseLab.weatherMapWindSpeed !=
                m_cloudParameters.weatherMapWindSpeed ||
            domainBeforeNoiseLab.cloudBottomAltitude !=
                m_cloudDomainParameters.cloudBottomAltitude ||
            domainBeforeNoiseLab.cloudLayerThickness !=
                m_cloudDomainParameters.cloudLayerThickness ||
            domainBeforeNoiseLab.maxViewTraceDistance !=
                m_cloudDomainParameters.maxViewTraceDistance ||
            domainBeforeNoiseLab.viewTraceFadeStartDistance !=
                m_cloudDomainParameters.viewTraceFadeStartDistance ||
            domainBeforeNoiseLab.maxLightTraceDistance !=
                m_cloudDomainParameters.maxLightTraceDistance ||
            lightBeforeNoiseLab.maxLightSteps != m_lightParameters.maxLightSteps ||
            lightBeforeNoiseLab.lightStepSize != m_lightParameters.lightStepSize ||
            lightBeforeNoiseLab.lightRayBias != m_lightParameters.lightRayBias;
        const bool shapeChanged = std::memcmp(
            &shapeBeforeNoiseLab, &m_cloudShapeParameters,
            sizeof(CloudShapeParameters)) != 0;
        const bool noiseVolumeScaleChanged =
            noiseVolumeBeforeNoiseLab.baseWorldSizeMeters !=
                m_noiseVolumeParameters.baseWorldSizeMeters ||
            noiseVolumeBeforeNoiseLab.baseVerticalWorldSizeMeters !=
                m_noiseVolumeParameters.baseVerticalWorldSizeMeters;
        const bool opticalPresetChanged =
            lightBeforeNoiseLab.singleScatteringAlbedo !=
                m_lightParameters.singleScatteringAlbedo;
        if (similarityChanged || opticalPresetChanged || shapeChanged ||
            noiseVolumeScaleChanged)
        {
            m_openWorldPipelinePreset = OpenWorldPipelinePreset::Custom;
        }
    }
    if (m_noiseLab.ConsumeTemporalResetRequest())
        ResetTemporalHistory(Stage11HistoryResetReason::Manual);
    Stage5WeatherPreset requestedWeatherPreset = m_weatherPreset;
    if (m_noiseLab.ConsumeWeatherPresetRequest(requestedWeatherPreset))
        ApplyStage5WeatherPreset(requestedWeatherPreset);
    WeatherMapGeneratorSettings requestedGeneratorSettings;
    const bool appearanceEdited = m_noiseLab.ConsumeCloudAppearanceEdited();
    if (m_noiseLab.ConsumeWeatherGeneratorRequest(requestedGeneratorSettings))
    {
        if (ApplyWeatherGeneratorSettings(requestedGeneratorSettings) &&
            appearanceEdited)
            MarkCloudAppearanceDirty();
    }
    else if (appearanceEdited)
    {
        MarkCloudAppearanceDirty();
    }
    OpenWorldPipelinePreset requestedPipelinePreset =
        m_openWorldPipelinePreset;
    if (m_noiseLab.ConsumeOpenWorldPipelinePresetRequest(
            requestedPipelinePreset))
    {
        if (ApplyOpenWorldPipelinePreset(requestedPipelinePreset))
            m_pipelineComparisonActive = true;
    }
    CloudAppearancePreset requestedAppearance = m_cloudAppearancePreset;
    if (m_noiseLab.ConsumeCloudAppearancePresetRequest(requestedAppearance))
        ApplyCloudAppearancePreset(requestedAppearance);
    if (m_noiseLab.ConsumeCloudAppearanceSaveRequest())
        SaveCurrentCloudAppearance();
    NoiseSource requestedNoiseSource = CurrentNoiseSource();
    if (m_noiseLab.ConsumeNoiseSourceRequest(requestedNoiseSource))
        SetNoiseSource(requestedNoiseSource);
    if (m_noiseLab.ConsumeNoiseVolumeRegenerateRequest())
        RegenerateNoiseVolumes();
    const float effectiveTime = ResolvePipelineComparisonTime(
        m_pipelineComparisonActive, m_noiseLab.EffectiveTime());
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(m_width);
    viewport.Height = static_cast<float>(m_height);
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);

    RenderDiagnosticScene(camera);
    m_frameProfiler.BeginCloudPass(m_context.Get());
    RenderDeepShadowCaches(camera, effectiveTime);
    m_frameProfiler.MarkShadowCacheEnd(m_context.Get());
    const CloudDebugMode debugMode = DebugMode();
    if ((debugMode == CloudDebugMode::Composite ||
        (static_cast<std::int32_t>(debugMode) >= 64 &&
         static_cast<std::int32_t>(debugMode) <= 73)) && EnsureCloudTargets())
    {
        PrepareTemporalFrame(camera, effectiveTime);
        RenderCloudDataPass(camera, effectiveTime);
        m_frameProfiler.MarkCloudRaymarchEnd(m_context.Get());
        if (!RenderCloudTemporalPass(camera, effectiveTime))
            RenderCloudUpsamplePass(camera, effectiveTime);
    }
    else
    {
        RenderCloudPass(camera, effectiveTime);
        m_frameProfiler.MarkCloudRaymarchEnd(m_context.Get());
    }
    m_frameProfiler.EndCloudPass(m_context.Get());
    if (m_captureFrameHashes)
        CaptureCloudFrameHash();
    if (m_renderNoiseLabPreviews)
    {
        m_noiseLab.RenderPreviews(
            m_fullscreenVs.Get(), m_noiseLabPs.Get(), m_cloudCb.Get(),
            m_noiseVolumeCb.Get(), m_cloudShapeCb.Get(), m_weatherMapSrv.Get(),
            m_baseNoiseVolumeSrv.Get(), m_detailNoiseVolumeSrv.Get(),
            m_weatherLinearWrapSampler.Get());
    }
    if (m_noiseLab.ConsumeExportRequest())
    {
        std::filesystem::path shaderDirectory(m_shaderDir);
        if (shaderDirectory.filename().empty())
            shaderDirectory = shaderDirectory.parent_path();
        m_noiseLab.ExportSnapshot(shaderDirectory.parent_path() / L"captures" / L"noise-lab",
                                  m_cloudParameters,
                                  m_cloudShapeParameters,
                                  m_cloudDomainParameters,
                                  m_cloudLodParameters,
                                  m_optimizationParameters,
                                  m_optimizationPreset,
                                  m_upsamplingParameters,
                                  m_resolutionPreset,
                                  m_temporalParameters,
                                  m_shadowParameters,
                                  m_cloudRenderWidth,
                                  m_cloudRenderHeight,
                                  m_lightParameters,
                                  m_sunPreset,
                                  m_phasePreset,
                                  m_environmentParameters,
                                  m_environmentPreset, m_weatherPreset,
                                  m_cloudTypeMode,
                                  m_cloudAppearancePreset,
                                  m_cloudAppearanceDirty,
                                  m_hasSavedCustomAppearance,
                                  m_savedCustomAppearance,
                                  m_noiseVolumeParameters,
                                  m_baseNoiseVolumeHash,
                                  m_detailNoiseVolumeHash,
                                  m_weatherGeneratorSettings,
                                  m_weatherMapHash,
                                  m_weatherMapTexture.Get(),
                                  shaderDirectory / L"Noise.hlsli");
    }
    m_noiseLab.EndFrame(m_backBufferRtv.Get());
    m_frameProfiler.EndGpuFrame(m_context.Get());
    m_swapChain->Present(m_vsyncEnabled ? 1u : 0u, 0);
    m_frameProfiler.EndCpuFrame();
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
    m_cloudParameters.debugMode = static_cast<std::int32_t>(
        stage13scene::SanitizeDebugMode(mode));
}

CloudDebugMode Renderer::DebugMode() const
{
    return stage13scene::SanitizeDebugMode(
        static_cast<CloudDebugMode>(m_cloudParameters.debugMode));
}

void Renderer::ConfigureVolumeForTest(DirectX::XMFLOAT3 boundsMin,
                                      DirectX::XMFLOAT3 boundsMax,
                                      float stepSize)
{
    m_openWorldPipelinePreset = OpenWorldPipelinePreset::Custom;
    // 각 키는 다른 키의 잔여 상태가 결과를 흐리지 않도록 단계 1 기본값에서 시작한다.
    m_cloudParameters.cloudBoundsMin = boundsMin;
    m_cloudParameters.cloudBoundsMax = boundsMax;
    m_cloudParameters.densityMultiplier = 1.0f;
    m_cloudParameters.stepSize = std::max(stepSize, 0.0001f);
    m_cloudParameters.maxViewSteps = 128;
    m_cloudParameters.extinctionCoefficient = 1.0f;
    m_cloudParameters.transmittanceThreshold = 0.01f;

}

void Renderer::ConfigureNoiseForTest(float baseScale, float coverage,
                                     float densityMultiplier, float windSpeed,
                                     float noiseOffset)
{
    m_openWorldPipelinePreset = OpenWorldPipelinePreset::Custom;
    // 프리셋을 누르는 순서와 무관하게 비교할 수 있도록 noise 관련 값만 기본화한다.
    // AABB와 step 프리셋은 유지되어 두 종류의 검증을 조합할 수 있다.
    m_cloudParameters.baseNoiseScale = std::max(baseScale, 0.0001f);
    m_cloudParameters.coverage = std::clamp(coverage, 0.0f, 1.0f);
    m_cloudParameters.densityMultiplier = std::max(densityMultiplier, 0.0f);
    m_cloudParameters.windDirection = { 0.9701425f, 0.0f, 0.2425356f };
    m_cloudParameters.windSpeed = std::max(windSpeed, 0.0f);
    m_cloudParameters.noiseOffset = noiseOffset;
}

void Renderer::SetHeightProfile(float bottomFadeEnd, float topFadeStart)
{
    m_openWorldPipelinePreset = OpenWorldPipelinePreset::Custom;
    // CPU에서도 UI와 같은 범위를 보장해 smoke test나 이후 프리셋이 잘못된
    // smoothstep edge를 GPU로 보내지 않게 한다. 두 범위의 교차는 의도적으로 허용한다.
    m_cloudParameters.bottomFadeEnd = std::clamp(bottomFadeEnd, 0.01f, 0.99f);
    m_cloudParameters.topFadeStart = std::clamp(topFadeStart, 0.01f, 0.99f);
}

void Renderer::ConfigureDetailForTest(float detailScale,
                                      float erosionStrength,
                                      float windSpeed, float noiseOffset)
{
    m_openWorldPipelinePreset = OpenWorldPipelinePreset::Custom;
    // 프리셋 전환 순서와 무관하게 네 Detail 값만 기본화한다. Base noise, 높이와
    // Q/Y 볼륨은 그대로 두므로 큰 형태가 변하지 않는지 직접 비교할 수 있다.
    m_cloudParameters.detailNoiseScale = std::max(detailScale, 0.0001f);
    m_cloudParameters.detailErosionStrength =
        std::clamp(erosionStrength, 0.0f, 1.0f);
    m_cloudParameters.detailWindSpeed = std::max(windSpeed, 0.0f);
    m_cloudParameters.detailNoiseOffset = noiseOffset;
}

bool Renderer::ApplyStage5WeatherPreset(Stage5WeatherPreset preset)
{
    const bool changed = preset != m_weatherPreset;
    // 프리셋 전환도 초기 texture/SRV를 재생성하지 않고 픽셀만 교체한다.
    if (!UpdateWeatherMapTexture(preset, m_weatherGeneratorSettings))
        return false;
    if (changed)
    {
        m_openWorldPipelinePreset = OpenWorldPipelinePreset::Custom;
        ResetTemporalHistory(Stage11HistoryResetReason::ParametersChanged);
    }
    return true;
}

void Renderer::ApplyStage6SunPreset(Stage6SunPreset preset)
{
    ResetTemporalHistory(Stage11HistoryResetReason::ParametersChanged);
    if (preset == Stage6SunPreset::Custom)
    {
        m_sunPreset = preset;
        return;
    }
    // 방향 프리셋은 사용자가 조절한 색·세기·품질 설정을 보존한다.
    m_lightParameters.directionToSun =
        stage6light::Preset(preset).directionToSun;
    m_sunPreset = preset;
}

void Renderer::ApplyStage7PhasePreset(Stage7PhasePreset preset)
{
    ResetTemporalHistory(Stage11HistoryResetReason::ParametersChanged);
    if (preset == Stage7PhasePreset::Custom)
    {
        m_phasePreset = preset;
        return;
    }
    stage6light::ApplyPhasePreset(m_lightParameters, preset);
    m_phasePreset = preset;
}

void Renderer::ApplyStage8EnvironmentPreset(Stage8EnvironmentPreset preset)
{
    ResetTemporalHistory(Stage11HistoryResetReason::ParametersChanged);
    if (preset == Stage8EnvironmentPreset::Custom)
    {
        m_environmentParameters =
            stage8environment::Sanitize(m_environmentParameters);
        m_environmentPreset = preset;
        return;
    }
    stage8environment::ApplyPreset(m_environmentParameters, preset);
    m_environmentPreset = preset;
}

void Renderer::ApplyPortfolioHeroLighting()
{
    ResetTemporalHistory(Stage11HistoryResetReason::ParametersChanged);
    m_lightParameters.directionToSun = stage6light::Preset(
        Stage6SunPreset::LowEast).directionToSun;
    m_lightParameters.sunColor = { 1.0f, 0.78f, 0.62f };
    m_lightParameters.sunIntensity = 1.15f;
    stage6light::ApplyPhasePreset(
        m_lightParameters, Stage7PhasePreset::SilverLining);
    stage8environment::ApplyPreset(
        m_environmentParameters, Stage8EnvironmentPreset::PortfolioHero);
    m_sunPreset = Stage6SunPreset::LowEast;
    m_phasePreset = Stage7PhasePreset::SilverLining;
    m_environmentPreset = Stage8EnvironmentPreset::PortfolioHero;
}

void Renderer::SetLightSampling(std::uint32_t maxSteps, float stepSize)
{
    m_lightParameters.maxLightSteps = maxSteps;
    m_lightParameters.lightStepSize = stepSize;
    m_lightParameters = stage6light::Sanitize(m_lightParameters);
    m_sunPreset = Stage6SunPreset::Custom;
}

void Renderer::SetCloudLodForValidation(bool enabled, float startMeters,
                                        float endMeters)
{
    m_cloudLodParameters.detailLodEnabled = enabled ? 1u : 0u;
    m_cloudLodParameters.detailLodStartMeters = startMeters;
    m_cloudLodParameters.detailLodEndMeters = endMeters;
    m_cloudLodParameters = stage13lod::Sanitize(m_cloudLodParameters);
}

void Renderer::SetViewSamplingForSmoke(std::uint32_t maxSteps, float stepSize)
{
    m_cloudParameters.maxViewSteps = std::max(maxSteps, 1u);
    m_cloudParameters.stepSize = std::max(stepSize, 1e-4f);
}

void Renderer::ApplyStage9OptimizationPreset(Stage9OptimizationPreset preset)
{
    ResetTemporalHistory(Stage11HistoryResetReason::ParametersChanged);
    stage9optimization::ApplyPreset(
        m_optimizationParameters, preset,
        m_cloudParameters.maxViewSteps, m_cloudParameters.stepSize,
        m_lightParameters.maxLightSteps, m_lightParameters.lightStepSize,
        m_cloudParameters.transmittanceThreshold);
    m_optimizationPreset = preset;
}

void Renderer::ApplyStage10ResolutionPreset(
    Stage10ResolutionPreset preset)
{
    if (preset == Stage10ResolutionPreset::Custom)
        return;
    stage10upsampling::ApplyResolutionPreset(
        m_upsamplingParameters, preset);
    m_upsamplingParameters = stage10upsampling::Sanitize(
        m_upsamplingParameters);
    m_resolutionPreset = preset;
    m_frameProfiler.ResetMeasurements();
    ResetTemporalHistory(Stage11HistoryResetReason::ResolutionOrFilter);
}

void Renderer::SetStage10UpsampleFilter(Stage10UpsampleFilter filter)
{
    m_upsamplingParameters.filterMode = static_cast<std::uint32_t>(filter);
    m_upsamplingParameters = stage10upsampling::Sanitize(
        m_upsamplingParameters);
    m_frameProfiler.ResetMeasurements();
    ResetTemporalHistory(Stage11HistoryResetReason::ResolutionOrFilter);
}

void Renderer::SetStage11TemporalMode(Stage11TemporalMode mode)
{
    const Stage11TemporalMode previous = TemporalMode();
    stage11temporal::ApplyMode(m_temporalParameters, mode);
    if (mode == Stage11TemporalMode::Stable4Phase)
    {
        stage10upsampling::ApplyResolutionPreset(
            m_upsamplingParameters, Stage10ResolutionPreset::Half);
        m_resolutionPreset = Stage10ResolutionPreset::Half;
    }
    m_temporalParameters = stage11temporal::Sanitize(m_temporalParameters);
    if (previous != mode)
    {
        ResetTemporalHistory(Stage11HistoryResetReason::TemporalToggle);
        m_frameProfiler.ResetMeasurements();
    }
}

void Renderer::SetStage12ShadowMode(Stage12ShadowMode mode)
{
    const Stage12ShadowMode previous = ShadowMode();
    m_shadowParameters.shadowMode = static_cast<std::uint32_t>(mode);
    m_shadowParameters = stage12shadow::Sanitize(m_shadowParameters);
    if (previous != mode)
    {
        ResetTemporalHistory(Stage11HistoryResetReason::ParametersChanged);
        m_frameProfiler.ResetMeasurements();
    }
}

bool Renderer::SetStage12ShadowPreset(Stage12ShadowPreset preset)
{
    if (preset == ShadowPreset())
        return true;
    if (!CreateDeepShadowResources(preset))
        return false;
    ResetTemporalHistory(Stage11HistoryResetReason::ParametersChanged);
    m_frameProfiler.ResetMeasurements();
    return true;
}

void Renderer::ResetTemporalHistory(Stage11HistoryResetReason reason)
{
    m_temporalHistoryValid = false;
    m_temporalPreviousFrameValid = false;
    m_temporalHistoryReadIndex = 0;
    m_temporalAccumulatedFrames = 0;
    m_temporalParameters.frameIndex = 0;
    m_temporalParameters.historyValid = 0;
    m_temporalParameters.deltaTimeSeconds = 0.0f;
    m_temporalParameters.jitterOffsetLowResTexels = {};
    m_temporalParameters.resetReason = static_cast<std::uint32_t>(reason);
}

void Renderer::ConfigureStage9ConeForValidation(std::uint32_t taps,
                                                float angleDegrees,
                                                float farSampleFraction)
{
    m_optimizationParameters.lightSamplingMode = static_cast<std::uint32_t>(
        Stage9LightSamplingMode::DeterministicCone);
    m_optimizationParameters.coneSampleCount = taps;
    m_optimizationParameters.coneAngleDegrees = angleDegrees;
    m_optimizationParameters.lightFarSampleFraction = farSampleFraction;
    m_optimizationParameters = stage9optimization::Sanitize(
        m_optimizationParameters);
    m_optimizationPreset = Stage9OptimizationPreset::Custom;
}

void Renderer::SetCloudWindSpeedsForValidation(float bulkSpeed,
                                               float weatherSpeed,
                                               float detailSpeed)
{
    const auto safeSpeed = [](float value)
    {
        return std::isfinite(value) ? std::max(value, 0.0f) : 0.0f;
    };
    m_cloudParameters.windSpeed = safeSpeed(bulkSpeed);
    m_cloudParameters.weatherMapWindSpeed = safeSpeed(weatherSpeed);
    m_cloudParameters.detailWindSpeed = safeSpeed(detailSpeed);
}

void Renderer::SetCloudDomainType(CloudDomainType type)
{
    if (type != DomainType())
    {
        ResetTemporalHistory(Stage11HistoryResetReason::ParametersChanged);
    }
    m_cloudDomainParameters.domainType = static_cast<std::uint32_t>(type);
    m_cloudDomainParameters = SanitizeCloudDomainParameters(
        m_cloudDomainParameters);
}

bool Renderer::ApplyStage13SimilarityScale(float scale)
{
    if (scale != 1.0f && scale != 10.0f &&
        scale != 100.0f && scale != 1000.0f)
        return false;

    const stage13scale::SimilarityParameters scaled =
        stage13scale::ScaleSimilarity({}, static_cast<double>(scale));
    const float horizontalHalf =
        static_cast<float>(scaled.horizontalSizeMeters * 0.5);
    const float bottom =
        static_cast<float>(scaled.layerBottomAltitudeMeters);
    const float top = bottom + static_cast<float>(scaled.verticalSizeMeters);

    // 어떤 순서로 버튼을 눌러도 같은 Stage 8 기준에서 정확히 S배가 되도록
    // 형태·광학·이동 파라미터를 모두 기준값에서 다시 계산한다.
    m_cloudParameters.cloudBoundsMin = { -horizontalHalf, bottom,
                                         -horizontalHalf };
    m_cloudParameters.cloudBoundsMax = { horizontalHalf, top,
                                         horizontalHalf };
    m_cloudParameters.stepSize = static_cast<float>(scaled.viewStepMeters);
    m_cloudParameters.maxViewSteps = scaled.maxViewSteps;
    m_cloudParameters.extinctionCoefficient =
        static_cast<float>(scaled.extinctionPerMeter);
    m_cloudParameters.transmittanceThreshold = 0.01f;
    m_cloudParameters.baseNoiseScale =
        static_cast<float>(scaled.baseNoiseCyclesPerMeter);
    m_cloudParameters.coverage = 0.55f;
    m_cloudParameters.densityMultiplier =
        static_cast<float>(scaled.densityMultiplier);
    m_cloudParameters.windDirection = { 0.9701425f, 0.0f, 0.2425356f };
    m_cloudParameters.windSpeed =
        static_cast<float>(scaled.baseWindMetersPerSecond);
    m_cloudParameters.noiseOffset = 0.0f;
    m_cloudParameters.bottomFadeEnd = 0.20f;
    m_cloudParameters.topFadeStart = 0.80f;
    m_cloudParameters.minimumLocalThicknessFraction = 0.40f;
    m_cloudParameters.localHeightVariation = 0.0f;
    m_cloudParameters.cumulusTopBoost = 0.35f;
    m_cloudParameters.detailNoiseScale =
        static_cast<float>(scaled.detailNoiseCyclesPerMeter);
    m_cloudParameters.detailErosionStrength = 0.25f;
    m_cloudParameters.detailWindSpeed =
        static_cast<float>(scaled.detailWindMetersPerSecond);
    m_cloudParameters.detailNoiseOffset = 17.3f;
    m_cloudParameters.weatherMapWorldSize =
        static_cast<float>(scaled.weatherWorldSizeMeters);
    m_cloudParameters.weatherMapWindSpeed =
        static_cast<float>(scaled.weatherWindMetersPerSecond);
    m_cloudParameters.weatherMapOffset = { 0.0f, 0.0f };

    m_cloudDomainParameters.cloudBottomAltitude = bottom;
    m_cloudDomainParameters.cloudLayerThickness =
        static_cast<float>(scaled.verticalSizeMeters);
    m_cloudDomainParameters.maxViewTraceDistance =
        static_cast<float>(scaled.maxViewTraceDistanceMeters);
    m_cloudDomainParameters.viewTraceFadeStartDistance =
        static_cast<float>(scaled.viewFadeStartDistanceMeters);
    m_cloudDomainParameters.maxLightTraceDistance =
        static_cast<float>(scaled.maxLightTraceDistanceMeters);
    m_cloudDomainParameters = SanitizeCloudDomainParameters(
        m_cloudDomainParameters);

    m_lightParameters.maxLightSteps = scaled.maxLightSteps;
    m_lightParameters.lightStepSize =
        static_cast<float>(scaled.lightStepMeters);
    m_lightParameters.lightRayBias =
        static_cast<float>(scaled.lightRayBiasMeters);
    m_lightParameters.singleScatteringAlbedo =
        static_cast<float>(scaled.singleScatteringAlbedo);
    m_lightParameters = stage6light::Sanitize(m_lightParameters);
    m_cloudLodParameters.detailLodEnabled = 0u;

    m_noiseVolumeParameters.noiseSource =
        static_cast<std::uint32_t>(NoiseSource::ProceduralLegacy);
    m_cloudShapeParameters.shapeMode =
        static_cast<std::uint32_t>(CloudShapeMode::LegacyNormalizedLayer);
    if (!ApplyStage5WeatherPreset(Stage5WeatherPreset::UniformLegacy))
        return false;
    return true;
}

bool Renderer::SetCloudTypeMode(CloudTypeMode type)
{
    const CloudTypeMode safe = static_cast<std::uint32_t>(type) <=
        static_cast<std::uint32_t>(CloudTypeMode::WeatherMap)
        ? type : CloudTypeMode::Mixed;
    WeatherMapGeneratorSettings settings = m_weatherGeneratorSettings;
    settings.cloudTypeMode = safe;
    if (!UpdateWeatherMapTexture(m_weatherPreset, settings))
        return false;
    m_cloudTypeMode = safe;
    m_openWorldPipelinePreset = OpenWorldPipelinePreset::Custom;
    ResetTemporalHistory(Stage11HistoryResetReason::ParametersChanged);
    return true;
}

bool Renderer::ApplyStage13OpenWorldPreset()
{
    const stage13openworld::Parameters value;
    m_weatherGeneratorSettings = WeatherMapGeneratorSettings{};

    const float half = static_cast<float>(value.previewHalfSizeMeters);
    const float bottom = static_cast<float>(value.layerBottomMeters);
    const float top = bottom + static_cast<float>(value.layerThicknessMeters);
    m_cloudParameters.cloudBoundsMin = { -half, bottom, -half };
    m_cloudParameters.cloudBoundsMax = { half, top, half };
    m_cloudParameters.densityMultiplier =
        static_cast<float>(value.densityMultiplier);
    m_cloudParameters.stepSize = static_cast<float>(value.viewStepMeters);
    m_cloudParameters.maxViewSteps = value.maxViewSteps;
    m_cloudParameters.extinctionCoefficient =
        static_cast<float>(value.extinctionPerMeter);
    m_cloudParameters.transmittanceThreshold = 0.01f;
    m_cloudParameters.baseNoiseScale =
        static_cast<float>(value.baseNoiseCyclesPerMeter);
    m_cloudParameters.coverage = static_cast<float>(value.coverage);
    m_cloudParameters.windDirection = { 0.9701425f, 0.0f, 0.2425356f };
    m_cloudParameters.windSpeed =
        static_cast<float>(value.baseWindMetersPerSecond);
    m_cloudParameters.noiseOffset = 0.0f;
    m_cloudParameters.bottomFadeEnd = static_cast<float>(value.bottomFadeEnd);
    m_cloudParameters.topFadeStart = static_cast<float>(value.topFadeStart);
    // 13-4B Open World는 b7 CloudShapeCB를 사용한다. b1의 세 필드는
    // Similarity/구형 회귀 전용 legacy 값으로만 유지한다.
    m_cloudParameters.minimumLocalThicknessFraction = 0.40f;
    m_cloudParameters.localHeightVariation = 0.0f;
    m_cloudParameters.cumulusTopBoost = 0.35f;
    m_cloudParameters.detailNoiseScale =
        static_cast<float>(value.detailNoiseCyclesPerMeter);
    m_cloudParameters.detailErosionStrength =
        static_cast<float>(value.detailErosionStrength);
    m_cloudParameters.detailWindSpeed =
        static_cast<float>(value.detailWindMetersPerSecond);
    m_cloudParameters.detailNoiseOffset = 17.3f;
    m_cloudParameters.weatherMapWorldSize =
        static_cast<float>(value.weatherWorldSizeMeters);
    m_cloudParameters.weatherMapWindSpeed =
        static_cast<float>(value.weatherWindMetersPerSecond);
    m_cloudParameters.weatherMapOffset = { 0.0f, 0.0f };

    m_cloudDomainParameters.domainType =
        static_cast<std::uint32_t>(CloudDomainType::PlanarLayer);
    m_cloudDomainParameters.cloudBottomAltitude = bottom;
    m_cloudDomainParameters.cloudLayerThickness =
        static_cast<float>(value.layerThicknessMeters);
    m_cloudDomainParameters.maxViewTraceDistance =
        static_cast<float>(value.maxViewTraceMeters);
    m_cloudDomainParameters.viewTraceFadeStartDistance =
        static_cast<float>(value.viewFadeStartMeters);
    m_cloudDomainParameters.maxLightTraceDistance =
        static_cast<float>(value.maxLightTraceMeters);
    m_cloudDomainParameters = SanitizeCloudDomainParameters(
        m_cloudDomainParameters);

    m_lightParameters.maxLightSteps = value.maxLightSteps;
    m_lightParameters.lightStepSize =
        static_cast<float>(value.lightStepMeters);
    m_lightParameters.lightRayBias =
        static_cast<float>(value.lightRayBiasMeters);
    m_lightParameters.singleScatteringAlbedo =
        static_cast<float>(value.singleScatteringAlbedo);
    m_lightParameters = stage6light::Sanitize(m_lightParameters);
    m_cloudLodParameters.detailLodEnabled = 1u;
    m_cloudLodParameters.detailLodStartMeters = 32000.0f;
    m_cloudLodParameters.detailLodEndMeters = 48000.0f;

    m_noiseVolumeParameters = NoiseVolumeParameters{};
    m_noiseVolumeParameters.noiseSource =
        static_cast<std::uint32_t>(NoiseSource::Texture3D);
    m_cloudShapeParameters = CloudShapeParameters{};
    m_cloudShapeParameters.shapeMode =
        static_cast<std::uint32_t>(CloudShapeMode::WeatherPhysicalThickness);
    const CloudAppearanceSettings denseMixed = DenseMixedAppearance();
    ApplyCloudAppearance(denseMixed, m_cloudParameters,
                         m_cloudShapeParameters, m_weatherGeneratorSettings);
    m_cloudShapeParameters = SanitizeCloudShapeParameters(
        m_cloudShapeParameters);
    if (!UpdateWeatherMapTexture(Stage5WeatherPreset::PeriodicPerlin,
                                 m_weatherGeneratorSettings))
        return false;
    m_noiseLab.SynchronizeWeatherGeneratorSettings(m_weatherGeneratorSettings);
    m_cloudTypeMode = CloudTypeMode::WeatherMap;
    m_cameraMoveSpeedMetersPerSecond =
        stage13scene::kMoveSpeedMetersPerSecond;
    m_cloudAppearancePreset = CloudAppearancePreset::DenseMixedDefault;
    m_cloudAppearanceDirty = false;
    m_cloudAppearanceStatus =
        "Deterministic Dense Mixed default applied (saved Custom not auto-applied)";
    m_pipelineComparisonActive = false;
    m_openWorldPipelinePreset = OpenWorldPipelinePreset::FullOpenWorld;
    // 2026-08-19 승인한 단계 9의 가장 싼 화질·성능 합격 후보를 기본으로 쓴다.
    ApplyStage9OptimizationPreset(Stage9OptimizationPreset::Balanced);
    return true;
}

CloudAppearanceSettings Renderer::CaptureCurrentCloudAppearance() const
{
    return CaptureCloudAppearance(m_cloudParameters, m_cloudShapeParameters,
                                  m_weatherGeneratorSettings);
}

bool Renderer::ApplyCloudAppearanceSettings(
    const CloudAppearanceSettings& settings, CloudAppearancePreset preset)
{
    if (!IsValidCloudAppearanceSettings(settings))
    {
        m_cloudAppearanceStatus =
            "Appearance rejected: invalid or out-of-range value";
        return false;
    }
    CloudParameters cloud = m_cloudParameters;
    CloudShapeParameters shape = m_cloudShapeParameters;
    WeatherMapGeneratorSettings weather = m_weatherGeneratorSettings;
    ApplyCloudAppearance(settings, cloud, shape, weather);
    shape.shapeMode = static_cast<std::uint32_t>(
        CloudShapeMode::WeatherPhysicalThickness);
    shape = SanitizeCloudShapeParameters(shape);
    weather = SanitizeWeatherMapGeneratorSettings(weather);
    if (!UpdateWeatherMapTexture(Stage5WeatherPreset::PeriodicPerlin, weather))
    {
        m_cloudAppearanceStatus =
            "Appearance Weather update failed; current renderer values retained";
        return false;
    }
    m_cloudParameters = cloud;
    m_cloudShapeParameters = shape;
    m_cloudTypeMode = weather.cloudTypeMode;
    m_noiseLab.SynchronizeWeatherGeneratorSettings(weather);
    m_cloudAppearancePreset = preset;
    m_cloudAppearanceDirty = preset == CloudAppearancePreset::CustomUnsaved;
    m_pipelineComparisonActive = false;
    m_openWorldPipelinePreset = OpenWorldPipelinePreset::Custom;
    ResetTemporalHistory(Stage11HistoryResetReason::ParametersChanged);
    return true;
}

bool Renderer::ApplyCloudAppearancePreset(CloudAppearancePreset preset)
{
    CloudAppearanceSettings settings;
    switch (preset)
    {
    case CloudAppearancePreset::DenseMixedDefault:
        settings = DenseMixedAppearance();
        break;
    case CloudAppearancePreset::Stratus:
        settings = StratusAppearance();
        break;
    case CloudAppearancePreset::Cumulus:
        settings = CumulusAppearance();
        break;
    case CloudAppearancePreset::Custom:
        if (!m_hasSavedCustomAppearance)
        {
            const bool fallbackApplied = ApplyCloudAppearanceSettings(
                DenseMixedAppearance(),
                CloudAppearancePreset::DenseMixedDefault);
            m_cloudAppearanceStatus =
                "No saved Custom appearance; Dense Mixed default applied";
            return fallbackApplied;
        }
        settings = m_savedCustomAppearance;
        break;
    case CloudAppearancePreset::CustomUnsaved:
    default:
        return false;
    }
    if (!ApplyCloudAppearanceSettings(settings, preset))
        return false;
    m_cloudAppearanceStatus = std::string(CloudAppearancePresetName(preset)) +
        " appearance applied; camera, weather placement, wind, lighting, and sampling preserved";
    return true;
}

void Renderer::MarkCloudAppearanceDirty()
{
    m_cloudAppearancePreset = CloudAppearancePreset::CustomUnsaved;
    m_cloudAppearanceDirty = true;
    m_pipelineComparisonActive = false;
    m_openWorldPipelinePreset = OpenWorldPipelinePreset::Custom;
    m_cloudAppearanceStatus =
        "Unsaved changes: Save Current as Custom to persist them";
}

bool Renderer::SaveCurrentCloudAppearance()
{
    const CloudAppearanceSettings current = CaptureCurrentCloudAppearance();
    std::string status;
    if (!SaveCustomCloudAppearanceAtomic(
            m_customAppearancePath, current, status))
    {
        m_cloudAppearanceStatus = status;
        return false;
    }
    m_savedCustomAppearance = current;
    m_hasSavedCustomAppearance = true;
    m_cloudAppearancePreset = CloudAppearancePreset::Custom;
    m_cloudAppearanceDirty = false;
    m_cloudAppearanceStatus = status;
    return true;
}

bool Renderer::ApplyOpenWorldPipelinePreset(OpenWorldPipelinePreset preset)
{
    if (preset == OpenWorldPipelinePreset::Custom)
        return false;

    const DirectX::XMFLOAT3 boundsMin = m_cloudParameters.cloudBoundsMin;
    const DirectX::XMFLOAT3 boundsMax = m_cloudParameters.cloudBoundsMax;
    const CloudDomainParameters domain = m_cloudDomainParameters;
    const float moveSpeed = m_cameraMoveSpeedMetersPerSecond;

    bool succeeded = false;
    if (preset == OpenWorldPipelinePreset::FullOpenWorld)
    {
        succeeded = ApplyStage13OpenWorldPreset();
    }
    else
    {
        succeeded = ApplyStage13SimilarityScale(1000.0f);
        if (succeeded)
        {
            m_cloudTypeMode = CloudTypeMode::Mixed;
            if (preset >= OpenWorldPipelinePreset::Texture3D)
            {
                m_noiseVolumeParameters = NoiseVolumeParameters{};
                m_noiseVolumeParameters.noiseSource =
                    static_cast<std::uint32_t>(NoiseSource::Texture3D);
            }
            if (preset >= OpenWorldPipelinePreset::PeriodicWeather)
            {
                const stage13openworld::Parameters openWorld;
                WeatherMapGeneratorSettings defaultWeatherSettings;
                CloudParameters unusedCloud;
                CloudShapeParameters unusedShape;
                ApplyCloudAppearance(DenseMixedAppearance(), unusedCloud,
                                     unusedShape, defaultWeatherSettings);
                succeeded = UpdateWeatherMapTexture(
                    Stage5WeatherPreset::PeriodicPerlin,
                    defaultWeatherSettings);
                if (succeeded)
                {
                    m_noiseLab.SynchronizeWeatherGeneratorSettings(
                        m_weatherGeneratorSettings);
                    m_cloudParameters.weatherMapWorldSize =
                        static_cast<float>(openWorld.weatherWorldSizeMeters);
                    m_cloudParameters.weatherMapWindSpeed =
                        static_cast<float>(openWorld.weatherWindMetersPerSecond);
                    m_cloudParameters.weatherMapOffset = { 0.0f, 0.0f };
                    m_cloudTypeMode = CloudTypeMode::WeatherMap;
                }
            }
            if (succeeded && preset >= OpenWorldPipelinePreset::PhysicalShape)
            {
                m_cloudShapeParameters = CloudShapeParameters{};
                m_cloudShapeParameters.shapeMode = static_cast<std::uint32_t>(
                    CloudShapeMode::WeatherPhysicalThickness);
                CloudParameters unusedCloud;
                WeatherMapGeneratorSettings unusedWeather;
                ApplyCloudAppearance(DenseMixedAppearance(), unusedCloud,
                                     m_cloudShapeParameters, unusedWeather);
            }
        }
    }

    m_cloudParameters.cloudBoundsMin = boundsMin;
    m_cloudParameters.cloudBoundsMax = boundsMax;
    m_cloudDomainParameters = domain;
    m_cameraMoveSpeedMetersPerSecond = moveSpeed;
    if (!succeeded)
        return false;

    m_openWorldPipelinePreset = preset;
    return true;
}

bool Renderer::ApplyWeatherGeneratorSettings(
    const WeatherMapGeneratorSettings& settings)
{
    const WeatherMapGeneratorSettings safeSettings =
        SanitizeWeatherMapGeneratorSettings(settings);
    const bool changed = !WeatherMapGeneratorSettingsEqual(
        safeSettings, m_weatherGeneratorSettings);
    if (!UpdateWeatherMapTexture(m_weatherPreset, safeSettings))
        return false;
    if (changed)
    {
        m_openWorldPipelinePreset = OpenWorldPipelinePreset::Custom;
        ResetTemporalHistory(Stage11HistoryResetReason::ParametersChanged);
    }
    return true;
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
    {
        ResetTemporalHistory(Stage11HistoryResetReason::ShaderReload);
        m_shaderStatus = "Reloaded: " + changed.str() + " @ " + CurrentLocalTimeText();
    }
    else
        m_shaderStatus = "Reload failed: " + changed.str();
    m_shaderWriteTimes = currentTimes;
}

bool Renderer::HandleWindowMessage(
    HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    return m_noiseLab.HandleWindowMessage(hwnd, message, wParam, lParam);
}

bool Renderer::DeveloperUiWantsKeyboard() const
{
    return m_noiseLab.WantsKeyboardCapture();
}

bool Renderer::ValidateNoiseLabPreviews()
{
    return m_noiseLab.ValidatePreviewData();
}

bool Renderer::ExportNoiseLabSnapshot(const std::filesystem::path& root)
{
    return m_noiseLab.ExportSnapshot(
        root, m_cloudParameters, m_cloudShapeParameters, m_cloudDomainParameters,
        m_cloudLodParameters,
        m_optimizationParameters, m_optimizationPreset,
        m_upsamplingParameters, m_resolutionPreset,
        m_temporalParameters,
        m_shadowParameters,
        m_cloudRenderWidth, m_cloudRenderHeight,
        m_lightParameters, m_sunPreset, m_phasePreset,
        m_environmentParameters, m_environmentPreset, m_weatherPreset,
        m_cloudTypeMode,
        m_cloudAppearancePreset, m_cloudAppearanceDirty,
        m_hasSavedCustomAppearance, m_savedCustomAppearance,
        m_noiseVolumeParameters, m_baseNoiseVolumeHash,
        m_detailNoiseVolumeHash,
        m_weatherGeneratorSettings, m_weatherMapHash, m_weatherMapTexture.Get(),
        std::filesystem::path(m_shaderDir) / L"Noise.hlsli");
}

std::uint64_t Renderer::NoiseLabPreviewHash(std::size_t targetIndex)
{
    return m_noiseLab.PreviewHash(targetIndex);
}
