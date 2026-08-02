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
#include "CloudAtmosphere.hlsli"

cbuffer cbCamera : register(b0)
{
    float4x4 invViewProj;  // 역 뷰-투영 행렬 (C++에서 transpose 후 업로드)
    float3   cameraPos;    // 카메라 월드 위치 = 레이 원점
    float    time;         // 경과 시간 (현재 미사용, 추후 애니메이션용)
    float2   rayJitterNdc;
    float2   renderSize;
    uint     temporalOutput;
    uint3    _cameraPad;
};

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

float3 SunDirection()
{
    return CloudSunDirection(sunAzimuth, sunElevation);
}

float2 CloudLayerInterval(float3 ro, float3 rd)
{
    float envelopeBottom = cloudBaseHeight - max(heightVariation, 0.0);
    float envelopeTop = cloudBaseHeight + max(heightVariation, 0.0) +
        max(cloudThickness, 0.1) * (1.0 + saturate(thicknessVariation)) *
        max(cumulusGrowth, 1.0) * (1.0 + saturate(placementHeightVariation));
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

void BuildConeBasis(float3 direction, out float3 tangent, out float3 bitangent)
{
    float3 helper = abs(direction.y) < 0.99 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    tangent = normalize(cross(helper, direction));
    bitangent = cross(direction, tangent);
}

float LightTransmittance(float3 p, float3 sunDir, out float opticalDepth)
{
    float3 lightOrigin = p + sunDir * 0.002;
    float2 lightInterval = CloudLayerInterval(lightOrigin, sunDir);
    float validInterval = step(lightInterval.x + 1e-5, lightInterval.y);
    float distanceToExit = max(lightInterval.y, 0.0) * validInterval;
    float nearDistance = min(distanceToExit, max(localLightDistance, 0.01));
    int nearSteps = clamp(lightSteps, 1, 5);
    float nearStepLength = nearDistance / nearSteps;
    opticalDepth = 0.0;
    float3 tangent, bitangent;
    BuildConeBasis(sunDir, tangent, bitangent);

    [loop]
    for (int j = 0; j < 5; ++j)
    {
        if (j >= nearSteps) break;
        float normalizedDistance = (j + 0.5) / nearSteps;
        float lightDistance = normalizedDistance * nearDistance;
        float angle = (j * 2.39996323) + dot(p.xz, float2(0.73, 1.37));
        float radius = max(lightConeRadius, 0.0) * normalizedDistance * normalizedDistance;
        float3 coneOffset = (cos(angle) * tangent + sin(angle) * bitangent) * radius;
        float3 lightPosition = lightOrigin + sunDir * lightDistance + coneOffset;
        float4 placement = SamplePlacement(lightPosition.xz, time);
        if (PlacementCoarsePotential(placement) <= 0.0)
            continue;
        float4 weather = SampleWeather(lightPosition.xz, time);
        float lightDensity =
            EvaluateLayerCloudComponents(lightPosition, weather, placement, time).w *
            densityMultiplier;
        opticalDepth += lightDensity * nearStepLength * lightAbsorption;
        if (opticalDepth > 4.60517) return 0.01;
    }

    float farDistance = max(distanceToExit - nearDistance, 0.0);
    if (farLightSteps > 0 && farDistance > 0.0)
    {
        float lightDistance = nearDistance + farDistance * 0.5;
        float3 lightPosition = lightOrigin + sunDir * lightDistance;
        float4 placement = SamplePlacement(lightPosition.xz, time);
        if (PlacementCoarsePotential(placement) > 0.0)
        {
            float4 weather = SampleWeather(lightPosition.xz, time);
            float lightDensity =
                EvaluateLayerCloudMacroDensity(lightPosition, weather, placement, time) *
                densityMultiplier;
            opticalDepth += lightDensity * farDistance * lightAbsorption;
        }
    }
    return lerp(1.0, exp(-opticalDepth), validInterval);
}

float SkyAmbientVisibility(float3 p)
{
    float segmentLength = max(localLightDistance, 0.1) * 0.5;
    float opticalDepth = 0.0;
    [unroll]
    for (int j = 0; j < 2; ++j)
    {
        float3 samplePosition = p + float3(0.0, (j + 0.5) * segmentLength, 0.0);
        float4 placement = SamplePlacement(samplePosition.xz, time);
        if (PlacementCoarsePotential(placement) > 0.0)
        {
            float4 weather = SampleWeather(samplePosition.xz, time);
            opticalDepth +=
                EvaluateLayerCloudMacroDensity(samplePosition, weather, placement, time) *
                densityMultiplier * segmentLength * lightAbsorption;
        }
    }
    return exp(-opticalDepth);
}

float InterleavedGradientNoise(float2 pixel)
{
    return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

float4 RenderCloud(VSOut input, out float firstCloudDistance, out float finalTransmittance)
{
    firstCloudDistance = 0.0;
    finalTransmittance = 1.0;
    // ---- 1) 픽셀 -> NDC -> 월드 레이 ----
    // uv(0,0)=좌상단 이므로 y를 뒤집어 NDC로 변환 (NDC 우하단 (1,-1), UV 우하단 (1,1))
    float2 ndc = float2(input.uv.x * 2.0 - 1.0, 1.0 - input.uv.y * 2.0) + rayJitterNdc;

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
    float3 sky = CloudSkyColor(rd, sunDir);

    // ---- 2) 광역 평면 구름층 교차 ----
    float2 cloudInterval = CloudLayerInterval(ro, rd);
    float t0 = cloudInterval.x;
    float t1 = cloudInterval.y;
    if (t1 <= t0)
    {
        if (temporalOutput != 0)
            return 0.0;
        return float4(CloudToneMap(sky * max(skyExposure, 0.0)), 1.0);
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
    float       debugPlacementSupport = 0.0;
    float2      debugPlacementAttributes = 0.0;
    bool        foundDensity = false;
    float       cachedLightVisibility = 1.0;
    float       cachedLightOpticalDepth = 0.0;
    float       cachedAmbientVisibility = 1.0;
    int         denseSampleIndex = 0;

    [loop]
    for (int i = 0; i < 2000; ++i)
    {
        if (rayDistance >= t1) break;
        float3 p = ro + rd * rayDistance;
        float4 placement = SamplePlacement(p.xz, time);
        float4 weather = 0.0;
        float4 components = 0.0;
        if (PlacementCoarsePotential(placement) > 0.0)
        {
            weather = SampleWeather(p.xz, time);
            components = EvaluateLayerCloudComponents(p, weather, placement, time);
        }
        float distanceFade = 1.0 - smoothstep(
            min(horizonFadeStart, horizonFadeEnd - 0.01),
            max(horizonFadeEnd, horizonFadeStart + 0.01), rayDistance);
        float density = components.w * densityMultiplier * distanceFade;

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
                float4 refinePlacement = SamplePlacement(refinePosition.xz, time);
                float refineDensity = 0.0;
                if (PlacementCoarsePotential(refinePlacement) > 0.0)
                {
                    float4 refineWeather = SampleWeather(refinePosition.xz, time);
                    refineDensity = EvaluateLayerCloudComponents(
                        refinePosition, refineWeather, refinePlacement, time).w *
                        densityMultiplier;
                }
                if (refineDensity > 0.0001) refineDense = refineDistance;
                else refineEmpty = refineDistance;
            }
            rayDistance = refineDense;
            p = ro + rd * rayDistance;
            placement = SamplePlacement(p.xz, time);
            weather = SampleWeather(p.xz, time);
            components = EvaluateLayerCloudComponents(p, weather, placement, time);
            distanceFade = 1.0 - smoothstep(
                min(horizonFadeStart, horizonFadeEnd - 0.01),
                max(horizonFadeEnd, horizonFadeStart + 0.01), rayDistance);
            density = components.w * densityMultiplier * distanceFade;
        }

        debugMax = max(debugMax, components);
        debugWeather = max(debugWeather, weather);
        float localBase, localTop;
        LocalCloudLayerBounds(weather, placement, localBase, localTop);
        float placementHeight01 =
            saturate((p.y - localBase) / max(localTop - localBase, 0.1));
        float currentPlacementSupport =
            PlacementHeightSupport(placement, placementHeight01);
        if (currentPlacementSupport > debugPlacementSupport)
        {
            debugPlacementSupport = currentPlacementSupport;
            debugPlacementAttributes = placement.gb;
        }

        float potential =
            WeatherPotential(weather) * PlacementCoarsePotential(placement);
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
            if (!foundDensity)
                firstCloudDistance = rayDistance;
            foundDensity = true;
            if ((denseSampleIndex & 1) == 0)
            {
                cachedLightVisibility = LightTransmittance(
                    p, sunDir, cachedLightOpticalDepth);
                cachedAmbientVisibility = SkyAmbientVisibility(p);
            }
            float lightVisibility = cachedLightVisibility;
            ++denseSampleIndex;
            LocalCloudLayerBounds(weather, placement, localBase, localTop);
            float height01 = saturate((p.y - localBase) / max(localTop - localBase, 0.1));
            float3 ambientTint = lerp(float3(0.08, 0.12, 0.20),
                                      float3(0.38, 0.52, 0.72), height01);
            float ambientOcclusion = lerp(
                1.0, cachedAmbientVisibility, saturate(ambientOcclusionStrength));
            float3 ambient = ambientTint * ambientIntensity * ambientOcclusion;
            float cosTheta = dot(-rd, sunDir);
            float singlePhase = DualLobePhaseWithG(cosTheta, phaseG);
            float secondPhase = DualLobePhaseWithG(
                cosTheta, phaseG * saturate(multiScatterEccentricityAttenuation));
            float secondVisibility = exp(
                -cachedLightOpticalDepth * saturate(multiScatterExtinctionAttenuation));
            float directScatter = lightVisibility * singlePhase +
                secondVisibility * secondPhase * 0.5 * saturate(multiScatterStrength);
            float viewAwayFromSun = 1.0 - saturate(cosTheta * 0.5 + 0.5);
            float powderDepth = 1.0 - exp(-cachedLightOpticalDepth * 1.5);
            float powder = 1.0 + powderDepth * viewAwayFromSun * powderStrength;
            float forward = pow(saturate(cosTheta), 8.0);
            float thinEdge = (1.0 - smoothstep(0.12, 1.10, density)) *
                saturate(lightVisibility * 1.5);
            float silver = forward * thinEdge * silverLiningStrength;
            float3 sunColor = float3(1.0, 0.92, 0.78);
            float3 direct = sunColor * sunIntensity *
                (directScatter * powder + silver);
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
    finalTransmittance = transmittance;

    float3 debugMidPosition = ro + rd * ((t0 + t1) * 0.5);
    float4 debugMidWeather = SampleWeather(debugMidPosition.xz, time);
    float3 debugMidUVW = WorldToLayerUVW(debugMidPosition, debugMidWeather, time);

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
    if (renderMode == 20)
        return float4((foundDensity ? cachedAmbientVisibility : 1.0).xxx, 1.0);
    if (renderMode == 23) return float4(debugPlacementSupport.xxx, 1.0);
    if (renderMode == 24) return float4(debugPlacementAttributes.xxx, 1.0);
    if (renderMode == 25) return float4(debugPlacementAttributes.yyy, 1.0);
    if (showBounds != 0)
    {
        float2 grid = abs(frac((ro + rd * t0).xz / 10.0) - 0.5);
        if (min(grid.x, grid.y) < 0.012)
            return float4(1.0, 0.45, 0.08, 1.0);
    }

    // ---- 5) 남은 배경 투과율과 산란광 합성 ----
    if (temporalOutput != 0)
        return float4(scattering * max(skyExposure, 0.0), 1.0);
    float3 color = (scattering + sky * transmittance) * max(skyExposure, 0.0);
    return float4(CloudToneMap(color), 1.0);
}

struct CloudOutput
{
    float4 color : SV_TARGET0;
    float2 metadata : SV_TARGET1;
};

CloudOutput main(VSOut input)
{
    CloudOutput output;
    output.color = RenderCloud(input, output.metadata.x, output.metadata.y);
    return output;
}
