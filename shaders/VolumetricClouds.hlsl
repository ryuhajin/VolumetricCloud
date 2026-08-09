// ============================================================================
//  VolumetricClouds.hlsl - 단계 8 환경광·다중 산란 합성
// ----------------------------------------------------------------------------
//  한 프레임의 렌더링 순서
//  1. CPU가 Camera/Cloud/Light/Environment를 b0/b1/b3/b4에 복사한다.
//  2. 앞선 DiagnosticScene 패스가 불투명 Scene Color와 Scene Depth를 만든다.
//  3. 이 풀스크린 PS가 UV → 월드 레이 → 깊이 거리 순으로 복원한다.
//  4. 레이와 AABB의 교차 구간을 구하고 Scene Depth보다 뒤를 잘라 낸다.
//  5. 월드 XZ Weather R/G/B로 배치·종류·밀도를 결정해 Base Shape를 만든다.
//  6. Base가 비어 있지 않을 때만 Detail로 깎는다.
//  7. 밀도가 있는 View 표본에서 태양 방향 Light Ray로 광학 깊이를 잰다.
//  8. 카메라→표본과 표본→태양 방향으로 Dual-lobe Phase Factor를 구한다.
//  9. 직접광에 하늘·지면 환경광과 광학 깊이 기반 다중 산란을 더한다.
// 10. 각 조명 성분을 View 투과율과 함께 누적해 장면 위에 합성한다.
// 11. 디버그 모드면 선택한 중간 조명 성분을 출력한다.
//
//  단계 9 Early Exit와 단계 14 실제 대기/Cube Map 입력은 아직 없다.
//  Light Ray는 비용을 분리하기 위해 Detail이 아닌 Base Density만 샘플링한다.
// ============================================================================

// CPU Renderer::CameraCB와 같은 96바이트 b0 상수버퍼다.
cbuffer cbCamera : register(b0)
{
    float4x4 invViewProj; // CPU invViewProj. UV/깊이를 월드 공간(m)으로 되돌린다.
    float3 cameraPos;     // CPU cameraPos. 월드 공간(m), 모든 레이의 원점.
    float time;           // CPU time. 초 단위, 단계 2 바람 이동에 사용한다.
    float2 renderSize;    // CPU renderSize. 현재 백버퍼 크기(pixel), 현재 예약 값.
    float nearPlane;      // CPU nearPlane. 카메라 근평면 거리(m), 현재 예약 값.
    float farPlane;       // CPU farPlane. 하늘 픽셀의 최대 추적 거리(m).
};

// cbCamera의 time 선언 뒤 포함해야 Light Ray가 같은 애니메이션 시간을 사용한다.
#include "CloudEnvironment.hlsli"

// t0: 앞선 DiagnosticScene PS가 R16G16B16A16_FLOAT에 쓴 linear RGB 장면색.
Texture2D<float4> sceneColorTexture : register(t0);
// t1: 같은 패스의 D32 depth를 R32_FLOAT SRV로 읽는 장치 깊이(near=0, far=1).
Texture2D<float> sceneDepthTexture : register(t1);
// s0: 깊이와 장면색을 픽셀 경계에서 섞지 않고 읽는 point+clamp sampler.
SamplerState pointClampSampler : register(s0);

// Fullscreen.hlsl의 정점 셰이더가 넘기는 화면 전체 삼각형 출력.
struct VSOut
{
    float4 position : SV_POSITION; // rasterizer가 정한 화면 픽셀 위치.
    float2 uv : TEXCOORD0;         // 좌상단 (0,0), 우하단 (1,1)의 화면 UV.
};

// 구름 패스가 이후 단계까지 유지할 합성 결과.
struct CloudResult
{
    float3 scattering;       // 안개가 카메라 쪽으로 새로 더한 linear RGB 빛.
    float transmittance;     // 뒤 배경빛의 생존 비율. 1=완전 투명, 0=완전 불투명.
    float representativeDepth; // 적분 구간 대표 거리(m), 이후 temporal/upsample용.
};

