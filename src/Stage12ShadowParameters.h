// ============================================================================
//  Stage12ShadowParameters.h - 고정 Balanced512 Deep Cache 공유 계약
// ============================================================================
#pragma once

#include <DirectXMath.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>

// HLSL ShadowCB(b8)의 열 개 16바이트 묶음과 순서가 정확히 같다.
struct alignas(16) Stage12ShadowParameters
{
    // [고정 품질] Near 가로/세로 512 texel. sanitize가 복원하며 단독 수정 금지.
    std::uint32_t nearResolution = 512;
    // [고정 품질] Far 가로/세로 512 texel. Near보다 넓은 월드를 담아 texel당 m가 크다.
    std::uint32_t farResolution = 512;
    // [패딩] 폐기된 미사용 enabled 공간. 실제 표면 그림자는 strength/floor로 조절한다.
    std::uint32_t paddingEnabled = 0;
    // [파생 값] 자원+태양 basis+최소 고도로 결정하는 0/1, 초기 0. 0이면 cone/중립 표면 fallback.
    std::uint32_t cacheReady = 0;

    // [파생 값] 태양 좌표 가로 xyz 단위벡터. 초기 (1,0,0), 매 프레임 basis로 교체. 직접 조절 금지.
    DirectX::XMFLOAT3 lightRight{ 1.0f, 0.0f, 0.0f };
    // [고정 품질] Near 수평 기준 폭 24000m. up 투영 폭은 태양 고도와 층 두께에서 계산한다.
    float nearWidthMeters = 24000.0f;

    // [파생 값] 태양 좌표 세로 xyz 단위벡터. 초기 (0,0,1), 월드 +Y와 다르다. UV 투영에 사용.
    DirectX::XMFLOAT3 lightUp{ 0.0f, 0.0f, 1.0f };
    // [고정 품질] Far 수평 기준 폭 128000m. right 폭과 up 폭은 저고도에서 다르다.
    float farWidthMeters = 128000.0f;

    // [파생 값] 표본→태양 xyz 단위벡터. 초기 (0,1,0); y=sin(고도), 세로 간격을 광선 길이로 바꾼다.
    DirectX::XMFLOAT3 lightForward{ 0.0f, 1.0f, 0.0f };
    // [파생 값] b5 바닥 월드 Y(m), 구조체 초기 1500. cache slice 0의 높이.
    float cloudBottomMeters = 1500.0f;

    // [파생 값] xyz 월드 m, 카메라 중심을 Near texel 크기에 snap. 이동 때 그림자가 미세하게 흔들리는 현상을 줄인다.
    DirectX::XMFLOAT3 nearCenter{};
    // [파생 값] b5 바닥+두께 월드 Y(m), 구조체 초기 7500. 마지막 slice의 높이.
    float cloudTopMeters = 7500.0f;

    // [파생 값] xyz 월드 m, 카메라 중심을 Far texel 크기에 snap. Near와 격자 간격이 다르다.
    DirectX::XMFLOAT3 farCenter{};
    // [고정 품질] 무차원 tau 상한 9.21034037=-ln(0.0001). 이미 거의 불투명한 누적을 제한한다.
    float maximumOpticalDepth = 9.21034037f;

    // [고정 품질] Near 높이 표본 80장. 높이 간 tau는 조회 시 두 slice를 보간한다.
    std::uint32_t nearSliceCount = 80;
    // [고정 품질] 06 사용자 승인: Far 높이 표본 79장. Near와 함께 R32 배열 159MiB.
    std::uint32_t farSliceCount = 79;
    // [파생 값] 현재 CS 출력 0=Near/1=Far, 초기 0, clamp <=1. Renderer가 각 Dispatch 직전에 쓴다.
    std::uint32_t dispatchCascade = 0;
    // [패딩] 16바이트 packing 예약 칸, 0 유지. 화면 효과 없음; 삭제/재배치 금지.
    std::uint32_t padding0 = 0;

    // [고정 품질] 중심 0/가장자리 1인 Near 거리 0.80까지 Near 100%.
    float nearCoreEnd = 0.80f;
    // [고정 품질] Near 거리 0.95에서 Far로 전환 완료. 0.80~0.95에서 부드럽게 혼합.
    float nearBlendEnd = 0.95f;
    // [고정 품질] Far 거리 0.90부터 그림자를 중립 T=1로 희미하게 한다.
    float farFadeStart = 0.90f;
    // [고정 품질] Far 거리 1.00에서 T=1. 영역 밖을 검은 그림자로 늘리지 않는다.
    float farFadeEnd = 1.00f;

