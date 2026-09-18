// [학습 지도] CPU highcloud 상수와 HLSL kHigh 상수의 고정 High 계약 → View/Light 적분. UI/프리셋으로 변경하지 않는다.
// [수정 안내] [직접 조절]의 CPU/UI 원본을 수정한다. 강제 범위는 입력 계약이며 화질 보장이 아니다.
// 별도 권장 구간이 없는 값은 표시된 기본값을 비교 출발점으로 삼는다. b/t/u/s는 버퍼/읽기/쓰기/샘플러 슬롯.
// ============================================================================
//  HighCloudQuality.hlsli - CPU HighCloudQuality.h와 같은 최종 High 상수
// ============================================================================
#ifndef VCLOUD_HIGH_CLOUD_QUALITY_HLSLI
#define VCLOUD_HIGH_CLOUD_QUALITY_HLSLI

// [고정 품질] 시선 기본 구간 100m. 작은 값은 세밀하지만 표본 비용/최대 거리 계약에 영향. CPU/HLSL 수치와 테스트를 함께 유지한다.
static const float kHighViewStepMeters = 100.0;
// [고정 품질] 시선 최대 반복 512회. 거친 빈 공간 탐색도 횟수에 포함. CPU/HLSL 수치와 테스트를 함께 유지한다.
static const uint kHighMaximumViewSteps = 512u;
// [고정 품질] T<=0.01 종료. 이미 99% 가려진 뒤쪽의 추가 계산을 줄인다. CPU/HLSL 수치와 테스트를 함께 유지한다.
static const float kHighTransmittanceThreshold = 0.01;

// [고정 품질] 연속 빈 Base 표본 3회 후 coarse 탐색으로 전환. CPU/HLSL 수치와 테스트를 함께 유지한다.
static const uint kHighEmptySamplesBeforeCoarse = 3u;
// [고정 품질] 빈 Base 판정 epsilon 0.0001(무차원). final Detail 밀도와 다르다. CPU/HLSL 수치와 테스트를 함께 유지한다.
static const float kHighBaseDensityEpsilon = 0.0001;
// [고정 품질] 빈 공간 탐색 step 2배. 구름 발견 시 되감아 작은 step으로 확인. CPU/HLSL 수치와 테스트를 함께 유지한다.
static const float kHighCoarseStepMultiplier = 2.0;
// [고정 품질] 거친 탐색 간격 기준 상한 200m. 정상 fullStep보다 작아지게 만들지 않는다. CPU/HLSL 수치와 테스트를 함께 유지한다.
static const float kHighMaximumSearchStepMeters = 200.0;

// [고정 품질] 24km부터 거리 step 확대 시작. 원거리 비용을 줄인다. CPU/HLSL 수치와 테스트를 함께 유지한다.
static const float kHighDistanceStepStartMeters = 24000.0;
// [고정 품질] 50km에서 거리 step 확대 완료. CPU/HLSL 수치와 테스트를 함께 유지한다.
static const float kHighDistanceStepEndMeters = 50000.0;
// [고정 품질] 원거리 기본 step 배율 상한 1.25. 기본 100m가 최대 125m. CPU/HLSL 수치와 테스트를 함께 유지한다.
static const float kHighFarStepMultiplier = 1.25;

// [고정 품질] 태양 cone 적분 8개 표본. 각 구간 길이를 가중해야 광학 두께 보존. CPU/HLSL 수치와 테스트를 함께 유지한다.
static const uint kHighConeSampleCount = 8u;
// [고정 품질] cone 반각 2도. 먼 표본은 더 넓게 읽어 평행 띠를 줄인다. CPU/HLSL 수치와 테스트를 함께 유지한다.
static const float kHighConeAngleDegrees = 2.0;
// [고정 품질] 마지막 넓은 구간 시작 비율 0.77. 77%만 추적한다는 뜻이 아니다. CPU/HLSL 수치와 테스트를 함께 유지한다.
static const float kHighLightFarSampleFraction = 0.77;
// [고정 품질] 자기 표본 과차폐를 줄이는 태양 방향 시작점 bias 1m. CPU/HLSL 수치와 테스트를 함께 유지한다.
static const float kHighLightRayBiasMeters = 1.0;

#endif
