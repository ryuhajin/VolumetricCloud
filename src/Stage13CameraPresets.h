// ============================================================================
//  Stage13CameraPresets.h - 단계 13 Open World 사용자/진단 카메라 단일 기준
// ============================================================================
#pragma once

#include <DirectXMath.h>
#include <array>
#include <cstddef>

enum class Stage13CameraPresetId : std::size_t
{
    HeroDepth = 0,
    GroundHorizon,
    InsideLayer,
    AboveLayer,
};

struct Stage13CameraPreset
{
    const char* diagnosticName;
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 target;
};

namespace stage13camera
{
inline constexpr float kNearPlaneMeters = 0.1f;
inline constexpr float kFarPlaneMeters = 60000.0f;

// 단일 포트폴리오 씬의 F5~F8이다. 지면과 건물은 모든 구도에서 항상 같은
// Scene Color/Depth에 렌더링되며 카메라 프리셋은 위치와 시선만 바꾼다.
inline constexpr std::array<Stage13CameraPreset, 4> kOpenWorldPresets = {{
    { "HeroDepth",     {  0.0f,   15.0f, 180.0f }, {  0.0f,  350.0f, -1200.0f } },
    { "GroundHorizon", { 40.0f,    2.0f,   0.0f }, { 40.0f,  700.0f, -2200.0f } },
    { "InsideLayer",   { 40.0f, 3000.0f,   0.0f }, { 40.0f, 3300.0f, -1600.0f } },
    { "AboveLayer",    { 40.0f, 7800.0f,   0.0f }, { 40.0f, 6000.0f, -1800.0f } },
}};

inline constexpr const Stage13CameraPreset& Get(Stage13CameraPresetId id)
{
    return kOpenWorldPresets[static_cast<std::size_t>(id)];
}

}
