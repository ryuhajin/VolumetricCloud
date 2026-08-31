// ============================================================================
//  CloudDomainParameters.hlsli - 단계 13 구름 교차 도메인 b5
// ============================================================================
#ifndef VCLOUD_DOMAIN_PARAMETERS_HLSLI
#define VCLOUD_DOMAIN_PARAMETERS_HLSLI

#include "CloudParameters.hlsli"
#include "Ray.hlsli"

static const uint kCloudDomainAabb = 0u;
static const uint kCloudDomainPlanar = 1u;

cbuffer CloudDomainCB : register(b5)
{
    uint cloudDomainType;
    float cloudBottomAltitude;
    float cloudLayerThickness;
    float maxViewTraceDistance;

    float viewTraceFadeStartDistance;
    float maxLightTraceDistance;
    float cloudLightingReferenceAltitudeMeters;
    float cloudDomainPadding;
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
    bool domainHit = false;
    if (cloudDomainType == kCloudDomainAabb)
    {
        float tNear = 0.0;
        float tFar = 0.0;
        bool hit = IntersectRayAABB(
            rayOrigin, rayDirection, cloudBoundsMin, cloudBoundsMax,
            tNear, tFar);
        if (hit)
        {
            tStart = max(tNear, 0.0);
            tEnd = min(tFar, externalTraceLimit);
        }
        domainHit = hit && tEnd > tStart;
    }
    else
    {
        float domainLimit = lightRay
            ? max(maxLightTraceDistance, 0.0)
            : max(maxViewTraceDistance, 0.0);
        domainHit = IntersectPlanarCloudLayer(
            rayOrigin, rayDirection, min(externalTraceLimit, domainLimit),
            tStart, tEnd);
    }
    return domainHit;
}

float CloudViewDistanceFade(float rayDistance)
{
    float fade = 1.0;
    if (cloudDomainType == kCloudDomainPlanar)
    {
        float safeEnd = max(maxViewTraceDistance, 1e-4);
        float safeStart = clamp(viewTraceFadeStartDistance, 0.0, safeEnd);
        fade = safeEnd - safeStart <= 1e-5
            ? (rayDistance < safeEnd ? 1.0 : 0.0)
            : 1.0 - smoothstep(safeStart, safeEnd, max(rayDistance, 0.0));
    }
    return fade;
}

float CloudDebugDistanceRange()
{
    return cloudDomainType == kCloudDomainPlanar
        ? max(maxViewTraceDistance, 1.0)
        : 20.0;
}

float CloudDebugDistanceValue(float distance)
{
    float normalized = saturate(distance / CloudDebugDistanceRange());
    // km 범위를 선형 회색으로 표시하면 1.5~4.5km가 거의 검게 보이므로
    // 평면층 진단만 제곱근으로 들어 올린다. AABB 회귀 출력은 선형을 유지한다.
    return cloudDomainType == kCloudDomainPlanar
        ? sqrt(normalized)
        : normalized;
}

#endif
