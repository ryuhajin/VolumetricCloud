// ============================================================================
//  AtmosphereParameters.h - 단계 14 물리 대기와 태양 제어 CPU 설정
// ============================================================================
#pragma once

#include <DirectXMath.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

enum class AtmospherePreset : std::uint32_t
{
    EarthClear,
    EarthHazy,
    Custom,
};

enum class SunControlMode : std::uint32_t
{
    Angles,
    TimeOfDay,
};

enum class Stage14DebugView : std::uint32_t
{
    None,
    Transmittance,
    OneMinusTransmittance,
    OpticalDepth,
    MultiScattering,
    SkyView,
    SkyIrradiance,
    AerialRadiance,
    AerialTransmittance,
    AtmosphereSun,
    SurfaceDirect,
    SurfaceSky,
    AerialOnly,
    HdrPreTone,
};

enum class Stage14DebugChannel : std::uint32_t
{
    Rgb,
    Red,
    Green,
    Blue,
};

struct AtmosphereParameters
{
    // [직접 조절] EarthClear/EarthHazy/Custom 열거값. 기본/권장 EarthClear; preset 적용은 물리 파라미터를 다시 덮어쓴다.
    AtmospherePreset preset = AtmospherePreset::EarthClear;
    // [파생 값] timePlaybackEnabled로 Angles/TimeOfDay를 결정. 독립 조절값 아님.
    SunControlMode sunControlMode = SunControlMode::Angles;

    // [직접 조절] Atmosphere 코드의 행성 반지름 km. [강제 범위] [1,100000], 기본/권장 Earth 6360. 곡률과 지평선이 달라진다.
    float bottomRadiusKm = 6360.0f;
    // [직접 조절] 대기 외곽 반지름 km. [강제 범위] [bottom+1,bottom+1000], 기본/권장 6460. 증가하면 대기 적분 영역이 늘어난다.
    float topRadiusKm = 6460.0f;
    // [직접 조절] 분자 밀도가 1/e가 되는 높이 km. [강제 범위] [4,16], 기본/권장 8. 증가하면 높은 곳에도 분자 산란이 남는다.
    float rayleighScaleHeightKm = 8.0f;
    // [직접 조절] 에어로졸 밀도의 1/e 높이 km. [강제 범위] [0.5,4], 기본/권장 2.0. 증가하면 연무가 위로 퍼진다.
    float mieScaleHeightKm = 2.0f;

    // [직접 조절] xyz=선형 RGB별 분자 산란 계수 1/km. 유한 [0,무제한), 기본/권장 (0.005802,0.013558,0.033100); 해당 색 산란/소멸 증가.
    DirectX::XMFLOAT3 rayleighScatteringPerKm = {
        0.005802f, 0.013558f, 0.033100f
    };
    // [직접 조절] F3 분자 산란 배율. [강제 범위]/UI [0.25,4], 기본 1 권장. 증가하면 푸른 하늘/대기 영향이 강해진다.
    float rayleighScale = 1.0f;

    // [직접 조절] 에어로졸 산란 1/km. 유한 [0,무제한), 기본/권장 0.003996. 증가하면 태양 주변 연무 산란이 늘어난다.
    float mieScatteringPerKm = 0.003996f;
    // [직접 조절] 에어로졸 산란+흡수 1/km. [강제 범위] [mieScatteringPerKm,무제한), 기본/권장 0.004440. 차이가 흡수량이다.
    float mieExtinctionPerKm = 0.004440f;
    // [직접 조절] 에어로졸 흡수 배율. [강제 범위] [0,4], 기본/권장 1. 증가하면 빛이 더 흡수되며 산란 자체는 늘지 않는다.
    float mieAbsorptionScale = 1.0f;
    // [직접 조절] 대기 Mie 방향 비대칭도. [강제 범위] [0,0.95], 기본/권장 0.3. 증가하면 태양 방향 봉우리가 좁아진다.
    float mieG = 0.3f;

