// [학습 지도] Scene HDR/depth + Weather/Noise/Shadow/LUT → Full-resolution raymarch → HDR RTV → Tone. ray 거리 m.
// [수정 안내] [직접 조절]의 CPU/UI 원본을 수정한다. 강제 범위는 입력 계약이며 화질 보장이 아니다.
// 별도 권장 구간이 없는 값은 표시된 기본값을 비교 출발점으로 삼는다. b/t/u/s는 버퍼/읽기/쓰기/샘플러 슬롯.
// ============================================================================
//  VolumetricClouds.hlsl - Stage 15 Full-resolution High 구름·대기 HDR 합성
// ----------------------------------------------------------------------------
//  한 프레임의 렌더링 순서
//  1. CPU가 Camera/Cloud/Light/Environment를 b0/b1/b3/b4에 복사한다.
//  2. 앞선 DiagnosticScene 패스가 불투명 Scene Color와 Scene Depth를 만든다.
//  3. 이 풀스크린 PS가 UV → 월드 레이 → 깊이 거리 순으로 복원한다.
//  4. 레이와 PlanarLayer의 교차 구간을 구하고 Scene Depth보다 뒤를 잘라 낸다.
//  5. 월드 XZ Weather RGBA와 타입 선택으로 배치·종류·밀도·두께를 결정해 Base Shape를 만든다.
//  6. Base가 비어 있지 않을 때만 Detail로 깎는다.
//  7. 밀도가 있는 View 표본에서 Deep Cache를 읽고 불가하면 cone Light Ray를 적분한다.
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
    // [파생 값] CPU transpose된 역 view-projection. 화면 UV/device depth [0,1] → 월드 위치(m). 직접 수정하면 깊이와 구름 가림이 어긋난다.
    float4x4 invViewProj; // CPU invViewProj. UV/깊이를 월드 공간(m)으로 되돌린다.
    // [파생 값] 역 투영 행렬. NDC 방향 → view ray; 큰 월드에서 translation 없이 정밀한 레이를 복원.
    float4x4 invProjection;   // NDC를 translation 없는 View Space로 복원한다.
    // [파생 값] 역 view 회전 행렬. view 방향 → 월드 방향, w=0으로 translation 제외.
    float4x4 invViewRotation; // View 방향에 카메라 회전만 적용한다.
    // [파생 값] xyz 카메라 월드 m, ray 시작점. F5~F8/이동에서 생성.
    float3 cameraPos;     // CPU cameraPos. 월드 공간(m), 모든 레이의 원점.
    // [파생 값] 유효 구름 시간 s. 일반 실행은 실제 delta 누적, 테스트는 고정 입력; Weather/Base/Shadow에 동일 값.
    float time;           // CPU time. 초 단위, 단계 2 바람 이동에 사용한다.
    // [파생 값] xy=전체 화면 가로/세로 pixel, 각각 >=1. Full-resolution ray/LUT 계약.
    float2 renderSize;    // CPU renderSize. 현재 백버퍼 크기(pixel), 현재 예약 값.
    // [파생 값] 카메라 near clip 거리 m, 양수. 깊이/투영 계약에서 생성.
    float nearPlane;      // CPU nearPlane. 카메라 근평면 거리(m), 현재 예약 값.
    // [파생 값] 카메라 far clip 거리 m, near보다 큼. 하늘 ray 외부 한계; 구름 최대 거리는 b5도 제한.
    float farPlane;       // CPU farPlane. 하늘 픽셀의 최대 추적 거리(m).
};

