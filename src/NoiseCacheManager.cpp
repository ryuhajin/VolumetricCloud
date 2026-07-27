#include "NoiseCacheManager.h"

#include <atomic>
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>

using Microsoft::WRL::ComPtr;

namespace
{
constexpr uint32_t kManifestMagic = 0x48434356; // VCCH
constexpr uint32_t kVolumeMagic = 0x4E434356;   // VCCN
constexpr uint32_t kCacheVersion = 6; // v6: 128^3 detail과 224바이트 월드 공간 파라미터
constexpr DXGI_FORMAT kBaseVolumeFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT kDetailVolumeFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT kWeatherFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr const char* kShaderNames[] = {
    "main_vs.cso", "main_ps.cso", "preview_vs.cso", "preview_ps.cso",
    "noise_cs_base.cso", "noise_cs_detail.cso", "noise_cs_weather.cso"
};

uint32_t BytesPerTexel(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_R8_UNORM:       return 1;
    case DXGI_FORMAT_R8G8B8A8_UNORM: return 4;
    default:                         return 0; // 미지원
    }
}

struct CacheManifest
{
    uint32_t magic;
    uint32_t version;
    uint64_t sourceHash;
    uint64_t parameterHash;
    uint32_t baseSize;
    uint32_t detailSize;
    uint32_t baseFormat;   // (구 format) base 볼륨 DXGI_FORMAT
    uint32_t detailFormat; // (구 reserved) detail 볼륨 DXGI_FORMAT
    uint32_t weatherSize;
    uint32_t weatherFormat;
    CloudParameters params;
};

struct VolumeHeader
{
    uint32_t magic;
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t format;
    uint64_t byteSize;
};

uint64_t HashBytes(uint64_t hash, const void* data, size_t size)
{
    constexpr uint64_t prime = 1099511628211ull;
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i)
    {
        hash ^= bytes[i];
        hash *= prime;
    }
    return hash;
}

bool ReadFile(const std::filesystem::path& path, std::vector<unsigned char>& bytes)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;
    const std::streamsize size = file.tellg();
    if (size <= 0) return false;
    bytes.resize(static_cast<size_t>(size));
    file.seekg(0);
    return file.read(reinterpret_cast<char*>(bytes.data()), size).good();
}

bool WriteFile(const std::filesystem::path& path, const void* data, size_t size)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file || !file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size)).good())
        return false;
    file.flush();
    return file.good();
}

std::string WindowsErrorMessage(DWORD error)
{
    char* message = nullptr;
    const DWORD size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, 0, reinterpret_cast<char*>(&message), 0, nullptr);
    std::string result = size && message ? std::string(message, size) : "unknown Windows error";
    if (message) LocalFree(message);
    while (!result.empty() && (result.back() == '\r' || result.back() == '\n'))
        result.pop_back();
    return result;
}

bool MoveWithRetry(const std::filesystem::path& from,
                   const std::filesystem::path& to,
                   DWORD flags,
                   std::string& error)
{
    constexpr DWORD delaysMs[] = { 0, 10, 25, 50, 100 };
    for (DWORD delay : delaysMs)
    {
        if (delay) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        if (MoveFileExW(from.c_str(), to.c_str(), flags)) return true;
        const DWORD code = GetLastError();
        error = "Win32 " + std::to_string(code) + ": " + WindowsErrorMessage(code);
        if (code != ERROR_SHARING_VIOLATION && code != ERROR_ACCESS_DENIED &&
            code != ERROR_LOCK_VIOLATION && code != ERROR_ALREADY_EXISTS)
            break;
    }
    return false;
}

bool IsSafeBundleName(const std::wstring& name)
{
    return !name.empty() && name != L"." && name != L".." &&
        name.find(L'/') == std::wstring::npos && name.find(L'\\') == std::wstring::npos &&
        name.rfind(L"bundle-v", 0) == 0;
}

