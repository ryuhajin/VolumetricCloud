#include "Stage13NoiseVolumeMath.h"
#include "Stage13OpenWorldMath.h"
#include "Stage13WeatherShapeMath.h"
#include "CloudShapeParameters.h"
#include "WeatherMap.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <vector>

namespace
{
[[noreturn]] void Fail(const char* message)
{
    std::fprintf(stderr, "Stage13WeatherShapeMath failure: %s\n", message);
    std::exit(1);
}

double Percentile(std::vector<double> values, double percentile)
{
    if (values.empty())
        return 0.0;
    std::sort(values.begin(), values.end());
    const std::size_t index = static_cast<std::size_t>(std::clamp(
        percentile, 0.0, 1.0) * static_cast<double>(values.size() - 1));
    return values[index];
}

double StandardDeviation(const std::vector<double>& values)
{
    if (values.empty())
        return 0.0;
    const double mean = std::accumulate(values.begin(), values.end(), 0.0) /
        static_cast<double>(values.size());
    double sum = 0.0;
    for (double value : values)
        sum += (value - mean) * (value - mean);
    return std::sqrt(sum / static_cast<double>(values.size()));
}

struct ProfileStats
{
    double centroid = 0.0;
    double upperMass = 0.0;
};

ProfileStats MeasureProfile(double type)
{
    double mass = 0.0;
    double moment = 0.0;
    double upper = 0.0;
    for (int index = 0; index <= 1000; ++index)
    {
        const double height = static_cast<double>(index) / 1000.0;
        const double density = stage13shape::EvaluateTypedShapeProfile(height, type);
        mass += density;
        moment += density * height;
        if (height >= 0.65)
            upper += density;
    }
    return { moment / std::max(mass, 1e-12), upper / std::max(mass, 1e-12) };
}

double MeasureSupportRatio(double type, double height)
{
    std::size_t supported = 0;
    constexpr std::size_t sampleCount = 2001;
    for (std::size_t index = 0; index < sampleCount; ++index)
    {
        const double x = static_cast<double>(index) /
            static_cast<double>(sampleCount - 1) * 2.0 - 1.0;
        const double radialCoverage = std::max(1.0 - std::abs(x), 0.0);
        const double shape = stage13shape::EvaluatePhysicalBaseShape(
            0.88, 0.75, radialCoverage, height, type);
        if (shape > 0.0)
            ++supported;
    }
    return static_cast<double>(supported) / static_cast<double>(sampleCount);
}
}