// cbCamera의 time 선언 뒤 포함해야 Light Ray가 같은 애니메이션 시간을 사용한다.
#include "CloudDomainParameters.hlsli"
#include "HighCloudQuality.hlsli"
#include "CloudEnvironment.hlsli"
// VCLOUD_LOCAL_AERIAL_HELPER

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
#if defined(VCLOUD_CLARITY_PROBE)
static uint clarityStep=0xffffffff;
static float clarityTarget=-1;
#endif
struct CloudResult
{
#if defined(VCLOUD_CLARITY_PROBE)
    float4 claritySample; // 실제 High의 거리m/불투명도 기여/ds/T앞
#endif
    float3 scattering;       // 안개가 카메라 쪽으로 새로 더한 linear RGB 빛.
    float transmittance;     // 뒤 배경빛의 생존 비율. 1=완전 투명, 0=완전 불투명.
    float representativeDepth; // 불투명도 가중 구름 깊이(m).
#if defined(VCLOUD_TEST_AERIAL_COMPOSITION)
    float3 sampleLut;
    float3 sampleDirect;
    float depthSecondMoment; // km² 가중 합 (half 저장 범위 보호)
    float firstDepthKm;
    float lastDepthKm;
    float4 depthBins; // 0~5 / 5~10 / 10~20 / 20km 이상 불투명도 기여
#endif
};

// 단계 1 교차, 단계 2 noise와 단계 3 높이 적분이 사용한 대표값 진단 자료.
struct CloudMarchDebug
{
#if defined(VCLOUD_TEST_NEAR_FAR)
    float sampleCount;
    float minDs;
    float maxDs;
    float lastDistance;
    float termination; // 0=구간 완료, 1=early exit, 2=반복 한도
#endif
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
    float cloudType;       // 대표 위치의 b10 선택 후 유효 타입(저장 G와 다를 수 있다).
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
    float dualPhaseFactor;    // 직접 산란에 실제로 적용한 최종 [0,2.5] 배율.
    float3 accumulatedDirect; // View Ray 전체의 직접 태양광 누적값.
#if defined(VCLOUD_TEST_RIM_BOUNDARY)
    float3 singleTransport; // 테스트 전용: Tview*(1-Tstep)*albedo*Sun*Tsun*기존 P0.
#endif
    float3 accumulatedSky;    // View Ray 전체의 LUT 기반 하늘 환경광.
    float3 accumulatedGround; // View Ray 전체의 LUT 기반 지면 반사 근사.
    float3 accumulatedMultiple;// View Ray 전체의 다중 산란 근사.
    float3 accumulatedSilverLining; // 외곽 범위 Phase가 추가한 양의 직접광.
    float shapedSunVisibilitySum;   // 조명 가중 평균용 직접광 투과율 합.
    float ambientVisibilitySum;     // 조명 가중 평균용 환경광 가시성 합.
    float lightingDiagnosticWeight; // 두 가시성 진단의 공통 분모.
    float visibleSunTransmittanceSum; // sum(Tview*(1-Tstep)*Tsun), albedo/phase/fill 독립.
    float visibleOpacityWeight;      // sum(Tview*(1-Tstep)), 빈 레이는 0.
    float viewOpticalDepth;   // View Ray 전체의 final density 광학 깊이.
    float4 baseNoiseChannels; // 단계 13-4 Base Texture3D RGBA.
    float4 detailNoiseChannels;// 단계 13-4 Detail Texture3D RGBA.
    // [파생 값: 진단] 대표 위치 Near tau [0,9.21034037].
    float stage12NearOpticalDepth;
    // [파생 값: 진단] 대표 위치 Far tau [0,9.21034037].
    float stage12FarOpticalDepth;
    // [파생 값: 진단] 대표 위치 Near 혼합 [0,1].
    float stage12NearWeight;
    // [파생 값: 진단] 대표 위치 Far fade [0,1].
    float stage12FarValidity;
};

// 화면 UV를 DirectX NDC로 바꾼다.
// 입력/출력은 단위 없는 좌표이며 UV의 아래 방향 Y를 NDC의 위 방향 Y로 뒤집는다.
// [좌표 변환] UV 좌상단 (0,0) → D3D NDC (-1,+1). x는 2u-1, y는 1-2v.
// 이 y 반전을 빼면 화면과 깊이의 위아래가 반대로 연결된다.
float2 UvToNdc(float2 uv)
{
    return float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
}

