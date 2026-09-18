#include "LightingPresetStore.h"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>

namespace {
void Require(bool ok, const char* message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
std::string Read(const std::filesystem::path& path) {
    std::ifstream in(path); return {std::istreambuf_iterator<char>(in), {}};
}
void Write(const std::filesystem::path& path, const std::string& value) {
    std::ofstream out(path); out << value; Require(bool(out), "write fixture");
}
}
int main()
{
    const auto root = std::filesystem::temp_directory_path() /
        ("vcloud-lighting-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::string status;
    std::map<std::filesystem::path, std::string> savedFiles;
    for (unsigned i=3; i<=6; ++i) {
        const auto slot = static_cast<Stage15ConceptPreset>(i);
        LightingPresetSettings v, loaded;
        CloudFormationPresetSource source;
        Require(ResolveLightingPreset(root,slot,true,v,source,status) &&
            source==CloudFormationPresetSource::BuiltIn && ValidateLightingPreset(v), "missing uses built-in");
        v.tone.exposureEv += .123f; v.light.sunColor.y=.654f;
        v.environment.multipleScatteringOctaves=3;
        v.atmosphere.ozoneScale=.777f; v.ground.albedo.x=.321f;
        Require(SaveLightingPreset(root,slot,v,status), "save lighting slot");
        Require(ResolveLightingPreset(root,slot,true,loaded,source,status) &&
            source==CloudFormationPresetSource::UserOverride && LightingPresetEqual(v,loaded), "full lighting round trip");
        const auto path = LightingPresetPath(root,slot);
        const auto saved = Read(path);
        savedFiles[path]=saved;
        for (const auto& file : savedFiles) Require(Read(file.first)==file.second, "other slots unchanged");
        auto bad=v; bad.light.rimIntensity=NAN;
        Require(!SaveLightingPreset(root,slot,bad,status) && Read(path)==saved, "invalid save preserves file");
        bad=v; bad.tone.exposureEv=9;
        Require(!SaveLightingPreset(root,slot,bad,status) && Read(path)==saved, "range validation");
        auto text=saved; text.replace(text.find("\"slot\": ")+8,1,"9"); Write(path,text);
        loaded=v;
        Require(!ResolveLightingPreset(root,slot,true,loaded,source,status) && LightingPresetEqual(v,loaded), "slot mismatch atomic");
        for (const std::string malformed : {
                saved.substr(0,saved.size()-3), saved+"garbage",
                std::string("{\"schemaVersion\":1,\"slot\":1}"),
                std::string("{\"schemaVersion\":1,\"schemaVersion\":1,\"slot\":1}")}) {
            Write(path,malformed);
            Require(!ResolveLightingPreset(root,slot,true,loaded,source,status), "malformed JSON rejected");
        }
        Write(path,saved);
        std::filesystem::create_directory(path.string()+".tmp");
        Require(!SaveLightingPreset(root,slot,v,status) && Read(path)==saved, "IO failure preserves file");
        std::filesystem::remove(path.string()+".tmp");
        auto diagnostics=v; diagnostics.atmosphere.debugView=Stage14DebugView::OpticalDepth;
        diagnostics.atmosphere.timePlaybackEnabled=true;
        Require(LightingPresetEqual(v,diagnostics), "diagnostics and playback excluded");
    }
    // 네 형상 슬롯도 모두 저장 가능하며 없는 Custom은 Snow 기본값을 쓴다.
    CloudFormationSettings snow, loaded;
    CloudFormationPresetSource source;
    Require(ResolveCloudFormationPreset(root,CustomFormationTarget(),true,snow,source,status) &&
        source==CloudFormationPresetSource::BuiltIn, "missing Custom fallback");
    for (unsigned i=0;i<4;++i) {
        auto target=i==3?CustomFormationTarget():TypeFormationTarget(static_cast<CloudFormationType>(i));
        CloudFormationSettings v;
        Require(ResolveCloudFormationPreset(root,target,true,v,source,status), "formation resolves");
        v.coverage=.51f+i*.03f;
        const auto path=CloudFormationPresetPath(root,target);
        Require(SaveCloudFormationPresetAtomic(path,target,v,status) &&
            ResolveCloudFormationPreset(root,target,true,loaded,source,status) &&
            CloudFormationSettingsEqual(v,loaded), "formation slot round trip");
        const auto good=Read(path);
        Write(path,good+"garbage");
        const auto before=loaded;
        Require(!ResolveCloudFormationPreset(root,target,true,loaded,source,status) &&
            CloudFormationSettingsEqual(before,loaded), "formation malformed JSON rollback");
        Write(path,good);
        savedFiles[path]=Read(path);
        for(const auto& file:savedFiles) Require(Read(file.first)==file.second,"formation/lighting file isolation");
    }
    const auto custom=CloudFormationPresetPath(root,CustomFormationTarget());
    auto old=Read(custom);
    const auto marker=old.find("  \"snowDefaultInitialized\": 1,\n");
    Require(marker!=std::string::npos,"atomic migration marker exists");
    old.erase(marker,std::string("  \"snowDefaultInitialized\": 1,\n").size());
    Write(custom,old);
    Require(InitializeSnowCustomPreset(root,status) &&
        Read(custom.string()+".before-snow.bak")==old &&
        LoadCloudFormationPreset(custom,CustomFormationTarget(),loaded,status) &&
        CloudFormationSettingsEqual(snow,loaded), "one-time Snow replacement with exact backup");
    loaded.coverage=.567f;
    Require(SaveCloudFormationPresetAtomic(custom,CustomFormationTarget(),loaded,status), "save after migration");
    auto edited=Read(custom);
    Require(InitializeSnowCustomPreset(root,status) && Read(custom)==edited, "restart never resets edited Custom");
    std::filesystem::remove_all(root);
    std::cout << "Lighting and formation slot persistence passed\n";
}
