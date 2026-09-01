#include "Renderer.h"
#include "Camera.h"
#include "CloudShapeDomainContract.h"
#include "HighCloudQuality.h"
#include "Stage13CameraPresets.h"
#include "Stage13SceneMath.h"
#include "Stage14AtmosphereMath.h"

#include <d3dcompiler.h>
#include <dxgi1_5.h>
#include <bcrypt.h>
#include <SetupAPI.h>
#include <devguid.h>
#include <DirectXPackedVector.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string_view>
#include <vector>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

static_assert(presentation::kAllowTearingPresentFlag ==
              DXGI_PRESENT_ALLOW_TEARING);

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

void HashBytes(std::uint64_t& hash, const void* data, std::size_t size)
{
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t index = 0; index < size; ++index)
    {
        hash ^= bytes[index];
        hash *= 1099511628211ull;
    }
}

template <typename T>
void HashValue(std::uint64_t& hash, const T& value)
{
    HashBytes(hash, &value, sizeof(value));
}

std::uint64_t BeginStage14Hash()
{
    return 1469598103934665603ull;
}

void AppendCacheBytes(std::vector<std::uint8_t>& bytes,
                      const void* data, std::size_t size)
{
    const std::uint64_t length = static_cast<std::uint64_t>(size);
    const auto* lengthBytes = reinterpret_cast<const std::uint8_t*>(&length);
    bytes.insert(bytes.end(), lengthBytes, lengthBytes + sizeof(length));
    const auto* source = static_cast<const std::uint8_t*>(data);
    bytes.insert(bytes.end(), source, source + size);
}

void AppendCacheText(std::vector<std::uint8_t>& bytes,
                     const std::string& text)
{
    AppendCacheBytes(bytes, text.data(), text.size());
}

using ShaderDependencySources =
    std::map<std::string, std::vector<char>>;

bool IsShaderPathInsideRoot(
    const std::filesystem::path& shaderRoot,
    const std::filesystem::path& shaderPath,
    std::string& relativePath)
{
    std::error_code error;
    const std::filesystem::path relative =
        std::filesystem::relative(shaderPath, shaderRoot, error);
    if (error || relative.empty() || relative.is_absolute())
        return false;
    for (const std::filesystem::path& component : relative)
    {
        if (component == L"..")
            return false;
    }
    relativePath = relative.generic_string();
    return !relativePath.empty();
}

bool ParseLiteralShaderIncludes(
    const std::vector<char>& contents,
    std::vector<std::string>& includePaths)
{
    const std::string source(contents.begin(), contents.end());
    std::size_t lineStart = 0;
    while (lineStart < source.size())
    {
        std::size_t lineEnd = source.find('\n', lineStart);
        if (lineEnd == std::string::npos)
            lineEnd = source.size();

        std::size_t cursor = lineStart;
        if (lineStart == 0 && lineEnd >= 3 &&
            static_cast<unsigned char>(source[0]) == 0xefu &&
            static_cast<unsigned char>(source[1]) == 0xbbu &&
            static_cast<unsigned char>(source[2]) == 0xbfu)
            cursor = 3;
        while (cursor < lineEnd &&
               (source[cursor] == ' ' || source[cursor] == '\t' ||
                source[cursor] == '\v' || source[cursor] == '\f' ||
                source[cursor] == '\r'))
            ++cursor;
        if (cursor >= lineEnd || source[cursor] != '#')
        {
            lineStart = lineEnd + (lineEnd < source.size() ? 1u : 0u);
            continue;
        }

        ++cursor;
        while (cursor < lineEnd &&
               (source[cursor] == ' ' || source[cursor] == '\t'))
            ++cursor;
        constexpr std::string_view includeDirective = "include";
        if (cursor + includeDirective.size() > lineEnd ||
            source.compare(cursor, includeDirective.size(),
                           includeDirective) != 0)
        {
            lineStart = lineEnd + (lineEnd < source.size() ? 1u : 0u);
            continue;
        }
        cursor += includeDirective.size();
        if (cursor < lineEnd &&
            ((source[cursor] >= 'a' && source[cursor] <= 'z') ||
             (source[cursor] >= 'A' && source[cursor] <= 'Z') ||
             (source[cursor] >= '0' && source[cursor] <= '9') ||
             source[cursor] == '_'))
        {
            lineStart = lineEnd + (lineEnd < source.size() ? 1u : 0u);
            continue;
        }
        while (cursor < lineEnd &&
               (source[cursor] == ' ' || source[cursor] == '\t'))
            ++cursor;

        // 현재 shader 계약은 literal local include만 사용한다. macro include나
        // 잘못 닫힌 경로는 cache를 포기해 stale bytecode 재사용을 막는다.
        if (cursor >= lineEnd ||
            (source[cursor] != '"' && source[cursor] != '<'))
            return false;
        const char terminator = source[cursor] == '"' ? '"' : '>';
        const std::size_t pathStart = ++cursor;
        const std::size_t pathEnd = source.find(terminator, pathStart);
        if (pathEnd == std::string::npos || pathEnd >= lineEnd ||
            pathEnd == pathStart)
            return false;
        includePaths.emplace_back(source.substr(pathStart, pathEnd - pathStart));
        lineStart = lineEnd + (lineEnd < source.size() ? 1u : 0u);
    }
    return true;
}

bool ResolveShaderInclude(
    const std::filesystem::path& shaderRoot,
    const std::filesystem::path& includingFile,
    const std::string& includeText,
    std::filesystem::path& resolvedPath)
{
    const std::filesystem::path includePath =
        std::filesystem::u8path(includeText);
    const std::filesystem::path candidates[] = {
        includingFile.parent_path() / includePath,
        shaderRoot / includePath,
    };

    std::filesystem::path selected;
    for (const std::filesystem::path& candidate : candidates)
    {
        std::error_code error;
        const std::filesystem::path canonical =
            std::filesystem::canonical(candidate, error);
        if (error)
            continue;
        if (!std::filesystem::is_regular_file(canonical, error) || error)
            return false;

        std::string relative;
        if (!IsShaderPathInsideRoot(shaderRoot, canonical, relative))
            return false;
        if (selected.empty())
        {
            selected = canonical;
            continue;
        }
        if (!std::filesystem::equivalent(selected, canonical, error) || error)
        {
            // parent-relative와 root-relative가 서로 다른 파일을 가리키면
            // 표준 include handler의 선택을 추측하지 않고 cache를 비활성화한다.
            return false;
        }
    }
    if (selected.empty())
        return false;
    resolvedPath = selected;
    return true;
}

bool CollectShaderDependencies(
    const std::filesystem::path& shaderRoot,
    const std::filesystem::path& shaderPath,
    ShaderDependencySources& sources)
{
    std::error_code error;
    const std::filesystem::path canonicalPath =
        std::filesystem::canonical(shaderPath, error);
    if (error)
        return false;

    std::string relativePath;
    if (!IsShaderPathInsideRoot(shaderRoot, canonicalPath, relativePath))
        return false;
    if (sources.find(relativePath) != sources.end())
        return true;

    std::ifstream source(canonicalPath, std::ios::binary);
    if (!source)
        return false;
    std::vector<char> contents(
        (std::istreambuf_iterator<char>(source)),
        std::istreambuf_iterator<char>());
    sources.emplace(relativePath, contents);

    std::vector<std::string> includePaths;
    if (!ParseLiteralShaderIncludes(contents, includePaths))
        return false;
    for (const std::string& includePath : includePaths)
    {
        std::filesystem::path dependency;
        if (!ResolveShaderInclude(
                shaderRoot, canonicalPath, includePath, dependency) ||
            !CollectShaderDependencies(shaderRoot, dependency, sources))
            return false;
    }
    return true;
}

std::string BuildShaderCacheKey(
    const std::filesystem::path& shaderRoot,
    const std::filesystem::path& shaderPath,
    const char* entryPoint, const char* target, UINT compileFlags,
    const D3D_SHADER_MACRO* defines)
{
    std::vector<std::uint8_t> input;
    AppendCacheText(input, "VolumetricCloudShaderCache-v2-dependency-closure");
    AppendCacheText(input, shaderPath.filename().string());
    AppendCacheText(input, entryPoint ? entryPoint : "");
    AppendCacheText(input, target ? target : "");
    AppendCacheBytes(input, &compileFlags, sizeof(compileFlags));
    if (defines)
    {
        for (const D3D_SHADER_MACRO* define = defines; define->Name; ++define)
        {
            AppendCacheText(input, define->Name);
            AppendCacheText(input, define->Definition
                ? define->Definition : "");
        }
    }

    std::error_code error;
    const std::filesystem::path canonicalRoot =
        std::filesystem::canonical(shaderRoot, error);
    ShaderDependencySources sources;
    if (error ||
        !CollectShaderDependencies(canonicalRoot, shaderPath, sources) ||
        sources.empty())
        return {};
    for (const auto& [relativePath, contents] : sources)
    {
        AppendCacheText(input, relativePath);
        AppendCacheBytes(input, contents.data(), contents.size());
    }

    wchar_t compilerPath[MAX_PATH] = {};
    HMODULE compilerModule = GetModuleHandleW(L"d3dcompiler_47.dll");
    if (compilerModule && GetModuleFileNameW(
            compilerModule, compilerPath,
            static_cast<DWORD>(std::size(compilerPath))) > 0)
    {
        const std::filesystem::path compiler(compilerPath);
        AppendCacheText(input, compiler.filename().string());
        const std::uintmax_t size = std::filesystem::file_size(compiler, error);
        if (!error)
            AppendCacheBytes(input, &size, sizeof(size));
        error.clear();
        const auto writeTime = std::filesystem::last_write_time(compiler, error);
        if (!error)
        {
            const auto ticks = writeTime.time_since_epoch().count();
            AppendCacheBytes(input, &ticks, sizeof(ticks));
        }
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectSize = 0;
    DWORD hashSize = 0;
    DWORD returned = 0;
    if (BCryptOpenAlgorithmProvider(
            &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize),
            &returned, 0) < 0 ||
        BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&hashSize), sizeof(hashSize),
            &returned, 0) < 0 || hashSize != 32u)
    {
        if (algorithm)
            BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }
    std::vector<std::uint8_t> object(objectSize);
    std::array<std::uint8_t, 32> digest = {};
    const bool succeeded = BCryptCreateHash(
            algorithm, &hash, object.data(), objectSize,
            nullptr, 0, 0) >= 0 &&
        BCryptHashData(hash, input.data(),
            static_cast<ULONG>(input.size()), 0) >= 0 &&
        BCryptFinishHash(hash, digest.data(),
            static_cast<ULONG>(digest.size()), 0) >= 0;
    if (hash)
        BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!succeeded)
        return {};
    static constexpr char hex[] = "0123456789abcdef";
    std::string result(digest.size() * 2u, '0');
    for (std::size_t index = 0; index < digest.size(); ++index)
    {
        result[index * 2u] = hex[digest[index] >> 4u];
        result[index * 2u + 1u] = hex[digest[index] & 0x0fu];
    }
    return result;
}

bool HasReflectedConstantBufferSize(
    ID3DBlob* blob, const char* name, UINT expectedSize)
{
    if (!blob || !name)
        return false;
    ComPtr<ID3D11ShaderReflection> reflection;
    if (FAILED(D3DReflect(blob->GetBufferPointer(), blob->GetBufferSize(),
                         __uuidof(ID3D11ShaderReflection), &reflection)))
        return false;
    ID3D11ShaderReflectionConstantBuffer* buffer =
        reflection->GetConstantBufferByName(name);
    D3D11_SHADER_BUFFER_DESC description = {};
    return buffer && SUCCEEDED(buffer->GetDesc(&description)) &&
        description.Size == expectedSize;
}

