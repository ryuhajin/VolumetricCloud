// ============================================================================
//  Ray.hlsli - 단계 1의 수치적으로 안전한 Ray-AABB 교차
// ----------------------------------------------------------------------------
//  렌더링 흐름에서의 위치
//  1. VolumetricClouds.hlsl이 화면 UV에서 월드 레이를 복원한다.
//  2. 이 파일의 IntersectRayAABB가 레이와 구름 박스의 겹친 거리 구간을 찾는다.
//  3. 호출자가 Scene Depth로 구간 끝을 잘라 실제 레이 마칭 범위를 만든다.
//
//  모든 위치와 t는 월드 공간 meter 단위다. rayDirection은 길이가 1이어야
//  t가 실제 거리가 된다. 축과 나란한 레이는 0으로 나누지 않고 별도 검사한다.
// ============================================================================

// 월드 레이와 축 정렬 박스(AABB)의 공통 구간을 slab 방식으로 구한다.
// 입력:
//   rayOrigin     - 월드 카메라 위치(m)
//   rayDirection  - 길이 1인 월드 방향
//   boundsMin/Max - 월드 AABB의 두 모서리(m)
// 출력:
//   tNear/tFar    - 박스 진입/이탈 거리(m), tNear는 카메라 내부일 때 음수 가능
// 반환:
//   카메라 앞쪽에 양의 길이를 가진 볼륨 구간이 있을 때만 true다.
// 접점(tNear==tFar)은 부피가 0이므로 레이 마칭 대상으로 취급하지 않는다.
bool IntersectRayAABB(float3 rayOrigin, float3 rayDirection,
                      float3 boundsMin, float3 boundsMax,
                      out float tNear, out float tFar)
{
    const float parallelEpsilon = 1e-6;

    // 1. 아직 어떤 축도 제한하지 않은 매우 넓은 유한 구간에서 시작한다.
    //    무한대 대신 큰 유한값을 써서 후속 디버그 출력도 finite하게 유지한다.
    tNear = -1e30;
    tFar = 1e30;
    bool valid = true;

    // 2. X slab. 평행이면 나누지 않고 원점이 두 평면 사이인지 본다.
    if (abs(rayDirection.x) < parallelEpsilon)
    {
        if (rayOrigin.x < boundsMin.x || rayOrigin.x > boundsMax.x)
            valid = false;
    }
    else
    {
        float2 axisHits = float2(
            (boundsMin.x - rayOrigin.x) / rayDirection.x,
            (boundsMax.x - rayOrigin.x) / rayDirection.x);
        tNear = max(tNear, min(axisHits.x, axisHits.y));
        tFar = min(tFar, max(axisHits.x, axisHits.y));
        if (tFar < tNear)
            valid = false;
    }

    // 3. Y slab을 같은 방식으로 공통 구간에 합친다.
    if (abs(rayDirection.y) < parallelEpsilon)
    {
        if (rayOrigin.y < boundsMin.y || rayOrigin.y > boundsMax.y)
            valid = false;
    }
    else
    {
        float2 axisHits = float2(
            (boundsMin.y - rayOrigin.y) / rayDirection.y,
            (boundsMax.y - rayOrigin.y) / rayDirection.y);
        tNear = max(tNear, min(axisHits.x, axisHits.y));
        tFar = min(tFar, max(axisHits.x, axisHits.y));
        if (tFar < tNear)
            valid = false;
    }

    // 4. Z slab을 합쳐 세 축 모두 안에 있는 최종 구간을 만든다.
    if (abs(rayDirection.z) < parallelEpsilon)
    {
        if (rayOrigin.z < boundsMin.z || rayOrigin.z > boundsMax.z)
            valid = false;
    }
    else
    {
        float2 axisHits = float2(
            (boundsMin.z - rayOrigin.z) / rayDirection.z,
            (boundsMax.z - rayOrigin.z) / rayDirection.z);
        tNear = max(tNear, min(axisHits.x, axisHits.y));
        tFar = min(tFar, max(axisHits.x, axisHits.y));
        if (tFar < tNear)
            valid = false;
    }

    // 5. 카메라 뒤쪽 구간과 면 한 점만 스치는 tangent는 실제 부피가 없으므로 제외한다.
    return valid && tFar > max(tNear, 0.0);
}
