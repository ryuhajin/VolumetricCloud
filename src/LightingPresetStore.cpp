// F4 조명·환경 슬롯: JSON에는 직접 편집 설정만 기록한다.
#include "LightingPresetStore.h"
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <fstream>
#include <cctype>
#include <locale>
#include <iomanip>
#include <map>
#include <array>
#include <regex>
#include <sstream>
#include <type_traits>
#include <limits>

namespace {
template<class S, class F> void Visit(S& v, F f)
{
    f("light.sunIntensity", v.light.sunIntensity);
    f("light.sunColor.x", v.light.sunColor.x);
    f("light.sunColor.y", v.light.sunColor.y);
    f("light.sunColor.z", v.light.sunColor.z);
    f("light.singleScatteringAlbedo", v.light.singleScatteringAlbedo);
    f("light.phaseEnabled", v.light.phaseEnabled);
    f("light.forwardScatteringG", v.light.forwardScatteringG);
    f("light.backwardScatteringG", v.light.backwardScatteringG);
    f("light.phaseBlend", v.light.phaseBlend);
    f("light.phaseIntensity", v.light.phaseIntensity);
    f("light.edgeInfluence", v.light.edgeInfluence);
    f("light.edgeOpticalDepthScale", v.light.edgeOpticalDepthScale);
    f("light.shadowExponent", v.light.shadowExponent);
    f("light.rimIntensity", v.light.rimIntensity);
    f("light.rimDepthScale", v.light.rimDepthScale);
    f("environment.ambientOcclusionStrength", v.environment.ambientOcclusionStrength);
    f("environment.ambientHeightInfluence", v.environment.ambientHeightInfluence);
    f("environment.multipleScatteringEnabled", v.environment.multipleScatteringEnabled);
    f("environment.multipleScatteringOctaves", v.environment.multipleScatteringOctaves);
    f("environment.multipleScatteringAttenuation", v.environment.multipleScatteringAttenuation);
    f("environment.multipleScatteringExtinctionFactor", v.environment.multipleScatteringExtinctionFactor);
    f("environment.multipleScatteringPhaseFactor", v.environment.multipleScatteringPhaseFactor);
    f("environment.physicalSkyFillScale", v.environment.physicalSkyFillScale);
    f("environment.ambientShadowCoupling", v.environment.ambientShadowCoupling);
    f("environment.ambientShadowExponent", v.environment.ambientShadowExponent);
    f("environment.multipleScatteringInteriorBlend", v.environment.multipleScatteringInteriorBlend);
    f("environment.physicalGroundFillScale", v.environment.physicalGroundFillScale);
    f("atmosphere.preset", v.atmosphere.preset);
    f("atmosphere.bottomRadiusKm", v.atmosphere.bottomRadiusKm);
    f("atmosphere.topRadiusKm", v.atmosphere.topRadiusKm);
    f("atmosphere.rayleighScaleHeightKm", v.atmosphere.rayleighScaleHeightKm);
    f("atmosphere.mieScaleHeightKm", v.atmosphere.mieScaleHeightKm);
    f("atmosphere.rayleighScatteringPerKm.x", v.atmosphere.rayleighScatteringPerKm.x);
    f("atmosphere.rayleighScatteringPerKm.y", v.atmosphere.rayleighScatteringPerKm.y);
    f("atmosphere.rayleighScatteringPerKm.z", v.atmosphere.rayleighScatteringPerKm.z);
    f("atmosphere.rayleighScale", v.atmosphere.rayleighScale);
    f("atmosphere.mieScatteringPerKm", v.atmosphere.mieScatteringPerKm);
    f("atmosphere.mieExtinctionPerKm", v.atmosphere.mieExtinctionPerKm);
    f("atmosphere.mieAbsorptionScale", v.atmosphere.mieAbsorptionScale);
    f("atmosphere.mieG", v.atmosphere.mieG);
    f("atmosphere.ozoneAbsorptionPerKm.x", v.atmosphere.ozoneAbsorptionPerKm.x);
    f("atmosphere.ozoneAbsorptionPerKm.y", v.atmosphere.ozoneAbsorptionPerKm.y);
    f("atmosphere.ozoneAbsorptionPerKm.z", v.atmosphere.ozoneAbsorptionPerKm.z);
    f("atmosphere.ozoneScale", v.atmosphere.ozoneScale);
    f("atmosphere.ozoneCenterKm", v.atmosphere.ozoneCenterKm);
    f("atmosphere.ozoneHalfWidthKm", v.atmosphere.ozoneHalfWidthKm);
    f("atmosphere.turbidity", v.atmosphere.turbidity);
    f("atmosphere.solarIrradiance.x", v.atmosphere.solarIrradiance.x);
    f("atmosphere.solarIrradiance.y", v.atmosphere.solarIrradiance.y);
    f("atmosphere.solarIrradiance.z", v.atmosphere.solarIrradiance.z);
    f("atmosphere.sunAzimuthDegrees", v.atmosphere.sunAzimuthDegrees);
    f("atmosphere.sunElevationDegrees", v.atmosphere.sunElevationDegrees);
    f("ground.preset", v.ground.preset);
    f("ground.albedo.x", v.ground.albedo.x);
    f("ground.albedo.y", v.ground.albedo.y);
    f("ground.albedo.z", v.ground.albedo.z);
    f("ground.bounceMultiplier", v.ground.bounceMultiplier);
    f("tone.mode", v.tone.mode);
    f("tone.exposureEv", v.tone.exposureEv);
    f("tone.whiteBalanceKelvin", v.tone.whiteBalanceKelvin);
    f("surfaceShadowStrength", v.surfaceShadowStrength);
    f("surfaceAmbientFloor", v.surfaceAmbientFloor);
}
std::array<double, 62> Values(const LightingPresetSettings& value)
{
    std::array<double, 62> result{};
    std::size_t index = 0;
    Visit(value, [&](const char*, auto field) { result[index++] = static_cast<double>(field); });
    return result;
}
// 이 schema는 이름이 있는 숫자 필드만 쓴다. 문법·중복·누락·추가 필드를 모두 검사한다.
bool Parse(const std::string& text, std::map<std::string, double>& values)
{
    std::istringstream in(text);
    in.imbue(std::locale::classic());
    char c;
    if (!(in >> c) || c != '{') return false;
    const std::regex number(R"(-?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?)");
    while (true) {
        if (!(in >> c) || c != '"') return false;
        std::string key;
        if (!std::getline(in, key, '"') || key.find('\\') != std::string::npos ||
            !(in >> c) || c != ':') return false;
        std::string token;
        in >> std::ws;
        while (in.peek() != EOF && in.peek() != ',' && in.peek() != '}' &&
               !std::isspace(static_cast<unsigned char>(in.peek())))
            token += static_cast<char>(in.get());
        if (!std::regex_match(token, number)) return false;
        double value;
        std::istringstream numberIn(token);
        numberIn.imbue(std::locale::classic());
        if (!(numberIn >> value) || !std::isfinite(value) ||
            !values.emplace(key, value).second) return false;
        if (!(in >> c)) return false;
        if (c == '}') { in >> std::ws; return in.eof(); }
        if (c != ',') return false;
    }
}
}

