#include "CloudShapeDomainContract.h"
#include "HighCloudQuality.h"
#include "Stage15Parameters.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

bool Near(float a, float b, float epsilon = 1.0e-6f)
{
    return std::abs(a - b) <= epsilon;
}

void RequireFit(const CloudFormationSettings& formation,
                float expectedHeadroom)
{
    PreparedCloudFormation prepared;
    std::string status;
    Require(PrepareCloudFormationSettings(
                formation, prepared, status, 200.0f),
            "formation must fit its planar domain with 200m headroom");
    Require(prepared.fit.valid &&
            prepared.fit.remainingHeadroomMeters >= expectedHeadroom - 0.01f,
            "formation headroom");
}

void RequireLegacyNonHeight(
    const CloudFormationSettings& formation, CloudTypeMode mode,
    float threshold, float softness, float coverageBias,
    float coverageContrast, float densityLink, float thicknessLink,
    float cloudTypeBias)
{
    Require(formation.weather.cloudTypeMode == mode &&
            Near(formation.weather.coverageThreshold, threshold) &&
            Near(formation.weather.coverageSoftness, softness) &&
            Near(formation.weather.coverage.bias, coverageBias) &&
            Near(formation.weather.coverage.contrast, coverageContrast) &&
            Near(formation.weather.densityCoverageInfluence, densityLink) &&
            Near(formation.weather.thicknessCoverageInfluence, thicknessLink) &&
            Near(formation.weather.cloudType.bias, cloudTypeBias),
            "legacy Stage 14 Weather appearance values");
}

void RequireLegacyProfiles(
    const CloudFormationSettings& formation,
    float stratusBottom, float stratusTop,
    float mixedBottom, float mixedTop,
    float cumulusBottom, float cumulusTop,
    float upperBottom, float upperStart, float upperEnd)
{
    const CloudShapeParameters& shape = formation.shape;
    Require(Near(shape.stratusBottomFadeEnd, stratusBottom) &&
            Near(shape.stratusTopFadeStart, stratusTop) &&
            Near(shape.mixedBottomFadeEnd, mixedBottom) &&
            Near(shape.mixedTopFadeStart, mixedTop) &&
            Near(shape.cumulusBottomFadeEnd, cumulusBottom) &&
            Near(shape.cumulusTopFadeStart, cumulusTop) &&
            Near(shape.cumulusUpperMassBottom, upperBottom) &&
            Near(shape.cumulusUpperMassStart, upperStart) &&
            Near(shape.cumulusUpperMassEnd, upperEnd),
            "legacy Stage 14 vertical profile values");
}
}

