// ============================================================================
//  Stage15Parameters.h - 프리셋 식별자와 역사적 scene 검증 계약
// ----------------------------------------------------------------------------
//  일반 F4는 LightingPresetStore의 독립 descriptor를 사용한다.
//  이 파일의 구 scene descriptor/resolver는 이전 진단 실행기 호환 전용이다.
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
    SnowOvercast = 2, // 역사적 검증 전용 scene; 일반 UI에는 노출하지 않는다.
    AutumnMorning = 3,
    BeachSunset = 4,
    BrightNoon = 5,
    PastelDream = 6,
};

struct Stage15SceneDescriptor
{
    // [직접 조절] ResolveBuiltInCloudFormation이 제공하는 모양 설정. 세 scene별 실제 원본은 preset store resolver.
    CloudFormationSettings formation = {};
    // [직접 조절] ResolveSceneConcept의 태양/phase 수치. 각 필드 범위는 LightParameters; F4 concept 적용 시 F3 수동값을 덮어쓴다.
    LightParameters light = {};
    // [파생 값] UI 태양 preset 이름, 기본 LowEast. 실제 방향은 atmosphere 각도에서 계산.
    Stage6SunPreset sunPreset = Stage6SunPreset::LowEast;
    // [직접 조절] phase preset 표식, 기본 SilverLining. 실제 계수는 resolver의 ApplyPhasePreset와 scene별 대입이 결정.
    Stage7PhasePreset phasePreset = Stage7PhasePreset::SilverLining;
    // [직접 조절] 환경광 설정, PortfolioHero 후 scene별 fill 수정. 각 필드 범위는 EnvironmentParameters.
    EnvironmentParameters environment = {};
    // [파생 값] UI 환경광 표식, Custom. 이름만 바꿔 계수 적용을 대신하지 않는다.
    Stage8EnvironmentPreset environmentPreset =
        Stage8EnvironmentPreset::Custom;
    // [직접 조절] EarthClear 대기와 태양 각도. AtmosphereParameters의 km/범위 계약을 따른다.
    AtmosphereParameters atmosphere = {};
    // [직접 조절] Concrete/Grass/Snow 반사율과 bounce; 구름 formation 저장과 별개.
    GroundLightingParameters ground = {};
    // [호환 유지] bool uint, sanitize 0/1, 초기 1. 현재 셰이더 미사용: 실제 표면 그림자는 strength/floor로 조절한다.
    // [직접 조절] F3/scene 그림자 강도 [0,1], 구조체 초기 1/내장 0.40~0.65 권장. 증가하면 지면/건물 그림자 대비 증가.
    float surfaceShadowStrength = 0.55f;
    // [직접 조절] F3/scene 표면 그림자 계수 하한 [0,1], 기본/권장 0.35. 증가하면 깊은 표면 그림자가 밝아진다.
    float surfaceAmbientFloor = 0.35f;
};

namespace stage15
{
inline const char* ConceptName(Stage15ConceptPreset preset)
{
    switch (preset)
    {
    case Stage15ConceptPreset::AutumnMorning: return "1";
    case Stage15ConceptPreset::BeachSunset: return "2";
    case Stage15ConceptPreset::BrightNoon: return "3";
    case Stage15ConceptPreset::PastelDream: return "4";
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
