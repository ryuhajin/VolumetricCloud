// ============================================================================
//  RaymarchSphere.hlsl  —  레이마칭 픽셀 셰이더 (반투명 안개 구)
// ----------------------------------------------------------------------------
//  볼류메트릭 렌더링의 가장 작은 뼈대:
//    1) 픽셀마다 카메라 레이(원점 ro, 방향 rd)를 만든다.
//    2) 레이와 구의 교차 구간 [t0, t1]을 해석적으로 구한다.
//    3) 그 구간을 고정 스텝으로 행진하며 상수 밀도(density)를 적분한다.
//    4) Beer-Lambert 법칙으로 투과율(transmittance)을 누적해 알파를 만든다.
//    5) 절차적 하늘(sky) 위에 안개 색을 합성한다.
//
//  ※ noise 없음 / light(산란) 없음. 이후 단계에서 density에 noise를,
//    적분 루프에 light scattering을 더하면 그대로 구름으로 확장된다.
//    (자세한 수식은 doc/RAYMARCHING.md 참고)
// ============================================================================

cbuffer cbCamera : register(b0)
{
    float4x4 invViewProj;  // 역 뷰-투영 행렬 (C++에서 transpose 후 업로드)
    float3   cameraPos;    // 카메라 월드 위치 = 레이 원점
    float    time;         // 경과 시간 (현재 미사용, 추후 애니메이션용)
    float3   sphereCenter; // 안개 구 중심
    float    sphereRadius; // 안개 구 반지름
    float2   screenSize;   // 화면 픽셀 크기 (현재 미사용)
    float    densityScale; // 밀도 (클수록 불투명)
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
    float t = saturate(rd.y * 0.5 + 0.5);
    float3 horizon = float3(0.52, 0.60, 0.70);
    float3 zenith  = float3(0.18, 0.32, 0.55);
    return lerp(horizon, zenith, t);
}

// ---- 해석적 ray-sphere 교차 (rd는 정규화되어 a=1) ----
//  교차하면 true, t0(진입)/t1(탈출) 출력. t0 <= t1.
bool RaySphere(float3 ro, float3 rd, float3 center, float radius,
               out float t0, out float t1)
{
    float3 oc = ro - center;
    float  b  = dot(oc, rd);
    float  c  = dot(oc, oc) - radius * radius;
    float  h  = b * b - c;          // 판별식
    if (h < 0.0)
    {
        t0 = 0.0; t1 = 0.0;
        return false;               // 구를 빗나감
    }
    h  = sqrt(h);
    t0 = -b - h;
    t1 = -b + h;
    return true;
}

float4 main(VSOut input) : SV_TARGET
{
    // ---- 1) 픽셀 -> NDC -> 월드 레이 ----
    // uv(0,0)=좌상단 이므로 y를 뒤집어 NDC로 변환
    float2 ndc = float2(input.uv.x * 2.0 - 1.0, 1.0 - input.uv.y * 2.0);

    // 근/원 평면 점을 역투영해서 레이 방향을 만든다.
    // (C++에서 transpose 했으므로 mul(vector, matrix) 사용)
    float4 nearH = mul(float4(ndc, 0.0, 1.0), invViewProj);
    float4 farH  = mul(float4(ndc, 1.0, 1.0), invViewProj);
    float3 nearP = nearH.xyz / nearH.w;
    float3 farP  = farH.xyz  / farH.w;

    float3 ro = cameraPos;
    float3 rd = normalize(farP - nearP);

    // 배경(하늘) 색
    float3 sky = SkyColor(rd);

    // ---- 2) 구 교차 ----
    float t0, t1;
    if (!RaySphere(ro, rd, sphereCenter, sphereRadius, t0, t1))
    {
        return float4(sky, 1.0); // 구를 안 맞으면 하늘만
    }

    // 카메라가 구 안/뒤에 있을 수 있으니 진입점을 0 이상으로 클램프
    t0 = max(t0, 0.0);
    if (t1 <= t0)
    {
        return float4(sky, 1.0); // 구가 카메라 뒤쪽
    }

    // ---- 3) 구간 [t0,t1]을 고정 스텝으로 행진하며 밀도 적분 ----
    const int   STEPS = 64;
    float       dt    = (t1 - t0) / STEPS;
    float       transmittance = 1.0; // 투과율 (1=완전 투명, 0=완전 불투명)

    [loop]
    for (int i = 0; i < STEPS; ++i)
    {
        // 스텝 구간의 중앙에서 샘플 (현재는 구 내부 = 상수 밀도)
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