    // [직접 조절] xyz=RGB별 오존 흡수 1/km. 유한 [0,무제한), 기본/권장 (0.000650,0.001881,0.000085); 해당 색 빛이 더 줄어든다.
    DirectX::XMFLOAT3 ozoneAbsorptionPerKm = {
        0.000650f, 0.001881f, 0.000085f
    };
    // [직접 조절] F3 오존 흡수 배율. [강제 범위]/UI [0,4], 기본/권장 1. 증가하면 흡수에 의한 대기 색 변화가 커진다.
    float ozoneScale = 1.0f;
    // [직접 조절] 오존 삼각형 밀도층 중심 고도 km. [강제 범위] [0,100], 기본/권장 25. 증가하면 흡수층이 상승한다.
    float ozoneCenterKm = 25.0f;
    // [직접 조절] 오존 밀도층 반폭 km. [강제 범위] [0.1,100], 기본/권장 15. 증가하면 오존 흡수가 더 넓은 고도에 분포한다.
    float ozoneHalfWidthKm = 15.0f;
    // [직접 조절] F3 Mie 산란/소멸 배율. [강제 범위]/UI [0.25,4], 기본/권장 맑음 1/연무 2.5. 증가하면 원경 연무가 강해진다.
    float turbidity = 1.0f;

    // [직접 조절] xyz=대기 상단 태양 RGB 에너지 기준. 유한 [0,무제한), 기본/권장 (1,1,1). 증가하면 LUT 광원 에너지가 증가한다.
    DirectX::XMFLOAT3 solarIrradiance = { 1.0f, 1.0f, 1.0f };
    // [직접 조절] F3 수평 방위각 도. [강제 범위]/UI [-180,180], 기본 -60. +X가 0도, +Z가 90도; 수평으로 태양/그림자가 회전한다.
    float sunAzimuthDegrees = -60.0f;
    // [직접 조절] F3 고도각 도. 대기 sanitize [-6,90], UI와 Light 방향 생성 [0,90], 기본 18. 3도 미만은 cache fallback 조건.
    float sunElevationDegrees = 18.0f;
    // [직접 조절: 코드 전용] 태양 재생 시각 hour [5.5,19.5], 기본 7.5. 현재 F3 스크럽 UI 없음; cloud advection 시간과 다르다.
    float timeOfDayHours = 7.5f;
    // [직접 조절: 코드 전용] 실제 1초당 태양 시각 minute [30,120], 기본 30. 증가하면 태양 재생이 빨라진다.
    float timePlaybackMinutesPerSecond = 30.0f;
    // [직접 조절: 코드 전용] bool 기본 false. true면 Renderer가 태양 각도를 시각 경로로 갱신한다; 구름 바람은 별개.
    bool timePlaybackEnabled = false;
    // schema 35 호환 필드다. Stage 14 통합 UI부터 시간 재생은 항상 순환한다.
    // [고정 품질] 호환 bool, sanitize가 true로 고정. 태양 시간은 반복하며 현재 UI 없음.
    bool timeLoopEnabled = true;

    // [직접 조절: 진단] F4 Stage14DebugView 열거값 0~13, 기본 None. 선택한 LUT/조명/HDR을 표시하며 연속 배율이 아니다.
    Stage14DebugView debugView = Stage14DebugView::None;
    // [직접 조절: 진단] F4 0=RGB,1=R,2=G,3=B; 기본 RGB. 물리 에너지는 그대로 두고 표시 채널만 선택.
    Stage14DebugChannel debugChannel = Stage14DebugChannel::Rgb;
    // [직접 조절: 진단] F4 표시 배율 [0.001,128], 기본 1. 증가하면 tau/HDR 진단이 밝아지며 최종 노출 EV와 별개.
    float debugExposure = 1.0f;
    // [직접 조절: 진단] F4 Aerial 깊이 slice [0,31], 기본 0, GPU uint로 변환. 증가하면 더 먼 누적 대기를 표시.
    float aerialSlice = 0.0f;
};

