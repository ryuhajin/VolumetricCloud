// ============================================================================
//  Stage13CameraPresets.h - 단계 13 Open World 사용자/진단 카메라 단일 기준
// ============================================================================
#pragma once

#include <DirectXMath.h>
#include <array>
#include <cstddef>
#include <algorithm>
#include <cmath>

enum class Stage13CameraPresetId : std::size_t
{
    HeroDepth = 0,
    GroundHorizon,
    CloudOverview,
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
// 수직60도: 16:9에서 수평 약91도. 지상/상공 모두 같은 원근으로 비교한다.
inline constexpr float kFovYDegrees = 60.0f;
inline constexpr float kNearPlaneMeters = 0.1f;
inline constexpr float kFarPlaneMeters = 60000.0f;

// 단일 포트폴리오 씬의 F5~F8이다. 지면과 건물은 모든 구도에서 항상 같은
// Scene Color/Depth에 렌더링되며 카메라 프리셋은 위치와 시선만 바꾼다.
inline constexpr std::array<Stage13CameraPreset, 4> kOpenWorldPresets = {{
    { "HeroDepth",     {  0.0f,    1.7f,  60.0f }, {  0.0f,   42.0f,  -40.0f } },
    { "GroundHorizon", {  0.0f,    1.7f,  60.0f }, {  0.0f,   42.0f,  160.0f } },
    { "CloudOverview", { 40.0f, 7800.0f,   0.0f }, { 40.0f, 3000.0f, -3000.0f } },
    { "AboveLayer",    { 40.0f, 7800.0f,   0.0f }, { 40.0f, 7417.4f, -1800.0f } },
}};

inline constexpr const Stage13CameraPreset& Get(Stage13CameraPresetId id)
{
    return kOpenWorldPresets[static_cast<std::size_t>(id)];
}

// F7을 누른 순간의 태양 방향에서 구름 중심을 본다. 저고도/야간에는
// 지면/구름 내부로 들어가지 않도록 고도각 약14.5도 이상을 확보한다.
inline Stage13CameraPreset FromSunDirection(const DirectX::XMFLOAT3& sun)
{
    float x=sun.x, y=sun.y, z=sun.z;
    float length=std::sqrt(x*x+y*y+z*z);
    if (!std::isfinite(length) || length<1e-6f) { x=0; y=.5f; z=-.8660254f; length=1; }
    x/=length; y/=length; z/=length;
    y=std::clamp(y,.25f,.9999f);
    float horizontal=std::sqrt(x*x+z*z);
    if(horizontal<1e-6f) { x=0; z=-1; horizontal=1; }
    float scale=std::sqrt(1-y*y)/horizontal;
    x*=scale; z*=scale;
    const DirectX::XMFLOAT3 target{0,3000,-1200};
    return {"SunOverview", {target.x+x*20000, target.y+y*20000, target.z+z*20000},target};
}

}
