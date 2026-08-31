#include "Stage13OpenWorldMath.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "Stage13OpenWorldMath failure: " << message << '\n';
        std::exit(1);
    }
}

bool Near(double a, double b, double epsilon = 1e-6)
{
    return std::abs(a - b) <= epsilon;
}
}

int main()
{
    using namespace stage13openworld;
    const Parameters value;

    Require(Near(value.previewHalfSizeMeters, 32000.0),
            "Noise Lab preview must span -32 km to +32 km");
    Require(Near(value.layerBottomMeters, 1500.0) &&
            Near(value.layerThicknessMeters, 6000.0) &&
            Near(value.layerBottomMeters + value.layerThicknessMeters, 7500.0),
            "cloud layer must span 1.5 km to 7.5 km");
    Require(Near(value.maxViewTraceMeters, 50000.0) &&
            Near(value.viewFadeStartMeters, 40000.0) &&
            Near(value.maxLightTraceMeters, 20000.0),
            "view, fade, and light distances must match the km contract");
    Require(Near(value.viewStepMeters, 100.0) && value.maxViewSteps == 512u &&
            Near(value.lightStepMeters, 250.0) && value.maxLightSteps == 80u &&
            Near(value.lightRayBiasMeters, 1.0),
            "view and light sampling budgets must match the 13-3 contract");
    Require(Near(value.baseNoiseCyclesPerMeter, 0.00035) &&
            Near(value.detailNoiseCyclesPerMeter, 0.0025),
            "base and detail frequencies must use cycles per meter");
    Require(Near(value.weatherWorldSizeMeters, 64000.0) &&
            value.weatherResolution == 256u,
            "weather must cover 64 km at 256 squared texels");
    Require(Near(value.densityMultiplier, 1.0) &&
            Near(value.extinctionPerMeter, 0.00025) &&
            Near(value.singleScatteringAlbedo, 1.0),
            "density, extinction, and albedo must match the optical contract");
    Require(Near(value.baseWindMetersPerSecond, 12.0) &&
            Near(value.detailWindMetersPerSecond, 18.0) &&
            Near(value.weatherWindMetersPerSecond, 8.0),
            "all wind speeds must use meters per second");
    Require(Near(value.coverage, 0.55) &&
            Near(value.bottomFadeEnd, 0.20) &&
            Near(value.topFadeStart, 0.80) &&
            Near(value.detailErosionStrength, 0.25),
            "coverage, height fades, and erosion must match the start values");
    Require(SimilarityPreset(1.0) == Stage13ScenePreset::Similarity1x &&
            SimilarityPreset(10.0) == Stage13ScenePreset::Similarity10x &&
            SimilarityPreset(100.0) == Stage13ScenePreset::Similarity100x &&
            SimilarityPreset(1000.0) == Stage13ScenePreset::Similarity1000x &&
            std::string(PresetName(Stage13ScenePreset::OpenWorld)) == "OpenWorld",
            "scene preset identities must remain stable");
    Require(std::string(PipelinePresetName(
                OpenWorldPipelinePreset::Legacy1000Baseline)) ==
                "Legacy1000Baseline" &&
            std::string(PipelinePresetName(
                OpenWorldPipelinePreset::FullOpenWorld)) ==
                "FullOpenWorld",
            "pipeline comparison preset identities must remain stable");
    Require(Near(SamplesPerWavelength(
                     value.baseNoiseCyclesPerMeter, value.viewStepMeters),
                 28.5714285714, 1e-5) &&
            Near(SamplesPerWavelength(
                     value.detailNoiseCyclesPerMeter, value.viewStepMeters), 4.0),
            "base/detail samples per wavelength must match the 13-3 contract");
    Require(Near(VerticalOpticalDepth(value), 1.5),
            "representative vertical optical depth must be 1.5");
    Require(RequiredSteps(value.maxViewTraceMeters, value.viewStepMeters) == 500u &&
            RequiredSteps(value.maxViewTraceMeters, value.viewStepMeters) <=
                value.maxViewSteps,
            "50 km view trace must fit in the 512-step budget");
    Require(RequiredSteps(value.maxLightTraceMeters, value.lightStepMeters) == 80u &&
            RequiredSteps(value.maxLightTraceMeters, value.lightStepMeters) <=
                value.maxLightSteps,
            "20 km light trace must fit exactly in the 80-step default budget");
    Require(Near(WeatherTexelMeters(value), 250.0),
            "64 km / 256 weather map must equal 250 m per texel");
    Require(Near(ViewDistanceFade(40000.0, value), 1.0) &&
            Near(ViewDistanceFade(45000.0, value), 0.5) &&
            Near(ViewDistanceFade(50000.0, value), 0.0),
            "view fade must transition smoothly from 40 km to 50 km");

    std::cout << "Stage13OpenWorldMath passed\n";
    return 0;
}