// 단계 1 교차, 단계 2 noise와 단계 3 높이 적분이 사용한 대표값 진단 자료.
struct CloudMarchDebug
{
    float entryDistance;   // Scene Depth 제한 전 AABB 진입을 0 이상으로 자른 거리(m).
    float exitDistance;    // Scene Depth로 제한된 실제 이탈 거리(m).
    float stepCount;       // 실제 반복 횟수. 색 출력 편의를 위해 float로 보관.
    float sampledDensity;  // 대표 중간 위치의 최종 noise 밀도. hit가 없으면 0.
    float hit;             // 유효 적분 구간이면 1, 아니면 0.
    float rawNoise;        // 대표 중간 위치의 threshold 전 value noise(0~1).
    float thresholdDensity;// coverage threshold와 remap만 적용한 밀도(0~1).
    float heightFraction;  // 대표 위치의 AABB 정규화 높이. 바닥 0, 천장 1.
    float heightProfile;   // 대표 위치의 상·하단 fade 곱(0~1).
    float baseDensity;     // 대표 위치의 Detail 적용 전 큰 구름 밀도.
    float detailNoise;     // 대표 위치에서 실제로 샘플한 고주파 noise.
    float erosion;         // 대표 위치에서 Base로부터 뺄 밀도.
    float detailSampled;   // 대표 위치가 Detail 함수를 실행했으면 1.
    float weatherCoverage; // 대표 위치 Weather R.
    float cloudType;       // 대표 위치 Weather G.
    float weatherDensityModifier; // 대표 위치 Weather B의 0.5~1.5 배율.
    float weatherThresholdDensity;// Weather coverage 적용 threshold.
    float typedHeightProfile; // Cloud Type 적용 높이 마스크.
    float3 noiseUvw;       // 대표 중간 위치의 연속 noise 좌표(cycle).
    float3 detailNoiseUvw; // 대표 Detail noise 좌표(cycle), 생략 시 0.
    float2 weatherUv;      // 대표 Weather Map UV(0~1).
    float lightTransmittance; // 대표 위치에서 태양까지 살아남은 직접광 비율.
    float lightOpticalDepth;  // 대표 위치에서 태양까지의 Base 광학 깊이.
    float totalLightSamples;  // 이 픽셀의 모든 View 표본이 실행한 Light 표본 합계.
    float3 directScattering;  // 대표 위치의 직접 단일 산란 linear RGB.
    float phaseCosTheta;      // 카메라→표본과 표본→태양의 내적. +1이면 태양을 바라봄.
    float forwardPhaseLobe;   // 양의 g를 사용한 전방 HG 값.
    float backwardPhaseLobe;  // 음의 g를 사용한 후방 HG 값.
    float dualPhaseFactor;    // 직접 산란에 실제로 적용한 최종 [0,16] 배율.
    float3 accumulatedDirect; // View Ray 전체의 직접 태양광 누적값.
};

// 화면 UV를 DirectX NDC로 바꾼다.
// 입력/출력은 단위 없는 좌표이며 UV의 아래 방향 Y를 NDC의 위 방향 Y로 뒤집는다.
float2 UvToNdc(float2 uv)
{
    return float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
}

// 화면 UV가 가리키는 월드 레이 방향을 복원한다.
// 출력은 길이 1인 월드 방향이므로 이후 tStart/tEnd가 meter 거리가 된다.
// homogeneous w가 0에 가까울 때 안전값을 사용해 NaN을 막는다.
float3 ReconstructWorldRay(float2 uv)
{
    // 1. far plane의 NDC 점을 inverse view-projection으로 월드에 되돌린다.
    float2 ndc = UvToNdc(uv);
    float4 farH = mul(float4(ndc, 1.0, 1.0), invViewProj);
    float safeW = abs(farH.w) > 1e-6 ? farH.w : (farH.w < 0.0 ? -1e-6 : 1e-6);
    float3 farPosition = farH.xyz / safeW;

    // 2. 카메라에서 그 점으로 향하는 화살표를 정규화한다.
    return normalize(farPosition - cameraPos);
}

// 장치 깊이와 화면 UV에서 불투명 표면의 월드 위치(m)를 복원한다.
// deviceDepth는 D3D 범위 [0,1]이며 비선형이므로 farPlane을 단순히 곱하지 않는다.
float3 ReconstructWorldPosition(float2 uv, float deviceDepth)
{
    // 1. 같은 픽셀의 NDC X/Y와 깊이를 한 점으로 만든다.
    float4 worldH = mul(float4(UvToNdc(uv), deviceDepth, 1.0), invViewProj);

    // 2. 원근 나눗셈을 안전하게 수행해 월드 위치를 얻는다.
    float safeW = abs(worldH.w) > 1e-6 ? worldH.w : (worldH.w < 0.0 ? -1e-6 : 1e-6);
    return worldH.xyz / safeW;
}

