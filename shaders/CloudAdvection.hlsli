// [학습 지도] CPU CloudMotion → b1 → 공통 이동 보정 월드 m → Weather/Noise/Shadow. Y는 고정, 시간은 s.
// [수정 안내] [직접 조절]의 CPU/UI 원본을 수정한다. 강제 범위는 입력 계약이며 화질 보장이 아니다.
// 별도 권장 구간이 없는 값은 표시된 기본값을 비교 출발점으로 삼는다. b/t/u/s는 버퍼/읽기/쓰기/샘플러 슬롯.
// ============================================================================
//  CloudAdvection.hlsli - Physical Shape의 공통 수평 구름 이동
// ----------------------------------------------------------------------------
//  Weather/Base/Detail이 같은 world-space 변위를 사용해야 큰 실루엣과 내부 밀도가
//  서로 미끄러지지 않는다. Y는 고정해 선택한 밑면 고도와 물리 두께 계약을 보존한다.
// ============================================================================
#ifndef VCLOUD_CLOUD_ADVECTION_HLSLI
#define VCLOUD_CLOUD_ADVECTION_HLSLI

#include "CloudParameters.hlsli"

// [데이터 흐름] b1의 세션 Motion → XZ 단위 방향 → 시간*속력(m) → noise 조회 위치.
// Weather/Base/Detail/Shadow가 같은 이동을 공유한다. 독립 offset으로 움직이면 그림자가 뒤처진다.
float2 SafeHorizontalWindDirection()
{
    float2 direction = windDirection.xz;
    float2 safeDirection = float2(0.0, 0.0);
    if (all(isfinite(direction)))
    {
        float directionLength = length(direction);
        if (directionLength > 1e-6)
            safeDirection = direction / directionLength;
    }
    return safeDirection;
}

// 1. 방향(무차원)*속력(m/s)*시간(s)=이동 거리(m). 속도를 바꾸면 절대 time과의
// 곱이 바뀌므로 무늬 위치가 순간 이동할 수 있다; 누적 위치 적분 방식은 아니다.
float2 ComputePhysicalCloudAdvectionOffset(float timeSeconds)
{
    float safeTime = isfinite(timeSeconds) ? max(timeSeconds, 0.0) : 0.0;
    float safeSpeed = isfinite(windSpeed) ? max(windSpeed, 0.0) : 0.0;
    return SafeHorizontalWindDirection() * (safeSpeed * safeTime);
}

// 2. 현재 월드 위치에서 이동량을 뺀다. 지도 자체가 +바람 방향으로 움직이려면
// 샘플 주소는 -바람 방향이어야 한다. y는 그대로 두어 구름이 수직으로 흐르지 않는다.
float3 ComputePhysicalCloudSamplePosition(float3 worldPosition,
                                          float timeSeconds)
{
    float2 stationaryXZ = worldPosition.xz -
        ComputePhysicalCloudAdvectionOffset(timeSeconds);
    return float3(stationaryXZ.x, worldPosition.y, stationaryXZ.y);
}

#endif
