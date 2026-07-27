// ============================================================================
//  VolumetricClouds.hlsl  —  광역 평면 구름층 레이마칭 픽셀 셰이더
// ----------------------------------------------------------------------------
//  볼류메트릭 렌더링의 가장 작은 뼈대:
//    1) 픽셀마다 카메라 레이(원점 ro, 방향 rd)를 만든다.
//    2) 레이와 평면 구름층의 교차 구간 [t0, t1]을 구한다.
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

float3 SkyColor(float3 rd, float3 sunDir)
{
    float up = saturate(rd.y);
    float horizonFactor = exp(-up * 5.5);
    float3 zenith = float3(0.12, 0.30, 0.58);
    float3 horizon = float3(0.58, 0.75, 0.92);
    float3 sky = lerp(zenith, horizon, horizonFactor);
    float sunAmount = saturate(dot(rd, sunDir));
    float glow = pow(sunAmount, 64.0) * 0.65 + pow(sunAmount, 1024.0) * 8.0;
    return sky + float3(1.0, 0.78, 0.52) * glow;
}

float3 ToneMap(float3 color)
{
    return 1.0 - exp(-max(color, 0.0));
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

float2 CloudLayerInterval(float3 ro, float3 rd)
{
    float envelopeBottom = cloudBaseHeight - max(heightVariation, 0.0);
    float envelopeTop = cloudBaseHeight + max(heightVariation, 0.0) +
        max(cloudThickness, 0.1) * (1.0 + saturate(thicknessVariation)) *
        max(cumulusGrowth, 1.0);
    float distanceLimit = max(maxMarchDistance, 1.0);
    float horizontal = 1.0 - step(1e-5, abs(rd.y));
    float safeY = lerp(rd.y, rd.y >= 0.0 ? 1e-5 : -1e-5, horizontal);
    float a = (envelopeBottom - ro.y) / safeY;
    float b = (envelopeTop - ro.y) / safeY;
    float2 regular = float2(max(min(a, b), 0.0), min(max(a, b), distanceLimit));
    float inside = step(envelopeBottom, ro.y) * step(ro.y, envelopeTop);
    float2 horizontalInterval = lerp(float2(1.0, 0.0), float2(0.0, distanceLimit), inside);
    return lerp(regular, horizontalInterval, horizontal);
}

float LightTransmittance(float3 p, float3 sunDir)
{
    float3 lightOrigin = p + sunDir * 0.002;
    float2 lightInterval = CloudLayerInterval(lightOrigin, sunDir);
    float validInterval = step(lightInterval.x + 1e-5, lightInterval.y);
    float distanceToExit = max(lightInterval.y, 0.0) * validInterval;
    float nearDistance = min(distanceToExit, max(localLightDistance, 0.01));
    int nearSteps = clamp(lightSteps, 1, 12);
    float nearStepLength = nearDistance / nearSteps;
    float opticalDepth = 0.0;

    [loop]
    for (int j = 0; j < 12; ++j)
    {
        if (j >= nearSteps) break;
        float lightDistance = (j + 0.5) * nearStepLength;
        float3 lightPosition = lightOrigin + sunDir * lightDistance;
        float4 weather = SampleWeather(lightPosition.xz);
        float lightDensity = EvaluateLayerCloudComponents(lightPosition, weather, time).w *
            densityMultiplier * densityScale;
        opticalDepth += lightDensity * nearStepLength * lightAbsorption;
        if (opticalDepth > 4.60517) return 0.01;
    }

    float farDistance = max(distanceToExit - nearDistance, 0.0);
    int farSteps = clamp(farLightSteps, 0, 8);
    float farStepLength = farSteps > 0 ? farDistance / farSteps : 0.0;
    [loop]
    for (int farJ = 0; farJ < 8; ++farJ)
    {
        if (farJ >= farSteps || farDistance <= 0.0) break;
        float lightDistance = nearDistance + (farJ + 0.5) * farStepLength;
        float3 lightPosition = lightOrigin + sunDir * lightDistance;
        float4 weather = SampleWeather(lightPosition.xz);
        float lightDensity = EvaluateLayerCloudComponents(lightPosition, weather, time).w *
            densityMultiplier * densityScale;
        opticalDepth += lightDensity * farStepLength * lightAbsorption;
        if (opticalDepth > 4.60517) return 0.01;
    }
    return lerp(1.0, exp(-opticalDepth), validInterval);
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

    float3 sunDir = SunDirection();
    float3 sky = SkyColor(rd, sunDir);

    // ---- 2) 광역 평면 구름층 교차 ----
    float2 cloudInterval = CloudLayerInterval(ro, rd);
    float t0 = cloudInterval.x;
    float t1 = cloudInterval.y;
    if (t1 <= t0)
    {
        return float4(ToneMap(sky * max(skyExposure, 0.0)), 1.0);
    }

    // ---- 3) 빈 공간은 2배 스텝, 밀도 구간은 기본 스텝으로 행진한다. ----
    int         steps = clamp(viewSteps, 48, 256);
    float       targetDt = (t1 - t0) / steps;
    float       fineDt = min(targetDt, max(maxViewStepLength, 0.001));
    float       rayDistance = t0 + InterleavedGradientNoise(input.pos.xy) * fineDt * saturate(jitterStrength);
    float       previousRayDistance = rayDistance;
    float       previousDensity = 0.0;
    float       transmittance = 1.0; // 투과율 (1=완전 투명, 0=완전 불투명)
    float4      debugMax = 0.0;
    float3      scattering = 0.0;
    // rd는 카메라→샘플이고 산란의 view 방향은 샘플→카메라이므로 부호를 뒤집는다.
    float       phase = DualLobePhase(dot(-rd, sunDir));
    float       debugLightVisibility = 1.0;
    float3      debugAmbient = 0.0;
    float3      debugDirect = 0.0;
    float4      debugWeather = 0.0;
    bool        foundDensity = false;
    float       cachedLightVisibility = 1.0;
    int         denseSampleIndex = 0;

    [loop]
    for (int i = 0; i < 2000; ++i)
    {
        if (rayDistance >= t1) break;
        float3 p = ro + rd * rayDistance;
        float4 weather = SampleWeather(p.xz);
        float4 components = EvaluateLayerCloudComponents(p, weather, time);
        float distanceFade = 1.0 - smoothstep(
            min(horizonFadeStart, horizonFadeEnd - 0.01),
            max(horizonFadeEnd, horizonFadeStart + 0.01), rayDistance);
        float density = components.w * densityMultiplier * densityScale * distanceFade;

        if (density > 0.0001 && previousDensity <= 0.0001 &&
            rayDistance > previousRayDistance + 1e-5)
        {
            float refineEmpty = previousRayDistance;
            float refineDense = rayDistance;
            int refineSteps = clamp(boundaryRefineSteps, 0, 5);
            [loop]
            for (int refine = 0; refine < 5; ++refine)
            {
                if (refine >= refineSteps) break;
                float refineDistance = (refineEmpty + refineDense) * 0.5;
                float3 refinePosition = ro + rd * refineDistance;
                float4 refineWeather = SampleWeather(refinePosition.xz);
                float refineDensity = EvaluateLayerCloudComponents(
                    refinePosition, refineWeather, time).w * densityMultiplier * densityScale;
                if (refineDensity > 0.0001) refineDense = refineDistance;
                else refineEmpty = refineDistance;
            }
            rayDistance = refineDense;
            p = ro + rd * rayDistance;
            weather = SampleWeather(p.xz);
            components = EvaluateLayerCloudComponents(p, weather, time);
            distanceFade = 1.0 - smoothstep(
                min(horizonFadeStart, horizonFadeEnd - 0.01),
                max(horizonFadeEnd, horizonFadeStart + 0.01), rayDistance);
            density = components.w * densityMultiplier * densityScale * distanceFade;
        }

        debugMax = max(debugMax, components);
        debugWeather = max(debugWeather, weather);

        float potential = WeatherPotential(weather);
        float weatherSkip = min(max(targetDt * 4.0, fineDt * 4.0), 1.6);
        float candidateSkip = min(max(targetDt * 2.0, fineDt * 2.0), 0.4);
        float adaptiveDt = density > 0.001 ? fineDt :
            (potential <= 0.001 ? weatherSkip : candidateSkip);
        float dt = min(adaptiveDt, t1 - rayDistance);

        // ---- 4) 태양 방향 self-shadow + 근사 다중 산란 ----
        float stepTransmittance = exp(-density * dt);
        float stepOpacity = 1.0 - stepTransmittance;
        if (density > 0.0001)
        {
            foundDensity = true;
            if ((denseSampleIndex & 1) == 0)
                cachedLightVisibility = LightTransmittance(p, sunDir);
            float lightVisibility = cachedLightVisibility;
            ++denseSampleIndex;
            float multiVisibility = MultiScatterVisibility(lightVisibility);
            float effectiveVisibility = lerp(lightVisibility, multiVisibility, saturate(multiScatterStrength));
            float localBase, localTop;
            LocalCloudLayerBounds(weather, localBase, localTop);
            float height01 = saturate((p.y - localBase) / max(localTop - localBase, 0.1));
            float3 ambientTint = lerp(float3(0.08, 0.12, 0.20),
                                      float3(0.38, 0.52, 0.72), height01);
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
        previousRayDistance = rayDistance;
        previousDensity = density;
        rayDistance += dt;
    }

    float3 debugMidPosition = ro + rd * ((t0 + t1) * 0.5);
    float4 debugMidWeather = SampleWeather(debugMidPosition.xz);
    float3 debugMidUVW = WorldToLayerUVW(debugMidPosition, debugMidWeather);

    if (renderMode == 1) return float4(debugMax.www, 1.0);
    if (renderMode == 2) return float4(debugMax.xxx, 1.0);
    if (renderMode == 3) return float4(debugMax.yyy, 1.0);
    if (renderMode == 4) return float4(debugMax.zzz, 1.0);
    if (renderMode == 5) return float4(transmittance.xxx, 1.0);
    if (renderMode == 6)
    {
        float procedural = EvaluateProceduralCloudComponents(debugMidUVW, time).w;
        float cached = EvaluateCachedCloudComponents(debugMidUVW, time).w;
        float difference = saturate(abs(procedural - cached) * 8.0);
        return float4(difference, 0.0, 1.0 - difference, 1.0);
    }
    if (renderMode == 7)
    {
        float seamError = saturate(EvaluatePeriodicSeamError(debugMidUVW) * 4096.0);
        return float4(seamError, 1.0 - seamError, 0.0, 1.0);
    }
    if (renderMode >= 8 && renderMode <= 11)
    {
        float4 baseChannels = EvaluateBaseChannels(debugMidUVW, time);
        if (renderMode == 8) return float4(baseChannels.rrr, 1.0);
        if (renderMode == 9) return float4(baseChannels.ggg, 1.0);
        if (renderMode == 10) return float4(baseChannels.bbb, 1.0);
        return float4(baseChannels.aaa, 1.0);
    }
    if (renderMode == 12) return float4((foundDensity ? debugLightVisibility : 1.0).xxx, 1.0);
    if (renderMode == 13) return float4(saturate(phase * 0.18).xxx, 1.0);
    if (renderMode == 14) return float4(saturate(debugAmbient), 1.0);
    if (renderMode == 15) return float4(saturate(debugDirect * 0.25), 1.0);
    if (renderMode == 16) return float4(debugWeather.rrr, 1.0);
    if (renderMode == 17) return float4(debugWeather.ggg, 1.0);
    if (renderMode == 18) return float4(debugWeather.bbb, 1.0);
    if (renderMode == 19) return float4(debugWeather.aaa, 1.0);
    if (showBounds != 0)
    {
        float2 grid = abs(frac((ro + rd * t0).xz / 10.0) - 0.5);
        if (min(grid.x, grid.y) < 0.012)
            return float4(1.0, 0.45, 0.08, 1.0);
    }

    // ---- 5) 남은 배경 투과율과 산란광 합성 ----
    float3 color = (scattering + sky * transmittance) * max(skyExposure, 0.0);
    return float4(ToneMap(color), 1.0);
}
