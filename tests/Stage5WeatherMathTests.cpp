#include "Stage5WeatherMath.h"
#include "Stage4DetailMath.h"
#include "WeatherMap.h"

#include <cmath>
#include <cstdio>

namespace
{
bool NearlyEqual(float a, float b, float tolerance = 1e-5f)
{
    return std::abs(a - b) <= tolerance;
}

int Fail(const char* message)
{
    std::fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

float Channel(const WeatherMapData& map, std::uint32_t x,
              std::uint32_t y, std::uint32_t channel)
{
    const std::size_t index =
        (static_cast<std::size_t>(y) * map.width + x) * 4u + channel;
    return static_cast<float>(map.rgba[index]) / 255.0f;
}

float MaximumInteriorDelta(const WeatherMapData& map, std::uint32_t channel)
{
    float maximum = 0.0f;
    for (std::uint32_t y = 0; y + 1 < map.height; ++y)
        for (std::uint32_t x = 0; x + 1 < map.width; ++x)
        {
            maximum = std::max(maximum, std::abs(
                Channel(map, x + 1, y, channel) - Channel(map, x, y, channel)));
            maximum = std::max(maximum, std::abs(
                Channel(map, x, y + 1, channel) - Channel(map, x, y, channel)));
        }
    return maximum;
}

float MaximumSeamDelta(const WeatherMapData& map, std::uint32_t channel)
{
    float maximum = 0.0f;
    for (std::uint32_t y = 0; y < map.height; ++y)
        maximum = std::max(maximum, std::abs(
            Channel(map, 0, y, channel) -
            Channel(map, map.width - 1, y, channel)));
    for (std::uint32_t x = 0; x < map.width; ++x)
        maximum = std::max(maximum, std::abs(
            Channel(map, x, 0, channel) -
            Channel(map, x, map.height - 1, channel)));
    return maximum;
}
}

int main()
{
    const WeatherMapGeneratorSettings defaults;
    const WeatherMapData uniformA = BuildWeatherMap(Stage5WeatherPreset::UniformLegacy);
    const WeatherMapData uniformB = BuildWeatherMap(Stage5WeatherPreset::UniformLegacy);
    const WeatherMapData perlinA = BuildWeatherMap(Stage5WeatherPreset::PeriodicPerlin, defaults);
    const WeatherMapData perlinB = BuildWeatherMap(Stage5WeatherPreset::PeriodicPerlin, defaults);
    const WeatherMapData debug = BuildWeatherMap(Stage5WeatherPreset::ChannelDebug);
    if (uniformA.width != 256 || uniformA.height != 256 ||
        uniformA.rgba.size() != 256u * 256u * 4u ||
        HashWeatherMap(uniformA) != HashWeatherMap(uniformB))
        return Fail("weather presets must be deterministic 256x256 RGBA maps");

    if (!NearlyEqual(Channel(uniformA, 0, 0, 0), 1.0f) ||
        !NearlyEqual(stage5::DecodeCanonicalWeatherChannel(
            Channel(uniformA, 0, 0, 1)), 0.5f) ||
        !NearlyEqual(stage5::DecodeCanonicalWeatherChannel(
            Channel(uniformA, 0, 0, 2)), 0.5f) ||
        !NearlyEqual(Channel(uniformA, 0, 0, 3), 1.0f))
        return Fail("uniform map must preserve the stage 4 neutral channels");
    for (std::uint32_t y = 0; y < uniformA.height; ++y)
        for (std::uint32_t x = 0; x < uniformA.width; ++x)
            for (std::uint32_t channel = 0; channel < 4; ++channel)
                if (Channel(uniformA, x, y, channel) !=
                    Channel(uniformA, 0, 0, channel))
                    return Fail("uniform legacy channels must be constant over the full map");

    if (HashWeatherMap(perlinA) != HashWeatherMap(perlinB))
        return Fail("periodic Perlin weather map must be deterministic");

    constexpr float derivativeStep = 1e-3f;
    for (int index = 0; index <= 16; ++index)
    {
        const float coordinate = static_cast<float>(index) / 16.0f;
        const float origin = SamplePeriodicPerlin2D(
            0.0f, coordinate, 5u, defaults.coverage.seed);
        const float repeated = SamplePeriodicPerlin2D(
            1.0f, coordinate, 5u, defaults.coverage.seed);
        const float derivativeAtZero = (
            SamplePeriodicPerlin2D(derivativeStep, coordinate, 5u, defaults.coverage.seed) -
            SamplePeriodicPerlin2D(-derivativeStep, coordinate, 5u, defaults.coverage.seed)) /
            (2.0f * derivativeStep);
        const float derivativeAtOne = (
            SamplePeriodicPerlin2D(1.0f + derivativeStep, coordinate, 5u, defaults.coverage.seed) -
            SamplePeriodicPerlin2D(1.0f - derivativeStep, coordinate, 5u, defaults.coverage.seed)) /
            (2.0f * derivativeStep);
        if (!NearlyEqual(origin, repeated, 1e-6f) ||
            !NearlyEqual(derivativeAtZero, derivativeAtOne, 1e-3f))
            return Fail("periodic Perlin value and derivative must repeat at UV seams");
        if (!NearlyEqual(
                SamplePeriodicPerlin2D(coordinate, 0.0f, 5u, defaults.coverage.seed),
                SamplePeriodicPerlin2D(coordinate, 1.0f, 5u, defaults.coverage.seed),
                1e-6f))
            return Fail("periodic Perlin must also repeat across the V seam");
    }

    float channelMinimum[3] = { 1.0f, 1.0f, 1.0f };
    float channelMaximum[3] = { 0.0f, 0.0f, 0.0f };
    for (std::uint32_t y = 0; y < perlinA.height; ++y)
        for (std::uint32_t x = 0; x < perlinA.width; ++x)
        {
            for (std::uint32_t channel = 0; channel < 3; ++channel)
            {
                const float value = Channel(perlinA, x, y, channel);
                channelMinimum[channel] = std::min(channelMinimum[channel], value);
                channelMaximum[channel] = std::max(channelMaximum[channel], value);
            }
        }
    if (channelMinimum[0] > 0.01f || channelMaximum[0] < 0.99f ||
        channelMaximum[1] - channelMinimum[1] < 0.25f ||
        channelMaximum[2] - channelMinimum[2] < 0.25f)
    {
        std::fprintf(stderr,
            "Perlin ranges: R %.3f..%.3f G %.3f..%.3f B %.3f..%.3f\n",
            channelMinimum[0], channelMaximum[0],
            channelMinimum[1], channelMaximum[1],
            channelMinimum[2], channelMaximum[2]);
        return Fail("periodic Perlin map must vary coverage, type and density");
    }
    for (std::uint32_t channel = 0; channel < 3; ++channel)
        if (MaximumSeamDelta(perlinA, channel) >
            MaximumInteriorDelta(perlinA, channel) + (2.0f / 255.0f))
            return Fail("periodic Perlin channels must be continuous across tile seams");

    WeatherMapGeneratorSettings changed = defaults;
    ++changed.coverage.seed;
    const WeatherMapData changedCoverage = BuildWeatherMap(
        Stage5WeatherPreset::PeriodicPerlin, changed);
    if (HashWeatherMap(changedCoverage) == HashWeatherMap(perlinA))
        return Fail("coverage seed must change the generated map");
    if (HashWeatherMap(BuildWeatherMap(
            Stage5WeatherPreset::UniformLegacy, changed)) != HashWeatherMap(uniformA) ||
        HashWeatherMap(BuildWeatherMap(
            Stage5WeatherPreset::ChannelDebug, changed)) != HashWeatherMap(debug))
        return Fail("F2 and F4 must ignore periodic generator settings");
    changed = defaults;
    changed.coverage.macroPeriod = 3u;
    if (HashWeatherMap(BuildWeatherMap(
            Stage5WeatherPreset::PeriodicPerlin, changed)) == HashWeatherMap(perlinA))
        return Fail("coverage period must change the generated map");
    changed = defaults;
    changed.coverage.detailWeight = 0.65f;
    if (HashWeatherMap(BuildWeatherMap(
            Stage5WeatherPreset::PeriodicPerlin, changed)) == HashWeatherMap(perlinA))
        return Fail("coverage detail weight must change the generated map");
    changed = defaults;
    ++changed.cloudType.seed;
    const WeatherMapData changedType = BuildWeatherMap(
        Stage5WeatherPreset::PeriodicPerlin, changed);
    changed = defaults;
    ++changed.density.seed;
    const WeatherMapData changedDensity = BuildWeatherMap(
        Stage5WeatherPreset::PeriodicPerlin, changed);
    bool typeChanged = false;
    bool densityChanged = false;
    for (std::uint32_t y = 0; y < perlinA.height; ++y)
        for (std::uint32_t x = 0; x < perlinA.width; ++x)
        {
            if (Channel(perlinA, x, y, 1) != Channel(changedType, x, y, 1))
                typeChanged = true;
            if (Channel(perlinA, x, y, 2) != Channel(changedDensity, x, y, 2))
                densityChanged = true;
            if (Channel(perlinA, x, y, 0) != Channel(changedType, x, y, 0) ||
                Channel(perlinA, x, y, 2) != Channel(changedType, x, y, 2))
                return Fail("type seed must only change the G channel");
            if (Channel(perlinA, x, y, 0) != Channel(changedDensity, x, y, 0) ||
                Channel(perlinA, x, y, 1) != Channel(changedDensity, x, y, 1))
                return Fail("density seed must only change the B channel");
        }
    if (!typeChanged || !densityChanged)
        return Fail("type and density seeds must change their target channels");

    bool foundEmptyPixel = false;
    for (std::uint32_t y = 0; y < perlinA.height && !foundEmptyPixel; ++y)
        for (std::uint32_t x = 0; x < perlinA.width; ++x)
            if (perlinA.rgba[(static_cast<std::size_t>(y) * perlinA.width + x) * 4u] <= 1u)
            {
                const std::size_t pixel =
                    (static_cast<std::size_t>(y) * perlinA.width + x) * 4u;
                if (perlinA.rgba[pixel + 1u] != 128u ||
                    perlinA.rgba[pixel + 2u] != 128u)
                    return Fail("empty weather pixels must store neutral G/B channels");
                foundEmptyPixel = true;
                break;
            }
    if (!foundEmptyPixel)
        return Fail("default periodic map must contain clear pixels");

    WeatherMapGeneratorSettings invalidSettings = defaults;
    invalidSettings.coverage.macroPeriod = 0u;
    invalidSettings.coverage.detailPeriod = 999u;
    invalidSettings.coverage.detailWeight = NAN;
    invalidSettings.cloudType.bias = INFINITY;
    invalidSettings.density.contrast = -INFINITY;
    invalidSettings.coverageThreshold = NAN;
    invalidSettings.coverageSoftness = -INFINITY;
    const WeatherMapGeneratorSettings sanitized =
        SanitizeWeatherMapGeneratorSettings(invalidSettings);
    const WeatherMapData finiteMap = BuildWeatherMap(
        Stage5WeatherPreset::PeriodicPerlin, invalidSettings);
    if (sanitized.coverage.macroPeriod != 1u ||
        sanitized.coverage.detailPeriod != 16u ||
        !IsValidWeatherMapData(finiteMap))
        return Fail("invalid generator settings must clamp to a valid weather map");
    WeatherMapData invalidMap = finiteMap;
    invalidMap.rgba.pop_back();
    if (IsValidWeatherMapData(invalidMap))
        return Fail("invalid RGBA length must be rejected before GPU upload");

    if (!NearlyEqual(stage5::DecodeCanonicalWeatherChannel(
            Channel(debug, 128, 20, 1)), 0.0f) ||
        !NearlyEqual(stage5::DecodeCanonicalWeatherChannel(
            Channel(debug, 128, 128, 1)), 0.5f) ||
        !NearlyEqual(stage5::DecodeCanonicalWeatherChannel(
            Channel(debug, 128, 235, 1)), 1.0f) ||
        !NearlyEqual(stage5::DecodeCanonicalWeatherChannel(
            Channel(debug, 20, 128, 2)), 0.0f) ||
        !NearlyEqual(stage5::DecodeCanonicalWeatherChannel(
            Channel(debug, 128, 128, 2)), 0.5f) ||
        !NearlyEqual(stage5::DecodeCanonicalWeatherChannel(
            Channel(debug, 235, 128, 2)), 1.0f))
        return Fail("debug map must expose canonical type and density bands");
    if (Channel(debug, 77, 87, 0) < 0.99f ||
        Channel(debug, 0, 0, 0) > 0.01f)
        return Fail("channel debug must preserve its bright islands and dark background");

    const stage2::Float3 world = { 3.0f, 0.0f, -2.0f };
    const stage2::Float3 wind = { 1.0f, 0.0f, 0.0f };
    const stage5::Float2 uv0 = stage5::ComputeWeatherUv(
        world, 0.0f, 16.0f, wind, 0.1f, {});
    const stage5::Float2 repeated = stage5::ComputeWeatherUv(
        { world.x + 16.0f, 0.0f, world.z - 32.0f },
        0.0f, 16.0f, wind, 0.1f, {});
    const stage5::Float2 moved = stage5::ComputeWeatherUv(
        { world.x + 0.4f, 0.0f, world.z }, 4.0f,
        16.0f, wind, 0.1f, {});
    if (!NearlyEqual(uv0.x, repeated.x) || !NearlyEqual(uv0.y, repeated.y) ||
        !NearlyEqual(uv0.x, moved.x) || !NearlyEqual(uv0.y, moved.y))
        return Fail("weather UV must repeat and move in the configured wind direction");

    const stage5::Float2 invalid = stage5::ComputeWeatherUv(
        { 1e20f, 0.0f, -1e20f }, -10.0f, 0.0f,
        { 0.0f, 1.0f, 0.0f }, -1.0f,
        { INFINITY, NAN });
    if (!std::isfinite(invalid.x) || !std::isfinite(invalid.y) ||
        invalid.x < 0.0f || invalid.x >= 1.0f ||
        invalid.y < 0.0f || invalid.y >= 1.0f)
        return Fail("invalid weather transforms must remain finite and wrapped");

    if (!NearlyEqual(stage5::EvaluateWeatherBaseDensity(
            0.9f, 0.55f, 0.0f, 0.5f, 0.5f,
            0.5f, 0.2f, 0.8f, 1.0f), 0.0f))
        return Fail("dark weather coverage must remove the base density");
    if (!NearlyEqual(0.5f + stage5::DecodeCanonicalWeatherChannel(0.0f), 0.5f) ||
        !NearlyEqual(0.5f + stage5::DecodeCanonicalWeatherChannel(128.0f / 255.0f), 1.0f) ||
        !NearlyEqual(0.5f + stage5::DecodeCanonicalWeatherChannel(1.0f), 1.5f))
        return Fail("weather B must decode to the 0.5/1.0/1.5 density multipliers");

    for (int index = 0; index <= 100; ++index)
    {
        const float h = static_cast<float>(index) / 100.0f;
        const float mixed = stage3::EvaluateHeightProfileFromFraction(
            h, 0.2f, 0.8f);
        if (!NearlyEqual(stage5::EvaluateTypedHeightProfile(
                h, 0.5f, 0.2f, 0.8f), mixed))
            return Fail("cloud type 0.5 must exactly preserve the stage 3 profile");
    }
    if (stage5::EvaluateStratusProfile(0.75f) > 1e-5f ||
        stage5::EvaluateCumulusProfile(0.85f, 0.2f, 0.8f) <=
        stage3::EvaluateHeightProfileFromFraction(0.85f, 0.2f, 0.8f))
        return Fail("stratus must stay low and cumulus must retain more upper mass");
    const float left = stage5::EvaluateTypedHeightProfile(
        0.45f, 0.5f - 1e-5f, 0.2f, 0.8f);
    const float right = stage5::EvaluateTypedHeightProfile(
        0.45f, 0.5f + 1e-5f, 0.2f, 0.8f);
    if (std::abs(left - right) > 1e-4f ||
        !std::isfinite(stage5::EvaluateTypedHeightProfile(
            NAN, INFINITY, 0.2f, 0.8f)))
        return Fail("typed profile must be continuous, clamped and finite");

    const float mixedBottom = stage5::EvaluateTypedWeatherCoverage(0.55f, 0.0f, 0.5f);
    const float mixedMiddle = stage5::EvaluateTypedWeatherCoverage(0.55f, 0.5f, 0.5f);
    const float mixedTop = stage5::EvaluateTypedWeatherCoverage(0.55f, 1.0f, 0.5f);
    const float cumulusBottom = stage5::EvaluateTypedWeatherCoverage(0.55f, 0.0f, 1.0f);
    const float cumulusMiddle = stage5::EvaluateTypedWeatherCoverage(0.55f, 0.5f, 1.0f);
    const float cumulusTop = stage5::EvaluateTypedWeatherCoverage(0.55f, 1.0f, 1.0f);
    if (!(mixedMiddle > mixedBottom && mixedMiddle > mixedTop &&
          cumulusMiddle > cumulusBottom && cumulusMiddle > cumulusTop))
        return Fail("mixed and cumulus footprints must be widest at middle height");
    if (!NearlyEqual(stage5::EvaluateTypedWeatherCoverage(0.55f, 0.0f, 0.0f),
                     stage5::EvaluateTypedWeatherCoverage(0.55f, 1.0f, 0.0f)) ||
        !NearlyEqual(stage5::EvaluateTypedWeatherCoverage(1.0f, 0.0f, 1.0f), 1.0f) ||
        !NearlyEqual(stage5::EvaluateTypedWeatherCoverage(1.0f, 1.0f, 0.5f), 1.0f) ||
        !std::isfinite(stage5::EvaluateTypedWeatherCoverage(NAN, INFINITY, INFINITY)))
        return Fail("typed footprint must preserve stratus width, legacy coverage and finite output");
    const float footprintLeft = stage5::EvaluateTypedWeatherCoverage(
        0.55f, 0.45f, 0.5f - 1e-5f);
    const float footprintRight = stage5::EvaluateTypedWeatherCoverage(
        0.55f, 0.45f, 0.5f + 1e-5f);
    if (std::abs(footprintLeft - footprintRight) > 1e-4f)
        return Fail("typed footprint must remain continuous around cloud type 0.5");

    const float legacyExpected = stage3::ApplyHeightProfile(
        stage5::RemapCoverage(0.9f, 0.55f),
        stage3::EvaluateHeightProfileFromFraction(0.5f, 0.2f, 0.8f), 1.0f);
    const float legacyWeather = stage5::EvaluateWeatherBaseDensity(
        0.9f, 0.55f, 1.0f, 0.5f, 0.5f,
        0.5f, 0.2f, 0.8f, 1.0f);
    if (!NearlyEqual(legacyWeather, legacyExpected))
        return Fail("uniform legacy weather must preserve the stage 4 base density");

    const float weatherEmptyBase = stage5::EvaluateWeatherBaseDensity(
        0.9f, 0.55f, 0.0f, 1.0f, 1.0f,
        0.8f, 0.2f, 0.8f, 1.0f);
    const stage4::DetailDensitySample skipped = stage4::ApplyDetailErosion(
        weatherEmptyBase, world, 0.0f, 2.5f, 0.25f,
        wind, 0.45f, 17.3f, true);
    if (skipped.detailSampled)
        return Fail("weather-empty base must skip stage 4 detail sampling");

    std::puts("Stage 5 weather map math tests passed");
    return 0;
}