bool HasReflectedConstantBufferContract(
    ID3DBlob* blob, const char* name, UINT expectedSize, UINT expectedSlot)
{
    if (!HasReflectedConstantBufferSize(blob, name, expectedSize))
        return false;
    ComPtr<ID3D11ShaderReflection> reflection;
    if (FAILED(D3DReflect(blob->GetBufferPointer(), blob->GetBufferSize(),
                         __uuidof(ID3D11ShaderReflection), &reflection)))
        return false;
    D3D11_SHADER_INPUT_BIND_DESC binding = {};
    return SUCCEEDED(reflection->GetResourceBindingDescByName(
               name, &binding)) &&
        binding.Type == D3D_SIT_CBUFFER && binding.BindPoint == expectedSlot;
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
    // 같은 모서리를 공유하면 normal이 보간되므로 면마다 네 정점을 둔다.
    const std::uint32_t facePositions[24] = {
        0, 1, 2, 3, 4, 5, 6, 7,
        0, 1, 5, 4, 3, 7, 6, 2,
        0, 4, 7, 3, 1, 2, 6, 5,
    };
    const XMFLOAT3 faceNormals[6] = {
        { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f, 1.0f },
        { 0.0f, -1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f },
        { -1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f },
    };
    for (std::uint32_t face = 0; face < 6; ++face)
    {
        for (std::uint32_t corner = 0; corner < 4; ++corner)
        {
            vertices.push_back({
                positions[facePositions[face * 4u + corner]], color,
                faceNormals[face], 1u
            });
        }
    }
    const std::uint32_t faceIndices[36] = {
         0,  2,  1,  0,  3,  2,
         4,  5,  6,  4,  6,  7,
         8,  9, 10,  8, 10, 11,
        12, 13, 14, 12, 14, 15,
        16, 17, 18, 16, 18, 19,
        20, 21, 22, 20, 22, 23,
    };
    for (const std::uint32_t index : faceIndices)
        indices.push_back(base + index);
}

void AppendGroundPlane(std::vector<DiagnosticSceneVertex>& vertices,
                       std::vector<std::uint32_t>& indices)
{
    const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
    constexpr float h = stage13scene::kGroundHalfSizeMeters;
    constexpr XMFLOAT3 color = { 0.50f, 0.50f, 0.50f };
    constexpr XMFLOAT3 normal = { 0.0f, 1.0f, 0.0f };
    vertices.push_back({ { -h, 0.0f, -h }, color, normal, 0u });
    vertices.push_back({ {  h, 0.0f, -h }, color, normal, 0u });
    vertices.push_back({ {  h, 0.0f,  h }, color, normal, 0u });
    vertices.push_back({ { -h, 0.0f,  h }, color, normal, 0u });
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

bool Renderer::Init(HWND hwnd, int width, int height,
                    bool enableNoiseVolumes,
                    bool showInitializationErrors)
{
    m_hwnd = hwnd;
    m_width = width;
    m_height = height;
    m_noiseVolumesEnabled = enableNoiseVolumes;
    m_shaderDir = ResolveShaderDir();
    m_fullscreenShaderPath = m_shaderDir + L"Fullscreen.hlsl";
    m_cloudShaderPath = m_shaderDir + L"VolumetricClouds.hlsl";
    m_noiseLabShaderPath = m_shaderDir + L"NoiseLab.hlsl";
    m_sceneShaderPath = m_shaderDir + L"DiagnosticScene.hlsl";
    m_noiseVolumeShaderPath = m_shaderDir + L"NoiseVolume.hlsl";
    m_deepShadowShaderPath = m_shaderDir + L"CloudDeepShadow.hlsl";
    m_atmosphereLutShaderPath = m_shaderDir + L"Stage14AtmosphereLut.hlsl";
    m_toneMapShaderPath = m_shaderDir + L"Stage14ToneMap.hlsl";
    // Physical 모드에서 기존 색·세기는 대기 결과의 예술적 tint/multiplier다.
    m_lightParameters.sunColor = { 1.0f, 1.0f, 1.0f };
    m_lightParameters.sunIntensity = 1.0f;
    std::filesystem::path shaderDirectory(m_shaderDir);
    if (shaderDirectory.filename().empty())
        shaderDirectory = shaderDirectory.parent_path();
    m_cloudFormationPresetRoot = shaderDirectory.parent_path() / L"captures" /
        L"noise-lab";
    const std::filesystem::path developerUiSettingsPath =
        shaderDirectory.parent_path() / L"captures" /
        L"noise-lab" / L"developer-ui.json";
    RefreshSavedCustomFormationState();

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

    ComPtr<IDXGIFactory1> factory;
    ComPtr<IDXGIFactory5> factory5;
    BOOL allowTearing = FALSE;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory.As(&factory5)) &&
        SUCCEEDED(factory5->CheckFeatureSupport(
            DXGI_FEATURE_PRESENT_ALLOW_TEARING,
            &allowTearing, sizeof(allowTearing))))
    {
        m_tearingSupported = allowTearing == TRUE;
    }
    if (m_tearingSupported)
        swapChainDesc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

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
        if (showInitializationErrors)
        {
            MessageBoxW(hwnd, L"D3D11 디바이스/스왑체인 생성 실패",
                        L"오류", MB_OK | MB_ICONERROR);
        }
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
        !CreateConstantBuffers() ||
        !CreateShaders(showInitializationErrors) ||
        !CreateDiagnosticScene() || !CreatePipelineStates() ||
        !CreateWeatherMapTexture(m_weatherPreset) ||
        !m_noiseLab.Init(
            hwnd, m_device.Get(), m_context.Get(), developerUiSettingsPath))
    {
        if (showInitializationErrors)
        {
            MessageBoxW(hwnd, L"High 렌더링 리소스 생성 실패",
                        L"오류", MB_OK | MB_ICONERROR);
        }
        return false;
    }
    m_sizeDependentResourcesValid = true;
    D3D11_VIEWPORT initialViewport = {};
    initialViewport.Width = static_cast<float>(m_width);
    initialViewport.Height = static_cast<float>(m_height);
    initialViewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &initialViewport);

    // Balanced512 Deep Cache는 화면 크기와 독립적인 고정 자원이다.
    CreateDeepShadowResources();
    if (!InitializeStage15Presets())
        return false;

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

    if (FAILED(m_device->CreateTexture2D(
            &colorDesc, nullptr, &m_hdrCloud)) ||
        FAILED(m_device->CreateRenderTargetView(
            m_hdrCloud.Get(), nullptr, &m_hdrCloudRtv)) ||
        FAILED(m_device->CreateShaderResourceView(
            m_hdrCloud.Get(), nullptr, &m_hdrCloudSrv)))
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
    if (FAILED(m_device->CreateShaderResourceView(
            m_sceneDepth.Get(), &srvDesc, &m_sceneDepthSrv)))
        return false;
    return true;
}

bool Renderer::CreateDeepShadowResources()
{
    if (!m_device)
        return false;
    const UINT resolution = stage12shadow::kResolution;
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
    m_shadowParameters = stage12shadow::Sanitize(m_shadowParameters);
    return true;
}

