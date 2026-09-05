#include "ShaderManifest.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>

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

void Write(const std::filesystem::path& path, const char* contents)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << contents;
    Require(static_cast<bool>(stream), "temporary shader write");
}
}

int main()
{
    const auto serial = std::chrono::high_resolution_clock::now()
        .time_since_epoch().count();
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("vcloud-shader-manifest-" + std::to_string(serial));
    std::filesystem::create_directories(root / "include");
    Write(root / "a.hlsl", "#include \"include/shared.hlsli\"\nfloat4 main():SV_TARGET{return Shared();}\n");
    Write(root / "b.hlsl", "float4 main():SV_TARGET{return 0;}\n");
    Write(root / "include/shared.hlsli", "#include \"leaf.hlsli\"\nfloat4 Shared(){return Leaf();}\n");
    Write(root / "include/leaf.hlsli", "float4 Leaf(){return 1;}\n");

    std::vector<shaderreload::Program> programs = {
        { shaderreload::ProgramId::CloudPs, "A", "a.hlsl", "main",
          "ps_5_0", shaderreload::ObjectType::PixelShader },
        { shaderreload::ProgramId::ToneMapPs, "B", "b.hlsl", "main",
          "ps_5_0", shaderreload::ObjectType::PixelShader },
    };
    std::string error;
    Require(shaderreload::RefreshDependencies(root, programs[0], error) &&
            shaderreload::RefreshDependencies(root, programs[1], error),
            "dependency closure builds");
    Require(shaderreload::SelectAffectedPrograms(
                programs, { "a.hlsl" }).size() == 1u,
            "direct source change selects one program");
    Require(shaderreload::SelectAffectedPrograms(
                programs, { "include/shared.hlsli" }).size() == 1u,
            "direct include change selects one program");
    Require(shaderreload::SelectAffectedPrograms(
                programs, { "include/leaf.hlsli" }).size() == 1u,
            "transitive include change selects one program");

    const auto previousDependencies = programs[0].dependencies;
    std::filesystem::remove(root / "include/leaf.hlsli");
    Require(shaderreload::SelectAffectedPrograms(
                programs, { "include/leaf.hlsli" }).size() == 1u,
            "removed include remains selectable from accepted closure");
    Require(!shaderreload::RefreshDependencies(root, programs[0], error) &&
            programs[0].dependencies == previousDependencies,
            "failed refresh preserves accepted dependency closure");

    std::vector<std::filesystem::path> includes;
    Require(!shaderreload::ParseLiteralIncludes(
                "#include SHADER_FILE\n", includes),
            "macro include is rejected");
    std::filesystem::remove_all(root);

#ifdef VCLOUD_SHADER_SOURCE_DIR
    auto highPrograms = shaderreload::MakeHighManifest(true);
    const std::filesystem::path highRoot = VCLOUD_SHADER_SOURCE_DIR;
    for (auto& program : highPrograms)
        Require(shaderreload::RefreshDependencies(
                    highRoot, program, error),
                "repository shader dependencies build");
    const auto noiseAffected = shaderreload::SelectAffectedPrograms(
        highPrograms, { "noise.hlsli" });
    Require(noiseAffected.size() == 3u,
            "Noise.hlsli must select exactly Cloud, Deep Shadow, NoiseLab");
    std::set<shaderreload::ProgramId> ids;
    for (std::size_t index : noiseAffected)
        ids.insert(highPrograms[index].id);
    Require(ids == std::set<shaderreload::ProgramId>{
                shaderreload::ProgramId::CloudPs,
                shaderreload::ProgramId::NoiseLabPs,
                shaderreload::ProgramId::DeepShadowCs },
            "Noise.hlsli affected program identity");
#endif

    std::cout << "ShaderManifest passed\n";
    return 0;
}
