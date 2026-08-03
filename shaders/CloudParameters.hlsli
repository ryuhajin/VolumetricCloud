// ============================================================================
//  CloudParameters.hlsli - CPU CloudParameters와 공유하는 112바이트 CloudCB
// ----------------------------------------------------------------------------
//  CPU Renderer가 매 프레임 b1에 복사하고 Cloud PS와 Noise Lab PS가 함께 읽는다.
//  모든 위치와 길이는 월드 공간 meter 기준이다. 필드 순서나 자료형을 바꾸면
//  src/CloudParameters.h와 doc/ARCHITECTURE.md도 반드시 같은 변경에서 맞춘다.
// ============================================================================
#ifndef VCLOUD_CLOUD_PARAMETERS_HLSLI
#define VCLOUD_CLOUD_PARAMETERS_HLSLI

cbuffer CloudCB : register(b1)
{
    float3 cloudBoundsMin;      // CPU cloudBoundsMin. AABB 최소 월드 위치(m), y가 구름 바닥.
    float densityMultiplier;   // CPU densityMultiplier. 최종 밀도 배율, 단위 없음.
    float3 cloudBoundsMax;      // CPU cloudBoundsMax. AABB 최대 월드 위치(m), y가 구름 천장.
    float stepSize;             // CPU stepSize. 목표 view-ray 표본 간격(m).
    uint maxViewSteps;          // CPU maxViewSteps. 한 픽셀의 최대 반복 횟수.
    float extinctionCoefficient;// CPU extinctionCoefficient. meter당 빛 소멸 강도.
    float transmittanceThreshold;// CPU 예약값. 단계 9 Early Exit 전에는 사용하지 않는다.
    int debugMode;              // CPU CloudDebugMode. 최종 합성 또는 중간값 출력 선택.
    float baseNoiseScale;       // CPU baseNoiseScale. 월드 1m당 noise cycle 수.
    float coverage;             // CPU coverage. noise에서 구름을 남기는 비율(0~1).
    float windSpeed;            // CPU windSpeed. 월드 공간 이동 속도(m/s).
    float noiseOffset;          // CPU noiseOffset. 세 축에 더하는 수동 noise 좌표 이동(cycle).
    float3 windDirection;       // CPU windDirection. 정규화 전 월드 공간 바람 방향.
    float bottomFadeEnd;        // CPU bottomFadeEnd. 바닥 fade가 끝나는 정규화 높이(0~1).
    float topFadeStart;         // CPU topFadeStart. 꼭대기 fade가 시작되는 정규화 높이(0~1).
    float3 heightProfilePadding;// 16바이트 정렬용 예약 필드. 현재 셰이더에서는 사용하지 않는다.
    float detailNoiseScale;     // CPU detailNoiseScale. 표면 침식 noise 주파수(cycle/m).
    float detailErosionStrength;// CPU detailErosionStrength. Base에서 뺄 최대 밀도(0~1 권장).
    float detailWindSpeed;      // CPU detailWindSpeed. Detail 무늬 이동 속도(m/s).
    float detailNoiseOffset;    // CPU detailNoiseOffset. Base와 분리할 noise 좌표 이동(cycle).
};

#endif