bool LoadBlob(const std::filesystem::path& path, ComPtr<ID3DBlob>& blob)
{
    std::vector<unsigned char> bytes;
    if (!ReadFile(path, bytes)) return false;
    ComPtr<ID3DBlob> loaded;
    if (FAILED(D3DCreateBlob(bytes.size(), &loaded))) return false;
    std::memcpy(loaded->GetBufferPointer(), bytes.data(), bytes.size());

    // 파일 길이만 정상인 임의 데이터도 blob으로는 만들 수 있다. 실제 DXBC인지
    // reflection까지 수행해 손상된 사용자 캐시가 기본 캐시 폴백을 막지 않게 한다.
    ComPtr<ID3D11ShaderReflection> reflection;
    if (FAILED(D3DReflect(loaded->GetBufferPointer(), loaded->GetBufferSize(),
                          __uuidof(ID3D11ShaderReflection), &reflection)))
        return false;
    blob = loaded;
    return true;
}

bool LoadVolume(const std::filesystem::path& path,
                ID3D11Device* device,
                UINT expectedSize,
                DXGI_FORMAT expectedFormat,
                ComPtr<ID3D11Texture3D>& texture,
                ComPtr<ID3D11UnorderedAccessView>& uav,
                ComPtr<ID3D11ShaderResourceView>& srv)
{
    const uint32_t bpt = BytesPerTexel(expectedFormat);
    if (bpt == 0) return false;
    std::ifstream file(path, std::ios::binary);
    VolumeHeader header = {};
    if (!file.read(reinterpret_cast<char*>(&header), sizeof(header))) return false;
    if (header.magic != kVolumeMagic || header.version != kCacheVersion ||
        header.width != expectedSize || header.height != expectedSize || header.depth != expectedSize ||
        header.format != static_cast<uint32_t>(expectedFormat) ||
        header.byteSize != static_cast<uint64_t>(expectedSize) * expectedSize * expectedSize * bpt)
        return false;

    std::vector<unsigned char> data(static_cast<size_t>(header.byteSize));
    if (!file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()))) return false;

    D3D11_TEXTURE3D_DESC desc = {};
    desc.Width = expectedSize;
    desc.Height = expectedSize;
    desc.Depth = expectedSize;
    desc.MipLevels = 1;
    desc.Format = expectedFormat;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

    D3D11_SUBRESOURCE_DATA initial = {};
    initial.pSysMem = data.data();
    initial.SysMemPitch = expectedSize * bpt;
    initial.SysMemSlicePitch = expectedSize * expectedSize * bpt;

    ComPtr<ID3D11Texture3D> newTexture;
    ComPtr<ID3D11UnorderedAccessView> newUav;
    ComPtr<ID3D11ShaderResourceView> newSrv;
    if (FAILED(device->CreateTexture3D(&desc, &initial, &newTexture))) return false;
    if (FAILED(device->CreateUnorderedAccessView(newTexture.Get(), nullptr, &newUav))) return false;
    if (FAILED(device->CreateShaderResourceView(newTexture.Get(), nullptr, &newSrv))) return false;
    texture = newTexture;
    uav = newUav;
    srv = newSrv;
    return true;
}

bool SaveVolume(const std::filesystem::path& path,
                ID3D11Device* device,
                ID3D11DeviceContext* context,
                ID3D11Texture3D* texture)
{
    if (!texture) return false;
    D3D11_TEXTURE3D_DESC desc = {};
    texture->GetDesc(&desc);
    const uint32_t bpt = BytesPerTexel(desc.Format);
    if (bpt == 0) return false;

    D3D11_TEXTURE3D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;
    ComPtr<ID3D11Texture3D> staging;
    if (FAILED(device->CreateTexture3D(&stagingDesc, nullptr, &staging))) return false;
    context->CopyResource(staging.Get(), texture);

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
    const size_t rowBytes = static_cast<size_t>(desc.Width) * bpt;
    const size_t sliceBytes = rowBytes * desc.Height;
    std::vector<unsigned char> data(sliceBytes * desc.Depth);
    for (UINT z = 0; z < desc.Depth; ++z)
    {
        for (UINT y = 0; y < desc.Height; ++y)
        {
            const auto* source = static_cast<const unsigned char*>(mapped.pData) +
                static_cast<size_t>(z) * mapped.DepthPitch + static_cast<size_t>(y) * mapped.RowPitch;
            std::memcpy(data.data() + static_cast<size_t>(z) * sliceBytes + static_cast<size_t>(y) * rowBytes,
                        source, rowBytes);
        }
    }
    context->Unmap(staging.Get(), 0);

    VolumeHeader header = { kVolumeMagic, kCacheVersion, desc.Width, desc.Height, desc.Depth,
                            static_cast<uint32_t>(desc.Format), data.size() };
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    return file &&
        file.write(reinterpret_cast<const char*>(&header), sizeof(header)).good() &&
        file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size())).good();
}

