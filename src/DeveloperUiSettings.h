// ============================================================================
//  DeveloperUiSettings.h - 렌더 상태와 분리된 개발 UI 배율 설정
// ============================================================================
#pragma once

#include <filesystem>
#include <string>

namespace developerui
{
constexpr unsigned int kSchemaVersion = 1u;
constexpr float kDefaultUserZoom = 1.0f;
constexpr float kMinimumUserZoom = 1.0f;
constexpr float kMaximumUserZoom = 2.0f;
constexpr float kUserZoomStep = 0.05f;

struct Settings
{
    float userZoom = kDefaultUserZoom;
};

// UI에서 드래그한 값과 파일에서 읽은 값을 같은 5% 격자에 맞춘다.
float SanitizeUserZoom(float value);

// PMv2의 물리 DPI와 사용자가 선택한 추가 확대를 한 번만 곱한다.
float EffectiveScale(unsigned int dpi, float userZoom);

// 파일이 없거나 손상됐을 때 outSettings는 변경하지 않는다. 호출자는 기본값을
// 계속 사용할 수 있고, status로 실패 원인을 F4에 표시한다.
bool Load(const std::filesystem::path& path, Settings& outSettings,
          std::string& status);

// 같은 디렉터리의 .tmp를 완전히 기록한 뒤 ReplaceExisting으로 교체한다.
bool SaveAtomic(const std::filesystem::path& path, const Settings& settings,
                std::string& status);
}