namespace stage14atmosphere
{
inline float FiniteOr(float value, float fallback)
{
    return std::isfinite(value) ? value : fallback;
}

inline AtmosphereParameters Sanitize(AtmosphereParameters value)
{
    value.bottomRadiusKm = std::clamp(
        FiniteOr(value.bottomRadiusKm, 6360.0f), 1.0f, 100000.0f);
    value.topRadiusKm = std::clamp(
        FiniteOr(value.topRadiusKm, 6460.0f),
        value.bottomRadiusKm + 1.0f, value.bottomRadiusKm + 1000.0f);
    value.rayleighScaleHeightKm = std::clamp(
        FiniteOr(value.rayleighScaleHeightKm, 8.0f), 4.0f, 16.0f);
    value.mieScaleHeightKm = std::clamp(
        FiniteOr(value.mieScaleHeightKm, 2.0f), 0.5f, 4.0f);
    value.rayleighScale = std::clamp(
        FiniteOr(value.rayleighScale, 1.0f), 0.25f, 4.0f);
    value.mieAbsorptionScale = std::clamp(
        FiniteOr(value.mieAbsorptionScale, 1.0f), 0.0f, 4.0f);
    value.mieG = std::clamp(FiniteOr(value.mieG, 0.3f), 0.0f, 0.95f);
    value.ozoneScale = std::clamp(
        FiniteOr(value.ozoneScale, 1.0f), 0.0f, 4.0f);
    value.ozoneCenterKm = std::clamp(
        FiniteOr(value.ozoneCenterKm, 25.0f), 0.0f, 100.0f);
    value.ozoneHalfWidthKm = std::clamp(
        FiniteOr(value.ozoneHalfWidthKm, 15.0f), 0.1f, 100.0f);
    value.turbidity = std::clamp(
        FiniteOr(value.turbidity, 1.0f), 0.25f, 4.0f);
    value.sunAzimuthDegrees = std::clamp(
        FiniteOr(value.sunAzimuthDegrees, -60.0f), -180.0f, 180.0f);
    value.sunElevationDegrees = std::clamp(
        FiniteOr(value.sunElevationDegrees, 18.0f), -6.0f, 90.0f);
    value.timeOfDayHours = std::clamp(
        FiniteOr(value.timeOfDayHours, 7.5f), 5.5f, 19.5f);
    value.timePlaybackMinutesPerSecond = std::clamp(
        FiniteOr(value.timePlaybackMinutesPerSecond, 30.0f), 30.0f, 120.0f);
    value.timeLoopEnabled = true;
    value.sunControlMode = value.timePlaybackEnabled
        ? SunControlMode::TimeOfDay : SunControlMode::Angles;
    value.debugExposure = std::clamp(
        FiniteOr(value.debugExposure, 1.0f), 0.001f, 128.0f);
    value.aerialSlice = std::clamp(
        FiniteOr(value.aerialSlice, 0.0f), 0.0f, 31.0f);

    auto sanitizeColor = [](DirectX::XMFLOAT3& color,
                            const DirectX::XMFLOAT3& fallback)
    {
        color.x = std::max(FiniteOr(color.x, fallback.x), 0.0f);
        color.y = std::max(FiniteOr(color.y, fallback.y), 0.0f);
        color.z = std::max(FiniteOr(color.z, fallback.z), 0.0f);
    };
    sanitizeColor(value.rayleighScatteringPerKm,
                  { 0.005802f, 0.013558f, 0.033100f });
    sanitizeColor(value.ozoneAbsorptionPerKm,
                  { 0.000650f, 0.001881f, 0.000085f });
    sanitizeColor(value.solarIrradiance, { 1.0f, 1.0f, 1.0f });
    value.mieScatteringPerKm = std::max(
        FiniteOr(value.mieScatteringPerKm, 0.003996f), 0.0f);
    value.mieExtinctionPerKm = std::max(
        FiniteOr(value.mieExtinctionPerKm, 0.004440f),
        value.mieScatteringPerKm);
    return value;
}

inline void ApplyPreset(AtmosphereParameters& value,
                        AtmospherePreset preset)
{
    value.preset = preset;
    if (preset == AtmospherePreset::Custom)
        return;

    value.bottomRadiusKm = 6360.0f;
    value.topRadiusKm = 6460.0f;
    value.rayleighScaleHeightKm = 8.0f;
    value.mieScaleHeightKm = 2.0f;
    value.rayleighScatteringPerKm = {
        0.005802f, 0.013558f, 0.033100f
    };
    value.rayleighScale = 1.0f;
    value.mieScatteringPerKm = 0.003996f;
    value.mieExtinctionPerKm = 0.004440f;
    value.mieAbsorptionScale = 1.0f;
    value.mieG = 0.3f;
    value.ozoneAbsorptionPerKm = {
        0.000650f, 0.001881f, 0.000085f
    };
    value.ozoneScale = 1.0f;
    value.ozoneCenterKm = 25.0f;
    value.ozoneHalfWidthKm = 15.0f;
    value.turbidity = preset == AtmospherePreset::EarthHazy ? 2.5f : 1.0f;
}
}
