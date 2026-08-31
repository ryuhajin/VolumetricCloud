// ============================================================================
//  CloudDomainParameters.hlsli - 단계 13 구름 교차 도메인 b5
// ============================================================================
#ifndef VCLOUD_DOMAIN_PARAMETERS_HLSLI
#define VCLOUD_DOMAIN_PARAMETERS_HLSLI

#include "CloudParameters.hlsli"

cbuffer CloudDomainCB : register(b5)
{
    float cloudBottomAltitude;
    float cloudLayerThickness;
    float maxViewTraceDistance;
    float viewTraceFadeStartDistance;

    float maxLightTraceDistance;
    float cloudLightingReferenceAltitudeMeters;
    float cloudDomainPadding0;
    float cloudDomainPadding1;
};

bool IsFiniteDomainScalar(float value)
{
    return value == value && abs(value) < 3.0e30;
}

bool IntersectPlanarCloudLayer(float3 rayOrigin, float3 rayDirection,
                               float traceLimit,
                               out float tStart, out float tEnd)
{
    tStart = 0.0;
    tEnd = 0.0;
    bool hit = false;
    bool valid = IsFiniteDomainScalar(rayOrigin.y) &&
        IsFiniteDomainScalar(rayDirection.y) &&
        IsFiniteDomainScalar(cloudBottomAltitude) &&
        IsFiniteDomainScalar(cloudLayerThickness) &&
        IsFiniteDomainScalar(traceLimit) &&
        cloudLayerThickness > 1e-5 && traceLimit > 0.0;
    if (valid)
    {
        float layerTop = cloudBottomAltitude + cloudLayerThickness;
        if (abs(rayDirection.y) <= 1e-6)
        {
            bool insideLayer = rayOrigin.y >= cloudBottomAltitude &&
                rayOrigin.y <= layerTop;
            tEnd = insideLayer ? traceLimit : 0.0;
            hit = insideLayer && tEnd > tStart;
        }
        else
        {
            float2 hits = float2(
                (cloudBottomAltitude - rayOrigin.y) / rayDirection.y,
                (layerTop - rayOrigin.y) / rayDirection.y);
            tStart = max(min(hits.x, hits.y), 0.0);
            tEnd = min(max(hits.x, hits.y), traceLimit);
            hit = tEnd > tStart;
        }
    }
    return hit;
}

bool IntersectCloudDomain(float3 rayOrigin, float3 rayDirection,
                          float externalTraceLimit, bool lightRay,
                          out float tStart, out float tEnd)
{
    tStart = 0.0;
    tEnd = 0.0;
    float domainLimit = lightRay
        ? max(maxLightTraceDistance, 0.0)
        : max(maxViewTraceDistance, 0.0);
    return IntersectPlanarCloudLayer(
        rayOrigin, rayDirection, min(externalTraceLimit, domainLimit),
        tStart, tEnd);
}

float CloudViewDistanceFade(float rayDistance)
{
    float safeEnd = max(maxViewTraceDistance, 1e-4);
    float safeStart = clamp(viewTraceFadeStartDistance, 0.0, safeEnd);
    return safeEnd - safeStart <= 1e-5
        ? (rayDistance < safeEnd ? 1.0 : 0.0)
        : 1.0 - smoothstep(safeStart, safeEnd, max(rayDistance, 0.0));
}

float CloudDebugDistanceRange()
{
    return max(maxViewTraceDistance, 1.0);
}

float CloudDebugDistanceValue(float distance)
{
    float normalized = saturate(distance / CloudDebugDistanceRange());
    // km 범위를 선형 회색으로 표시하면 1.5~4.5km가 거의 검게 보이므로
    // 제곱근으로 들어 올려 가까운 구름층 거리도 읽기 쉽게 만든다.
    return sqrt(normalized);
}

#endif
