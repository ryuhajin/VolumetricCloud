#include "Stage5WeatherMath.h"
#include "Stage4DetailMath.h"
#include "WeatherMap.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <queue>
#include <vector>

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

int CountCoveredComponents(const WeatherMapData& map)
{
    std::vector<bool> visited(
        static_cast<std::size_t>(map.width) * map.height, false);
    int components = 0;
    for (std::uint32_t y = 0; y < map.height; ++y)
        for (std::uint32_t x = 0; x < map.width; ++x)
        {
            const std::size_t start =
                static_cast<std::size_t>(y) * map.width + x;
            if (visited[start] || Channel(map, x, y, 0) <= 1.0f / 255.0f)
                continue;
            ++components;
            std::queue<std::pair<std::uint32_t, std::uint32_t>> pending;
            pending.push({ x, y });
            visited[start] = true;
            while (!pending.empty())
            {
                const auto [cx, cy] = pending.front();
                pending.pop();
                const int dx[] = { -1, 1, 0, 0 };
                const int dy[] = { 0, 0, -1, 1 };
                for (int index = 0; index < 4; ++index)
                {
                    const int nx = static_cast<int>(cx) + dx[index];
                    const int ny = static_cast<int>(cy) + dy[index];
                    if (nx < 0 || ny < 0 || nx >= static_cast<int>(map.width) ||
                        ny >= static_cast<int>(map.height))
                        continue;
                    const std::size_t next = static_cast<std::size_t>(ny) *
                        map.width + static_cast<std::uint32_t>(nx);
                    if (!visited[next] && Channel(map,
                            static_cast<std::uint32_t>(nx),
                            static_cast<std::uint32_t>(ny), 0) > 1.0f / 255.0f)
                    {
                        visited[next] = true;
                        pending.push({ static_cast<std::uint32_t>(nx),
                                       static_cast<std::uint32_t>(ny) });
                    }
                }
            }
        }
    return components;
}

struct PeriodicCoverageStats
{
    std::size_t coveredPixels = 0;
    std::size_t componentCount = 0;
    std::size_t largestComponentPixels = 0;
};

PeriodicCoverageStats MeasurePeriodicCoverage(const WeatherMapData& map)
{
    PeriodicCoverageStats stats;
    std::vector<bool> visited(
        static_cast<std::size_t>(map.width) * map.height, false);
    for (std::uint32_t y = 0; y < map.height; ++y)
        for (std::uint32_t x = 0; x < map.width; ++x)
        {
            const std::size_t start =
                static_cast<std::size_t>(y) * map.width + x;
            if (Channel(map, x, y, 0) > 1.0f / 255.0f)
                ++stats.coveredPixels;
            if (visited[start] || Channel(map, x, y, 0) <= 1.0f / 255.0f)
                continue;

            ++stats.componentCount;
            std::size_t componentPixels = 0;
            std::queue<std::pair<std::uint32_t, std::uint32_t>> pending;
            pending.push({ x, y });
            visited[start] = true;
            while (!pending.empty())
            {
                const auto [cx, cy] = pending.front();
                pending.pop();
                ++componentPixels;
                const int dx[] = { -1, 1, 0, 0 };
                const int dy[] = { 0, 0, -1, 1 };
                for (int index = 0; index < 4; ++index)
                {
                    const std::uint32_t nx = static_cast<std::uint32_t>(
                        (static_cast<int>(cx) + dx[index] +
                         static_cast<int>(map.width)) %
                        static_cast<int>(map.width));
                    const std::uint32_t ny = static_cast<std::uint32_t>(
                        (static_cast<int>(cy) + dy[index] +
                         static_cast<int>(map.height)) %
                        static_cast<int>(map.height));
                    const std::size_t next =
                        static_cast<std::size_t>(ny) * map.width + nx;
                    if (!visited[next] &&
                        Channel(map, nx, ny, 0) > 1.0f / 255.0f)
                    {
                        visited[next] = true;
                        pending.push({ nx, ny });
                    }
                }
            }
            stats.largestComponentPixels = std::max(
                stats.largestComponentPixels, componentPixels);
        }
    return stats;
}

