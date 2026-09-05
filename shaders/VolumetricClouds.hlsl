// ============================================================================
//  VolumetricClouds.hlsl - 단계 8 환경광·다중 산란 합성
// ----------------------------------------------------------------------------
//  한 프레임의 렌더링 순서
//  1. CPU가 Camera/Cloud/Light/Environment를 b0/b1/b3/b4에 복사한다.
//  2. 앞선 DiagnosticScene 패스가 불투명 Scene Color와 Scene Depth를 만든다.
//  3. 이 풀스크린 PS가 UV → 월드 레이 → 깊이 거리 순으로 복원한다.
//  4. 레이와 PlanarLayer의 교차 구간을 구하고 Scene Depth보다 뒤를 잘라 낸다.
//  5. 월드 XZ Weather R/G/B로 배치·종류·밀도를 결정해 Base Shape를 만든다.
//  6. Base가 비어 있지 않을 때만 Detail로 깎는다.
//  7. 밀도가 있는 View 표본에서 태양 방향 Light Ray로 광학 깊이를 잰다.
//  8. 카메라→표본과 표본→태양 방향으로 Dual-lobe Phase Factor를 구한다.
//  9. 직접광에 하늘·지면 환경광과 광학 깊이 기반 다중 산란을 더한다.
// 10. 각 조명 성분을 View 투과율과 함께 누적해 장면 위에 합성한다.
// 11. 디버그 모드면 선택한 중간 조명 성분을 출력한다.
//
//  Stage 9 High 고정 최적화와 Stage 14 물리 대기 LUT를 함께 사용한다.
//  Light Ray는 비용을 분리하기 위해 Detail이 아닌 Base Density만 샘플링한다.
// ============================================================================

// CPU Renderer::CameraCB와 같은 224바이트 b0 상수버퍼다.
cbuffer cbCamera : register(b0)
{
    float4x4 invViewProj; // CPU invViewProj. UV/깊이를 월드 공간(m)으로 되돌린다.
    float4x4 invProjection;   // NDC를 translation 없는 View Space로 복원한다.
    float4x4 invViewRotation; // View 방향에 카메라 회전만 적용한다.
    float3 cameraPos;     // CPU cameraPos. 월드 공간(m), 모든 레이의 원점.
    float time;           // CPU time. 초 단위, 단계 2 바람 이동에 사용한다.
    float2 renderSize;    // CPU renderSize. 현재 백버퍼 크기(pixel), 현재 예약 값.
    float nearPlane;      // CPU nearPlane. 카메라 근평면 거리(m), 현재 예약 값.
    float farPlane;       // CPU farPlane. 하늘 픽셀의 최대 추적 거리(m).
};

// cbCamera의 time 선언 뒤 포함해야 Light Ray가 같은 애니메이션 시간을 사용한다.
#include "CloudDomainParameters.hlsli"
#include "HighCloudQuality.hlsli"
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
    float representativeDepth; // 불투명도 가중 구름 깊이(m).
};