// 불투명 장면이 없는 픽셀 뒤에 사용할 간단한 배경 하늘색.
float3 SkyColor(float3 rayDirection)
{
    float height = saturate(rayDirection.y * 0.5 + 0.5);
    return lerp(float3(0.55, 0.63, 0.72), float3(0.12, 0.27, 0.52), height);
}

// 현재 CloudCB의 월드 AABB와 레이를 교차하고 실제 적분 구간을 만든다.
// sceneDistance(m)는 첫 불투명 표면까지 거리이며 그 뒤쪽 안개를 보이지 않게 자른다.
// 카메라가 박스 안이면 raw tNear가 음수이므로 tStart를 0으로 고정한다.
bool IntersectCloudVolume(float3 rayOrigin, float3 rayDirection,
                          float sceneDistance,
                          out float tStart, out float tEnd)
{
    tStart = 0.0;
    tEnd = 0.0;

    // 1. AABB 자체의 진입/이탈 거리를 구한다.
    float tNear = 0.0;
    float tFar = 0.0;
    bool intersectsAabb = IntersectRayAABB(
        rayOrigin, rayDirection, cloudBoundsMin, cloudBoundsMax, tNear, tFar);

    // 2. 뒤쪽 진입은 카메라 위치부터, 이탈은 첫 불투명 물체까지만 허용한다.
    //    tStart가 커지면 안개가 더 멀리서 시작하고, tEnd가 작아지면 보이는 두께가 줄어든다.
    if (intersectsAabb)
    {
        tStart = max(tNear, 0.0);
        tEnd = min(tFar, sceneDistance);
    }

    // 3. 물체가 볼륨 앞에 있거나 접점뿐이면 계산할 부피가 없다.
    return intersectsAabb && tEnd > tStart;
}