    // [직접 조절] F3/scene 그림자 강도 [0,1], 구조체 초기 1/내장 0.40~0.65 권장. 증가하면 지면/건물 그림자 대비 증가.
    float surfaceShadowStrength = 1.0f;
    // [직접 조절] F3/scene 표면 그림자 계수 하한 [0,1], 기본/권장 0.35. 증가하면 깊은 표면 그림자가 밝아진다.
    float surfaceAmbientFloor = 0.35f;
    // [고정 품질] sin(3도)=0.052335956. 낮은 태양에서 긴 적분과 불안정한 cache 투영을 피한다.
    float minimumSunY = 0.052335956f;
    // [직접 조절: 진단] cache 표시 [0.1,20], 기본 4. 1-exp(-tau*exposure)를 밝게 만들며 실제 그림자는 불변.
    float cacheDebugExposure = 4.0f;

    // [직접 조절: 진단] Near 배열 index [0,79], 기본 0. 증가하면 높은 곳의 남은 tau를 표시.
    std::uint32_t debugNearSlice = 0;
    // [직접 조절: 진단] Far 배열 index [0,78], 기본 0. 증가하면 높은 곳의 남은 tau를 표시.
    std::uint32_t debugFarSlice = 0;
    // [패딩] 폐기 기능의 공간. GPU 정렬을 위해 0 유지.
    std::uint32_t padding1 = 0;
    // [패딩] 16바이트 packing 예약 칸, 0 유지. 화면 효과 없음; 삭제/재배치 금지.
    std::uint32_t padding2 = 0;
};

static_assert(sizeof(Stage12ShadowParameters) == 160,
              "Stage12ShadowParameters must match ShadowCB");
static_assert(offsetof(Stage12ShadowParameters, padding1) == 152);

namespace stage12shadow
{
inline constexpr std::uint32_t kResolution = 512;
inline constexpr std::uint32_t kNearSlices = 80;
inline constexpr std::uint32_t kFarSlices = 79;
inline constexpr float kNearWidthMeters = 24000.0f;
inline constexpr float kFarWidthMeters = 128000.0f;
inline constexpr std::uint64_t kCacheBytes =
    static_cast<std::uint64_t>(kResolution) * kResolution *
    (kNearSlices + kFarSlices) * sizeof(float);

inline Stage12ShadowParameters Sanitize(Stage12ShadowParameters value)
{
    value.nearResolution = kResolution;
    value.farResolution = kResolution;
    value.paddingEnabled = 0;
    value.cacheReady = value.cacheReady ? 1u : 0u;
    value.nearWidthMeters = kNearWidthMeters;
    value.farWidthMeters = kFarWidthMeters;
    value.nearSliceCount = kNearSlices;
    value.farSliceCount = kFarSlices;
    value.dispatchCascade = std::min(value.dispatchCascade, 1u);
    value.padding0 = 0u;
    value.nearCoreEnd = 0.80f;
    value.nearBlendEnd = 0.95f;
    value.farFadeStart = 0.90f;
    value.farFadeEnd = 1.00f;
    value.maximumOpticalDepth = 9.21034037f;
    value.minimumSunY = 0.052335956f;
    value.surfaceShadowStrength = std::isfinite(value.surfaceShadowStrength)
        ? std::clamp(value.surfaceShadowStrength, 0.0f, 1.0f) : 1.0f;
    value.surfaceAmbientFloor = std::isfinite(value.surfaceAmbientFloor)
        ? std::clamp(value.surfaceAmbientFloor, 0.0f, 1.0f) : 0.35f;
    value.cacheDebugExposure = std::isfinite(value.cacheDebugExposure)
        ? std::clamp(value.cacheDebugExposure, 0.1f, 20.0f) : 4.0f;
    value.debugNearSlice = std::min(
        value.debugNearSlice, value.nearSliceCount - 1u);
    value.debugFarSlice = std::min(
        value.debugFarSlice, value.farSliceCount - 1u);
    value.padding1 = 0u;
    value.padding2 = 0u;
    return value;
}
}
