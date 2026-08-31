#include "Renderer.h"
#include "Camera.h"
#include "CloudShapeDomainContract.h"
#include "Stage13CameraPresets.h"
#include "Stage13SceneMath.h"
#include "Stage11TemporalMath.h"
#include "Stage14AtmosphereMath.h"

#include <d3dcompiler.h>
#include <bcrypt.h>
#include <SetupAPI.h>
#include <devguid.h>
#include <DirectXPackedVector.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string_view>
#include <vector>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace
{
bool TryMapStage15ConceptToFormation(
    Stage15ConceptPreset preset, CloudFormationConcept& outConcept)
{
    switch (preset)
    {
    case Stage15ConceptPreset::UrbanFairWeather:
        outConcept = CloudFormationConcept::UrbanFairWeather;
        return true;
    case Stage15ConceptPreset::MeadowBrokenClouds:
        outConcept = CloudFormationConcept::MeadowBrokenClouds;
        return true;
    case Stage15ConceptPreset::DesertCirrus:
        outConcept = CloudFormationConcept::DesertCirrus;
        return true;
    case Stage15ConceptPreset::SnowOvercast:
        outConcept = CloudFormationConcept::SnowOvercast;
        return true;
    case Stage15ConceptPreset::Custom:
    default:
        return false;
    }
}

CloudAppearancePreset LegacyAppearanceForFormation(
    const CloudFormationSettings& formation)
{
    if (cloudshape::Mode(formation.shape.shapeMode) ==
        CloudShapeMode::CirrusPhysicalLayer)
    {
        return CloudAppearancePreset::CustomUnsaved;
    }
    switch (formation.weather.cloudTypeMode)
    {
    case CloudTypeMode::Stratus: return CloudAppearancePreset::Stratus;
    case CloudTypeMode::Cumulus: return CloudAppearancePreset::Cumulus;
    case CloudTypeMode::Mixed:
    case CloudTypeMode::WeatherMap:
    default:
        return CloudAppearancePreset::DenseMixedDefault;
    }
}

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

bool Renderer::Init(HWND hwnd, int width, int height, bool enableNoiseVolumes)
{
    m_hwnd = hwnd;
    m_width = width;
    m_height = height;
    m_noiseVolumesEnabled = enableNoiseVolumes;
    m_shaderDir = ResolveShaderDir();
    m_fullscreenShaderPath = m_shaderDir + L"Fullscreen.hlsl";
    m_cloudShaderPath = m_shaderDir + L"VolumetricClouds.hlsl";
    m_cloudUpsampleShaderPath = m_shaderDir + L"CloudUpsample.hlsl";
    m_cloudTemporalResolveShaderPath =
        m_shaderDir + L"CloudTemporalResolve.hlsl";
    m_cloudCompositeShaderPath = m_shaderDir + L"CloudComposite.hlsl";
    m_noiseLabShaderPath = m_shaderDir + L"NoiseLab.hlsl";
    m_sceneShaderPath = m_shaderDir + L"DiagnosticScene.hlsl";
    m_noiseVolumeShaderPath = m_shaderDir + L"NoiseVolume.hlsl";
    m_deepShadowShaderPath = m_shaderDir + L"CloudDeepShadow.hlsl";
    m_atmosphereLutShaderPath = m_shaderDir + L"Stage14AtmosphereLut.hlsl";
    m_toneMapShaderPath = m_shaderDir + L"Stage14ToneMap.hlsl";
    m_stage15CaptureAccumulateShaderPath =
        m_shaderDir + L"Stage15CaptureAccumulate.hlsl";
    // Physical 모드에서 기존 색·세기는 대기 결과의 예술적 tint/multiplier다.
    m_lightParameters.sunColor = { 1.0f, 1.0f, 1.0f };
    m_lightParameters.sunIntensity = 1.0f;
    std::filesystem::path shaderDirectory(m_shaderDir);
    if (shaderDirectory.filename().empty())
        shaderDirectory = shaderDirectory.parent_path();
    m_customAppearancePath = shaderDirectory.parent_path() / L"captures" /
        L"noise-lab" / L"custom" / L"noise-settings.json";
    m_cloudFormationPresetRoot = shaderDirectory.parent_path() / L"captures" /
        L"noise-lab" / L"cloud-presets";
    const std::filesystem::path developerUiSettingsPath =
        shaderDirectory.parent_path() / L"captures" /
        L"noise-lab" / L"developer-ui.json";
    m_hasSavedCustomAppearance = LoadCustomCloudAppearance(
        m_customAppearancePath, m_savedCustomAppearance,
        m_cloudAppearanceStatus);
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
        !m_noiseLab.Init(
            hwnd, m_device.Get(), m_context.Get(), developerUiSettingsPath))
    {
        MessageBoxW(hwnd, L"단계 8 렌더링 리소스 생성 실패", L"오류", MB_OK | MB_ICONERROR);
        return false;
    }
    m_sizeDependentResourcesValid = true;
    D3D11_VIEWPORT initialViewport = {};
    initialViewport.Width = static_cast<float>(m_width);
    initialViewport.Height = static_cast<float>(m_height);
    initialViewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &initialViewport);

    // 단계 12 cache는 화면 크기와 독립적이다. 실패해도 DirectReference로
    // 렌더를 계속할 수 있으므로 앱 초기화 자체를 중단하지 않는다.
    CreateDeepShadowResources(ShadowPreset());
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
            &colorDesc, nullptr, &m_hdrComposite)) ||
        FAILED(m_device->CreateRenderTargetView(
            m_hdrComposite.Get(), nullptr, &m_hdrCompositeRtv)) ||
        FAILED(m_device->CreateShaderResourceView(
            m_hdrComposite.Get(), nullptr, &m_hdrCompositeSrv)))
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
    return CreateStage15CaptureTarget();
}

bool Renderer::CreateStage15CaptureTarget()
{
    if (!m_device || m_width <= 0 || m_height <= 0)
        return false;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = static_cast<UINT>(m_width);
    desc.Height = static_cast<UINT>(m_height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET |
                     D3D11_BIND_SHADER_RESOURCE;

    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(m_device->CreateTexture2D(&desc, nullptr, &texture)) ||
        FAILED(m_device->CreateRenderTargetView(
            texture.Get(), nullptr, &rtv)) ||
        FAILED(m_device->CreateShaderResourceView(
            texture.Get(), nullptr, &srv)))
        return false;

    m_stage15CaptureAccumulator = texture;
    m_stage15CaptureAccumulatorRtv = rtv;
    m_stage15CaptureAccumulatorSrv = srv;
    if (m_context)
    {
        const float clear[4] = {};
        m_context->ClearRenderTargetView(
            m_stage15CaptureAccumulatorRtv.Get(), clear);
    }
    return true;
}

