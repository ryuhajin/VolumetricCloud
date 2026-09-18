// [학습 지도] CPU LightParameters → b3(80B), 일부 b9 재패킹 → phase/직접광/림. 방향은 단위벡터, 색은 linear RGB.
// [수정 안내] [직접 조절]의 CPU/UI 원본을 수정한다. 강제 범위는 입력 계약이며 화질 보장이 아니다.
// 별도 권장 구간이 없는 값은 표시된 기본값을 비교 출발점으로 삼는다. b/t/u/s는 버퍼/읽기/쓰기/샘플러 슬롯.
// ============================================================================
//  LightParameters.hlsli - CPU LightParameters와 공유하는 80바이트 LightCB
// ----------------------------------------------------------------------------
//  Renderer가 매 프레임 b3에 복사하며 단계 7 Cloud PS가 읽는다. 방향은 월드
//  좌표계, 길이는 meter, 시간은 second 기준이다. src/LightParameters.h 및
//  doc/ARCHITECTURE.md의 표와 필드 순서를 항상 함께 맞춘다.
// ============================================================================
#ifndef VCLOUD_LIGHT_PARAMETERS_HLSLI
#define VCLOUD_LIGHT_PARAMETERS_HLSLI

cbuffer LightCB : register(b3)
{
    // [파생 값] F3 대기 태양 각도에서 생성한 표본→태양 xyz 단위벡터. Light sanitize 고도 [0,90]도; 기본 Urban 방위 -60/고도 18도.
    float3 directionToSun;       // CPU directionToSun. 표본에서 태양으로 향하는 단위 월드 방향.
    // [직접 조절] F3/Stage15 concept 태양 배율. [강제 범위] 유한 [0,무제한). [권장 범위] UI [0,8], 내장 0.75~1; 실제 빛은 b9.w로 전달.
    float sunIntensity;          // CPU sunIntensity. 태양 복사광 세기, 0이면 직접광이 없다.
    // [직접 조절] Light/scene 선형 RGB tint. [강제 범위] 성분별 유한 [0,무제한). [권장 범위] 초기 (1,0.95,0.85) 주변; b9.xyz로 재패킹된다.
    float3 sunColor;             // CPU sunColor. linear RGB 태양색.
    // [직접 조절] Light/scene, 소멸 중 산란 비율 sigma_s/sigma_t. [강제 범위] [0,1], 기본/권장 1; 낮추면 흡수가 늘어 어두워진다.
    float singleScatteringAlbedo;// CPU singleScatteringAlbedo. 소멸된 빛 중 산란되는 무차원 비율 ω=σs/σt.
    // [직접 조절] CPU phase preset. [강제 범위] 0 또는 1(입력 >=0.5면 1). 초기 0, Urban 1; 0은 방향 배율만 등방성 1로 만든다.
    float phaseEnabled;          // CPU phaseEnabled. 0이면 단계 6 등방성 산란을 정확히 유지한다.
    // [직접 조절] F3/phase preset. [강제 범위] [0,0.95], 초기 0.65/Urban 0.75. 증가하면 태양 방향 봉우리가 좁고 강해진다.
    float forwardScatteringG;    // CPU forwardScatteringG. [0,0.95], 태양을 보는 전방 lobe 비대칭도.
    // [직접 조절] phase preset 코드. [강제 범위] [-0.95,0], 초기 -0.25/Urban -0.15. 더 음수이면 태양 반대 방향에 집중한다.
    float backwardScatteringG;   // CPU backwardScatteringG. [-0.95,0], 태양 반대 후방 lobe 비대칭도.
    // [직접 조절] phase preset 코드. [강제 범위] [0,1], 초기 0.80/Urban 0.90. 0=후방, 1=전방 lobe; 기존 프리셋 주변에서 조절.
    float phaseBlend;            // CPU phaseBlend. 0=후방, 1=전방 lobe 혼합 비율.
    // [직접 조절] F3/phase preset. [강제 범위] [0,1], 초기 0.25/Urban 0.20. 증가하면 등방성 1에서 방향성 결과로 이동한다.
    float phaseIntensity;        // CPU phaseIntensity. 등방성 1에서 Dual-lobe로 이동하는 강도 [0,1].
    // [직접 조절] F3/scene. [강제 범위] [0,1], 초기 0/내장 0.35~0.85. 증가하면 phase 효과를 태양 노출 외곽에 한정한다.
    float edgeInfluence;         // CPU 동일 필드. 0=전체 Phase, 1=태양 노출 표면에만 Phase 적용.
    // [직접 조절] scene 코드. [강제 범위] [0.25,8], 초기 1/내장 1.4~2.0 권장. T의 지수; 증가하면 외곽광 영역이 좁아진다.
    float edgeOpticalDepthScale; // CPU 동일 필드. Tsun 거듭제곱으로 외곽 폭을 좁히는 값.
    // [직접 조절] scene 코드. [강제 범위] [0.5,4], 초기 1/내장 1.15~1.35 권장. 증가하면 T^지수가 작아져 직접광 그림자가 깊어진다.
    float shadowExponent;        // CPU 동일 필드. Tsun 거듭제곱으로 직접광 그림자 대비를 조절.
    // 06 직접광의 양의 phase 성분만 조절. Multiple/태양 T는 불변.
    float rimIntensity;          // offset64, 실제 배율 [0,4], 사용자 승인 기본2.
    float rimDepthScale;         // offset68, 외곽 지수 배율 [.5,2], 기본1.
    float2 rimPadding;
};

#endif
