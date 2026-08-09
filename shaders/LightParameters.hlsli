// ============================================================================
//  LightParameters.hlsli - CPU LightParameters와 공유하는 64바이트 LightCB
// ----------------------------------------------------------------------------
//  Renderer가 매 프레임 b3에 복사하며 단계 7 Cloud PS가 읽는다. 방향은 월드
//  좌표계, 길이는 meter, 시간은 second 기준이다. src/LightParameters.h 및
//  doc/ARCHITECTURE.md의 표와 필드 순서를 항상 함께 맞춘다.
// ============================================================================
#ifndef VCLOUD_LIGHT_PARAMETERS_HLSLI
#define VCLOUD_LIGHT_PARAMETERS_HLSLI

cbuffer LightCB : register(b3)
{
    float3 directionToSun;       // CPU directionToSun. 표본에서 태양으로 향하는 단위 월드 방향.
    float sunIntensity;          // CPU sunIntensity. 태양 복사광 세기, 0이면 직접광이 없다.
    float3 sunColor;             // CPU sunColor. linear RGB 태양색.
    float singleScatteringAlbedo;// CPU singleScatteringAlbedo. 소멸 에너지 중 산란 비율 [0,1].
    uint maxLightSteps;          // CPU maxLightSteps. 한 View 표본당 Light Ray 최대 반복 수.
    float lightStepSize;         // CPU lightStepSize. 목표 Light Ray 간격(m).
    float lightRayBias;          // CPU lightRayBias. 시작점 자기 교차 방지 거리(m).
    float phaseEnabled;          // CPU phaseEnabled. 0이면 단계 6 등방성 산란을 정확히 유지한다.
    float forwardScatteringG;    // CPU forwardScatteringG. [0,0.95], 태양을 보는 전방 lobe 비대칭도.
    float backwardScatteringG;   // CPU backwardScatteringG. [-0.95,0], 태양 반대 후방 lobe 비대칭도.
    float phaseBlend;            // CPU phaseBlend. 0=후방, 1=전방 lobe 혼합 비율.
    float phaseIntensity;        // CPU phaseIntensity. 등방성 1에서 Dual-lobe로 이동하는 강도 [0,1].
};

#endif
