// ============================================================================
//  NoiseVolumeCache.h - 단계 13-4 테스트용 Texture3D payload 왕복 형식
// ============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace noisevolumecache
{
enum class Kind : std::uint32_t { Base = 1, Detail = 2 };
constexpr std::uint32_t kMagic = 0x334e4356u; // VCN3
constexpr std::uint32_t kVersion = 3u;
constexpr std::uint32_t kRgba8Unorm = 28u; // DXGI_FORMAT_R8G8B8A8_UNORM

#pragma pack(push, 1)
struct Header
{
    std::uint32_t magic = kMagic;
    std::uint32_t version = kVersion;
    std::uint32_t kind = 0;
    std::uint32_t resolution = 0;
    std::uint32_t format = kRgba8Unorm;
    std::uint32_t reserved = 0;
    std::uint64_t parameterHash = 0;
    std::uint64_t payloadBytes = 0;
    std::uint64_t payloadHash = 0;
};
#pragma pack(pop)
static_assert(sizeof(Header) == 48, "Noise volume cache header is stable");

inline std::uint64_t HashBytes(const void* data, std::size_t size)
{
    std::uint64_t hash = 1469598103934665603ull;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t index = 0; index < size; ++index)
    {
        hash ^= bytes[index];
        hash *= 1099511628211ull;
    }
    return hash;
}

inline bool Save(const std::filesystem::path& path, Kind kind,
                 std::uint32_t resolution, std::uint64_t parameterHash,
                 const std::vector<std::uint8_t>& payload)
{
    if (resolution == 0 || payload.size() !=
            static_cast<std::size_t>(resolution) * resolution * resolution * 4u)
        return false;
    Header header;
    header.kind = static_cast<std::uint32_t>(kind);
    header.resolution = resolution;
    header.parameterHash = parameterHash;
    header.payloadBytes = payload.size();
    header.payloadHash = HashBytes(payload.data(), payload.size());
    const std::filesystem::path temporary = path.wstring() + L".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output ||
            !output.write(reinterpret_cast<const char*>(&header), sizeof(header)) ||
            !output.write(reinterpret_cast<const char*>(payload.data()),
                          static_cast<std::streamsize>(payload.size())))
            return false;
    }
    std::error_code error;
    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(temporary, path, error);
    return !error;
}

inline bool Load(const std::filesystem::path& path, Kind expectedKind,
                 std::uint32_t expectedResolution,
                 std::uint64_t expectedParameterHash,
                 std::vector<std::uint8_t>& payload)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        return false;
    const std::streamsize fileSize = input.tellg();
    input.seekg(0);
    Header header;
    if (!input.read(reinterpret_cast<char*>(&header), sizeof(header)) ||
        header.magic != kMagic || header.version != kVersion ||
        header.kind != static_cast<std::uint32_t>(expectedKind) ||
        header.resolution != expectedResolution ||
        header.format != kRgba8Unorm ||
        header.parameterHash != expectedParameterHash ||
        header.payloadBytes != static_cast<std::uint64_t>(expectedResolution) *
            expectedResolution * expectedResolution * 4u ||
        fileSize != static_cast<std::streamsize>(sizeof(Header) +
                                                 header.payloadBytes))
        return false;
    std::vector<std::uint8_t> loaded(static_cast<std::size_t>(header.payloadBytes));
    if (!input.read(reinterpret_cast<char*>(loaded.data()),
                    static_cast<std::streamsize>(loaded.size())) ||
        HashBytes(loaded.data(), loaded.size()) != header.payloadHash)
        return false;
    payload = std::move(loaded);
    return true;
}
}
