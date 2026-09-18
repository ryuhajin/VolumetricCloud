#pragma once

#include "Stage15Parameters.h"
#include "ToneMappingParameters.h"
#include <filesystem>
#include <string>

// F4 슬롯은 형상·진단·캐시 리소스를 소유하지 않는다.
struct LightingPresetSettings
{
    LightParameters light;
    EnvironmentParameters environment;
    AtmosphereParameters atmosphere;
    GroundLightingParameters ground;
    ToneMappingParameters tone;
    float surfaceShadowStrength = 0.55f;
    float surfaceAmbientFloor = 0.35f;
};

bool IsLightingPreset(Stage15ConceptPreset preset);
unsigned LightingPresetSlot(Stage15ConceptPreset preset);
LightingPresetSettings BuiltInLightingPreset(Stage15ConceptPreset preset);
bool ValidateLightingPreset(const LightingPresetSettings& settings);
bool LightingPresetEqual(const LightingPresetSettings& a, const LightingPresetSettings& b);
std::filesystem::path LightingPresetPath(const std::filesystem::path& root, Stage15ConceptPreset preset);
bool SaveLightingPreset(const std::filesystem::path& root, Stage15ConceptPreset preset,
    const LightingPresetSettings& settings, std::string& status);
bool ResolveLightingPreset(const std::filesystem::path& root, Stage15ConceptPreset preset,
    bool useSaved, LightingPresetSettings& settings, CloudFormationPresetSource& source,
    std::string& status);