int main()
{
    Require(sizeof(CloudShapeParameters) == 64u,
            "physical-only CloudShapeCB must be 64 bytes");
    Require(highcloud::kMaximumViewSteps == 512u &&
            highcloud::kViewStepMeters == 100.0f,
            "Stage 15 uses the fixed High contract");

    CloudFormationSettings stratus;
    Require(ResolveBuiltInCloudFormation(
                CloudFormationType::Stratus, stratus),
            "resolve Stratus");
    Require(Near(stratus.coverage, 0.40f) &&
            Near(stratus.densityMultiplier, 1.20f) &&
            Near(stratus.extinctionPerMeter, 0.00042f) &&
            Near(stratus.detailErosion, 0.12f),
            "Stratus legacy appearance values");
    RequireLegacyNonHeight(
        stratus, CloudTypeMode::Stratus,
        0.49f, 0.22f, 0.01f, 1.03f, 0.50f, 0.65f, 0.0f);
    RequireLegacyProfiles(
        stratus, 0.05f, 0.72f, 0.10f, 0.86f,
        0.08f, 0.93f, 0.65f, 0.08f, 0.70f);
    Require(Near(stratus.shape.stratusMinimumThicknessMeters, 1500.0f) &&
            Near(stratus.shape.stratusMaximumThicknessMeters, 2300.0f) &&
            Near(stratus.shape.localBaseLiftMaxMeters, 0.0f) &&
            Near(stratus.shape.footprintCoverageInfluence, 0.20f) &&
            Near(stratus.domainBottomMeters, 1500.0f) &&
            Near(stratus.domainThicknessMeters, 2500.0f),
            "Stratus anti-clipping geometry");
    RequireFit(stratus, 200.0f);

    CloudFormationSettings cumulus;
    Require(ResolveBuiltInCloudFormation(
                CloudFormationType::Cumulus, cumulus),
            "resolve Cumulus");
    Require(Near(cumulus.coverage, 0.45f) &&
            Near(cumulus.densityMultiplier, 1.25f) &&
            Near(cumulus.extinctionPerMeter, 0.00038f) &&
            Near(cumulus.detailErosion, 0.18f),
            "Cumulus legacy appearance values");
    RequireLegacyNonHeight(
        cumulus, CloudTypeMode::Cumulus,
        0.52f, 0.20f, 0.0f, 1.05f, 0.45f, 0.70f, 0.0f);
    RequireLegacyProfiles(
        cumulus, 0.06f, 0.65f, 0.10f, 0.86f,
        0.08f, 0.94f, 0.65f, 0.08f, 0.70f);
    Require(Near(cumulus.shape.cumulusMinimumThicknessMeters, 2000.0f) &&
            Near(cumulus.shape.cumulusMaximumThicknessMeters, 3200.0f) &&
            Near(cumulus.shape.localBaseLiftMaxMeters, 300.0f) &&
            Near(cumulus.shape.footprintCoverageInfluence, 0.50f) &&
            Near(cumulus.domainBottomMeters, 1800.0f) &&
            Near(cumulus.domainThicknessMeters, 3700.0f),
            "Cumulus anti-clipping geometry");
    RequireFit(cumulus, 200.0f);

    CloudFormationSettings mixed;
    Require(ResolveBuiltInCloudFormation(
                CloudFormationType::Mixed, mixed),
            "resolve Mixed");
    Require(Near(mixed.coverage, 0.68f) &&
            Near(mixed.densityMultiplier, 1.15f) &&
            Near(mixed.extinctionPerMeter, 0.00035f) &&
            Near(mixed.detailErosion, 0.18f),
            "Mixed legacy appearance values");
    RequireLegacyNonHeight(
        mixed, CloudTypeMode::WeatherMap,
        0.50f, 0.20f, 0.0f, 1.05f, 0.45f, 0.60f, 0.08f);
    RequireLegacyProfiles(
        mixed, 0.06f, 0.65f, 0.10f, 0.86f,
        0.08f, 0.93f, 0.65f, 0.08f, 0.70f);
    Require(Near(mixed.shape.stratusMinimumThicknessMeters, 1500.0f) &&
            Near(mixed.shape.stratusMaximumThicknessMeters, 2500.0f) &&
            Near(mixed.shape.cumulusMinimumThicknessMeters, 3000.0f) &&
            Near(mixed.shape.cumulusMaximumThicknessMeters, 4600.0f) &&
            Near(mixed.shape.localBaseLiftMaxMeters, 200.0f) &&
            Near(mixed.shape.footprintCoverageInfluence, 0.40f) &&
            Near(mixed.domainBottomMeters, 1500.0f) &&
            Near(mixed.domainThicknessMeters, 5000.0f),
            "Mixed anti-clipping geometry");
    RequireFit(mixed, 200.0f);

    for (std::uint32_t index = 0u; index < 3u; ++index)
    {
        const auto concept = static_cast<Stage15ConceptPreset>(index);
        const Stage15SceneDescriptor descriptor =
            stage15::ResolveSceneConcept(concept);
        Require(IsValidCloudFormationSettings(descriptor.formation),
                "scene concept formation is valid");
        Require(descriptor.surfaceShadowEnabled == 1u,
                "scene concept keeps physical atmosphere and Deep Cache");
        RequireFit(descriptor.formation, 200.0f);
    }

    std::cout << "Stage15 preset math passed\n";
    return 0;
}