bool LoadWeatherMap(const std::filesystem::path& path,
                    ID3D11Device* device,
                    UINT expectedSize,
                    WeatherMapResources& weather)
{
    const uint32_t bpt = BytesPerTexel(kWeatherFormat);
    std::ifstream file(path, std::ios::binary);
    VolumeHeader header = {};
    if (!file.read(reinterpret_cast<char*>(&header), sizeof(header))) return false;
    if (header.magic != kVolumeMagic || header.version != kCacheVersion ||
        header.width != expectedSize || header.height != expectedSize || header.depth != 1 ||
        header.format != static_cast<uint32_t>(kWeatherFormat) ||
        header.byteSize != static_cast<uint64_t>(expectedSize) * expectedSize * bpt)
        return false;

    std::vector<unsigned char> data(static_cast<size_t>(header.byteSize));
    if (!file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()))) return false;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = expectedSize;
    desc.Height = expectedSize;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = kWeatherFormat;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    D3D11_SUBRESOURCE_DATA initial = { data.data(), expectedSize * bpt, 0 };

    WeatherMapResources loaded;
    if (FAILED(device->CreateTexture2D(&desc, &initial, &loaded.texture))) return false;
    if (FAILED(device->CreateUnorderedAccessView(loaded.texture.Get(), nullptr, &loaded.uav))) return false;
    if (FAILED(device->CreateShaderResourceView(loaded.texture.Get(), nullptr, &loaded.srv))) return false;
    weather = loaded;
    return true;
}

bool SaveWeatherMap(const std::filesystem::path& path,
                    ID3D11Device* device,
                    ID3D11DeviceContext* context,
                    ID3D11Texture2D* texture)
{
    if (!texture) return false;
    D3D11_TEXTURE2D_DESC desc = {};
    texture->GetDesc(&desc);
    const uint32_t bpt = BytesPerTexel(desc.Format);
    if (bpt == 0 || desc.ArraySize != 1 || desc.MipLevels != 1) return false;

    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&stagingDesc, nullptr, &staging))) return false;
    context->CopyResource(staging.Get(), texture);

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
    const size_t rowBytes = static_cast<size_t>(desc.Width) * bpt;
    std::vector<unsigned char> data(rowBytes * desc.Height);
    for (UINT y = 0; y < desc.Height; ++y)
    {
        const auto* source = static_cast<const unsigned char*>(mapped.pData) +
            static_cast<size_t>(y) * mapped.RowPitch;
        std::memcpy(data.data() + static_cast<size_t>(y) * rowBytes, source, rowBytes);
    }
    context->Unmap(staging.Get(), 0);

    VolumeHeader header = { kVolumeMagic, kCacheVersion, desc.Width, desc.Height, 1,
                            static_cast<uint32_t>(desc.Format), data.size() };
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    return file &&
        file.write(reinterpret_cast<const char*>(&header), sizeof(header)).good() &&
        file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size())).good();
}
}

void NoiseCacheManager::Init(const std::filesystem::path& defaultCacheRoot,
                             const std::vector<std::wstring>& sourcePaths)
{
    m_defaultCacheRoot = defaultCacheRoot;
    const wchar_t* localAppData = _wgetenv(L"LOCALAPPDATA");
    m_userCacheRoot = std::filesystem::path(localAppData ? localAppData : L".") /
        L"VolumetricCloud" / L"cache";
    m_sourcePaths = sourcePaths;
}

