#include "DeveloperUiSettings.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iterator>

namespace
{
bool IsJsonValueEnd(const char* end)
{
    while (*end != '\0' && std::isspace(static_cast<unsigned char>(*end)))
        ++end;
    return *end == ',' || *end == '}';
}

bool FindValueStart(const std::string& text, const char* key,
                    std::size_t& valueStart)
{
    const std::string quotedKey = std::string("\"") + key + "\"";
    const std::size_t keyPosition = text.find(quotedKey);
    if (keyPosition == std::string::npos)
        return false;
    const std::size_t colon = text.find(':', keyPosition + quotedKey.size());
    if (colon == std::string::npos)
        return false;
    valueStart = text.find_first_not_of(" \t\r\n", colon + 1u);
    return valueStart != std::string::npos;
}

bool ParseUnsigned(const std::string& text, const char* key,
                   unsigned int& value)
{
    std::size_t start = 0;
    if (!FindValueStart(text, key, start))
        return false;
    errno = 0;
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(text.c_str() + start, &end, 10);
    if (errno != 0 || end == text.c_str() + start || !IsJsonValueEnd(end) ||
        parsed > static_cast<unsigned long>(UINT_MAX))
    {
        return false;
    }
    value = static_cast<unsigned int>(parsed);
    return true;
}

bool ParseFloat(const std::string& text, const char* key, float& value)
{
    std::size_t start = 0;
    if (!FindValueStart(text, key, start))
        return false;
    errno = 0;
    char* end = nullptr;
    const float parsed = std::strtof(text.c_str() + start, &end);
    if (errno != 0 || end == text.c_str() + start ||
        !IsJsonValueEnd(end) || !std::isfinite(parsed))
        return false;
    value = parsed;
    return true;
}

void RemoveTemporary(const std::filesystem::path& path)
{
    std::error_code error;
    std::filesystem::remove(path, error);
}
}

namespace developerui
{
float SanitizeUserZoom(float value)
{
    if (!std::isfinite(value))
        return kDefaultUserZoom;
    const float clamped = std::clamp(
        value, kMinimumUserZoom, kMaximumUserZoom);
    const float stepIndex = std::round(
        (clamped - kMinimumUserZoom) / kUserZoomStep);
    return std::clamp(
        kMinimumUserZoom + stepIndex * kUserZoomStep,
        kMinimumUserZoom, kMaximumUserZoom);
}

float EffectiveScale(unsigned int dpi, float userZoom)
{
    const unsigned int safeDpi = std::max(dpi, 96u);
    return static_cast<float>(safeDpi) / 96.0f *
        SanitizeUserZoom(userZoom);
}

bool Load(const std::filesystem::path& path, Settings& outSettings,
          std::string& status)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        status = "Developer UI settings not found; using defaults";
        return false;
    }
    const std::string text(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    unsigned int schemaVersion = 0u;
    float userZoom = kDefaultUserZoom;
    if (!ParseUnsigned(text, "schemaVersion", schemaVersion) ||
        schemaVersion != kSchemaVersion ||
        !ParseFloat(text, "userZoom", userZoom))
    {
        status = "Developer UI settings rejected: expected schema 1";
        return false;
    }

    Settings candidate;
    candidate.userZoom = SanitizeUserZoom(userZoom);
    outSettings = candidate;
    status = "Developer UI settings loaded";
    return true;
}

bool SaveAtomic(const std::filesystem::path& path, const Settings& settings,
                std::string& status)
{
    std::error_code error;
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty())
    {
        std::filesystem::create_directories(parent, error);
        if (error)
        {
            status = "Developer UI settings save failed: cannot create directory";
            return false;
        }
    }

    const std::filesystem::path temporary = path.wstring() + L".tmp";
    {
        std::ofstream output(
            temporary, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            status = "Developer UI settings save failed: cannot open temporary file";
            return false;
        }
        output << "{\n"
               << "  \"schemaVersion\": " << kSchemaVersion << ",\n"
               << "  \"userZoom\": " << std::fixed << std::setprecision(2)
               << SanitizeUserZoom(settings.userZoom) << "\n"
               << "}\n";
        output.flush();
        if (!output)
        {
            output.close();
            RemoveTemporary(temporary);
            status = "Developer UI settings save failed: incomplete temporary file";
            return false;
        }
    }

    if (!MoveFileExW(
            temporary.c_str(), path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        RemoveTemporary(temporary);
        status = "Developer UI settings save failed: atomic replace failed";
        return false;
    }
    status = "Developer UI settings saved";
    return true;
}
}