// AABB의 유효 구간을 noise × 높이 프로파일 밀도로 레이 마칭한다.
// 입력은 월드 위치(m), 정규화 월드 방향, 장면 거리(m)이고 출력은 합성 가능한
// CloudResult와 관찰용 CloudMarchDebug다. 모든 실패 경로는 산란 0, 투과율 1의
// 중립 결과를 반환해 배경을 바꾸지 않는다.
CloudResult RaymarchCloud(float3 rayOrigin, float3 rayDirection,
                          float sceneDistance,
                          out CloudMarchDebug debugData)
{
    CloudResult result = (CloudResult)0;
    result.transmittance = 1.0;
    result.representativeDepth = sceneDistance;

    debugData = (CloudMarchDebug)0;

    // 1. 물체 폐색까지 반영된 AABB 구간을 구한다.
    float tStart = 0.0;
    float tEnd = 0.0;
    bool hasSegment = IntersectCloudVolume(
        rayOrigin, rayDirection, sceneDistance, tStart, tEnd);
    if (hasSegment)
    {
        debugData.entryDistance = tStart;
        debugData.exitDistance = tEnd;

        // 2. stepSize는 목표 간격이고, 실제 간격은 전체 구간을 빠짐없이 덮도록 다시 나눈다.
        float segmentLength = tEnd - tStart;
        float safeStepSize = max(stepSize, 1e-4);
        uint safeMaxSteps = max(maxViewSteps, 1u);
        uint stepCount = min(safeMaxSteps, (uint)ceil(segmentLength / safeStepSize));
        float actualStepLength = segmentLength / (float)stepCount;

        // 3. View Ray 소멸계수와 대표 표본을 준비한다.
        float extinction = max(extinctionCoefficient, 0.0);

        // 디버그 모드는 같은 대표 위치에서 raw→threshold→final→UVW를 비교한다.
        float representativeDistance = (tStart + tEnd) * 0.5;
        float3 representativePosition =
            rayOrigin + rayDirection * representativeDistance;
        CloudDensitySample representativeSample =
            SampleCloudDensity(representativePosition, time);
        debugData.rawNoise = representativeSample.rawNoise;
        debugData.thresholdDensity = representativeSample.thresholdDensity;
        debugData.heightFraction = representativeSample.heightFraction;
        debugData.heightProfile = representativeSample.heightProfile;
        debugData.baseDensity = representativeSample.baseDensity;
        debugData.detailNoise = representativeSample.detailNoise;
        debugData.erosion = representativeSample.erosion;
        debugData.detailSampled = representativeSample.detailSampled;
        debugData.weatherCoverage = representativeSample.weatherCoverage;
        debugData.cloudType = representativeSample.cloudType;
        debugData.weatherDensityModifier = representativeSample.weatherDensityModifier;
        debugData.weatherThresholdDensity = representativeSample.weatherThresholdDensity;
        debugData.typedHeightProfile = representativeSample.typedHeightProfile;
        debugData.sampledDensity = representativeSample.finalDensity;
        debugData.noiseUvw = representativeSample.noiseUvw;
        debugData.detailNoiseUvw = representativeSample.detailNoiseUvw;
        debugData.weatherUv = representativeSample.weatherUv;

        // Phase는 한 View Ray 안에서 방향이 변하지 않으므로 픽셀당 한 번만 계산한다.
        // viewRayDirection은 카메라→표본, directionToSun은 표본→태양이며 두 방향이
        // 나란한 cosTheta=+1이 태양을 바라보는 전방 산란 방향이다.
        PhaseSample phase = EvaluateDualLobePhase(
            rayDirection, directionToSun);
        debugData.phaseCosTheta = phase.cosTheta;
        debugData.forwardPhaseLobe = phase.forwardLobe;
        debugData.backwardPhaseLobe = phase.backwardLobe;
        debugData.dualPhaseFactor = phase.phaseFactor;

        // 조명 디버그를 선택했을 때만 대표 위치에 추가 Light Ray를 쏜다.
        // 실제 합성용 Light Ray는 아래 View 반복 안에서 따로 누적한다.
        if (debugMode == 24 || debugMode == 25 || debugMode == 27)
        {
            LightMarchResult representativeLight = ComputeLightTransmittance(
                representativePosition, directionToSun);
            debugData.lightTransmittance = representativeLight.transmittance;
            debugData.lightOpticalDepth = representativeLight.opticalDepth;
            debugData.directScattering = IntegrateSingleScattering(
                representativeSample.finalDensity,
                representativeLight.transmittance, 1.0, actualStepLength,
                phase.phaseFactor);
        }

        // 4. 각 구간 중앙에서 Base를 만들고 필요한 위치에서만 Detail로 침식한다.
        [loop]
        for (uint stepIndex = 0u; stepIndex < stepCount; ++stepIndex)
        {
            float sampleDistance = tStart + ((float)stepIndex + 0.5) * actualStepLength;
            float3 samplePosition = rayOrigin + rayDirection * sampleDistance;
            CloudDensitySample densitySample = SampleCloudDensity(samplePosition, time);
            float sampledDensity = densitySample.finalDensity;
            float sampledStepTransmittance = exp(
                -sampledDensity * extinction * actualStepLength);

            // 5. 최종 합성, Light 비용과 누적 직접광 모드에서만 조명 적분을 실행한다.
            bool requiresLighting = debugMode == 0 || debugMode == 26 ||
                                    debugMode == 32;
            if (sampledDensity > 0.0 && requiresLighting)
            {
                LightMarchResult light = { 1.0, 0.0, 0.0 };
                bool needsLightRay = debugMode == 0 || debugMode == 26 ||
                                     debugMode == 32;
                if (needsLightRay)
                {
                    light = ComputeLightTransmittance(
                        samplePosition, directionToSun);
                    debugData.totalLightSamples += light.stepCount;
                }
                EnvironmentLightingSample lighting = EvaluateEnvironmentLighting(
                    densitySample, light, phase, result.transmittance,
                    actualStepLength);
                debugData.accumulatedDirect += lighting.direct;
                result.scattering += lighting.direct + lighting.skyAmbient +
                                     lighting.groundBounce +
                                     lighting.multipleScattering;
            }
            result.transmittance *= sampledStepTransmittance;
            // 단계 9 Early Exit 자리: 현재는 transmittanceThreshold를 사용하지 않고
            // 항상 stepCount 전체를 돌아 fine/coarse 적분의 동일성을 먼저 검증한다.
        }

        // 6. 디버그와 이후 temporal 단계가 사용할 최종 값을 기록한다.
        result.transmittance = saturate(result.transmittance);
        result.representativeDepth = (tStart + tEnd) * 0.5;
        debugData.stepCount = (float)stepCount;
        debugData.hit = 1.0;
    }
    return result;
}