// 단계 1 교차, 단계 2 noise와 단계 3 높이 적분이 사용한 대표값 진단 자료.
struct CloudMarchDebug
{
    float entryDistance;   // 선택한 도메인의 진입을 0 이상으로 자른 거리(m).
    float exitDistance;    // Scene Depth로 제한된 실제 이탈 거리(m).
    float segmentLength;   // 실제 적분 구간 길이(m).
    float sampledDensity;  // 대표 중간 위치의 최종 noise 밀도. hit가 없으면 0.
    float hit;             // 유효 적분 구간이면 1, 아니면 0.
    float rawNoise;        // 대표 중간 위치의 threshold 전 Base 조합값(0~1).
    float thresholdDensity;// coverage threshold와 remap만 적용한 밀도(0~1).
    float heightFraction;  // 대표 위치의 PlanarLayer 정규화 높이. 바닥 0, 천장 1.
    float heightProfile;   // 대표 위치의 상·하단 fade 곱(0~1).
    float baseDensity;     // 대표 위치의 Detail 적용 전 큰 구름 밀도.
    float detailNoise;     // 대표 위치에서 실제로 샘플한 고주파 noise.
    float erosion;         // 대표 위치에서 Base로부터 뺄 밀도.
    float detailSampled;   // 대표 위치가 Detail 함수를 실행했으면 1.
    float weatherCoverage; // 대표 위치 Weather R.
    float cloudType;       // 대표 위치 Weather G.
    float weatherDensityModifier; // 대표 위치 Weather B의 0.5~1.5 배율.
    float weatherThresholdDensity;// Weather coverage 적용 threshold.
    float typedShapeProfile; // Cloud Type/높이가 정한 shape threshold 마스크.
    float effectiveShapeCoverage; // profile까지 적용한 Base 통과 coverage.
    float baseSupport;       // 밀도 배율·Detail 전 Base shape 존재 마스크.
    float weatherThicknessPotential; // Weather A: 로컬 두께 보간값.
    float localThicknessMeters;      // 해당 XZ 기둥의 물리 두께(m).
    float localBaseLiftMeters;       // 전역 바닥에서 올라간 로컬 바닥(m).
    float localHeightFraction;    // 로컬 바닥 0, 로컬 상단 1.
    float3 noiseUvw;       // 대표 중간 위치의 연속 noise 좌표(cycle).
    float3 detailNoiseUvw; // 대표 Detail noise 좌표(cycle), 생략 시 0.
    float2 weatherUv;      // 대표 Weather Map UV(0~1).
    float lightTransmittance; // 대표 위치에서 태양까지 살아남은 직접광 비율.
    float lightOpticalDepth;  // 대표 위치에서 태양까지의 Base 광학 깊이.
    float3 directScattering;  // 대표 위치의 직접 단일 산란 linear RGB.
    float phaseCosTheta;      // 카메라→표본과 표본→태양의 내적. +1이면 태양을 바라봄.
    float forwardPhaseLobe;   // 양의 g를 사용한 전방 HG 값.
    float backwardPhaseLobe;  // 음의 g를 사용한 후방 HG 값.
    float dualPhaseFactor;    // 직접 산란에 실제로 적용한 최종 [0,16] 배율.
    float3 accumulatedDirect; // View Ray 전체의 직접 태양광 누적값.
    float3 accumulatedSky;    // View Ray 전체의 분석적 하늘 환경광.
    float3 accumulatedGround; // View Ray 전체의 분석적 지면 반사 근사.
    float3 accumulatedMultiple;// View Ray 전체의 다중 산란 근사.
    float3 accumulatedSilverLining; // 외곽 범위 Phase가 추가한 양의 직접광.
    float shapedSunVisibilitySum;   // 조명 가중 평균용 직접광 투과율 합.
    float ambientVisibilitySum;     // 조명 가중 평균용 환경광 가시성 합.
    float lightingDiagnosticWeight; // 두 가시성 진단의 공통 분모.
    float viewOpticalDepth;   // View Ray 전체의 final density 광학 깊이.
    float4 baseNoiseChannels; // 단계 13-4 Base Texture3D RGBA.
    float4 detailNoiseChannels;// 단계 13-4 Detail Texture3D RGBA.
    float stage12NearOpticalDepth;
    float stage12FarOpticalDepth;
    float stage12NearWeight;
    float stage12FarValidity;
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
    // 1. NDC far 점을 카메라가 원점인 View Space로 되돌린다.
    float2 ndc = UvToNdc(uv);
    float4 viewH = mul(float4(ndc, 1.0, 1.0), invProjection);
    float safeW = abs(viewH.w) > 1e-6 ? viewH.w :
        (viewH.w < 0.0 ? -1e-6 : 1e-6);
    float3 viewDirection = normalize(viewH.xyz / safeW);

