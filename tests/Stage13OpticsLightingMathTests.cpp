#include "CloudLodParameters.h"
#include "LightParameters.h"
#include "Stage13OpticsLightingMath.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "Stage13OpticsLightingMath failure: " << message << '\n';
        std::exit(1);
    }
}

bool Near(double a, double b, double epsilon = 1e-9)
{
    return std::abs(a - b) <= epsilon;
}
}

int main()
{
    using namespace stage13optics;

    Require(sizeof(CloudLodParameters) == 16u,
            "CloudLodCB CPU layout must remain 16 bytes");
    Require(RequiredSteps(kOpenWorldLightTraceMeters,
                          kReferenceLightStepMeters,
                          kReferenceLightSteps) == kReferenceLightSteps &&
            RequiredSteps(kOpenWorldLightTraceMeters,
                          kPreviousQualityLightStepMeters,
                          kPreviousQualityLightSteps) == kPreviousQualityLightSteps &&
            RequiredSteps(kOpenWorldLightTraceMeters,
                          kDefaultLightStepMeters,
                          kDefaultLightSteps) == kDefaultLightSteps,
            "62.5/125/250m candidates must cover the 20km Light trace");
    Require(Near(SamplesPerWavelength(kSmallestBaseWavelengthMeters,
                                      kReferenceLightStepMeters),
                 8.34782608695652, 1e-12) &&
            Near(SamplesPerWavelength(kSmallestBaseWavelengthMeters,
                                      kPreviousQualityLightStepMeters),
                 4.17391304347826, 1e-12) &&
            Near(SamplesPerWavelength(kSmallestBaseWavelengthMeters,
                                      kDefaultLightStepMeters),
                 2.08695652173913, 1e-12),
            "Light candidates must expose the shortest Base wavelength budget");
    Require(Near(kLightEarlyExitTransmittance, 1.0e-4) &&
            Near(kLightEarlyExitOpticalDepth,
                 -std::log(kLightEarlyExitTransmittance), 1.0e-12),
            "Light early exit must use the fixed 0.01 percent threshold");
    Require(Near(SamplesPerWavelength(kSmallestDetailWavelengthMeters, 100.0),
                 4.0),
            "100m View steps must retain four samples per shortest Detail wave");
    Require(Near(DisplayShoulderPeak(0.5), 0.5) &&
                DisplayShoulderPeak(1.0) < 1.0 &&
                DisplayShoulderPeak(2.0) > DisplayShoulderPeak(1.0) &&
                DisplayShoulderPeak(100.0) < 1.0,
            "LDR display shoulder must preserve midtones and compress HDR peaks");

    const double analytic = UniformOpticalDepth(0.4, 0.00025, 6000.0);
    Require(Near(analytic, 0.6),
            "km constant-density optical depth must use meter and 1/m");
    const std::vector<double> densities(48u, 0.4);
    const std::vector<double> lengths(48u, 125.0);
    Require(Near(IntegrateOpticalDepth(densities, lengths, 0.00025), analytic),
            "partitioned optical depth must match the analytic result");
    Require(Near(TransmittanceFromOpticalDepth(analytic), std::exp(-0.6)),
            "transmittance must match Beer-Lambert");

    for (double scale : { 1.0, 10.0, 100.0, 1000.0 })
    {
        const double scaled = UniformOpticalDepth(
            0.4, 0.00025 / scale, 6000.0 * scale);
        Require(Near(scaled, analytic, 1e-12),
                "length times S and extinction divided by S must preserve tau");
    }

    CloudLodParameters lod;
    Require(Near(DetailLodFactor(0.0, lod), 1.0) &&
            Near(DetailLodFactor(32000.0, lod), 1.0) &&
            Near(DetailLodFactor(40000.0, lod), 0.5) &&
            Near(DetailLodFactor(48000.0, lod), 0.0),
            "Detail LOD must be continuous from 32km to 48km");
    Require(Near(FilterDetail(0.8, 1.0, 0.6), 0.8) &&
            Near(FilterDetail(0.8, 0.5, 0.6), 0.7) &&
            Near(FilterDetail(0.8, 0.0, 0.6), 0.6),
            "Detail filtering must converge to the measured neutral mean");
    Require(ShouldSampleDetail(47999.0, 0.5, 0.25, lod) &&
            !ShouldSampleDetail(48000.0, 0.5, 0.25, lod) &&
            !ShouldSampleDetail(1000.0, 0.0, 0.25, lod),
            "Detail texture fetch must stop at LOD end and empty Base");

    const std::vector<std::uint8_t> canonical = {
        0u, 0u, 0u, 0u,
        255u, 128u, 64u, 32u,
    };
    const std::array<double, 4> weights = { 0.50, 0.30, 0.15, 0.05 };
    const double expectedMean = 0.5 *
        (0.50 + (128.0 / 255.0) * 0.30 +
         (64.0 / 255.0) * 0.15 + (32.0 / 255.0) * 0.05);
    Require(Near(WeightedDetailMean(canonical, weights), expectedMean, 1e-12),
            "Detail neutral value must use actual RGBA bytes and channel weights");

    CloudLodParameters invalid;
    invalid.detailLodStartMeters = std::numeric_limits<float>::quiet_NaN();
    invalid.detailLodEndMeters = -1.0f;
    invalid.detailNeutralValue = std::numeric_limits<float>::infinity();
    invalid = stage13lod::Sanitize(invalid);
    Require(std::isfinite(invalid.detailLodStartMeters) &&
            invalid.detailLodEndMeters > invalid.detailLodStartMeters &&
            std::isfinite(invalid.detailNeutralValue),
            "invalid LOD settings must sanitize to finite ordered values");

    LightParameters light;
    light.lightRayBias = 10.0f;
    Require(Near(stage6light::Sanitize(light).lightRayBias, 10.0),
            "CPU Light sanitize must preserve a 10m similarity bias");

    std::cout << "Stage13OpticsLightingMath passed\n";
    return 0;
}
