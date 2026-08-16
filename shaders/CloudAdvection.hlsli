// ============================================================================
//  CloudAdvection.hlsli - Physical Shape의 공통 수평 구름 이동
// ----------------------------------------------------------------------------
//  Weather/Base/Detail이 같은 world-space 변위를 사용해야 큰 실루엣과 내부 밀도가
//  서로 미끄러지지 않는다. Y는 고정해 1,500m 밑면과 물리 두께 계약을 보존한다.
// ============================================================================
#ifndef VCLOUD_CLOUD_ADVECTION_HLSLI
#define VCLOUD_CLOUD_ADVECTION_HLSLI

#include "CloudParameters.hlsli"

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

float2 ComputePhysicalCloudAdvectionOffset(float timeSeconds)
{
    float safeTime = isfinite(timeSeconds) ? max(timeSeconds, 0.0) : 0.0;
    float safeSpeed = isfinite(windSpeed) ? max(windSpeed, 0.0) : 0.0;
    return SafeHorizontalWindDirection() * (safeSpeed * safeTime);
}

float3 ComputePhysicalCloudSamplePosition(float3 worldPosition,
                                          float timeSeconds)
{
    float2 stationaryXZ = worldPosition.xz -
        ComputePhysicalCloudAdvectionOffset(timeSeconds);
    return float3(stationaryXZ.x, worldPosition.y, stationaryXZ.y);
}

#endif