// 최종 합성 전용 경로가 반환하는 최소 통계다. 기존 CloudMarchDebug처럼 모든 중간
// 물리값을 들고 있지 않아 일반 화면에서 레지스터 압력을 줄인다.
struct OptimizationMarchDebug
{
    float executedFineSteps;
    float skippedDistanceRatio;
    float earlyExitSavings;
    float supportPrecheckRejects;
    float stateTransitions;
    float hit;
};

// 단계 9 최종 합성용 레이마칭이다.
// Search 모드는 빈 교실 복도를 빠르게 지나가듯 Base만 큰 간격으로 확인하고,
// 구름 후보를 찾으면 한 coarse 구간을 되돌아가 Full 모드에서 정밀 적분한다.
void RaymarchCloudOptimized(
    float3 rayOrigin, float3 rayDirection, float sceneDistance,
    out CloudResult output,
    out OptimizationMarchDebug optimizationDebug)
{
    output.scattering = 0.0.xxx;
    output.transmittance = 1.0;
    output.representativeDepth = sceneDistance;
    optimizationDebug.executedFineSteps = 0.0;
    optimizationDebug.skippedDistanceRatio = 0.0;
    optimizationDebug.earlyExitSavings = 0.0;
    optimizationDebug.supportPrecheckRejects = 0.0;
    optimizationDebug.stateTransitions = 0.0;
    optimizationDebug.hit = 0.0;

    float tStart = 0.0;
    float tEnd = 0.0;
    if (!IntersectCloudVolume(
            rayOrigin, rayDirection, sceneDistance, tStart, tEnd))
        return;

    optimizationDebug.hit = 1.0;
    float segmentLength = tEnd - tStart;
    uint safeMaxSteps = max(maxViewSteps, 1u);
    float targetFineStep = max(stepSize, 1e-4);
    // maxViewSteps에 걸렸을 때도 전체 구간을 반드시 덮도록 fine 간격을 키운다.
    float fineStep = max(targetFineStep, segmentLength / (float)safeMaxSteps);
    float safeCoarseMultiplier = clamp(coarseStepMultiplier, 1.0, 8.0);
    float coarseStep = fineStep * safeCoarseMultiplier;
    float safeDensityEpsilon = clamp(baseDensityEpsilon, 0.0, 0.05);
    uint safeEmptyLimit = clamp(emptySamplesBeforeCoarse, 1u, 8u);
    float safeThreshold = clamp(transmittanceThreshold, 0.0, 0.1);
    float extinction = max(extinctionCoefficient, 0.0);

    PhaseSample phase = EvaluateDualLobePhase(rayDirection, directionToSun);
    bool searchMode = emptySpaceSkippingEnabled != 0u;
    uint consecutiveEmpty = 0u;
    float distance = tStart;
    float lastIntegratedDistance = tStart;
    uint iteration = 0u;
    uint maxIterations = safeMaxSteps * 3u + 16u;

    [loop]
    while (distance < tEnd && iteration < maxIterations)
    {
        ++iteration;
        if (searchMode)
        {
            float searchLength = min(coarseStep, tEnd - distance);
            float probeDistance = distance + searchLength * 0.5;
            float3 probePosition = rayOrigin + rayDirection * probeDistance;
            CloudDensitySample baseProbe = MakeEmptyCloudDensitySample();
            if (supportPrecheckEnabled != 0u)
                EvaluateBaseCloudDensityFast(probePosition, time, baseProbe);
            else
                baseProbe = EvaluateBaseCloudDensity(probePosition, time);
            optimizationDebug.supportPrecheckRejects += baseProbe.supportRejected;

            if (baseProbe.baseDensity <= safeDensityEpsilon)
            {
                optimizationDebug.skippedDistanceRatio += searchLength;
                distance += searchLength;
                continue;
            }

            // 후보를 찾으면 방금 건너뛴 구간의 시작으로 돌아가 얇은 경계를 다시 확인한다.
            float rewind = max(tStart, distance - coarseStep);
            distance = max(lastIntegratedDistance, rewind);
            searchMode = false;
            consecutiveEmpty = 0u;
            optimizationDebug.stateTransitions += 1.0;
            continue;
        }

        float viewStepLength = min(fineStep, tEnd - distance);
        float sampleDistance = distance + viewStepLength * 0.5;
        float3 samplePosition = rayOrigin + rayDirection * sampleDistance;
        CloudDensitySample baseSample = MakeEmptyCloudDensitySample();
        if (supportPrecheckEnabled != 0u)
            EvaluateBaseCloudDensityFast(samplePosition, time, baseSample);
        else
            baseSample = EvaluateBaseCloudDensity(samplePosition, time);
        optimizationDebug.supportPrecheckRejects += baseSample.supportRejected;
        CloudDensitySample densitySample = ApplyDetailErosion(
            baseSample, samplePosition, time);
        float density = densitySample.finalDensity;

        if (density > 0.0)
        {
            LightMarchResult light = ComputeLightTransmittance(
                samplePosition, directionToSun);
            EnvironmentLightingSample lighting = EvaluateEnvironmentLighting(
                densitySample, light, phase, output.transmittance,
                viewStepLength);
            output.scattering += lighting.direct + lighting.skyAmbient +
                                 lighting.groundBounce +
                                 lighting.multipleScattering;
        }

        float stepTransmittance = exp(
            -density * extinction * viewStepLength);
        output.transmittance *= stepTransmittance;
        optimizationDebug.executedFineSteps += 1.0;
        distance += viewStepLength;
        lastIntegratedDistance = distance;

        if (baseSample.baseDensity <= safeDensityEpsilon)
            ++consecutiveEmpty;
        else
            consecutiveEmpty = 0u;

        if (earlyExitEnabled != 0u && safeThreshold > 0.0 &&
            output.transmittance <= safeThreshold)
        {
            float remainingSteps = ceil(max(tEnd - distance, 0.0) / fineStep);
            optimizationDebug.earlyExitSavings = remainingSteps;
            break;
        }

        if (emptySpaceSkippingEnabled != 0u &&
            consecutiveEmpty >= safeEmptyLimit && distance < tEnd)
        {
            searchMode = true;
            consecutiveEmpty = 0u;
            optimizationDebug.stateTransitions += 1.0;
        }
    }

    output.transmittance = saturate(output.transmittance);
    output.representativeDepth = (tStart + tEnd) * 0.5;
    optimizationDebug.skippedDistanceRatio = saturate(
        optimizationDebug.skippedDistanceRatio / max(segmentLength, 1e-5));
}

