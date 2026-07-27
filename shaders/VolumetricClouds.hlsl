// ============================================================================
//  VolumetricClouds.hlsl  —  레이마칭 픽셀 셰이더 (AABB 박스 볼륨)
// ----------------------------------------------------------------------------
//  볼류메트릭 렌더링의 가장 작은 뼈대:
//    1) 픽셀마다 카메라 레이(원점 ro, 방향 rd)를 만든다.
//    2) 레이와 AABB 박스의 교차 구간 [t0, t1]을 slab 방식으로 구한다.
//    3) periodic noise 밀도를 적분한다.
//    4) 태양 방향 light march로 self-shadow를 구한다.
//    5) dual-lobe 위상, 근사 다중 산란과 Beer-Lambert 투과율을 합성한다.
// ============================================================================

#include "Ray.hlsli"
#include "CloudNoise.hlsli"

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

float3 SunDirection()
{
    float azimuth = radians(sunAzimuth);
    float elevation = radians(sunElevation);
    float cosElevation = cos(elevation);
    return normalize(float3(
        cosElevation * cos(azimuth),
        sin(elevation),
        cosElevation * sin(azimuth)));
}

float LightTransmittance(float3 p, float3 sunDir, float3 boxMin, float3 boxMax)
{
    float lightT0 = 0.0;
    float lightT1 = 0.0;
    float3 lightOrigin = p + sunDir * 0.002;
    if (!RayBox(lightOrigin, sunDir, boxMin, boxMax, lightT0, lightT1))
        return 1.0;

    float distanceToExit = max(lightT1, 0.0);
    int steps = clamp(lightSteps, 1, 12);
    float stepLength = distanceToExit / steps;
    float visibility = 1.0;

    [loop]
    for (int j = 0; j < 12; ++j)
    {
        if (j >= steps) break;
        float lightDistance = (j + 0.5) * stepLength;
        float3 lightPosition = lightOrigin + sunDir * lightDistance;
        float3 lightUVW = (lightPosition - boxMin) / (boxMax - boxMin);
        float lightDensity = EvaluateCloudDensity(lightUVW, time) * densityScale;
        visibility *= exp(-lightDensity * stepLength * lightAbsorption);
        if (visibility < 0.01) break;
    }
    return visibility;
}