uint64_t NoiseCacheManager::SourceHash() const
{
    uint64_t hash = 1469598103934665603ull;
    for (const auto& path : m_sourcePaths)
    {
        std::vector<unsigned char> bytes;
        if (!ReadFile(path, bytes)) return 0;
        hash = HashBytes(hash, bytes.data(), bytes.size());
    }
    return hash;
}

uint64_t NoiseCacheManager::ParameterHash(const CloudParameters& p)
{
    uint64_t hash = 1469598103934665603ull;
    hash = HashBytes(hash, &p.noiseWorldScale, sizeof(p.noiseWorldScale));
    hash = HashBytes(hash, &p.basePeriod, sizeof(p.basePeriod));
    hash = HashBytes(hash, &p.detailPeriod, sizeof(p.detailPeriod));
    hash = HashBytes(hash, &p.seed, sizeof(p.seed));
    hash = HashBytes(hash, &p.baseOctaves, sizeof(p.baseOctaves));
    hash = HashBytes(hash, &p.detailOctaves, sizeof(p.detailOctaves));
    hash = HashBytes(hash, &p.weatherSeed, sizeof(p.weatherSeed));
    return hash;
}

bool NoiseCacheManager::LoadPreferred(
    ID3D11Device* device, CloudParameters& params, ShaderBlobArray& shaderBlobs,
    std::array<ComPtr<ID3D11Texture3D>, 2>& volumes,
    std::array<ComPtr<ID3D11UnorderedAccessView>, 2>& uavs,
    std::array<ComPtr<ID3D11ShaderResourceView>, 2>& srvs,
    WeatherMapResources& weather,
    bool& sourceModified)
{
    m_lastError.clear();
    if (LoadBundle(m_userCacheRoot, device, params, shaderBlobs, volumes, uavs, srvs, weather, sourceModified)) return true;
    return LoadBundle(m_defaultCacheRoot, device, params, shaderBlobs, volumes, uavs, srvs, weather, sourceModified);
}

bool NoiseCacheManager::LoadBundle(
    const std::filesystem::path& root, ID3D11Device* device, CloudParameters& params,
    ShaderBlobArray& shaderBlobs, std::array<ComPtr<ID3D11Texture3D>, 2>& volumes,
    std::array<ComPtr<ID3D11UnorderedAccessView>, 2>& uavs,
    std::array<ComPtr<ID3D11ShaderResourceView>, 2>& srvs,
    WeatherMapResources& weather, bool& sourceModified)
{
    return LoadBundleDirectory(ResolveBundleDirectory(root), device, params, shaderBlobs,
                               volumes, uavs, srvs, weather, sourceModified);
}

std::filesystem::path NoiseCacheManager::ResolveBundleDirectory(
    const std::filesystem::path& root) const
{
    std::wifstream pointer(root / L"active-bundle.txt");
    std::wstring name;
    if (pointer && std::getline(pointer, name) && IsSafeBundleName(name))
    {
        const auto generation = root / name;
        if (std::filesystem::is_directory(generation)) return generation;
    }
    return root / L"bundle";
}

