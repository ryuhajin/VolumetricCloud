// ============================================================================
//  WeatherMap.h - GPU Weather Map 계약과 CPU 2-scale periodic Perlin 검증 기준
// ----------------------------------------------------------------------------
//  런타임 Compute와 CPU 검증 기준이 공유하는 256² RGBA8 생성 계약이다.
// ============================================================================
#pragma once

#include "CloudParameters.h"
#include "WeatherColumnParameters.h"

#include <cstdint>
#include <cstddef>
#include <vector>

constexpr std::uint32_t kWeatherMapSize = 256;

struct PeriodicChannelSettings
{
    std::uint32_t seed = 0;
    std::uint32_t macroPeriod = 2;
    std::uint32_t detailPeriod = 5;
    float detailWeight = 0.25f;
    float bias = 0.0f;
    float contrast = 1.0f;
};

struct WeatherMapGeneratorSettings
{
    PeriodicChannelSettings coverage = {
        1013u, 4u, 11u, 0.42f, -0.02f, 1.15f
    };
    PeriodicChannelSettings cloudType = {
        2027u, 2u, 4u, 0.20f, 0.0f, 0.85f
    };
    PeriodicChannelSettings density = {
        3041u, 3u, 6u, 0.25f, 0.0f, 0.75f
    };
    PeriodicChannelSettings localThickness = {
        4051u, 2u, 5u, 0.20f, 0.0f, 0.90f
    };
    float coverageThreshold = 0.56f;
    float coverageSoftness = 0.14f;
    float densityCoverageInfluence = 0.35f;
    float thicknessCoverageInfluence = 0.20f;
};

struct WeatherMapDefinition
{
    // RGBA 생성만 소유하며 렌더 타입 선택과 이동은 포함하지 않는다.
    WeatherMapGeneratorSettings generator = {};
    WeatherColumnSettings column = {};
    float worldSizeMeters = 64000.0f;
};

struct alignas(16) WeatherMapChannelGpuParameters
{
    std::uint32_t seed = 0;
    std::uint32_t macroPeriod = 2;
    std::uint32_t detailPeriod = 5;
    float detailWeight = 0.25f;
    float bias = 0.0f;
    float contrast = 1.0f;
    float padding0 = 0.0f;
    float padding1 = 0.0f;
};

struct alignas(16) WeatherMapComputeParameters
{
    WeatherMapChannelGpuParameters channels[4] = {};
    float coverageThreshold = 0.56f;
    float coverageSoftness = 0.14f;
    float densityCoverageInfluence = 0.35f;
    float thicknessCoverageInfluence = 0.20f;
    std::uint32_t preset = 0;
    std::uint32_t width = kWeatherMapSize;
    std::uint32_t height = kWeatherMapSize;
    std::uint32_t padding0 = 0;
};

static_assert(sizeof(WeatherMapComputeParameters) == 160,
              "WeatherMap compute cbuffer ABI changed");

WeatherMapComputeParameters BuildWeatherMapComputeParameters(
    Stage5WeatherPreset preset, const WeatherMapGeneratorSettings& settings);

struct WeatherMapData
{
    std::uint32_t width = kWeatherMapSize;
    std::uint32_t height = kWeatherMapSize;
    std::vector<std::uint8_t> rgba;
};

WeatherMapGeneratorSettings SanitizeWeatherMapGeneratorSettings(
    const WeatherMapGeneratorSettings& settings);
bool WeatherMapGeneratorSettingsEqual(const WeatherMapGeneratorSettings& a,
                                      const WeatherMapGeneratorSettings& b);
WeatherMapDefinition SanitizeWeatherMapDefinition(
    const WeatherMapDefinition& definition);
float SamplePeriodicPerlin2D(float u, float v, std::uint32_t period,
                             std::uint32_t seed);
WeatherMapData BuildWeatherMap(
    Stage5WeatherPreset preset,
    const WeatherMapGeneratorSettings& settings = {});
bool IsValidWeatherMapData(const WeatherMapData& map);
std::uint64_t HashWeatherMap(const WeatherMapData& map);