// 최종 풀스크린 픽셀 셰이더.
// UV → 레이 → 깊이 → 교차 → 레이 마칭 → 디버그 → 합성 순서를 한곳에서 보여 준다.
float4 mainLegacy(VSOut input) : SV_TARGET
{
    // 1. UV를 유효 범위로 제한하고 같은 픽셀의 Scene Depth를 읽는다.
    float2 uv = saturate(input.uv);
    float deviceDepth = sceneDepthTexture.SampleLevel(pointClampSampler, uv, 0);
    bool hasGeometry = deviceDepth < 0.999999;

    // 2. 카메라에서 픽셀로 나가는 월드 레이를 복원한다.
    float3 rayDirection = ReconstructWorldRay(uv);

    // 3. 깊이가 있으면 월드 표면과 meter 거리를 복원하고, 하늘이면 farPlane을 쓴다.
    float3 worldPosition = hasGeometry
        ? ReconstructWorldPosition(uv, deviceDepth)
        : cameraPos + rayDirection * farPlane;
    float sceneDistance = hasGeometry
        ? length(worldPosition - cameraPos)
        : farPlane;

    // 4. AABB 교차와 Scene Depth 제한 뒤 noise × 높이 프로파일 밀도를 적분한다.
    CloudMarchDebug marchDebug;
    CloudResult cloud = RaymarchCloud(
        cameraPos, rayDirection, sceneDistance, marchDebug);
    OptimizationMarchDebug optimizationDebug;
    if (debugMode >= 38 && debugMode <= 42)
    {
        CloudResult ignoredCloud;
        RaymarchCloudOptimized(
            cameraPos, rayDirection, sceneDistance,
            ignoredCloud, optimizationDebug);
    }
    else
    {
        optimizationDebug.executedFineSteps = 0.0;
        optimizationDebug.skippedDistanceRatio = 0.0;
        optimizationDebug.earlyExitSavings = 0.0;
        optimizationDebug.supportPrecheckRejects = 0.0;
        optimizationDebug.stateTransitions = 0.0;
        optimizationDebug.hit = 0.0;
    }

    // 5. 단계 0 디버그 1~4는 그대로 유지한다.
    if (debugMode == 1)
        return float4(rayDirection * 0.5 + 0.5, 1.0);
    if (debugMode == 2)
        return float4(saturate(sceneDistance / 30.0).xxx, 1.0);
    if (debugMode == 3)
    {
        float3 positionBands = frac(abs(worldPosition) * 0.2);
        return float4(hasGeometry ? positionBands : 0.0.xxx, 1.0);
    }
    if (debugMode == 4)
        return float4(uv, 0.0, 1.0);

    // 6. 단계 1 디버그 5~9는 교차·step·투과율·밀도를 각각 분리해 보여 준다.
    if (debugMode == 5)
        return float4((marchDebug.hit * saturate(marchDebug.entryDistance / 20.0)).xxx, 1.0);
    if (debugMode == 6)
        return float4((marchDebug.hit * saturate(marchDebug.exitDistance / 20.0)).xxx, 1.0);
    if (debugMode == 7)
        return float4((marchDebug.hit * saturate(marchDebug.stepCount / max((float)maxViewSteps, 1.0))).xxx, 1.0);
    if (debugMode == 8)
        return float4(cloud.transmittance.xxx, 1.0);
    if (debugMode == 9)
        return float4((marchDebug.sampledDensity * marchDebug.hit).xxx, 1.0);
    if (debugMode == 10)
        return float4((marchDebug.rawNoise * marchDebug.hit).xxx, 1.0);
    if (debugMode == 11)
        return float4((marchDebug.thresholdDensity * marchDebug.hit).xxx, 1.0);
    if (debugMode == 12)
        return float4((marchDebug.sampledDensity * marchDebug.hit).xxx, 1.0);
    if (debugMode == 13)
        return float4(frac(marchDebug.noiseUvw) * marchDebug.hit, 1.0);
    if (debugMode == 14)
        return float4((marchDebug.heightFraction * marchDebug.hit).xxx, 1.0);
    if (debugMode == 15)
        return float4((marchDebug.heightProfile * marchDebug.hit).xxx, 1.0);
    if (debugMode == 16)
        return float4((marchDebug.baseDensity * marchDebug.hit).xxx, 1.0);
    if (debugMode == 17)
        return float4((marchDebug.detailNoise * marchDebug.hit).xxx, 1.0);
    if (debugMode == 18)
        return float4((marchDebug.erosion * marchDebug.hit).xxx, 1.0);
    if (debugMode == 19)
        return float4((marchDebug.detailSampled * marchDebug.hit).xxx, 1.0);
    if (debugMode == 20)
        return float4((marchDebug.weatherCoverage * marchDebug.hit).xxx, 1.0);
    if (debugMode == 21)
        return float4((marchDebug.cloudType * marchDebug.hit).xxx, 1.0);
    if (debugMode == 22)
        return float4((marchDebug.weatherThresholdDensity * marchDebug.hit).xxx, 1.0);
    if (debugMode == 23)
        return float4((marchDebug.typedHeightProfile * marchDebug.hit).xxx, 1.0);
    if (debugMode == 24)
        return float4((marchDebug.lightTransmittance * marchDebug.hit).xxx, 1.0);
    if (debugMode == 25)
    {
        float opticalDepthView = 1.0 - exp(-max(marchDebug.lightOpticalDepth, 0.0));
        return float4((opticalDepthView * marchDebug.hit).xxx, 1.0);
    }
    if (debugMode == 26)
    {
        float maxSamples = max((float)(maxViewSteps * max(maxLightSteps, 1u)), 1.0);
        float normalizedCost = sqrt(saturate(marchDebug.totalLightSamples / maxSamples));
        float3 heat = normalizedCost < 0.5
            ? lerp(float3(0.0, 0.12, 0.25), float3(0.0, 0.9, 0.8), normalizedCost * 2.0)
            : lerp(float3(0.0, 0.9, 0.8), float3(1.0, 0.9, 0.1), (normalizedCost - 0.5) * 2.0);
        return float4(heat * marchDebug.hit, 1.0);
    }
    if (debugMode == 27)
    {
        float3 mappedScattering = marchDebug.directScattering /
            (1.0.xxx + max(marchDebug.directScattering, 0.0.xxx));
        return float4(mappedScattering * marchDebug.hit, 1.0);
    }
    if (debugMode == 28)
    {
        float mappedCosTheta = marchDebug.phaseCosTheta * 0.5 + 0.5;
        return float4((mappedCosTheta * marchDebug.hit).xxx, 1.0);
    }
    if (debugMode == 29)
    {
        float exposedForward = 1.0 - exp(
            -0.25 * max(marchDebug.forwardPhaseLobe, 0.0));
        return float4(float3(1.0, 0.45, 0.08) *
                      exposedForward * marchDebug.hit, 1.0);
    }
    if (debugMode == 30)
    {
        float exposedBackward = 1.0 - exp(
            -0.25 * max(marchDebug.backwardPhaseLobe, 0.0));
        return float4(float3(0.10, 0.42, 1.0) *
                      exposedBackward * marchDebug.hit, 1.0);
    }
    if (debugMode == 31)
    {
        float factor = clamp(marchDebug.dualPhaseFactor, 0.0, kMaxPhaseFactor);
        float3 factorColor = factor < 1.0
            ? lerp(float3(0.10, 0.35, 1.0), 1.0.xxx, factor)
            : lerp(1.0.xxx, float3(1.0, 0.82, 0.08),
                   saturate((factor - 1.0) / 3.0));
        return float4(factorColor * marchDebug.hit, 1.0);
    }
    if (debugMode == 32)
    {
        float3 component = marchDebug.accumulatedDirect;
        float3 mapped = max(component, 0.0.xxx) /
            (1.0.xxx + max(component, 0.0.xxx));
        return float4(mapped * marchDebug.hit, 1.0);
    }
    if (debugMode == 38)
    {
        float value = sqrt(saturate(
            optimizationDebug.executedFineSteps / max((float)maxViewSteps, 1.0)));
        return float4(value * float3(0.0, 0.9, 0.8) * optimizationDebug.hit, 1.0);
    }
    if (debugMode == 39)
        return float4((optimizationDebug.skippedDistanceRatio *
                       optimizationDebug.hit).xxx, 1.0);
    if (debugMode == 40)
    {
        float value = sqrt(saturate(
            optimizationDebug.earlyExitSavings / max((float)maxViewSteps, 1.0)));
        return float4(value * float3(1.0, 0.75, 0.08) * optimizationDebug.hit, 1.0);
    }
    if (debugMode == 41)
    {
        float value = saturate(optimizationDebug.supportPrecheckRejects /
                               max((float)maxViewSteps, 1.0));
        return float4(value * float3(0.15, 0.55, 1.0) * optimizationDebug.hit, 1.0);
    }
    if (debugMode == 42)
    {
        float value = saturate(optimizationDebug.stateTransitions / 8.0);
        return float4(value * float3(0.85, 0.2, 1.0) * optimizationDebug.hit, 1.0);
    }

    // 7. 모드 0: 안개가 더한 빛 + 안개를 통과한 배경빛으로 최종 합성한다.
    float3 background = hasGeometry
        ? sceneColorTexture.SampleLevel(pointClampSampler, uv, 0).rgb
        : SkyColor(rayDirection);
    float3 composite = cloud.scattering + background * cloud.transmittance;
    return float4(composite, 1.0);
}

// 최종 합성 모드 0 전용 엔트리 포인트다. 디버그 대표 표본과 CloudMarchDebug를
// 만들지 않아 일반 실행에서 불필요한 분기와 레지스터 생존 범위를 제거한다.
float4 mainOptimized(VSOut input) : SV_TARGET
{
    float2 uv = saturate(input.uv);
    float deviceDepth = sceneDepthTexture.SampleLevel(pointClampSampler, uv, 0);
    bool hasGeometry = deviceDepth < 0.999999;
    float3 rayDirection = ReconstructWorldRay(uv);
    float3 worldPosition = hasGeometry
        ? ReconstructWorldPosition(uv, deviceDepth)
        : cameraPos + rayDirection * farPlane;
    float sceneDistance = hasGeometry
        ? length(worldPosition - cameraPos)
        : farPlane;

    OptimizationMarchDebug ignoredDebug;
    CloudResult marched;
    RaymarchCloudOptimized(
        cameraPos, rayDirection, sceneDistance, marched, ignoredDebug);
    float3 background = hasGeometry
        ? sceneColorTexture.SampleLevel(pointClampSampler, uv, 0).rgb
        : SkyColor(rayDirection);
    return float4(marched.scattering +
                  background * marched.transmittance, 1.0);
}