bool NoiseCacheManager::LoadBundleDirectory(
    const std::filesystem::path& bundle, ID3D11Device* device, CloudParameters& params,
    ShaderBlobArray& shaderBlobs, std::array<ComPtr<ID3D11Texture3D>, 2>& volumes,
    std::array<ComPtr<ID3D11UnorderedAccessView>, 2>& uavs,
    std::array<ComPtr<ID3D11ShaderResourceView>, 2>& srvs,
    WeatherMapResources& weather, bool& sourceModified)
{
    CacheManifest manifest = {};
    std::ifstream file(bundle / L"manifest.bin", std::ios::binary);
    if (!file.read(reinterpret_cast<char*>(&manifest), sizeof(manifest))) return false;
    if (manifest.magic != kManifestMagic || manifest.version != kCacheVersion ||
        manifest.baseSize != 128 || manifest.detailSize != 128 ||
        manifest.weatherSize != 512 ||
        manifest.baseFormat != static_cast<uint32_t>(kBaseVolumeFormat) ||
        manifest.detailFormat != static_cast<uint32_t>(kDetailVolumeFormat) ||
        manifest.weatherFormat != static_cast<uint32_t>(kWeatherFormat) ||
        manifest.parameterHash != ParameterHash(manifest.params))
        return false;

    ShaderBlobArray loadedBlobs;
    for (size_t i = 0; i < loadedBlobs.size(); ++i)
        if (!LoadBlob(bundle / kShaderNames[i], loadedBlobs[i])) return false;

    std::array<ComPtr<ID3D11Texture3D>, 2> loadedVolumes;
    std::array<ComPtr<ID3D11UnorderedAccessView>, 2> loadedUavs;
    std::array<ComPtr<ID3D11ShaderResourceView>, 2> loadedSrvs;
    if (!LoadVolume(bundle / L"base.vcnoise", device, 128, kBaseVolumeFormat, loadedVolumes[0], loadedUavs[0], loadedSrvs[0])) return false;
    if (!LoadVolume(bundle / L"detail.vcnoise", device, 128, kDetailVolumeFormat, loadedVolumes[1], loadedUavs[1], loadedSrvs[1])) return false;
    WeatherMapResources loadedWeather;
    if (!LoadWeatherMap(bundle / L"weather.vcnoise", device, 512, loadedWeather)) return false;

    params = manifest.params;
    shaderBlobs = loadedBlobs;
    volumes = loadedVolumes;
    uavs = loadedUavs;
    srvs = loadedSrvs;
    weather = loadedWeather;
    sourceModified = manifest.sourceHash != SourceHash();
    return true;
}

bool NoiseCacheManager::SaveUser(ID3D11Device* device, ID3D11DeviceContext* context,
                                  const CloudParameters& params, const ShaderBlobArray& shaderBlobs,
                                  const std::array<ComPtr<ID3D11Texture3D>, 2>& volumes,
                                  const WeatherMapResources& weather)
{
    return SaveBundle(m_userCacheRoot, device, context, params, shaderBlobs, volumes, weather);
}

bool NoiseCacheManager::SaveDefault(ID3D11Device* device, ID3D11DeviceContext* context,
                                     const CloudParameters& params, const ShaderBlobArray& shaderBlobs,
                                     const std::array<ComPtr<ID3D11Texture3D>, 2>& volumes,
                                     const WeatherMapResources& weather)
{
    return SaveBundle(m_defaultCacheRoot, device, context, params, shaderBlobs, volumes, weather);
}

