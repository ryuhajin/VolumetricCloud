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
    PreviewVS,
    PreviewPS,
    NoiseCSBase,   // R8 단일값 굽기 컴퓨트
    NoiseCSDetail, // RGBA8 옥타브 굽기 컴퓨트
    Count,
};

using ShaderBlobArray = std::array<Microsoft::WRL::ComPtr<ID3DBlob>,
                                   static_cast<size_t>(CachedShader::Count)>;

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
        bool& sourceModified);

    bool SaveUser(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        const CloudParameters& params,
        const ShaderBlobArray& shaderBlobs,
        const std::array<Microsoft::WRL::ComPtr<ID3D11Texture3D>, 2>& volumes);

    bool SaveDefault(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        const CloudParameters& params,
        const ShaderBlobArray& shaderBlobs,
        const std::array<Microsoft::WRL::ComPtr<ID3D11Texture3D>, 2>& volumes);

    bool RunRoundTripTest(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        const CloudParameters& params,
        const ShaderBlobArray& shaderBlobs,
        const std::array<Microsoft::WRL::ComPtr<ID3D11Texture3D>, 2>& volumes);

    uint64_t SourceHash() const;
    static uint64_t ParameterHash(const CloudParameters& params);
    const std::filesystem::path& UserCacheRoot() const { return m_userCacheRoot; }

private:
    bool LoadBundle(
        const std::filesystem::path& root,
        ID3D11Device* device,
        CloudParameters& params,
        ShaderBlobArray& shaderBlobs,
        std::array<Microsoft::WRL::ComPtr<ID3D11Texture3D>, 2>& volumes,
        std::array<Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>, 2>& uavs,
        std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>, 2>& srvs,
        bool& sourceModified);
    bool SaveBundle(
        const std::filesystem::path& root,
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        const CloudParameters& params,
        const ShaderBlobArray& shaderBlobs,
        const std::array<Microsoft::WRL::ComPtr<ID3D11Texture3D>, 2>& volumes);

    std::filesystem::path m_defaultCacheRoot;
    std::filesystem::path m_userCacheRoot;
    std::vector<std::wstring> m_sourcePaths;
};