double CoveredChannelCorrelation(const WeatherMapData& map,
                                 std::uint32_t channelA,
                                 std::uint32_t channelB)
{
    double sumA = 0.0;
    double sumB = 0.0;
    std::size_t count = 0;
    for (std::uint32_t y = 0; y < map.height; ++y)
        for (std::uint32_t x = 0; x < map.width; ++x)
            if (Channel(map, x, y, 0) > 1.0f / 255.0f)
            {
                sumA += Channel(map, x, y, channelA);
                sumB += Channel(map, x, y, channelB);
                ++count;
            }
    const double meanA = sumA / std::max<std::size_t>(count, 1u);
    const double meanB = sumB / std::max<std::size_t>(count, 1u);
    double covariance = 0.0;
    double varianceA = 0.0;
    double varianceB = 0.0;
    for (std::uint32_t y = 0; y < map.height; ++y)
        for (std::uint32_t x = 0; x < map.width; ++x)
            if (Channel(map, x, y, 0) > 1.0f / 255.0f)
            {
                const double a = Channel(map, x, y, channelA) - meanA;
                const double b = Channel(map, x, y, channelB) - meanB;
                covariance += a * b;
                varianceA += a * a;
                varianceB += b * b;
            }
    return covariance / std::sqrt(
        std::max(varianceA * varianceB, 1e-20));
}
}