bool NoiseCacheManager::RunRoundTripTest(
    ID3D11Device* device, ID3D11DeviceContext* context, const CloudParameters& params,
    const ShaderBlobArray& shaderBlobs, const std::array<ComPtr<ID3D11Texture3D>, 2>& volumes,
    const WeatherMapResources& weather)
{
    const auto testRoot = std::filesystem::temp_directory_path() / L"VolumetricCloudCacheRoundTrip";
    const auto first = testRoot / L"first";
    const auto second = testRoot / L"second";
    std::error_code ec;
    std::filesystem::remove_all(testRoot, ec);
    if (!SaveBundle(first, device, context, params, shaderBlobs, volumes, weather)) return false;
    std::vector<unsigned char> originalPointer;
    if (!ReadFile(first / L"active-bundle.txt", originalPointer) || originalPointer.empty())
        return false;

    CloudParameters loadedParams;
    ShaderBlobArray loadedBlobs;
    std::array<ComPtr<ID3D11Texture3D>, 2> loadedVolumes;
    std::array<ComPtr<ID3D11UnorderedAccessView>, 2> loadedUavs;
    std::array<ComPtr<ID3D11ShaderResourceView>, 2> loadedSrvs;
    WeatherMapResources loadedWeather;
    bool modified = false;
    if (!LoadBundle(first, device, loadedParams, loadedBlobs, loadedVolumes, loadedUavs, loadedSrvs,
                    loadedWeather, modified))
        return false;
    if (!SaveBundle(second, device, context, loadedParams, loadedBlobs, loadedVolumes, loadedWeather)) return false;

    const wchar_t* files[] = { L"manifest.bin", L"base.vcnoise", L"detail.vcnoise", L"weather.vcnoise",
        L"main_vs.cso", L"main_ps.cso", L"preview_vs.cso", L"preview_ps.cso",
        L"noise_cs_base.cso", L"noise_cs_detail.cso", L"noise_cs_weather.cso" };
    const auto firstBundle = ResolveBundleDirectory(first);
    const auto secondBundle = ResolveBundleDirectory(second);
    for (const wchar_t* name : files)
    {
        std::vector<unsigned char> a, b;
        if (!ReadFile(firstBundle / name, a) || !ReadFile(secondBundle / name, b) || a != b)
            return false;
    }

    // manifest의 호환성 필드가 깨지면 bundle 전체를 거부해야 한다.
    const auto manifestPath = firstBundle / L"manifest.bin";
    std::vector<unsigned char> originalManifest;
    if (!ReadFile(manifestPath, originalManifest) || originalManifest.size() != sizeof(CacheManifest))
        return false;
    const auto rejectsManifest = [&](const CacheManifest& bad)
    {
        if (!WriteFile(manifestPath, &bad, sizeof(bad))) return false;
        CloudParameters rejectedParams;
        ShaderBlobArray rejectedBlobs;
        std::array<ComPtr<ID3D11Texture3D>, 2> rejectedVolumes;
        std::array<ComPtr<ID3D11UnorderedAccessView>, 2> rejectedUavs;
        std::array<ComPtr<ID3D11ShaderResourceView>, 2> rejectedSrvs;
        WeatherMapResources rejectedWeather;
        bool rejectedModified = false;
        return !LoadBundle(first, device, rejectedParams, rejectedBlobs,
                           rejectedVolumes, rejectedUavs, rejectedSrvs, rejectedWeather, rejectedModified);
    };
    CacheManifest bad = {};
    std::memcpy(&bad, originalManifest.data(), sizeof(bad));
    bad.version = 5;
    if (!rejectsManifest(bad)) return false;
    std::memcpy(&bad, originalManifest.data(), sizeof(bad));
    ++bad.parameterHash;
    if (!rejectsManifest(bad)) return false;
    std::memcpy(&bad, originalManifest.data(), sizeof(bad));
    bad.baseFormat = static_cast<uint32_t>(DXGI_FORMAT_UNKNOWN);
    if (!rejectsManifest(bad)) return false;
    if (!WriteFile(manifestPath, originalManifest.data(), originalManifest.size())) return false;

    // 저장 입력이 불완전하면 live bundle을 건드리지 않는다.
    ShaderBlobArray incompleteBlobs = shaderBlobs;
    incompleteBlobs[0].Reset();
    if (SaveBundle(first, device, context, params, incompleteBlobs, volumes, weather)) return false;
    std::vector<unsigned char> manifestAfterFailure;
    if (!ReadFile(manifestPath, manifestAfterFailure) || manifestAfterFailure != originalManifest)
        return false;
    std::vector<unsigned char> pointerAfterFailure;
    if (!ReadFile(first / L"active-bundle.txt", pointerAfterFailure) ||
        pointerAfterFailure != originalPointer)
        return false;
    std::filesystem::remove_all(testRoot, ec);
    return true;
}

