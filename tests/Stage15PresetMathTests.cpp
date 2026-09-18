#include "CloudShapeDomainContract.h"
#include "HighCloudQuality.h"
#include "CloudMotionParameters.h"
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
    const CloudFormationSettings& formation, CloudTypeSelectionMode mode,
    float threshold, float softness, float coverageBias,
    float coverageContrast, float densityLink, float thicknessLink,
    float cloudTypeBias)
{
    Require(formation.typeSelection.mode == mode &&
            Near(formation.weather.generator.coverageThreshold, threshold) &&
            Near(formation.weather.generator.coverageSoftness, softness) &&
            Near(formation.weather.generator.coverage.bias, coverageBias) &&
            Near(formation.weather.generator.coverage.contrast, coverageContrast) &&
            Near(formation.weather.generator.densityCoverageInfluence, densityLink) &&
            Near(formation.weather.generator.thicknessCoverageInfluence, thicknessLink),
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
    const float t=FixedCloudType(formation.typeSelection);
    Require(Near(shape.bottomFadeEnd,t==0.f?stratusBottom:(t==1.f?cumulusBottom:mixedBottom)) &&
            Near(shape.topFadeStart,t==0.f?stratusTop:(t==1.f?cumulusTop:mixedTop)) &&
            Near(shape.lowerDensityScale,t==1.f?upperBottom:1.f) &&
            Near(shape.upperTransitionStart,upperStart) && Near(shape.upperTransitionEnd,upperEnd),
            "common profile preserves active fixed-type curve");
    for(int i=0;i<=100;++i) {
        const float h=i/100.f;
        const float profile=EvaluateCommonVerticalProfile(h,shape);
        Require(std::isfinite(profile) && profile>=0.f && profile<=1.f,"profile finite and bounded");
        if(i==0 || i==100) Require(profile==0.f,"profile ends empty");
    }
}
}