int main()
{
    const WeatherMapGeneratorSettings defaults;
    if (defaults.coverage.seed != 1013u ||
        defaults.coverage.macroPeriod != 4u ||
        defaults.coverage.detailPeriod != 11u ||
        !NearlyEqual(defaults.coverage.detailWeight, 0.42f) ||
        !NearlyEqual(defaults.coverage.bias, -0.02f) ||
        !NearlyEqual(defaults.coverage.contrast, 1.15f) ||
        !NearlyEqual(defaults.coverageThreshold, 0.56f) ||
        !NearlyEqual(defaults.coverageSoftness, 0.14f) ||
        !NearlyEqual(defaults.thicknessCoverageInfluence, 0.20f))
        return Fail("Open World weather defaults changed");
    WeatherMapGeneratorSettings equalDefaults = defaults;
    if (!WeatherMapGeneratorSettingsEqual(defaults, equalDefaults))
        return Fail("equal weather generator settings must compare equal");
    equalDefaults.thicknessCoverageInfluence = 0.21f;
    if (WeatherMapGeneratorSettingsEqual(defaults, equalDefaults))
        return Fail("thickness-coverage influence must participate in equality");
    const WeatherMapData uniformA = BuildWeatherMap(Stage5WeatherPreset::UniformLegacy);
    const WeatherMapData uniformB = BuildWeatherMap(Stage5WeatherPreset::UniformLegacy);
    const WeatherMapData perlinA = BuildWeatherMap(Stage5WeatherPreset::PeriodicPerlin, defaults);
    const WeatherMapData perlinB = BuildWeatherMap(Stage5WeatherPreset::PeriodicPerlin, defaults);
    const WeatherMapData debug = BuildWeatherMap(Stage5WeatherPreset::ChannelDebug);
    if (uniformA.width != 256 || uniformA.height != 256 ||
        uniformA.rgba.size() != 256u * 256u * 4u ||
        HashWeatherMap(uniformA) != HashWeatherMap(uniformB))
        return Fail("weather presets must be deterministic 256x256 RGBA maps");

    WeatherMapGeneratorSettings globalStratusSettings = defaults;
    globalStratusSettings.cloudTypeMode = CloudTypeMode::Stratus;
    const WeatherMapData perlinStratus = BuildWeatherMap(
        Stage5WeatherPreset::PeriodicPerlin, globalStratusSettings);
    WeatherMapGeneratorSettings changedFixedStratus = globalStratusSettings;
    ++changedFixedStratus.cloudType.seed;
    if (HashWeatherMap(BuildWeatherMap(
            Stage5WeatherPreset::PeriodicPerlin, changedFixedStratus)) !=
        HashWeatherMap(perlinStratus))
    {
        return Fail(
            "fixed Cloud Type source must ignore disabled G generator fields");
    }
    for (std::uint32_t y = 0; y < perlinA.height; ++y)
        for (std::uint32_t x = 0; x < perlinA.width; ++x)
        {
            if (Channel(perlinStratus, x, y, 0) != Channel(perlinA, x, y, 0) ||
                Channel(perlinStratus, x, y, 2) != Channel(perlinA, x, y, 2) ||
                Channel(perlinStratus, x, y, 3) != Channel(perlinA, x, y, 3) ||
                !NearlyEqual(Channel(perlinStratus, x, y, 1), 0.0f))
                return Fail("global Cloud Type mode must replace only Weather G for Open World maps");
        }

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

    const PeriodicCoverageStats coverageStats =
        MeasurePeriodicCoverage(perlinA);
    const double totalPixels = static_cast<double>(perlinA.width) *
        static_cast<double>(perlinA.height);
    const double coveredFraction = coverageStats.coveredPixels / totalPixels;
    const double largestComponentFraction =
        coverageStats.largestComponentPixels / totalPixels;
    if (coveredFraction < 0.40 || coveredFraction > 0.48 ||
        coverageStats.componentCount < 6u ||
        largestComponentFraction > 0.35)
    {
        std::fprintf(stderr,
            "Coverage stats: fraction %.5f components %zu largest %.5f\n",
            coveredFraction, coverageStats.componentCount,
            largestComponentFraction);
        return Fail("default coverage must form several medium weather groups");
    }

    WeatherMapGeneratorSettings independentThicknessSettings = defaults;
    independentThicknessSettings.thicknessCoverageInfluence = 0.0f;
    const WeatherMapData independentThickness = BuildWeatherMap(
        Stage5WeatherPreset::PeriodicPerlin, independentThicknessSettings);
    WeatherMapGeneratorSettings linkedThicknessSettings = defaults;
    linkedThicknessSettings.thicknessCoverageInfluence = 1.0f;
    const WeatherMapData linkedThickness = BuildWeatherMap(
        Stage5WeatherPreset::PeriodicPerlin, linkedThicknessSettings);
    const double independentCorrelation = CoveredChannelCorrelation(
        independentThickness, 0, 3);
    const double defaultCorrelation = CoveredChannelCorrelation(perlinA, 0, 3);
    const double linkedCorrelation = CoveredChannelCorrelation(
        linkedThickness, 0, 3);
    std::printf(
        "[WEATHER][DEFAULT] COVERAGE=%.5f COMPONENTS=%zu LARGEST=%.5f "
        "R_A_CORRELATION=%.5f PASS\n",
        coveredFraction, coverageStats.componentCount,
        largestComponentFraction, defaultCorrelation);
    if (defaultCorrelation > 0.65 ||
        !(independentCorrelation < defaultCorrelation &&
          defaultCorrelation < linkedCorrelation))
        return Fail("thickness link 0/0.20/1 must progress from independent to coverage-shaped");
    for (std::uint32_t y = 0; y < perlinA.height; ++y)
        for (std::uint32_t x = 0; x < perlinA.width; ++x)
        {
            const float coverage = Channel(perlinA, x, y, 0);
            const float actual = Channel(perlinA, x, y, 3);
            if (coverage <= 1.0f / 255.0f)
            {
                if (actual != 0.0f ||
                    Channel(independentThickness, x, y, 3) != 0.0f ||
                    Channel(linkedThickness, x, y, 3) != 0.0f)
                    return Fail("all thickness link modes must keep empty coverage A at zero");
                continue;
            }
            const float expected =
                Channel(independentThickness, x, y, 3) * 0.8f +
                Channel(linkedThickness, x, y, 3) * 0.2f;
            if (!NearlyEqual(actual, expected, 3.0f / 255.0f))
                return Fail("default thickness link must be an 80/20 interpolation");
        }

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

    float channelMinimum[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float channelMaximum[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float coveredHeightMinimum = 1.0f;
    float coveredHeightMaximum = 0.0f;
    for (std::uint32_t y = 0; y < perlinA.height; ++y)
        for (std::uint32_t x = 0; x < perlinA.width; ++x)
        {
            for (std::uint32_t channel = 0; channel < 4; ++channel)
            {
                const float value = Channel(perlinA, x, y, channel);
                channelMinimum[channel] = std::min(channelMinimum[channel], value);
                channelMaximum[channel] = std::max(channelMaximum[channel], value);
            }
            if (Channel(perlinA, x, y, 0) > (1.0f / 255.0f))
            {
                coveredHeightMinimum = std::min(
                    coveredHeightMinimum, Channel(perlinA, x, y, 3));
                coveredHeightMaximum = std::max(
                    coveredHeightMaximum, Channel(perlinA, x, y, 3));
            }
        }
    if (channelMinimum[0] > 0.01f || channelMaximum[0] < 0.99f ||
        channelMaximum[1] - channelMinimum[1] < 0.25f ||
        channelMaximum[2] - channelMinimum[2] < 0.25f ||
        coveredHeightMaximum - coveredHeightMinimum < 0.25f)
    {
        std::fprintf(stderr,
            "Perlin ranges: R %.3f..%.3f G %.3f..%.3f B %.3f..%.3f A(covered) %.3f..%.3f\n",
            channelMinimum[0], channelMaximum[0],
            channelMinimum[1], channelMaximum[1],
            channelMinimum[2], channelMaximum[2],
            coveredHeightMinimum, coveredHeightMaximum);
        return Fail("periodic Perlin map must vary coverage, type, density and covered height");
    }
    for (std::uint32_t channel = 0; channel < 4; ++channel)
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
        return Fail("non-Perlin presets must ignore periodic generator settings");
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
    changed = defaults;
    ++changed.localThickness.seed;
    const WeatherMapData changedHeight = BuildWeatherMap(
        Stage5WeatherPreset::PeriodicPerlin, changed);
    bool typeChanged = false;
    bool densityChanged = false;
    bool heightChanged = false;
    for (std::uint32_t y = 0; y < perlinA.height; ++y)
        for (std::uint32_t x = 0; x < perlinA.width; ++x)
        {
            if (Channel(perlinA, x, y, 1) != Channel(changedType, x, y, 1))
                typeChanged = true;
            if (Channel(perlinA, x, y, 2) != Channel(changedDensity, x, y, 2))
                densityChanged = true;
            if (Channel(perlinA, x, y, 3) != Channel(changedHeight, x, y, 3))
                heightChanged = true;
            if (Channel(perlinA, x, y, 0) != Channel(changedType, x, y, 0) ||
                Channel(perlinA, x, y, 2) != Channel(changedType, x, y, 2))
                return Fail("type seed must only change the G channel");
            if (Channel(perlinA, x, y, 0) != Channel(changedDensity, x, y, 0) ||
                Channel(perlinA, x, y, 1) != Channel(changedDensity, x, y, 1) ||
                Channel(perlinA, x, y, 3) != Channel(changedDensity, x, y, 3))
                return Fail("density seed must only change the B channel");
            for (std::uint32_t channel = 0; channel < 3; ++channel)
                if (Channel(perlinA, x, y, channel) !=
                    Channel(changedHeight, x, y, channel))
                    return Fail("height seed must only change the A channel");
        }
    if (!typeChanged || !densityChanged || !heightChanged)
        return Fail("type, density and height seeds must change their target channels");

    bool foundEmptyPixel = false;
    for (std::uint32_t y = 0; y < perlinA.height && !foundEmptyPixel; ++y)
        for (std::uint32_t x = 0; x < perlinA.width; ++x)
            if (perlinA.rgba[(static_cast<std::size_t>(y) * perlinA.width + x) * 4u] <= 1u)
            {
                const std::size_t pixel =
                    (static_cast<std::size_t>(y) * perlinA.width + x) * 4u;
                if (perlinA.rgba[pixel + 1u] != 128u ||
                    perlinA.rgba[pixel + 2u] != 128u ||
                    perlinA.rgba[pixel + 3u] != 0u)
                    return Fail("empty weather pixels must store neutral G/B and zero A");
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
    invalidSettings.localThickness.detailWeight = NAN;
    invalidSettings.coverageThreshold = NAN;
    invalidSettings.coverageSoftness = -INFINITY;
    invalidSettings.densityCoverageInfluence = INFINITY;
    invalidSettings.thicknessCoverageInfluence = NAN;
    const WeatherMapGeneratorSettings sanitized =
        SanitizeWeatherMapGeneratorSettings(invalidSettings);
    const WeatherMapData finiteMap = BuildWeatherMap(
        Stage5WeatherPreset::PeriodicPerlin, invalidSettings);
    if (sanitized.coverage.macroPeriod != 1u ||
        sanitized.coverage.detailPeriod != 16u ||
        !NearlyEqual(sanitized.coverageThreshold, 0.56f) ||
        !NearlyEqual(sanitized.coverageSoftness, 0.14f) ||
        !NearlyEqual(sanitized.densityCoverageInfluence, 0.35f) ||
        !NearlyEqual(sanitized.thicknessCoverageInfluence, 0.20f) ||
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
    if (!NearlyEqual(Channel(debug, 77, 87, 3), 0.35f, 1.0f / 255.0f) ||
        !NearlyEqual(Channel(debug, 187, 177, 3), 0.85f, 1.0f / 255.0f))
        return Fail("channel debug islands must expose distinct A heights");

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

    const float minimumThickness = 0.30f;
    float previousWeatherTop = 0.0f;
    for (int index = 0; index <= 100; ++index)
    {
        const float weatherHeight = static_cast<float>(index) / 100.0f;
        const float localTop = stage5::EvaluateLocalTopFraction(
            weatherHeight, 0.5f, minimumThickness, 1.0f, 0.35f);
        if (!std::isfinite(localTop) || localTop < minimumThickness ||
            localTop > 1.0f || localTop + 1e-6f < previousWeatherTop)
            return Fail("local top must be finite, bounded and monotonic in Weather A");
        previousWeatherTop = localTop;
    }
    float previousTypeTop = 0.0f;
    for (int index = 0; index <= 100; ++index)
    {
        const float type = static_cast<float>(index) / 100.0f;
        const float localTop = stage5::EvaluateLocalTopFraction(
            0.4f, type, minimumThickness, 1.0f, 0.35f);
        if (localTop + 1e-6f < previousTypeTop)
            return Fail("cumulus type increase must not lower the local top");
        previousTypeTop = localTop;
    }
    if (!NearlyEqual(stage5::EvaluateLocalTopFraction(
            0.2f, 0.0f, minimumThickness, 0.0f, 0.35f), 1.0f) ||
        !NearlyEqual(stage5::EvaluateLocalTopFraction(
            1.0f, 0.0f, minimumThickness, 1.0f, 0.35f), 1.0f) ||
        !std::isfinite(stage5::EvaluateLocalTopFraction(
            NAN, INFINITY, NAN, INFINITY, NAN)))
        return Fail("variation zero and Weather A one must exactly restore the legacy top");

    const float testLocalTop = stage5::EvaluateLocalTopFraction(
        0.45f, 0.7f, minimumThickness, 1.0f, 0.35f);
    const float justBelowTop = stage5::EvaluateWeatherBaseDensity(
        1.0f, 1.0f, 1.0f, 0.7f, 0.5f,
        testLocalTop - 1e-5f, 0.2f, 0.8f, 1.0f,
        0.45f, minimumThickness, 1.0f, 0.35f);
    const float aboveTop = stage5::EvaluateWeatherBaseDensity(
        1.0f, 1.0f, 1.0f, 0.7f, 0.5f,
        testLocalTop + 1e-5f, 0.2f, 0.8f, 1.0f,
        0.45f, minimumThickness, 1.0f, 0.35f);
    if (aboveTop != 0.0f || justBelowTop > 1e-3f ||
        !NearlyEqual(stage5::EvaluateLocalHeightFraction(0.0f, 0.3f), 0.0f))
        return Fail("density must fade continuously to zero above a fixed shared bottom");

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
        0.8f, 0.2f, 0.8f, 1.0f,
        1.0f, 0.30f, 1.0f, 0.35f);
    const stage4::DetailDensitySample skipped = stage4::ApplyDetailErosion(
        weatherEmptyBase, world, 0.0f, 2.5f, 0.25f,
        wind, 0.45f, 17.3f, true);
    if (skipped.detailSampled)
        return Fail("weather-empty base must skip stage 4 detail sampling");

    std::puts("Stage 5 weather map math tests passed");
    return 0;
}
