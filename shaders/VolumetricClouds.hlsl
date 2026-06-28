// ============================================================================
//  VolumetricClouds.hlsl  —  레이마칭 픽셀 셰이더 (AABB 박스 볼륨)
// ----------------------------------------------------------------------------
//  볼류메트릭 렌더링의 가장 작은 뼈대:
//    1) 픽셀마다 카메라 레이(원점 ro, 방향 rd)를 만든다.
//    2) 레이와 AABB 박스의 교차 구간 [t0, t1]을 slab 방식으로 구한다.
//    3) 그 구간을 고정 스텝으로 행진하며 상수 밀도(density)를 적분한다.
//    4) Beer-Lambert 법칙으로 투과율(transmittance)을 누적해 알파를 만든다.
//    5) 절차적 하늘(sky) 위에 안개 색을 합성한다.
//
//  ※ noise 없음 / light(산란) 없음. 이후 단계에서 density에 noise를,
//    적분 루프에 light scattering을 더하면 그대로 구름으로 확장된다.
//    (자세한 수식은 doc/RAYMARCHING.md 참고)
// ============================================================================

#include "Ray.hlsli"

cbuffer cbCamera : register(b0)
{
    float4x4 invViewProj;  // 역 뷰-투영 행렬 (C++에서 transpose 후 업로드)
    float3   cameraPos;    // 카메라 월드 위치 = 레이 원점
    float    time;         // 경과 시간 (현재 미사용, 추후 애니메이션용)
    float3   volumeCenter; // 박스 볼륨 중심
    float    densityScale; // 밀도 (클수록 불투명)
    float3   volumeHalfSize; // 박스 볼륨 절반 크기
    float    _pad;         // 16바이트 정렬용 패딩
};

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

// ---- 절차적 하늘: 레이 방향의 높이에 따른 그라데이션 ----
float3 SkyColor(float3 rd)
{
    // rd.y = 1 : 위, rd.y = -1 : 아래
    // rd 정규화된 방향 : [-1~1] -> [0~1]
    float t = saturate(rd.y * 0.5 + 0.5);

    // 수평선 쪽 밝은 회청색
    float3 horizon = float3(0.52, 0.60, 0.70);

    // 머리 위쪽의 진한 파란색
    float3 zenith  = float3(0.18, 0.32, 0.55);

    // 레이가 위를 향할수록 zenith 색에 가까워짐
    return lerp(horizon, zenith, t);
}

float4 main(VSOut input) : SV_TARGET
{
    // ---- 1) 픽셀 -> NDC -> 월드 레이 ----
    // uv(0,0)=좌상단 이므로 y를 뒤집어 NDC로 변환 (NDC 우하단 (1,-1), UV 우하단 (1,1))
    float2 ndc = float2(input.uv.x * 2.0 - 1.0, 1.0 - input.uv.y * 2.0);

    // 근/원 평면 점을 역투영해서 레이 방향을 만든다.
    // (C++에서 transpose 했으므로 mul(vector, matrix) 사용)
    // H = homogeneous coordinates = (x,y,z,w) 형태
    float4 nearH = mul(float4(ndc, 0.0, 1.0), invViewProj);
    float4 farH  = mul(float4(ndc, 1.0, 1.0), invViewProj);
    float3 nearP = nearH.xyz / nearH.w;
    float3 farP  = farH.xyz  / farH.w;

    float3 ro = cameraPos; // ray origin = 레이 시작 위치
    float3 rd = normalize(farP - nearP); // ray direction = 레이가 진행할 방향

    // 배경(하늘) 색
    float3 sky = SkyColor(rd);

    // ---- 2) 박스 볼륨 교차 ----
    float t0, t1;
    float3 boxMin = volumeCenter - volumeHalfSize;
    float3 boxMax = volumeCenter + volumeHalfSize;
    if (!RayBox(ro, rd, boxMin, boxMax, t0, t1))
    {
        return float4(sky, 1.0); // 박스를 안 맞으면 하늘만
    }

    // 카메라가 박스 안/뒤에 있을 수 있으니 진입점을 0 이상으로 클램프
    t0 = max(t0, 0.0);
    if (t1 <= t0)
    {
        return float4(sky, 1.0); // 박스가 카메라 뒤쪽
    }

    // ---- 3) 박스 내부 구간 [t0,t1]을 고정 스텝으로 행진하며 밀도 적분 ----
    const int   STEPS = 64;
    float       dt    = (t1 - t0) / STEPS;
    float       transmittance = 1.0; // 투과율 (1=완전 투명, 0=완전 불투명)

    [loop]
    for (int i = 0; i < STEPS; ++i)
    {
        // 스텝 구간의 중앙에서 샘플 (3단계 noise에서 p를 밀도장 좌표로 사용)
        float t = t0 + (i + 0.5) * dt;
        float3 p = ro + rd * t;
        float density = densityScale;

        // ---- 4) Beer-Lambert: 거리 dt 만큼 통과 시 투과율 감쇠 ----
        transmittance *= exp(-density * dt);
    }

    float  alpha      = 1.0 - transmittance;     // 누적 불투명도
    float3 fogColor   = float3(0.92, 0.93, 0.96); // 균일한 안개색 (light 없음)



    // ---- 5) 하늘 위에 안개 합성 (반투명) ----
    float3 color = lerp(sky, fogColor, alpha);
    return float4(color, 1.0);
}
