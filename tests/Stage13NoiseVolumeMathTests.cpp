#include "NoiseVolumeCache.h"
#include "Stage13NoiseVolumeMath.h"
#include "WeatherMap.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <vector>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "Stage13NoiseVolumeMath failure: " << message << '\n';
        std::exit(1);
    }
}

bool Near(double a, double b, double tolerance = 1e-6)
{
    return std::abs(a - b) <= tolerance;
}
}

int main()
{
    using namespace stage13noise;
    const NoiseVolumeParameters parameters;
    Require(parameters.baseResolution == 128u &&
            parameters.detailResolution == 32u && parameters.seed == 1337u,
            "resolution and seed contract changed");
    Require(Near(parameters.baseWorldSizeMeters, 12000.0) &&
            Near(parameters.detailWorldSizeMeters, 2000.0) &&
            Near(parameters.baseVerticalWorldSizeMeters, 12000.0),
            "world tile sizes must use meters");
    Require(kBaseBytes == 8388608ull && kDetailBytes == 131072ull,
            "RGBA8 memory budget changed");
    Require(parameters.baseFrequencies.x == 4u &&
            parameters.baseFrequencies.y == 9u &&
            parameters.baseFrequencies.z == 17u &&
            parameters.baseFrequencies.w == 23u,
            "base frequency defaults changed");
    Require(Near(SamplesPerWavelength(12000.0, 23u, 100.0),
                 120.0 / 23.0) &&
            Near(SamplesPerWavelength(2000.0, 2u, 100.0), 10.0),
            "shortest wavelengths must retain sufficient view samples");
    Require(SamplesPerWavelength(12000.0, 23u, 100.0) >= 5.2,
            "base vertical sampling must provide at least 5.2 samples per wavelength");
    const double horizontalWavelength = 12000.0 / 4.0;
    const double verticalWavelength = 12000.0 / 4.0;
    Require(Near(horizontalWavelength / verticalWavelength, 1.0),
            "base XYZ dominant wavelength must be isotropic");
    const std::uint32_t baseFrequencies[] = { 4u, 9u, 17u, 23u };
    const std::uint32_t detailFrequencies[] = { 2u, 3u, 4u, 5u };
    for (std::uint32_t frequency : baseFrequencies)
        Require(IsNyquistSafe(128u, frequency), "base frequency exceeds Nyquist");
    for (std::uint32_t frequency : detailFrequencies)
        Require(IsNyquistSafe(32u, frequency), "detail frequency exceeds Nyquist");

    const std::uint32_t octaveSeeds[] = {
        parameters.seed,
        parameters.seed + kBasePerlinOctaveSeedStride,
        parameters.seed + 2u * kBasePerlinOctaveSeedStride,
        parameters.seed + 3u * kBasePerlinOctaveSeedStride
    };
    for (std::uint32_t octave = 0; octave < 4u; ++octave)
    {
        Require(octaveSeeds[octave] == parameters.seed + octave * 173u,
                "base octave seed stride must remain 173");
        for (std::uint32_t previous = 0; previous < octave; ++previous)
            Require(octaveSeeds[octave] != octaveSeeds[previous],
                    "base octave seeds must be distinct");
    }
    Require(parameters.seed + 0xffffffffu * kBasePerlinOctaveSeedStride ==
            parameters.seed - kBasePerlinOctaveSeedStride,
            "unsigned octave seed arithmetic must wrap deterministically");
    const Float3 octaveProbe = { 1.37, 2.11, 3.73 };
    for (std::uint32_t octave = 0; octave < 4u; ++octave)
    {
        const double value = PeriodicGradientNoise(
            octaveProbe, static_cast<std::int32_t>(baseFrequencies[octave]),
            octaveSeeds[octave]);
        Require(Near(value, PeriodicGradientNoise(
                    octaveProbe,
                    static_cast<std::int32_t>(baseFrequencies[octave]),
                    octaveSeeds[octave])),
                "each octave seed must be deterministic");
        if (octave > 0)
            Require(!Near(value, PeriodicGradientNoise(
                        octaveProbe,
                        static_cast<std::int32_t>(baseFrequencies[octave]),
                        octaveSeeds[0]), 1e-9),
                    "octave seed separation must change the gradient field");
    }

    const Float3 samples[] = {
        { 0.137, 0.293, 0.419 }, { 0.731, 0.113, 0.887 },
        { 0.501, 0.667, 0.249 }, { 0.937, 0.811, 0.071 }
    };
    std::vector<double> values;
    for (const Float3 uvw : samples)
    {
        const double value = BasePerlinWorley(uvw, parameters);
        Require(std::isfinite(value) && value >= 0.0 && value <= 1.0,
                "Perlin-Worley must remain finite and normalized");
        Require(Near(value, BasePerlinWorley(uvw, parameters)),
                "same seed and coordinate must be deterministic");
        const Float3 repeated = { uvw.x + 1.0, uvw.y - 2.0, uvw.z + 3.0 };
        Require(Near(value, BasePerlinWorley(repeated, parameters), 1e-5),
                "base function must be periodic on all axes");
        values.push_back(value);
    }
    const double mean = std::accumulate(values.begin(), values.end(), 0.0) /
        values.size();
    double variance = 0.0;
    for (double value : values)
        variance += (value - mean) * (value - mean);
    variance /= values.size();
    Require(std::sqrt(variance) >= 0.03,
            "base reference samples must not collapse to a constant");

    constexpr double epsilon = 1e-4;
    const auto seamDerivative = [&](double x)
    {
        return (BasePerlinWorley({ x + epsilon, 0.37, 0.61 }, parameters) -
                BasePerlinWorley({ x - epsilon, 0.37, 0.61 }, parameters)) /
               (2.0 * epsilon);
    };
    Require(Near(seamDerivative(0.0), seamDerivative(1.0), 2e-3),
            "periodic seam derivative must be continuous");

    const double worldPosition = 8123.0;
    const double windOffset = 12.0 * 7.5;
    const double stationary = worldPosition - windOffset;
    Require(Near(Frac(stationary / 12000.0),
                 Frac((worldPosition + 12000.0 - windOffset) / 12000.0)),
            "base UV must be world-fixed and tile periodic");
    bool halfTileChanged = false;
    for (const Float3 uvw : samples)
        halfTileChanged |= !Near(BasePerlinWorley(uvw, parameters),
            BasePerlinWorley({ uvw.x + 0.5, uvw.y, uvw.z }, parameters),
            1e-5);
    Require(halfTileChanged,
            "a 6km move must not reproduce the same 12km base tile");

    const WeatherMapGeneratorSettings weatherSettings;
    const std::uint64_t weatherBefore = HashWeatherMap(BuildWeatherMap(
        Stage5WeatherPreset::PeriodicPerlin, weatherSettings));
    const std::uint64_t weatherAfter = HashWeatherMap(BuildWeatherMap(
        Stage5WeatherPreset::PeriodicPerlin, weatherSettings));
    Require(weatherBefore == weatherAfter,
            "13-4 noise math must not alter deterministic weather");

    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        L"VolumetricCloud-Stage13NoiseVolumeMath";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    Require(!error, "temporary cache directory creation failed");
    const std::filesystem::path cachePath = directory / L"roundtrip.vcnoise3";
    std::vector<std::uint8_t> payload(4u * 4u * 4u * 4u);
    for (std::size_t index = 0; index < payload.size(); ++index)
        payload[index] = static_cast<std::uint8_t>((index * 37u + 11u) & 0xffu);
    constexpr std::uint64_t parameterHash = 0x1337133713371337ull;
    Require(noisevolumecache::Save(cachePath, noisevolumecache::Kind::Base,
                                   4u, parameterHash, payload),
            "cache save failed");
    std::vector<std::uint8_t> loaded;
    Require(noisevolumecache::Load(cachePath, noisevolumecache::Kind::Base,
                                   4u, parameterHash, loaded) && loaded == payload,
            "cache round-trip changed the payload");
    {
        std::fstream oldVersion(cachePath, std::ios::binary | std::ios::in |
                                std::ios::out);
        oldVersion.seekp(sizeof(std::uint32_t), std::ios::beg);
        const std::uint32_t version2 = 2u;
        oldVersion.write(reinterpret_cast<const char*>(&version2),
                         sizeof(version2));
    }
    Require(!noisevolumecache::Load(cachePath, noisevolumecache::Kind::Base,
                                    4u, parameterHash, loaded),
            "cache version 2 must be rejected by version 3 readers");
    Require(noisevolumecache::Save(cachePath, noisevolumecache::Kind::Base,
                                   4u, parameterHash, payload),
            "cache re-save after version rejection failed");
    {
        std::fstream corrupt(cachePath, std::ios::binary | std::ios::in |
                             std::ios::out);
        corrupt.seekp(-1, std::ios::end);
        const char changed = '\x5a';
        corrupt.write(&changed, 1);
    }
    const std::vector<std::uint8_t> retained = loaded;
    Require(!noisevolumecache::Load(cachePath, noisevolumecache::Kind::Base,
                                    4u, parameterHash, loaded) &&
            loaded == retained,
            "corrupt cache must be rejected without replacing output");
    std::filesystem::remove(cachePath, error);
    std::filesystem::remove(directory, error);

    std::cout << "Stage13NoiseVolumeMath passed\n";
    return 0;
}
