// [학습 지도] CPU Formation/Motion → b1(80B) → 밀도/이동/진단 → Cloud/Shadow. cbuffer는 CPU 값의 읽기 전용 거울.
// [수정 안내] [직접 조절]의 CPU/UI 원본을 수정한다. 강제 범위는 입력 계약이며 화질 보장이 아니다.
// 별도 권장 구간이 없는 값은 표시된 기본값을 비교 출발점으로 삼는다. b/t/u/s는 버퍼/읽기/쓰기/샘플러 슬롯.
// ============================================================================
//  CloudParameters.hlsli - CPU CloudParameters와 공유하는 80바이트 CloudCB
// ----------------------------------------------------------------------------
//  CPU Renderer가 매 프레임 b1에 복사하고 Cloud PS와 Noise Lab PS가 함께 읽는다.
//  모든 위치와 길이는 월드 공간 meter 기준이다. 필드 순서나 자료형을 바꾸면
//  src/CloudParameters.h와 doc/ARCHITECTURE.md도 반드시 같은 변경에서 맞춘다.
// ============================================================================
#ifndef VCLOUD_CLOUD_PARAMETERS_HLSLI
#define VCLOUD_CLOUD_PARAMETERS_HLSLI

cbuffer CloudCB : register(b1)
{
    // [파생 값] xyz 월드 m; x/z=장면 검사 footprint, y=Planar 바닥. 최종 교차는 b5이며 유한 XZ 박스가 아니다.
    float3 cloudBoundsMin;      // CPU cloudBoundsMin. y가 Planar 구름 바닥.
    // [직접 조절] Formation/F1 밀도 배율. [강제 범위] [0,5]. [권장 범위] 내장 1.10~1.25; 증가하면 광학적으로 두꺼워진다.
    float densityMultiplier;   // CPU densityMultiplier. 최종 밀도 배율, 단위 없음.
    // [파생 값] xyz 월드 m; x/z=검사 footprint, y=바닥+층 두께. Formation 적용 시 동기화한다.
    float3 cloudBoundsMax;      // CPU cloudBoundsMax. y가 Planar 구름 천장.
    // [직접 조절] Formation.extinctionPerMeter/F1, 단위 1/m. [강제 범위] Formation [0.000001,0.01]. [권장 범위] 내장 0.00035~0.00046; 증가하면 T가 작아진다.
    float extinctionCoefficient;// CPU extinctionCoefficient. meter당 빛 소멸 강도.
    // [직접 조절] F4/숫자 키의 CloudDebugMode 열거값(기본 Composite=0). 연속 수치가 아니며 키 숫자와 enum 값은 다르다.
    int debugMode;              // CPU CloudDebugMode. 최종 합성 또는 중간값 출력 선택.
    // [직접 조절] Formation/F1, 무차원 [강제 범위] [0,1]. [권장 범위] 내장 0.38~0.90; 증가하면 noise 문턱이 낮아져 구름이 넓어진다.
    float coverage;             // CPU coverage. noise에서 구름을 남기는 비율(0~1).
    // [파생 값] CloudMotion.speedMetersPerSecond에서 복사. m/s [0,1000], F1 권장 [0,400], 기본 12. Formation/Custom 저장 밖이다.
    float windSpeed;            // 세션 전역 CloudMotion에서 매 frame 패킹한 이동 속도(m/s).
    // [직접 조절] CloudParameters 코드 원본; Base xyz에 같은 cycle offset, 초기 0. 유한 실수에 별도 clamp 없음; 권장 초기값 유지, 이동은 Motion 사용.
    float noiseOffset;          // CPU noiseOffset. 세 축에 더하는 수동 noise 좌표 이동(cycle).
    // [파생 값] CloudMotion.direction의 xyz 월드 단위벡터, y=0. 기본 (0.9701425,0,0.2425356). 방향만 바꾸며 속력은 windSpeed가 소유한다.
    float3 windDirection;       // CPU가 정규화한 월드 XZ 바람 방향.
    // [직접 조절] Formation.detailErosion/F1. [강제 범위] [0,1]. [권장 범위] 내장 0.10~0.24; 증가하면 얇은 경계가 더 깎인다.
    float detailErosionStrength;// CPU detailErosionStrength. Base에서 뺄 최대 밀도(0~1 권장).
    // [직접 조절] CloudParameters 코드 원본; Detail xyz cycle offset, 초기 17.3. 별도 clamp 없음; 권장 초기값 유지, 패턴 위치만 바뀐다.
    float detailNoiseOffset;    // CPU detailNoiseOffset. Base와 분리할 noise 좌표 이동(cycle).
    // [직접 조절] Formation.weather.worldSizeMeters. m/반복, 최종 기본 64000. Formation [17600,160000], 일반 Weather sanitize [1000,1000000]. 늘리면 배치가 커진다.
    float weatherMapWorldSize;  // CPU weatherMapWorldSize. Weather Map 한 반복의 월드 XZ 크기(m).
    // [직접 조절] CloudParameters 코드 원본; xy=월드 XZ의 cycle offset, 초기 (0,0). clamp 없음, frac으로 반복. 권장 초기값 유지.
    float2 weatherMapOffset;    // CPU weatherMapOffset. Weather UV 수동 이동(cycle).
};

#endif