// 화면 UV가 가리키는 월드 레이 방향을 복원한다.
// 출력은 길이 1인 월드 방향이므로 이후 tStart/tEnd가 meter 거리가 된다.
// homogeneous w가 0에 가까울 때 안전값을 사용해 NaN을 막는다.
// [ray 복원] invProjection으로 view 방향을 얻고 invViewRotation으로 월드 회전.
// 방향은 길이 1이므로 t의 단위가 m가 된다. 큰 camera translation을 방향 계산에서
// 제외해 먼 월드 좌표 두 점의 뺄셈 정밀도 손실을 피한다.
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
#if defined(VCLOUD_TEST_VIEW_STEP_METERS)
    // 05 진단 전용: 캐시를 고정한 채 시선 표본 간격만 비교한다.
    return VCLOUD_TEST_VIEW_STEP_METERS;
#endif
    float distanceBlend = smoothstep(
        kHighDistanceStepStartMeters, kHighDistanceStepEndMeters,
        sampleDistance);
    float normalStep = kHighViewStepMeters * lerp(1.0, kHighFarStepMultiplier, distanceBlend);
#if defined(VCLOUD_TEST_NEAR_STEP_METERS)
    // 시험 전용: 5km까지 촘촘하게, 5~15km에서 기존 거리 step으로 복귀.
    return lerp(VCLOUD_TEST_NEAR_STEP_METERS, normalStep, smoothstep(5000.0,15000.0,sampleDistance));
#else
    return normalStep;
#endif
}

#if defined(VCLOUD_TEST_NEAR_FAR_SPHERE)
// 진단 입력만 교체하고 아래의 실제 View 적분은 그대로 사용한다.
CloudDensitySample NearFarSphereDensity(float3 p, float seconds, bool detail)
{
    CloudDensitySample s = (CloudDensitySample)0;
    float r = length(p-float3(0,3200,-3000));
    float support = r < 600 ? 1.0 : 0.0;
#if VCLOUD_TEST_NEAR_FAR_SPHERE == 2
    support = 1-smoothstep(450.0,600.0,r);
#endif
    s.baseDensity = s.finalDensity = support * 0.0005 / max(extinctionCoefficient,1e-8);
    return s;
}
#define SampleCloudDensity NearFarSphereDensity
#endif

// [적분 전체] Scene depth로 끝을 자른 Planar 구간에서 앞→뒤 순서로 진행한다.
// 출력 scattering=누적 선형 HDR RGB, transmittance=남은 배경 비율,
// representativeDepth=불투명도 기여로 가중 평균한 거리(m). 실제 표면 깊이는 아니다.
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
#if defined(VCLOUD_TEST_NEAR_FAR)
    debugData.minDs = 65504;
    debugData.lastDistance = tStart;
#endif

    // 1. 구간 중점 표본은 진단용이다. 실제 화면 빛은 아래 loop의 모든 유효 표본을 적분한다.
    // LUT 입사광과 phase는 한 ray에서 재사용해 중복 비용을 줄인다.
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
            CloudSunIncident(lightingContext), phase.rimPhaseFactor);
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

    // 2. cursor는 광선을 따라간 거리(m). T=1에서 시작하며 이미 앞에서 가린 만큼
    // 다음 표본의 빛을 줄인다. 최대 512회는 coarse 검사/되감기 반복도 포함한다.
    float cursor = tStart;
    uint consecutiveEmpty = 0u;
    bool coarseSearch = false;
    float extinction = max(extinctionCoefficient, 0.0);
    float opacityDepthMoment = 0.0;
    float opacityWeight = 0.0;
#if defined(VCLOUD_TEST_AERIAL_COMPOSITION)
    CompositionAir compositionAir=EmptyCompositionAir();
#endif
    uint safeMaxSteps = kHighMaximumViewSteps;
#if defined(VCLOUD_TEST_VIEW_STEP_METERS) || defined(VCLOUD_TEST_NEAR_STEP_METERS)
    safeMaxSteps = 4096u;
