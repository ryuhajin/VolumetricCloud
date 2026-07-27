// ============================================================================
//  Ray.hlsli  —  레이 교차 함수 모음
// ----------------------------------------------------------------------------
//  레이마칭을 시작하기 전에 "레이가 어느 구간에서 볼륨 안에 있는가"를 구하는
//  작은 수학 함수들을 모아둔다. 각 함수는 교차하면 true를 반환하고,
//  레이 파라미터 t0(진입) / t1(탈출)을 출력한다.
// ============================================================================

// ---- 해석적 ray-sphere 교차 (rd는 정규화되어 a=1) ----
// 구의 방정식 (x - cx)^2 + (y - cy)^2 + (z - cz)^2 = r^2에
// ray 방정식을 대입하여 2차방정식을 푼다
//  교차하면 true, t0(진입)/t1(탈출) 출력. t0 <= t1.
bool RaySphere(float3 ro, float3 rd, float3 center, float radius,
               out float t0, out float t1)
{
    // 구 중심을 원점으로 옮겼을 때 레이 시작점
    float3 oc = ro - center;

    // oc가 rd 방향으로 얼마나 놓여져 있는지
    float  b  = dot(oc, rd);

    // 카메라와 구 중심 사이² - 반지름²
    // 양수 = 시작점이 구 밖, 0 = 시작점이 표면, 음수 = 시작점이 구 안
    float  c  = dot(oc, oc) - radius * radius;

    // 레이와 구가 만나는지 판단하는 판별식.
    // 양수 = 두 교차점, 0 = 접함, 음수 = 빗나감.
    float  h  = b * b - c;

    if (h < 0.0)
    {
        t0 = 0.0; t1 = 0.0;
        return false;               // 구를 빗나감 (교차점 없음)
    }

    // 제곱근은 교차 구간의 절반 길이
    h  = sqrt(h);

    // 진입점(첫 번째 교차점): 중앙(-b) - 절반 길이
    t0 = -b - h;

    // 탈출점(두 번째 교차점): 중앙(-b) + 절반 길이
    t1 = -b + h;
    return true;
}

// ---- ray-box(AABB) 교차: slab 방식 ----
// 점 P(t)가 세 조건을 모두 만족하는지 체크
// 박스 안이라는 건 x/y/z 범위에 동시에 들어간다는 뜻이므로, 세 구간의 교집합을 구함
// boxMin.x <= p.x <= boxMax.x
// boxMin.y <= p.y <= boxMax.y
// boxMin.z <= p.z <= boxMax.z
//  교차하면 true, t0(진입)/t1(탈출) 출력. t0 <= t1.
bool RayBox(float3 ro, float3 rd, float3 boxMin, float3 boxMax,
            out float t0, out float t1)
{
    // 각 축의 두 평면과 만나는 t를 구한다.
    float3 invRd = 1.0 / rd;

    // ray 공식 p(t) = ro + rd * t
    // t = (boxMin.x - ro.x) / rd.x
    float3 tA = (boxMin - ro) * invRd; // Min tx,ty,tz
    float3 tB = (boxMax - ro) * invRd; // Max tx,ty,tz

    // 레이 방향이 음수인 축은 near/far가 뒤집히므로 min/max로 정렬한다.
    float3 tNear = min(tA, tB);
    float3 tFar  = max(tA, tB);

    // 세 축 slab에 모두 들어와 있는 공통 구간을 구한다.
    t0 = max(max(tNear.x, tNear.y), tNear.z);
    t1 = min(min(tFar.x,  tFar.y),  tFar.z);

    // t1이 진입점보다 뒤에 있고, 카메라 앞쪽 구간과 겹치면 교차.
    return t1 > max(t0, 0.0);
}