int main()
{
    for (int i = 0; i < 3; ++i)
    {
        CloudFormationSettings candidate;
        Require(ResolveBuiltInCloudFormation(static_cast<CloudFormationType>(i), candidate) &&
            candidate.shape.densityShaping == 0.70f, "02 approved Type shaping");
        Require(ResolveBuiltInCloudFormation(static_cast<CloudFormationConcept>(i), candidate) &&
            candidate.shape.densityShaping == (i == 0 ? 0.70f : 0.0f), "02 Urban-only Concept shaping");
    }
    // 공통 두께는 타입 선택만 바꾸어도 달라지지 않는다.
    WeatherColumnSettings common;
    common.minimumThicknessMeters=900.f; common.maximumThicknessMeters=2700.f;
    for (unsigned type=0;type<3;++type) {
        CloudTypeSelection selection{static_cast<CloudTypeSelectionMode>(type)};
        auto cb=ResolveWeatherColumnParameters(common,selection);
        Require(cb.minimumThicknessMeters==900.f && cb.maximumThicknessMeters==2700.f &&
            cloudshapedomain::ActiveMaximumThicknessMeters(common,selection)==2700.f,"common thickness independent of type");
    }
    CloudShapeParameters p;
    const float lower=EvaluateCommonVerticalProfile(.2f,p);
    p.lowerDensityScale=.2f;
    Require(EvaluateCommonVerticalProfile(.2f,p)<lower,"lower density changes interior curve");
    p=CloudShapeParameters{};
    const float low=EvaluateCommonVerticalProfile(.05f,p);
    p.bottomFadeEnd=.3f;
    Require(EvaluateCommonVerticalProfile(.05f,p)<low,"bottom fade changes base");
    p=CloudShapeParameters{};
    const float high=EvaluateCommonVerticalProfile(.8f,p);
    p.topFadeStart=.5f;
    Require(EvaluateCommonVerticalProfile(.8f,p)<high,"top fade changes upper body");
    for (float strength : {0.0f, 0.35f, 0.70f, 1.0f})
    {
        float previous = 0;
        Require(ShapeCloudDensity(0, strength) == 0, "empty space preserved");
        for (int i = 0; i <= 10000; ++i)
        {
            const float base = float(i) / 2000.0f; // Base > 1 포함.
            const float shaped = ShapeCloudDensity(base, strength);
            Require(std::isfinite(shaped) && shaped >= previous, "monotone density curve");
            if (strength == 0) Require(shaped == base, "zero strength exact restoration");
            for (float erosion : {0.0f, 0.12f, 0.24f, 1.0f})
                Require(ShapeCloudDensity(std::clamp(base - erosion, 0.0f, 1.0f), strength)
                    <= shaped + 1e-6f, "Detail stays bounded by Base after shaping");
            previous = shaped;
        }
        for (float q : {0.0f, 1e-7f, 1e-5f, 1e-4f})
            Require(ShapeCloudDensity(q, strength) <= q, "tiny raw support cannot become occupied");
    }
    Require(Near(ShapeCloudDensity(0.2f, 0.35f), 0.305f) &&
            Near(ShapeCloudDensity(0.4f, 0.70f), 0.82f) &&
            Near(ShapeCloudDensity(2.0f, 0.70f), 1.3f), "density curve anchors");
    CloudShapeParameters invalidShape;
    invalidShape.densityShaping = NAN;
    Require(SanitizeCloudShapeParameters(invalidShape).densityShaping == 0, "NaN shaping defaults off");
    invalidShape.densityShaping = 2;
    Require(SanitizeCloudShapeParameters(invalidShape).densityShaping == 1, "shaping clamps to one");
    invalidShape.densityShaping = -1;
    Require(SanitizeCloudShapeParameters(invalidShape).densityShaping == 0, "shaping clamps to zero");
    Require(sizeof(CloudShapeParameters) == 48u &&
            sizeof(WeatherColumnParameters) == 32u,
            "Shape b7 and Weather Column b10 ABI");
    Require(highcloud::kMaximumViewSteps == 512u &&
            highcloud::kViewStepMeters == 100.0f,
            "Stage 15 uses the fixed High contract");
    CloudMotionParameters invalidMotion;
    invalidMotion.direction = { 0.0f, 7.0f, 0.0f };
    invalidMotion.speedMetersPerSecond = NAN;
    const CloudMotionParameters safeMotion =
        SanitizeCloudMotionParameters(invalidMotion);
    Require(Near(safeMotion.speedMetersPerSecond, 12.0f) &&
            Near(safeMotion.direction.y, 0.0f),
            "session motion sanitizes to the app default");

    CloudFormationSettings stratus;
    Require(ResolveBuiltInCloudFormation(
                CloudFormationType::Stratus, stratus),
            "resolve Stratus");
    Require(Near(stratus.coverage, 0.60f) &&
            Near(stratus.densityMultiplier, 1.20f) &&
            Near(stratus.extinctionPerMeter, 0.00042f) &&
            Near(stratus.detailErosion, 0.12f),
            "Stratus legacy appearance values");
    RequireLegacyNonHeight(
        stratus, CloudTypeSelectionMode::FixedStratus,
        0.49f, 0.22f, 0.01f, 1.03f, 0.50f, 0.65f, 0.0f);
    RequireLegacyProfiles(
        stratus, 0.05f, 0.72f, 0.10f, 0.86f,
        0.08f, 0.93f, 0.65f, 0.08f, 0.70f);
    Require(Near(stratus.weather.column.minimumThicknessMeters, 450.0f) &&
            Near(stratus.weather.column.maximumThicknessMeters, 850.0f) &&
            Near(stratus.weather.column.maximumBaseLiftMeters, 0.0f) &&
            Near(stratus.shape.footprintCoverageInfluence, 0.20f) &&
            Near(stratus.domainBottomMeters, 1500.0f) &&
            Near(stratus.domainThicknessMeters, 1050.0f),
            "Stratus anti-clipping geometry");
    RequireFit(stratus, 200.0f);

    CloudFormationSettings cumulus;
    Require(ResolveBuiltInCloudFormation(
                CloudFormationType::Cumulus, cumulus),
            "resolve Cumulus");
    Require(Near(cumulus.coverage, 0.48f) &&
            Near(cumulus.densityMultiplier, 1.25f) &&
            Near(cumulus.extinctionPerMeter, 0.00038f) &&
            Near(cumulus.detailErosion, 0.18f),
            "Cumulus legacy appearance values");
    RequireLegacyNonHeight(
        cumulus, CloudTypeSelectionMode::FixedCumulus,
        0.56f, 0.14f, 0.0f, 1.05f, 0.45f, 0.70f, 0.0f);
    RequireLegacyProfiles(
        cumulus, 0.06f, 0.65f, 0.10f, 0.86f,
        0.08f, 0.94f, 0.65f, 0.08f, 0.70f);
    Require(Near(cumulus.weather.column.minimumThicknessMeters, 2000.0f) &&
            Near(cumulus.weather.column.maximumThicknessMeters, 3200.0f) &&
            Near(cumulus.weather.column.maximumBaseLiftMeters, 300.0f) &&
            Near(cumulus.shape.footprintCoverageInfluence, 0.50f) &&
            Near(cumulus.domainBottomMeters, 1800.0f) &&
            Near(cumulus.domainThicknessMeters, 3700.0f),
            "Cumulus anti-clipping geometry");
    RequireFit(cumulus, 200.0f);

    CloudFormationSettings mixed;
    Require(ResolveBuiltInCloudFormation(
                CloudFormationType::Mixed, mixed),
            "resolve Mixed");
    Require(Near(mixed.coverage, 0.42f) &&
            Near(mixed.densityMultiplier, 1.15f) &&
            Near(mixed.extinctionPerMeter, 0.00035f) &&
            Near(mixed.detailErosion, 0.18f),
            "Mixed legacy appearance values");
    RequireLegacyNonHeight(
        mixed, CloudTypeSelectionMode::FixedMixed,
        0.35f, 0.20f, 0.0f, 1.05f, 0.45f, 0.60f, 0.08f);
    RequireLegacyProfiles(
        mixed, 0.06f, 0.65f, 0.10f, 0.86f,
        0.08f, 0.93f, 0.65f, 0.08f, 0.70f);
    Require(Near(mixed.weather.column.minimumThicknessMeters, 650.0f) &&
            Near(mixed.weather.column.maximumThicknessMeters, 1150.0f) &&
            Near(mixed.weather.column.maximumBaseLiftMeters, 100.0f) &&
            Near(mixed.shape.footprintCoverageInfluence, 0.40f) &&
            Near(mixed.domainBottomMeters, 3000.0f) &&
            Near(mixed.domainThicknessMeters, 1450.0f),
            "Mixed anti-clipping geometry");
    RequireFit(mixed, 200.0f);

    Require(stratus.weather.column.maximumThicknessMeters < cumulus.weather.column.minimumThicknessMeters &&
        mixed.baseNoiseWorldSizeMeters < cumulus.baseNoiseWorldSizeMeters &&
        stratus.baseNoiseWorldSizeMeters > cumulus.baseNoiseWorldSizeMeters,
        "type silhouettes differ in thickness and horizontal scale");
    for (std::uint32_t index = 0u; index < 3u; ++index)
    {
        const auto concept = static_cast<Stage15ConceptPreset>(index);
        const Stage15SceneDescriptor descriptor =
            stage15::ResolveSceneConcept(concept);
        Require(IsValidCloudFormationSettings(descriptor.formation),
                "scene concept formation is valid");
        RequireFit(descriptor.formation, 200.0f);
    }

    std::cout << "Stage15 preset math passed\n";
    return 0;
}