#endif
    [loop]
    for (uint iteration = 0u;
         iteration < safeMaxSteps && cursor < tEnd - 1e-5;
         ++iteration)
    {
        // 3. 고정 High 기본 100m, 24~50km에서 최대 1.25배. 마지막 구간은 tEnd에서 잘라
        // 건물 뒤의 구름을 적분하지 않는다. 각 구간 중심에서 밀도를 대표 표본으로 읽는다.
        float fullStep = Stage9ViewStep(cursor);
        float candidateStep = coarseSearch
            ? min(fullStep * kHighCoarseStepMultiplier,
                  max(kHighMaximumSearchStepMeters, fullStep))
            : fullStep;
        float marchLength = min(candidateStep, tEnd - cursor);
        float sampleDistance = cursor + 0.5 * marchLength;
#if defined(VCLOUD_TEST_NEAR_FAR)
        debugData.sampleCount += 1;
        debugData.minDs = min(debugData.minDs,marchLength);
        debugData.maxDs = max(debugData.maxDs,marchLength);
        debugData.lastDistance = cursor + marchLength;
#endif
        float3 samplePosition = rayOrigin + rayDirection * sampleDistance;

        // 4. 빈 Base 3개 뒤 큰 간격으로 탐색한다. 구름을 다시 찾으면 한 구간 되감아
        // 작은 step으로 재검사한다. 이 과정이 없으면 구름 앞쪽 얇은 경계를 건너뛴다.
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
        // 5. Beer-Lambert: dTau=rho*sigma_t*ds는 무차원, Tstep=exp(-dTau).
        // 거리 fade는 밀도에 반영된다. rho/소멸계수/길이가 늘면 투과율이 감소한다.
        float stepTransmittance = exp(
            -sampledDensity * extinction * marchLength);
        debugData.viewOpticalDepth += sampledDensity * extinction * marchLength;

        bool requiresLighting = debugMode == 0 || debugMode == 90 ||
            debugMode == 32 || debugMode == 53 || debugMode == 54 ||
            debugMode == 55 || debugMode == 57 || debugMode == 58 ||
            debugMode == 59 || debugMode == 60;
#if defined(VCLOUD_TEST_AERIAL_COMPOSITION)
        requiresLighting=true;
#endif
#if defined(VCLOUD_TEST_AERIAL_SCALE_RUNTIME)
        requiresLighting=true;
#endif
        if (sampledDensity > 0.0 && requiresLighting)
        {
            LightMarchResult light = ComputeLightTransmittance(
                samplePosition, directionToSun);
#if defined(VCLOUD_TEST_RIM_BOUNDARY)
            debugData.singleTransport += ComputeDirectInteractionColor(
                sampledDensity, result.transmittance, marchLength,
                CloudSunIncident(lightingContext)) * light.transmittance * phase.phaseFactor;
            float boundaryWeight = result.transmittance * (1.0 - stepTransmittance);
            debugData.visibleSunTransmittanceSum += boundaryWeight * light.transmittance;
            debugData.visibleOpacityWeight += boundaryWeight;
#endif
            // 실제 보이는 표본의 차폐를 진단한다. Composite의 적분/기본값은 바꾸지 않는다.
            if (debugMode == 60)
            {
                float visibleWeight = result.transmittance * (1.0 - stepTransmittance);
                debugData.visibleSunTransmittanceSum += visibleWeight * light.transmittance;
                debugData.visibleOpacityWeight += visibleWeight;
            }
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
#if defined(VCLOUD_TEST_AERIAL_COMPOSITION)
            float3 contribution=lighting.direct+lighting.skyAmbient+lighting.groundBounce+lighting.multipleScattering;
            float w=result.transmittance*(1-stepTransmittance);
            result.sampleLut+=CompositionContribution(contribution,w,
                SampleAtmosphereAerialTransmittance(compositionUv,sampleDistance),
                SampleAtmosphereAerialRadiance(compositionUv,sampleDistance));
            AdvanceCompositionAir(compositionAir,rayDirection,sampleDistance);
            result.sampleDirect+=CompositionContribution(contribution,w,compositionAir.T,compositionAir.L);
#endif
        }
        // 6. 이번 불투명도 기여=T앞*(1-Tstep). 거리 moment에 이 가중치를 사용해
        // 보이지 않는 깊은 표본이 대표 깊이를 과도하게 뒤로 밀지 않게 한다.
        float sampleOpacityContribution = result.transmittance *
            (1.0 - stepTransmittance);
#if defined(VCLOUD_CLARITY_PROBE)
        if (iteration==clarityStep || (clarityTarget>=0 && result.claritySample.x==0 &&
            opacityWeight+sampleOpacityContribution>=clarityTarget && sampleOpacityContribution>0))
            result.claritySample=float4(sampleDistance,sampleOpacityContribution,marchLength,result.transmittance);
#endif
        opacityDepthMoment += sampleDistance * sampleOpacityContribution;
        opacityWeight += sampleOpacityContribution;
#if defined(VCLOUD_TEST_AERIAL_COMPOSITION)
        float distanceKm=sampleDistance*.001;
        result.depthSecondMoment+=distanceKm*distanceKm*sampleOpacityContribution;
        if(sampleOpacityContribution>0)
        {
            if(result.firstDepthKm==0)result.firstDepthKm=distanceKm;
            result.lastDepthKm=distanceKm;
            // 통계는 실제 T 갱신 전후 차이를 누적해 작은 1-exp 상쇄 오차를 피한다.
            precise float binNextT=result.transmittance*stepTransmittance;
            precise float binOpacity=result.transmittance-binNextT;
            result.depthBins+=binOpacity*float4(distanceKm<5,
                distanceKm>=5 && distanceKm<10,distanceKm>=10 && distanceKm<20,distanceKm>=20);
        }
#endif
        result.transmittance *= stepTransmittance;
        cursor += marchLength;

        consecutiveEmpty = densitySample.baseDensity <= kHighBaseDensityEpsilon
            ? consecutiveEmpty + 1u : 0u;
#if !defined(VCLOUD_TEST_BOUNDARY_NO_SKIP)
        if (consecutiveEmpty >= kHighEmptySamplesBeforeCoarse)
            coarseSearch = true;
#endif
        // 7. 남은 배경빛이 1% 이하이면 종료하는 고정 High 계약이다.
        // 이 값을 바꾸면 내부/원경 품질과 성능 기준을 함께 재검증해야 한다.
#if !defined(VCLOUD_TEST_BOUNDARY_NO_EXIT)
        if (result.transmittance <= kHighTransmittanceThreshold)
        {
            break;
        }
#endif
    }

    result.transmittance = saturate(result.transmittance);
