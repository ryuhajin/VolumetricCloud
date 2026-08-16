#include "Stage6LightMath.h"
#include "LightParameters.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
bool Near(float a, float b, float tolerance = 1e-5f)
{
    return std::abs(a - b) <= tolerance;
}

void Require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "Stage6LightMath failure: " << message << '\n';
        std::exit(1);
    }
}
}

int main()
{
    using namespace stage6;

    const auto uniform = MarchConstantDensity(4.0f, 0.35f, 1.0f, 0.25f, 32u);
    Require(Near(uniform.transmittance, std::exp(-1.4f), 1e-5f),
            "uniform Beer-Lambert must match analytic result");
    Require(uniform.stepCount == 16u, "target step must cover the entire path");

    const auto thin = MarchConstantDensity(1.0f, 0.5f, 1.0f, 0.25f, 16u);
    const auto thick = MarchConstantDensity(5.0f, 0.5f, 1.0f, 0.25f, 16u);
    Require(thick.transmittance < thin.transmittance,
            "a thicker sun path must transmit less light");

    const auto empty = MarchConstantDensity(5.0f, 0.0f, 1.0f, 0.25f, 16u);
    Require(Near(empty.transmittance, 1.0f), "empty density must be transparent");
    const auto noExtinction = MarchConstantDensity(
        5.0f, 0.5f, 0.0f, 0.25f, 16u);
    Require(Near(noExtinction.transmittance, 1.0f),
            "zero extinction must preserve full transmittance");
    Require(!ShouldTraceLightRay(0.0f) && ShouldTraceLightRay(0.01f),
            "empty view samples must skip the light ray entirely");
    Require(Near(SelectLightRayDensity(0.4f, 0.1f), 0.4f),
            "light ray must select base density instead of detailed final density");

    const auto steps8 = MarchConstantDensity(3.7f, 0.42f, 0.9f, 0.05f, 8u);
    const auto steps16 = MarchConstantDensity(3.7f, 0.42f, 0.9f, 0.05f, 16u);
    const auto steps32 = MarchConstantDensity(3.7f, 0.42f, 0.9f, 0.05f, 32u);
    Require(Near(steps8.transmittance, steps16.transmittance, 1e-5f) &&
            Near(steps16.transmittance, steps32.transmittance, 1e-5f),
            "constant density must converge for 8/16/32 light steps");

    const auto earlyExit = MarchConstantDensity(
        20000.0f, 2.0f, 0.001f, 250.0f, 80u, 1.0e-4f);
    Require(earlyExit.stepCount < 80u && earlyExit.transmittance > 0.0f &&
            earlyExit.transmittance <= 1.0e-4f,
            "opaque Light rays must stop after reaching the fixed cutoff");
    const auto sparseFull = MarchConstantDensity(
        20000.0f, 0.01f, 0.0001f, 250.0f, 80u, 1.0e-4f);
    Require(sparseFull.stepCount == 80u &&
            Near(sparseFull.transmittance, std::exp(-0.02f), 1e-5f),
            "sparse Light rays must preserve the full Beer-Lambert integral");

    const float scattering = IntegrateSingleScattering(
        0.5f, 0.6f, 0.8f, 0.2f, 1.0f, 2.0f, 0.75f);
    Require(scattering > 0.0f && std::isfinite(scattering),
            "single scattering must be positive and finite");
    Require(Near(IntegrateSingleScattering(
        0.5f, 0.6f, 0.8f, 0.2f, 1.0f, 4.0f, 0.75f), scattering * 2.0f),
        "sun intensity must scale scattering linearly");
    Require(Near(IntegrateSingleScattering(
        0.5f, 0.6f, 0.4f, 0.2f, 1.0f, 2.0f, 0.75f), scattering * 0.5f),
        "view transmittance must scale scattering linearly");
    const float albedoOne = IntegrateSingleScattering(
        0.5f, 0.6f, 0.8f, 0.2f, 1.0f, 2.0f, 1.0f);
    const float albedoHalf = IntegrateSingleScattering(
        0.5f, 0.6f, 0.8f, 0.2f, 1.0f, 2.0f, 0.5f);
    Require(Near(albedoHalf, albedoOne * 0.5f),
            "single-scattering albedo must scale scattering linearly");
    Require(Near(IntegrateSingleScattering(
        0.5f, 0.6f, 0.8f, 0.2f, 1.0f, 2.0f, 0.0f), 0.0f),
        "zero albedo must produce no scattering");
    Require(Near(IntegrateSingleScattering(
        0.5f, 0.6f, 0.8f, 0.2f, 0.0f, 2.0f, 1.0f), 0.0f),
        "zero extinction must preserve transmittance and produce no scattering");

    const auto invalid = MarchConstantDensity(
        std::numeric_limits<float>::quiet_NaN(), 1.0f, 1.0f, 0.0f, 0u);
    Require(std::isfinite(invalid.transmittance) &&
            std::isfinite(invalid.opticalDepth),
            "invalid inputs must retain finite neutral output");

    LightParameters bad;
    bad.directionToSun = { 0.0f, 0.0f, 0.0f };
    bad.sunIntensity = -1.0f;
    bad.maxLightSteps = 0;
    bad.lightStepSize = -1.0f;
    bad.singleScatteringAlbedo = 2.0f;
    const LightParameters safe = stage6light::Sanitize(bad);
    const float directionLength = std::sqrt(
        safe.directionToSun.x * safe.directionToSun.x +
        safe.directionToSun.y * safe.directionToSun.y +
        safe.directionToSun.z * safe.directionToSun.z);
    Require(Near(directionLength, 1.0f, 1e-4f), "sun direction must be normalized");
    Require(safe.sunIntensity == 0.0f && safe.maxLightSteps >= 1u &&
            safe.lightStepSize > 0.0f, "invalid CPU light settings must be clamped");
    Require(safe.singleScatteringAlbedo == 1.0f,
            "single-scattering albedo above one must clamp to one");
    bad.singleScatteringAlbedo = -0.5f;
    Require(stage6light::Sanitize(bad).singleScatteringAlbedo == 0.0f,
            "negative single-scattering albedo must clamp to zero");
    bad.singleScatteringAlbedo = std::numeric_limits<float>::quiet_NaN();
    Require(stage6light::Sanitize(bad).singleScatteringAlbedo == 1.0f,
            "NaN single-scattering albedo must reset to the default");

    const auto east = stage6light::Preset(Stage6SunPreset::LowEast).directionToSun;
    const auto west = stage6light::Preset(Stage6SunPreset::LowWest).directionToSun;
    Require(east.x * west.x + east.z * west.z < 0.0f,
            "east and west presets must reverse the horizontal sun direction");

    std::cout << "Stage6LightMath passed\n";
    return 0;
}
