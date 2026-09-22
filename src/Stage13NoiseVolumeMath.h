// ============================================================================
//  Stage13NoiseVolumeMath.h - 단계 13-4 Texture3D 규격과 CPU 기준 수학
// ============================================================================
#pragma once

#include <DirectXMath.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <vector>

namespace stage13noise
{
inline constexpr float kBaseHorizontalWorldSizeMeters = 12000.0f;
inline constexpr float kBaseVerticalWorldSizeMeters = 12000.0f;
inline constexpr std::uint32_t kBasePerlinOctaveSeedStride = 173u;
}

struct alignas(16) NoiseVolumeParameters
{
    // [고정 품질] Base 한 축 128 texel(128³ RGBA8). 텍스처 할당·검증 계약과 함께 고정.
    std::uint32_t baseResolution = 128;
    // [고정 품질] Detail 한 축 64 texel(64³ RGBA8). world size와 달리 내용 생성 규격.
    std::uint32_t detailResolution = 64;
    // [고정 품질] 3D Noise seed uint [0,4294967295], 기본/권장 1337. 같은 seed는 같은 무늬; 변경은 재생성이 필요하며 크기/밀도와 무관.
    std::uint32_t seed = 1337;
    // [패딩] 16바이트 packing 예약 칸, 0 유지. 화면 효과 없음; 삭제/재배치 금지.
    std::uint32_t paddingUint0 = 0;

    // [직접 조절] F2/Formation Base XZ m/반복. Formation [1,200000], 기본/권장 12000. 늘리면 덩어리가 넓어지며 재생성 불필요.
    float baseWorldSizeMeters = stage13noise::kBaseHorizontalWorldSizeMeters;
    // [직접 조절] F2/Formation Detail XYZ m/반복. Formation [1,100000], 기본/권장 2000. 늘리면 표면 파임이 커진다.
    float detailWorldSizeMeters = 2000.0f;
    // [직접 조절] F2/Formation Base Y m/반복. Formation [1,200000], 기본/권장 12000. 늘리면 세로 무늬가 늘어진다.
    float baseVerticalWorldSizeMeters = stage13noise::kBaseVerticalWorldSizeMeters;
    // [03 임시 실험] F2 선택; 생성에만 사용. Custom 저장 대상 아님.
    float baseMidOctaveExtra = 0.5f; // 03 승인: 2/3번째 진폭 1.50배, offset28.

    // [고정 품질] x/y/z/w={4,9,17,23} cycle/타일. R fBm의 4옥타브, G/B/A Worley는 xyz 사용. 생성 경로 최소 1, 권장 현행 유지.
    DirectX::XMUINT4 baseFrequencies = { 4, 9, 17, 23 };
    // [고정 품질] RGBA Worley fBm 기저 {2,3,4,5} cycle/타일. 각 채널 f/2f/4f, .625/.25/.125.
    // 2026-09-22 무보정 fBm 채택. 최대 명목20cycle/타일; 화면 High 안정성은 별도 검증.
    DirectX::XMUINT4 detailFrequencies = { 2, 3, 4, 5 };
    // [직접 조절: 코드] x/y/z=Base G/B/A Worley 가중치 (0.625,0.25,0.125), w=0 미사용. 별도 CPU clamp/정규화 없음; 권장 비음수 합 1 유지.
    DirectX::XMFLOAT4 baseWeights = { 0.625f, 0.25f, 0.125f, 0.0f };
    // [직접 조절: 코드] x/y/z/w=Detail R/G/B/A 가중치 (0.50,0.30,0.15,0.05). CPU clamp 없음, 결과 saturate. 권장 비음수 합 1; 큰 주파수 비중↑면 파임이 잘게 된다.
    DirectX::XMFLOAT4 detailWeights = { 0.50f, 0.30f, 0.15f, 0.05f };
};

static_assert(sizeof(NoiseVolumeParameters) == 96,
              "NoiseVolumeParameters must match NoiseVolumeCB");
static_assert(offsetof(NoiseVolumeParameters, baseMidOctaveExtra) == 28);
inline float SanitizeBaseMidOctaveExtra(float value)
{
    if (!std::isfinite(value)) return 0.0f;
    return value >= 0.375f ? 0.5f : (value >= 0.125f ? 0.25f : 0.0f);
}

