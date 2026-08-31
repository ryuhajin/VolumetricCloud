// ============================================================================
//  Stage15Parameters.h - 최종 장면 콘셉트의 CPU 전용 계약
// ----------------------------------------------------------------------------
//  렌더 품질은 High 하나로 고정한다. 이 파일은 장면 콘셉트가 함께 소유하는
//  formation, 태양/환경광, 대기, 지면과 지표 그림자만 정의한다.
// ============================================================================
#pragma once

#include "AtmosphereParameters.h"
#include "CloudFormationPresetStore.h"
#include "EnvironmentParameters.h"
#include "GroundLightingParameters.h"
#include "LightParameters.h"
#include "Stage12ShadowParameters.h"

#include <cstdint>

enum class Stage15ConceptPreset : std::uint32_t
{
    UrbanFairWeather = 0,
    MeadowBrokenClouds = 1,
    SnowOvercast = 2,
};

struct Stage15SceneDescriptor
{
    CloudFormationSettings formation = {};
    LightParameters light = {};
    Stage6SunPreset sunPreset = Stage6SunPreset::LowEast;
    Stage7PhasePreset phasePreset = Stage7PhasePreset::SilverLining;
    EnvironmentParameters environment = {};
    Stage8EnvironmentPreset environmentPreset =
        Stage8EnvironmentPreset::Custom;
    AtmosphereParameters atmosphere = {};
    GroundLightingParameters ground = {};
    std::uint32_t surfaceShadowEnabled = 1u;
    float surfaceShadowStrength = 0.55f;
    float surfaceAmbientFloor = 0.35f;
};

namespace stage15
{
inline const char* ConceptName(Stage15ConceptPreset preset)
{
    switch (preset)
    {
    case Stage15ConceptPreset::UrbanFairWeather:
        return "Urban Fair Weather";
    case Stage15ConceptPreset::MeadowBrokenClouds:
        return "Meadow Broken Clouds";
    case Stage15ConceptPreset::SnowOvercast:
        return "Snow Overcast";
    }
    return "Urban Fair Weather";
}

inline CloudFormationConcept FormationConcept(Stage15ConceptPreset preset)
{
    switch (preset)
    {
    case Stage15ConceptPreset::MeadowBrokenClouds:
        return CloudFormationConcept::MeadowBrokenClouds;
    case Stage15ConceptPreset::SnowOvercast:
        return CloudFormationConcept::SnowOvercast;
    case Stage15ConceptPreset::UrbanFairWeather:
    default:
        return CloudFormationConcept::UrbanFairWeather;
    }
}

inline Stage15SceneDescriptor ResolveSceneConcept(Stage15ConceptPreset preset)
{
    Stage15SceneDescriptor result;
    ResolveBuiltInCloudFormation(FormationConcept(preset), result.formation);
    stage6light::ApplyPhasePreset(result.light, Stage7PhasePreset::SilverLining);
    stage8environment::ApplyPreset(
        result.environment, Stage8EnvironmentPreset::PortfolioHero);
    stage14atmosphere::ApplyPreset(
        result.atmosphere, AtmospherePreset::EarthClear);
    switch (preset)
    {
    case Stage15ConceptPreset::MeadowBrokenClouds:
        result.light.sunIntensity = 0.95f;
        result.light.forwardScatteringG = 0.75f;
        result.light.edgeInfluence = 0.70f;
        result.light.edgeOpticalDepthScale = 1.8f;
        result.light.shadowExponent = 1.30f;
        result.environment.physicalSkyFillScale = 0.95f;
        result.environment.physicalGroundFillScale = 0.95f;
        result.environment.multipleScatteringInteriorBlend = 0.60f;
        stage14ground::ApplyPreset(result.ground, GroundMaterialPreset::Grass);
        result.ground.bounceMultiplier = 1.0f;
        result.surfaceShadowStrength = 0.65f;
        break;
    case Stage15ConceptPreset::SnowOvercast:
        result.light.sunIntensity = 0.75f;
        result.light.forwardScatteringG = 0.75f;
        result.light.edgeInfluence = 0.35f;
        result.light.edgeOpticalDepthScale = 1.4f;
        result.light.shadowExponent = 1.15f;
        result.environment.physicalSkyFillScale = 1.10f;
        result.environment.physicalGroundFillScale = 1.10f;
        result.environment.multipleScatteringInteriorBlend = 0.80f;
        stage14ground::ApplyPreset(result.ground, GroundMaterialPreset::Snow);
        result.ground.bounceMultiplier = 1.5f;
        result.surfaceShadowStrength = 0.40f;
        break;
    case Stage15ConceptPreset::UrbanFairWeather:
    default:
        result.light.sunIntensity = 1.0f;
        result.light.forwardScatteringG = 0.75f;
        result.light.edgeInfluence = 0.85f;
        result.light.edgeOpticalDepthScale = 2.0f;
        result.light.shadowExponent = 1.35f;
        result.environment.physicalSkyFillScale = 0.85f;
        result.environment.physicalGroundFillScale = 0.85f;
        result.environment.multipleScatteringInteriorBlend = 0.55f;
        stage14ground::ApplyPreset(
            result.ground, GroundMaterialPreset::Concrete);
        result.ground.bounceMultiplier = 1.0f;
        result.surfaceShadowStrength = 0.55f;
        break;
    }

    result.light.directionToSun = stage6light::DirectionFromAngles(
        result.atmosphere.sunAzimuthDegrees,
        result.atmosphere.sunElevationDegrees);
    result.light = stage6light::Sanitize(result.light);
    result.environment = stage8environment::Sanitize(result.environment);
    result.atmosphere = stage14atmosphere::Sanitize(result.atmosphere);
    result.ground = stage14ground::Sanitize(result.ground);
    return result;
}
}
