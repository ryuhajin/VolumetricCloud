// ============================================================================
//  LightParameters.hlsli - CPU LightParameters와 공유하는 48바이트 LightCB
// ----------------------------------------------------------------------------
//  Renderer가 매 프레임 b3에 복사하며 단계 6 Cloud PS가 읽는다. 방향은 월드
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
    float scatteringCoefficient;// CPU scatteringCoefficient. 밀도가 빛을 카메라로 흩는 비율.
    uint maxLightSteps;          // CPU maxLightSteps. 한 View 표본당 Light Ray 최대 반복 수.
    float lightStepSize;         // CPU lightStepSize. 목표 Light Ray 간격(m).
    float lightRayBias;          // CPU lightRayBias. 시작점 자기 교차 방지 거리(m).
    float lightPadding;          // 16바이트 정렬용 예약 필드. 현재 사용하지 않는다.
};

#endif