void Renderer::ReleaseStage15CaptureTarget()
{
    m_stage15CaptureAccumulatorSrv.Reset();
    m_stage15CaptureAccumulatorRtv.Reset();
    m_stage15CaptureAccumulator.Reset();
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

    // Temporal shader가 쓴 accepted/weight를 mip 평균으로 1x1까지 줄인다.
    // 지원하지 않는 특수 장치에서는 렌더를 계속하고 overlay만 N/A로 둔다.
    m_temporalStatistics.Reset();
    m_temporalStatisticsRtv.Reset();
    m_temporalStatisticsSrv.Reset();
    for (auto& staging : m_temporalStatisticsStaging)
        staging.Reset();
    m_temporalStatisticsPending = {};
    m_temporalStatisticsMipLevels = 0;
    m_temporalStatisticsWriteIndex = 0;
    m_temporalStatisticsValid = false;
    UINT formatSupport = 0;
    const bool statisticsSupported = SUCCEEDED(m_device->CheckFormatSupport(
        DXGI_FORMAT_R16G16_FLOAT, &formatSupport)) &&
        (formatSupport & D3D11_FORMAT_SUPPORT_RENDER_TARGET) != 0u &&
        (formatSupport & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) != 0u &&
        (formatSupport & D3D11_FORMAT_SUPPORT_MIP_AUTOGEN) != 0u;
    if (statisticsSupported)
    {
        D3D11_TEXTURE2D_DESC statisticsDesc = {};
        statisticsDesc.Width = static_cast<UINT>(m_width);
        statisticsDesc.Height = static_cast<UINT>(m_height);
        statisticsDesc.MipLevels = 0;
        statisticsDesc.ArraySize = 1;
        statisticsDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
        statisticsDesc.SampleDesc.Count = 1;
        statisticsDesc.Usage = D3D11_USAGE_DEFAULT;
        statisticsDesc.BindFlags = D3D11_BIND_RENDER_TARGET |
            D3D11_BIND_SHADER_RESOURCE;
        statisticsDesc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        ComPtr<ID3D11Texture2D> statistics;
        ComPtr<ID3D11RenderTargetView> statisticsRtv;
        ComPtr<ID3D11ShaderResourceView> statisticsSrv;
        if (SUCCEEDED(m_device->CreateTexture2D(
                &statisticsDesc, nullptr, &statistics)) &&
            SUCCEEDED(m_device->CreateRenderTargetView(
                statistics.Get(), nullptr, &statisticsRtv)) &&
            SUCCEEDED(m_device->CreateShaderResourceView(
                statistics.Get(), nullptr, &statisticsSrv)))
        {
            statistics->GetDesc(&statisticsDesc);
            D3D11_TEXTURE2D_DESC stagingDesc = {};
            stagingDesc.Width = 1;
            stagingDesc.Height = 1;
            stagingDesc.MipLevels = 1;
            stagingDesc.ArraySize = 1;
            stagingDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
            stagingDesc.SampleDesc.Count = 1;
            stagingDesc.Usage = D3D11_USAGE_STAGING;
            stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            bool stagingReady = true;
            std::array<ComPtr<ID3D11Texture2D>, 3> stagingTextures;
            for (auto& staging : stagingTextures)
            {
                if (FAILED(m_device->CreateTexture2D(
                        &stagingDesc, nullptr, &staging)))
                {
                    stagingReady = false;
                    break;
                }
            }
            if (stagingReady)
            {
                m_temporalStatistics = statistics;
                m_temporalStatisticsRtv = statisticsRtv;
                m_temporalStatisticsSrv = statisticsSrv;
                m_temporalStatisticsStaging = stagingTextures;
                m_temporalStatisticsMipLevels = statisticsDesc.MipLevels;
            }
        }
    }
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
                                     bool showErrors,
                                     const D3D_SHADER_MACRO* defines)
{
    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    compileFlags |= D3DCOMPILE_DEBUG;
    // 동적 단계 9 shader는 /Od에서 하드웨어 instruction 한도를 넘길 수 있다.
    // 실제 최적화 비용을 재는 경로이기도 하므로 Debug에서도 최소 O1을 사용한다.
    compileFlags |= (std::strcmp(entryPoint, "mainOptimized") == 0 ||
                     std::strcmp(entryPoint, "mainOptimizedData") == 0 ||
                     (std::strcmp(entryPoint, "main") == 0 &&
                     (path == m_deepShadowShaderPath ||
                      path == m_atmosphereLutShaderPath)))
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

bool Renderer::CreateShaders(bool showErrors)
{
    m_shaderError.clear();
    ComPtr<ID3DBlob> fullscreenVsBlob;
    ComPtr<ID3DBlob> cloudReferencePsBlob;
    ComPtr<ID3DBlob> cloudOptimizedPsBlob;
    ComPtr<ID3DBlob> cloudReferenceDataPsBlob;
    ComPtr<ID3DBlob> cloudOptimizedDataPsBlob;
    ComPtr<ID3DBlob> cirrusReferencePsBlob;
    ComPtr<ID3DBlob> cirrusOptimizedPsBlob;
    ComPtr<ID3DBlob> cirrusReferenceDataPsBlob;
    ComPtr<ID3DBlob> cirrusOptimizedDataPsBlob;
    ComPtr<ID3DBlob> cloudUpsamplePsBlob;
    ComPtr<ID3DBlob> cloudTemporalResolvePsBlob;
    ComPtr<ID3DBlob> cloudCompositePsBlob;
    ComPtr<ID3DBlob> cloudCompositeNoRimPsBlob;
    ComPtr<ID3DBlob> noiseLabPsBlob;
    ComPtr<ID3DBlob> sceneVsBlob;
    ComPtr<ID3DBlob> scenePsBlob;
    ComPtr<ID3DBlob> toneMapPsBlob;
    ComPtr<ID3DBlob> stage15CaptureAccumulatePsBlob;
    ComPtr<ID3DBlob> noiseBaseCsBlob;
    ComPtr<ID3DBlob> noiseDetailCsBlob;
    ComPtr<ID3DBlob> nonCirrusDeepShadowCsBlob;
    ComPtr<ID3DBlob> cirrusDeepShadowCsBlob;
    ComPtr<ID3DBlob> atmosphereTransmittanceCsBlob;
    ComPtr<ID3DBlob> atmosphereMultiScatteringCsBlob;
    ComPtr<ID3DBlob> atmosphereSkyViewCsBlob;
    ComPtr<ID3DBlob> atmosphereSkyIrradianceCsBlob;
    ComPtr<ID3DBlob> atmosphereAerialCsBlob;
    const D3D_SHADER_MACRO nonCirrusDefines[] = {
        { "VCLOUD_CIRRUS_VARIANT", "0" }, { nullptr, nullptr }
    };
    const D3D_SHADER_MACRO cirrusDefines[] = {
        { "VCLOUD_CIRRUS_VARIANT", "1" }, { nullptr, nullptr }
    };
    const D3D_SHADER_MACRO diagnosticShapeDefines[] = {
        { "VCLOUD_CIRRUS_VARIANT", "-1" }, { nullptr, nullptr }
    };
    const D3D_SHADER_MACRO compositeNoRimDefines[] = {
        { "VCLOUD_DISABLE_RIM", "1" }, { nullptr, nullptr }
    };
    if (!CompileShaderFromFile(m_fullscreenShaderPath, "main", "vs_5_0", fullscreenVsBlob, showErrors) ||
        !CompileShaderFromFile(m_cloudShaderPath, "mainReference", "ps_5_0", cloudReferencePsBlob, showErrors, nonCirrusDefines) ||
        !CompileShaderFromFile(m_cloudShaderPath, "mainOptimized", "ps_5_0", cloudOptimizedPsBlob, showErrors, nonCirrusDefines) ||
        !CompileShaderFromFile(m_cloudShaderPath, "mainReferenceData", "ps_5_0", cloudReferenceDataPsBlob, showErrors, nonCirrusDefines) ||
        !CompileShaderFromFile(m_cloudShaderPath, "mainOptimizedData", "ps_5_0", cloudOptimizedDataPsBlob, showErrors, nonCirrusDefines) ||
        !CompileShaderFromFile(m_cloudShaderPath, "mainReference", "ps_5_0", cirrusReferencePsBlob, showErrors, cirrusDefines) ||
        !CompileShaderFromFile(m_cloudShaderPath, "mainOptimized", "ps_5_0", cirrusOptimizedPsBlob, showErrors, cirrusDefines) ||
        !CompileShaderFromFile(m_cloudShaderPath, "mainReferenceData", "ps_5_0", cirrusReferenceDataPsBlob, showErrors, cirrusDefines) ||
        !CompileShaderFromFile(m_cloudShaderPath, "mainOptimizedData", "ps_5_0", cirrusOptimizedDataPsBlob, showErrors, cirrusDefines) ||
        !CompileShaderFromFile(m_cloudUpsampleShaderPath, "main", "ps_5_0", cloudUpsamplePsBlob, showErrors) ||
        !CompileShaderFromFile(m_cloudTemporalResolveShaderPath, "main", "ps_5_0", cloudTemporalResolvePsBlob, showErrors) ||
        !CompileShaderFromFile(m_cloudCompositeShaderPath, "main", "ps_5_0", cloudCompositePsBlob, showErrors) ||
        !CompileShaderFromFile(m_cloudCompositeShaderPath, "main", "ps_5_0", cloudCompositeNoRimPsBlob, showErrors, compositeNoRimDefines) ||
        !CompileShaderFromFile(m_noiseLabShaderPath, "main", "ps_5_0", noiseLabPsBlob, showErrors, diagnosticShapeDefines) ||
        !CompileShaderFromFile(m_sceneShaderPath, "VSMain", "vs_5_0", sceneVsBlob, showErrors) ||
        !CompileShaderFromFile(m_sceneShaderPath, "PSMain", "ps_5_0", scenePsBlob, showErrors) ||
        !CompileShaderFromFile(m_toneMapShaderPath, "main", "ps_5_0", toneMapPsBlob, showErrors) ||
        !CompileShaderFromFile(m_stage15CaptureAccumulateShaderPath, "main", "ps_5_0", stage15CaptureAccumulatePsBlob, showErrors) ||
        !CompileShaderFromFile(m_deepShadowShaderPath, "main", "cs_5_0", nonCirrusDeepShadowCsBlob, showErrors, nonCirrusDefines) ||
        !CompileShaderFromFile(m_deepShadowShaderPath, "main", "cs_5_0", cirrusDeepShadowCsBlob, showErrors, cirrusDefines) ||
        !CompileShaderFromFile(m_atmosphereLutShaderPath, "CSTransmittance", "cs_5_0", atmosphereTransmittanceCsBlob, showErrors) ||
        !CompileShaderFromFile(m_atmosphereLutShaderPath, "CSMultiScattering", "cs_5_0", atmosphereMultiScatteringCsBlob, showErrors) ||
        !CompileShaderFromFile(m_atmosphereLutShaderPath, "CSSkyView", "cs_5_0", atmosphereSkyViewCsBlob, showErrors) ||
        !CompileShaderFromFile(m_atmosphereLutShaderPath, "CSSkyIrradiance", "cs_5_0", atmosphereSkyIrradianceCsBlob, showErrors) ||
        !CompileShaderFromFile(m_atmosphereLutShaderPath, "CSAerialPerspective", "cs_5_0", atmosphereAerialCsBlob, showErrors) ||
        (m_noiseVolumesEnabled &&
         (!CompileShaderFromFile(m_noiseVolumeShaderPath, "CSBase", "cs_5_0", noiseBaseCsBlob, showErrors) ||
          !CompileShaderFromFile(m_noiseVolumeShaderPath, "CSDetail", "cs_5_0", noiseDetailCsBlob, showErrors))))
    {
        m_shaderStatus = "Reload failed; previous generation kept";
        return false;
    }
    if (!HasReflectedConstantBufferSize(
            cloudOptimizedDataPsBlob.Get(), "CloudShapeCB", 64u) ||
        !HasReflectedConstantBufferSize(
            nonCirrusDeepShadowCsBlob.Get(), "CloudShapeCB", 64u) ||
        !HasReflectedConstantBufferSize(
            cirrusOptimizedDataPsBlob.Get(), "CloudShapeCB", 112u) ||
        !HasReflectedConstantBufferSize(
            cirrusDeepShadowCsBlob.Get(), "CloudShapeCB", 112u) ||
        !HasReflectedConstantBufferSize(
            cloudCompositePsBlob.Get(), "CloudRimCB", 48u))
    {
        m_shaderError =
            "Stage 15 shader specialization reflection contract failed";
        m_shaderStatus = "Reload failed; previous generation kept";
        return false;
    }

    ComPtr<ID3D11VertexShader> fullscreenVs;
    ComPtr<ID3D11PixelShader> cloudReferencePs;
    ComPtr<ID3D11PixelShader> cloudOptimizedPs;
    ComPtr<ID3D11PixelShader> cloudReferenceDataPs;
    ComPtr<ID3D11PixelShader> cloudOptimizedDataPs;
    ComPtr<ID3D11PixelShader> cirrusReferencePs;
    ComPtr<ID3D11PixelShader> cirrusOptimizedPs;
    ComPtr<ID3D11PixelShader> cirrusReferenceDataPs;
    ComPtr<ID3D11PixelShader> cirrusOptimizedDataPs;
    ComPtr<ID3D11PixelShader> cloudUpsamplePs;
    ComPtr<ID3D11PixelShader> cloudTemporalResolvePs;
    ComPtr<ID3D11PixelShader> cloudCompositePs;
    ComPtr<ID3D11PixelShader> cloudCompositeNoRimPs;
    ComPtr<ID3D11PixelShader> noiseLabPs;
    ComPtr<ID3D11VertexShader> sceneVs;
    ComPtr<ID3D11PixelShader> scenePs;
    ComPtr<ID3D11PixelShader> toneMapPs;
    ComPtr<ID3D11PixelShader> stage15CaptureAccumulatePs;
    ComPtr<ID3D11ComputeShader> noiseBaseCs;
    ComPtr<ID3D11ComputeShader> noiseDetailCs;
    ComPtr<ID3D11ComputeShader> nonCirrusDeepShadowCs;
    ComPtr<ID3D11ComputeShader> cirrusDeepShadowCs;
    ComPtr<ID3D11ComputeShader> atmosphereTransmittanceCs;
    ComPtr<ID3D11ComputeShader> atmosphereMultiScatteringCs;
    ComPtr<ID3D11ComputeShader> atmosphereSkyViewCs;
    ComPtr<ID3D11ComputeShader> atmosphereSkyIrradianceCs;
    ComPtr<ID3D11ComputeShader> atmosphereAerialCs;
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
            cirrusReferencePsBlob->GetBufferPointer(), cirrusReferencePsBlob->GetBufferSize(),
            nullptr, &cirrusReferencePs)) ||
        FAILED(m_device->CreatePixelShader(
            cirrusOptimizedPsBlob->GetBufferPointer(), cirrusOptimizedPsBlob->GetBufferSize(),
            nullptr, &cirrusOptimizedPs)) ||
        FAILED(m_device->CreatePixelShader(
            cirrusReferenceDataPsBlob->GetBufferPointer(), cirrusReferenceDataPsBlob->GetBufferSize(),
            nullptr, &cirrusReferenceDataPs)) ||
        FAILED(m_device->CreatePixelShader(
            cirrusOptimizedDataPsBlob->GetBufferPointer(), cirrusOptimizedDataPsBlob->GetBufferSize(),
            nullptr, &cirrusOptimizedDataPs)) ||
        FAILED(m_device->CreatePixelShader(
            cloudUpsamplePsBlob->GetBufferPointer(), cloudUpsamplePsBlob->GetBufferSize(),
            nullptr, &cloudUpsamplePs)) ||
        FAILED(m_device->CreatePixelShader(
            cloudTemporalResolvePsBlob->GetBufferPointer(),
            cloudTemporalResolvePsBlob->GetBufferSize(), nullptr,
            &cloudTemporalResolvePs)) ||
        FAILED(m_device->CreatePixelShader(
            cloudCompositePsBlob->GetBufferPointer(),
            cloudCompositePsBlob->GetBufferSize(), nullptr,
            &cloudCompositePs)) ||
        FAILED(m_device->CreatePixelShader(
            cloudCompositeNoRimPsBlob->GetBufferPointer(),
            cloudCompositeNoRimPsBlob->GetBufferSize(), nullptr,
            &cloudCompositeNoRimPs)) ||
        FAILED(m_device->CreatePixelShader(
            noiseLabPsBlob->GetBufferPointer(), noiseLabPsBlob->GetBufferSize(),
            nullptr, &noiseLabPs)) ||
        FAILED(m_device->CreateVertexShader(
            sceneVsBlob->GetBufferPointer(), sceneVsBlob->GetBufferSize(),
            nullptr, &sceneVs)) ||
        FAILED(m_device->CreatePixelShader(
            scenePsBlob->GetBufferPointer(), scenePsBlob->GetBufferSize(),
            nullptr, &scenePs)) ||
        FAILED(m_device->CreatePixelShader(
            toneMapPsBlob->GetBufferPointer(), toneMapPsBlob->GetBufferSize(),
            nullptr, &toneMapPs)) ||
        FAILED(m_device->CreatePixelShader(
            stage15CaptureAccumulatePsBlob->GetBufferPointer(),
            stage15CaptureAccumulatePsBlob->GetBufferSize(), nullptr,
            &stage15CaptureAccumulatePs)) ||
        FAILED(m_device->CreateComputeShader(
            nonCirrusDeepShadowCsBlob->GetBufferPointer(),
            nonCirrusDeepShadowCsBlob->GetBufferSize(), nullptr,
            &nonCirrusDeepShadowCs)) ||
        FAILED(m_device->CreateComputeShader(
            cirrusDeepShadowCsBlob->GetBufferPointer(),
            cirrusDeepShadowCsBlob->GetBufferSize(), nullptr,
            &cirrusDeepShadowCs)) ||
        FAILED(m_device->CreateComputeShader(
            atmosphereTransmittanceCsBlob->GetBufferPointer(),
            atmosphereTransmittanceCsBlob->GetBufferSize(), nullptr,
            &atmosphereTransmittanceCs)) ||
        FAILED(m_device->CreateComputeShader(
            atmosphereMultiScatteringCsBlob->GetBufferPointer(),
            atmosphereMultiScatteringCsBlob->GetBufferSize(), nullptr,
            &atmosphereMultiScatteringCs)) ||
        FAILED(m_device->CreateComputeShader(
            atmosphereSkyViewCsBlob->GetBufferPointer(),
            atmosphereSkyViewCsBlob->GetBufferSize(), nullptr,
            &atmosphereSkyViewCs)) ||
        FAILED(m_device->CreateComputeShader(
            atmosphereSkyIrradianceCsBlob->GetBufferPointer(),
            atmosphereSkyIrradianceCsBlob->GetBufferSize(), nullptr,
            &atmosphereSkyIrradianceCs)) ||
        FAILED(m_device->CreateComputeShader(
            atmosphereAerialCsBlob->GetBufferPointer(),
            atmosphereAerialCsBlob->GetBufferSize(), nullptr,
            &atmosphereAerialCs)) ||
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
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24,
          D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "MATERIALID", 0, DXGI_FORMAT_R32_UINT, 0, 36,
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
    m_cirrusReferencePs = cirrusReferencePs;
    m_cirrusOptimizedPs = cirrusOptimizedPs;
    m_cirrusReferenceDataPs = cirrusReferenceDataPs;
    m_cirrusOptimizedDataPs = cirrusOptimizedDataPs;
    m_cloudUpsamplePs = cloudUpsamplePs;
    m_cloudTemporalResolvePs = cloudTemporalResolvePs;
    m_cloudCompositePs = cloudCompositePs;
    m_cloudCompositeNoRimPs = cloudCompositeNoRimPs;
    m_noiseLabPs = noiseLabPs;
    m_sceneVs = sceneVs;
    m_scenePs = scenePs;
    m_toneMapPs = toneMapPs;
    m_stage15CaptureAccumulatePs = stage15CaptureAccumulatePs;
    // 두 variant가 모두 컴파일·생성된 뒤에만 같은 shader 세대로 교체한다.
    m_nonCirrusDeepShadowCs = nonCirrusDeepShadowCs;
    m_cirrusDeepShadowCs = cirrusDeepShadowCs;
    m_nonCirrusRaymarchShaderHash = 1469598103934665603ull;
    HashBytes(m_nonCirrusRaymarchShaderHash,
              cloudOptimizedDataPsBlob->GetBufferPointer(),
              cloudOptimizedDataPsBlob->GetBufferSize());
    m_nonCirrusDeepShadowShaderHash = 1469598103934665603ull;
    HashBytes(m_nonCirrusDeepShadowShaderHash,
              nonCirrusDeepShadowCsBlob->GetBufferPointer(),
              nonCirrusDeepShadowCsBlob->GetBufferSize());
    m_cirrusDeepShadowShaderHash = 1469598103934665603ull;
    HashBytes(m_cirrusDeepShadowShaderHash,
              cirrusDeepShadowCsBlob->GetBufferPointer(),
              cirrusDeepShadowCsBlob->GetBufferSize());
    m_cloudCompositeShaderHash = 1469598103934665603ull;
    HashBytes(m_cloudCompositeShaderHash,
              cloudCompositePsBlob->GetBufferPointer(),
              cloudCompositePsBlob->GetBufferSize());
    HashBytes(m_cloudCompositeShaderHash,
              cloudCompositeNoRimPsBlob->GetBufferPointer(),
              cloudCompositeNoRimPsBlob->GetBufferSize());
    m_atmosphereTransmittanceCs = atmosphereTransmittanceCs;
    m_atmosphereMultiScatteringCs = atmosphereMultiScatteringCs;
    m_atmosphereSkyViewCs = atmosphereSkyViewCs;
    m_atmosphereSkyIrradianceCs = atmosphereSkyIrradianceCs;
    m_atmosphereAerialCs = atmosphereAerialCs;
    // 새 shader 세대는 기존 정상 LUT를 보존하되 다음 프레임에 원자 재생성한다.
    std::fill(std::begin(m_atmosphereLutHashes),
              std::end(m_atmosphereLutHashes), 0ull);
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
    if (FAILED(m_device->CreateSamplerState(
            &samplerDesc, &m_weatherLinearWrapSampler)))
        return false;

    // Capture의 n번째 HDR sample은 alpha가 아니라 고정 blend factor
    // 1/(n+1)을 사용해 R32G32B32A32_FLOAT 누적 표면에 running average한다.
    D3D11_BLEND_DESC captureBlendDesc = {};
    captureBlendDesc.RenderTarget[0].BlendEnable = TRUE;
    captureBlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_BLEND_FACTOR;
    captureBlendDesc.RenderTarget[0].DestBlend =
        D3D11_BLEND_INV_BLEND_FACTOR;
    captureBlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    captureBlendDesc.RenderTarget[0].SrcBlendAlpha =
        D3D11_BLEND_BLEND_FACTOR;
    captureBlendDesc.RenderTarget[0].DestBlendAlpha =
        D3D11_BLEND_INV_BLEND_FACTOR;
    captureBlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    captureBlendDesc.RenderTarget[0].RenderTargetWriteMask =
        D3D11_COLOR_WRITE_ENABLE_ALL;
    return SUCCEEDED(m_device->CreateBlendState(
        &captureBlendDesc, &m_stage15CaptureBlendState));
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
    for (std::size_t index = 0; index < m_stage15WeatherMaps.size(); ++index)
    {
        const auto preset = static_cast<Stage15ConceptPreset>(index);
        const Stage15ConceptDescriptor descriptor = stage15::ResolveConcept(preset);
        WeatherMapData map = BuildWeatherMap(
            descriptor.formation.weatherPreset,
            descriptor.formation.weather);
        if (!IsValidWeatherMapData(map))
        {
            m_weatherMapStatus = "Stage 15 Weather cache generation failed";
            return false;
        }
        m_stage15WeatherHashes[index] = HashWeatherMap(map);
        m_stage15WeatherMaps[index] = std::move(map);
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
    {
        ResetTemporalHistory(Stage11HistoryResetReason::ParametersChanged);
        m_stage15ConceptPreset = Stage15ConceptPreset::Custom;
    }
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
           createDynamicBuffer(sizeof(CloudRimParameters),
                               &m_cloudRimCb) &&
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
    gpu.modeFlags = {
        static_cast<std::uint32_t>(atmosphere.mode),
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
    // schema 34 자동 회귀처럼 첫 프레임부터 Manual Reference인 경우에는
    // Stage 14가 생기기 전 LightParameters 방향을 Angle 원본으로 승격한다.
    // Physical을 한 번이라도 사용한 일반 실행은 이미 Angle이 원본이므로
    // 이후 Manual 전환에서도 같은 방향을 그대로 유지한다.
    if (!m_stage14SunDirectionInitialized &&
        m_atmosphereParameters.mode == AtmosphereMode::ManualReference)
    {
        stage6light::AnglesFromDirection(
            m_lightParameters.directionToSun,
            m_atmosphereParameters.sunAzimuthDegrees,
            m_atmosphereParameters.sunElevationDegrees);
    }
    const stage14math::Float3 sun = stage14math::DirectionFromAngles(
        m_atmosphereParameters.sunAzimuthDegrees,
        m_atmosphereParameters.sunElevationDegrees);
    // Physical/Manual 모두 같은 각도를 사용해야 방향 도식과 실제 그림자가
    // 서로 다른 원본을 참조하지 않는다.
    m_lightParameters.directionToSun = { sun.x, sun.y, sun.z };
    m_stage14SunDirectionInitialized = true;
    m_stage14GpuParameters = BuildStage14GpuParameters(camera);

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (m_stage14Cb && SUCCEEDED(m_context->Map(
            m_stage14Cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        std::memcpy(mapped.pData, &m_stage14GpuParameters,
                    sizeof(m_stage14GpuParameters));
        m_context->Unmap(m_stage14Cb.Get(), 0);
    }

    if (m_atmosphereParameters.mode != AtmosphereMode::Physical)
        return m_atmosphereLutsValid;
    if (!m_atmosphereTransmittanceCs || !m_atmosphereMultiScatteringCs ||
        !m_atmosphereSkyViewCs || !m_atmosphereSkyIrradianceCs ||
        !m_atmosphereAerialCs)
    {
        if (!m_atmosphereLutsValid)
            m_atmosphereParameters.mode = AtmosphereMode::ManualReference;
        m_atmosphereStatus = "Physical disabled: LUT shaders unavailable";
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
        if (!m_atmosphereLutsValid)
            m_atmosphereParameters.mode = AtmosphereMode::ManualReference;
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
    m_context->CSSetConstantBuffers(13, 1, &stage14Buffer);
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
    m_context->PSSetConstantBuffers(13, 1, &stage14Buffer);
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
    m_hdrCompositeSrv.Reset();
    m_hdrCompositeRtv.Reset();
    m_hdrComposite.Reset();
    ReleaseStage15CaptureTarget();
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
    m_temporalStatisticsSrv.Reset();
    m_temporalStatisticsRtv.Reset();
    m_temporalStatistics.Reset();
    for (auto& staging : m_temporalStatisticsStaging)
        staging.Reset();
    m_temporalStatisticsPending = {};
    m_temporalStatisticsMipLevels = 0;
    m_temporalStatisticsValid = false;
    m_cloudRenderWidth = 0;
    m_cloudRenderHeight = 0;
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

    if (SUCCEEDED(m_swapChain->ResizeBuffers(
            0, width, height, DXGI_FORMAT_UNKNOWN, 0)) &&
        CreateBackBufferTarget() && CreateSceneTargets() &&
        CreateCloudTargets() && CreateTemporalHistoryTargets())
    {
        m_sizeDependentResourcesValid = true;
        D3D11_VIEWPORT resizedViewport = {};
        resizedViewport.Width = static_cast<float>(m_width);
        resizedViewport.Height = static_cast<float>(m_height);
        resizedViewport.MaxDepth = 1.0f;
        m_context->RSSetViewports(1, &resizedViewport);
        ResetTemporalHistory(Stage11HistoryResetReason::Resize);
        if (m_stage15CaptureState == Stage15CaptureState::Accumulating ||
            m_stage15CaptureState == Stage15CaptureState::Ready)
        {
            m_stage15CaptureState = Stage15CaptureState::Failed;
            m_stage15CaptureFreezeValid = false;
            m_stage15CaptureStatus =
                "Capture cancelled because output size changed";
        }
    }
}

void Renderer::UpdateWindowMetrics(int physicalWidth, int physicalHeight,
                                   unsigned int dpi, float dpiScale,
                                   bool perMonitorV2, bool native1080p)
{
    // WM_SIZE 할당이 일시적으로 실패했거나 메시지가 합쳐졌더라도 매 프레임
    // 실측 physical client를 기준으로 같은 크기 재생성을 한 번 더 시도한다.
    if (physicalWidth > 0 && physicalHeight > 0 &&
        (!m_sizeDependentResourcesValid || m_width != physicalWidth ||
         m_height != physicalHeight))
    {
        Resize(physicalWidth, physicalHeight);
    }
    m_outputExtent.physicalClientWidth = std::max(physicalWidth, 0);
    m_outputExtent.physicalClientHeight = std::max(physicalHeight, 0);
    m_outputExtent.dpi = dpi;
    m_outputExtent.dpiScale = std::isfinite(dpiScale)
        ? std::max(dpiScale, 0.0f) : 0.0f;
    m_outputExtent.perMonitorV2 = perMonitorV2;
    m_outputExtent.native1080p = native1080p;
    // 렌더 target 크기와 무관한 UI 상태다. 같은 DPI면 no-op이고 history/LUT/
    // Stage 15 fingerprint에는 참여하지 않는다.
    m_noiseLab.SetDpi(dpi);
}

Stage15OutputExtentSnapshot Renderer::OutputExtentSnapshot() const
{
    Stage15OutputExtentSnapshot result = m_outputExtent;
    const auto readExtent = [](ID3D11Texture2D* texture,
                               int& width, int& height)
    {
        if (!texture)
            return;
        D3D11_TEXTURE2D_DESC desc = {};
        texture->GetDesc(&desc);
        width = static_cast<int>(desc.Width);
        height = static_cast<int>(desc.Height);
    };

    ComPtr<ID3D11Texture2D> backBuffer;
    if (m_swapChain && SUCCEEDED(m_swapChain->GetBuffer(
            0, IID_PPV_ARGS(&backBuffer))))
        readExtent(backBuffer.Get(), result.swapChainWidth,
                   result.swapChainHeight);
    D3D11_VIEWPORT viewport = {};
    UINT viewportCount = 1u;
    if (m_context)
        m_context->RSGetViewports(&viewportCount, &viewport);
    if (viewportCount == 1u && std::isfinite(viewport.Width) &&
        std::isfinite(viewport.Height))
    {
        result.viewportWidth = static_cast<int>(std::lround(viewport.Width));
        result.viewportHeight = static_cast<int>(std::lround(viewport.Height));
    }
    readExtent(m_sceneColor.Get(), result.sceneColorWidth,
               result.sceneColorHeight);
    readExtent(m_sceneDepth.Get(), result.sceneDepthWidth,
               result.sceneDepthHeight);
    readExtent(m_cloudScatteringTransmittance.Get(), result.cloudWidth,
               result.cloudHeight);
    readExtent(m_temporalHistoryCloud[0].Get(), result.historyWidth,
               result.historyHeight);
    return result;
}

bool Renderer::ConsumeNative1080pRequest(bool& enable)
{
    if (m_native1080pRequest < 0)
        return false;
    enable = m_native1080pRequest != 0;
    m_native1080pRequest = -1;
    return true;
}

void Renderer::NotifyNative1080pResult(bool requestedEnable, bool succeeded,
                                       bool active,
                                       const std::string& status)
{
    m_outputExtent.native1080p = active;
    if (!requestedEnable)
    {
        if (succeeded && !active)
        {
            m_stage15CaptureOwnsNative1080p = false;
        }
        else if (m_stage15CaptureOwnsNative1080p)
        {
            // Win32 style/placement/client 중 하나라도 복원되지 않았으면 Capture
            // ownership을 유지하고 다음 frame에 같은 restore를 재시도한다.
            m_native1080pRequest = 0;
            m_stage15CaptureState = Stage15CaptureState::Failed;
            m_stage15CaptureFreezeValid = false;
        }
        if (!status.empty())
            m_stage15CaptureStatus = status;
        return;
    }

    // Win32 전환 자체는 성공했지만 렌더 리소스 재생성이 실패할 수 있다.
    // 이 경우에도 Capture가 연 Native 창의 소유권을 먼저 기록해야 다음
    // 프레임에서 안전하게 windowed 출력으로 되돌릴 수 있다.
    m_stage15CaptureOwnsNative1080p = active;
    if (!succeeded || !active)
    {
        m_stage15CaptureState = Stage15CaptureState::Failed;
        m_stage15CaptureFreezeValid = false;
        m_stage15CaptureStatus = status.empty()
            ? "Native 1920x1080 transition failed" : status;
        if (active)
            m_native1080pRequest = 0;
        return;
    }

    if (m_stage15DiagnosticMode != Stage15DiagnosticMode::CaptureStill)
        return;
    if (!ApplyStage15QualityDescriptor(
            stage15::ResolveCaptureQuality(), m_stage15QualityPreset,
            false, true))
    {
        m_stage15CaptureState = Stage15CaptureState::Failed;
        m_stage15CaptureFreezeValid = false;
        m_stage15CaptureStatus = "Capture quality preflight failed";
        return;
    }
    m_stage15CaptureState = Stage15CaptureState::PendingNative;
    m_stage15CaptureStatus = status.empty()
        ? "Native 1920x1080 ready" : status;
}

bool Renderer::SceneInputLocked() const
{
    return m_stage15DiagnosticMode == Stage15DiagnosticMode::CaptureStill &&
        m_stage15CaptureState != Stage15CaptureState::Inactive &&
        m_stage15CaptureState != Stage15CaptureState::Failed;
}

std::uint32_t Renderer::TemporalResetCountLast60Frames() const
{
    std::uint32_t count = 0;
    for (std::uint64_t frame : m_temporalResetFrames)
    {
        if (frame != 0 && frame <= m_renderFrameSerial + 1u &&
            m_renderFrameSerial + 1u - frame < 60u)
            ++count;
    }
    return count;
}

void Renderer::RenderDiagnosticScene(const Camera& camera,
                                     DirectX::XMFLOAT2 jitterPixels)
{
    SceneCB scene = {};
    XMStoreFloat4x4(
        &scene.viewProj,
        XMMatrixTranspose(camera.GetViewProj(
            jitterPixels.x, jitterPixels.y, m_width, m_height)));
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
    m_context->PSSetConstantBuffers(12, 1, &shadowBuffer);
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
        targetOverride ? targetOverride : m_hdrCompositeRtv.Get();
    m_context->OMSetRenderTargets(1, &cloudTarget, nullptr);
    m_context->ClearRenderTargetView(cloudTarget, clearColor);
    m_context->OMSetDepthStencilState(nullptr, 0);
    m_context->RSSetState(nullptr);

    m_context->IASetInputLayout(nullptr);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_fullscreenVs.Get(), nullptr, 0);
    const bool cirrus = static_cast<CloudShapeMode>(
        m_cloudShapeParameters.shapeMode) ==
        CloudShapeMode::CirrusPhysicalLayer;
    const bool reference =
        stage9optimization::UsesReferenceShader(m_optimizationParameters);
    ID3D11PixelShader* cloudShader = cirrus
        ? (reference ? m_cirrusReferencePs.Get() : m_cirrusOptimizedPs.Get())
        : (reference ? m_cloudReferencePs.Get() : m_cloudOptimizedPs.Get());
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
    const bool resourcesReady = m_nonCirrusDeepShadowCs &&
        m_cirrusDeepShadowCs && m_shadowNearSrv &&
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
    const bool cirrusShape = static_cast<CloudShapeMode>(
        m_cloudShapeParameters.shapeMode) ==
        CloudShapeMode::CirrusPhysicalLayer;
    ID3D11ComputeShader* deepShadowShader = cirrusShape
        ? m_cirrusDeepShadowCs.Get() : m_nonCirrusDeepShadowCs.Get();
    m_context->CSSetShader(deepShadowShader, nullptr, 0);

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
                                          int renderHeight,
                                          DirectX::XMFLOAT2 jitterPixels)
{
    CameraCB cameraData = {};
    XMStoreFloat4x4(&cameraData.invViewProj,
                    XMMatrixTranspose(camera.GetInvViewProj(
                        jitterPixels.x, jitterPixels.y,
                        renderWidth, renderHeight)));
    XMStoreFloat4x4(&cameraData.invProjection,
                    XMMatrixTranspose(camera.GetInvProjection(
                        jitterPixels.x, jitterPixels.y,
                        renderWidth, renderHeight)));
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
    update(7, m_cloudLodCb.Get(), &m_cloudLodParameters,
           sizeof(m_cloudLodParameters));
    update(8, m_optimizationCb.Get(), &m_optimizationParameters,
           sizeof(m_optimizationParameters));
    update(9, m_upsamplingCb.Get(), &m_upsamplingParameters,
           sizeof(m_upsamplingParameters));
    update(10, m_temporalCb.Get(), &m_temporalParameters,
           sizeof(m_temporalParameters));
    update(11, m_shadowCb.Get(), &m_shadowParameters,
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
}

void Renderer::UnbindCloudShaderResources(UINT count)
{
    std::vector<ID3D11ShaderResourceView*> nullResources(count, nullptr);
    m_context->PSSetShaderResources(0, count, nullResources.data());
}

void Renderer::RenderCloudDataPass(const Camera& camera, float timeSeconds,
                                   DirectX::XMFLOAT2 jitterPixels)
{
    if (!EnsureCloudTargets())
        return;
    const std::int32_t savedDebugMode = m_cloudParameters.debugMode;
    m_cloudParameters.debugMode = static_cast<std::int32_t>(
        CloudDebugMode::Composite);
    UpdateCloudConstantBuffers(camera, timeSeconds,
                               m_cloudRenderWidth, m_cloudRenderHeight,
                               jitterPixels);
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

    const bool cirrus = static_cast<CloudShapeMode>(
        m_cloudShapeParameters.shapeMode) ==
        CloudShapeMode::CirrusPhysicalLayer;
    const bool reference =
        stage9optimization::UsesReferenceShader(m_optimizationParameters);
    ID3D11PixelShader* shader = cirrus
        ? (reference ? m_cirrusReferenceDataPs.Get()
                     : m_cirrusOptimizedDataPs.Get())
        : (reference ? m_cloudReferenceDataPs.Get()
                     : m_cloudOptimizedDataPs.Get());
    BindCloudRaymarchResources(shader);
    m_context->Draw(3, 0);
    UnbindCloudShaderResources(14);
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
}

void Renderer::RenderCloudUpsamplePass(const Camera& camera,
                                       float timeSeconds,
                                       ID3D11RenderTargetView* targetOverride,
                                       DirectX::XMFLOAT2 jitterPixels)
{
    const std::uint32_t resolvedIndex = 1u - m_temporalHistoryReadIndex;
    if (!m_cloudUpsamplePs ||
        !m_temporalHistoryCloudRtv[resolvedIndex] ||
        !m_temporalHistoryAuxRtv[resolvedIndex] ||
        !m_temporalHistoryCloudSrv[resolvedIndex] ||
        !m_temporalHistoryAuxSrv[resolvedIndex])
        return;
    UpdateCloudConstantBuffers(
        camera, timeSeconds, m_width, m_height, jitterPixels);
    ID3D11RenderTargetView* target = targetOverride
        ? targetOverride : m_hdrCompositeRtv.Get();
    const CloudDebugMode mode = DebugMode();
    const bool requiresComposite =
        mode == CloudDebugMode::Composite ||
        mode == CloudDebugMode::NteRimMask ||
        mode == CloudDebugMode::NteRimContribution;
    const float clearColor[4] = { 0.02f, 0.03f, 0.05f, 1.0f };
    ID3D11RenderTargetView* targets[3] = {
        m_temporalHistoryCloudRtv[resolvedIndex].Get(),
        m_temporalHistoryAuxRtv[resolvedIndex].Get(), target
    };
    // Composite가 최종 HDR을 다시 쓰는 모드에서는 resolve pair 두 장만
    // 기록한다. 쓰지 않을 SV_TARGET2를 full-resolution으로 한 번 더 clear/write
    // 하지 않아 Resolve와 Composite 비용을 명확히 분리한다.
    m_context->OMSetRenderTargets(requiresComposite ? 2u : 3u, targets, nullptr);
    if (!requiresComposite)
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
    BindAtmosphereResources();
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
    UnbindCloudShaderResources(14);
    m_context->OMSetRenderTargets(0, nullptr, nullptr);

    if (requiresComposite)
    {
        RenderCloudCompositePass(
            camera, timeSeconds,
            m_temporalHistoryCloudSrv[resolvedIndex].Get(),
            m_temporalHistoryAuxSrv[resolvedIndex].Get(), target);
    }
}

void Renderer::RenderCloudCompositePass(
    const Camera& camera, float timeSeconds,
    ID3D11ShaderResourceView* resolvedCloud,
    ID3D11ShaderResourceView* resolvedAux,
    ID3D11RenderTargetView* targetOverride)
{
    (void)camera;
    (void)timeSeconds;
    if (!m_cloudCompositePs || !m_cloudCompositeNoRimPs ||
        !resolvedCloud || !resolvedAux)
        return;

    ID3D11RenderTargetView* target = targetOverride
        ? targetOverride : m_hdrCompositeRtv.Get();
    if (!target)
        return;

    CloudRimParameters effectiveRim = cloudrim::Sanitize(
        m_cloudRimParameters);
    const bool realtimeLow =
        m_stage15DiagnosticMode == Stage15DiagnosticMode::None &&
        m_stage15QualityPreset == Stage15QualityPreset::Low;
    if (realtimeLow ||
        m_stage15DiagnosticMode == Stage15DiagnosticMode::Reference)
    {
        // Off는 enabled 한 필드에만 의존하지 않고 비용/출력 계수도 0으로
        // 만든다. pass-local b10을 다른 resolve pass의 b10과 교대 바인딩하는
        // D3D11 경로에서도 stale enable 하나가 외곽을 되살릴 수 없다.
        effectiveRim.enabled = 0u;
        effectiveRim.widthPixels = 0.0f;
        effectiveRim.intensity = 0.0f;
        effectiveRim.radianceClamp = 0.0f;
    }
    m_lastEffectiveRimEnabled = effectiveRim.enabled != 0u;
    const bool useNoRimShader = effectiveRim.enabled == 0u;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    m_lastRimUploadSucceeded = false;
    if (FAILED(m_context->Map(
            m_cloudRimCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return;
    std::memcpy(mapped.pData, &effectiveRim, sizeof(effectiveRim));
    m_context->Unmap(m_cloudRimCb.Get(), 0);
    m_lastRimUploadSucceeded = true;

    // Resolve/history pair가 완전히 확정된 직후부터 target clear와 full-screen
    // composite draw 전체를 Composite 비용으로 잰다. 이 timestamp보다 앞선
    // 명령만 Spatial/Temporal Resolve 구간에 속한다.
    m_frameProfiler.MarkCloudCompositeBegin(m_context.Get());
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
    m_context->PSSetShader(
        useNoRimShader ? m_cloudCompositeNoRimPs.Get()
                       : m_cloudCompositePs.Get(),
        nullptr, 0);

    ID3D11Buffer* cameraAndCloud[2] = { m_cameraCb.Get(), m_cloudCb.Get() };
    m_context->PSSetConstantBuffers(0, 2, cameraAndCloud);
    ID3D11Buffer* lightBuffer = m_lightCb.Get();
    m_context->PSSetConstantBuffers(3, 1, &lightBuffer);
    ID3D11Buffer* rimBuffer = m_cloudRimCb.Get();
    m_context->PSSetConstantBuffers(
        cloudrim::kConstantBufferSlot, 1, &rimBuffer);
    ID3D11Buffer* shadowBuffer = m_shadowCb.Get();
    m_context->PSSetConstantBuffers(12, 1, &shadowBuffer);
    BindAtmosphereResources();

    ID3D11ShaderResourceView* resources[4] = {
        m_sceneColorSrv.Get(), m_sceneDepthSrv.Get(),
        resolvedCloud, resolvedAux
    };
    m_context->PSSetShaderResources(0, 4, resources);
    ID3D11ShaderResourceView* shadowResources[2] = {
        m_shadowNearSrv.Get(), m_shadowFarSrv.Get()
    };
    m_context->PSSetShaderResources(6, 2, shadowResources);
    ID3D11SamplerState* linearSampler = m_linearClampSampler.Get();
    m_context->PSSetSamplers(0, 1, &linearSampler);
    m_context->PSSetSamplers(2, 1, &linearSampler);
    m_context->Draw(3, 0);
    UnbindCloudShaderResources(14);
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    m_frameProfiler.EndCloudPass(m_context.Get());
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
        ? targetOverride : m_hdrCompositeRtv.Get();
    const CloudDebugMode mode = DebugMode();
    const bool requiresComposite =
        mode == CloudDebugMode::Composite ||
        mode == CloudDebugMode::NteRimMask ||
        mode == CloudDebugMode::NteRimContribution;
    ID3D11RenderTargetView* targets[4] = {
        m_temporalHistoryCloudRtv[writeIndex].Get(),
        m_temporalHistoryAuxRtv[writeIndex].Get(),
        requiresComposite ? nullptr : outputTarget,
        m_temporalStatisticsRtv.Get()
    };
    // history valid/weight 통계는 사용자 overlay용 관측 리소스다. 자동 품질·
    // 성능 fixture는 값을 읽지 않으므로 1080p RG16F 4번째 MRT와 전체 mip-chain
    // reduction을 렌더 비용에서 제외한다.
    const bool collectTemporalStatistics =
        !m_automatedRenderMode && m_temporalStatisticsRtv.Get() != nullptr;
    const float clearColor[4] = { 0.02f, 0.03f, 0.05f, 1.0f };
    const UINT targetCount = collectTemporalStatistics
        ? 4u : (requiresComposite ? 2u : 3u);
    m_context->OMSetRenderTargets(targetCount, targets, nullptr);
    if (!requiresComposite)
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
    BindAtmosphereResources();
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
    UnbindCloudShaderResources(14);
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    if (requiresComposite)
    {
        RenderCloudCompositePass(
            camera, timeSeconds,
            m_temporalHistoryCloudSrv[writeIndex].Get(),
            m_temporalHistoryAuxSrv[writeIndex].Get(), outputTarget);
    }
    if (collectTemporalStatistics)
        UpdateTemporalStatisticsReadback();
    CommitTemporalFrame(camera, timeSeconds);
    return true;
}

void Renderer::UpdateTemporalStatisticsReadback()
{
    if (!m_temporalStatistics || !m_temporalStatisticsSrv ||
        m_temporalStatisticsMipLevels == 0u)
        return;
    const std::uint32_t slot = m_temporalStatisticsWriteIndex %
        static_cast<std::uint32_t>(m_temporalStatisticsStaging.size());
    ID3D11Texture2D* staging = m_temporalStatisticsStaging[slot].Get();
    if (!staging)
        return;

    if (m_temporalStatisticsPending[slot])
    {
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        const HRESULT mapResult = m_context->Map(
            staging, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
        if (FAILED(mapResult))
            return;
        const auto* halfValues =
            static_cast<const std::uint16_t*>(mapped.pData);
        const float validFraction =
            DirectX::PackedVector::XMConvertHalfToFloat(halfValues[0]);
        const float averageWeight =
            DirectX::PackedVector::XMConvertHalfToFloat(halfValues[1]);
        m_context->Unmap(staging, 0);
        m_temporalHistoryValidPercent = std::clamp(
            validFraction * 100.0f, 0.0f, 100.0f);
        m_temporalAverageHistoryWeight = std::clamp(
            averageWeight, 0.0f, 1.0f);
        m_temporalStatisticsValid = std::isfinite(validFraction) &&
            std::isfinite(averageWeight);
        m_temporalStatisticsPending[slot] = false;
    }

    m_context->GenerateMips(m_temporalStatisticsSrv.Get());
    m_context->CopySubresourceRegion(
        staging, 0, 0, 0, 0, m_temporalStatistics.Get(),
        m_temporalStatisticsMipLevels - 1u, nullptr);
    m_temporalStatisticsPending[slot] = true;
    m_temporalStatisticsWriteIndex = (slot + 1u) %
        static_cast<std::uint32_t>(m_temporalStatisticsStaging.size());
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
    // Offscreen 진단도 일반 Render와 같은 b13/LUT 상태를 먼저 준비해야 한다.
    // Manual Reference에서는 LUT를 만들지 않더라도 mode flag를 업로드한다.
    EnsureAtmosphereLuts(camera);
    RenderDiagnosticScene(camera);
    RenderDeepShadowCaches(camera, timeSeconds);
    const bool requiresResolvedCloudPath =
        mode == CloudDebugMode::Composite ||
        mode == CloudDebugMode::Transmittance ||
        mode == CloudDebugMode::Stage15ResolvedCloud ||
        mode == CloudDebugMode::UpsampleAcceptedTapCount ||
        mode == CloudDebugMode::NteRimMask ||
        mode == CloudDebugMode::NteRimContribution ||
        (static_cast<std::int32_t>(mode) >= 64 &&
         static_cast<std::int32_t>(mode) <= 73);
    if (!forceDirectComposite && requiresResolvedCloudPath)
    {
        // 품질 fixture가 요청한 Half/Spatial/Temporal 리소스 준비에 실패했을
        // 때 Full direct 결과로 조용히 바꾸면 잘못된 후보가 통과한다.
        if (!EnsureCloudTargets())
        {
            SetDebugMode(previousMode);
            return false;
        }
        const std::int32_t diagnosticValue =
            static_cast<std::int32_t>(mode);
        const bool temporalDebug =
            diagnosticValue >= 68 && diagnosticValue <= 73;
        if (TemporalMode() != Stage11TemporalMode::Off || temporalDebug)
            PrepareTemporalFrame(camera, timeSeconds);
        RenderCloudDataPass(camera, timeSeconds);
        if ((TemporalMode() == Stage11TemporalMode::Off && !temporalDebug) ||
            !RenderCloudTemporalPass(camera, timeSeconds, targetRtv.Get()))
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
    // schema 34 이하의 회귀 fixture는 Composite가 Back Buffer에 직접 쓰이던
    // 시절의 0.8-knee shoulder 값을 비교한다. 현재 일반 경로는 HDR을 보존하고
    // 최종 pass에서 처리하므로 readback에만 동일 곡선을 재현한다.
    if (mode == CloudDebugMode::Composite &&
        m_toneMappingParameters.mode == ToneMappingMode::LegacyShoulder)
    {
        for (DirectX::XMFLOAT4& pixel : frame.pixels)
        {
            const float peak = std::max(pixel.x, std::max(pixel.y, pixel.z));
            if (peak <= 0.8f)
                continue;
            const float excess = peak - 0.8f;
            const float mappedPeak = 0.8f + excess * 0.2f /
                (excess + 0.2f);
            const float scale = mappedPeak / std::max(peak, 1.0e-6f);
            pixel.x = std::max(pixel.x, 0.0f) * scale;
            pixel.y = std::max(pixel.y, 0.0f) * scale;
            pixel.z = std::max(pixel.z, 0.0f) * scale;
        }
    }
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
    EnsureAtmosphereLuts(camera);
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
    if (!m_sizeDependentResourcesValid || !m_backBufferRtv ||
        !m_sceneColorRtv || !m_sceneDepthDsv)
        return;

    ++m_renderFrameSerial;
    ApplyPendingStage15Requests();
    if (m_stage15CaptureState == Stage15CaptureState::PendingNative &&
        !m_stage15CaptureFreezeValid)
    {
        m_stage15CaptureFrozenTime = timeSeconds;
        m_stage15CaptureFreezeValid = true;
    }
    m_frameProfiler.BeginCpuFrame();
    CheckShaderHotReload();
    if ((m_stage15CaptureState == Stage15CaptureState::Accumulating ||
         m_stage15CaptureState == Stage15CaptureState::Ready) &&
        m_stage15CaptureShaderGeneration != m_shaderGeneration)
    {
        ResetStage15CaptureAccumulation(
            "Shader changed; capture accumulation restarted");
        m_stage15CaptureShaderGeneration = m_shaderGeneration;
        m_stage15CaptureState = Stage15CaptureState::Accumulating;
    }
    m_frameProfiler.BeginGpuFrame(m_context.Get());
    // Noise Lab은 CloudParameters를 직접 편집한다. 편집 전 값을 보관해
    // Pipeline Compare가 현재 최종값인지 판정한다.
    const CloudParameters parametersBeforeNoiseLab = m_cloudParameters;
    const CloudShapeParameters shapeBeforeNoiseLab = m_cloudShapeParameters;
    const CloudDomainParameters domainBeforeNoiseLab = m_cloudDomainParameters;
    const CloudLodParameters lodBeforeNoiseLab = m_cloudLodParameters;
    const OptimizationParameters optimizationBeforeNoiseLab =
        m_optimizationParameters;
    const Stage10UpsamplingParameters upsamplingBeforeNoiseLab =
        m_upsamplingParameters;
    const Stage11TemporalParameters temporalBeforeNoiseLab =
        m_temporalParameters;
    const Stage15TemporalTuning temporalTuningBeforeNoiseLab =
        stage15::CaptureTemporalTuning(m_temporalParameters);
    const NoiseVolumeParameters noiseVolumeBeforeNoiseLab = m_noiseVolumeParameters;
    const LightParameters lightBeforeNoiseLab = m_lightParameters;
    const EnvironmentParameters environmentBeforeNoiseLab =
        m_environmentParameters;
    const AtmosphereParameters atmosphereBeforeNoiseLab =
        m_atmosphereParameters;
    const GroundLightingParameters groundBeforeNoiseLab =
        m_groundLightingParameters;
    const ToneMappingParameters toneMappingBeforeNoiseLab =
        m_toneMappingParameters;
    const CloudRimParameters rimBeforeNoiseLab = m_cloudRimParameters;
    const Stage12ShadowParameters shadowBeforeNoiseLab = m_shadowParameters;
    const Stage12ShadowPreset shadowPresetBeforeNoiseLab = ShadowPreset();
    const Stage12ShadowMode shadowModeBeforeNoiseLab = ShadowMode();
    const CloudFormationSettings formationBeforeNoiseLab =
        CurrentCloudFormation();
    const CloudFormationSettings rawFormationBeforeNoiseLab =
        CaptureCloudFormationSettingsUnchecked(
            m_cloudParameters, m_cloudShapeParameters,
            m_cloudDomainParameters, m_weatherPreset,
            m_weatherGeneratorSettings, m_noiseVolumeParameters);
    if (m_atmosphereParameters.timePlaybackEnabled && !SceneInputLocked())
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
    // 시간 재생은 UI 입력이 아니므로 ownership 비교의 before는 자동 진행을
    // 마친 뒤에 잡는다. 이후 NoiseLab 안에서 바뀐 값만 Concept Custom이다.
    const AtmosphereParameters atmosphereBeforeStage15Ownership =
        m_atmosphereParameters;
    m_noiseLab.SetOpenWorldPipelinePreset(m_openWorldPipelinePreset);
    const std::array<ID3D11ShaderResourceView*, 6> atmosphereLutSrvs = {
        m_atmosphereLuts.transmittance.srv.Get(),
        m_atmosphereLuts.multiScattering.srv.Get(),
        m_atmosphereLuts.skyView.srv.Get(),
        m_atmosphereLuts.skyIrradiance.srv.Get(),
        m_atmosphereLuts.aerialRadiance.srv.Get(),
        m_atmosphereLuts.aerialTransmittance.srv.Get()
    };
    const std::array<std::uint64_t, 6> atmosphereLutGenerations = {
        m_atmosphereLutGenerations[0], m_atmosphereLutGenerations[1],
        m_atmosphereLutGenerations[2], m_atmosphereLutGenerations[3],
        m_atmosphereLutGenerations[4], m_atmosphereLutGenerations[5]
    };
    const std::array<std::uint64_t, 6> atmosphereLutHashes = {
        m_atmosphereLutHashes[0], m_atmosphereLutHashes[1],
        m_atmosphereLutHashes[2], m_atmosphereLutHashes[3],
        m_atmosphereLutHashes[4], m_atmosphereLutHashes[5]
    };
    const Stage15OutputExtentSnapshot outputExtent = OutputExtentSnapshot();
    if (!m_automatedRenderMode)
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
                           m_cloudRimParameters,
                           m_atmosphereParameters, m_groundLightingParameters,
                          m_toneMappingParameters,
                          m_stage15QualityPreset, m_stage15ConceptPreset,
                          m_stage15DiagnosticMode,
                          m_stage15TemporalOverrideActive,
                          outputExtent, m_stage15CaptureState,
                          m_stage15CaptureCompletedSamples,
                          m_stage15CaptureStatus,
                          TemporalResetCountLast60Frames(),
                          m_lastTemporalResetReason,
                          m_temporalStatisticsValid,
                          m_temporalHistoryValidPercent,
                          m_temporalAverageHistoryWeight,
                          m_stage15StatusOverlayVisible,
                          m_performanceOverlayVisible, atmosphereLutSrvs,
                          atmosphereLutGenerations, atmosphereLutHashes,
                          m_atmosphereStatus,
                          m_weatherPreset, m_weatherGeneratorSettings,
                          m_cloudTypeMode, m_cloudAppearancePreset,
                          m_cloudAppearanceDirty,
                          m_hasSavedCustomAppearance,
                          m_cloudAppearanceStatus,
                          m_cloudFormationTarget,
                          m_cloudFormationTargetValid,
                          m_cloudFormationSource,
                          m_cloudFormationCloudDirty,
                          m_cloudFormationSceneDirty,
                          m_hasSavedCustomFormation,
                          m_cloudFormationStatus,
                          m_cameraMoveSpeedMetersPerSecond,
                           m_noiseVolumeParameters, m_baseNoiseVolumeHash,
                          m_detailNoiseVolumeHash,
                          m_noiseVolumeGenerationMilliseconds,
                          m_weatherMapSrv.Get(), m_weatherMapStatus,
                          m_frameProfiler.Snapshot(), m_vsyncEnabled,
                          m_shaderGeneration, m_shaderStatus, m_shaderError);
    // ImGui는 편의를 위해 현재 구조체를 직접 편집하지만, 그 결과를 곧바로
    // runtime으로 인정하지 않는다. 후보 formation을 먼저 캡처하고 소유 필드를
    // 전부 이전 값으로 복원한 뒤 F1/F4 preset과 같은 공통 transaction으로만
    // 다시 적용한다. 따라서 coverage와 domain을 한 frame에 함께 바꿔도 fit
    // 실패 시 둘 중 하나만 남는 부분 commit이 생기지 않는다.
    const CloudFormationSettings rawFormationCandidateFromNoiseLab =
        CaptureCloudFormationSettingsUnchecked(
            m_cloudParameters, m_cloudShapeParameters,
            m_cloudDomainParameters, m_weatherPreset,
            m_weatherGeneratorSettings, m_noiseVolumeParameters);
    const CloudFormationSettings formationCandidateFromNoiseLab =
        SanitizeCloudFormationSettings(rawFormationCandidateFromNoiseLab);
    // canonical 값만 비교하면 min/max 역전이나 wind Y/길이처럼 sanitize 뒤
    // 이전 값과 같아지는 raw 편집이 runtime에 남는다. formation 소유 필드는
    // raw snapshot으로 감지하고, 언제나 이전 상태 복구→공통 transaction 순서로
    // 처리한다.
    if (!CloudFormationSettingsEqual(
            rawFormationBeforeNoiseLab,
            rawFormationCandidateFromNoiseLab, 0.0f))
    {
        PreparedCloudFormation previousPrepared;
        std::string previousStatus;
        if (PrepareCloudFormationSettings(
                formationBeforeNoiseLab, previousPrepared,
                previousStatus, 200.0f))
        {
            WriteCloudFormationToRuntime(
                previousPrepared, m_cloudParameters,
                m_cloudShapeParameters, m_cloudDomainParameters,
                m_weatherPreset, m_weatherGeneratorSettings,
                m_noiseVolumeParameters);
            m_cloudTypeMode = m_weatherGeneratorSettings.cloudTypeMode;
            if (!CloudFormationSettingsEqual(
                    formationBeforeNoiseLab,
                    formationCandidateFromNoiseLab))
            {
                ApplyCloudFormationAtomic(
                    formationCandidateFromNoiseLab, false);
            }
            else
            {
                m_cloudFormationStatus =
                    "Formation edit canonicalized to the current value";
            }
        }
        else
        {
            // 정상 Stage 15 formation은 이 경로에 오지 않는다. 그래도 이전 raw
            // 구조체를 복구해 UI 후보가 검증 없이 남는 일은 막는다.
            m_cloudParameters = parametersBeforeNoiseLab;
            m_cloudShapeParameters = shapeBeforeNoiseLab;
            m_cloudDomainParameters = domainBeforeNoiseLab;
            m_noiseVolumeParameters = noiseVolumeBeforeNoiseLab;
            m_cloudFormationStatus =
                "Formation edit rollback failed to prepare prior state: " +
                previousStatus;
        }
    }
    Stage15ConceptPreset requestedStage15Concept = m_stage15ConceptPreset;
    if (m_noiseLab.ConsumeStage15ConceptRequest(requestedStage15Concept))
        RequestStage15ConceptPreset(requestedStage15Concept);
    Stage15QualityPreset requestedStage15Quality = m_stage15QualityPreset;
    if (m_noiseLab.ConsumeStage15QualityRequest(requestedStage15Quality))
        RequestStage15QualityPreset(requestedStage15Quality);
    Stage15DiagnosticMode requestedStage15Diagnostic = m_stage15DiagnosticMode;
    if (m_noiseLab.ConsumeStage15DiagnosticRequest(requestedStage15Diagnostic))
        RequestStage15DiagnosticMode(requestedStage15Diagnostic);
    const float sunAzimuthDelta = std::abs(
        m_atmosphereParameters.sunAzimuthDegrees -
        atmosphereBeforeNoiseLab.sunAzimuthDegrees);
    const float sunElevationDelta = std::abs(
        m_atmosphereParameters.sunElevationDegrees -
        atmosphereBeforeNoiseLab.sunElevationDegrees);
    if (std::max(sunAzimuthDelta, sunElevationDelta) > 0.25f)
        ResetTemporalHistory(Stage11HistoryResetReason::ParametersChanged);
    if (ShadowPreset() != shadowPresetBeforeNoiseLab &&
        !CreateDeepShadowResources(ShadowPreset()))
    {
        stage12shadow::ApplyPreset(
            m_shadowParameters, shadowPresetBeforeNoiseLab);
    }
    if (m_noiseLab.ConsumeParametersChanged())
    {
        // ImGui는 후보 값을 구조체에 먼저 쓴다. formation preflight가 거부되어
        // 위에서 모든 render-owned 값을 복구한 경우 raw UI 플래그만 남을 수
        // 있으므로, 실제 렌더 상태가 하나라도 달라졌을 때만 preset ownership과
        // temporal history를 변경한다.
        const bool effectiveDirectUiChange =
            std::memcmp(&parametersBeforeNoiseLab, &m_cloudParameters,
                        sizeof(CloudParameters)) != 0 ||
            std::memcmp(&shapeBeforeNoiseLab, &m_cloudShapeParameters,
                        sizeof(CloudShapeParameters)) != 0 ||
            std::memcmp(&domainBeforeNoiseLab, &m_cloudDomainParameters,
                        sizeof(CloudDomainParameters)) != 0 ||
            std::memcmp(&lodBeforeNoiseLab, &m_cloudLodParameters,
                        sizeof(CloudLodParameters)) != 0 ||
            std::memcmp(&optimizationBeforeNoiseLab,
                        &m_optimizationParameters,
                        sizeof(OptimizationParameters)) != 0 ||
            std::memcmp(&upsamplingBeforeNoiseLab, &m_upsamplingParameters,
                        sizeof(Stage10UpsamplingParameters)) != 0 ||
            std::memcmp(&temporalBeforeNoiseLab, &m_temporalParameters,
                        sizeof(Stage11TemporalParameters)) != 0 ||
            std::memcmp(&noiseVolumeBeforeNoiseLab,
                        &m_noiseVolumeParameters,
                        sizeof(NoiseVolumeParameters)) != 0 ||
            std::memcmp(&lightBeforeNoiseLab, &m_lightParameters,
                        sizeof(LightParameters)) != 0 ||
            std::memcmp(&environmentBeforeNoiseLab,
                        &m_environmentParameters,
                        sizeof(EnvironmentParameters)) != 0 ||
            !stage15::ConceptAtmosphereEqual(
                atmosphereBeforeStage15Ownership,
                m_atmosphereParameters) ||
            std::memcmp(&groundBeforeNoiseLab,
                        &m_groundLightingParameters,
                        sizeof(GroundLightingParameters)) != 0 ||
            std::memcmp(&shadowBeforeNoiseLab, &m_shadowParameters,
                        sizeof(Stage12ShadowParameters)) != 0 ||
            std::memcmp(&toneMappingBeforeNoiseLab,
                        &m_toneMappingParameters,
                        sizeof(ToneMappingParameters)) != 0 ||
            std::memcmp(&rimBeforeNoiseLab, &m_cloudRimParameters,
                        sizeof(CloudRimParameters)) != 0;
        if (!effectiveDirectUiChange)
        {
            // 오류 상태 문구는 남기되 history/reset count와 현재 preset target은
            // 그대로 둔다. 실패한 후보는 렌더 fingerprint에 포함되지 않는다.
        }
        else
        {
        const bool temporalModeEdited =
            temporalBeforeNoiseLab.temporalEnabled !=
                m_temporalParameters.temporalEnabled ||
            temporalBeforeNoiseLab.jitterEnabled !=
                m_temporalParameters.jitterEnabled;
        const bool temporalTuningEdited =
            !stage15::TemporalTuningEqual(
                temporalTuningBeforeNoiseLab,
                stage15::CaptureTemporalTuning(m_temporalParameters));
        const bool temporalControlsEdited =
            temporalModeEdited || temporalTuningEdited;
        const bool qualityEdited =
            parametersBeforeNoiseLab.stepSize != m_cloudParameters.stepSize ||
            parametersBeforeNoiseLab.maxViewSteps !=
                m_cloudParameters.maxViewSteps ||
            parametersBeforeNoiseLab.transmittanceThreshold !=
                m_cloudParameters.transmittanceThreshold ||
            std::memcmp(&lodBeforeNoiseLab, &m_cloudLodParameters,
                        sizeof(CloudLodParameters)) != 0 ||
            std::memcmp(&optimizationBeforeNoiseLab,
                        &m_optimizationParameters,
                        sizeof(OptimizationParameters)) != 0 ||
            std::memcmp(&upsamplingBeforeNoiseLab, &m_upsamplingParameters,
                        sizeof(Stage10UpsamplingParameters)) != 0 ||
            temporalTuningEdited ||
            temporalModeEdited ||
            shadowPresetBeforeNoiseLab != ShadowPreset() ||
            shadowModeBeforeNoiseLab != ShadowMode() ||
            lightBeforeNoiseLab.maxLightSteps !=
                m_lightParameters.maxLightSteps ||
            lightBeforeNoiseLab.lightStepSize !=
                m_lightParameters.lightStepSize;
        const bool sceneEdited =
            !stage15::ConceptLightEqual(
                lightBeforeNoiseLab, m_lightParameters) ||
            !stage15::ConceptShadowEqual(
                shadowBeforeNoiseLab, m_shadowParameters) ||
            std::memcmp(&environmentBeforeNoiseLab,
                        &m_environmentParameters,
                        sizeof(EnvironmentParameters)) != 0 ||
            !stage15::ConceptAtmosphereEqual(
                atmosphereBeforeStage15Ownership,
                m_atmosphereParameters) ||
            std::memcmp(&groundBeforeNoiseLab,
                        &m_groundLightingParameters,
                        sizeof(GroundLightingParameters)) != 0;
        if (qualityEdited)
            m_stage15QualityPreset = Stage15QualityPreset::Custom;
        const CloudFormationSettings formationAfterNoiseLab =
            CurrentCloudFormation();
        if (!CloudFormationSettingsEqual(
                formationBeforeNoiseLab, formationAfterNoiseLab))
            MarkCloudFormationDirty();
        if (sceneEdited)
            MarkCloudFormationSceneDirty();
        if (temporalControlsEdited)
        {
            const Stage11TemporalMode temporalMode = TemporalMode();
            m_stage15TemporalOverrideActive = false;
            m_stage15TemporalOverrideMode = temporalMode;
            m_stage15TemporalOverrideRestoreMode = temporalMode;
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
    }
    if (m_noiseLab.ConsumeCompositeOnlyParametersChanged())
    {
        // Rim은 resolve된 full-resolution 결과에만 적용되므로 history를
        // 무효화하지 않는다. 구름 저장 대상은 유지하고 씬 dirty만 표시한다.
        MarkCloudFormationSceneDirty();
    }
    if (m_noiseLab.ConsumeTemporalResetRequest())
        ResetTemporalHistory(Stage11HistoryResetReason::Manual);
    Stage5WeatherPreset requestedWeatherPreset = m_weatherPreset;
    if (m_noiseLab.ConsumeWeatherPresetRequest(requestedWeatherPreset))
    {
        if (ApplyStage5WeatherPreset(requestedWeatherPreset))
            MarkCloudFormationDirty();
    }
    WeatherMapGeneratorSettings requestedGeneratorSettings;
    const bool appearanceEdited = m_noiseLab.ConsumeCloudAppearanceEdited();
    if (m_noiseLab.ConsumeWeatherGeneratorRequest(requestedGeneratorSettings))
    {
        if (ApplyWeatherGeneratorSettings(requestedGeneratorSettings))
            MarkCloudFormationDirty();
    }
    else if (appearanceEdited &&
             !CloudFormationSettingsEqual(
                 formationBeforeNoiseLab, CurrentCloudFormation()))
    {
        MarkCloudFormationDirty();
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
    CloudFormationPresetTarget requestedFormationTarget =
        m_cloudFormationTarget;
    if (m_noiseLab.ConsumeCloudFormationPresetRequest(
            requestedFormationTarget))
    {
        ApplyCloudFormationPresetTarget(
            requestedFormationTarget,
            !m_ignoreCloudFormationPresetOverrides, true);
    }
    if (m_noiseLab.ConsumeCloudFormationSaveToPresetRequest())
    {
        if (m_cloudFormationTargetValid &&
            m_cloudFormationTarget.group !=
                CloudFormationPresetGroup::Custom)
        {
            SaveCurrentCloudFormationToTarget(
                m_cloudFormationTarget, false);
        }
        else
        {
            m_cloudFormationStatus =
                "Save to Preset requires an active F1 or F4 target";
        }
    }
    if (m_noiseLab.ConsumeCloudFormationSaveAsCustomRequest())
    {
        SaveCurrentCloudFormationToTarget(
            CustomFormationTarget(), true);
    }
    if (m_noiseLab.ConsumeCloudFormationRestoreBuiltInRequest())
        RestoreCurrentCloudFormationBuiltIn();
    NoiseSource requestedNoiseSource = CurrentNoiseSource();
    if (m_noiseLab.ConsumeNoiseSourceRequest(requestedNoiseSource))
        SetNoiseSource(requestedNoiseSource);
    if (m_noiseLab.ConsumeNoiseVolumeRegenerateRequest())
        RegenerateNoiseVolumes();
    const float liveEffectiveTime = ResolvePipelineComparisonTime(
        m_pipelineComparisonActive, m_noiseLab.EffectiveTime());
    BeginStage15CaptureIfReady(liveEffectiveTime);
    const bool captureFrozen =
        m_stage15DiagnosticMode == Stage15DiagnosticMode::CaptureStill &&
        m_stage15CaptureFreezeValid;
    const float effectiveTime = captureFrozen
        ? m_stage15CaptureFrozenTime : liveEffectiveTime;
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(m_width);
    viewport.Height = static_cast<float>(m_height);
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);

    const bool captureReady =
        m_stage15CaptureState == Stage15CaptureState::Ready;
    const bool captureAccumulating =
        m_stage15CaptureState == Stage15CaptureState::Accumulating;
    if (!captureReady)
    {
        // Stage 14 물리 대기 LUT가 Stage 12 cache와 모든 장면 조명보다 먼저 온다.
        EnsureAtmosphereLuts(camera);
        m_frameProfiler.MarkAtmosphereLutEnd(m_context.Get());
        m_frameProfiler.BeginCloudPass(m_context.Get());
        RenderDeepShadowCaches(camera, effectiveTime);
        m_frameProfiler.MarkShadowCacheEnd(m_context.Get());

        const XMFLOAT2 captureJitter = captureAccumulating
            ? stage15::CaptureJitterForSample(
                m_stage15CaptureCompletedSamples)
            : XMFLOAT2{};
        RenderDiagnosticScene(camera, captureJitter);
        m_frameProfiler.MarkOpaqueSceneEnd(m_context.Get());
        // 최종 Stage 15는 Full-resolution optimized direct 경로 하나만 쓴다.
        // 저해상도 MRT, 공간 resolve와 Temporal history는 화면 결과에 이득이
        // 없었으므로 Scene/Atmosphere를 같은 PS에서 바로 합성한다.
        RenderCloudPass(camera, effectiveTime);
        m_frameProfiler.MarkCloudRaymarchEnd(m_context.Get());
        if (captureAccumulating && !AccumulateStage15CaptureSample())
        {
            m_stage15CaptureState = Stage15CaptureState::Failed;
            m_stage15CaptureFreezeValid = false;
            m_stage15CaptureStatus = "HDR accumulation failed";
        }
        m_frameProfiler.EndCloudPass(m_context.Get());
    }
    else
    {
        // Ready에서는 4 spp HDR 결과를 고정하고 장면/LUT/구름을 다시 그리지 않는다.
        m_frameProfiler.MarkAtmosphereLutEnd(m_context.Get());
        m_frameProfiler.BeginCloudPass(m_context.Get());
        m_frameProfiler.MarkShadowCacheEnd(m_context.Get());
        m_frameProfiler.MarkOpaqueSceneEnd(m_context.Get());
        m_frameProfiler.MarkCloudRaymarchEnd(m_context.Get());
        m_frameProfiler.EndCloudPass(m_context.Get());
    }
    ID3D11ShaderResourceView* toneSource =
        (m_stage15CaptureCompletedSamples > 0u &&
         (captureAccumulating || captureReady ||
          m_stage15CaptureState == Stage15CaptureState::Ready))
            ? m_stage15CaptureAccumulatorSrv.Get() : nullptr;
    RenderToneMapPass(toneSource);
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
                                  m_environmentPreset, m_cloudRimParameters,
                                  m_weatherPreset,
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
    if (!m_automatedRenderMode)
        m_noiseLab.EndFrame(m_backBufferRtv.Get());
    // ImGui/Noise Lab preview는 자체 512x512 viewport를 사용할 수 있다.
    // frame 종료 상태와 다음 overlay의 "Viewport" 계측은 실제 물리 출력
    // viewport여야 하므로 Present 직전에 명시적으로 복원한다.
    m_context->RSSetViewports(1, &viewport);
    m_frameProfiler.EndGpuFrame(m_context.Get());
    m_swapChain->Present(m_vsyncEnabled ? 1u : 0u, 0);
    m_frameProfiler.EndCpuFrame();
}

bool Renderer::AccumulateStage15CaptureSample()
{
    if (!m_stage15CaptureAccumulatorRtv ||
        !m_stage15CaptureAccumulatorSrv || !m_hdrCompositeSrv ||
        !m_stage15CaptureAccumulatePs || !m_stage15CaptureBlendState)
        return false;
    if (m_stage15CaptureCompletedSamples >= stage15::kCaptureSampleCount)
        return true;

    ID3D11ShaderResourceView* nullSrv = nullptr;
    m_context->PSSetShaderResources(0, 1, &nullSrv);
    ID3D11RenderTargetView* target =
        m_stage15CaptureAccumulatorRtv.Get();
    m_context->OMSetRenderTargets(1, &target, nullptr);
    m_context->OMSetDepthStencilState(nullptr, 0);
    m_context->RSSetState(nullptr);
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(m_width);
    viewport.Height = static_cast<float>(m_height);
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);
    m_context->IASetInputLayout(nullptr);
    m_context->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_fullscreenVs.Get(), nullptr, 0);
    m_context->PSSetShader(
        m_stage15CaptureAccumulatePs.Get(), nullptr, 0);
    ID3D11ShaderResourceView* hdrSample = m_hdrCompositeSrv.Get();
    m_context->PSSetShaderResources(0, 1, &hdrSample);

    const float sampleWeight = 1.0f /
        static_cast<float>(m_stage15CaptureCompletedSamples + 1u);
    const float blendFactor[4] = {
        sampleWeight, sampleWeight, sampleWeight, sampleWeight
    };
    m_context->OMSetBlendState(
        m_stage15CaptureBlendState.Get(), blendFactor, 0xffffffffu);
    m_context->Draw(3, 0);
    m_context->OMSetBlendState(nullptr, nullptr, 0xffffffffu);
    m_context->PSSetShaderResources(0, 1, &nullSrv);
    m_context->OMSetRenderTargets(0, nullptr, nullptr);

    ++m_stage15CaptureCompletedSamples;
    m_stage15CaptureState = stage15::CaptureStateForCompletedSamples(
        m_stage15CaptureCompletedSamples);
    std::ostringstream status;
    status << stage15::CaptureStateName(m_stage15CaptureState) << " "
           << m_stage15CaptureCompletedSamples << "/"
           << stage15::kCaptureSampleCount << " HDR samples";
    m_stage15CaptureStatus = status.str();
    return true;
}

void Renderer::ResetStage15CaptureAccumulation(const char* reason)
{
    m_stage15CaptureCompletedSamples = 0;
    if (m_context && m_stage15CaptureAccumulatorRtv)
    {
        ID3D11ShaderResourceView* nullSrv = nullptr;
        m_context->PSSetShaderResources(0, 1, &nullSrv);
        const float clear[4] = {};
        m_context->ClearRenderTargetView(
            m_stage15CaptureAccumulatorRtv.Get(), clear);
    }
    m_stage15CaptureStatus = reason ? reason : "Capture reset";
}

void Renderer::BeginStage15CaptureIfReady(float effectiveTime)
{
    if (m_stage15DiagnosticMode != Stage15DiagnosticMode::CaptureStill ||
        m_stage15CaptureState != Stage15CaptureState::PendingNative)
        return;
    if (!m_outputExtent.native1080p || m_width != 1920 || m_height != 1080 ||
        m_outputExtent.physicalClientWidth != 1920 ||
        m_outputExtent.physicalClientHeight != 1080)
        return;

    ResetStage15CaptureAccumulation("Capture accumulation started");
    if (!m_stage15CaptureFreezeValid)
    {
        m_stage15CaptureFrozenTime = effectiveTime;
        m_stage15CaptureFreezeValid = true;
    }
    m_stage15CaptureShaderGeneration = m_shaderGeneration;
    m_stage15CaptureState = Stage15CaptureState::Accumulating;
}

void Renderer::RenderToneMapPass(ID3D11ShaderResourceView* sourceOverride)
{
    ID3D11ShaderResourceView* source = sourceOverride
        ? sourceOverride : m_hdrCompositeSrv.Get();
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
    CloudFormationSettings formation = CurrentCloudFormation();
    // F1/F4 authoring target만 전체 formation transaction으로 갱신한다.
    // Stage 5/13 CLI 회귀 preset은 200 m Stage 15 headroom 계약보다 오래된
    // 도메인을 의도적으로 재현하므로 Weather texture만 바꾸는 legacy 경로를
    // 계속 사용해야 한다.
    if (changed && m_cloudFormationTargetValid &&
        IsValidCloudFormationSettings(formation))
    {
        formation.weatherPreset = preset;
        return ApplyCloudFormationAtomic(formation, true);
    }
    if (!changed)
        return true;
    // 프리셋 전환도 초기 texture/SRV를 재생성하지 않고 픽셀만 교체한다.
    if (!UpdateWeatherMapTexture(preset, m_weatherGeneratorSettings))
        return false;
    if (changed)
    {
        m_stage15ConceptApplied = false;
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
    stage6light::AnglesFromDirection(
        m_lightParameters.directionToSun,
        m_atmosphereParameters.sunAzimuthDegrees,
        m_atmosphereParameters.sunElevationDegrees);
    m_atmosphereParameters.timePlaybackEnabled = false;
    m_atmosphereParameters.sunControlMode = SunControlMode::Angles;
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
    m_atmosphereParameters.sunAzimuthDegrees = -60.0f;
    m_atmosphereParameters.sunElevationDegrees = 18.0f;
    m_atmosphereParameters.timePlaybackEnabled = false;
    m_atmosphereParameters.sunControlMode = SunControlMode::Angles;
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
    if (m_stage15DiagnosticMode != Stage15DiagnosticMode::None)
        return;
    m_lightParameters.maxLightSteps = maxSteps;
    m_lightParameters.lightStepSize = stepSize;
    m_lightParameters = stage6light::Sanitize(m_lightParameters);
    m_sunPreset = Stage6SunPreset::Custom;
}

void Renderer::SetCloudLodForValidation(bool enabled, float startMeters,
                                        float endMeters)
{
    if (m_stage15DiagnosticMode != Stage15DiagnosticMode::None)
        return;
    m_cloudLodParameters.detailLodEnabled = enabled ? 1u : 0u;
    m_cloudLodParameters.detailLodStartMeters = startMeters;
    m_cloudLodParameters.detailLodEndMeters = endMeters;
    m_cloudLodParameters = stage13lod::Sanitize(m_cloudLodParameters);
}

void Renderer::SetViewSamplingForSmoke(std::uint32_t maxSteps, float stepSize)
{
    if (m_stage15DiagnosticMode != Stage15DiagnosticMode::None)
        return;
    m_cloudParameters.maxViewSteps = std::max(maxSteps, 1u);
    m_cloudParameters.stepSize = std::max(stepSize, 1e-4f);
}

bool Renderer::ApplyStage15Defaults()
{
    m_stage15DiagnosticMode = Stage15DiagnosticMode::None;
    m_stage15TemporalOverrideActive = false;
    m_stage15TemporalOverrideMode = Stage11TemporalMode::Stable4Phase;
    m_stage15TemporalOverrideRestoreMode =
        Stage11TemporalMode::Stable4Phase;
    if (!m_ignoreCloudFormationPresetOverrides &&
        !m_hasSavedCustomFormation)
    {
        CloudFormationSettings migrationBase;
        if (ResolveBuiltInCloudFormation(
                CloudFormationType::Cumulus, migrationBase))
        {
            std::string migrationStatus;
            const LegacyCloudFormationMigrationResult migration =
                MigrateLegacyCustomCloudFormation(
                    m_customAppearancePath, m_cloudFormationPresetRoot,
                    migrationBase, migrationStatus);
            if (migration == LegacyCloudFormationMigrationResult::Migrated)
            {
                m_hasSavedCustomFormation = true;
                m_cloudFormationStatus = migrationStatus;
            }
            else if (migration ==
                     LegacyCloudFormationMigrationResult::Rejected ||
                     migration ==
                     LegacyCloudFormationMigrationResult::SaveFailed)
            {
                m_cloudFormationStatus = migrationStatus;
            }
        }
    }
    if (!ApplyStage15QualityPresetImmediate(
            Stage15QualityPreset::High, false))
        return false;
    if (!ApplyStage15ConceptPresetImmediate(
            Stage15ConceptPreset::UrbanFairWeather, false))
        return false;
    stage11temporal::ApplyMode(
        m_temporalParameters, Stage11TemporalMode::Off);
    m_cloudLodParameters.detailLodEnabled = 0u;
    m_cloudRimParameters.enabled = 0u;
    ResetTemporalHistory(Stage11HistoryResetReason::ParametersChanged);
    m_frameProfiler.ResetMeasurements();
    return true;
}

Stage15QualityDescriptor Renderer::CaptureCurrentStage15Quality() const
{
    Stage15QualityDescriptor result;
    result.resolutionPreset = m_resolutionPreset;
    result.upsampling = m_upsamplingParameters;
    result.temporalMode = TemporalMode();
    result.temporalTuning = stage15::CaptureTemporalTuning(
        m_temporalParameters);
    result.viewStepMeters = m_cloudParameters.stepSize;
    result.maximumViewSamples = m_cloudParameters.maxViewSteps;
    result.viewTransmittanceThreshold =
        m_cloudParameters.transmittanceThreshold;
    result.lightStepMeters = m_lightParameters.lightStepSize;
    result.maximumLightSamples = m_lightParameters.maxLightSteps;
    result.optimization = m_optimizationParameters;
    result.detailLod = m_cloudLodParameters;
    result.shadowMode = ShadowMode();
    result.shadowPreset = ShadowPreset();
    return result;
}

bool Renderer::ApplyStage15QualityDescriptor(
    const Stage15QualityDescriptor& descriptor,
    Stage15QualityPreset selection, bool updateSelection, bool resetState)
{
    const Stage15QualityDescriptor current = CaptureCurrentStage15Quality();
    if (stage15::QualityDescriptorEqual(current, descriptor))
    {
        if (updateSelection)
            m_stage15QualityPreset = selection;
        return true;
    }
    if (m_stage15FailCloudTargetPreflight)
    {
        m_stage15FailCloudTargetPreflight = false;
        return false;
    }
    Stage10UpsamplingParameters desiredUpsampling = descriptor.upsampling;
    desiredUpsampling.resolutionScale =
        stage10upsampling::ResolutionScale(descriptor.resolutionPreset);
    desiredUpsampling = stage10upsampling::Sanitize(desiredUpsampling);

    // 크기 의존 리소스를 먼저 준비한다. 두 할당 중 하나라도 실패하면 CPU의
    // 품질 상태는 이전 값 그대로 남으므로 프레임 경계 원자 적용 계약을 지킨다.
    const Stage10UpsamplingParameters oldUpsampling = m_upsamplingParameters;
    const Stage10ResolutionPreset oldResolution = m_resolutionPreset;
    m_upsamplingParameters = desiredUpsampling;
    m_resolutionPreset = descriptor.resolutionPreset;
    if (!EnsureCloudTargets())
    {
        m_upsamplingParameters = oldUpsampling;
        m_resolutionPreset = oldResolution;
        EnsureCloudTargets();
        return false;
    }
    if (descriptor.shadowPreset != ShadowPreset() &&
        (m_stage15FailShadowResourcePreflight ||
         !CreateDeepShadowResources(descriptor.shadowPreset)))
    {
        m_stage15FailShadowResourcePreflight = false;
        m_upsamplingParameters = oldUpsampling;
        m_resolutionPreset = oldResolution;
        EnsureCloudTargets();
        return false;
    }

    m_cloudParameters.stepSize = descriptor.viewStepMeters;
    m_cloudParameters.maxViewSteps = descriptor.maximumViewSamples;
    m_cloudParameters.transmittanceThreshold =
        descriptor.viewTransmittanceThreshold;
    m_lightParameters.lightStepSize = descriptor.lightStepMeters;
    m_lightParameters.maxLightSteps = descriptor.maximumLightSamples;
    m_optimizationParameters = descriptor.optimization;
    m_cloudLodParameters = descriptor.detailLod;
    m_shadowParameters.shadowMode = static_cast<std::uint32_t>(
        descriptor.shadowMode);
    m_shadowParameters = stage12shadow::Sanitize(m_shadowParameters);
    stage15::ApplyTemporalTuning(
        m_temporalParameters, descriptor.temporalTuning);
    stage11temporal::ApplyMode(m_temporalParameters, descriptor.temporalMode);
    m_temporalParameters = stage11temporal::Sanitize(m_temporalParameters);

    if (m_stage15DiagnosticMode == Stage15DiagnosticMode::Reference)
        m_optimizationPreset = Stage9OptimizationPreset::FineReference;
    else if (selection == Stage15QualityPreset::Medium)
        m_optimizationPreset = Stage9OptimizationPreset::Balanced;
    else
        m_optimizationPreset = Stage9OptimizationPreset::Custom;
    if (updateSelection)
        m_stage15QualityPreset = selection;
    if (resetState)
    {
        ResetTemporalHistory(Stage11HistoryResetReason::ParametersChanged);
        m_frameProfiler.ResetMeasurements();
    }
    return true;
}

bool Renderer::ApplyStage15QualityPresetImmediate(
    Stage15QualityPreset preset, bool resetState)
{
    if (preset == Stage15QualityPreset::Custom)
        return false;
    Stage15QualityDescriptor descriptor = stage15::ResolveQuality(
        preset, m_stage15DiagnosticMode);
    const Stage11TemporalMode presetTemporalMode = descriptor.temporalMode;
    if (m_stage15DiagnosticMode == Stage15DiagnosticMode::None &&
        m_stage15TemporalOverrideActive)
    {
        descriptor.temporalMode = m_stage15TemporalOverrideMode;
    }
    const bool succeeded = ApplyStage15QualityDescriptor(
        descriptor, preset, true, resetState);
    if (succeeded && m_stage15TemporalOverrideActive)
    {
        m_stage15TemporalOverrideRestoreMode = presetTemporalMode;
    }
    else if (succeeded)
    {
        m_stage15TemporalOverrideMode = descriptor.temporalMode;
        m_stage15TemporalOverrideRestoreMode = descriptor.temporalMode;
    }
    return succeeded;
}

bool Renderer::ApplyStage15ConceptPresetImmediate(
    Stage15ConceptPreset preset, bool resetState)
{
    if (preset == Stage15ConceptPreset::Custom)
        return false;
    if (m_stage15FailWeatherPreflight)
    {
        m_stage15FailWeatherPreflight = false;
        return false;
    }
    CloudFormationConcept formationConcept;
    if (!TryMapStage15ConceptToFormation(preset, formationConcept))
        return false;

    const Stage15ConceptDescriptor descriptor = stage15::ResolveConcept(preset);
    const CloudFormationPresetTarget formationTarget =
        ConceptFormationTarget(formationConcept);
    if (!ApplyCloudFormationPresetTarget(
            formationTarget,
            !m_ignoreCloudFormationPresetOverrides, false))
        return false;
    const std::string formationResolveStatus = m_cloudFormationStatus;

    LightParameters light = descriptor.light;
    light.maxLightSteps = m_lightParameters.maxLightSteps;
    light.lightStepSize = m_lightParameters.lightStepSize;
    light.lightRayBias = m_lightParameters.lightRayBias;
    light.directionToSun = stage6light::DirectionFromAngles(
        descriptor.atmosphere.sunAzimuthDegrees,
        descriptor.atmosphere.sunElevationDegrees);
    light = stage6light::Sanitize(light);

    Stage12ShadowParameters shadow = m_shadowParameters;
    shadow.surfaceShadowEnabled = descriptor.surfaceShadowEnabled;
    shadow.surfaceShadowStrength = descriptor.surfaceShadowStrength;
    shadow.surfaceAmbientFloor = descriptor.surfaceAmbientFloor;
    shadow = stage12shadow::Sanitize(shadow);

    const float half = stage13scene::kGroundHalfSizeMeters;
    m_cloudParameters.cloudBoundsMin.x = -half;
    m_cloudParameters.cloudBoundsMin.z = -half;
    m_cloudParameters.cloudBoundsMax.x = half;
    m_cloudParameters.cloudBoundsMax.z = half;
    m_lightParameters = light;
    m_environmentParameters = descriptor.environment;
    m_cloudRimParameters = descriptor.rim;
    m_atmosphereParameters = descriptor.atmosphere;
    m_groundLightingParameters = descriptor.ground;
    m_shadowParameters = shadow;
    m_sunPreset = descriptor.sunPreset;
    m_phasePreset = descriptor.phasePreset;
    m_environmentPreset = descriptor.environmentPreset;
    m_openWorldPipelinePreset = OpenWorldPipelinePreset::Custom;
    m_pipelineComparisonActive = false;
    m_cloudAppearanceDirty = false;
    m_cloudFormationCloudDirty = false;
    m_cloudFormationSceneDirty = false;
    const char* sourceName =
        m_cloudFormationSource == CloudFormationPresetSource::UserOverride
            ? "user override" : "built-in";
    m_cloudFormationStatus = std::string(stage15::ConceptName(preset)) +
        " concept formation (" + sourceName +
        ") and scene applied atomically; " + formationResolveStatus;
    m_cloudAppearanceStatus = m_cloudFormationStatus;
    m_stage15ConceptPreset = preset;
    m_stage15ConceptApplied = true;
    if (resetState)
    {
        ResetTemporalHistory(Stage11HistoryResetReason::ParametersChanged);
        m_frameProfiler.ResetMeasurements();
    }
    return true;
}

bool Renderer::ApplyStage15DiagnosticModeImmediate(
    Stage15DiagnosticMode mode, bool resetState)
{
    if (static_cast<std::uint32_t>(mode) >
        static_cast<std::uint32_t>(Stage15DiagnosticMode::Reference))
        return false;
    if (mode == m_stage15DiagnosticMode)
        return true;

    if (m_stage15DiagnosticMode == Stage15DiagnosticMode::None)
    {
        m_stage15SavedRealtimeQuality = CaptureCurrentStage15Quality();
        m_stage15SavedRealtimeQualityPreset = m_stage15QualityPreset;
        m_stage15SavedRealtimeQualityValid = true;
    }

    const Stage15DiagnosticMode previousMode = m_stage15DiagnosticMode;
    if (mode == Stage15DiagnosticMode::None)
    {
        if (!m_stage15SavedRealtimeQualityValid)
            return false;
        m_stage15DiagnosticMode = Stage15DiagnosticMode::None;
        if (!ApplyStage15QualityDescriptor(
                m_stage15SavedRealtimeQuality,
                m_stage15SavedRealtimeQualityPreset, false, resetState))
        {
            m_stage15DiagnosticMode = previousMode;
            return false;
        }
        // 진단 descriptor는 선택 이름을 바꾸지 않지만, 진단 중 외부 개발
        // setter가 잘못 호출되더라도 Restore의 표시 이름까지 진입 전 값으로
        // 확정한다. 실제 품질 값은 위 descriptor 적용이 복원한다.
        m_stage15QualityPreset = m_stage15SavedRealtimeQualityPreset;
        m_stage15SavedRealtimeQualityValid = false;
        if (previousMode == Stage15DiagnosticMode::CaptureStill)
        {
            ResetStage15CaptureAccumulation("Capture restored to realtime");
            m_stage15CaptureState = Stage15CaptureState::Inactive;
            m_stage15CaptureFreezeValid = false;
            if (m_stage15CaptureSavedDebugModeValid)
                SetDebugMode(m_stage15CaptureSavedDebugMode);
            m_stage15CaptureSavedDebugModeValid = false;
            if (m_native1080pRequest == 1)
                m_native1080pRequest = -1;
            if (m_stage15CaptureOwnsNative1080p)
                m_native1080pRequest = 0;
        }
        return true;
    }

    m_stage15DiagnosticMode = mode;
    if (mode == Stage15DiagnosticMode::CaptureStill)
    {
        if (previousMode != Stage15DiagnosticMode::CaptureStill)
        {
            m_stage15CaptureSavedDebugMode = DebugMode();
            m_stage15CaptureSavedDebugModeValid = true;
        }
        SetDebugMode(CloudDebugMode::Composite);
        ResetStage15CaptureAccumulation("Waiting for Native 1920x1080");
        m_stage15CaptureState = Stage15CaptureState::PendingNative;
        m_stage15CaptureFreezeValid = false;
        const bool exactNative = m_outputExtent.native1080p &&
            m_outputExtent.physicalClientWidth == 1920 &&
            m_outputExtent.physicalClientHeight == 1080 &&
            m_width == 1920 && m_height == 1080;
        if (!exactNative)
        {
            m_native1080pRequest = 1;
            return true;
        }
    }

    const Stage15QualityPreset resolverPreset =
        m_stage15QualityPreset == Stage15QualityPreset::Custom
            ? Stage15QualityPreset::Medium : m_stage15QualityPreset;
    if (ApplyStage15QualityDescriptor(
            stage15::ResolveQuality(resolverPreset, mode),
            m_stage15QualityPreset, false, resetState))
    {
        if (previousMode == Stage15DiagnosticMode::CaptureStill &&
            mode != Stage15DiagnosticMode::CaptureStill)
        {
            ResetStage15CaptureAccumulation("Capture changed to Reference");
            m_stage15CaptureState = Stage15CaptureState::Inactive;
            m_stage15CaptureFreezeValid = false;
            if (m_stage15CaptureSavedDebugModeValid)
                SetDebugMode(m_stage15CaptureSavedDebugMode);
            m_stage15CaptureSavedDebugModeValid = false;
            if (m_native1080pRequest == 1)
                m_native1080pRequest = -1;
            if (m_stage15CaptureOwnsNative1080p)
                m_native1080pRequest = 0;
        }
        return true;
    }
    m_stage15DiagnosticMode = previousMode;
    if (mode == Stage15DiagnosticMode::CaptureStill)
    {
        m_stage15CaptureState = Stage15CaptureState::Failed;
        m_stage15CaptureFreezeValid = false;
        m_stage15CaptureStatus = "Capture quality preflight failed";
        if (m_stage15CaptureSavedDebugModeValid)
            SetDebugMode(m_stage15CaptureSavedDebugMode);
        m_stage15CaptureSavedDebugModeValid = false;
    }
    return false;
}

void Renderer::CycleStage15QualityPreset()
{
    if (m_stage15DiagnosticMode != Stage15DiagnosticMode::None)
        return;
    const std::uint32_t current = static_cast<std::uint32_t>(
        m_stage15QualityPreset);
    RequestStage15QualityPreset(static_cast<Stage15QualityPreset>(
        current >= static_cast<std::uint32_t>(Stage15QualityPreset::High)
            ? 0u : current + 1u));
}

void Renderer::ToggleStage15TemporalOverride()
{
    if (m_stage15DiagnosticMode != Stage15DiagnosticMode::None)
        return;
    // 같은 프레임 경계 전에 두 번 들어온 물리 입력도 두 번 반전되어
    // 최종적으로 no-op이 되도록 toggle 요청은 parity로 합친다.
    m_pendingStage15Transition.toggleTemporalOverride =
        !m_pendingStage15Transition.toggleTemporalOverride;
}

void Renderer::ApplyPendingStage15Requests()
{
    if (m_pendingStage15Transition.Empty())
        return;
    const Stage15TransitionRequest request = m_pendingStage15Transition;
    m_pendingStage15Transition = {};
    const auto retainFailedRequest = [&]()
    {
        m_pendingStage15Transition = request;
        m_stage15TransitionStatus =
            "Preflight/apply failed; request retained";
    };

    // 모든 값/CPU cache/fault-injection 검사를 실제 CPU/GPU 상태 변경보다
    // 먼저 끝낸다. 특히 concept Weather preflight가 실패할 요청에서 quality
    // resource만 먼저 교체되는 부분 commit을 허용하지 않는다.
    if (request.qualityPreset >= 0 &&
        request.qualityPreset >= static_cast<std::int32_t>(
            Stage15QualityPreset::Custom))
    {
        retainFailedRequest();
        return;
    }
    if (request.conceptPreset >= 0)
    {
        const std::size_t conceptIndex = static_cast<std::size_t>(
            request.conceptPreset);
        if (conceptIndex >= static_cast<std::size_t>(
                Stage15ConceptPreset::Custom) ||
            conceptIndex >= m_stage15WeatherMaps.size() ||
            !IsValidWeatherMapData(m_stage15WeatherMaps[conceptIndex]) ||
            m_stage15FailWeatherPreflight)
        {
            m_stage15FailWeatherPreflight = false;
            retainFailedRequest();
            return;
        }
    }
    if (request.diagnosticMode >= 0)
    {
        const auto requested = static_cast<Stage15DiagnosticMode>(
            request.diagnosticMode);
        if (request.diagnosticMode > static_cast<std::int32_t>(
                Stage15DiagnosticMode::Reference) ||
            (requested == Stage15DiagnosticMode::None &&
             m_stage15DiagnosticMode != Stage15DiagnosticMode::None &&
             !m_stage15SavedRealtimeQualityValid))
        {
            retainFailedRequest();
            return;
        }
    }

    // 한 요청 안의 뒤쪽 단계가 실패해도 앞에서 적용한 Concept/Quality와 GPU
    // 객체 identity가 남지 않도록, fallible apply 전에 완전한 transaction
    // snapshot을 잡는다. ComPtr 복사는 기존 객체를 살아 있게 하므로 새 target을
    // 잠깐 만들었더라도 실패 시 정확히 같은 객체로 돌아갈 수 있다.
    struct TransactionSnapshot
    {
        CloudParameters cloud;
        CloudDomainParameters domain;
        CloudShapeParameters shape;
        CloudLodParameters lod;
        OptimizationParameters optimization;
        Stage10UpsamplingParameters upsampling;
        Stage11TemporalParameters temporal;
        Stage12ShadowParameters shadow;
        LightParameters light;
        EnvironmentParameters environment;
        CloudRimParameters rim;
        AtmosphereParameters atmosphere;
        GroundLightingParameters ground;
        NoiseVolumeParameters noiseVolume;
        Stage9OptimizationPreset optimizationPreset;
        Stage10ResolutionPreset resolutionPreset;
        Stage5WeatherPreset weatherPreset;
        WeatherMapGeneratorSettings weatherSettings;
        WeatherMapData weatherData;
        CloudTypeMode cloudTypeMode;
        std::uint64_t weatherHash;
        std::uint64_t weatherUploads;
        std::string weatherStatus;
        Stage6SunPreset sunPreset;
        Stage7PhasePreset phasePreset;
        Stage8EnvironmentPreset environmentPreset;
        OpenWorldPipelinePreset openWorldPreset;
        bool pipelineComparisonActive;
        CloudAppearancePreset appearancePreset;
        bool appearanceDirty;
        std::string appearanceStatus;
        CloudFormationPresetTarget formationTarget;
        bool formationTargetValid;
        CloudFormationPresetSource formationSource;
        bool formationCloudDirty;
        bool formationSceneDirty;
        bool hasSavedCustomFormation;
        std::string formationStatus;
        Stage15QualityPreset qualityPreset;
        Stage15ConceptPreset conceptPreset;
        Stage15DiagnosticMode diagnosticMode;
        bool temporalOverrideActive;
        Stage11TemporalMode temporalOverrideMode;
        Stage11TemporalMode temporalOverrideRestoreMode;
        Stage15QualityDescriptor savedRealtimeQuality;
        Stage15QualityPreset savedRealtimeQualityPreset;
        bool savedRealtimeQualityValid;
        bool conceptApplied;
        int native1080pRequest;
        bool captureOwnsNative1080p;
        Stage15CaptureState captureState;
        std::uint32_t captureCompletedSamples;
        float captureFrozenTime;
        bool captureFreezeValid;
        std::uint64_t captureShaderGeneration;
        std::string captureStatus;
        CloudDebugMode captureSavedDebugMode;
        bool captureSavedDebugModeValid;
        bool historyValid;
        bool previousFrameValid;
        std::uint32_t historyReadIndex;
        std::uint32_t accumulatedFrames;
        ComPtr<ID3D11Texture2D> cloudScattering;
        ComPtr<ID3D11RenderTargetView> cloudScatteringRtv;
        ComPtr<ID3D11ShaderResourceView> cloudScatteringSrv;
        ComPtr<ID3D11Texture2D> cloudDepth;
        ComPtr<ID3D11RenderTargetView> cloudDepthRtv;
        ComPtr<ID3D11ShaderResourceView> cloudDepthSrv;
        int cloudWidth;
        int cloudHeight;
        ComPtr<ID3D11Texture2D> shadowNear;
        ComPtr<ID3D11ShaderResourceView> shadowNearSrv;
        ComPtr<ID3D11UnorderedAccessView> shadowNearUav;
        ComPtr<ID3D11Texture2D> shadowFar;
        ComPtr<ID3D11ShaderResourceView> shadowFarSrv;
        ComPtr<ID3D11UnorderedAccessView> shadowFarUav;
    };
    TransactionSnapshot snapshot;
    snapshot.cloud = m_cloudParameters;
    snapshot.domain = m_cloudDomainParameters;
    snapshot.shape = m_cloudShapeParameters;
    snapshot.lod = m_cloudLodParameters;
    snapshot.optimization = m_optimizationParameters;
    snapshot.upsampling = m_upsamplingParameters;
    snapshot.temporal = m_temporalParameters;
    snapshot.shadow = m_shadowParameters;
    snapshot.light = m_lightParameters;
    snapshot.environment = m_environmentParameters;
    snapshot.rim = m_cloudRimParameters;
    snapshot.atmosphere = m_atmosphereParameters;
    snapshot.ground = m_groundLightingParameters;
    snapshot.noiseVolume = m_noiseVolumeParameters;
    snapshot.optimizationPreset = m_optimizationPreset;
    snapshot.resolutionPreset = m_resolutionPreset;
    snapshot.weatherPreset = m_weatherPreset;
    snapshot.weatherSettings = m_weatherGeneratorSettings;
    snapshot.weatherData = m_currentWeatherMapData;
    snapshot.cloudTypeMode = m_cloudTypeMode;
    snapshot.weatherHash = m_weatherMapHash;
    snapshot.weatherUploads = m_weatherUploadCount;
    snapshot.weatherStatus = m_weatherMapStatus;
    snapshot.sunPreset = m_sunPreset;
    snapshot.phasePreset = m_phasePreset;
    snapshot.environmentPreset = m_environmentPreset;
    snapshot.openWorldPreset = m_openWorldPipelinePreset;
    snapshot.pipelineComparisonActive = m_pipelineComparisonActive;
    snapshot.appearancePreset = m_cloudAppearancePreset;
    snapshot.appearanceDirty = m_cloudAppearanceDirty;
    snapshot.appearanceStatus = m_cloudAppearanceStatus;
    snapshot.formationTarget = m_cloudFormationTarget;
    snapshot.formationTargetValid = m_cloudFormationTargetValid;
    snapshot.formationSource = m_cloudFormationSource;
    snapshot.formationCloudDirty = m_cloudFormationCloudDirty;
    snapshot.formationSceneDirty = m_cloudFormationSceneDirty;
    snapshot.hasSavedCustomFormation = m_hasSavedCustomFormation;
    snapshot.formationStatus = m_cloudFormationStatus;
    snapshot.qualityPreset = m_stage15QualityPreset;
    snapshot.conceptPreset = m_stage15ConceptPreset;
    snapshot.diagnosticMode = m_stage15DiagnosticMode;
    snapshot.temporalOverrideActive = m_stage15TemporalOverrideActive;
    snapshot.temporalOverrideMode = m_stage15TemporalOverrideMode;
    snapshot.temporalOverrideRestoreMode =
        m_stage15TemporalOverrideRestoreMode;
    snapshot.savedRealtimeQuality = m_stage15SavedRealtimeQuality;
    snapshot.savedRealtimeQualityPreset =
        m_stage15SavedRealtimeQualityPreset;
    snapshot.savedRealtimeQualityValid =
        m_stage15SavedRealtimeQualityValid;
    snapshot.conceptApplied = m_stage15ConceptApplied;
    snapshot.native1080pRequest = m_native1080pRequest;
    snapshot.captureOwnsNative1080p = m_stage15CaptureOwnsNative1080p;
    snapshot.captureState = m_stage15CaptureState;
    snapshot.captureCompletedSamples = m_stage15CaptureCompletedSamples;
    snapshot.captureFrozenTime = m_stage15CaptureFrozenTime;
    snapshot.captureFreezeValid = m_stage15CaptureFreezeValid;
    snapshot.captureShaderGeneration = m_stage15CaptureShaderGeneration;
    snapshot.captureStatus = m_stage15CaptureStatus;
    snapshot.captureSavedDebugMode = m_stage15CaptureSavedDebugMode;
    snapshot.captureSavedDebugModeValid =
        m_stage15CaptureSavedDebugModeValid;
    snapshot.historyValid = m_temporalHistoryValid;
    snapshot.previousFrameValid = m_temporalPreviousFrameValid;
    snapshot.historyReadIndex = m_temporalHistoryReadIndex;
    snapshot.accumulatedFrames = m_temporalAccumulatedFrames;
    snapshot.cloudScattering = m_cloudScatteringTransmittance;
    snapshot.cloudScatteringRtv = m_cloudScatteringTransmittanceRtv;
    snapshot.cloudScatteringSrv = m_cloudScatteringTransmittanceSrv;
    snapshot.cloudDepth = m_cloudDepthSceneLimit;
    snapshot.cloudDepthRtv = m_cloudDepthSceneLimitRtv;
    snapshot.cloudDepthSrv = m_cloudDepthSceneLimitSrv;
    snapshot.cloudWidth = m_cloudRenderWidth;
    snapshot.cloudHeight = m_cloudRenderHeight;
    snapshot.shadowNear = m_shadowNearTexture;
    snapshot.shadowNearSrv = m_shadowNearSrv;
    snapshot.shadowNearUav = m_shadowNearUav;
    snapshot.shadowFar = m_shadowFarTexture;
    snapshot.shadowFarSrv = m_shadowFarSrv;
    snapshot.shadowFarUav = m_shadowFarUav;
    const CloudFormationSettings formationBeforeTransition =
        CaptureCloudFormationSettings(
            snapshot.cloud, snapshot.shape, snapshot.domain,
            snapshot.weatherPreset, snapshot.weatherSettings,
            snapshot.noiseVolume);

    bool weatherPixelsMayHaveChanged = false;
    const auto rollback = [&]()
    {
        m_cloudParameters = snapshot.cloud;
        m_cloudDomainParameters = snapshot.domain;
        m_cloudShapeParameters = snapshot.shape;
        m_cloudLodParameters = snapshot.lod;
        m_optimizationParameters = snapshot.optimization;
        m_upsamplingParameters = snapshot.upsampling;
        m_temporalParameters = snapshot.temporal;
        m_shadowParameters = snapshot.shadow;
        m_lightParameters = snapshot.light;
        m_environmentParameters = snapshot.environment;
        m_cloudRimParameters = snapshot.rim;
        m_atmosphereParameters = snapshot.atmosphere;
        m_groundLightingParameters = snapshot.ground;
        m_noiseVolumeParameters = snapshot.noiseVolume;
        m_optimizationPreset = snapshot.optimizationPreset;
        m_resolutionPreset = snapshot.resolutionPreset;
        m_weatherPreset = snapshot.weatherPreset;
        m_weatherGeneratorSettings = snapshot.weatherSettings;
        m_cloudTypeMode = snapshot.cloudTypeMode;
        m_weatherMapHash = snapshot.weatherHash;
        m_weatherUploadCount = snapshot.weatherUploads;
        m_weatherMapStatus = snapshot.weatherStatus;
        m_currentWeatherMapData = snapshot.weatherData;
        m_sunPreset = snapshot.sunPreset;
        m_phasePreset = snapshot.phasePreset;
        m_environmentPreset = snapshot.environmentPreset;
        m_openWorldPipelinePreset = snapshot.openWorldPreset;
        m_pipelineComparisonActive = snapshot.pipelineComparisonActive;
        m_cloudAppearancePreset = snapshot.appearancePreset;
        m_cloudAppearanceDirty = snapshot.appearanceDirty;
        m_cloudAppearanceStatus = snapshot.appearanceStatus;
        m_cloudFormationTarget = snapshot.formationTarget;
        m_cloudFormationTargetValid = snapshot.formationTargetValid;
        m_cloudFormationSource = snapshot.formationSource;
        m_cloudFormationCloudDirty = snapshot.formationCloudDirty;
        m_cloudFormationSceneDirty = snapshot.formationSceneDirty;
        m_hasSavedCustomFormation = snapshot.hasSavedCustomFormation;
        m_cloudFormationStatus = snapshot.formationStatus;
        m_stage15QualityPreset = snapshot.qualityPreset;
        m_stage15ConceptPreset = snapshot.conceptPreset;
        m_stage15DiagnosticMode = snapshot.diagnosticMode;
        m_stage15TemporalOverrideActive = snapshot.temporalOverrideActive;
        m_stage15TemporalOverrideMode = snapshot.temporalOverrideMode;
        m_stage15TemporalOverrideRestoreMode =
            snapshot.temporalOverrideRestoreMode;
        m_stage15SavedRealtimeQuality = snapshot.savedRealtimeQuality;
        m_stage15SavedRealtimeQualityPreset =
            snapshot.savedRealtimeQualityPreset;
        m_stage15SavedRealtimeQualityValid =
            snapshot.savedRealtimeQualityValid;
        m_stage15ConceptApplied = snapshot.conceptApplied;
        m_native1080pRequest = snapshot.native1080pRequest;
        m_stage15CaptureOwnsNative1080p =
            snapshot.captureOwnsNative1080p;
        m_stage15CaptureState = snapshot.captureState;
        m_stage15CaptureCompletedSamples =
            snapshot.captureCompletedSamples;
        m_stage15CaptureFrozenTime = snapshot.captureFrozenTime;
        m_stage15CaptureFreezeValid = snapshot.captureFreezeValid;
        m_stage15CaptureShaderGeneration =
            snapshot.captureShaderGeneration;
        m_stage15CaptureStatus = snapshot.captureStatus;
        m_stage15CaptureSavedDebugMode = snapshot.captureSavedDebugMode;
        m_stage15CaptureSavedDebugModeValid =
            snapshot.captureSavedDebugModeValid;
        m_temporalHistoryValid = snapshot.historyValid;
        m_temporalPreviousFrameValid = snapshot.previousFrameValid;
        m_temporalHistoryReadIndex = snapshot.historyReadIndex;
        m_temporalAccumulatedFrames = snapshot.accumulatedFrames;
        m_cloudScatteringTransmittance = snapshot.cloudScattering;
        m_cloudScatteringTransmittanceRtv = snapshot.cloudScatteringRtv;
        m_cloudScatteringTransmittanceSrv = snapshot.cloudScatteringSrv;
        m_cloudDepthSceneLimit = snapshot.cloudDepth;
        m_cloudDepthSceneLimitRtv = snapshot.cloudDepthRtv;
        m_cloudDepthSceneLimitSrv = snapshot.cloudDepthSrv;
        m_cloudRenderWidth = snapshot.cloudWidth;
        m_cloudRenderHeight = snapshot.cloudHeight;
        m_shadowNearTexture = snapshot.shadowNear;
        m_shadowNearSrv = snapshot.shadowNearSrv;
        m_shadowNearUav = snapshot.shadowNearUav;
        m_shadowFarTexture = snapshot.shadowFar;
        m_shadowFarSrv = snapshot.shadowFarSrv;
        m_shadowFarUav = snapshot.shadowFarUav;
        m_noiseLab.SynchronizeWeatherGeneratorSettings(snapshot.weatherSettings);

        // Weather는 같은 DEFAULT texture를 제자리 갱신하므로 CPU state를
        // 되돌리는 것만으로는 부족하다. 이전 RGBA를 bookkeeping 없이 복구한다.
        if (weatherPixelsMayHaveChanged && m_context && m_weatherMapTexture &&
            IsValidWeatherMapData(snapshot.weatherData))
        {
            ID3D11ShaderResourceView* nullWeatherSrv = nullptr;
            m_context->PSSetShaderResources(2, 1, &nullWeatherSrv);
            m_context->UpdateSubresource(
                m_weatherMapTexture.Get(), 0, nullptr,
                snapshot.weatherData.rgba.data(),
                snapshot.weatherData.width * 4u, 0);
        }
    };
    bool succeeded = true;
    bool changed = false;
    // allocation 가능한 품질 리소스를 먼저 preflight/적용한다. 이후 concept의
    // Weather upload는 검증된 CPU cache와 기존 단일 texture만 사용한다.
    if (request.qualityPreset >= 0)
    {
        const Stage15QualityPreset requested =
            static_cast<Stage15QualityPreset>(request.qualityPreset);
        changed = changed || requested != m_stage15QualityPreset;
        succeeded = ApplyStage15QualityPresetImmediate(requested, false);
    }
    if (succeeded && request.conceptPreset >= 0)
    {
        const Stage15ConceptPreset requested =
            static_cast<Stage15ConceptPreset>(request.conceptPreset);
        const bool selectionOrSceneChanged =
            !m_stage15ConceptApplied ||
            requested != m_stage15ConceptPreset;
        succeeded = ApplyStage15ConceptPresetImmediate(requested, false);
        // 사용자 override는 built-in cache와 다른 hash를 가질 수 있다. 뒤의
        // Diagnostic 단계가 실패하면 실제 업로드 여부와 무관하게 snapshot
        // Weather RGBA를 되써야 완전한 transaction rollback이 된다.
        weatherPixelsMayHaveChanged = succeeded;
        if (succeeded)
        {
            changed = changed || selectionOrSceneChanged ||
                !CloudFormationSettingsEqual(
                    formationBeforeTransition,
                    CurrentCloudFormation()) ||
                snapshot.weatherHash != m_weatherMapHash;
        }
    }
    if (succeeded && request.toggleTemporalOverride)
    {
        const Stage11TemporalMode currentMode = TemporalMode();
        if (!m_stage15TemporalOverrideActive)
        {
            Stage15QualityDescriptor desired =
                CaptureCurrentStage15Quality();
            const Stage11TemporalMode desiredMode =
                currentMode == Stage11TemporalMode::Off
                    ? (desired.resolutionPreset ==
                            Stage10ResolutionPreset::Full
                        ? Stage11TemporalMode::FullResolution
                        : Stage11TemporalMode::Stable4Phase)
                    : Stage11TemporalMode::Off;
            desired.temporalMode = desiredMode;

            succeeded = ApplyStage15QualityDescriptor(
                desired, m_stage15QualityPreset, false, false);
            if (succeeded)
            {
                m_stage15TemporalOverrideActive = true;
                m_stage15TemporalOverrideMode = desiredMode;
                m_stage15TemporalOverrideRestoreMode = currentMode;
                changed = true;
            }
        }
        else
        {
            Stage15QualityDescriptor restored =
                CaptureCurrentStage15Quality();
            restored.temporalMode = m_stage15TemporalOverrideRestoreMode;
            succeeded = ApplyStage15QualityDescriptor(
                restored, m_stage15QualityPreset, false, false);
            if (succeeded)
            {
                m_stage15TemporalOverrideActive = false;
                m_stage15TemporalOverrideMode = restored.temporalMode;
                m_stage15TemporalOverrideRestoreMode =
                    restored.temporalMode;
                changed = true;
            }
        }
    }
    if (succeeded && request.diagnosticMode >= 0)
    {
        const Stage15DiagnosticMode requested =
            static_cast<Stage15DiagnosticMode>(request.diagnosticMode);
        changed = changed || requested != m_stage15DiagnosticMode;
        succeeded = ApplyStage15DiagnosticModeImmediate(requested, false);
    }
    if (succeeded && changed)
    {
        ResetTemporalHistory(Stage11HistoryResetReason::ParametersChanged);
        m_frameProfiler.ResetMeasurements();
        ++m_stage15TransitionCommitCount;
        m_stage15TransitionStatus = "Committed once at frame boundary";
    }
    else if (succeeded)
    {
        m_stage15TransitionStatus = "No-op; resolved fingerprint unchanged";
    }
    else if (!succeeded)
    {
        rollback();
        // 실패한 요청을 보존해 UI/자동 테스트가 무시된 transition을 관찰한다.
        retainFailedRequest();
    }
}

void Renderer::ApplyStage9OptimizationPreset(Stage9OptimizationPreset preset)
{
    if (m_stage15DiagnosticMode != Stage15DiagnosticMode::None)
        return;
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
    if (m_stage15DiagnosticMode != Stage15DiagnosticMode::None)
        return;
    if (preset == Stage10ResolutionPreset::Custom)
        return;
    stage10upsampling::ApplyResolutionPreset(
        m_upsamplingParameters, preset);
    m_upsamplingParameters = stage10upsampling::Sanitize(
        m_upsamplingParameters);
    m_resolutionPreset = preset;
    if (TemporalMode() != Stage11TemporalMode::Off)
    {
        const Stage11TemporalMode resolutionMode =
            preset == Stage10ResolutionPreset::Full
                ? Stage11TemporalMode::FullResolution
                : Stage11TemporalMode::Stable4Phase;
        stage11temporal::ApplyMode(m_temporalParameters, resolutionMode);
        if (!m_stage15TemporalOverrideActive)
        {
            m_stage15TemporalOverrideMode = resolutionMode;
            m_stage15TemporalOverrideRestoreMode = resolutionMode;
        }
    }
    m_frameProfiler.ResetMeasurements();
    ResetTemporalHistory(Stage11HistoryResetReason::ResolutionOrFilter);
}

void Renderer::SetStage10UpsampleFilter(Stage10UpsampleFilter filter)
{
    if (m_stage15DiagnosticMode != Stage15DiagnosticMode::None)
        return;
    m_upsamplingParameters.filterMode = static_cast<std::uint32_t>(filter);
    m_upsamplingParameters = stage10upsampling::Sanitize(
        m_upsamplingParameters);
    m_frameProfiler.ResetMeasurements();
    ResetTemporalHistory(Stage11HistoryResetReason::ResolutionOrFilter);
}

void Renderer::SetStage11TemporalMode(Stage11TemporalMode mode)
{
    if (m_stage15DiagnosticMode != Stage15DiagnosticMode::None)
        return;
    const Stage11TemporalMode previous = TemporalMode();
    stage11temporal::ApplyMode(m_temporalParameters, mode);
    m_temporalParameters = stage11temporal::Sanitize(m_temporalParameters);
    if (previous != mode)
    {
        m_stage15QualityPreset = Stage15QualityPreset::Custom;
        m_stage15TemporalOverrideActive = false;
        m_stage15TemporalOverrideMode = mode;
        m_stage15TemporalOverrideRestoreMode = mode;
        ResetTemporalHistory(Stage11HistoryResetReason::TemporalToggle);
        m_frameProfiler.ResetMeasurements();
    }
}

void Renderer::SetStage12ShadowMode(Stage12ShadowMode mode)
{
    if (m_stage15DiagnosticMode != Stage15DiagnosticMode::None)
        return;
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
    if (m_stage15DiagnosticMode != Stage15DiagnosticMode::None)
        return false;
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
    m_lastTemporalResetReason = reason;
    m_temporalResetFrames[m_temporalResetFrameCursor] =
        std::max<std::uint64_t>(m_renderFrameSerial, 1u);
    m_temporalResetFrameCursor =
        (m_temporalResetFrameCursor + 1u) % m_temporalResetFrames.size();
    m_temporalHistoryValid = false;
    m_temporalPreviousFrameValid = false;
    m_temporalHistoryReadIndex = 0;
    m_temporalAccumulatedFrames = 0;
    m_temporalParameters.frameIndex = 0;
    m_temporalParameters.historyValid = 0;
    m_temporalParameters.deltaTimeSeconds = 0.0f;
    m_temporalParameters.jitterOffsetLowResTexels = {};
    m_temporalParameters.resetReason = static_cast<std::uint32_t>(reason);
    m_temporalStatisticsValid = false;
    // reset 전에 copy된 staging 값은 이전 history 세대다. pending을
    // 폐기해야 다음 몇 프레임에 오래된 accepted/weight가 다시 보이지 않는다.
    m_temporalStatisticsPending = {};
    m_temporalHistoryValidPercent = 0.0f;
    m_temporalAverageHistoryWeight = 0.0f;
}

void Renderer::ConfigureStage9ConeForValidation(std::uint32_t taps,
                                                 float angleDegrees,
                                                 float farSampleFraction)
{
    if (m_stage15DiagnosticMode != Stage15DiagnosticMode::None)
        return;
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
    const cloudshapedomain::FitResult fit = cloudshapedomain::EvaluateFit(
        m_cloudShapeParameters, safe, m_cloudDomainParameters);
    if (!fit.valid)
    {
        m_cloudAppearanceStatus =
            "Cloud type rejected: active local thickness does not fit domain";
        return false;
    }
    CloudDomainParameters domain = m_cloudDomainParameters;
    domain.cloudLightingReferenceAltitudeMeters =
        cloudshapedomain::LightingReferenceAltitudeMeters(
            m_cloudShapeParameters, safe, domain);
    domain = SanitizeCloudDomainParameters(domain);
    if (!UpdateWeatherMapTexture(m_weatherPreset, settings))
        return false;
    m_cloudDomainParameters = domain;
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
    // Domain 수치만 먼저 초기화하면 이전 appearance가 남긴 LUT 대표 고도가
    // Stage 13 preset 재적용 뒤에도 잔존한다. 최종 shape/type이 확정된 시점에
    // 파생값을 다시 계산해 preset을 완전한 원자 상태로 만든다.
    m_cloudDomainParameters.cloudLightingReferenceAltitudeMeters =
        cloudshapedomain::LightingReferenceAltitudeMeters(
            m_cloudShapeParameters, m_cloudTypeMode,
            m_cloudDomainParameters);
    m_cloudDomainParameters = SanitizeCloudDomainParameters(
        m_cloudDomainParameters);
    m_cameraMoveSpeedMetersPerSecond =
        stage13scene::kMoveSpeedMetersPerSecond;
    m_cloudAppearancePreset = CloudAppearancePreset::DenseMixedDefault;
    m_cloudAppearanceDirty = false;
    m_cloudAppearanceStatus =
        "Deterministic Dense Mixed default applied (saved Custom not auto-applied)";
    // 이 함수는 schema/CLI 회귀용 Stage 13 상태를 직접 구성한다. 이전 F1/F4
    // 저장 target이 남으면 뒤의 Weather preset 전환이 Stage 15 formation으로
    // 오인되므로 authoring provenance를 명시적으로 해제한다.
    m_cloudFormationTargetValid = false;
    m_cloudFormationCloudDirty = false;
    m_cloudFormationSceneDirty = false;
    m_cloudFormationSource = CloudFormationPresetSource::BuiltIn;
    m_cloudFormationStatus = "Legacy Stage 13 open-world regression state";
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

CloudFormationSettings Renderer::CurrentCloudFormation() const
{
    return CaptureCloudFormationSettings(
        m_cloudParameters, m_cloudShapeParameters, m_cloudDomainParameters,
        m_weatherPreset, m_weatherGeneratorSettings,
        m_noiseVolumeParameters);
}

bool Renderer::ApplyCloudFormationAtomic(
    const CloudFormationSettings& settings, bool resetState)
{
    PreparedCloudFormation prepared;
    std::string status;
    if (!PrepareCloudFormationSettings(settings, prepared, status, 200.0f))
    {
        m_cloudFormationStatus = status;
        m_cloudAppearanceStatus = status;
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
    m_cloudAppearancePreset = LegacyAppearanceForFormation(
        prepared.settings);
    m_cloudAppearanceDirty = false;
    m_pipelineComparisonActive = false;
    m_openWorldPipelinePreset = OpenWorldPipelinePreset::Custom;
    m_cloudFormationStatus = "Formation applied atomically";
    if (resetState)
        ResetTemporalHistory(Stage11HistoryResetReason::ParametersChanged);
    return true;
}

bool Renderer::ApplyCloudFormationPresetTarget(
    const CloudFormationPresetTarget& target, bool allowUserOverrides,
    bool resetState)
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
    if (!ApplyCloudFormationAtomic(settings, resetState))
        return false;

    m_cloudFormationTarget = target;
    m_cloudFormationTargetValid = true;
    m_cloudFormationSource = source;
    m_cloudFormationCloudDirty = false;
    m_cloudFormationSceneDirty = false;
    m_cloudFormationStatus = status;
    m_hasSavedCustomFormation = m_hasSavedCustomFormation ||
        target.group == CloudFormationPresetGroup::Custom;
    if (target.group != CloudFormationPresetGroup::Concept)
    {
        m_stage15ConceptPreset = Stage15ConceptPreset::Custom;
        m_stage15ConceptApplied = false;
    }
    m_noiseVolumeParameters.noiseSource = static_cast<std::uint32_t>(
        NoiseSource::Texture3D);
    return true;
}

bool Renderer::ApplyCloudFormationPresetForValidation(
    const CloudFormationPresetTarget& target, bool allowUserOverrides)
{
    return ApplyCloudFormationPresetTarget(
        target, allowUserOverrides, true);
}

bool Renderer::ApplyCloudFormationSettingsForValidation(
    const CloudFormationSettings& settings)
{
    if (!ApplyCloudFormationAtomic(settings, true))
        return false;
    MarkCloudFormationDirty();
    return true;
}

bool Renderer::SaveCurrentCloudFormationPresetForValidation()
{
    return m_cloudFormationTargetValid &&
        m_cloudFormationTarget.group != CloudFormationPresetGroup::Custom &&
        SaveCurrentCloudFormationToTarget(m_cloudFormationTarget, false);
}

bool Renderer::SaveCurrentCloudFormationAsCustomForValidation()
{
    return SaveCurrentCloudFormationToTarget(
        CustomFormationTarget(), true);
}

bool Renderer::RestoreCurrentCloudFormationBuiltInForValidation()
{
    return RestoreCurrentCloudFormationBuiltIn();
}

void Renderer::SetCloudFormationPresetRootForValidation(
    const std::filesystem::path& root)
{
    m_cloudFormationPresetRoot = root;
    RefreshSavedCustomFormationState();
}

void Renderer::SetIgnoreCloudFormationPresetOverrides(bool ignored)
{
    m_ignoreCloudFormationPresetOverrides = ignored;
    RefreshSavedCustomFormationState();
}

void Renderer::RefreshSavedCustomFormationState()
{
    if (m_ignoreCloudFormationPresetOverrides)
    {
        m_hasSavedCustomFormation = false;
        return;
    }

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
    if (!IsValidCloudFormationPresetTarget(target))
    {
        m_cloudFormationStatus = "Formation save rejected: invalid target";
        return false;
    }
    if (!switchTargetAfterSave &&
        (target.group == CloudFormationPresetGroup::Custom ||
         !m_cloudFormationTargetValid ||
         !CloudFormationPresetTargetEqual(target, m_cloudFormationTarget)))
    {
        m_cloudFormationStatus =
            "Formation save rejected: target changed before save";
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
        m_stage15ConceptPreset = Stage15ConceptPreset::Custom;
        m_stage15ConceptApplied = false;
        m_cloudFormationSceneDirty = false;
    }
    m_cloudFormationSource = CloudFormationPresetSource::UserOverride;
    m_cloudFormationCloudDirty = false;
    if (target.group == CloudFormationPresetGroup::Custom)
        m_hasSavedCustomFormation = true;
    m_cloudFormationStatus = status;
    m_cloudAppearanceDirty = false;
    return true;
}

bool Renderer::RestoreCurrentCloudFormationBuiltIn()
{
    if (!m_cloudFormationTargetValid ||
        m_cloudFormationTarget.group == CloudFormationPresetGroup::Custom)
    {
        m_cloudFormationStatus =
            "Restore Built-in is available only for F1/F4 presets";
        return false;
    }
    CloudFormationSettings builtIn;
    PreparedCloudFormation prepared;
    std::string preflightStatus;
    if (!ResolveBuiltInCloudFormation(m_cloudFormationTarget, builtIn) ||
        !PrepareCloudFormationSettings(
            builtIn, prepared, preflightStatus, 200.0f))
    {
        m_cloudFormationStatus = preflightStatus.empty()
            ? "Restore failed: built-in formation is unavailable"
            : preflightStatus;
        return false;
    }

    // override는 즉시 삭제하지 않고 같은 디렉터리의 고유 backup으로 먼저
    // 원자 이동한다. GPU Weather 적용이 실패하면 원래 이름으로 되돌려 파일과
    // runtime 중 한쪽만 Restore되는 부분 성공을 막는다.
    const std::filesystem::path overridePath = CloudFormationPresetPath(
        m_cloudFormationPresetRoot, m_cloudFormationTarget);
    std::error_code fileError;
    const bool overrideExists =
        std::filesystem::exists(overridePath, fileError);
    if (fileError)
    {
        m_cloudFormationStatus =
            "Restore failed: cannot inspect preset override";
        return false;
    }
    std::filesystem::path stagedBackup;
    if (overrideExists)
    {
        stagedBackup = overridePath.wstring() + L".restore-" +
            std::to_wstring(GetCurrentProcessId()) + L"-" +
            std::to_wstring(GetTickCount64()) + L".bak";
        if (!MoveFileExW(
                overridePath.c_str(), stagedBackup.c_str(),
                MOVEFILE_WRITE_THROUGH))
        {
            m_cloudFormationStatus =
                "Restore failed: cannot stage preset override";
            return false;
        }
    }
    if (!ApplyCloudFormationAtomic(prepared.settings, true))
    {
        if (!stagedBackup.empty() &&
            !MoveFileExW(
                stagedBackup.c_str(), overridePath.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            m_cloudFormationStatus +=
                "; override recovery failed, backup preserved at " +
                stagedBackup.string();
        }
        return false;
    }
    std::string removeStatus = std::string(
        CloudFormationPresetTargetName(m_cloudFormationTarget));
    if (!stagedBackup.empty())
    {
        std::filesystem::remove(stagedBackup, fileError);
        removeStatus += fileError
            ? " override staged backup could not be deleted"
            : " override removed";
    }
    else
    {
        removeStatus += " already uses built-in values";
    }
    m_cloudFormationSource = CloudFormationPresetSource::BuiltIn;
    m_cloudFormationCloudDirty = false;
    m_cloudFormationStatus = removeStatus + "; built-in applied";
    return true;
}

void Renderer::MarkCloudFormationDirty()
{
    m_cloudFormationCloudDirty = true;
    m_cloudFormationSource = CloudFormationPresetSource::Unsaved;
    m_stage15ConceptApplied = false;
    if (!m_cloudFormationTargetValid ||
        m_cloudFormationTarget.group != CloudFormationPresetGroup::Concept)
    {
        m_stage15ConceptPreset = Stage15ConceptPreset::Custom;
    }
    m_cloudAppearancePreset = CloudAppearancePreset::CustomUnsaved;
    m_cloudAppearanceDirty = true;
    m_pipelineComparisonActive = false;
    m_openWorldPipelinePreset = OpenWorldPipelinePreset::Custom;
    m_cloudFormationStatus = m_cloudFormationTargetValid
        ? std::string("Unsaved formation changes for ") +
            CloudFormationPresetTargetName(m_cloudFormationTarget)
        : "Unsaved formation changes (no preset target)";
}

void Renderer::MarkCloudFormationSceneDirty()
{
    m_cloudFormationSceneDirty = true;
    m_stage15ConceptApplied = false;
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
    CloudDomainParameters domain = m_cloudDomainParameters;
    const cloudshapedomain::FitResult fit = cloudshapedomain::EvaluateFit(
        shape, weather.cloudTypeMode, domain);
    if (!fit.valid)
    {
        std::ostringstream status;
        status << "Appearance rejected: local shape requires "
               << fit.requiredLayerThicknessMeters
               << " m, current domain provides "
               << fit.availableLayerThicknessMeters << " m";
        m_cloudAppearanceStatus = status.str();
        return false;
    }
    domain.cloudLightingReferenceAltitudeMeters =
        cloudshapedomain::LightingReferenceAltitudeMeters(
            shape, weather.cloudTypeMode, domain);
    domain = SanitizeCloudDomainParameters(domain);
    if (!UpdateWeatherMapTexture(Stage5WeatherPreset::PeriodicPerlin, weather))
    {
        m_cloudAppearanceStatus =
            "Appearance Weather update failed; current renderer values retained";
        return false;
    }
    m_cloudParameters = cloud;
    m_cloudShapeParameters = shape;
    m_cloudDomainParameters = domain;
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
    m_stage15ConceptPreset = Stage15ConceptPreset::Custom;
    m_cloudAppearanceStatus = std::string(CloudAppearancePresetName(preset)) +
        " appearance applied; camera, weather placement, wind, lighting, and sampling preserved";
    return true;
}

void Renderer::MarkCloudAppearanceDirty()
{
    MarkCloudFormationDirty();
    m_cloudAppearanceStatus = m_cloudFormationStatus;
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
    if (!changed)
        return true;

    CloudFormationSettings formation = CurrentCloudFormation();
    if (m_cloudFormationTargetValid &&
        IsValidCloudFormationSettings(formation))
    {
        formation.weather = safeSettings;
        return ApplyCloudFormationAtomic(formation, true);
    }

    // 구형 schema/CLI 회귀 경로는 CloudFormationSettings가 소유하지 않는
    // Legacy shape도 다루므로 기존 제한된 transaction을 보존한다.
    const cloudshapedomain::FitResult fit = cloudshapedomain::EvaluateFit(
        m_cloudShapeParameters, safeSettings.cloudTypeMode,
        m_cloudDomainParameters, 200.0f);
    if (!fit.valid)
    {
        m_weatherMapStatus =
            "Weather edit rejected: active local thickness does not fit domain";
        return false;
    }
    CloudDomainParameters domain = m_cloudDomainParameters;
    domain.cloudLightingReferenceAltitudeMeters =
        cloudshapedomain::LightingReferenceAltitudeMeters(
            m_cloudShapeParameters, safeSettings.cloudTypeMode, domain);
    domain = SanitizeCloudDomainParameters(domain);
    if (!UpdateWeatherMapTexture(m_weatherPreset, safeSettings))
        return false;
    m_cloudDomainParameters = domain;
    if (changed)
    {
        m_stage15ConceptApplied = false;
        m_openWorldPipelinePreset = OpenWorldPipelinePreset::Custom;
        ResetTemporalHistory(Stage11HistoryResetReason::ParametersChanged);
    }
    return true;
}

std::uint64_t Renderer::Stage15StateFingerprint() const
{
    // 모든 구조체는 생성·sanitize 때 0 초기화된다. 포인터 값은 실행마다 달라지므로
    // 제외하고 GPU 결과를 소유하는 값과 실제 texture/shader content hash만 묶는다.
    std::uint64_t hash = 1469598103934665603ull;
    HashBytes(hash, &m_cloudParameters, sizeof(m_cloudParameters));
    HashBytes(hash, &m_cloudDomainParameters, sizeof(m_cloudDomainParameters));
    HashBytes(hash, &m_cloudShapeParameters, sizeof(m_cloudShapeParameters));
    HashBytes(hash, &m_cloudLodParameters, sizeof(m_cloudLodParameters));
    HashBytes(hash, &m_optimizationParameters, sizeof(m_optimizationParameters));
    HashBytes(hash, &m_upsamplingParameters, sizeof(m_upsamplingParameters));
    // 이전 행렬/history/jitter는 설정이 아니라 frame-local 실행 상태다.
    // 같은 canonical preset은 몇 frame을 그렸는지와 무관하게 같은
    // fingerprint를 가져야 한다.
    Stage11TemporalParameters temporalConfiguration = m_temporalParameters;
    temporalConfiguration.previousViewProjection = {};
    temporalConfiguration.previousCameraPosition = {};
    temporalConfiguration.deltaTimeSeconds = 0.0f;
    temporalConfiguration.jitterOffsetLowResTexels = {};
    temporalConfiguration.frameIndex = 0u;
    temporalConfiguration.historyValid = 0u;
    temporalConfiguration.resetReason = 0u;
    HashBytes(hash, &temporalConfiguration, sizeof(temporalConfiguration));
    HashBytes(hash, &m_shadowParameters, sizeof(m_shadowParameters));
    HashBytes(hash, &m_lightParameters, sizeof(m_lightParameters));
    HashBytes(hash, &m_environmentParameters, sizeof(m_environmentParameters));
    HashBytes(hash, &m_cloudRimParameters, sizeof(m_cloudRimParameters));
    HashBytes(hash, &m_atmosphereParameters, sizeof(m_atmosphereParameters));
    HashBytes(hash, &m_groundLightingParameters,
              sizeof(m_groundLightingParameters));
    HashBytes(hash, &m_toneMappingParameters, sizeof(m_toneMappingParameters));
    HashValue(hash, m_stage15QualityPreset);
    HashValue(hash, m_stage15ConceptPreset);
    HashValue(hash, m_stage15DiagnosticMode);
    HashValue(hash, m_stage15TemporalOverrideActive);
    HashValue(hash, m_stage15TemporalOverrideMode);
    HashValue(hash, m_stage15TemporalOverrideRestoreMode);
    HashValue(hash, m_weatherMapHash);
    HashValue(hash, m_baseNoiseVolumeHash);
    HashValue(hash, m_detailNoiseVolumeHash);
    HashValue(hash, m_cloudRenderWidth);
    HashValue(hash, m_cloudRenderHeight);
    HashValue(hash, m_nonCirrusRaymarchShaderHash);
    HashValue(hash, m_nonCirrusDeepShadowShaderHash);
    HashValue(hash, m_cirrusDeepShadowShaderHash);
    HashValue(hash, m_cloudCompositeShaderHash);
    return hash;
}

std::uint64_t Renderer::Stage15GpuResourceIdentityFingerprint() const
{
    // rollback 검증 전용이다. canonical 상태 fingerprint와 달리 같은 실행 안에서
    // 실제 D3D 객체가 교체됐다가 되돌아왔는지 포인터 identity까지 비교한다.
    std::uint64_t hash = 1469598103934665603ull;
    const auto append = [&](const void* pointer)
    {
        const std::uintptr_t identity =
            reinterpret_cast<std::uintptr_t>(pointer);
        HashValue(hash, identity);
    };
    append(m_weatherMapTexture.Get());
    append(m_weatherMapSrv.Get());
    append(m_cloudScatteringTransmittance.Get());
    append(m_cloudScatteringTransmittanceRtv.Get());
    append(m_cloudScatteringTransmittanceSrv.Get());
    append(m_cloudDepthSceneLimit.Get());
    append(m_cloudDepthSceneLimitRtv.Get());
    append(m_cloudDepthSceneLimitSrv.Get());
    append(m_shadowNearTexture.Get());
    append(m_shadowNearSrv.Get());
    append(m_shadowNearUav.Get());
    append(m_shadowFarTexture.Get());
    append(m_shadowFarSrv.Get());
    append(m_shadowFarUav.Get());
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
    const std::uint64_t raymarchHash = m_nonCirrusRaymarchShaderHash;
    const std::uint64_t shadowHash = m_nonCirrusDeepShadowShaderHash;
    const std::uint64_t cirrusShadowHash = m_cirrusDeepShadowShaderHash;
    const std::uint64_t compositeHash = m_cloudCompositeShaderHash;
    m_shaderCompileCallCount = 0;
    m_shaderCacheHitCount = 0;
    if (!CreateShaders(false))
        return false;
    return m_shaderCompileCallCount == 0u && m_shaderCacheHitCount > 0u &&
        raymarchHash == m_nonCirrusRaymarchShaderHash &&
        shadowHash == m_nonCirrusDeepShadowShaderHash &&
        cirrusShadowHash == m_cirrusDeepShadowShaderHash &&
        compositeHash == m_cloudCompositeShaderHash;
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

void Renderer::InjectStage15TransitionFailureForTest(
    Stage15TransitionFailurePoint point)
{
    switch (point)
    {
    case Stage15TransitionFailurePoint::CloudTargetPreflight:
        m_stage15FailCloudTargetPreflight = true;
        break;
    case Stage15TransitionFailurePoint::ShadowResourcePreflight:
        m_stage15FailShadowResourcePreflight = true;
        break;
    case Stage15TransitionFailurePoint::WeatherPreflight:
        m_stage15FailWeatherPreflight = true;
        break;
    }
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

bool Renderer::ExportNoiseLabSnapshot(const std::filesystem::path& root,
                                      bool includePng)
{
    m_noiseLab.SynchronizeStage15Snapshot(
        m_stage15QualityPreset, m_stage15ConceptPreset,
        m_stage15DiagnosticMode, m_stage15TemporalOverrideActive);
    return m_noiseLab.ExportSnapshot(
        root, m_cloudParameters, m_cloudShapeParameters, m_cloudDomainParameters,
        m_cloudLodParameters,
        m_optimizationParameters, m_optimizationPreset,
        m_upsamplingParameters, m_resolutionPreset,
        m_temporalParameters,
        m_shadowParameters,
        m_cloudRenderWidth, m_cloudRenderHeight,
        m_lightParameters, m_sunPreset, m_phasePreset,
        m_environmentParameters, m_environmentPreset, m_cloudRimParameters,
        m_weatherPreset,
        m_cloudTypeMode,
        m_cloudAppearancePreset, m_cloudAppearanceDirty,
        m_hasSavedCustomAppearance, m_savedCustomAppearance,
        m_noiseVolumeParameters, m_baseNoiseVolumeHash,
        m_detailNoiseVolumeHash,
        m_weatherGeneratorSettings, m_weatherMapHash, m_weatherMapTexture.Get(),
        std::filesystem::path(m_shaderDir) / L"Noise.hlsli", includePng);
}

std::uint64_t Renderer::NoiseLabPreviewHash(std::size_t targetIndex)
{
    return m_noiseLab.PreviewHash(targetIndex);
}
