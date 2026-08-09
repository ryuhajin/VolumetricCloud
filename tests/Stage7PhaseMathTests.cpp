#include "LightParameters.h"
#include "Stage6LightMath.h"
#include "Stage7PhaseMath.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "Stage7PhaseMath failure: " << message << '\n';
        std::exit(1);
    }
}

bool NearlyEqual(float a, float b, float epsilon = 1e-5f)
{
    return std::abs(a - b) <= epsilon;
}

bool Finite(const stage7::PhaseSample& sample)
{
    return std::isfinite(sample.cosTheta) &&
           std::isfinite(sample.forwardLobe) &&
           std::isfinite(sample.backwardLobe) &&
           std::isfinite(sample.dualLobe) &&
           std::isfinite(sample.phaseFactor);
}
}

int main()
{
    for (float cosTheta : { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f })
        Require(NearlyEqual(stage7::HenyeyGreenstein(cosTheta, 0.0f), 1.0f),
                "g=0 must be isotropic relative value 1");

    Require(stage7::HenyeyGreenstein(1.0f, 0.65f) >
                stage7::HenyeyGreenstein(-1.0f, 0.65f),
            "positive g must peak toward the sun");
    Require(stage7::HenyeyGreenstein(-1.0f, -0.55f) >
                stage7::HenyeyGreenstein(1.0f, -0.55f),
            "negative g must peak opposite the sun");

    const stage7::Direction3 x = { 1.0f, 0.0f, 0.0f };
    const stage7::Direction3 minusX = { -1.0f, 0.0f, 0.0f };
    const stage7::Direction3 y = { 0.0f, 1.0f, 0.0f };
    const auto aligned = stage7::EvaluateDualLobePhase(
        x, x, true, 0.65f, -0.25f, 0.8f, 0.25f);
    const auto opposite = stage7::EvaluateDualLobePhase(
        x, minusX, true, 0.65f, -0.25f, 0.8f, 0.25f);
    const auto perpendicular = stage7::EvaluateDualLobePhase(
        x, y, true, 0.65f, -0.25f, 0.8f, 0.25f);
    Require(NearlyEqual(aligned.cosTheta, 1.0f) &&
                NearlyEqual(opposite.cosTheta, -1.0f) &&
                NearlyEqual(perpendicular.cosTheta, 0.0f),
            "direction convention must map aligned/opposite/perpendicular to 1/-1/0");

    const auto backwardOnly = stage7::EvaluateDualLobePhase(
        x, x, true, 0.65f, -0.25f, 0.0f, 1.0f);
    const auto forwardOnly = stage7::EvaluateDualLobePhase(
        x, x, true, 0.65f, -0.25f, 1.0f, 1.0f);
    Require(NearlyEqual(backwardOnly.dualLobe, backwardOnly.backwardLobe) &&
                NearlyEqual(forwardOnly.dualLobe, forwardOnly.forwardLobe),
            "blend endpoints must select exact backward/forward lobes");

    const auto disabled = stage7::EvaluateDualLobePhase(
        x, x, false, 0.8f, -0.5f, 0.9f, 1.0f);
    const auto zeroIntensity = stage7::EvaluateDualLobePhase(
        x, x, true, 0.8f, -0.5f, 0.9f, 0.0f);
    Require(disabled.phaseFactor == 1.0f && zeroIntensity.phaseFactor == 1.0f,
            "disabled phase and zero intensity must preserve stage 6");

    const auto lightBefore = stage6::MarchConstantDensity(
        3.0f, 0.4f, 1.0f, 0.25f, 16u);
    const auto lightAfter = stage6::MarchConstantDensity(
        3.0f, 0.4f, 1.0f, 0.25f, 16u);
    const float stage6Scattering = stage6::IntegrateSingleScattering(
        0.4f, lightBefore.transmittance, 0.75f, 0.1f, 1.0f, 1.0f, 1.0f);
    const float phasedScattering = stage6Scattering * aligned.phaseFactor;
    Require(NearlyEqual(lightBefore.transmittance, lightAfter.transmittance) &&
                NearlyEqual(lightBefore.opticalDepth, lightAfter.opticalDepth) &&
                NearlyEqual(phasedScattering,
                            stage6Scattering * aligned.phaseFactor),
            "phase must scale scattering without changing light march");

    LightParameters parameters;
    stage6light::ApplyPhasePreset(parameters, Stage7PhasePreset::Off);
    Require(parameters.phaseEnabled == 0.0f,
            "Off preset must be the default isotropic path");
    stage6light::ApplyPhasePreset(parameters, Stage7PhasePreset::Balanced);
    Require(parameters.phaseEnabled == 1.0f &&
                NearlyEqual(parameters.forwardScatteringG, 0.65f) &&
                NearlyEqual(parameters.backwardScatteringG, -0.25f),
            "Balanced preset must be deterministic");
    stage6light::ApplyPhasePreset(parameters, Stage7PhasePreset::SilverLining);
    Require(NearlyEqual(parameters.forwardScatteringG, 0.80f) &&
                NearlyEqual(parameters.phaseBlend, 0.90f),
            "Silver Lining preset must be deterministic");
    stage6light::ApplyPhasePreset(parameters, Stage7PhasePreset::BackscatterCheck);
    Require(NearlyEqual(parameters.backwardScatteringG, -0.55f) &&
                NearlyEqual(parameters.phaseBlend, 0.30f),
            "Backscatter preset must be deterministic");

    parameters.phaseEnabled = std::numeric_limits<float>::quiet_NaN();
    parameters.forwardScatteringG = 100.0f;
    parameters.backwardScatteringG = -100.0f;
    parameters.phaseBlend = std::numeric_limits<float>::infinity();
    parameters.phaseIntensity = -100.0f;
    parameters = stage6light::Sanitize(parameters);
    Require(parameters.phaseEnabled == 0.0f &&
                parameters.forwardScatteringG == 0.95f &&
                parameters.backwardScatteringG == -0.95f &&
                parameters.phaseBlend == 0.80f &&
                parameters.phaseIntensity == 0.0f,
            "CPU phase parameters must sanitize into documented ranges");

    const auto invalidDirections = stage7::EvaluateDualLobePhase(
        {}, x, true,
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity());
    const auto extreme = stage7::EvaluateDualLobePhase(
        x, x, true, 10.0f, -10.0f, 10.0f, 10.0f);
    Require(Finite(invalidDirections) && invalidDirections.phaseFactor == 1.0f &&
                Finite(extreme) && extreme.phaseFactor >= 0.0f &&
                extreme.phaseFactor <= stage7::kMaxPhaseFactor,
            "invalid and extreme phase inputs must remain finite and bounded");

    std::cout << "Stage7PhaseMath passed\n";
    return 0;
}