float InterleavedGradientNoise(float2 pixel)
{
    return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

float MultiScatterVisibility(float visibility)
{
    float energy = 0.0;
    float weight = 0.5;
    float softenedVisibility = saturate(visibility);
    [unroll]
    for (int octave = 0; octave < 3; ++octave)
    {
        energy += softenedVisibility * weight;
        softenedVisibility = sqrt(softenedVisibility);
        weight *= 0.5;
    }
    return energy / 0.875;
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

    // ---- 3) 빈 공간은 2배 스텝, 밀도 구간은 기본 스텝으로 행진한다. ----
    int         steps = clamp(viewSteps, 48, 128);
    float       baseDt = (t1 - t0) / steps;
    float       rayDistance = t0 + InterleavedGradientNoise(input.pos.xy) * baseDt * saturate(jitterStrength);
    float       transmittance = 1.0; // 투과율 (1=완전 투명, 0=완전 불투명)
    float4      debugMax = 0.0;
    float3      scattering = 0.0;
    float3      sunDir = SunDirection();
    // rd는 카메라→샘플이고 산란의 view 방향은 샘플→카메라이므로 부호를 뒤집는다.
    float       phase = DualLobePhase(dot(-rd, sunDir));
    float       debugLightVisibility = 1.0;
    float3      debugAmbient = 0.0;
    float3      debugDirect = 0.0;
    bool        foundDensity = false;

    [loop]
    for (int i = 0; i < 128; ++i)
    {
        if (i >= steps || rayDistance >= t1) break;
        float3 p = ro + rd * rayDistance;
        float3 uvw = (p - boxMin) / (boxMax - boxMin);
        float4 components = EvaluateCloudComponents(uvw, time);
        float density = components.w * densityMultiplier * densityScale;
        debugMax = max(debugMax, components);

        float stepMultiplier = density > 0.001 ? 1.0 : 2.0;
        float dt = min(baseDt * stepMultiplier, t1 - rayDistance);

        // ---- 4) 태양 방향 self-shadow + 근사 다중 산란 ----
        float stepTransmittance = exp(-density * dt);
        float stepOpacity = 1.0 - stepTransmittance;
        if (density > 0.0001)
        {
            foundDensity = true;
            float lightVisibility = LightTransmittance(p, sunDir, boxMin, boxMax);
            float multiVisibility = MultiScatterVisibility(lightVisibility);
            float effectiveVisibility = lerp(lightVisibility, multiVisibility, saturate(multiScatterStrength));
            float3 ambientTint = lerp(float3(0.18, 0.24, 0.34),
                                      float3(0.46, 0.57, 0.74), saturate(uvw.y));
            float3 ambient = ambientTint * ambientIntensity;
            float powder = 1.0 + (1.0 - exp(-density * dt * 2.0)) * 0.75 * powderStrength;
            float forward = pow(saturate(dot(-rd, sunDir)), 8.0);
            float thinEdge = 1.0 - smoothstep(0.15, 1.25, density);
            float silver = forward * thinEdge * silverLiningStrength;
            float3 sunColor = float3(1.0, 0.92, 0.78);
            float3 direct = sunColor * sunIntensity *
                (effectiveVisibility * phase * powder + silver);
            scattering += transmittance * stepOpacity * (ambient + direct);
            debugLightVisibility = min(debugLightVisibility, lightVisibility);
            debugAmbient = max(debugAmbient, ambient);
            debugDirect = max(debugDirect, direct);
        }
        transmittance *= stepTransmittance;
        if (transmittance < 0.01) break;
        rayDistance += dt;
    }

    if (renderMode == 1) return float4(debugMax.www, 1.0);
    if (renderMode == 2) return float4(debugMax.xxx, 1.0);
    if (renderMode == 3) return float4(debugMax.yyy, 1.0);
    if (renderMode == 4) return float4(debugMax.zzz, 1.0);
    if (renderMode == 5) return float4(transmittance.xxx, 1.0);
    if (renderMode == 6)
    {
        float3 midUVW = (ro + rd * ((t0 + t1) * 0.5) - boxMin) / (boxMax - boxMin);
        float procedural = EvaluateProceduralCloudComponents(midUVW, time).w;
        float cached = EvaluateCachedCloudComponents(midUVW, time).w;
        float difference = saturate(abs(procedural - cached) * 8.0);
        return float4(difference, 0.0, 1.0 - difference, 1.0);
    }
    if (renderMode == 7)
    {
        float3 midUVW = (ro + rd * ((t0 + t1) * 0.5) - boxMin) / (boxMax - boxMin);
        float seamError = saturate(EvaluatePeriodicSeamError(midUVW) * 4096.0);
        return float4(seamError, 1.0 - seamError, 0.0, 1.0);
    }
    if (renderMode >= 8 && renderMode <= 11)
    {
        float3 midUVW = (ro + rd * ((t0 + t1) * 0.5) - boxMin) / (boxMax - boxMin);
        float4 baseChannels = EvaluateBaseChannels(midUVW, time);
        if (renderMode == 8) return float4(baseChannels.rrr, 1.0);
        if (renderMode == 9) return float4(baseChannels.ggg, 1.0);
        if (renderMode == 10) return float4(baseChannels.bbb, 1.0);
        return float4(baseChannels.aaa, 1.0);
    }
    if (renderMode == 12) return float4((foundDensity ? debugLightVisibility : 1.0).xxx, 1.0);
    if (renderMode == 13) return float4(saturate(phase * 0.18).xxx, 1.0);
    if (renderMode == 14) return float4(saturate(debugAmbient), 1.0);
    if (renderMode == 15) return float4(saturate(debugDirect * 0.25), 1.0);

    // 진입점이 두 축의 경계에 가까우면 AABB 와이어를 표시한다.
    float3 entryUVW = (ro + rd * t0 - boxMin) / (boxMax - boxMin);
    float3 edgeDistance = min(entryUVW, 1.0 - entryUVW);
    int edgeAxes = (edgeDistance.x < 0.012) + (edgeDistance.y < 0.012) + (edgeDistance.z < 0.012);
    if (showBounds != 0 && edgeAxes >= 2)
        return float4(1.0, 0.45, 0.08, 1.0);

    // ---- 5) 남은 배경 투과율과 산란광 합성 ----
    float3 color = (scattering + sky * transmittance) * max(skyExposure, 0.0);
    return float4(color, 1.0);
}
