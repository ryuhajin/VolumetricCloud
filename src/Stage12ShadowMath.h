// ============================================================================
//  Stage12ShadowMath.h - 단계 12 light-space/cache CPU 회귀 기준
// ============================================================================
#pragma once

#include "Stage12ShadowParameters.h"

#include <algorithm>
#include <cmath>

namespace stage12shadow
{
inline float Dot(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline DirectX::XMFLOAT3 Add(const DirectX::XMFLOAT3& a,
                             const DirectX::XMFLOAT3& b)
{
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}

inline DirectX::XMFLOAT3 Scale(const DirectX::XMFLOAT3& value, float scale)
{
    return { value.x * scale, value.y * scale, value.z * scale };
}

inline DirectX::XMFLOAT3 Cross(const DirectX::XMFLOAT3& a,
                               const DirectX::XMFLOAT3& b)
{
    return { a.y * b.z - a.z * b.y,
             a.z * b.x - a.x * b.z,
             a.x * b.y - a.y * b.x };
}

inline float Length(const DirectX::XMFLOAT3& value)
{
    return std::sqrt(std::max(Dot(value, value), 0.0f));
}

inline DirectX::XMFLOAT3 NormalizeOr(const DirectX::XMFLOAT3& value,
                                     const DirectX::XMFLOAT3& fallback)
{
    const float length = Length(value);
    return std::isfinite(length) && length > 1.0e-6f
        ? Scale(value, 1.0f / length) : fallback;
}

struct LightBasis
{
    DirectX::XMFLOAT3 right{};
    DirectX::XMFLOAT3 up{};
    DirectX::XMFLOAT3 forward{};
    bool valid = false;
};

inline LightBasis BuildLightBasis(const DirectX::XMFLOAT3& directionToSun)
{
    LightBasis result{};
    result.forward = NormalizeOr(directionToSun, { 0.0f, 1.0f, 0.0f });
    const DirectX::XMFLOAT3 helper = std::abs(result.forward.y) < 0.999f
        ? DirectX::XMFLOAT3{ 0.0f, 1.0f, 0.0f }
        : DirectX::XMFLOAT3{ 0.0f, 0.0f, 1.0f };
    result.right = NormalizeOr(Cross(helper, result.forward),
                               { 1.0f, 0.0f, 0.0f });
    result.up = NormalizeOr(Cross(result.forward, result.right),
                            { 0.0f, 0.0f, 1.0f });
    result.valid = std::isfinite(directionToSun.x) &&
        std::isfinite(directionToSun.y) && std::isfinite(directionToSun.z) &&
        Length(directionToSun) > 1.0e-6f;
    return result;
}

inline DirectX::XMFLOAT2 CacheUv(const DirectX::XMFLOAT3& position,
                                 const DirectX::XMFLOAT3& center,
                                 const LightBasis& basis, float widthMeters)
{
    const DirectX::XMFLOAT3 delta = Add(position, Scale(center, -1.0f));
    const float invWidth = widthMeters > 1.0e-6f ? 1.0f / widthMeters : 0.0f;
    return { Dot(delta, basis.right) * invWidth + 0.5f,
             Dot(delta, basis.up) * invWidth + 0.5f };
}

inline DirectX::XMFLOAT3 SnappedCenter(
    const DirectX::XMFLOAT3& rawCenter, const LightBasis& basis,
    float widthMeters, std::uint32_t resolution)
{
    const float texel = widthMeters /
        static_cast<float>(std::max(resolution, 1u));
    const float u = std::round(Dot(rawCenter, basis.right) / texel) * texel;
    const float v = std::round(Dot(rawCenter, basis.up) / texel) * texel;
    const float forward = Dot(rawCenter, basis.forward);
    return Add(Add(Scale(basis.right, u), Scale(basis.up, v)),
               Scale(basis.forward, forward));
}

inline float Smoothstep(float edge0, float edge1, float value)
{
    const float t = std::clamp((value - edge0) /
        std::max(edge1 - edge0, 1.0e-6f), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline float CascadeEdge(const DirectX::XMFLOAT2& uv)
{
    return std::max(std::abs(uv.x - 0.5f), std::abs(uv.y - 0.5f)) * 2.0f;
}

inline float NearWeight(float edge)
{
    return 1.0f - Smoothstep(0.80f, 0.95f, edge);
}

inline float FarValidity(float edge)
{
    return 1.0f - Smoothstep(0.90f, 1.00f, edge);
}

struct SliceInterpolation
{
    std::uint32_t lower = 0;
    std::uint32_t upper = 0;
    float fraction = 0.0f;
};

inline SliceInterpolation HeightToSlices(float heightMeters,
                                         float bottomMeters,
                                         float topMeters,
                                         std::uint32_t sliceCount)
{
    const float height = std::clamp(
        (heightMeters - bottomMeters) /
            std::max(topMeters - bottomMeters, 1.0e-6f), 0.0f, 1.0f);
    const float coordinate = height *
        static_cast<float>(std::max(sliceCount, 2u) - 1u);
    SliceInterpolation result{};
    result.lower = static_cast<std::uint32_t>(std::floor(coordinate));
    result.upper = std::min(result.lower + 1u,
                            std::max(sliceCount, 2u) - 1u);
    result.fraction = coordinate - static_cast<float>(result.lower);
    return result;
}

inline float Transmittance(float opticalDepth)
{
    return std::exp(-std::clamp(opticalDepth, 0.0f, 9.21034037f));
}

inline float SurfaceFactor(float cloudTransmittance, float ambientFloor,
                           float strength)
{
    const float shadowed = std::clamp(ambientFloor, 0.0f, 1.0f) +
        (1.0f - std::clamp(ambientFloor, 0.0f, 1.0f)) *
        std::clamp(cloudTransmittance, 0.0f, 1.0f);
    return 1.0f + (shadowed - 1.0f) * std::clamp(strength, 0.0f, 1.0f);
}

inline float SunPathLength(float verticalThicknessMeters,
                           float elevationDegrees)
{
    const float radians = elevationDegrees * 0.01745329251994329577f;
    return verticalThicknessMeters /
        std::max(std::sin(radians), 1.0e-6f);
}
}