#if defined(VCLOUD_TEST_NEAR_FAR)
    debugData.termination = cursor >= tEnd-1e-5 ? 0 :
        (result.transmittance <= kHighTransmittanceThreshold ? 1 : 2);
#endif
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
#if defined(VCLOUD_TEST_RIM_BOUNDARY)
    // 선형 HDR 수치 덤프. 색 영상이 아닌 채널별 관찰량이다. 일반 경로에는 컴파일되지 않는다.
    static const float3 boundaryLuma = float3(.2126, .7152, .0722);
#if VCLOUD_TEST_RIM_BOUNDARY == 1
    return float4(dot(marchDebug.accumulatedDirect, boundaryLuma),
        dot(marchDebug.accumulatedSilverLining, boundaryLuma),
        dot(marchDebug.singleTransport, boundaryLuma), cloud.transmittance);
#elif VCLOUD_TEST_RIM_BOUNDARY == 2
    return float4(dot(marchDebug.accumulatedSky, boundaryLuma),
        dot(marchDebug.accumulatedGround, boundaryLuma),
        dot(marchDebug.accumulatedMultiple, boundaryLuma), cloud.transmittance);
#elif VCLOUD_TEST_RIM_BOUNDARY == 4
    return float4(marchDebug.singleTransport, 1);
#elif VCLOUD_TEST_RIM_BOUNDARY == 5
    return float4(marchDebug.accumulatedDirect, 1);
#elif VCLOUD_TEST_RIM_BOUNDARY == 6
    return float4(marchDebug.accumulatedSilverLining, 1);
#elif VCLOUD_TEST_RIM_BOUNDARY == 7
    return float4(marchDebug.accumulatedSky, cloud.transmittance);
