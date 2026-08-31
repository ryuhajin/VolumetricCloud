// ============================================================================
//  ShaderManifest.h - High 단일 셰이더 프로그램과 literal include 의존성
// ============================================================================
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace shaderreload
{
enum class ProgramId : std::uint32_t
{
    FullscreenVs,
    CloudPs,
    NoiseLabPs,
    SceneVs,
    ScenePs,
    ToneMapPs,
    NoiseBaseCs,
    NoiseDetailCs,
    DeepShadowCs,
    AtmosphereTransmittanceCs,
    AtmosphereMultiScatteringCs,
    AtmosphereSkyViewCs,
    AtmosphereSkyIrradianceCs,
    AtmosphereAerialCs,
};

enum class ObjectType : std::uint32_t
{
    VertexShader,
    PixelShader,
    ComputeShader,
};

enum Invalidation : std::uint32_t
{
    InvalidateNone = 0u,
    InvalidateAtmosphereLuts = 1u << 0u,
    InvalidateDeepShadow = 1u << 1u,
    InvalidateNoiseVolumes = 1u << 2u,
};

struct Define
{
    std::string name;
    std::string value;
};

struct Program
{
    ProgramId id = ProgramId::FullscreenVs;
    std::string name;
    std::filesystem::path source;
    std::string entry;
    std::string target;
    ObjectType object = ObjectType::VertexShader;
    std::vector<Define> defines;
    std::set<std::string> dependencies;
    std::uint32_t invalidation = InvalidateNone;
};

struct ReloadReport
{
    bool attempted = false;
    bool succeeded = false;
    std::vector<std::string> changedFiles;
    std::size_t affectedPrograms = 0;
    std::uint64_t compileCount = 0;
    std::uint64_t cacheHits = 0;
    double elapsedMilliseconds = 0.0;
};

inline std::string NormalizeRelativePath(const std::filesystem::path& path)
{
    std::string value = path.lexically_normal().generic_string();
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
    return value;
}

inline bool IsInsideRoot(const std::filesystem::path& root,
                         const std::filesystem::path& path,
                         std::filesystem::path& relative)
{
    std::error_code error;
    relative = std::filesystem::relative(path, root, error);
    if (error || relative.empty() || relative.is_absolute())
        return false;
    for (const auto& component : relative)
    {
        if (component == L"..")
            return false;
    }
    return true;
}

inline bool ParseLiteralIncludes(
    std::string_view source, std::vector<std::filesystem::path>& includes)
{
    includes.clear();
    std::size_t lineStart = 0;
    while (lineStart < source.size())
    {
        std::size_t lineEnd = source.find('\n', lineStart);
        if (lineEnd == std::string_view::npos)
            lineEnd = source.size();
        std::size_t cursor = lineStart;
        if (lineStart == 0 && lineEnd >= 3 &&
            static_cast<unsigned char>(source[0]) == 0xefu &&
            static_cast<unsigned char>(source[1]) == 0xbbu &&
            static_cast<unsigned char>(source[2]) == 0xbfu)
            cursor = 3;
        while (cursor < lineEnd &&
               (source[cursor] == ' ' || source[cursor] == '\t' ||
                source[cursor] == '\v' || source[cursor] == '\f' ||
                source[cursor] == '\r'))
            ++cursor;
        if (cursor >= lineEnd || source[cursor++] != '#')
        {
            lineStart = lineEnd + (lineEnd < source.size() ? 1u : 0u);
            continue;
        }
        while (cursor < lineEnd &&
               (source[cursor] == ' ' || source[cursor] == '\t'))
            ++cursor;
        constexpr std::string_view directive = "include";
        if (cursor + directive.size() > lineEnd ||
            source.substr(cursor, directive.size()) != directive)
        {
            lineStart = lineEnd + (lineEnd < source.size() ? 1u : 0u);
            continue;
        }
        cursor += directive.size();
        if (cursor < lineEnd &&
            (std::isalnum(static_cast<unsigned char>(source[cursor])) ||
             source[cursor] == '_'))
        {
            lineStart = lineEnd + (lineEnd < source.size() ? 1u : 0u);
            continue;
        }
        while (cursor < lineEnd &&
               (source[cursor] == ' ' || source[cursor] == '\t'))
            ++cursor;
        if (cursor >= lineEnd ||
            (source[cursor] != '"' && source[cursor] != '<'))
            return false;
        const char terminator = source[cursor] == '"' ? '"' : '>';
        const std::size_t begin = ++cursor;
        const std::size_t end = source.find(terminator, begin);
        if (end == std::string_view::npos || end >= lineEnd || end == begin)
            return false;
        includes.emplace_back(std::filesystem::u8path(
            std::string(source.substr(begin, end - begin))));
        lineStart = lineEnd + (lineEnd < source.size() ? 1u : 0u);
    }
    return true;
}

inline bool CollectDependencyClosure(
    const std::filesystem::path& root,
    const std::filesystem::path& source,
    std::set<std::string>& dependencies,
    std::string& errorMessage)
{
    std::error_code error;
    const std::filesystem::path canonicalRoot =
        std::filesystem::canonical(root, error);
    if (error)
    {
        errorMessage = "shader root is unavailable";
        return false;
    }

    const auto visit = [&](const auto& self,
                           const std::filesystem::path& input) -> bool
    {
        std::error_code localError;
        const std::filesystem::path canonical =
            std::filesystem::canonical(input, localError);
        if (localError || !std::filesystem::is_regular_file(
                canonical, localError) || localError)
        {
            errorMessage = "missing shader dependency: " + input.string();
            return false;
        }
        std::filesystem::path relative;
        if (!IsInsideRoot(canonicalRoot, canonical, relative))
        {
            errorMessage = "shader dependency escaped root";
            return false;
        }
        const std::string normalized = NormalizeRelativePath(relative);
        if (!dependencies.insert(normalized).second)
            return true;

        std::ifstream stream(canonical, std::ios::binary);
        if (!stream)
        {
            errorMessage = "shader dependency could not be read";
            return false;
        }
        const std::string contents(
            (std::istreambuf_iterator<char>(stream)),
            std::istreambuf_iterator<char>());
        std::vector<std::filesystem::path> includes;
        if (!ParseLiteralIncludes(contents, includes))
        {
            errorMessage = "shader include must be a literal local path";
            return false;
        }
        for (const auto& include : includes)
        {
            const std::filesystem::path candidates[] = {
                canonical.parent_path() / include,
                canonicalRoot / include,
            };
            std::filesystem::path resolved;
            for (const auto& candidate : candidates)
            {
                const std::filesystem::path candidateCanonical =
                    std::filesystem::canonical(candidate, localError);
                if (!localError && std::filesystem::is_regular_file(
                        candidateCanonical, localError) && !localError)
                {
                    resolved = candidateCanonical;
                    break;
                }
                localError.clear();
            }
            if (resolved.empty() || !self(self, resolved))
            {
                if (errorMessage.empty())
                    errorMessage = "shader include could not be resolved";
                return false;
            }
        }
        return true;
    };

    dependencies.clear();
    errorMessage.clear();
    return visit(visit, canonicalRoot / source);
}

inline bool RefreshDependencies(const std::filesystem::path& root,
                                Program& program,
                                std::string& errorMessage)
{
    std::set<std::string> candidate;
    if (!CollectDependencyClosure(root, program.source, candidate,
                                  errorMessage))
        return false;
    program.dependencies = std::move(candidate);
    return true;
}

inline std::vector<std::size_t> SelectAffectedPrograms(
    const std::vector<Program>& programs,
    const std::set<std::string>& changedFiles)
{
    std::vector<std::size_t> result;
    for (std::size_t index = 0; index < programs.size(); ++index)
    {
        const auto& dependencies = programs[index].dependencies;
        const bool affected = std::any_of(
            changedFiles.begin(), changedFiles.end(),
            [&](const std::string& changed)
            {
                return dependencies.find(changed) != dependencies.end();
            });
        if (affected)
            result.push_back(index);
    }
    return result;
}

inline std::vector<Program> MakeHighManifest(bool includeNoiseVolume)
{
    using Id = ProgramId;
    using Type = ObjectType;
    std::vector<Program> result = {
        { Id::FullscreenVs, "Fullscreen VS", "Fullscreen.hlsl", "main",
          "vs_5_0", Type::VertexShader },
        { Id::CloudPs, "Cloud PS", "VolumetricClouds.hlsl", "main",
          "ps_5_0", Type::PixelShader },
        { Id::NoiseLabPs, "NoiseLab PS", "NoiseLab.hlsl", "main",
          "ps_5_0", Type::PixelShader },
        { Id::SceneVs, "Scene VS", "DiagnosticScene.hlsl", "VSMain",
          "vs_5_0", Type::VertexShader },
        { Id::ScenePs, "Scene PS", "DiagnosticScene.hlsl", "PSMain",
          "ps_5_0", Type::PixelShader },
        { Id::ToneMapPs, "Tone Map PS", "Stage14ToneMap.hlsl", "main",
          "ps_5_0", Type::PixelShader },
        { Id::DeepShadowCs, "Deep Shadow CS", "CloudDeepShadow.hlsl", "main",
          "cs_5_0", Type::ComputeShader, {}, {}, InvalidateDeepShadow },
        { Id::AtmosphereTransmittanceCs, "Atmosphere Transmittance CS",
          "Stage14AtmosphereLut.hlsl", "CSTransmittance", "cs_5_0",
          Type::ComputeShader, {}, {}, InvalidateAtmosphereLuts },
        { Id::AtmosphereMultiScatteringCs, "Atmosphere Multi Scattering CS",
          "Stage14AtmosphereLut.hlsl", "CSMultiScattering", "cs_5_0",
          Type::ComputeShader, {}, {}, InvalidateAtmosphereLuts },
        { Id::AtmosphereSkyViewCs, "Atmosphere Sky View CS",
          "Stage14AtmosphereLut.hlsl", "CSSkyView", "cs_5_0",
          Type::ComputeShader, {}, {}, InvalidateAtmosphereLuts },
        { Id::AtmosphereSkyIrradianceCs, "Atmosphere Sky Irradiance CS",
          "Stage14AtmosphereLut.hlsl", "CSSkyIrradiance", "cs_5_0",
          Type::ComputeShader, {}, {}, InvalidateAtmosphereLuts },
        { Id::AtmosphereAerialCs, "Atmosphere Aerial CS",
          "Stage14AtmosphereLut.hlsl", "CSAerialPerspective", "cs_5_0",
          Type::ComputeShader, {}, {}, InvalidateAtmosphereLuts },
    };
    if (includeNoiseVolume)
    {
        result.push_back({ Id::NoiseBaseCs, "Noise Base CS",
            "NoiseVolume.hlsl", "CSBase", "cs_5_0", Type::ComputeShader,
            {}, {}, InvalidateNoiseVolumes });
        result.push_back({ Id::NoiseDetailCs, "Noise Detail CS",
            "NoiseVolume.hlsl", "CSDetail", "cs_5_0", Type::ComputeShader,
            {}, {}, InvalidateNoiseVolumes });
    }
    return result;
}
}
