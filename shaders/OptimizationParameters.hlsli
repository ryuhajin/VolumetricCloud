// ============================================================================
//  OptimizationParameters.hlsli - 단계 9 빈 공간 탐색과 Early Exit 설정
// ----------------------------------------------------------------------------
//  CPU OptimizationParameters와 같은 32바이트 b5 상수버퍼다.
//  모든 거리는 월드 meter이며, epsilon과 투과율은 단위가 없는 0~1 값이다.
// ============================================================================
#ifndef VCLOUD_OPTIMIZATION_PARAMETERS_HLSLI
#define VCLOUD_OPTIMIZATION_PARAMETERS_HLSLI

cbuffer OptimizationCB : register(b5)
{
    uint earlyExitEnabled;          // CPU 동일 필드. 누적 투과율이 충분히 작으면 View 반복 종료.
    uint supportPrecheckEnabled;    // Height·Weather가 빈 곳이면 비싼 3D Base Noise 생략.
    uint emptySpaceSkippingEnabled; // 빈 구간에서 coarse 간격으로 빠르게 이동.
    uint emptySamplesBeforeCoarse;  // Full 모드에서 이 횟수만큼 비면 Search 모드로 복귀.

    float baseDensityEpsilon;       // 이 값 이하 Base는 빈 공간 후보로 취급한다.
    float coarseStepMultiplier;     // coarseStep = fineStep × 이 배율.
    float2 optimizationPadding;     // 16바이트 정렬 전용. 셰이더에서 사용하지 않는다.
};

#endif