#else
    return float4(marchDebug.visibleSunTransmittanceSum /
        max(marchDebug.visibleOpacityWeight, 1e-10), marchDebug.viewOpticalDepth,
        marchDebug.dualPhaseFactor, cloud.transmittance);
#endif
#endif
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
    if (debugMode == 60)
    {
        float visibility = marchDebug.visibleOpacityWeight > 1e-6
            ? marchDebug.visibleSunTransmittanceSum / marchDebug.visibleOpacityWeight : 0.0;
        return float4(saturate(visibility).xxx, 1.0);
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
    // 불투명도 가중 대표거리의 승인된 2배 Air 조회. 실제 깊이 진단값은 유지한다.
    if (debugMode == 91 || debugMode == 92)
    {
        bool visibleCloud = (1.0 - cloud.transmittance) > 1.0e-6;
        float3 air = debugMode == 91
            ? SampleAtmosphereAerialTransmittance(uv, CloudAerialLookupDepth(cloud.representativeDepth))
            : SampleAtmosphereAerialRadiance(uv, CloudAerialLookupDepth(cloud.representativeDepth));
        return float4(visibleCloud ? air : 0.0.xxx, visibleCloud ? 1.0 : 0.0);
    }
    // 7. 모드 0: 안개가 더한 빛 + 안개를 통과한 배경빛으로 최종 합성한다.
    float3 background = hasGeometry
        ? sceneColorTexture.SampleLevel(pointClampSampler, uv, 0).rgb
        : 0.0.xxx;
    float3 composite = ComposeStage14Atmosphere(
        uv, rayDirection, hasGeometry, background, sceneDistance,
        cloud.scattering, cloud.transmittance,
        cloud.representativeDepth, debugMode == 90);
    return float4(max(composite, 0.0.xxx), 1.0);
}

