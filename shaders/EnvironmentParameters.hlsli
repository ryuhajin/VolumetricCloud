// [학습 지도] CPU EnvironmentParameters → b4(48B) → CloudEnvironment의 fill/다중 산란. RGB는 선형, 비율은 무차원.
// [수정 안내] [직접 조절]의 CPU/UI 원본을 수정한다. 강제 범위는 입력 계약이며 화질 보장이 아니다.
// 별도 권장 구간이 없는 값은 표시된 기본값을 비교 출발점으로 삼는다. b/t/u/s는 버퍼/읽기/쓰기/샘플러 슬롯.
// ============================================================================
//  EnvironmentParameters.hlsli - CPU와 공유하는 48바이트 EnvironmentCB(b4)
// ----------------------------------------------------------------------------
//  물리 대기 LUT의 입사광을 현재 표본의 높이·밀도·차폐로 가중한다.
//  직접광과 독립적인 환경광 설정이며 옛 분석적 상수색 필드는 제거했다.
// ============================================================================
#ifndef VCLOUD_ENVIRONMENT_PARAMETERS_HLSLI
#define VCLOUD_ENVIRONMENT_PARAMETERS_HLSLI

cbuffer EnvironmentCB : register(b4)
{
    // [직접 조절] Environment preset 코드. [강제 범위] [0,16], 초기 1.5/Urban 1.8 권장. 증가하면 고밀도 내부의 fill이 어두워진다.
    float ambientOcclusionStrength;         // CPU 동일 필드. 밀도 기반 환경광 감쇠 강도.
    // [직접 조절] Environment 코드. [강제 범위] [0,1], 기본/권장 0.65. 증가하면 낮은 local height의 하늘광이 줄어든다.
    float ambientHeightInfluence;           // CPU 동일 필드. 0=높이 무관, 1=하늘광이 높이에 비례.
    // [직접 조절] Environment preset. [강제 범위] 0/1, 초기/권장 1. 0이면 다중 산란 근사만 끈다.
    float multipleScatteringEnabled;        // CPU 동일 필드. 0이면 다중 산란 결과가 정확히 0.
    // [직접 조절] Environment preset. [강제 범위] 정수 [0,4], 기본/권장 2. 증가하면 추가 산란 항과 계산량이 늘어난다.
    uint multipleScatteringOctaves;         // CPU 동일 필드. 저비용 반복 수 [0,4].
    // [직접 조절] Environment 코드. [강제 범위] [0,1], 초기 0.20/Urban 0.15 권장. 증가하면 octave별 에너지가 더 오래 남는다.
    float multipleScatteringAttenuation;    // CPU 동일 필드. octave별 에너지 감소.
    // [직접 조절] Environment 코드. [강제 범위] [0,1], 기본/권장 0.50. 감소하면 근사 tau가 작아져 빛이 내부까지 들어간다.
    float multipleScatteringExtinctionFactor;// CPU 동일 필드. 반복 광학 깊이 비율.
    // [직접 조절] Environment 코드. [강제 범위] [0,1], 초기 0.25/Urban 0.15 권장. 감소하면 고차 산란 방향성이 등방성 1에 가까워진다.
    float multipleScatteringPhaseFactor;    // CPU 동일 필드. 반복 Phase 방향성 비율.
    // [직접 조절] F3/scene 하늘 fill 배율. [강제 범위]/UI [0,2], 초기 1/내장 0.85~1.10. 증가하면 구름 하늘광만 밝아진다.
    float physicalSkyFillScale;             // Physical LUT 하늘 입사광 배율 [0,2].
    // [직접 조절] Environment 코드. [강제 범위] [0,1], 초기 0/Urban 0.55 권장. 증가하면 태양 차폐를 지면 반사광에 더 반영한다. 하늘광에는 적용하지 않는다.
    float ambientShadowCoupling;             // CPU 동일 필드. 태양 차폐를 지면 반사광 AO에 섞는 비율.
    // [직접 조절] Environment 코드. [강제 범위] [0.1,8], 초기 1/Urban 0.50 권장. 증가하면 차폐된 곳의 지면 반사광이 더 어두워진다.
    float ambientShadowExponent;             // CPU 동일 필드. Tsun 기반 지면 반사광 차폐 곡선.
    // [직접 조절] F3/scene. [강제 범위]/UI [0,1], 초기 0/내장 0.55~0.80. 증가하면 다중 산란을 차폐된 내부에 집중한다.
    float multipleScatteringInteriorBlend;   // CPU 동일 필드. 다중 산란을 차폐 영역으로 옮기는 비율.
    // [직접 조절] F3/scene 지면 fill 배율. [강제 범위]/UI [0,2], 초기 1/내장 0.85~1.10. 증가하면 구름 하부 반사광이 밝아진다.
    float physicalGroundFillScale;           // Physical LUT 지면 입사광 배율 [0,2].
};

#endif
