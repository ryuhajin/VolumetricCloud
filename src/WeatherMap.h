// ============================================================================
//  WeatherMap.h - 단계 5 CPU 2-scale periodic Perlin Weather Map
// ----------------------------------------------------------------------------
//  GPU에 올리기 전의 256² RGBA8 데이터와 CPU 전용 생성 설정이다.
//  CloudCB에는 포함하지 않으며 UI가 바꾸면 같은 texture에 UpdateSubresource한다.
// ============================================================================
#pragma once

#include "CloudParameters.h"

#include <cstdint>
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
        1013u, 2u, 5u, 0.28f, 0.0f, 1.0f
    };
    PeriodicChannelSettings cloudType = {
        2027u, 2u, 4u, 0.20f, 0.0f, 0.85f
    };
    PeriodicChannelSettings density = {
        3041u, 3u, 6u, 0.25f, 0.0f, 0.75f
    };
    float coverageThreshold = 0.52f;
    float coverageSoftness = 0.22f;
    float densityCoverageInfluence = 0.35f;
};

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
float SamplePeriodicPerlin2D(float u, float v, std::uint32_t period,
                             std::uint32_t seed);
WeatherMapData BuildWeatherMap(
    Stage5WeatherPreset preset,
    const WeatherMapGeneratorSettings& settings = {});
bool IsValidWeatherMapData(const WeatherMapData& map);
std::uint64_t HashWeatherMap(const WeatherMapData& map);