// VCLOUD_LOCAL_CLARITY_HELPER
// 최종 High 경로의 유일한 픽셀 셰이더 엔트리다.
// [패스 지도] t0 Scene HDR/t1 depth + Weather/Noise/Shadow/LUT + b0~b10 → HDR target.
// 1. 캐시 진단이면 해당 texture 표시. 2. Scene depth [0,1]로 물체 유무 판정.
// 3. 월드 ray와 표면 거리(m) 복원. 4. Raymarch. 5. 대기/배경 합성.
// 출력은 linear HDR이며 sRGB 변환은 다음 Tone Map에서 한 번만 수행한다.
// VCLOUD_LOCAL_NEAR_FAR_HELPER
float4 main(VSOut input) : SV_TARGET
{
#if defined(VCLOUD_CLARITY_PROBE)
    return NearClarityProbe(input);
#endif
#if defined(VCLOUD_TEST_AERIAL_COMPOSITION)
    if(debugMode==120) return input.position.y<1 && input.position.x<5 ? CompositionContract((uint)input.position.x) : 0;
    compositionUv=saturate(input.uv);
    if(input.position.x<VCLOUD_TEST_ROI_X || input.position.x>=VCLOUD_TEST_ROI_X+VCLOUD_TEST_ROI_W ||
       input.position.y<VCLOUD_TEST_ROI_Y || input.position.y>=VCLOUD_TEST_ROI_Y+VCLOUD_TEST_ROI_H) return 0;
#endif
#if defined(VCLOUD_TEST_NEAR_FAR_PROBE)
    return NearFarProbe(input);
#endif
#if defined(VCLOUD_TEST_NEAR_FAR_ROI)
    if(input.position.x<VCLOUD_TEST_ROI_X || input.position.x>=VCLOUD_TEST_ROI_X+VCLOUD_TEST_ROI_W ||
       input.position.y<VCLOUD_TEST_ROI_Y || input.position.y>=VCLOUD_TEST_ROI_Y+VCLOUD_TEST_ROI_H) return 0;
#endif
#if defined(VCLOUD_TEST_BASE_WEATHER_SLICE)
    // 진단 전용 직교 단면. 빈 공간도 원본 noise를 조회하며 HDR 채널을 CPU에서 분리한다.
    float2 sliceUv = saturate(input.uv);
    float3 p = float3((sliceUv.x-0.5)*24000.0,
        cloudBoundsMin.y + VCLOUD_TEST_SLICE_HEIGHT * (cloudBoundsMax.y-cloudBoundsMin.y),
        (sliceUv.y-0.5)*24000.0);
#if VCLOUD_TEST_SLICE_VERTICAL
    p = float3((sliceUv.x-0.5)*24000.0,
        lerp(cloudBoundsMax.y,cloudBoundsMin.y,sliceUv.y),0.0);
#endif
    NoiseFieldSample n = SampleBaseShapeNoise(p,time);
    WeatherSample w = SampleWeatherMap(p,time);
    CloudDensitySample b = ComposeBaseCloudDensity(p,n,w);
#if VCLOUD_TEST_SLICE_PACK == 0
    return float4(n.value,RemapCoverage(n.value,coverage),w.coverage,
        smoothstep(0.02,0.20,w.coverage));
#elif VCLOUD_TEST_SLICE_PACK == 1
    return float4(b.weatherThresholdDensity,b.baseDensity,
        ShapeCloudDensity(b.baseDensity),SampleCloudDensity(p,time,true).finalDensity);
#else
    WeatherSample neutral = w;
    neutral.coverage=1.0; neutral.densityModifier=1.0; neutral.localThicknessPotential=0.5;
    // 일정 Base 0.65는 평균 대체가 아닌 명시적 제거 대조군이다.
    NoiseFieldSample constantNoise=n; constantNoise.value=0.65;
    return float4(ComposeBaseCloudDensity(p,n,neutral).baseDensity,
        ComposeBaseCloudDensity(p,constantNoise,w).baseDensity,
        EvaluateCommonVerticalProfile(b.localHeightFraction),w.densityModifier/1.5);
#endif
#endif
#if defined(VCLOUD_TEST_SOLAR_REFERENCE_STEP)
#if defined(VCLOUD_TEST_SOLAR_REFERENCE_FULL)
    solarReferencePixel = true;
#else
    solarReferencePixel = input.position.x >= 550 && input.position.x < 750 &&
        input.position.y >= 640 && input.position.y < 710;
#endif
#endif
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
    if(debugMode>=93 && debugMode<=95)
    {
        // 구름 점유 여부는 미소 밀도 문턱에 의존한다. 거리 fade 전 값을 측정하는 진단 전용 적분.
        float start=0.0,end=0.0,occupied=0.0,mass=0.0,first=0.0;
        if(IntersectCloudVolume(cameraPos,rayDirection,sceneDistance,start,end))
        {
            float t=start;
            [loop] for(uint i=0;i<4096 && t<end-1e-5;++i)
            {
                float ds=min(100.0,end-t);
                float rho=SampleCloudDensity(cameraPos+rayDirection*(t+0.5*ds),time,true).finalDensity;
                if(rho>0.001){if(occupied==0.0)first=t+0.5*ds;occupied+=ds;mass+=rho*ds;}
                t+=ds;
            }
        }
        float value=debugMode==93?occupied:(debugMode==94?mass/max(occupied,1e-6):first);
        return float4(value.xxx,occupied>0.0?1.0:0.0);
    }
    CloudMarchDebug marchDebug = (CloudMarchDebug)0;
    CloudResult cloud = (CloudResult)0;
    cloud = RaymarchCloud(
        cameraPos, rayDirection, sceneDistance, marchDebug);
#if defined(VCLOUD_TEST_AERIAL_SCALE_RUNTIME)
    // 하나의 같은 DXBC/같은 View 경로에서 공기 거리만 바꾼다. Alpha에는 원시 Cloud T.
    testAerialScale=debugMode==122?1.5:(debugMode==123?2.0:1.0);
    float3 scaleSurface=hasGeometry?sceneColorTexture.SampleLevel(pointClampSampler,uv,0).rgb:0;
    return float4(ComposeStage14Atmosphere(uv,rayDirection,hasGeometry,scaleSurface,sceneDistance,
        cloud.scattering,cloud.transmittance,cloud.representativeDepth),cloud.transmittance);
#endif
#if defined(VCLOUD_TEST_AERIAL_COMPOSITION)
    float3 surface=hasGeometry?sceneColorTexture.SampleLevel(pointClampSampler,uv,0).rgb:0;
    float3 bg=ComposeStage14Atmosphere(uv,rayDirection,hasGeometry,surface,sceneDistance,0,1,0,true);
    CompositionAir representativeAir=EmptyCompositionAir();
    AdvanceCompositionAir(representativeAir,rayDirection,cloud.representativeDepth);
    float airLookupDepth=cloud.representativeDepth;
#if defined(VCLOUD_TEST_AERIAL_SCALE)
    airLookupDepth*=VCLOUD_TEST_AERIAL_SCALE;
#endif
    float3 airT=SampleAtmosphereAerialTransmittance(uv,airLookupDepth);
    float3 airL=SampleAtmosphereAerialRadiance(uv,airLookupDepth);
    float tc=cloud.transmittance;
    float3 color=ComposeStage14Atmosphere(uv,rayDirection,hasGeometry,surface,sceneDistance,
        cloud.scattering,tc,cloud.representativeDepth);
    if(debugMode==101)color=cloud.sampleLut+tc*bg;
    if(debugMode==102)color=CompositionContribution(cloud.scattering,1-tc,representativeAir.T,representativeAir.L)+tc*bg;
    if(debugMode==103)color=cloud.sampleDirect+tc*bg;
    if(debugMode==104)return float4(cloud.representativeDepth*.001,
        sqrt(max(cloud.depthSecondMoment/max(1-tc,1e-6)-pow(cloud.representativeDepth*.001,2),0)),
        cloud.firstDepthKm,cloud.lastDepthKm);
    if(debugMode==105)return float4(cloud.scattering,tc);
    if(debugMode==106)return float4(bg,hasGeometry?1:0);
    if(debugMode==107)return float4(airT,tc);
    if(debugMode==108)return float4(airL,tc);
    if(debugMode==109)return float4(representativeAir.T,tc);
    if(debugMode==110)return float4(representativeAir.L,tc);
    if(debugMode==111)return cloud.depthBins;
    if(debugMode==112)color=cloud.scattering+tc*bg;
    return float4(color,tc);
#endif
#if defined(VCLOUD_TEST_NEAR_FAR_RAW)
    if(debugMode==32) return float4(marchDebug.accumulatedDirect,cloud.transmittance);
    if(debugMode==53) return float4(marchDebug.accumulatedSky,cloud.transmittance);
    if(debugMode==54) return float4(marchDebug.accumulatedGround,cloud.transmittance);
    if(debugMode==55) return float4(marchDebug.accumulatedMultiple,cloud.transmittance);
    if(debugMode==57) return float4(marchDebug.accumulatedSilverLining,cloud.transmittance);
    if(debugMode==59) return float4(cloud.scattering,cloud.transmittance);
    if(debugMode==56) return float4(cloud.representativeDepth.xxx,1-cloud.transmittance);
    if(debugMode==91) return float4(SampleAtmosphereAerialTransmittance(uv,CloudAerialLookupDepth(cloud.representativeDepth)),1-cloud.transmittance);
    if(debugMode==92) return float4(SampleAtmosphereAerialRadiance(uv,CloudAerialLookupDepth(cloud.representativeDepth)),1-cloud.transmittance);
#endif
#if defined(VCLOUD_TEST_NEAR_FAR_PACK)
#if VCLOUD_TEST_NEAR_FAR_PACK == 0
    return float4(cloud.transmittance,marchDebug.viewOpticalDepth,cloud.representativeDepth,1);
#elif VCLOUD_TEST_NEAR_FAR_PACK == 1
    return float4(marchDebug.sampleCount,marchDebug.minDs,marchDebug.maxDs,marchDebug.termination);
#else
    return float4(marchDebug.lastDistance,marchDebug.exitDistance,marchDebug.termination,1);
#endif
#endif
    return RenderCloudOutput(
        input, hasGeometry, rayDirection, cloud, marchDebug, sceneDistance);
}