bool IsLightingPreset(Stage15ConceptPreset preset)
{
    return preset >= Stage15ConceptPreset::AutumnMorning && preset <= Stage15ConceptPreset::PastelDream;
}
unsigned LightingPresetSlot(Stage15ConceptPreset preset)
{
    return IsLightingPreset(preset) ? static_cast<unsigned>(preset) - 2u : 0u;
}
LightingPresetSettings BuiltInLightingPreset(Stage15ConceptPreset preset)
{
    LightingPresetSettings v;
    stage6light::ApplyPhasePreset(v.light, Stage7PhasePreset::SilverLining);
    stage8environment::ApplyPreset(v.environment, Stage8EnvironmentPreset::PortfolioHero);
    v.light.sunColor = {1,1,1};
    v.atmosphere.sunAzimuthDegrees = -108.5f;
    v.ground.preset = GroundMaterialPreset::Custom;
    switch (preset) {
    case Stage15ConceptPreset::AutumnMorning:
        v.atmosphere.sunElevationDegrees = 8;
        v.atmosphere.turbidity = 1.5f;
        v.light.sunColor = {1,.93f,.82f};
        v.ground.albedo = {.22f,.14f,.07f};
        v.tone.exposureEv = .35f;
        v.tone.whiteBalanceKelvin = 5800;
        v.surfaceShadowStrength = .55f;
        break;
    case Stage15ConceptPreset::BeachSunset:
        v.atmosphere.sunElevationDegrees = 4;
        v.atmosphere.turbidity = 2;
        v.atmosphere.ozoneScale = 1.25f;
        v.light.sunColor = {1,.82f,.76f};
        v.ground.albedo = {.48f,.36f,.22f};
        v.ground.bounceMultiplier = 1.2f;
        v.environment.physicalSkyFillScale = .8f;
        v.tone.exposureEv = .6f;
        v.tone.whiteBalanceKelvin = 5600;
        v.surfaceShadowStrength = .55f;
        break;
    case Stage15ConceptPreset::PastelDream:
        v.atmosphere.sunElevationDegrees = 12;
        v.atmosphere.turbidity = 1.8f;
        v.atmosphere.ozoneScale = 1.8f;
        v.atmosphere.solarIrradiance = {1.6f,1.3f,1.8f};
        v.light.sunColor = {1,.83f,1};
        v.ground.albedo = {.40f,.30f,.48f};
        v.ground.bounceMultiplier = 1.3f;
        v.environment.physicalSkyFillScale = 1.2f;
        v.environment.physicalGroundFillScale = 1.1f;
        v.environment.ambientOcclusionStrength = .8f;
        v.light.shadowExponent = .85f;
        v.light.rimIntensity = 1.3f;
        v.surfaceShadowStrength = .3f;
        v.surfaceAmbientFloor = .55f;
        v.tone.exposureEv = 1.3f;
        v.tone.whiteBalanceKelvin = 7000;
        break;
    case Stage15ConceptPreset::BrightNoon:
    default:
        v.atmosphere.sunElevationDegrees = 65;
        v.light.sunIntensity = 1.7f;
        v.atmosphere.rayleighScale = 1.25f;
        v.ground.preset = GroundMaterialPreset::Grass;
        v.ground.albedo = stage14ground::PresetAlbedo(v.ground.preset);
        v.surfaceShadowStrength = .7f;
        v.tone.exposureEv = .9f;
        break;
    }
    v.light.directionToSun = stage6light::DirectionFromAngles(v.atmosphere.sunAzimuthDegrees, v.atmosphere.sunElevationDegrees);
    return v;
}
bool LightingPresetEqual(const LightingPresetSettings& a, const LightingPresetSettings& b)
{
    return Values(a) == Values(b);
}
bool ValidateLightingPreset(const LightingPresetSettings& v)
{
    for (double field : Values(v)) if (!std::isfinite(field)) return false;
    if (static_cast<unsigned>(v.atmosphere.preset) > 2 ||
        static_cast<unsigned>(v.ground.preset) > 3 ||
        static_cast<unsigned>(v.tone.mode) > 2 ||
        v.surfaceShadowStrength < 0 || v.surfaceShadowStrength > 1 ||
        v.surfaceAmbientFloor < 0 || v.surfaceAmbientFloor > 1) return false;
    auto safe = v;
    safe.light = stage6light::Sanitize(v.light);
    safe.environment = stage8environment::Sanitize(v.environment);
    safe.atmosphere = stage14atmosphere::Sanitize(v.atmosphere);
    safe.ground = stage14ground::Sanitize(v.ground);
    safe.tone = stage14tone::Sanitize(v.tone);
    return LightingPresetEqual(v, safe);
}
std::filesystem::path LightingPresetPath(const std::filesystem::path& root, Stage15ConceptPreset preset)
{
    return root / "lighting" / (std::to_string(LightingPresetSlot(preset)) + ".json");
}
bool SaveLightingPreset(const std::filesystem::path& root, Stage15ConceptPreset preset,
    const LightingPresetSettings& settings, std::string& status)
{
    if (!IsLightingPreset(preset) || !ValidateLightingPreset(settings)) {
        status = "Save rejected: invalid lighting settings"; return false;
    }
    const auto path = LightingPresetPath(root, preset);
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) { status = "Save failed: directory unavailable"; return false; }
    auto temporary = path; temporary += L".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.imbue(std::locale::classic());
        out << std::setprecision(std::numeric_limits<float>::max_digits10)
            << "{\n  \"schemaVersion\": 1,\n  \"slot\": " << LightingPresetSlot(preset);
        Visit(settings, [&](const char* name, auto value) {
            out << ",\n  \"" << name << "\": " << static_cast<double>(value);
        });
        out << "\n}\n"; out.flush();
        if (!out) { out.close(); std::filesystem::remove(temporary, error);
            status = "Save failed: incomplete temporary file"; return false; }
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temporary, error);
        status = "Save failed: atomic replacement failed"; return false;
    }
    status = "Lighting preset saved"; return true;
}
bool ResolveLightingPreset(const std::filesystem::path& root, Stage15ConceptPreset preset,
    bool useSaved, LightingPresetSettings& settings, CloudFormationPresetSource& source, std::string& status)
{
    if (!IsLightingPreset(preset)) { status = "Invalid lighting slot"; return false; }
    const auto path = LightingPresetPath(root, preset);
    std::error_code error;
    const bool exists = useSaved && std::filesystem::exists(path, error);
    if (error) { status = "Cannot inspect lighting file"; return false; }
    if (!exists) {
        settings = BuiltInLightingPreset(preset); source = CloudFormationPresetSource::BuiltIn;
        status = "Built-in lighting preset loaded"; return true;
    }
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0 || size > 1024*1024) { status = "Invalid lighting file size"; return false; }
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer; buffer << in.rdbuf();
    std::map<std::string, double> values;
    if (!in || !Parse(buffer.str(), values) || values["schemaVersion"] != 1 ||
        values["slot"] != LightingPresetSlot(preset)) {
        status = "Invalid lighting JSON or schema/slot"; return false;
    }
    values.erase("schemaVersion"); values.erase("slot");
    LightingPresetSettings candidate;
    bool valid = true;
    Visit(candidate, [&](const char* name, auto& field) {
        const auto found = values.find(name);
        if (found == values.end()) { valid = false; return; }
        using T = std::decay_t<decltype(field)>;
        const double n = found->second;
        if constexpr (std::is_enum_v<T> || std::is_integral_v<T>) {
            if (n < 0 || n > 100 || std::floor(n) != n) { valid = false; return; }
        }
        else if (std::abs(n) > std::numeric_limits<float>::max()) { valid = false; return; }
        field = static_cast<T>(n);
        values.erase(found);
    });
    if (!valid || !values.empty() || !ValidateLightingPreset(candidate)) {
        status = "Invalid lighting fields/ranges"; return false;
    }
    candidate.light.directionToSun = stage6light::DirectionFromAngles(candidate.atmosphere.sunAzimuthDegrees, candidate.atmosphere.sunElevationDegrees);
    settings = candidate; source = CloudFormationPresetSource::UserOverride;
    status = "Saved lighting preset loaded"; return true;
}