bool Renderer::CompileShaderFromFile(const std::wstring& path,
                                     const char* entryPoint,
                                     const char* target,
                                     ComPtr<ID3DBlob>& outBlob,
                                     bool showErrors,
                                     const D3D_SHADER_MACRO* defines)
{
    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    compileFlags |= D3DCOMPILE_DEBUG;
    // High cloud와 compute 경로는 /Od에서 하드웨어 instruction 한도를 넘길 수
    // 있으므로 Debug에서도 최소 O1을 사용한다.
    compileFlags |= (std::strcmp(entryPoint, "main") == 0 &&
                     (path == m_cloudShaderPath ||
                      path == m_deepShadowShaderPath ||
                      path == m_atmosphereLutShaderPath))
        ? D3DCOMPILE_OPTIMIZATION_LEVEL1
        : D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    const std::filesystem::path shaderRoot(m_shaderDir);
    const std::string cacheKey = BuildShaderCacheKey(
        shaderRoot, std::filesystem::path(path), entryPoint, target,
        compileFlags, defines);
    const std::filesystem::path cacheDirectory =
        std::filesystem::path(GetExeDir()) / L"shader-cache";
    const std::filesystem::path cachePath = cacheKey.empty()
        ? std::filesystem::path{}
        : cacheDirectory / (std::filesystem::path(cacheKey).wstring() + L".cso");
    if (!cachePath.empty() &&
        SUCCEEDED(D3DReadFileToBlob(cachePath.c_str(), &outBlob)) && outBlob)
    {
        ++m_shaderCacheHitCount;
        return true;
    }

    ComPtr<ID3DBlob> errors;
    ++m_shaderCompileCallCount;
    const HRESULT result = D3DCompileFromFile(
        path.c_str(), defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entryPoint, target, compileFlags, 0, &outBlob, &errors);
    if (SUCCEEDED(result))
    {
        if (!cachePath.empty())
        {
            std::error_code cacheError;
            std::filesystem::create_directories(cacheDirectory, cacheError);
            if (!cacheError)
            {
                const std::filesystem::path temporary = cachePath.wstring() +
                    L".tmp-" + std::to_wstring(GetCurrentProcessId());
                if (SUCCEEDED(D3DWriteBlobToFile(
                        outBlob.Get(), temporary.c_str(), TRUE)))
                    MoveFileExW(temporary.c_str(), cachePath.c_str(),
                                MOVEFILE_REPLACE_EXISTING |
                                MOVEFILE_WRITE_THROUGH);
            }
        }
        return true;
    }

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

bool Renderer::InitializeShaderManifest()
{
    std::vector<shaderreload::Program> candidate =
        shaderreload::MakeHighManifest(m_noiseVolumesEnabled);
    const std::filesystem::path root(m_shaderDir);
    std::string dependencyError;
    for (auto& program : candidate)
    {
        if (!shaderreload::RefreshDependencies(
                root, program, dependencyError))
        {
            m_shaderError = "Shader manifest failed for " + program.name +
                ": " + dependencyError;
            m_shaderStatus = "Shader manifest initialization failed";
            return false;
        }
    }
    m_shaderManifest = std::move(candidate);
    return true;
}

bool Renderer::ReloadShaderPrograms(
    const std::vector<std::size_t>& programIndices,
    const std::vector<std::string>& changedFiles,
    bool showErrors)
{
    using shaderreload::ObjectType;
    using shaderreload::ProgramId;
    const auto started = std::chrono::steady_clock::now();
    const std::uint64_t compileBefore = m_shaderCompileCallCount;
    const std::uint64_t cacheBefore = m_shaderCacheHitCount;
    shaderreload::ReloadReport report;
    report.attempted = true;
    report.changedFiles = changedFiles;
    report.affectedPrograms = programIndices.size();

    const auto finish = [&](bool succeeded)
    {
        report.succeeded = succeeded;
        report.compileCount = m_shaderCompileCallCount - compileBefore;
        report.cacheHits = m_shaderCacheHitCount - cacheBefore;
        report.elapsedMilliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        m_lastShaderReloadReport = report;
        std::ostringstream status;
        status << (succeeded ? "Reload succeeded" : "Reload failed")
               << ": files=" << report.changedFiles.size()
               << " programs=" << report.affectedPrograms
               << " compile=" << report.compileCount
               << " cache=" << report.cacheHits
               << " elapsed=" << std::fixed << std::setprecision(2)
               << report.elapsedMilliseconds << " ms";
        m_shaderStatus = status.str();
        return succeeded;
    };

    if (programIndices.empty())
        return finish(true);

    struct PendingProgram
    {
        std::size_t manifestIndex = 0;
        ComPtr<ID3DBlob> blob;
        ComPtr<ID3D11VertexShader> vertexShader;
        ComPtr<ID3D11PixelShader> pixelShader;
        ComPtr<ID3D11ComputeShader> computeShader;
        ComPtr<ID3D11InputLayout> inputLayout;
    };
    std::vector<PendingProgram> pending;
    pending.reserve(programIndices.size());
    const std::filesystem::path root(m_shaderDir);

    const auto hasContract = [](ID3DBlob* blob, const char* name,
                                UINT size, UINT slot)
    {
        return HasReflectedConstantBufferContract(
            blob, name, size, slot);
    };
    const auto validateContracts = [&](ProgramId id, ID3DBlob* blob)
    {
        switch (id)
        {
        case ProgramId::CloudPs:
            return hasContract(blob, "CloudCB", 80u, 1u) &&
                hasContract(blob, "LightCB", 64u, 3u) &&
                hasContract(blob, "EnvironmentCB", 80u, 4u) &&
                hasContract(blob, "CloudDomainCB", 32u, 5u) &&
                hasContract(blob, "NoiseVolumeCB", 96u, 6u) &&
                hasContract(blob, "CloudShapeCB", 64u, 7u) &&
                hasContract(blob, "ShadowCB", 160u, 8u) &&
                hasContract(blob, "Stage14CB", 224u, 9u);
        case ProgramId::NoiseLabPs:
            return hasContract(blob, "CloudCB", 80u, 1u) &&
                hasContract(blob, "NoiseVolumeCB", 96u, 6u) &&
                hasContract(blob, "CloudShapeCB", 64u, 7u);
        case ProgramId::DeepShadowCs:
            return hasContract(blob, "CloudCB", 80u, 1u) &&
                hasContract(blob, "NoiseVolumeCB", 96u, 6u) &&
                hasContract(blob, "CloudShapeCB", 64u, 7u) &&
                hasContract(blob, "ShadowCB", 160u, 8u);
        case ProgramId::ScenePs:
            return hasContract(blob, "ShadowCB", 160u, 8u) &&
                hasContract(blob, "Stage14CB", 224u, 9u);
        case ProgramId::ToneMapPs:
        case ProgramId::AtmosphereTransmittanceCs:
        case ProgramId::AtmosphereMultiScatteringCs:
        case ProgramId::AtmosphereSkyViewCs:
        case ProgramId::AtmosphereSkyIrradianceCs:
        case ProgramId::AtmosphereAerialCs:
            return hasContract(blob, "Stage14CB", 224u, 9u);
        default:
            return true;
        }
    };

    for (std::size_t manifestIndex : programIndices)
    {
        if (manifestIndex >= m_shaderManifest.size())
        {
            m_shaderError = "Shader manifest index is out of range";
            return finish(false);
        }
        const shaderreload::Program& program =
            m_shaderManifest[manifestIndex];
        std::vector<D3D_SHADER_MACRO> macros;
        macros.reserve(program.defines.size() + 1u);
        for (const auto& define : program.defines)
            macros.push_back({ define.name.c_str(), define.value.c_str() });
        macros.push_back({ nullptr, nullptr });

        PendingProgram item;
        item.manifestIndex = manifestIndex;
        if (!CompileShaderFromFile(
                (root / program.source).wstring(), program.entry.c_str(),
                program.target.c_str(), item.blob, showErrors,
                program.defines.empty() ? nullptr : macros.data()))
            return finish(false);
        if (!validateContracts(program.id, item.blob.Get()))
        {
            m_shaderError = "Shader reflection contract failed for " +
                program.name;
            return finish(false);
        }

        HRESULT objectResult = E_FAIL;
        if (program.object == ObjectType::VertexShader)
            objectResult = m_device->CreateVertexShader(
                item.blob->GetBufferPointer(), item.blob->GetBufferSize(),
                nullptr, &item.vertexShader);
        else if (program.object == ObjectType::PixelShader)
            objectResult = m_device->CreatePixelShader(
                item.blob->GetBufferPointer(), item.blob->GetBufferSize(),
                nullptr, &item.pixelShader);
        else
            objectResult = m_device->CreateComputeShader(
                item.blob->GetBufferPointer(), item.blob->GetBufferSize(),
                nullptr, &item.computeShader);
        if (FAILED(objectResult))
        {
            m_shaderError = "D3D11 object creation failed for " +
                program.name;
            return finish(false);
        }

        if (program.id == ProgramId::SceneVs)
        {
            const D3D11_INPUT_ELEMENT_DESC elements[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
                  D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
                  D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24,
                  D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "MATERIALID", 0, DXGI_FORMAT_R32_UINT, 0, 36,
                  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            };
            if (FAILED(m_device->CreateInputLayout(
                    elements, static_cast<UINT>(std::size(elements)),
                    item.blob->GetBufferPointer(),
                    item.blob->GetBufferSize(), &item.inputLayout)))
            {
                m_shaderError = "Scene input layout creation failed";
                return finish(false);
            }
        }
        pending.push_back(std::move(item));
    }

    std::vector<shaderreload::Program> manifestCandidate = m_shaderManifest;
    std::string dependencyError;
    for (std::size_t manifestIndex : programIndices)
    {
        if (!shaderreload::RefreshDependencies(
                root, manifestCandidate[manifestIndex], dependencyError))
        {
            m_shaderError = "Shader dependency refresh failed for " +
                manifestCandidate[manifestIndex].name + ": " +
                dependencyError;
            return finish(false);
        }
    }

    std::uint32_t invalidation = shaderreload::InvalidateNone;
    for (std::size_t manifestIndex : programIndices)
        invalidation |= m_shaderManifest[manifestIndex].invalidation;

    ComPtr<ID3D11Texture3D> generatedBaseTexture;
    ComPtr<ID3D11ShaderResourceView> generatedBaseSrv;
    ComPtr<ID3D11Texture3D> generatedDetailTexture;
    ComPtr<ID3D11ShaderResourceView> generatedDetailSrv;
    std::uint64_t generatedBaseHash = 0;
    std::uint64_t generatedDetailHash = 0;
    float generatedDetailNeutral = 0.5f;
    double generatedMilliseconds = 0.0;
    if ((invalidation & shaderreload::InvalidateNoiseVolumes) != 0u &&
        m_noiseVolumesEnabled)
    {
        ID3D11ComputeShader* baseShader = m_noiseBaseCs.Get();
        ID3D11ComputeShader* detailShader = m_noiseDetailCs.Get();
        for (const auto& item : pending)
        {
            const ProgramId id =
                m_shaderManifest[item.manifestIndex].id;
            if (id == ProgramId::NoiseBaseCs)
                baseShader = item.computeShader.Get();
            else if (id == ProgramId::NoiseDetailCs)
                detailShader = item.computeShader.Get();
        }
        if (!GenerateNoiseVolumes(
                baseShader, detailShader,
                generatedBaseTexture, generatedBaseSrv,
                generatedDetailTexture, generatedDetailSrv,
                generatedBaseHash, generatedDetailHash,
                generatedDetailNeutral, generatedMilliseconds))
        {
            m_shaderError = "Texture3D noise regeneration failed";
            return finish(false);
        }
    }

    for (const auto& item : pending)
    {
        const ProgramId id = m_shaderManifest[item.manifestIndex].id;
        switch (id)
        {
        case ProgramId::FullscreenVs:
            m_fullscreenVs = item.vertexShader;
            break;
        case ProgramId::CloudPs:
            m_cloudPs = item.pixelShader;
            m_cloudShaderHash = 1469598103934665603ull;
            HashBytes(m_cloudShaderHash, item.blob->GetBufferPointer(),
                      item.blob->GetBufferSize());
            break;
        case ProgramId::NoiseLabPs:
            m_noiseLabPs = item.pixelShader;
            break;
        case ProgramId::SceneVs:
            m_sceneVs = item.vertexShader;
            m_sceneInputLayout = item.inputLayout;
            break;
        case ProgramId::ScenePs:
            m_scenePs = item.pixelShader;
            break;
        case ProgramId::ToneMapPs:
            m_toneMapPs = item.pixelShader;
            break;
        case ProgramId::NoiseBaseCs:
            m_noiseBaseCs = item.computeShader;
            break;
        case ProgramId::NoiseDetailCs:
            m_noiseDetailCs = item.computeShader;
            break;
        case ProgramId::DeepShadowCs:
            m_deepShadowCs = item.computeShader;
            m_deepShadowShaderHash = 1469598103934665603ull;
            HashBytes(m_deepShadowShaderHash,
                      item.blob->GetBufferPointer(),
                      item.blob->GetBufferSize());
            break;
        case ProgramId::AtmosphereTransmittanceCs:
            m_atmosphereTransmittanceCs = item.computeShader;
            break;
        case ProgramId::AtmosphereMultiScatteringCs:
            m_atmosphereMultiScatteringCs = item.computeShader;
            break;
        case ProgramId::AtmosphereSkyViewCs:
            m_atmosphereSkyViewCs = item.computeShader;
            break;
        case ProgramId::AtmosphereSkyIrradianceCs:
            m_atmosphereSkyIrradianceCs = item.computeShader;
            break;
        case ProgramId::AtmosphereAerialCs:
            m_atmosphereAerialCs = item.computeShader;
            break;
        }
    }
    m_shaderManifest = std::move(manifestCandidate);
    if ((invalidation & shaderreload::InvalidateAtmosphereLuts) != 0u)
        std::fill(std::begin(m_atmosphereLutHashes),
                  std::end(m_atmosphereLutHashes), 0ull);
    if ((invalidation & shaderreload::InvalidateDeepShadow) != 0u)
        m_shadowParameters.cacheReady = 0u;
    if ((invalidation & shaderreload::InvalidateNoiseVolumes) != 0u &&
        m_noiseVolumesEnabled)
    {
        m_baseNoiseVolume = generatedBaseTexture;
        m_baseNoiseVolumeSrv = generatedBaseSrv;
        m_detailNoiseVolume = generatedDetailTexture;
        m_detailNoiseVolumeSrv = generatedDetailSrv;
        m_baseNoiseVolumeHash = generatedBaseHash;
        m_detailNoiseVolumeHash = generatedDetailHash;
        m_noiseVolumeGenerationMilliseconds = generatedMilliseconds;
        (void)generatedDetailNeutral;
    }
    ++m_shaderGeneration;
    m_shaderError.clear();
    return finish(true);
}

bool Renderer::CreateShaders(bool showErrors)
{
    m_shaderError.clear();
    if (!InitializeShaderManifest())
        return false;
    std::vector<std::size_t> allPrograms(m_shaderManifest.size());
    for (std::size_t index = 0; index < allPrograms.size(); ++index)
        allPrograms[index] = index;
    return ReloadShaderPrograms(allPrograms, { "startup" }, showErrors);
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
    if (FAILED(m_device->CreateSamplerState(
            &samplerDesc, &m_weatherLinearWrapSampler)))
        return false;

    return true;
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
    m_currentWeatherMapData = map;
    m_weatherGeneratorSettings = safeSettings;
    m_cloudTypeMode = safeSettings.cloudTypeMode;
    m_weatherMapHash = HashWeatherMap(map);
    m_weatherPreset = preset;
    m_weatherMapStatus = "DEFAULT texture created";
    return true;
}

bool Renderer::UpdateWeatherMapTexture(
    Stage5WeatherPreset preset,
    const WeatherMapGeneratorSettings& settings,
    const WeatherMapData* prebuiltMap)
{
    if (!m_weatherMapTexture || !m_weatherMapSrv || !m_context)
    {
        m_weatherMapStatus = "Weather texture is not initialized";
        return false;
    }

    const WeatherMapGeneratorSettings safeSettings =
        SanitizeWeatherMapGeneratorSettings(settings);
    const WeatherMapData generated = prebuiltMap
        ? WeatherMapData{} : BuildWeatherMap(preset, safeSettings);
    const WeatherMapData& map = prebuiltMap ? *prebuiltMap : generated;
    if (!IsValidWeatherMapData(map))
    {
        m_weatherMapStatus = "Weather map validation failed; previous map retained";
        return false;
    }

    const std::uint64_t requestedHash = HashWeatherMap(map);
    if (requestedHash == m_weatherMapHash)
    {
        m_currentWeatherMapData = map;
        m_weatherGeneratorSettings = safeSettings;
        m_cloudTypeMode = safeSettings.cloudTypeMode;
        m_weatherPreset = preset;
        std::ostringstream status;
        status << "Weather upload skipped; hash unchanged " << std::hex
               << requestedHash;
        m_weatherMapStatus = status.str();
        return true;
    }

    // 이전 frame의 t2 바인딩을 명시적으로 해제한 뒤 같은 DEFAULT texture에
    // 새 CPU RGBA를 복사한다. texture/SRV 객체는 생성 이후 바뀌지 않는다.
    ID3D11ShaderResourceView* nullWeatherSrv = nullptr;
    m_context->PSSetShaderResources(2, 1, &nullWeatherSrv);
    m_context->UpdateSubresource(
        m_weatherMapTexture.Get(), 0, nullptr, map.rgba.data(), map.width * 4u, 0);
    ++m_weatherUploadCount;
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
    m_currentWeatherMapData = map;
    m_weatherMapHash = requestedHash;
    m_weatherPreset = preset;
    std::ostringstream status;
    status << "UpdateSubresource OK, hash " << std::hex << m_weatherMapHash;
    m_weatherMapStatus = status.str();
    return true;
}

bool Renderer::InitializeStage15Presets()
{
    for (std::uint32_t index = 0; index < 3u; ++index)
    {
        const auto preset = static_cast<Stage15ConceptPreset>(index);
        const Stage15SceneDescriptor descriptor =
            stage15::ResolveSceneConcept(preset);
        WeatherMapData map = BuildWeatherMap(
            descriptor.formation.weatherPreset,
            descriptor.formation.weather);
        if (!IsValidWeatherMapData(map))
        {
            m_weatherMapStatus = "Stage 15 Weather cache generation failed";
            return false;
        }
    }
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
        stage13noise::WeightedDetailMean(
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
    m_noiseVolumeGenerationMilliseconds = milliseconds;
    return true;
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
           createDynamicBuffer(sizeof(Stage12ShadowParameters),
                               &m_shadowCb) &&
           createDynamicBuffer(sizeof(stage14::GpuParameters),
                               &m_stage14Cb) &&
           createDynamicBuffer(sizeof(SceneCB), &m_sceneCb);
}

bool Renderer::CreateAtmosphereLut2D(
    UINT width, UINT height, AtmosphereLut2D& target) const
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE |
                     D3D11_BIND_UNORDERED_ACCESS;
    AtmosphereLut2D candidate;
    if (FAILED(m_device->CreateTexture2D(&desc, nullptr, &candidate.texture)) ||
        FAILED(m_device->CreateShaderResourceView(
            candidate.texture.Get(), nullptr, &candidate.srv)) ||
        FAILED(m_device->CreateUnorderedAccessView(
            candidate.texture.Get(), nullptr, &candidate.uav)))
        return false;
    target = candidate;
    return true;
}

bool Renderer::CreateAtmosphereLut3D(UINT size,
                                     AtmosphereLut3D& target) const
{
    D3D11_TEXTURE3D_DESC desc = {};
    desc.Width = size;
    desc.Height = size;
    desc.Depth = size;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE |
                     D3D11_BIND_UNORDERED_ACCESS;
    AtmosphereLut3D candidate;
    if (FAILED(m_device->CreateTexture3D(&desc, nullptr, &candidate.texture)) ||
        FAILED(m_device->CreateShaderResourceView(
            candidate.texture.Get(), nullptr, &candidate.srv)) ||
        FAILED(m_device->CreateUnorderedAccessView(
            candidate.texture.Get(), nullptr, &candidate.uav)))
        return false;
    target = candidate;
    return true;
}

stage14::GpuParameters Renderer::BuildStage14GpuParameters(
    const Camera& camera) const
{
    const AtmosphereParameters atmosphere =
        stage14atmosphere::Sanitize(m_atmosphereParameters);
    const GroundLightingParameters ground =
        stage14ground::Sanitize(m_groundLightingParameters);
    const ToneMappingParameters tone =
        stage14tone::Sanitize(m_toneMappingParameters);
    const stage14math::Float3 sun = stage14math::DirectionFromAngles(
        atmosphere.sunAzimuthDegrees, atmosphere.sunElevationDegrees);
    const float cameraHeightKm = std::max(camera.GetPosition().y, 0.0f) * 0.001f;

    stage14::GpuParameters gpu = {};
    gpu.planetRadiiDensityHeights = {
        atmosphere.bottomRadiusKm, atmosphere.topRadiusKm,
        atmosphere.rayleighScaleHeightKm, atmosphere.mieScaleHeightKm
    };
    gpu.rayleighScatteringAndScale = {
        atmosphere.rayleighScatteringPerKm.x,
        atmosphere.rayleighScatteringPerKm.y,
        atmosphere.rayleighScatteringPerKm.z,
        atmosphere.rayleighScale
    };
    gpu.mieScatteringExtinctionGAbsorption = {
        atmosphere.mieScatteringPerKm, atmosphere.mieExtinctionPerKm,
        atmosphere.mieG, atmosphere.mieAbsorptionScale
    };
    gpu.ozoneAbsorptionAndScale = {
        atmosphere.ozoneAbsorptionPerKm.x,
        atmosphere.ozoneAbsorptionPerKm.y,
        atmosphere.ozoneAbsorptionPerKm.z,
        atmosphere.ozoneScale
    };
    gpu.ozoneLayerTurbidityAerialDistance = {
        atmosphere.ozoneCenterKm, atmosphere.ozoneHalfWidthKm,
        atmosphere.turbidity, 128.0f
    };
    gpu.solarIrradianceAndMultiplier = {
        atmosphere.solarIrradiance.x, atmosphere.solarIrradiance.y,
        atmosphere.solarIrradiance.z, std::max(m_lightParameters.sunIntensity, 0.0f)
    };
    gpu.sunDirectionAndCameraHeight = { sun.x, sun.y, sun.z, cameraHeightKm };
    gpu.sunTintAndGroundBounce = {
        std::max(m_lightParameters.sunColor.x, 0.0f),
        std::max(m_lightParameters.sunColor.y, 0.0f),
        std::max(m_lightParameters.sunColor.z, 0.0f),
        ground.bounceMultiplier
    };
    gpu.groundAlbedoAndDebugExposure = {
        ground.albedo.x, ground.albedo.y, ground.albedo.z,
        atmosphere.debugExposure
    };
    gpu.toneAndTime = {
        tone.exposureEv, tone.whiteBalanceKelvin,
        atmosphere.timeOfDayHours, atmosphere.sunElevationDegrees
    };
    gpu.renderFlags = {
        0u,
        static_cast<std::uint32_t>(tone.mode),
        static_cast<std::uint32_t>(atmosphere.debugView),
        static_cast<std::uint32_t>(atmosphere.debugChannel)
    };
    gpu.transmittanceMultiSize = {
        static_cast<float>(stage14::kTransmittanceWidth),
        static_cast<float>(stage14::kTransmittanceHeight),
        static_cast<float>(stage14::kMultiScatteringWidth),
        static_cast<float>(stage14::kMultiScatteringHeight)
    };
    gpu.skyViewIrradianceSize = {
        static_cast<float>(stage14::kSkyViewWidth),
        static_cast<float>(stage14::kSkyViewHeight),
        static_cast<float>(stage14::kSkyIrradianceWidth),
        static_cast<float>(stage14::kSkyIrradianceHeight)
    };
    const std::uint64_t maximumGeneration = *std::max_element(
        std::begin(m_atmosphereLutGenerations),
        std::end(m_atmosphereLutGenerations));
    gpu.aerialDebugGeneration = {
        stage14::kAerialSize,
        static_cast<std::uint32_t>(atmosphere.aerialSlice),
        static_cast<std::uint32_t>(maximumGeneration & 0xffffffffu), 0u
    };
    return gpu;
}

bool Renderer::EnsureAtmosphereLuts(const Camera& camera)
{
    m_atmosphereParameters = stage14atmosphere::Sanitize(
        m_atmosphereParameters);
    m_groundLightingParameters = stage14ground::Sanitize(
        m_groundLightingParameters);
    m_toneMappingParameters = stage14tone::Sanitize(
        m_toneMappingParameters);
    const stage14math::Float3 sun = stage14math::DirectionFromAngles(
        m_atmosphereParameters.sunAzimuthDegrees,
        m_atmosphereParameters.sunElevationDegrees);
    m_lightParameters.directionToSun = { sun.x, sun.y, sun.z };
    m_stage14GpuParameters = BuildStage14GpuParameters(camera);

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (m_stage14Cb && SUCCEEDED(m_context->Map(
            m_stage14Cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        std::memcpy(mapped.pData, &m_stage14GpuParameters,
                    sizeof(m_stage14GpuParameters));
        m_context->Unmap(m_stage14Cb.Get(), 0);
    }

    if (!m_atmosphereTransmittanceCs || !m_atmosphereMultiScatteringCs ||
        !m_atmosphereSkyViewCs || !m_atmosphereSkyIrradianceCs ||
        !m_atmosphereAerialCs)
    {
        m_atmosphereStatus = "LUT shaders unavailable; last good set kept";
        return false;
    }

    std::uint64_t baseHash = BeginStage14Hash();
    HashValue(baseHash, m_atmosphereParameters.bottomRadiusKm);
    HashValue(baseHash, m_atmosphereParameters.topRadiusKm);
    HashValue(baseHash, m_atmosphereParameters.rayleighScaleHeightKm);
    HashValue(baseHash, m_atmosphereParameters.mieScaleHeightKm);
    HashValue(baseHash, m_atmosphereParameters.rayleighScatteringPerKm);
    HashValue(baseHash, m_atmosphereParameters.rayleighScale);
    HashValue(baseHash, m_atmosphereParameters.mieScatteringPerKm);
    HashValue(baseHash, m_atmosphereParameters.mieExtinctionPerKm);
    HashValue(baseHash, m_atmosphereParameters.mieAbsorptionScale);
    HashValue(baseHash, m_atmosphereParameters.mieG);
    HashValue(baseHash, m_atmosphereParameters.ozoneAbsorptionPerKm);
    HashValue(baseHash, m_atmosphereParameters.ozoneScale);
    HashValue(baseHash, m_atmosphereParameters.ozoneCenterKm);
    HashValue(baseHash, m_atmosphereParameters.ozoneHalfWidthKm);
    HashValue(baseHash, m_atmosphereParameters.turbidity);
    HashValue(baseHash, m_atmosphereParameters.solarIrradiance);

    std::uint64_t multiHash = baseHash;
    HashValue(multiHash, m_groundLightingParameters.albedo);
    std::uint64_t skyViewHash = multiHash;
    HashValue(skyViewHash, sun);
    const float cameraHeightKm = std::max(camera.GetPosition().y, 0.0f) * 0.001f;
    HashValue(skyViewHash, cameraHeightKm);
    const std::uint64_t skyIrradianceHash = multiHash;
    std::uint64_t aerialHash = skyViewHash;
    XMFLOAT4X4 invProjection = {};
    XMFLOAT4X4 invViewRotation = {};
    XMStoreFloat4x4(&invProjection, XMMatrixTranspose(camera.GetInvProjection()));
    XMStoreFloat4x4(&invViewRotation,
                    XMMatrixTranspose(camera.GetInvViewRotation()));
    HashValue(aerialHash, invProjection);
    HashValue(aerialHash, invViewRotation);
    HashValue(aerialHash, camera.GetFarPlane());

    const std::uint64_t requestedHashes[6] = {
        baseHash, multiHash, skyViewHash, skyIrradianceHash,
        aerialHash, aerialHash
    };
    bool dirty[6] = {};
    for (std::size_t index = 0; index < 6; ++index)
        dirty[index] = !m_atmosphereLutsValid ||
                       requestedHashes[index] != m_atmosphereLutHashes[index];
    if (dirty[0])
        std::fill(std::begin(dirty), std::end(dirty), true);
    if (dirty[1])
        dirty[2] = dirty[3] = dirty[4] = dirty[5] = true;
    if (dirty[2])
        dirty[4] = dirty[5] = true;
    if (!std::any_of(std::begin(dirty), std::end(dirty),
                     [](bool value) { return value; }))
        return true;

    AtmosphereLutSet candidate = m_atmosphereLuts;
    const bool resourcesCreated =
        (!dirty[0] || CreateAtmosphereLut2D(
            stage14::kTransmittanceWidth, stage14::kTransmittanceHeight,
            candidate.transmittance)) &&
        (!dirty[1] || CreateAtmosphereLut2D(
            stage14::kMultiScatteringWidth, stage14::kMultiScatteringHeight,
            candidate.multiScattering)) &&
        (!dirty[2] || CreateAtmosphereLut2D(
            stage14::kSkyViewWidth, stage14::kSkyViewHeight,
            candidate.skyView)) &&
        (!dirty[3] || CreateAtmosphereLut2D(
            stage14::kSkyIrradianceWidth, stage14::kSkyIrradianceHeight,
            candidate.skyIrradiance)) &&
        (!(dirty[4] || dirty[5]) ||
         (CreateAtmosphereLut3D(stage14::kAerialSize,
                                candidate.aerialRadiance) &&
          CreateAtmosphereLut3D(stage14::kAerialSize,
                                candidate.aerialTransmittance)));
    if (!resourcesCreated)
    {
        m_atmosphereStatus = "LUT allocation failed; last good set kept";
        return false;
    }

    CameraCB cameraData = {};
    XMStoreFloat4x4(&cameraData.invViewProj,
                    XMMatrixTranspose(camera.GetInvViewProj()));
    XMStoreFloat4x4(&cameraData.invProjection,
                    XMMatrixTranspose(camera.GetInvProjection()));
    XMStoreFloat4x4(&cameraData.invViewRotation,
                    XMMatrixTranspose(camera.GetInvViewRotation()));
    cameraData.cameraPos = camera.GetPosition();
    cameraData.renderSize = {
        static_cast<float>(std::max(m_width, 1)),
        static_cast<float>(std::max(m_height, 1))
    };
    cameraData.nearPlane = camera.GetNearPlane();
    cameraData.farPlane = camera.GetFarPlane();
    if (m_cameraCb && SUCCEEDED(m_context->Map(
            m_cameraCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        std::memcpy(mapped.pData, &cameraData, sizeof(cameraData));
        m_context->Unmap(m_cameraCb.Get(), 0);
    }

    ID3D11Buffer* stage14Buffer = m_stage14Cb.Get();
    ID3D11Buffer* cameraBuffer = m_cameraCb.Get();
    m_context->CSSetConstantBuffers(0, 1, &cameraBuffer);
    m_context->CSSetConstantBuffers(9, 1, &stage14Buffer);
    ID3D11SamplerState* atmosphereSampler = m_linearClampSampler.Get();
    m_context->CSSetSamplers(3, 1, &atmosphereSampler);

    const auto bindInputs = [&](int outputIndex)
    {
        ID3D11ShaderResourceView* inputs[6] = {
            candidate.transmittance.srv.Get(),
            candidate.multiScattering.srv.Get(),
            candidate.skyView.srv.Get(),
            candidate.skyIrradiance.srv.Get(),
            candidate.aerialRadiance.srv.Get(),
            candidate.aerialTransmittance.srv.Get()
        };
        if (outputIndex >= 0 && outputIndex < 6)
            inputs[outputIndex] = nullptr;
        if (outputIndex == 4 || outputIndex == 5)
            inputs[4] = inputs[5] = nullptr;
        m_context->CSSetShaderResources(8, 6, inputs);
    };
    const auto dispatch2D = [&](ID3D11ComputeShader* shader,
                                ID3D11UnorderedAccessView* uav,
                                UINT width, UINT height, int outputIndex)
    {
        bindInputs(outputIndex);
        m_context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
        m_context->CSSetShader(shader, nullptr, 0);
        m_context->Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u);
        ID3D11UnorderedAccessView* nullUav = nullptr;
        m_context->CSSetUnorderedAccessViews(0, 1, &nullUav, nullptr);
    };
    if (dirty[0])
        dispatch2D(m_atmosphereTransmittanceCs.Get(),
                   candidate.transmittance.uav.Get(),
                   stage14::kTransmittanceWidth,
                   stage14::kTransmittanceHeight, 0);
    if (dirty[1])
        dispatch2D(m_atmosphereMultiScatteringCs.Get(),
                   candidate.multiScattering.uav.Get(),
                   stage14::kMultiScatteringWidth,
                   stage14::kMultiScatteringHeight, 1);
    if (dirty[2])
        dispatch2D(m_atmosphereSkyViewCs.Get(), candidate.skyView.uav.Get(),
                   stage14::kSkyViewWidth, stage14::kSkyViewHeight, 2);
    if (dirty[3])
        dispatch2D(m_atmosphereSkyIrradianceCs.Get(),
                   candidate.skyIrradiance.uav.Get(),
                   stage14::kSkyIrradianceWidth,
                   stage14::kSkyIrradianceHeight, 3);
    if (dirty[4] || dirty[5])
    {
        bindInputs(4);
        ID3D11UnorderedAccessView* outputs[2] = {
            candidate.aerialRadiance.uav.Get(),
            candidate.aerialTransmittance.uav.Get()
        };
        m_context->CSSetUnorderedAccessViews(0, 2, outputs, nullptr);
        m_context->CSSetShader(m_atmosphereAerialCs.Get(), nullptr, 0);
        m_context->Dispatch(
            (stage14::kAerialSize + 3u) / 4u,
            (stage14::kAerialSize + 3u) / 4u,
            (stage14::kAerialSize + 3u) / 4u);
        ID3D11UnorderedAccessView* nullOutputs[2] = {};
        m_context->CSSetUnorderedAccessViews(0, 2, nullOutputs, nullptr);
    }
    ID3D11ShaderResourceView* nullInputs[6] = {};
    m_context->CSSetShaderResources(8, 6, nullInputs);
    m_context->CSSetShader(nullptr, nullptr, 0);

    m_atmosphereLuts = candidate;
    for (std::size_t index = 0; index < 6; ++index)
    {
        if (dirty[index])
            ++m_atmosphereLutGenerations[index];
        m_atmosphereLutHashes[index] = requestedHashes[index];
    }
    m_atmosphereLutsValid = true;
    m_atmosphereStatus = "Physical LUT generation succeeded";
    return true;
}

void Renderer::BindAtmosphereResources()
{
    ID3D11Buffer* stage14Buffer = m_stage14Cb.Get();
    m_context->PSSetConstantBuffers(9, 1, &stage14Buffer);
    ID3D11ShaderResourceView* resources[6] = {
        m_atmosphereLuts.transmittance.srv.Get(),
        m_atmosphereLuts.multiScattering.srv.Get(),
        m_atmosphereLuts.skyView.srv.Get(),
        m_atmosphereLuts.skyIrradiance.srv.Get(),
        m_atmosphereLuts.aerialRadiance.srv.Get(),
        m_atmosphereLuts.aerialTransmittance.srv.Get()
    };
    m_context->PSSetShaderResources(8, 6, resources);
    ID3D11SamplerState* sampler = m_linearClampSampler.Get();
    m_context->PSSetSamplers(3, 1, &sampler);
}

bool Renderer::ValidateStage14Luts(Stage14LutValidationResult& result)
{
    result = {};
    if (!m_atmosphereLutsValid || !m_atmosphereLuts.transmittance.texture)
        return false;

    bool finiteNonNegative = true;
    std::vector<double> transmittanceErrors;
    const auto read2D = [&](ID3D11Texture2D* source, bool compareTransmittance)
    {
        if (!source)
            return false;
        D3D11_TEXTURE2D_DESC desc = {};
        source->GetDesc(&desc);
        D3D11_TEXTURE2D_DESC stagingDesc = desc;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;
        ComPtr<ID3D11Texture2D> staging;
        if (FAILED(m_device->CreateTexture2D(&stagingDesc, nullptr, &staging)))
            return false;
        m_context->CopyResource(staging.Get(), source);
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(m_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            return false;
        for (UINT y = 0; y < desc.Height; ++y)
        {
            const auto* row = reinterpret_cast<const std::uint16_t*>(
                static_cast<const std::uint8_t*>(mapped.pData) +
                static_cast<std::size_t>(y) * mapped.RowPitch);
            for (UINT x = 0; x < desc.Width; ++x)
            {
                float values[4] = {};
                for (int channel = 0; channel < 4; ++channel)
                {
                    values[channel] = DirectX::PackedVector::XMConvertHalfToFloat(
                        row[static_cast<std::size_t>(x) * 4u + channel]);
                    finiteNonNegative &= std::isfinite(values[channel]) &&
                                         values[channel] >= 0.0f;
                }
                if (compareTransmittance)
                {
                    const stage14math::Float2 uv = {
                        (static_cast<float>(x) + 0.5f) /
                            static_cast<float>(desc.Width),
                        (static_cast<float>(y) + 0.5f) /
                            static_cast<float>(desc.Height)
                    };
                    const stage14math::Float3 reference =
                        stage14math::EarthClearTransmittanceAtUv(uv);
                    const float expected[3] = {
                        reference.x, reference.y, reference.z
                    };
                    for (int channel = 0; channel < 3; ++channel)
                        transmittanceErrors.push_back(std::abs(
                            static_cast<double>(values[channel] -
                                                expected[channel])));
                }
            }
        }
        m_context->Unmap(staging.Get(), 0);
        return true;
    };
    const auto read3D = [&](ID3D11Texture3D* source)
    {
        if (!source)
            return false;
        D3D11_TEXTURE3D_DESC desc = {};
        source->GetDesc(&desc);
        D3D11_TEXTURE3D_DESC stagingDesc = desc;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;
        ComPtr<ID3D11Texture3D> staging;
        if (FAILED(m_device->CreateTexture3D(&stagingDesc, nullptr, &staging)))
            return false;
        m_context->CopyResource(staging.Get(), source);
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(m_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            return false;
        for (UINT z = 0; z < desc.Depth; ++z)
        {
            for (UINT y = 0; y < desc.Height; ++y)
            {
                const auto* row = reinterpret_cast<const std::uint16_t*>(
                    static_cast<const std::uint8_t*>(mapped.pData) +
                    static_cast<std::size_t>(z) * mapped.DepthPitch +
                    static_cast<std::size_t>(y) * mapped.RowPitch);
                for (UINT x = 0; x < desc.Width; ++x)
                {
                    for (int channel = 0; channel < 4; ++channel)
                    {
                        const float value =
                            DirectX::PackedVector::XMConvertHalfToFloat(
                                row[static_cast<std::size_t>(x) * 4u + channel]);
                        finiteNonNegative &= std::isfinite(value) && value >= 0.0f;
                    }
                }
            }
        }
        m_context->Unmap(staging.Get(), 0);
        return true;
    };

    const bool readSucceeded =
        read2D(m_atmosphereLuts.transmittance.texture.Get(), true) &&
        read2D(m_atmosphereLuts.multiScattering.texture.Get(), false) &&
        read2D(m_atmosphereLuts.skyView.texture.Get(), false) &&
        read2D(m_atmosphereLuts.skyIrradiance.texture.Get(), false) &&
        read3D(m_atmosphereLuts.aerialRadiance.texture.Get()) &&
        read3D(m_atmosphereLuts.aerialTransmittance.texture.Get());
    if (!readSucceeded || transmittanceErrors.empty())
        return false;
    result.comparedChannelCount = transmittanceErrors.size();
    double sum = 0.0;
    for (double error : transmittanceErrors)
        sum += error;
    result.transmittanceMae = sum /
        static_cast<double>(transmittanceErrors.size());
    std::sort(transmittanceErrors.begin(), transmittanceErrors.end());
    const std::size_t p99Index = std::min(
        transmittanceErrors.size() - 1u,
        static_cast<std::size_t>(std::ceil(
            0.99 * static_cast<double>(transmittanceErrors.size()))) - 1u);
    result.transmittanceP99 = transmittanceErrors[p99Index];
    result.finiteNonNegative = finiteNonNegative;
    return true;
}

void Renderer::ReleaseSizeDependentResources()
{
    m_sizeDependentResourcesValid = false;
    m_backBufferRtv.Reset();
    m_sceneColorSrv.Reset();
    m_sceneColorRtv.Reset();
    m_sceneColor.Reset();
    m_sceneDepthSrv.Reset();
    m_sceneDepthDsv.Reset();
    m_sceneDepth.Reset();
    m_hdrCloudSrv.Reset();
    m_hdrCloudRtv.Reset();
    m_hdrCloud.Reset();
}

void Renderer::Resize(int width, int height)
{
    if (!m_swapChain || width <= 0 || height <= 0)
        return;
    if (width == m_width && height == m_height &&
        m_sizeDependentResourcesValid)
        return;

    m_width = width;
    m_height = height;
    m_frameProfiler.ResetMeasurements();
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    ID3D11ShaderResourceView* nullSrvs[6] = {};
    m_context->PSSetShaderResources(0, 6, nullSrvs);
    ReleaseSizeDependentResources();

    const UINT resizeFlags = m_tearingSupported
        ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;
    if (SUCCEEDED(m_swapChain->ResizeBuffers(
            0, width, height, DXGI_FORMAT_UNKNOWN, resizeFlags)) &&
        CreateBackBufferTarget() && CreateSceneTargets())
    {
        m_sizeDependentResourcesValid = true;
        D3D11_VIEWPORT resizedViewport = {};
        resizedViewport.Width = static_cast<float>(m_width);
        resizedViewport.Height = static_cast<float>(m_height);
        resizedViewport.MaxDepth = 1.0f;
        m_context->RSSetViewports(1, &resizedViewport);
    }
}

void Renderer::RenderDiagnosticScene(const Camera& camera)
{
    SceneCB scene = {};
    XMStoreFloat4x4(
        &scene.viewProj,
        XMMatrixTranspose(camera.GetViewProj()));
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
    ID3D11Buffer* shadowBuffer = m_shadowCb.Get();
    m_context->PSSetConstantBuffers(8, 1, &shadowBuffer);
    ID3D11ShaderResourceView* shadowResources[2] = {
        m_shadowNearSrv.Get(), m_shadowFarSrv.Get()
    };
    m_context->PSSetShaderResources(6, 2, shadowResources);
    ID3D11SamplerState* shadowSampler = m_linearClampSampler.Get();
    m_context->PSSetSamplers(2, 1, &shadowSampler);
    BindAtmosphereResources();
    m_context->DrawIndexed(m_sceneIndexCount, 0, 0);
    UnbindCloudShaderResources(14);

    m_context->OMSetRenderTargets(0, nullptr, nullptr);
}

void Renderer::RenderCloudPass()
{
    const float clearColor[4] = { 0.02f, 0.03f, 0.05f, 1.0f };
    ID3D11RenderTargetView* cloudTarget = m_hdrCloudRtv.Get();
    m_context->OMSetRenderTargets(1, &cloudTarget, nullptr);
    m_context->ClearRenderTargetView(cloudTarget, clearColor);
    m_context->OMSetDepthStencilState(nullptr, 0);
    m_context->RSSetState(nullptr);

    m_context->IASetInputLayout(nullptr);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_fullscreenVs.Get(), nullptr, 0);
    m_context->PSSetShader(m_cloudPs.Get(), nullptr, 0);
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
    ID3D11Buffer* cloudShapeBuffer = m_cloudShapeCb.Get();
    m_context->PSSetConstantBuffers(7, 1, &cloudShapeBuffer);
    ID3D11Buffer* shadowBuffer = m_shadowCb.Get();
    m_context->PSSetConstantBuffers(8, 1, &shadowBuffer);
    BindAtmosphereResources();
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

    UnbindCloudShaderResources(14);
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
    m_shadowParameters.cacheReady = resourcesReady &&
        basis.valid && basis.forward.y >= m_shadowParameters.minimumSunY
        ? 1u : 0u;
}

void Renderer::RenderDeepShadowCaches(const Camera& camera, float timeSeconds)
{
    UpdateCloudConstantBuffers(camera, timeSeconds);
    if (m_shadowParameters.cacheReady == 0u)
        return;

    ID3D11Buffer* cameraBuffer = m_cameraCb.Get();
    ID3D11Buffer* cloudBuffer = m_cloudCb.Get();
    ID3D11Buffer* domainBuffer = m_cloudDomainCb.Get();
    ID3D11Buffer* noiseBuffer = m_noiseVolumeCb.Get();
    ID3D11Buffer* shapeBuffer = m_cloudShapeCb.Get();
    ID3D11Buffer* shadowBuffer = m_shadowCb.Get();
    m_context->CSSetConstantBuffers(0, 1, &cameraBuffer);
    m_context->CSSetConstantBuffers(1, 1, &cloudBuffer);
    m_context->CSSetConstantBuffers(5, 1, &domainBuffer);
    m_context->CSSetConstantBuffers(6, 1, &noiseBuffer);
    m_context->CSSetConstantBuffers(7, 1, &shapeBuffer);
    m_context->CSSetConstantBuffers(8, 1, &shadowBuffer);
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
                                          float timeSeconds)
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
        static_cast<float>(std::max(m_width, 1)),
        static_cast<float>(std::max(m_height, 1))
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
    UpdateStage12ShadowParameters(camera);

    const auto update = [&](std::size_t index, ID3D11Buffer* buffer,
                            const void* data, std::size_t size)
    {
        std::uint64_t contentHash = 1469598103934665603ull;
        HashBytes(contentHash, data, size);
        if (index < m_constantBufferUploadHashes.size() &&
            m_constantBufferUploadValid[index] &&
            m_constantBufferUploadHashes[index] == contentHash)
            return;
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (buffer && SUCCEEDED(m_context->Map(
                buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            std::memcpy(mapped.pData, data, size);
            m_context->Unmap(buffer, 0);
            if (index < m_constantBufferUploadHashes.size())
            {
                m_constantBufferUploadHashes[index] = contentHash;
                m_constantBufferUploadValid[index] = true;
            }
            ++m_constantBufferUploadCount;
        }
    };
    update(0, m_cameraCb.Get(), &cameraData, sizeof(cameraData));
    update(1, m_cloudCb.Get(), &m_cloudParameters, sizeof(m_cloudParameters));
    update(2, m_lightCb.Get(), &m_lightParameters, sizeof(m_lightParameters));
    update(3, m_environmentCb.Get(), &m_environmentParameters,
           sizeof(m_environmentParameters));
    update(4, m_cloudDomainCb.Get(), &m_cloudDomainParameters,
           sizeof(m_cloudDomainParameters));
    update(5, m_noiseVolumeCb.Get(), &m_noiseVolumeParameters,
           sizeof(m_noiseVolumeParameters));
    update(6, m_cloudShapeCb.Get(), &m_cloudShapeParameters,
           sizeof(m_cloudShapeParameters));
    update(7, m_shadowCb.Get(), &m_shadowParameters,
           sizeof(m_shadowParameters));
}

void Renderer::UnbindCloudShaderResources(UINT count)
{
    std::vector<ID3D11ShaderResourceView*> nullResources(count, nullptr);
    m_context->PSSetShaderResources(0, count, nullResources.data());
}

void Renderer::Render(Camera& camera, float timeSeconds)
{
    if (!m_sizeDependentResourcesValid || !m_backBufferRtv ||
        !m_sceneColorRtv || !m_sceneDepthDsv)
        return;

    ++m_renderFrameSerial;
    m_frameProfiler.BeginCpuFrame();
    CheckShaderHotReload();
    m_frameProfiler.BeginGpuFrame(m_context.Get());

    if (m_atmosphereParameters.timePlaybackEnabled)
    {
        float deltaSeconds = 0.0f;
        if (m_previousAtmosphereTimeValid)
        {
            const float candidate = timeSeconds - m_previousAtmosphereTimeSeconds;
            if (std::isfinite(candidate) && candidate >= 0.0f && candidate <= 0.25f)
                deltaSeconds = candidate;
        }
        m_atmosphereParameters.sunControlMode = SunControlMode::TimeOfDay;
        m_atmosphereParameters.timeOfDayHours =
            stage14math::AdvanceLoopingTimeOfDay(
                m_atmosphereParameters.timeOfDayHours, deltaSeconds,
                m_atmosphereParameters.timePlaybackMinutesPerSecond);
        const stage14math::SunAngles path = stage14math::TimeOfDayPath(
            m_atmosphereParameters.timeOfDayHours);
        m_atmosphereParameters.sunAzimuthDegrees = path.azimuthDegrees;
        m_atmosphereParameters.sunElevationDegrees = path.elevationDegrees;
    }
    else
    {
        m_atmosphereParameters.sunControlMode = SunControlMode::Angles;
    }
    m_previousAtmosphereTimeSeconds = timeSeconds;
    m_previousAtmosphereTimeValid = true;

    const CloudFormationSettings formationBeforeUi = CurrentCloudFormation();
    const CloudFormationSettings rawFormationBeforeUi =
        CaptureCloudFormationSettingsUnchecked(
            m_cloudParameters, m_cloudShapeParameters,
            m_cloudDomainParameters, m_weatherPreset,
            m_weatherGeneratorSettings, m_noiseVolumeParameters);
    const std::array<ID3D11ShaderResourceView*, 6> atmosphereLutSrvs = {
        m_atmosphereLuts.transmittance.srv.Get(),
        m_atmosphereLuts.multiScattering.srv.Get(),
        m_atmosphereLuts.skyView.srv.Get(),
        m_atmosphereLuts.skyIrradiance.srv.Get(),
        m_atmosphereLuts.aerialRadiance.srv.Get(),
        m_atmosphereLuts.aerialTransmittance.srv.Get()
    };
    if (!m_automatedRenderMode)
    {
        m_noiseLab.BeginFrame(
            timeSeconds, camera, m_cloudParameters, m_cloudShapeParameters,
            m_cloudDomainParameters, m_weatherGeneratorSettings,
            m_shadowParameters, m_lightParameters, m_sunPreset,
            m_phasePreset, m_environmentParameters, m_environmentPreset,
            m_atmosphereParameters, m_groundLightingParameters,
            m_toneMappingParameters, m_stage15ConceptPreset,
            m_cloudFormationTarget, m_cloudFormationSource,
            m_hasSavedCustomFormation, m_cloudFormationStatus,
            m_cameraMoveSpeedMetersPerSecond, m_noiseVolumeParameters,
            m_baseNoiseVolumeHash, m_detailNoiseVolumeHash,
            m_noiseVolumeGenerationMilliseconds, m_weatherMapSrv.Get(),
            atmosphereLutSrvs, m_frameProfiler.Snapshot(), m_vsyncEnabled,
            m_tearingSupported,
            m_shaderGeneration, m_shaderStatus, m_shaderError,
            m_lastShaderReloadReport);

        const CloudFormationSettings rawCandidate =
            CaptureCloudFormationSettingsUnchecked(
                m_cloudParameters, m_cloudShapeParameters,
                m_cloudDomainParameters, m_weatherPreset,
                m_weatherGeneratorSettings, m_noiseVolumeParameters);
        if (m_noiseLab.ConsumeFormationEdited() ||
            !CloudFormationSettingsEqual(rawFormationBeforeUi, rawCandidate, 0.0f))
        {
            PreparedCloudFormation previous;
            std::string previousStatus;
            if (PrepareCloudFormationSettings(
                    formationBeforeUi, previous, previousStatus, 200.0f))
            {
                WriteCloudFormationToRuntime(
                    previous, m_cloudParameters, m_cloudShapeParameters,
                    m_cloudDomainParameters, m_weatherPreset,
                    m_weatherGeneratorSettings, m_noiseVolumeParameters);
                m_cloudTypeMode = m_weatherGeneratorSettings.cloudTypeMode;
                ApplyCloudFormationAtomic(
                    SanitizeCloudFormationSettings(rawCandidate));
            }
        }

        CloudFormationPresetTarget formationRequest;
        if (m_noiseLab.ConsumeFormationPresetRequest(formationRequest))
            ApplyCloudType(formationRequest);
        if (m_noiseLab.ConsumeSaveCustomRequest())
            SaveCustomFormation();
        if (m_noiseLab.ConsumeLoadCustomRequest())
            LoadCustomFormation();
        Stage15ConceptPreset conceptRequest = m_stage15ConceptPreset;
        if (m_noiseLab.ConsumeSceneConceptRequest(conceptRequest))
            ApplySceneConcept(conceptRequest);
        if (m_noiseLab.ConsumeNoiseVolumeRegenerateRequest())
            RegenerateNoiseVolumes();
    }

    const float effectiveTime = m_automatedRenderMode
        ? timeSeconds : m_noiseLab.EffectiveTime();
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(m_width);
    viewport.Height = static_cast<float>(m_height);
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);

    EnsureAtmosphereLuts(camera);
    m_frameProfiler.MarkAtmosphereLutEnd(m_context.Get());
    RenderDeepShadowCaches(camera, effectiveTime);
    m_frameProfiler.MarkShadowCacheEnd(m_context.Get());
    RenderDiagnosticScene(camera);
    m_frameProfiler.MarkOpaqueSceneEnd(m_context.Get());
    RenderCloudPass();
    m_frameProfiler.MarkCloudEnd(m_context.Get());
    RenderToneMapPass();
    m_frameProfiler.MarkToneMapEnd(m_context.Get());
    if (m_captureFrameHashes)
        CaptureCloudFrameHash();
    if (!m_automatedRenderMode && m_renderNoiseLabPreviews)
    {
        m_noiseLab.RenderPreviews(
            m_fullscreenVs.Get(), m_noiseLabPs.Get(), m_cloudCb.Get(),
            m_noiseVolumeCb.Get(), m_cloudShapeCb.Get(), m_weatherMapSrv.Get(),
            m_baseNoiseVolumeSrv.Get(), m_detailNoiseVolumeSrv.Get(),
            m_weatherLinearWrapSampler.Get());
    }
    if (!m_automatedRenderMode && m_noiseLab.ConsumeExportRequest())
        ExportNoiseLabSnapshot(DefaultCloudFormationPresetRoot());
    if (!m_automatedRenderMode)
        m_noiseLab.EndFrame(m_backBufferRtv.Get());
    m_context->RSSetViewports(1, &viewport);
    m_frameProfiler.EndGpuFrame(m_context.Get());
    const presentation::PresentParameters present =
        presentation::ResolvePresentParameters(
            m_vsyncEnabled, m_tearingSupported, true);
    m_swapChain->Present(present.syncInterval, present.flags);
    m_frameProfiler.EndCpuFrame();
}

void Renderer::RenderToneMapPass()
{
    ID3D11ShaderResourceView* source = m_hdrCloudSrv.Get();
    if (!m_toneMapPs || !source || !m_backBufferRtv)
        return;
    const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    m_context->OMSetRenderTargets(1, m_backBufferRtv.GetAddressOf(), nullptr);
    m_context->ClearRenderTargetView(m_backBufferRtv.Get(), clearColor);
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
    m_context->PSSetShader(m_toneMapPs.Get(), nullptr, 0);
    BindAtmosphereResources();
    m_context->PSSetShaderResources(0, 1, &source);
    ID3D11SamplerState* sampler = m_linearClampSampler.Get();
    m_context->PSSetSamplers(0, 1, &sampler);
    m_context->Draw(3, 0);
    // 다음 프레임의 LUT compute dispatch가 같은 리소스를 UAV로 다시 바인딩할 수
    // 있으므로 HDR뿐 아니라 단계 14의 t8~t13도 명시적으로 해제한다.
    UnbindCloudShaderResources(14);
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

bool Renderer::ApplyStage15Defaults()
{
    m_shadowParameters = Stage12ShadowParameters{};
    m_shadowParameters = stage12shadow::Sanitize(m_shadowParameters);
    if (!CreateDeepShadowResources())
        return false;
    if (!ApplySceneConcept(Stage15ConceptPreset::UrbanFairWeather))
        return false;
    m_frameProfiler.ResetMeasurements();
    return true;
}

bool Renderer::ApplySceneConcept(Stage15ConceptPreset preset)
{
    const std::uint32_t index = static_cast<std::uint32_t>(preset);
    if (index > static_cast<std::uint32_t>(
            Stage15ConceptPreset::SnowOvercast))
        return false;

    const Stage15SceneDescriptor descriptor =
        stage15::ResolveSceneConcept(preset);
    if (!ApplyCloudFormationPresetTarget(
            ConceptFormationTarget(stage15::FormationConcept(preset)),
            false))
        return false;

    LightParameters light = descriptor.light;
    light.directionToSun = stage6light::DirectionFromAngles(
        descriptor.atmosphere.sunAzimuthDegrees,
        descriptor.atmosphere.sunElevationDegrees);
    m_lightParameters = stage6light::Sanitize(light);
    m_environmentParameters = descriptor.environment;
    m_atmosphereParameters = descriptor.atmosphere;
    m_groundLightingParameters = descriptor.ground;
    m_shadowParameters.surfaceShadowEnabled =
        descriptor.surfaceShadowEnabled;
    m_shadowParameters.surfaceShadowStrength =
        descriptor.surfaceShadowStrength;
    m_shadowParameters.surfaceAmbientFloor =
        descriptor.surfaceAmbientFloor;
    m_shadowParameters = stage12shadow::Sanitize(m_shadowParameters);
    m_sunPreset = descriptor.sunPreset;
    m_phasePreset = descriptor.phasePreset;
    m_environmentPreset = descriptor.environmentPreset;

    const float half = stage13scene::kGroundHalfSizeMeters;
    m_cloudParameters.cloudBoundsMin.x = -half;
    m_cloudParameters.cloudBoundsMin.z = -half;
    m_cloudParameters.cloudBoundsMax.x = half;
    m_cloudParameters.cloudBoundsMax.z = half;
    m_stage15ConceptPreset = preset;
    m_cloudFormationStatus = std::string(stage15::ConceptName(preset)) +
        " scene concept applied";
    m_frameProfiler.ResetMeasurements();
    return true;
}

bool Renderer::ApplyCloudType(const CloudFormationPresetTarget& target)
{
    if (target.group != CloudFormationPresetGroup::Type)
        return false;
    const bool applied = ApplyCloudFormationPresetTarget(
        target, false);
    if (applied)
        m_frameProfiler.ResetMeasurements();
    return applied;
}

bool Renderer::SaveCustomFormation()
{
    return SaveCurrentCloudFormationToTarget(
        CustomFormationTarget(), true);
}

bool Renderer::LoadCustomFormation()
{
    return ApplyCloudFormationPresetTarget(
        CustomFormationTarget(), true);
}

CloudFormationSettings Renderer::CurrentCloudFormation() const
{
    return CaptureCloudFormationSettings(
        m_cloudParameters, m_cloudShapeParameters, m_cloudDomainParameters,
        m_weatherPreset, m_weatherGeneratorSettings,
        m_noiseVolumeParameters);
}

bool Renderer::ApplyCloudFormationAtomic(
    const CloudFormationSettings& settings)
{
    PreparedCloudFormation prepared;
    std::string status;
    if (!PrepareCloudFormationSettings(settings, prepared, status, 200.0f))
    {
        m_cloudFormationStatus = status;
        return false;
    }
    if (m_failNextCloudFormationApplyForValidation)
    {
        m_failNextCloudFormationApplyForValidation = false;
        m_cloudFormationStatus =
            "Injected formation apply failure; runtime state retained";
        return false;
    }

    CloudParameters cloud = m_cloudParameters;
    CloudShapeParameters shape = m_cloudShapeParameters;
    CloudDomainParameters domain = m_cloudDomainParameters;
    Stage5WeatherPreset weatherPreset = m_weatherPreset;
    WeatherMapGeneratorSettings weather = m_weatherGeneratorSettings;
    NoiseVolumeParameters noise = m_noiseVolumeParameters;
    WriteCloudFormationToRuntime(
        prepared, cloud, shape, domain, weatherPreset, weather, noise);

    // XZ의 오픈 월드 footprint는 formation 슬롯이 아니라 공통 Stage 13 장면이
    // 소유한다. Y만 검증된 PlanarLayer domain과 함께 원자적으로 갱신한다.
    cloud.cloudBoundsMin.y = domain.cloudBottomAltitude;
    cloud.cloudBoundsMax.y =
        domain.cloudBottomAltitude + domain.cloudLayerThickness;

    if (!UpdateWeatherMapTexture(
            weatherPreset, weather, &prepared.weatherMap))
    {
        m_cloudFormationStatus =
            "Formation Weather upload failed; runtime state retained";
        return false;
    }

    m_cloudParameters = cloud;
    m_cloudShapeParameters = shape;
    m_cloudDomainParameters = domain;
    m_noiseVolumeParameters = noise;
    m_weatherPreset = weatherPreset;
    m_weatherGeneratorSettings = weather;
    m_cloudTypeMode = weather.cloudTypeMode;
    m_noiseLab.SynchronizeWeatherGeneratorSettings(weather);
    m_cloudFormationStatus = "Formation applied atomically";
    return true;
}

bool Renderer::ApplyCloudFormationPresetTarget(
    const CloudFormationPresetTarget& target, bool allowUserOverrides)
{
    CloudFormationSettings settings;
    CloudFormationPresetSource source = CloudFormationPresetSource::BuiltIn;
    std::string status;
    if (!ResolveCloudFormationPreset(
            m_cloudFormationPresetRoot, target, allowUserOverrides,
            settings, source, status))
    {
        m_cloudFormationStatus = status;
        return false;
    }
    if (!ApplyCloudFormationAtomic(settings))
        return false;

    m_cloudFormationTarget = target;
    m_cloudFormationTargetValid = true;
    m_cloudFormationSource = source;
    m_cloudFormationStatus = status;
    m_hasSavedCustomFormation = m_hasSavedCustomFormation ||
        target.group == CloudFormationPresetGroup::Custom;
    return true;
}

bool Renderer::ApplyCloudFormationPresetForValidation(
    const CloudFormationPresetTarget& target, bool allowUserOverrides)
{
    return ApplyCloudFormationPresetTarget(
        target, allowUserOverrides);
}

bool Renderer::ApplyCloudFormationSettingsForValidation(
    const CloudFormationSettings& settings)
{
    if (!ApplyCloudFormationAtomic(settings))
        return false;
    MarkCloudFormationDirty();
    return true;
}

bool Renderer::SaveCurrentCloudFormationAsCustomForValidation()
{
    return SaveCurrentCloudFormationToTarget(
        CustomFormationTarget(), true);
}

void Renderer::SetCloudFormationPresetRootForValidation(
    const std::filesystem::path& root)
{
    m_cloudFormationPresetRoot = root;
    RefreshSavedCustomFormationState();
}

void Renderer::RefreshSavedCustomFormationState()
{
    CloudFormationSettings custom;
    std::string status;
    m_hasSavedCustomFormation = LoadCloudFormationPreset(
        CloudFormationPresetPath(
            m_cloudFormationPresetRoot, CustomFormationTarget()),
        CustomFormationTarget(), custom, status);
}

bool Renderer::SaveCurrentCloudFormationToTarget(
    const CloudFormationPresetTarget& target, bool switchTargetAfterSave)
{
    if (target.group != CloudFormationPresetGroup::Custom)
    {
        m_cloudFormationStatus =
            "Formation save rejected: built-in presets are immutable";
        return false;
    }

    const CloudFormationSettings current = CurrentCloudFormation();
    PreparedCloudFormation prepared;
    std::string validationStatus;
    if (!PrepareCloudFormationSettings(
            current, prepared, validationStatus, 200.0f))
    {
        m_cloudFormationStatus = validationStatus;
        return false;
    }

    const std::filesystem::path path = CloudFormationPresetPath(
        m_cloudFormationPresetRoot, target);
    std::string status;
    if (!SaveCloudFormationPresetAtomic(path, target, current, status))
    {
        m_cloudFormationStatus = status;
        return false;
    }

    if (switchTargetAfterSave)
    {
        m_cloudFormationTarget = target;
        m_cloudFormationTargetValid = true;
    }
    m_cloudFormationSource = CloudFormationPresetSource::UserOverride;
    if (target.group == CloudFormationPresetGroup::Custom)
        m_hasSavedCustomFormation = true;
    m_cloudFormationStatus = status;
    return true;
}

void Renderer::MarkCloudFormationDirty()
{
    m_cloudFormationSource = CloudFormationPresetSource::Unsaved;
    m_cloudFormationStatus = m_cloudFormationTargetValid
        ? std::string("Unsaved formation changes for ") +
            CloudFormationPresetTargetName(m_cloudFormationTarget)
        : "Unsaved formation changes (no preset target)";
}

bool Renderer::ApplyWeatherGeneratorSettings(
    const WeatherMapGeneratorSettings& settings)
{
    const WeatherMapGeneratorSettings safeSettings =
        SanitizeWeatherMapGeneratorSettings(settings);
    const bool changed = !WeatherMapGeneratorSettingsEqual(
        safeSettings, m_weatherGeneratorSettings);
    if (!changed)
        return true;

    CloudFormationSettings formation = CurrentCloudFormation();
    formation.weather = safeSettings;
    const bool applied = ApplyCloudFormationAtomic(formation);
    if (applied)
        MarkCloudFormationDirty();
    return applied;
}

std::uint64_t Renderer::RuntimeStateHash() const
{
    std::uint64_t hash = 1469598103934665603ull;
    HashBytes(hash, &m_cloudParameters, sizeof(m_cloudParameters));
    HashBytes(hash, &m_cloudDomainParameters, sizeof(m_cloudDomainParameters));
    HashBytes(hash, &m_cloudShapeParameters, sizeof(m_cloudShapeParameters));
    HashBytes(hash, &m_shadowParameters, sizeof(m_shadowParameters));
    HashBytes(hash, &m_lightParameters, sizeof(m_lightParameters));
    HashBytes(hash, &m_environmentParameters, sizeof(m_environmentParameters));
    HashBytes(hash, &m_atmosphereParameters, sizeof(m_atmosphereParameters));
    HashBytes(hash, &m_groundLightingParameters,
              sizeof(m_groundLightingParameters));
    HashBytes(hash, &m_toneMappingParameters, sizeof(m_toneMappingParameters));
    HashValue(hash, m_stage15ConceptPreset);
    HashValue(hash, m_weatherMapHash);
    HashValue(hash, m_baseNoiseVolumeHash);
    HashValue(hash, m_detailNoiseVolumeHash);
    HashValue(hash, m_cloudShaderHash);
    HashValue(hash, m_deepShadowShaderHash);
    return hash;
}

std::uint64_t Renderer::GpuResourceIdentityHash() const
{
    std::uint64_t hash = 1469598103934665603ull;
    const auto append = [&](const void* pointer)
    {
        const std::uintptr_t identity =
            reinterpret_cast<std::uintptr_t>(pointer);
        HashValue(hash, identity);
    };
    append(m_weatherMapTexture.Get());
    append(m_weatherMapSrv.Get());
    append(m_shadowNearTexture.Get());
    append(m_shadowNearSrv.Get());
    append(m_shadowNearUav.Get());
    append(m_shadowFarTexture.Get());
    append(m_shadowFarSrv.Get());
    append(m_shadowFarUav.Get());
    return hash;
}

std::uint64_t Renderer::ShaderObjectIdentityHash() const
{
    std::uint64_t hash = 1469598103934665603ull;
    const auto append = [&](const void* pointer)
    {
        const std::uintptr_t identity =
            reinterpret_cast<std::uintptr_t>(pointer);
        HashValue(hash, identity);
    };
    append(m_fullscreenVs.Get());
    append(m_cloudPs.Get());
    append(m_noiseLabPs.Get());
    append(m_sceneVs.Get());
    append(m_scenePs.Get());
    append(m_toneMapPs.Get());
    append(m_noiseBaseCs.Get());
    append(m_noiseDetailCs.Get());
    append(m_deepShadowCs.Get());
    append(m_atmosphereTransmittanceCs.Get());
    append(m_atmosphereMultiScatteringCs.Get());
    append(m_atmosphereSkyViewCs.Get());
    append(m_atmosphereSkyIrradianceCs.Get());
    append(m_atmosphereAerialCs.Get());
    return hash;
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
        if (SUCCEEDED(infoQueue->GetMessage(i, message, &messageSize)))
        {
            const bool resourceHazard =
                message->ID ==
                    D3D11_MESSAGE_ID_DEVICE_PSSETSHADERRESOURCES_HAZARD ||
                message->ID ==
                    D3D11_MESSAGE_ID_DEVICE_CSSETSHADERRESOURCES_HAZARD ||
                message->ID ==
                    D3D11_MESSAGE_ID_DEVICE_CSSETUNORDEREDACCESSVIEWS_HAZARD ||
                message->ID ==
                    D3D11_MESSAGE_ID_DEVICE_OMSETRENDERTARGETS_HAZARD ||
                message->ID ==
                    D3D11_MESSAGE_ID_DEVICE_OMSETRENDERTARGETSANDUNORDEREDACCESSVIEWS_HAZARD;
            // SRV/RTV/UAV 중복 바인딩은 debug layer에서 WARNING으로 보고한 뒤
            // 해당 input을 자동 NULL 처리한다. ERROR만 검사하면 화면 손상을
            // 일으키는 이 경고를 모든 GPU smoke가 놓치므로 hazard ID도 실패다.
            if (resourceHazard ||
                message->Severity == D3D11_MESSAGE_SEVERITY_ERROR ||
                message->Severity == D3D11_MESSAGE_SEVERITY_CORRUPTION)
                return true;
        }
    }
#endif
    return false;
}

bool Renderer::ValidateWarmShaderCache()
{
    const std::uint64_t cloudHash = m_cloudShaderHash;
    const std::uint64_t shadowHash = m_deepShadowShaderHash;
    m_shaderCompileCallCount = 0;
    m_shaderCacheHitCount = 0;
    if (!CreateShaders(false))
        return false;
    return m_shaderCompileCallCount == 0u && m_shaderCacheHitCount > 0u &&
        cloudHash == m_cloudShaderHash &&
        shadowHash == m_deepShadowShaderHash;
}

bool Renderer::ValidateNoiseSamplerContract() const
{
    if (!m_weatherLinearWrapSampler)
        return false;
    D3D11_SAMPLER_DESC desc = {};
    m_weatherLinearWrapSampler->GetDesc(&desc);
    return desc.Filter == D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT &&
        desc.AddressU == D3D11_TEXTURE_ADDRESS_WRAP &&
        desc.AddressV == D3D11_TEXTURE_ADDRESS_WRAP &&
        desc.AddressW == D3D11_TEXTURE_ADDRESS_WRAP;
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
    m_lastShaderScanTime = std::chrono::steady_clock::now();
}

bool Renderer::ScanShaderChanges(bool forced)
{
    std::map<std::wstring, std::filesystem::file_time_type> currentTimes;
    if (!GetShaderWriteTimes(currentTimes) || currentTimes == m_shaderWriteTimes)
        return false;

    const std::filesystem::path root(m_shaderDir);
    std::set<std::string> changedSet;
    std::vector<std::string> changedFiles;
    const auto recordChanged = [&](const std::wstring& path)
    {
        const std::filesystem::path relative =
            std::filesystem::path(path).lexically_relative(root);
        const std::string normalized =
            shaderreload::NormalizeRelativePath(relative);
        if (!normalized.empty() && changedSet.insert(normalized).second)
            changedFiles.push_back(normalized);
    };
    for (const auto& [path, time] : currentTimes)
    {
        const auto previous = m_shaderWriteTimes.find(path);
        if (previous == m_shaderWriteTimes.end() || previous->second != time)
            recordChanged(path);
    }
    for (const auto& [path, time] : m_shaderWriteTimes)
    {
        if (currentTimes.find(path) == currentTimes.end())
            recordChanged(path);
    }

    const std::vector<std::size_t> affected =
        shaderreload::SelectAffectedPrograms(m_shaderManifest, changedSet);
    const bool succeeded = ReloadShaderPrograms(
        affected, changedFiles, false);
    // 실패한 세대의 객체·dependency closure는 유지하되 관찰 시각은 갱신한다.
    // 파일을 다시 고치거나 복원하면 새 write-time 차이로 정확히 한 번 재시도한다.
    m_shaderWriteTimes = currentTimes;
    (void)forced;
    return succeeded;
}

bool Renderer::ForceShaderReloadScan()
{
    m_lastShaderScanTime = std::chrono::steady_clock::now();
    return ScanShaderChanges(true);
}

void Renderer::CheckShaderHotReload()
{
    const auto now = std::chrono::steady_clock::now();
    if (m_lastShaderScanTime.time_since_epoch().count() != 0 &&
        now - m_lastShaderScanTime < std::chrono::milliseconds(250))
        return;
    m_lastShaderScanTime = now;
    ScanShaderChanges(false);
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
        root, CurrentCloudFormation(), m_shadowParameters,
        m_lightParameters, m_environmentParameters,
        m_atmosphereParameters, m_groundLightingParameters,
        m_toneMappingParameters, m_stage15ConceptPreset,
        m_noiseVolumeParameters, m_baseNoiseVolumeHash,
        m_detailNoiseVolumeHash, m_weatherMapHash, m_shaderGeneration);
}

std::uint64_t Renderer::NoiseLabPreviewHash(std::size_t targetIndex)
{
    return m_noiseLab.PreviewHash(targetIndex);
}