int main()
{
    const auto zeroDirection = stage13shape::EvaluatePhysicalCloudAdvectionOffset(
        0.0, 0.0, 100.0, 10.0);
    const auto zeroSpeed = stage13shape::EvaluatePhysicalCloudAdvectionOffset(
        1.0, 0.0, 0.0, 10.0);
    const auto zeroTime = stage13shape::EvaluatePhysicalCloudAdvectionOffset(
        1.0, 0.0, 100.0, 0.0);
    const auto invalidWind = stage13shape::EvaluatePhysicalCloudAdvectionOffset(
        NAN, INFINITY, INFINITY, NAN);
    if (zeroDirection.x != 0.0 || zeroDirection.z != 0.0 ||
        zeroSpeed.x != 0.0 || zeroSpeed.z != 0.0 ||
        zeroTime.x != 0.0 || zeroTime.z != 0.0 ||
        invalidWind.x != 0.0 || invalidWind.z != 0.0)
        Fail("invalid, stopped, or zero-direction bulk wind must not move");

    constexpr double directionX = 4.0;
    constexpr double directionZ = 3.0;
    constexpr double speed = 12.0;
    constexpr double startTime = 7.0;
    constexpr double deltaTime = 25.0;
    const auto movement = stage13shape::EvaluatePhysicalCloudAdvectionOffset(
        directionX, directionZ, speed, deltaTime);
    const auto beforeMove = stage13shape::EvaluatePhysicalCloudSamplePosition(
        1200.0, 3500.0, -800.0, directionX, directionZ, speed, startTime);
    const auto afterMove = stage13shape::EvaluatePhysicalCloudSamplePosition(
        1200.0 + movement.x, 3500.0, -800.0 + movement.z,
        directionX, directionZ, speed, startTime + deltaTime);
    if (std::abs(beforeMove.x - afterMove.x) > 1e-9 ||
        std::abs(beforeMove.y - afterMove.y) > 1e-9 ||
        std::abs(beforeMove.z - afterMove.z) > 1e-9)
        Fail("physical Weather/Base/Detail coordinates must share one rigid offset");
    if (afterMove.y != 3500.0)
        Fail("physical bulk advection must not move the fixed vertical coordinate");

    const double runningTime = stage13shape::AdvanceEffectiveTime(
        3.0, 0.1, 2.0, false);
    const double pausedTime = stage13shape::AdvanceEffectiveTime(
        runningTime, 0.1, 2.0, true);
    const double resumedTime = stage13shape::AdvanceEffectiveTime(
        pausedTime, 0.1, 2.0, false);
    if (std::abs(runningTime - 3.2) > 1e-9 || pausedTime != runningTime ||
        std::abs(resumedTime - 3.4) > 1e-9 ||
        stage13shape::AdvanceEffectiveTime(NAN, INFINITY, NAN, false) != 0.0)
        Fail("effective time must pause exactly and resume continuously");

    CloudShapeParameters invalidShape;
    invalidShape.shapeMode = 99u;
    invalidShape.stratusMinimumThicknessMeters = NAN;
    invalidShape.stratusMaximumThicknessMeters = -100.0f;
    invalidShape.cumulusMinimumThicknessMeters = INFINITY;
    invalidShape.cumulusMaximumThicknessMeters = 100000.0f;
    const CloudShapeParameters sanitizedShape =
        SanitizeCloudShapeParameters(invalidShape);
    if (sanitizedShape.shapeMode != static_cast<std::uint32_t>(
            CloudShapeMode::LegacyNormalizedLayer) ||
        sanitizedShape.stratusMinimumThicknessMeters < 1.0f ||
        sanitizedShape.stratusMaximumThicknessMeters <
            sanitizedShape.stratusMinimumThicknessMeters ||
        sanitizedShape.cumulusMaximumThicknessMeters > 6000.0f)
        Fail("CloudShapeCB sanitize must preserve ordered finite bounds");

    const stage13openworld::Parameters openWorld;
    if (std::abs(openWorld.layerBottomMeters - 1500.0) > 1e-6 ||
        std::abs(openWorld.layerThicknessMeters - 6000.0) > 1e-6)
        Fail("Open World domain must be 1500m..7500m");

    const WeatherMapData weather = BuildWeatherMap(Stage5WeatherPreset::PeriodicPerlin);
    std::vector<double> potentials;
    std::vector<double> thicknesses;
    std::size_t ceilingCount = 0;
    for (std::size_t pixel = 0; pixel < weather.rgba.size() / 4; ++pixel)
    {
        const double coverage = weather.rgba[pixel * 4 + 0] / 255.0;
        if (coverage <= 1.0 / 255.0)
            continue;
        const double type = weather.rgba[pixel * 4 + 1] / 255.0;
        const double potential = weather.rgba[pixel * 4 + 3] / 255.0;
        const double thickness = stage13shape::EvaluateLocalThicknessMeters(
            potential, type);
        potentials.push_back(potential);
        thicknesses.push_back(thickness);
        if (thickness >= 5900.0)
            ++ceilingCount;
        if (thickness < 1500.0 || thickness > 6000.0 || !std::isfinite(thickness))
            Fail("Dense Mixed local thickness must stay in 1.5km..6km");
    }
    const double potentialSpan = Percentile(potentials, 0.95) -
        Percentile(potentials, 0.05);
    const double thicknessSpan = Percentile(thicknesses, 0.95) -
        Percentile(thicknesses, 0.05);
    const double topStddev = StandardDeviation(thicknesses);
    const double ceilingRatio = static_cast<double>(ceilingCount) /
        std::max<std::size_t>(thicknesses.size(), 1u);
    const bool distributionPassed = potentialSpan >= 0.30 &&
        thicknessSpan >= 1750.0 && thicknessSpan <= 1900.0 &&
        topStddev >= 500.0 && ceilingRatio <= 0.05;
    std::printf("[SHAPE][WEATHER] A_P05=%.3f A_P95=%.3f THICKNESS_SPAN_M=%.1f TOP_STDDEV_M=%.1f CEILING_RATIO=%.5f %s\n",
                Percentile(potentials, 0.05), Percentile(potentials, 0.95),
                thicknessSpan, topStddev, ceilingRatio,
                distributionPassed ? "PASS" : "FAIL");
    if (!distributionPassed)
        Fail("Dense Mixed Weather thickness distribution left its deterministic range");

    for (int potentialIndex = 0; potentialIndex <= 10; ++potentialIndex)
    {
        const double potential = potentialIndex / 10.0;
        double previousMinimum = 0.0;
        double previousMaximum = 0.0;
        for (int typeIndex = 0; typeIndex <= 10; ++typeIndex)
        {
            const auto range = stage13shape::EvaluateThicknessRange(typeIndex / 10.0);
            if (range.minimumMeters < previousMinimum ||
                range.maximumMeters < previousMaximum)
                Fail("type increase must not reduce thickness bounds");
            const double thickness = stage13shape::EvaluateLocalThicknessMeters(
                potential, typeIndex / 10.0);
            if (thickness < range.minimumMeters || thickness > range.maximumMeters)
                Fail("thickness potential must remain inside the typed range");
            previousMinimum = range.minimumMeters;
            previousMaximum = range.maximumMeters;
        }
    }

    const ProfileStats stratus = MeasureProfile(0.0);
    const ProfileStats mixed = MeasureProfile(0.5);
    const ProfileStats cumulus = MeasureProfile(1.0);
    std::printf("[SHAPE][TYPE] STRATUS_CENTROID=%.4f MIXED_CENTROID=%.4f CUMULUS_CENTROID=%.4f PASS\n",
                stratus.centroid, mixed.centroid, cumulus.centroid);
    if (!(stratus.centroid < mixed.centroid && mixed.centroid < cumulus.centroid) ||
        !(stratus.upperMass < mixed.upperMass && mixed.upperMass < cumulus.upperMass))
        Fail("typed shape profiles must carry progressively more upper mass");

    if (stage13shape::EvaluatePhysicalBaseShape(1.0, 1.0, 1.0, 0.0, 1.0) != 0.0 ||
        stage13shape::EvaluateEffectiveShapeCoverage(1.0, 1.0, 0.5) >= 1.0)
        Fail("shape profile zero must remove support and saturated Weather R must still taper");
    double previousCoverage = 0.0;
    double previousShape = 0.0;
    for (int index = 0; index <= 10; ++index)
    {
        const double profile = index / 10.0;
        const double effective = stage13shape::EvaluateEffectiveShapeCoverage(
            0.75, 0.8, profile);
        const double shape = stage13shape::RemapCoverage(0.88, effective);
        if (effective + 1e-12 < previousCoverage || shape + 1e-12 < previousShape)
            Fail("larger shape profile must not reduce effective coverage or support");
        previousCoverage = effective;
        previousShape = shape;
    }

    const double typePeaks[3] = { 0.45, 0.50, 0.58 };
    for (int typeIndex = 0; typeIndex < 3; ++typeIndex)
    {
        const double type = typeIndex * 0.5;
        const double bottomDensity = stage13shape::EvaluatePhysicalBaseShape(
            0.95, 0.75, 0.80, 0.01, type);
        const double middleDensity = stage13shape::EvaluatePhysicalBaseShape(
            0.95, 0.75, 0.80, typePeaks[typeIndex], type);
        const double topDensity = stage13shape::EvaluatePhysicalBaseShape(
            0.95, 0.75, 0.80, 0.99, type);
        if (!(middleDensity > bottomDensity && middleDensity > topDensity))
            Fail("each cloud type must keep more density in its middle than at boundaries");

        int longestPlateau = 0;
        int currentPlateau = 0;
        double previous = stage13shape::EvaluateTypedShapeProfile(0.10, type);
        for (int heightIndex = 11; heightIndex <= 90; ++heightIndex)
        {
            const double height = heightIndex / 100.0;
            const double value = stage13shape::EvaluateTypedShapeProfile(height, type);
            currentPlateau = std::abs(value - previous) <= 1e-10
                ? currentPlateau + 1 : 0;
            longestPlateau = std::max(longestPlateau, currentPlateau);
            previous = value;
        }
        if (longestPlateau > 2)
            Fail("typed shape profile must not contain a long constant-width plateau");
    }
    if (!std::isfinite(stage13shape::EvaluateTypedShapeProfile(NAN, INFINITY)) ||
        !std::isfinite(stage13shape::EvaluatePhysicalBaseShape(
            INFINITY, NAN, INFINITY, NAN, INFINITY)))
        Fail("typed shape threshold math must remain finite for invalid inputs");

    const NoiseVolumeParameters noise;
    double minimumBaseSamples = 1e9;
    const std::uint32_t baseFrequencies[4] = {
        noise.baseFrequencies.x, noise.baseFrequencies.y,
        noise.baseFrequencies.z, noise.baseFrequencies.w
    };
    for (std::uint32_t frequency : baseFrequencies)
        minimumBaseSamples = std::min(minimumBaseSamples,
            stage13noise::SamplesPerWavelength(
                noise.baseVerticalWorldSizeMeters, frequency,
                openWorld.viewStepMeters));
    double minimumDetailSamples = 1e9;
    const std::uint32_t detailFrequencies[4] = {
        noise.detailFrequencies.x, noise.detailFrequencies.y,
        noise.detailFrequencies.z, noise.detailFrequencies.w
    };
    for (std::uint32_t frequency : detailFrequencies)
        minimumDetailSamples = std::min(minimumDetailSamples,
            stage13noise::SamplesPerWavelength(
                noise.detailWorldSizeMeters, frequency,
                openWorld.viewStepMeters));
    std::printf("[SHAPE][FREQUENCY][BASE] MIN_SAMPLES_PER_WAVELENGTH=%.3f PASS\n",
                minimumBaseSamples);
    std::printf("[SHAPE][FREQUENCY][DETAIL] MIN_SAMPLES_PER_WAVELENGTH=%.3f PASS\n",
                minimumDetailSamples);
    if (std::abs(noise.baseWorldSizeMeters - 12000.0f) > 1e-4f ||
        std::abs(noise.baseVerticalWorldSizeMeters - 12000.0f) > 1e-4f ||
        minimumBaseSamples < 5.2 || minimumDetailSamples < 4.0)
        Fail("3D noise world scale/frequencies exceed the 100m sampling budget");

    std::puts("Stage13WeatherShapeMath passed");
    return 0;
}