namespace stage13noise
{
constexpr std::uint32_t kBaseResolution = 128;
constexpr std::uint32_t kDetailResolution = 64;
constexpr std::uint32_t kBytesPerTexel = 4;
constexpr std::uint64_t kBaseBytes =
    static_cast<std::uint64_t>(kBaseResolution) * kBaseResolution *
    kBaseResolution * kBytesPerTexel;
constexpr std::uint64_t kDetailBytes =
    static_cast<std::uint64_t>(kDetailResolution) * kDetailResolution *
    kDetailResolution * kBytesPerTexel;

struct Float3
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

inline double Frac(double value)
{
    return value - std::floor(value);
}

inline std::int32_t WrapCell(std::int32_t value, std::int32_t period)
{
    const std::int32_t safe = std::max(period, 1);
    const std::int32_t remainder = value % safe;
    return remainder < 0 ? remainder + safe : remainder;
}

inline std::uint32_t Hash(std::int32_t x, std::int32_t y, std::int32_t z,
                          std::uint32_t seed)
{
    std::uint32_t value = seed ^ 0x9e3779b9u;
    const std::uint32_t coordinates[3] = {
        static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
        static_cast<std::uint32_t>(z)
    };
    for (std::uint32_t coordinate : coordinates)
    {
        value ^= coordinate + 0x9e3779b9u + (value << 6u) + (value >> 2u);
        value ^= value >> 16u;
        value *= 0x7feb352du;
        value ^= value >> 15u;
        value *= 0x846ca68bu;
        value ^= value >> 16u;
    }
    return value;
}

inline Float3 Gradient(std::uint32_t hash)
{
    static constexpr Float3 gradients[] = {
        { 1, 1, 0 }, { -1, 1, 0 }, { 1, -1, 0 }, { -1, -1, 0 },
        { 1, 0, 1 }, { -1, 0, 1 }, { 1, 0, -1 }, { -1, 0, -1 },
        { 0, 1, 1 }, { 0, -1, 1 }, { 0, 1, -1 }
    };
    const std::uint32_t selector = hash & 15u;
    const Float3 value = selector < 11u ? gradients[selector] :
        Float3{ 0, -1, -1 };
    constexpr double inverseSqrtTwo = 0.7071067811865475244;
    return { value.x * inverseSqrtTwo, value.y * inverseSqrtTwo,
             value.z * inverseSqrtTwo };
}

inline double Fade(double value)
{
    return value * value * value * (value * (value * 6.0 - 15.0) + 10.0);
}

inline double Lerp(double a, double b, double t)
{
    return a + (b - a) * t;
}

inline double PeriodicGradientNoise(Float3 point, std::int32_t period,
                                    std::uint32_t seed)
{
    const std::int32_t x0 = static_cast<std::int32_t>(std::floor(point.x));
    const std::int32_t y0 = static_cast<std::int32_t>(std::floor(point.y));
    const std::int32_t z0 = static_cast<std::int32_t>(std::floor(point.z));
    const double fx = Frac(point.x);
    const double fy = Frac(point.y);
    const double fz = Frac(point.z);
    const auto corner = [&](std::int32_t dx, std::int32_t dy, std::int32_t dz)
    {
        const Float3 gradient = Gradient(Hash(
            WrapCell(x0 + dx, period), WrapCell(y0 + dy, period),
            WrapCell(z0 + dz, period), seed));
        return gradient.x * (fx - dx) + gradient.y * (fy - dy) +
               gradient.z * (fz - dz);
    };
    const double u = Fade(fx), v = Fade(fy), w = Fade(fz);
    const double x00 = Lerp(corner(0, 0, 0), corner(1, 0, 0), u);
    const double x10 = Lerp(corner(0, 1, 0), corner(1, 1, 0), u);
    const double x01 = Lerp(corner(0, 0, 1), corner(1, 0, 1), u);
    const double x11 = Lerp(corner(0, 1, 1), corner(1, 1, 1), u);
    return Lerp(Lerp(x00, x10, v), Lerp(x01, x11, v), w);
}

inline double PeriodicWorleyDistance(Float3 point, std::int32_t period,
                                     std::uint32_t seed)
{
    const std::int32_t cellX = static_cast<std::int32_t>(std::floor(point.x));
    const std::int32_t cellY = static_cast<std::int32_t>(std::floor(point.y));
    const std::int32_t cellZ = static_cast<std::int32_t>(std::floor(point.z));
    const Float3 local = { Frac(point.x), Frac(point.y), Frac(point.z) };
    double nearestSquared = 4.0;
    for (std::int32_t z = -1; z <= 1; ++z)
        for (std::int32_t y = -1; y <= 1; ++y)
            for (std::int32_t x = -1; x <= 1; ++x)
            {
                const std::int32_t wx = WrapCell(cellX + x, period);
                const std::int32_t wy = WrapCell(cellY + y, period);
                const std::int32_t wz = WrapCell(cellZ + z, period);
                const auto unit = [&](std::uint32_t salt)
                {
                    return static_cast<double>(Hash(wx, wy, wz, seed + salt) &
                                               0x00ffffffu) /
                           static_cast<double>(0x01000000u);
                };
                const double dx = x + unit(17u) - local.x;
                const double dy = y + unit(59u) - local.y;
                const double dz = z + unit(101u) - local.z;
                nearestSquared = std::min(nearestSquared,
                    dx * dx + dy * dy + dz * dz);
            }
    return std::clamp(std::sqrt(nearestSquared) / 1.15, 0.0, 1.0);
}

inline double BasePerlinWorley(Float3 uvw,
                               const NoiseVolumeParameters& parameters = {})
{
    double sum = 0.0, normalization = 0.0, amplitude = 0.5;
    const std::uint32_t frequencies[4] = {
        parameters.baseFrequencies.x, parameters.baseFrequencies.y,
        parameters.baseFrequencies.z, parameters.baseFrequencies.w
    };
    for (std::uint32_t octave = 0; octave < 4u; ++octave)
    {
        const std::uint32_t frequency = frequencies[octave];
        const Float3 point = { uvw.x * frequency, uvw.y * frequency,
                               uvw.z * frequency };
        const double weight = amplitude * ((octave == 1u || octave == 2u)
            ? 1.0 + SanitizeBaseMidOctaveExtra(parameters.baseMidOctaveExtra) : 1.0);
        sum += PeriodicGradientNoise(point, static_cast<std::int32_t>(frequency),
                                     parameters.seed + octave *
                                         kBasePerlinOctaveSeedStride) * weight;
        normalization += weight;
        amplitude *= 0.5;
    }
    const double perlin = std::clamp(sum / std::max(normalization, 1e-6) *
                                     0.5 + 0.5, 0.0, 1.0);
    const std::uint32_t frequency = parameters.baseFrequencies.x;
    const Float3 point = { uvw.x * frequency, uvw.y * frequency,
                           uvw.z * frequency };
    const double cellularMass = 1.0 - PeriodicWorleyDistance(
        point, static_cast<std::int32_t>(frequency), parameters.seed + 211u);
    return std::clamp(Lerp(perlin, perlin * cellularMass + perlin * 0.35,
                           0.42), 0.0, 1.0);
}

inline double SamplesPerWavelength(double worldSizeMeters,
                                   std::uint32_t frequency,
                                   double stepMeters)
{
    return worldSizeMeters /
           (std::max<std::uint32_t>(frequency, 1u) * std::max(stepMeters, 1e-9));
}

inline bool IsNyquistSafe(std::uint32_t resolution, std::uint32_t frequency)
{
    return frequency <= resolution / 2u;
}

inline double WeightedDetailMean(
    const std::vector<std::uint8_t>& rgba,
    const std::array<double, 4>& weights)
{
    if (rgba.empty() || rgba.size() % 4u != 0u)
        return 0.5;
    std::array<double, 4> safeWeights = {};
    for (std::size_t channel = 0; channel < safeWeights.size(); ++channel)
    {
        safeWeights[channel] = std::isfinite(weights[channel])
            ? std::max(weights[channel], 0.0) : 0.0;
    }
    double total = 0.0;
    for (std::size_t texel = 0; texel < rgba.size(); texel += 4u)
    {
        double value = 0.0;
        for (std::size_t channel = 0; channel < 4u; ++channel)
        {
            value += (static_cast<double>(rgba[texel + channel]) / 255.0) *
                safeWeights[channel];
        }
        total += std::clamp(value, 0.0, 1.0);
    }
    return std::clamp(
        total / static_cast<double>(rgba.size() / 4u), 0.0, 1.0);
}
}
