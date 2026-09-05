#include "WeatherMap.h"
#include "Fnv1a64.h"

#include <algorithm>
#include <cmath>

namespace
{
float Saturate(float value)
{
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}

float FiniteOr(float value, float fallback)
{
    return std::isfinite(value) ? value : fallback;
}

float Smoothstep(float edge0, float edge1, float value)
{
    const float divisor = std::max(edge1 - edge0, 1e-6f);
    const float t = Saturate((value - edge0) / divisor);
    return t * t * (3.0f - 2.0f * t);
}

float Fade(float value)
{
    const float t = Saturate(value);
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

std::int32_t WrapLattice(std::int32_t value, std::int32_t period)
{
    const std::int32_t remainder = value % period;
    return remainder < 0 ? remainder + period : remainder;
}

std::uint32_t HashLattice(std::int32_t x, std::int32_t y,
                          std::uint32_t seed)
{
    std::uint32_t hash = seed ^ 0x9e3779b9u;
    hash ^= static_cast<std::uint32_t>(x) * 0x85ebca6bu;
    hash = (hash << 13u) | (hash >> 19u);
    hash ^= static_cast<std::uint32_t>(y) * 0xc2b2ae35u;
    hash ^= hash >> 16u;
    hash *= 0x7feb352du;
    hash ^= hash >> 15u;
    hash *= 0x846ca68bu;
    return hash ^ (hash >> 16u);
}

float GradientDot(std::uint32_t hash, float x, float y)
{
    constexpr float kDiagonal = 0.7071067811865475244f;
    switch (hash & 7u)
    {
    case 0u: return x;
    case 1u: return -x;
    case 2u: return y;
    case 3u: return -y;
    case 4u: return (x + y) * kDiagonal;
    case 5u: return (x - y) * kDiagonal;
    case 6u: return (-x + y) * kDiagonal;
    default: return (-x - y) * kDiagonal;
    }
}

float AdjustField(float field, const PeriodicChannelSettings& settings)
{
    return Saturate((field - 0.5f) * settings.contrast + 0.5f + settings.bias);
}

float SampleTwoScale(float u, float v,
                     const PeriodicChannelSettings& settings)
{
    const float macro = SamplePeriodicPerlin2D(
        u, v, settings.macroPeriod, settings.seed);
    const float detail = SamplePeriodicPerlin2D(
        u, v, settings.detailPeriod, settings.seed ^ 0x9e3779b9u);
    return macro + (detail - macro) * settings.detailWeight;
}

// F4 채널 검증용 원형 섬도 UV 0/1에서 이어지도록 토러스 거리를 사용한다.
float TiledDistance(float u, float v, float centerU, float centerV)
{
    float du = std::abs(u - centerU);
    float dv = std::abs(v - centerV);
    du = std::min(du, 1.0f - du);
    dv = std::min(dv, 1.0f - dv);
    return std::sqrt(du * du + dv * dv);
}

std::uint8_t ToUnorm(float value)
{
    return static_cast<std::uint8_t>(
        std::lround(Saturate(value) * 255.0f));
}

bool ChannelSettingsEqual(const PeriodicChannelSettings& a,
                          const PeriodicChannelSettings& b)
{
    return a.seed == b.seed &&
           a.macroPeriod == b.macroPeriod &&
           a.detailPeriod == b.detailPeriod &&
           a.detailWeight == b.detailWeight &&
           a.bias == b.bias &&
           a.contrast == b.contrast;
}
}

WeatherMapGeneratorSettings SanitizeWeatherMapGeneratorSettings(
    const WeatherMapGeneratorSettings& settings)
{
    WeatherMapGeneratorSettings result = settings;
    const auto sanitizeChannel = [](PeriodicChannelSettings& channel)
    {
        channel.macroPeriod = std::clamp(channel.macroPeriod, 1u, 8u);
        channel.detailPeriod = std::clamp(channel.detailPeriod, 2u, 16u);
        channel.detailWeight = std::clamp(
            FiniteOr(channel.detailWeight, 0.25f), 0.0f, 1.0f);
        channel.bias = std::clamp(
            FiniteOr(channel.bias, 0.0f), -0.5f, 0.5f);
        channel.contrast = std::clamp(
            FiniteOr(channel.contrast, 1.0f), 0.25f, 3.0f);
    };
    sanitizeChannel(result.coverage);
    sanitizeChannel(result.cloudType);
    sanitizeChannel(result.density);
    sanitizeChannel(result.localThickness);
    result.coverageThreshold = std::clamp(
        FiniteOr(result.coverageThreshold, 0.56f), 0.0f, 1.0f);
    result.coverageSoftness = std::clamp(
        FiniteOr(result.coverageSoftness, 0.14f), 0.02f, 0.8f);
    result.densityCoverageInfluence = std::clamp(
        FiniteOr(result.densityCoverageInfluence, 0.35f), 0.0f, 1.0f);
    result.thicknessCoverageInfluence = std::clamp(
        FiniteOr(result.thicknessCoverageInfluence, 0.20f), 0.0f, 1.0f);
    return result;
}

bool WeatherMapGeneratorSettingsEqual(const WeatherMapGeneratorSettings& a,
                                      const WeatherMapGeneratorSettings& b)
{
    return ChannelSettingsEqual(a.coverage, b.coverage) &&
           ChannelSettingsEqual(a.cloudType, b.cloudType) &&
           ChannelSettingsEqual(a.density, b.density) &&
           ChannelSettingsEqual(a.localThickness, b.localThickness) &&
           a.coverageThreshold == b.coverageThreshold &&
           a.coverageSoftness == b.coverageSoftness &&
           a.densityCoverageInfluence == b.densityCoverageInfluence &&
           a.thicknessCoverageInfluence == b.thicknessCoverageInfluence;
}

WeatherMapDefinition SanitizeWeatherMapDefinition(
    const WeatherMapDefinition& definition)
{
    WeatherMapDefinition result = definition;
    result.generator = SanitizeWeatherMapGeneratorSettings(result.generator);
    result.column = SanitizeWeatherColumnSettings(result.column);
    result.worldSizeMeters = std::clamp(
        FiniteOr(result.worldSizeMeters, 64000.0f), 1000.0f, 1000000.0f);
    return result;
}

WeatherMapComputeParameters BuildWeatherMapComputeParameters(
    Stage5WeatherPreset preset, const WeatherMapGeneratorSettings& settings)
{
    const WeatherMapGeneratorSettings safe =
        SanitizeWeatherMapGeneratorSettings(settings);
    WeatherMapComputeParameters result;
    const PeriodicChannelSettings inputs[4] = {
        safe.coverage, safe.cloudType, safe.density, safe.localThickness };
    for (std::size_t index = 0; index < 4; ++index)
    {
        result.channels[index].seed = inputs[index].seed;
        result.channels[index].macroPeriod = inputs[index].macroPeriod;
        result.channels[index].detailPeriod = inputs[index].detailPeriod;
        result.channels[index].detailWeight = inputs[index].detailWeight;
        result.channels[index].bias = inputs[index].bias;
        result.channels[index].contrast = inputs[index].contrast;
    }
    result.coverageThreshold = safe.coverageThreshold;
    result.coverageSoftness = safe.coverageSoftness;
    result.densityCoverageInfluence = safe.densityCoverageInfluence;
    result.thicknessCoverageInfluence = safe.thicknessCoverageInfluence;
    result.preset = static_cast<std::uint32_t>(preset);
    return result;
}

float SamplePeriodicPerlin2D(float u, float v, std::uint32_t period,
                             std::uint32_t seed)
{
    const std::int32_t safePeriod = static_cast<std::int32_t>(
        std::clamp(period, 1u, 16u));
    const float safeU = FiniteOr(u, 0.0f);
    const float safeV = FiniteOr(v, 0.0f);
    const float x = safeU * static_cast<float>(safePeriod);
    const float y = safeV * static_cast<float>(safePeriod);
    const std::int32_t x0 = static_cast<std::int32_t>(std::floor(x));
    const std::int32_t y0 = static_cast<std::int32_t>(std::floor(y));
    const float fx = x - std::floor(x);
    const float fy = y - std::floor(y);
    const std::int32_t wx0 = WrapLattice(x0, safePeriod);
    const std::int32_t wy0 = WrapLattice(y0, safePeriod);
    const std::int32_t wx1 = WrapLattice(x0 + 1, safePeriod);
    const std::int32_t wy1 = WrapLattice(y0 + 1, safePeriod);

    const float n00 = GradientDot(HashLattice(wx0, wy0, seed), fx, fy);
    const float n10 = GradientDot(HashLattice(wx1, wy0, seed), fx - 1.0f, fy);
    const float n01 = GradientDot(HashLattice(wx0, wy1, seed), fx, fy - 1.0f);
    const float n11 = GradientDot(
        HashLattice(wx1, wy1, seed), fx - 1.0f, fy - 1.0f);
    const float fadeX = Fade(fx);
    const float fadeY = Fade(fy);
    const float nx0 = n00 + (n10 - n00) * fadeX;
    const float nx1 = n01 + (n11 - n01) * fadeX;
    const float noise = nx0 + (nx1 - nx0) * fadeY;
    return Saturate(0.5f + noise * 0.7071067811865475244f);
}

WeatherMapData BuildWeatherMap(Stage5WeatherPreset preset,
                               const WeatherMapGeneratorSettings& settings)
{
    const WeatherMapGeneratorSettings safe =
        SanitizeWeatherMapGeneratorSettings(settings);
    WeatherMapData map;
    map.rgba.resize(static_cast<std::size_t>(map.width) * map.height * 4u);

    for (std::uint32_t y = 0; y < map.height; ++y)
    {
        for (std::uint32_t x = 0; x < map.width; ++x)
        {
            const float u = (static_cast<float>(x) + 0.5f) /
                static_cast<float>(map.width);
            const float v = (static_cast<float>(y) + 0.5f) /
                static_cast<float>(map.height);

            float coverage = 1.0f;
            float cloudType = 0.5f;
            float density = 0.5f;
            float localThickness = 1.0f;

            if (preset == Stage5WeatherPreset::PeriodicPerlin)
            {
                localThickness = 0.0f;
                const float coverageField = AdjustField(
                    SampleTwoScale(u, v, safe.coverage), safe.coverage);
                const float halfSoftness = safe.coverageSoftness * 0.5f;
                coverage = Smoothstep(
                    safe.coverageThreshold - halfSoftness,
                    safe.coverageThreshold + halfSoftness, coverageField);
                // RGBA8에 기록된 R이 0 또는 1이면 빈 영역으로 취급한다. float
                // 기준만 쓰면 반올림 뒤 R=1인데 G/B가 활성인 경계 픽셀이 생긴다.
                if (ToUnorm(coverage) > 1u)
                {
                    cloudType = AdjustField(
                        SampleTwoScale(u, v, safe.cloudType), safe.cloudType);
                    const float densityField = AdjustField(
                        SampleTwoScale(u, v, safe.density), safe.density);
                    density = Saturate(densityField +
                        (coverage - densityField) *
                            safe.densityCoverageInfluence);
                    const float thicknessField = AdjustField(
                        SampleTwoScale(u, v, safe.localThickness),
                        safe.localThickness);
                    const float coverageCore = Smoothstep(0.05f, 0.95f, coverage);
                    localThickness = Saturate(thicknessField +
                        (coverageCore - thicknessField) *
                            safe.thicknessCoverageInfluence);
                }
            }
            else if (preset == Stage5WeatherPreset::ChannelDebug)
            {
                const float largeIsland = 1.0f - Smoothstep(
                    0.16f, 0.29f, TiledDistance(u, v, 0.30f, 0.34f));
                const float smallIsland = 1.0f - Smoothstep(
                    0.11f, 0.23f, TiledDistance(u, v, 0.73f, 0.69f));
                coverage = std::max(largeIsland, smallIsland);
                cloudType = v < (1.0f / 3.0f) ? 0.0f :
                    (v < (2.0f / 3.0f) ? 0.5f : 1.0f);
                density = u < (1.0f / 3.0f) ? 0.0f :
                    (u < (2.0f / 3.0f) ? 0.5f : 1.0f);
                // 큰 섬은 낮고 넓게, 작은 섬은 높게 만들어 A 채널을 구분한다.
                localThickness = coverage > 0.0f
                    ? (smallIsland > largeIsland ? 0.85f : 0.35f) : 0.0f;
            }
            const std::size_t index =
                (static_cast<std::size_t>(y) * map.width + x) * 4u;
            map.rgba[index + 0] = ToUnorm(coverage);
            map.rgba[index + 1] = ToUnorm(cloudType);
            map.rgba[index + 2] = ToUnorm(density);
            map.rgba[index + 3] = ToUnorm(localThickness);
        }
    }
    return map;
}

bool IsValidWeatherMapData(const WeatherMapData& map)
{
    return map.width == kWeatherMapSize && map.height == kWeatherMapSize &&
           map.rgba.size() == static_cast<std::size_t>(map.width) *
               map.height * 4u;
}

std::uint64_t HashWeatherMap(const WeatherMapData& map)
{
    return fnv1a64::Hash(map.rgba.data(), map.rgba.size());
}
