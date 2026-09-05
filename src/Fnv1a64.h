// ============================================================================
//  Fnv1a64.h - 프로젝트 공통 표준 FNV-1a 64-bit 해시
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

namespace fnv1a64
{
constexpr std::uint64_t kOffsetBasis = 14695981039346656037ull;
constexpr std::uint64_t kPrime = 1099511628211ull;

inline void Append(std::uint64_t& hash, const void* data, std::size_t size)
{
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t index = 0; index < size; ++index)
    {
        hash ^= bytes[index];
        hash *= kPrime;
    }
}

inline std::uint64_t Hash(const void* data, std::size_t size)
{
    std::uint64_t hash = kOffsetBasis;
    Append(hash, data, size);
    return hash;
}
}