bool NoiseCacheManager::SaveBundle(
    const std::filesystem::path& root, ID3D11Device* device, ID3D11DeviceContext* context,
    const CloudParameters& params, const ShaderBlobArray& shaderBlobs,
    const std::array<ComPtr<ID3D11Texture3D>, 2>& volumes,
    const WeatherMapResources& weather)
{
    m_lastError.clear();
    const auto fail = [&](const char* stage, const std::string& detail = {})
    {
        m_lastError = stage;
        if (!detail.empty()) m_lastError += ": " + detail;
        return false;
    };
    if (!device || !context) return fail("Validate", "missing D3D11 device or context");
    for (const auto& blob : shaderBlobs)
        if (!blob) return fail("Validate", "missing compiled shader blob");

    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    static std::atomic_uint64_t generationCounter = 0;
    std::wostringstream generationStream;
    generationStream << L"bundle-v" << kCacheVersion << L"-" << stamp << L"-"
                     << GetCurrentProcessId() << L"-" << generationCounter++;
    const std::wstring generationName = generationStream.str();
    const auto temp = root / (generationName + L".tmp");
    const auto generation = root / generationName;
    const auto pointerTemp = root / L"active-bundle.tmp";
    const auto pointerLive = root / L"active-bundle.txt";
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    std::filesystem::remove_all(temp, ec);
    std::filesystem::create_directories(temp, ec);
    if (ec) return fail("Prepare", ec.message());

    for (size_t i = 0; i < shaderBlobs.size(); ++i)
        if (!WriteFile(temp / kShaderNames[i], shaderBlobs[i]->GetBufferPointer(), shaderBlobs[i]->GetBufferSize()))
            return fail("Writing shaders", kShaderNames[i]);
    if (!SaveVolume(temp / L"base.vcnoise", device, context, volumes[0].Get()))
        return fail("GPU readback", "base volume");
    if (!SaveVolume(temp / L"detail.vcnoise", device, context, volumes[1].Get()))
        return fail("GPU readback", "detail volume");
    if (!SaveWeatherMap(temp / L"weather.vcnoise", device, context, weather.texture.Get()))
        return fail("GPU readback", "weather map");

    CacheManifest manifest = {};
    manifest.magic = kManifestMagic;
    manifest.version = kCacheVersion;
    manifest.sourceHash = SourceHash();
    manifest.parameterHash = ParameterHash(params);
    manifest.baseSize = 128;
    manifest.detailSize = 128;
    manifest.weatherSize = 512;
    manifest.baseFormat = static_cast<uint32_t>(kBaseVolumeFormat);
    manifest.detailFormat = static_cast<uint32_t>(kDetailVolumeFormat);
    manifest.weatherFormat = static_cast<uint32_t>(kWeatherFormat);
    manifest.params = params;
    if (!WriteFile(temp / L"manifest.bin", &manifest, sizeof(manifest)))
        return fail("Writing manifest");

    // 활성 bundle을 건드리기 전에 방금 기록한 모든 파일을 실제 D3D 리소스로 다시 읽는다.
    CloudParameters verifiedParams;
    ShaderBlobArray verifiedBlobs;
    std::array<ComPtr<ID3D11Texture3D>, 2> verifiedVolumes;
    std::array<ComPtr<ID3D11UnorderedAccessView>, 2> verifiedUavs;
    std::array<ComPtr<ID3D11ShaderResourceView>, 2> verifiedSrvs;
    WeatherMapResources verifiedWeather;
    bool verifiedModified = false;
    if (!LoadBundleDirectory(temp, device, verifiedParams, verifiedBlobs, verifiedVolumes,
                             verifiedUavs, verifiedSrvs, verifiedWeather, verifiedModified))
        return fail("Verifying", "saved bundle reload failed");

    std::string moveError;
    if (!MoveWithRetry(temp, generation, MOVEFILE_WRITE_THROUGH, moveError))
        return fail("Publishing generation", moveError);

    std::string pointerValue;
    pointerValue.reserve(generationName.size());
    for (wchar_t c : generationName) pointerValue.push_back(static_cast<char>(c));
    if (!WriteFile(pointerTemp, pointerValue.data(), pointerValue.size()))
        return fail("Activating", "could not write active pointer");
    if (!MoveWithRetry(pointerTemp, pointerLive,
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH, moveError))
        return fail("Activating", moveError);

    m_lastError.clear();
    return true;
}