    // 2. w=0 방향 벡터에 inverse View의 회전만 적용한다.
    //    큰 camera/world translation은 이 계산에 들어오지 않는다.
    return normalize(mul(float4(viewDirection, 0.0), invViewRotation).xyz);
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

// 이전 단계의 0~0.8 합성값은 그대로 두고, LDR swap chain에서 1 초과값이
// 통째로 흰색에 고정되는 구간만 hue를 유지한 채 부드럽게 압축한다.
float3 ApplyLdrHighlightShoulder(float3 color)
{
    float3 safeColor = max(color, 0.0.xxx);
    float peak = max(safeColor.r, max(safeColor.g, safeColor.b));
    const float knee = 0.80;
    float3 result = safeColor;
    if (peak > knee)
    {
        float excess = peak - knee;
        float mappedPeak = knee + excess * (1.0 - knee) /
            (excess + (1.0 - knee));
        result = safeColor * (mappedPeak / max(peak, 1e-6));
    }
    return result;
}

// Y 평면층과 레이를 교차하고 scene depth로 실제 적분 구간을 제한한다.
// sceneDistance(m)는 첫 불투명 표면까지 거리이며 그 뒤쪽 구름을 보이지 않게 자른다.
bool IntersectCloudVolume(float3 rayOrigin, float3 rayDirection,
                          float sceneDistance,
                          out float tStart, out float tEnd)
{
    tStart = 0.0;
    tEnd = 0.0;

    return IntersectCloudDomain(
        rayOrigin, rayDirection, sceneDistance, false, tStart, tEnd);
}

// PlanarLayer의 유효 구간을 Texture3D × 로컬 프로파일 밀도로 레이 마칭한다.
// 입력은 월드 위치(m), 정규화 월드 방향, 장면 거리(m)이고 출력은 합성 가능한
// CloudResult와 관찰용 CloudMarchDebug다. 모든 실패 경로는 산란 0, 투과율 1의
// 중립 결과를 반환해 배경을 바꾸지 않는다.

float Stage9ViewStep(float sampleDistance)
{
    float distanceBlend = smoothstep(
        kHighDistanceStepStartMeters, kHighDistanceStepEndMeters,
        sampleDistance);
    return kHighViewStepMeters * lerp(
        1.0, kHighFarStepMultiplier, distanceBlend);
}

CloudResult RaymarchCloud(float3 rayOrigin, float3 rayDirection,
                          float sceneDistance,
                          out CloudMarchDebug debugData)
{
    CloudResult result = (CloudResult)0;
    result.transmittance = 1.0;
    result.representativeDepth = sceneDistance;
    debugData = (CloudMarchDebug)0;

    float tStart = 0.0;
    float tEnd = 0.0;
    if (!IntersectCloudVolume(rayOrigin, rayDirection, sceneDistance,
                              tStart, tEnd))
        return result;

    float segmentLength = tEnd - tStart;
    debugData.entryDistance = tStart;
    debugData.exitDistance = tEnd;
    debugData.segmentLength = segmentLength;

    float representativeDistance = 0.5 * (tStart + tEnd);
    CloudLightingContext lightingContext = BuildCloudLightingContext();
    float3 representativePosition = rayOrigin +
        rayDirection * representativeDistance;
    CloudDensitySample representativeSample = SampleCloudDensity(
        representativePosition, time, true);
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
    debugData.typedShapeProfile = representativeSample.typedShapeProfile;
    debugData.effectiveShapeCoverage = representativeSample.effectiveShapeCoverage;
    debugData.baseSupport = representativeSample.baseSupport;
    debugData.weatherThicknessPotential = representativeSample.weatherThicknessPotential;
    debugData.localThicknessMeters = representativeSample.localThicknessMeters;
    debugData.localBaseLiftMeters = representativeSample.localBaseLiftMeters;
    debugData.localHeightFraction = representativeSample.localHeightFraction;
    debugData.sampledDensity = representativeSample.finalDensity;
    debugData.noiseUvw = representativeSample.noiseUvw;
    debugData.detailNoiseUvw = representativeSample.detailNoiseUvw;
    debugData.weatherUv = representativeSample.weatherUv;
    debugData.baseNoiseChannels = representativeSample.baseNoiseChannels;
    debugData.detailNoiseChannels = representativeSample.detailNoiseChannels;

    PhaseSample phase = EvaluateDualLobePhase(rayDirection, directionToSun);
    debugData.phaseCosTheta = phase.cosTheta;
    debugData.forwardPhaseLobe = phase.forwardLobe;
    debugData.backwardPhaseLobe = phase.backwardLobe;
    debugData.dualPhaseFactor = phase.phaseFactor;
    if (debugMode == 24 || debugMode == 25 || debugMode == 27)
    {
        LightMarchResult representativeLight = ComputeLightTransmittance(
            representativePosition, directionToSun);
        debugData.lightTransmittance = representativeLight.transmittance;
        debugData.lightOpticalDepth = representativeLight.opticalDepth;
        debugData.directScattering = IntegrateSingleScattering(
            representativeSample.finalDensity,
            representativeLight.transmittance, 1.0,
            Stage9ViewStep(representativeDistance), phase.phaseFactor,
            CloudSunIncident(lightingContext));
    }
    if (debugMode >= 74 && debugMode <= 77)
    {
        Stage12ShadowSample cacheSample = SampleStage12DeepShadow(
            representativePosition);
        debugData.stage12NearOpticalDepth = cacheSample.nearOpticalDepth;
        debugData.stage12FarOpticalDepth = cacheSample.farOpticalDepth;
        debugData.stage12NearWeight = cacheSample.nearWeight;
        debugData.stage12FarValidity = cacheSample.farValidity;
    }

    float cursor = tStart;
    uint consecutiveEmpty = 0u;
    bool coarseSearch = false;
    float extinction = max(extinctionCoefficient, 0.0);
    float opacityDepthMoment = 0.0;
    float opacityWeight = 0.0;
    uint safeMaxSteps = kHighMaximumViewSteps;
    [loop]
    for (uint iteration = 0u;
         iteration < safeMaxSteps && cursor < tEnd - 1e-5;
         ++iteration)
    {
        float fullStep = Stage9ViewStep(cursor);
        float candidateStep = coarseSearch
            ? min(fullStep * kHighCoarseStepMultiplier,
                  max(kHighMaximumSearchStepMeters, fullStep))
            : fullStep;
        float marchLength = min(candidateStep, tEnd - cursor);
        float sampleDistance = cursor + 0.5 * marchLength;
        float3 samplePosition = rayOrigin + rayDirection * sampleDistance;

        if (coarseSearch)
        {
            CloudDensitySample candidate = SampleCloudDensity(
                samplePosition, time, false);
            if (candidate.baseDensity > kHighBaseDensityEpsilon)
            {
                cursor = max(tStart, cursor - marchLength);
                coarseSearch = false;
                consecutiveEmpty = 0u;
                continue;
            }
            cursor += marchLength;
            continue;
        }

        CloudDensitySample densitySample = SampleCloudDensity(
            samplePosition, time, true);
        float distanceFade = CloudViewDistanceFade(sampleDistance);
        densitySample.baseDensity *= distanceFade;
        densitySample.finalDensity *= distanceFade;
        float sampledDensity = densitySample.finalDensity;
        float stepTransmittance = exp(
            -sampledDensity * extinction * marchLength);
        debugData.viewOpticalDepth += sampledDensity * extinction * marchLength;

        bool requiresLighting = debugMode == 0 ||
            debugMode == 32 || debugMode == 53 || debugMode == 54 ||
            debugMode == 55 || debugMode == 57 || debugMode == 58 ||
            debugMode == 59;
        if (sampledDensity > 0.0 && requiresLighting)
        {
            LightMarchResult light = ComputeLightTransmittance(
                samplePosition, directionToSun);
            EnvironmentLightingSample lighting = EvaluateEnvironmentLighting(
                densitySample, light, phase, result.transmittance, marchLength,
                lightingContext);
            debugData.accumulatedDirect += lighting.direct;
            debugData.accumulatedSky += lighting.skyAmbient;
            debugData.accumulatedGround += lighting.groundBounce;
            debugData.accumulatedMultiple += lighting.multipleScattering;
            debugData.accumulatedSilverLining += lighting.silverLiningContribution;
            debugData.shapedSunVisibilitySum +=
                lighting.shapedSunVisibility * lighting.diagnosticWeight;
            debugData.ambientVisibilitySum +=
                lighting.ambientVisibility * lighting.diagnosticWeight;
            debugData.lightingDiagnosticWeight += lighting.diagnosticWeight;
            result.scattering += lighting.direct + lighting.skyAmbient +
                lighting.groundBounce + lighting.multipleScattering;
        }
        float sampleOpacityContribution = result.transmittance *
            (1.0 - stepTransmittance);
        opacityDepthMoment += sampleDistance * sampleOpacityContribution;
        opacityWeight += sampleOpacityContribution;
        result.transmittance *= stepTransmittance;
        cursor += marchLength;

        consecutiveEmpty = densitySample.baseDensity <= kHighBaseDensityEpsilon
            ? consecutiveEmpty + 1u : 0u;
        if (consecutiveEmpty >= kHighEmptySamplesBeforeCoarse)
            coarseSearch = true;
        if (result.transmittance <= kHighTransmittanceThreshold)
        {
            break;
        }
    }

    result.transmittance = saturate(result.transmittance);
    result.representativeDepth = opacityWeight > 1e-6
        ? opacityDepthMoment / opacityWeight : sceneDistance;
    debugData.hit = 1.0;
    return result;
}

// 최종 풀스크린 픽셀 셰이더.
// UV → 레이 → 깊이 → 교차 → 레이 마칭 → 디버그 → 합성 순서를 한곳에서 보여 준다.
float4 RenderCloudOutput(VSOut input, bool hasGeometry,
                         float3 rayDirection, CloudResult cloud,
                         CloudMarchDebug marchDebug, float sceneDistance)
{
    float2 uv = saturate(input.uv);
    // 5. 1~7은 13-4D에서 삭제한 사용자 디버그 ID다. CPU가 오래된 값을
    // 전달해도 별도 분기로 들어가지 않고 아래 최종 HDR 합성으로 안전하게 폴백한다.
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
        return float4((marchDebug.typedShapeProfile * marchDebug.hit).xxx, 1.0);
    if (debugMode == 24)
        return float4((marchDebug.lightTransmittance * marchDebug.hit).xxx, 1.0);
    if (debugMode == 25)
    {
        float opticalDepthView = 1.0 - exp(-max(marchDebug.lightOpticalDepth, 0.0));
        return float4((opticalDepthView * marchDebug.hit).xxx, 1.0);
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
    if (debugMode == 33)
        return float4((marchDebug.hit * CloudDebugDistanceValue(
            marchDebug.segmentLength)).xxx, 1.0);
    if (debugMode == 35)
        return float4(marchDebug.hit.xxx, 1.0);
    if (debugMode >= 36 && debugMode <= 39)
        return float4((marchDebug.baseNoiseChannels[debugMode - 36] *
                       marchDebug.hit).xxx, 1.0);
    if (debugMode == 40)
        return float4((marchDebug.rawNoise * marchDebug.hit).xxx, 1.0);
    if (debugMode >= 41 && debugMode <= 44)
        return float4((marchDebug.detailNoiseChannels[debugMode - 41] *
                       marchDebug.hit).xxx, 1.0);
    if (debugMode == 45)
        return float4((marchDebug.detailNoise * marchDebug.hit).xxx, 1.0);
    if (debugMode == 46)
    {
        float3 baseUvw = marchDebug.noiseUvw;
        float3 detailUvw = marchDebug.detailNoiseUvw;
        float4 baseOrigin = baseNoiseVolumeTexture.SampleLevel(
            weatherMapSampler, baseUvw, 0);
        float4 detailOrigin = detailNoiseVolumeTexture.SampleLevel(
            weatherMapSampler, detailUvw, 0);
        float difference = 0.0;
        difference = max(difference, max(max(abs(baseOrigin -
            baseNoiseVolumeTexture.SampleLevel(weatherMapSampler,
                baseUvw + float3(1,0,0), 0)).r,
            abs(baseOrigin - baseNoiseVolumeTexture.SampleLevel(
                weatherMapSampler, baseUvw + float3(0,1,0), 0)).g),
            abs(baseOrigin - baseNoiseVolumeTexture.SampleLevel(
                weatherMapSampler, baseUvw + float3(0,0,1), 0)).b));
        difference = max(difference, max(max(abs(detailOrigin -
            detailNoiseVolumeTexture.SampleLevel(weatherMapSampler,
                detailUvw + float3(1,0,0), 0)).r,
            abs(detailOrigin - detailNoiseVolumeTexture.SampleLevel(
                weatherMapSampler, detailUvw + float3(0,1,0), 0)).g),
            abs(detailOrigin - detailNoiseVolumeTexture.SampleLevel(
                weatherMapSampler, detailUvw + float3(0,0,1), 0)).b));
        return float4((saturate(difference * 255.0) * marchDebug.hit).xxx, 1.0);
    }
    if (debugMode == 47)
        return float4((marchDebug.weatherThicknessPotential * marchDebug.hit).xxx, 1.0);
    if (debugMode == 48)
        return float4((saturate(marchDebug.localThicknessMeters / 6000.0) *
                       marchDebug.hit).xxx, 1.0);
    if (debugMode == 49)
    {
        float insideLocalColumn = marchDebug.localHeightFraction <= 1.0 ? 1.0 : 0.0;
        return float4((saturate(marchDebug.localHeightFraction) *
                       insideLocalColumn * marchDebug.hit).xxx, 1.0);
    }
    if (debugMode == 50)
        return float4((marchDebug.effectiveShapeCoverage * marchDebug.hit).xxx, 1.0);
    if (debugMode == 51)
        return float4((marchDebug.baseSupport * marchDebug.hit).xxx, 1.0);
    if (debugMode == 52)
    {
        float mappedDepth = 1.0 - exp(-max(marchDebug.viewOpticalDepth, 0.0));
        return float4((mappedDepth * marchDebug.hit).xxx, 1.0);
    }
    if (debugMode == 53)
    {
        float3 mapped = max(marchDebug.accumulatedSky, 0.0.xxx) /
            (1.0.xxx + max(marchDebug.accumulatedSky, 0.0.xxx));
        return float4(mapped * marchDebug.hit, 1.0);
    }
    if (debugMode == 54)
    {
        float3 mapped = max(marchDebug.accumulatedGround, 0.0.xxx) /
            (1.0.xxx + max(marchDebug.accumulatedGround, 0.0.xxx));
        return float4(mapped * marchDebug.hit, 1.0);
    }
    if (debugMode == 55)
    {
        float3 mapped = max(marchDebug.accumulatedMultiple, 0.0.xxx) /
            (1.0.xxx + max(marchDebug.accumulatedMultiple, 0.0.xxx));
        return float4(mapped * marchDebug.hit, 1.0);
    }
    if (debugMode == 56)
        return float4((saturate(cloud.representativeDepth /
                       max(farPlane, 1.0)) * marchDebug.hit).xxx, 1.0);
    if (debugMode == 57)
    {
        float3 mapped = max(marchDebug.accumulatedSilverLining, 0.0.xxx) /
            (1.0.xxx + max(marchDebug.accumulatedSilverLining, 0.0.xxx));
        return float4(mapped * marchDebug.hit, 1.0);
    }
    if (debugMode == 58)
    {
        float visibility = marchDebug.lightingDiagnosticWeight > 1e-6
            ? marchDebug.shapedSunVisibilitySum /
              marchDebug.lightingDiagnosticWeight : 0.0;
        return float4((saturate(visibility) * marchDebug.hit).xxx, 1.0);
    }
    if (debugMode == 59)
    {
        float visibility = marchDebug.lightingDiagnosticWeight > 1e-6
            ? marchDebug.ambientVisibilitySum /
              marchDebug.lightingDiagnosticWeight : 0.0;
        return float4((saturate(visibility) * marchDebug.hit).xxx, 1.0);
    }
    if (debugMode == 76)
    {
        float3 lookupPosition;
        bool lookupValid = true;
        if (hasGeometry)
        {
            float deviceDepth = sceneDepthTexture.SampleLevel(
                pointClampSampler, uv, 0);
            lookupPosition = ReconstructWorldPosition(uv, deviceDepth);
        }
        else
        {
            float middleHeight = 0.5 * (
                stage12CloudBottomMeters + stage12CloudTopMeters);
            float safeRayY = abs(rayDirection.y) > 1e-5
                ? rayDirection.y : (rayDirection.y < 0.0 ? -1e-5 : 1e-5);
            float planeDistance = (middleHeight - cameraPos.y) / safeRayY;
            lookupValid = abs(rayDirection.y) > 1e-5 && planeDistance >= 0.0;
            lookupPosition = cameraPos + rayDirection * max(planeDistance, 0.0);
        }
        if (!lookupValid)
            return float4(0.0, 0.0, 0.0, 1.0);
        Stage12ShadowSample cascade = SampleStage12DeepShadow(lookupPosition);
        return float4(cascade.nearWeight, 0.0,
                      cascade.farValidity, 1.0);
    }
    if (debugMode == 77)
    {
        if (!hasGeometry)
            return float4(0.0, 0.0, 0.0, 1.0);
        float deviceDepth = sceneDepthTexture.SampleLevel(
            pointClampSampler, uv, 0);
        float3 surfacePosition = ReconstructWorldPosition(uv, deviceDepth);
        float surfaceT = Stage12SurfaceTransmittance(surfacePosition);
        return float4(surfaceT.xxx, 1.0);
    }
    if (debugMode == 81)
    {
        float normalizedLift = marchDebug.localBaseLiftMeters /
            max(maximumBaseLiftMeters, 1.0);
        return float4((saturate(normalizedLift) * marchDebug.hit).xxx, 1.0);
    }
    // 7. 모드 0: 안개가 더한 빛 + 안개를 통과한 배경빛으로 최종 합성한다.
    float3 background = hasGeometry
        ? sceneColorTexture.SampleLevel(pointClampSampler, uv, 0).rgb
        : 0.0.xxx;
    float3 composite = ComposeStage14Atmosphere(
        uv, rayDirection, hasGeometry, background, sceneDistance,
        cloud.scattering, cloud.transmittance,
        cloud.representativeDepth);
    return float4(max(composite, 0.0.xxx), 1.0);
}

// 최종 High 경로의 유일한 픽셀 셰이더 엔트리다.
float4 main(VSOut input) : SV_TARGET
{
    float2 uv = saturate(input.uv);
    if (debugMode == 74)
        return Stage12DebugCacheTexture(uv, true);
    if (debugMode == 75)
        return Stage12DebugCacheTexture(uv, false);
    float deviceDepth = sceneDepthTexture.SampleLevel(pointClampSampler, uv, 0);
    bool hasGeometry = deviceDepth < 0.999999;
    float3 rayDirection = ReconstructWorldRay(uv);
    float3 worldPosition = hasGeometry
        ? ReconstructWorldPosition(uv, deviceDepth)
        : cameraPos + rayDirection * farPlane;
    float sceneDistance = hasGeometry
        ? length(worldPosition - cameraPos) : farPlane;
    CloudMarchDebug marchDebug = (CloudMarchDebug)0;
    CloudResult cloud = (CloudResult)0;
    cloud = RaymarchCloud(
        cameraPos, rayDirection, sceneDistance, marchDebug);
    return RenderCloudOutput(
        input, hasGeometry, rayDirection, cloud, marchDebug, sceneDistance);
}
