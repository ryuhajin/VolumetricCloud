// [학습 지도] CPU CloudDomainParameters → b5(32B) → Planar Y 교차/거리 fade → Cloud와 Light. 위치/길이 m.
// [수정 안내] [직접 조절]의 CPU/UI 원본을 수정한다. 강제 범위는 입력 계약이며 화질 보장이 아니다.
// 별도 권장 구간이 없는 값은 표시된 기본값을 비교 출발점으로 삼는다. b/t/u/s는 버퍼/읽기/쓰기/샘플러 슬롯.
// ============================================================================
//  CloudDomainParameters.hlsli - 단계 13 구름 교차 도메인 b5
// ============================================================================
#ifndef VCLOUD_DOMAIN_PARAMETERS_HLSLI
#define VCLOUD_DOMAIN_PARAMETERS_HLSLI

#include "CloudParameters.hlsli"

cbuffer CloudDomainCB : register(b5)
{
    // 구름층 외곽의 시작 월드 Y(m). 증가하면 구름층 전체가 위로 이동한다.
    // [직접 조절] Formation/F1 절대 월드 Y(m). Formation [-100000,100000], domain sanitize는 finite만 보장. 초기 1500/Urban 1800; 올리면 층 전체 상승.
    float cloudBottomAltitude;
    // local thickness + base lift의 최대 수직 공간(m). 부족하면 top clipping이 난다.
    // [직접 조절] Formation/F1 도메인 두께 m. Formation [1,100000], domain sanitize >=0.0001. 초기 3000/Urban 3700. 최대 local 두께+lift+200m를 담아야 한다.
    float cloudLayerThickness;
    // [직접 조절] Formation 최대 시선 거리 m. Formation [1,200000], domain sanitize >=0.0001. 기본/권장 50000; 늘리면 원경과 비용이 증가한다.
    float maxViewTraceDistance;
    // [직접 조절] Formation 거리 fade 시작 m. [강제 범위] [0,maxViewTraceDistance], 기본/권장 40000. 낮추면 원경이 더 일찍 희미해진다.
    float viewTraceFadeStartDistance;

    // [직접 조절] Formation cone 추적 거리 m. Formation [1,200000], domain sanitize >=0.0001. 기본/권장 20000; Deep Cache 크기와는 별개다.
    float maxLightTraceDistance;
    // [파생 값] Formation의 유효 shape 대표 절대 고도 m. domain sanitize [bottom,top], 구조체 초기 3000. LUT 입사광 조회 기준이며 직접 튜닝하지 않는다.
    float cloudLightingReferenceAltitudeMeters;
    // [패딩] 16바이트 packing을 위한 예약 칸, 0 유지. 화면 효과 없음; 삭제/순서 변경 금지.
    float cloudDomainPadding0;
    // [패딩] 16바이트 packing을 위한 예약 칸, 0 유지. 화면 효과 없음; 삭제/순서 변경 금지.
    float cloudDomainPadding1;
};

bool IsFiniteDomainScalar(float value)
{
    return value == value && abs(value) < 3.0e30;
}

// [교차 순서] 입력 origin(m), 정규화 direction, limit(m) → 유효 구간 tStart/tEnd(m).
// 1. Y=bottom/top 평면까지 t=(높이-origin.y)/direction.y를 구한다.
// 2. 가까운 교점은 0 이상, 먼 교점은 limit 이하로 자른다.
// 3. 수평 ray는 나누지 않고 층 안이면 limit까지, 밖이면 miss로 처리한다.
// XZ는 무한 반복이며 카메라 깊이/최대 추적 거리로만 수평 범위가 제한된다.
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

// [원경 fade] 기본 40~50km에서 1→0인 밀도 배율. 시작=끝이면 hard cut.
// 색을 검게 곱하는 대신 밀도를 줄여 배경이 자연스럽게 드러나게 한다.
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
