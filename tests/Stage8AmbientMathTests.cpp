#include "EnvironmentParameters.h"
#include "LegacyEnvironmentParameters.h"
#include "Stage6LightMath.h"
#include "LegacyStage8AmbientMath.h"

#include <cstddef>
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
        std::cerr << "Stage8AmbientMath failure: " << message << '\n';
        std::exit(1);
    }
}

bool Near(float a, float b, float epsilon = 1e-5f)
{
    return std::abs(a - b) <= epsilon;
}
}

int main()
{
    Require(sizeof(EnvironmentParameters)==48 && offsetof(EnvironmentParameters,physicalSkyFillScale)==28 && offsetof(EnvironmentParameters,physicalGroundFillScale)==44, "06 Physical Environment ABI");
    Require(sizeof(LegacyEnvironmentParameters) == 80u,
            "historical analytic environment fixture remains 80 bytes");
    Require(offsetof(LegacyEnvironmentParameters, physicalSkyFillScale) == 60u &&
                offsetof(LegacyEnvironmentParameters, physicalGroundFillScale) == 76u,
            "Physical fill controls must occupy the former padding offsets");

    LegacyEnvironmentParameters off;
    legacyenvironment::ApplyPreset(off, LegacyEnvironmentPreset::Off);
    Require(off.skyStrength == 0.0f && off.groundStrength == 0.0f &&
                off.physicalSkyFillScale == 0.0f &&
                off.physicalGroundFillScale == 0.0f &&
                off.multipleScatteringEnabled == 0.0f &&
                off.multipleScatteringOctaves == 0u &&
                stage8::MultipleScatteringFactor(2.0f, 4.0f, off) == 0.0f,
            "Off must preserve stage 7 without indirect light");

    LegacyEnvironmentParameters balanced;
    Require(balanced.physicalSkyFillScale == 1.0f &&
                balanced.physicalGroundFillScale == 1.0f,
            "Balanced Physical fill must be neutral");
    const auto bottom = stage8::EvaluateWeights(0.0f, 0.4f, balanced);
    const auto middle = stage8::EvaluateWeights(0.5f, 0.4f, balanced);
    const auto top = stage8::EvaluateWeights(1.0f, 0.4f, balanced);
    Require(bottom.sky < middle.sky && middle.sky < top.sky &&
                bottom.ground > middle.ground && middle.ground > top.ground,
            "height must raise sky weight and lower ground weight");
    const auto lowDensity = stage8::EvaluateWeights(0.5f, 0.1f, balanced);
    const auto highDensity = stage8::EvaluateWeights(0.5f, 0.9f, balanced);
    Require(lowDensity.ambientOcclusion > highDensity.ambientOcclusion,
            "ambient visibility must decrease with density");
    Require(Near(stage8::AmbientVisibility(0.4f, 0.1f, balanced),
                 stage8::AmbientVisibility(0.4f, 1.0f, balanced)),
            "neutral ambient shadow coupling must preserve stage 8 visibility");

    LegacyEnvironmentParameters doubledSky = balanced;
    doubledSky.skyStrength *= 2.0f;
    const auto baseAmbient = stage8::EvaluateAmbientRadiance(
        0.5f, 0.4f, balanced);
    const auto doubledAmbient = stage8::EvaluateAmbientRadiance(
        0.5f, 0.4f, doubledSky);
    Require(Near(doubledAmbient.sky.r, baseAmbient.sky.r * 2.0f) &&
                Near(doubledAmbient.sky.g, baseAmbient.sky.g * 2.0f) &&
                Near(doubledAmbient.ground.r, baseAmbient.ground.r),
            "sky strength must scale only sky radiance linearly");
    LegacyEnvironmentParameters physicalFillOff = balanced;
    physicalFillOff.physicalSkyFillScale = 0.0f;
    physicalFillOff.physicalGroundFillScale = 0.0f;
    const auto manualAmbientWithPhysicalFillOff = stage8::EvaluateAmbientRadiance(
        0.5f, 0.4f, physicalFillOff);
    Require(Near(manualAmbientWithPhysicalFillOff.sky.r, baseAmbient.sky.r) &&
                Near(manualAmbientWithPhysicalFillOff.sky.g, baseAmbient.sky.g) &&
                Near(manualAmbientWithPhysicalFillOff.ground.r,
                     baseAmbient.ground.r),
            "Physical fill controls must not change legacy analytic radiance");

    LegacyEnvironmentParameters one = balanced;
    one.multipleScatteringOctaves = 1;
    const float expectedOne = one.multipleScatteringAttenuation *
        std::exp(-2.0f * one.multipleScatteringExtinctionFactor) *
        (1.0f + (3.0f - 1.0f) * one.multipleScatteringPhaseFactor);
    Require(Near(stage8::MultipleScatteringFactor(2.0f, 3.0f, one), expectedOne),
            "one octave must match the documented analytic formula");
    LegacyEnvironmentParameters two = one;
    two.multipleScatteringOctaves = 2;
    const float expectedTwo = expectedOne +
        one.multipleScatteringAttenuation *
        one.multipleScatteringAttenuation *
        std::exp(-2.0f * one.multipleScatteringExtinctionFactor *
                 one.multipleScatteringExtinctionFactor) *
        (1.0f + (3.0f - 1.0f) * one.multipleScatteringPhaseFactor *
         one.multipleScatteringPhaseFactor);
    Require(Near(stage8::MultipleScatteringFactor(2.0f, 3.0f, two), expectedTwo),
            "two octaves must reuse optical depth with decayed factors");

    LegacyEnvironmentParameters strong;
    legacyenvironment::ApplyPreset(strong, LegacyEnvironmentPreset::StrongFill);
    Require(Near(strong.skyStrength, 0.40f) &&
                Near(strong.groundStrength, 0.15f) &&
                Near(strong.physicalSkyFillScale, 1.25f) &&
                Near(strong.physicalGroundFillScale, 1.25f) &&
                strong.multipleScatteringOctaves == 3u,
            "Strong Fill preset must be deterministic");
    LegacyEnvironmentParameters ground;
    legacyenvironment::ApplyPreset(ground, LegacyEnvironmentPreset::GroundCheck);
    Require(ground.skyStrength == 0.0f && Near(ground.groundStrength, 0.35f) &&
                ground.physicalSkyFillScale == 0.0f &&
                ground.physicalGroundFillScale == 1.0f &&
                ground.multipleScatteringEnabled == 0.0f,
            "Ground Check must isolate ground bounce");
    LegacyEnvironmentParameters hero;
    legacyenvironment::ApplyPreset(hero, LegacyEnvironmentPreset::PortfolioHero);
    const float heroLit = stage8::AmbientVisibility(0.4f, 1.0f, hero);
    const float heroShadow = stage8::AmbientVisibility(0.4f, 0.1f, hero);
    Require(hero.ambientShadowCoupling > 0.0f &&
                hero.multipleScatteringInteriorBlend > 0.0f &&
                hero.physicalSkyFillScale == 1.0f &&
                hero.physicalGroundFillScale == 1.0f &&
                heroShadow < heroLit,
            "Portfolio Hero must couple ambient fill to sun visibility");
    Require(stage8::MultipleScatteringInteriorWeight(1.0f, hero) <
                stage8::MultipleScatteringInteriorWeight(0.1f, hero),
            "Portfolio Hero multiple scattering must favor occluded interiors");

    const auto lightBefore = stage6::MarchConstantDensity(
        3.0f, 0.4f, 1.0f, 0.25f, 16u);
    (void)stage8::MultipleScatteringFactor(
        lightBefore.opticalDepth, 2.0f, balanced);
    const auto lightAfter = stage6::MarchConstantDensity(
        3.0f, 0.4f, 1.0f, 0.25f, 16u);
    Require(Near(lightBefore.transmittance, lightAfter.transmittance) &&
                Near(lightBefore.opticalDepth, lightAfter.opticalDepth) &&
                lightBefore.stepCount == lightAfter.stepCount,
            "environment math must not change Light Ray results");

    LegacyEnvironmentParameters invalid;
    invalid.skyColor.x = std::numeric_limits<float>::quiet_NaN();
    invalid.groundStrength = -10.0f;
    invalid.ambientOcclusionStrength = std::numeric_limits<float>::infinity();
    invalid.ambientHeightInfluence = -5.0f;
    invalid.multipleScatteringOctaves = 999u;
    invalid.multipleScatteringAttenuation = std::numeric_limits<float>::quiet_NaN();
    invalid.ambientShadowCoupling = 10.0f;
    invalid.ambientShadowExponent = -10.0f;
    invalid.multipleScatteringInteriorBlend = 10.0f;
    invalid.physicalSkyFillScale = std::numeric_limits<float>::quiet_NaN();
    invalid.physicalGroundFillScale = 10.0f;
    invalid = legacyenvironment::Sanitize(invalid);
    const auto safeWeights = stage8::EvaluateWeights(
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(), invalid);
    const float safeMultiple = stage8::MultipleScatteringFactor(
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(), invalid);
    Require(invalid.multipleScatteringOctaves == 4u &&
                invalid.ambientShadowCoupling == 1.0f &&
                invalid.ambientShadowExponent == 0.1f &&
                invalid.multipleScatteringInteriorBlend == 1.0f &&
                invalid.physicalSkyFillScale == 1.0f &&
                invalid.physicalGroundFillScale == 2.0f &&
                std::isfinite(safeWeights.sky) &&
                std::isfinite(safeWeights.ground) &&
                std::isfinite(safeWeights.ambientOcclusion) &&
                std::isfinite(safeMultiple),
            "invalid environment inputs must sanitize to finite bounds");

    std::cout << "Stage8AmbientMath passed\n";
    return 0;
}
