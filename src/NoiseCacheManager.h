// ============================================================================
//  NoiseCacheManager.h — 셰이더 bytecode와 3D 노이즈 볼륨 영구 캐시
// ============================================================================
#pragma once

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "CloudParameters.h"

enum class CachedShader : size_t
{
    MainVS = 0,
    MainPS,
    TemporalPS,
    CompositePS,
    PreviewVS,
    PreviewPS,
    NoiseCSBase,   // RGBA8 base 채널 굽기 컴퓨트
    NoiseCSDetail, // RGBA8 옥타브 굽기 컴퓨트
    NoiseCSWeather,// RGBA8 광역 weather map 굽기 컴퓨트
    Count,
};

using ShaderBlobArray = std::array<Microsoft::WRL::ComPtr<ID3DBlob>,
                                   static_cast<size_t>(CachedShader::Count)>;

struct WeatherMapResources
{
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
};

using PlacementMapResources = WeatherMapResources;

class NoiseCacheManager
{
public:
    void Init(const std::filesystem::path& defaultCacheRoot,
              const std::vector<std::wstring>& sourcePaths);

    bool LoadPreferred(
        ID3D11Device* device,
        CloudParameters& params,
        ShaderBlobArray& shaderBlobs,
        std::array<Microsoft::WRL::ComPtr<ID3D11Texture3D>, 2>& volumes,
        std::array<Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>, 2>& uavs,
        std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>, 2>& srvs,
        WeatherMapResources& weather,
        PlacementMapResources& placement,
        bool& sourceModified);

    bool SaveUser(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        const CloudParameters& params,
        const ShaderBlobArray& shaderBlobs,
        const std::array<Microsoft::WRL::ComPtr<ID3D11Texture3D>, 2>& volumes,
        const WeatherMapResources& weather,
        const PlacementMapResources& placement);

    bool SaveDefault(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        const CloudParameters& params,
        const ShaderBlobArray& shaderBlobs,
        const std::array<Microsoft::WRL::ComPtr<ID3D11Texture3D>, 2>& volumes,
        const WeatherMapResources& weather,
        const PlacementMapResources& placement);

    bool RunRoundTripTest(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        const CloudParameters& params,
        const ShaderBlobArray& shaderBlobs,
        const std::array<Microsoft::WRL::ComPtr<ID3D11Texture3D>, 2>& volumes,
        const WeatherMapResources& weather,
        const PlacementMapResources& placement);

    uint64_t SourceHash() const;
    static uint64_t ParameterHash(const CloudParameters& params);
    const std::filesystem::path& UserCacheRoot() const { return m_userCacheRoot; }
    const std::string& LastError() const { return m_lastError; }

private:
    bool LoadBundle(
        const std::filesystem::path& root,
        ID3D11Device* device,
        CloudParameters& params,
        ShaderBlobArray& shaderBlobs,
        std::array<Microsoft::WRL::ComPtr<ID3D11Texture3D>, 2>& volumes,
        std::array<Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>, 2>& uavs,
        std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>, 2>& srvs,
        WeatherMapResources& weather,
        PlacementMapResources& placement,
        bool& sourceModified);
    bool LoadBundleDirectory(
        const std::filesystem::path& bundle,
        ID3D11Device* device,
        CloudParameters& params,
        ShaderBlobArray& shaderBlobs,
        std::array<Microsoft::WRL::ComPtr<ID3D11Texture3D>, 2>& volumes,
        std::array<Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>, 2>& uavs,
        std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>, 2>& srvs,
        WeatherMapResources& weather,
        PlacementMapResources& placement,
        bool& sourceModified);
    std::filesystem::path ResolveBundleDirectory(const std::filesystem::path& root) const;
    bool SaveBundle(
        const std::filesystem::path& root,
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        const CloudParameters& params,
        const ShaderBlobArray& shaderBlobs,
        const std::array<Microsoft::WRL::ComPtr<ID3D11Texture3D>, 2>& volumes,
        const WeatherMapResources& weather,
        const PlacementMapResources& placement);

    std::filesystem::path m_defaultCacheRoot;
    std::filesystem::path m_userCacheRoot;
    std::vector<std::wstring> m_sourcePaths;
    std::string m_lastError;
};
