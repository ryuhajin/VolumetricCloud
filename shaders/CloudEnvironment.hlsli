// [학습 지도] b4/b9 + density/Light/Phase → 표본별 Direct/Sky/Ground/Multiple HDR RGB → View 누적. 월드 m, LUT 조회 km.
// [수정 안내] [직접 조절]의 CPU/UI 원본을 수정한다. 강제 범위는 입력 계약이며 화질 보장이 아니다.
// 별도 권장 구간이 없는 값은 표시된 기본값을 비교 출발점으로 삼는다. b/t/u/s는 버퍼/읽기/쓰기/샘플러 슬롯.
// ============================================================================
//  CloudEnvironment.hlsli - 물리 LUT 환경광과 저비용 다중 산란
// ----------------------------------------------------------------------------
//  1. 최종 밀도와 높이로 하늘/지면 가중치와 Ambient Occlusion을 만든다.
//  2. 단계 6 Light Ray의 광학 깊이를 재사용해 추가 레이 없이 산란 octave를 만든다.
//  3. Direct/Sky/Ground/Multiple을 같은 View 구간 적분으로 카메라에 누적한다.
//  환경광에는 방향 Phase를 곱하지 않으며, 다중 산란만 약해진 Phase를 사용한다.
// ============================================================================
#ifndef VCLOUD_CLOUD_ENVIRONMENT_HLSLI
#define VCLOUD_CLOUD_ENVIRONMENT_HLSLI

#include "EnvironmentParameters.hlsli"
#include "CloudLighting.hlsli"

struct EnvironmentLightingSample
{
    // [파생 값] 이번 구간의 직접 태양 산란 linear HDR RGB, >=0. 이미 Tview 가중.
    float3 direct;
    // [파생 값] direct 중 phase>1이 추가한 양의 RGB 부분(진단). 최종 합에 다시 더하지 않는다.
    float3 silverLiningContribution;
    // [파생 값] LUT 하늘광*높이/AO*공통 적분 가중치, linear HDR RGB.
    float3 skyAmbient;
    // [파생 값] 지면 반사 입사광*하부 높이/AO*공통 가중치, linear HDR RGB.
    float3 groundBounce;
    // [파생 값] tau 재사용 근사 다중 산란의 이번 구간 RGB.
    float3 multipleScattering;
    // [파생 값] 직접광의 T^shadowExponent, [0,1].
    float shapedSunVisibility;
    // [파생 값] local AO와 태양 차폐 결합 가시성 [0,1].
    float ambientVisibility;
    // [파생 값] Tview*albedo*(1-Tstep), [0,1]. 가시성 진단의 가중 평균 분모.
    float diagnosticWeight;
};

// 대기 LUT는 구름 표본마다 다시 읽지 않는다. 한 View Ray에서 구름층 대표 고도의
// 입사광을 한 번 읽으면 수십~수백 View step의 중복 texture fetch를 제거할 수 있다.
// 구름 내부 높이 변화는 아래의 sky/ground 가중치가 담당한다.
struct CloudLightingContext
{
    // [파생 값] 대표 고도에서 대기를 통과한 태양 linear RGB, 표본 밀도/구름 T 적용 전.
    float3 sunIncident;
    // [파생 값] 대표 고도의 SkyIrradiance*fill, linear RGB.
    float3 skyIncident;
    // [파생 값] 지면 irradiance*albedo/pi*bounce*fill, linear RGB.
    float3 groundIncident;
};

// [입사광 준비] b5 대표 고도 m를 km로 바꿔 대기 태양/하늘 LUT를 한 ray당 읽는다.
// 지면은 albedo*(직접 태양*cos+하늘 irradiance)/pi로 반사하는 Lambert 근사다.
// context는 아직 View 감쇠/밀도 기여를 곱하지 않은 선형 HDR 입사광이다.
CloudLightingContext BuildCloudLightingContext()
{
    CloudLightingContext context = (CloudLightingContext)0;
    float representativeAltitudeKm = max(
        cloudLightingReferenceAltitudeMeters, 0.0) * 0.001;
    float3 atmosphereSun = AtmosphereSunDirection();
    float3 representativeSun = SampleAtmosphereSunRadiance(
        representativeAltitudeKm, atmosphereSun);
    float atmosphereThicknessKm = max(
        AtmosphereTopRadiusKm() - AtmosphereBottomRadiusKm(), 1.0e-6);
    float sunUv = atmosphereSun.y * 0.5 + 0.5;
    float3 representativeSky = atmosphereSkyIrradianceLut.SampleLevel(
        atmosphereLinearClampSampler,
        float2(sunUv, saturate(representativeAltitudeKm /
            atmosphereThicknessKm)), 0).rgb;
    float3 groundSky = atmosphereSkyIrradianceLut.SampleLevel(
        atmosphereLinearClampSampler, float2(sunUv, 0.0), 0).rgb;
    float3 groundSun = SampleAtmosphereSunRadiance(0.0, atmosphereSun) *
        saturate(atmosphereSun.y);
    context.sunIncident = representativeSun;
    context.skyIncident = representativeSky *
        max(physicalSkyFillScale, 0.0);
    context.groundIncident = max(groundAlbedoAndDebugExposure.xyz,
        0.0.xxx) * (groundSun + groundSky) / kAtmospherePi *
        max(sunTintAndGroundBounce.w, 0.0) *
        max(physicalGroundFillScale, 0.0);
    return context;
}

float3 CloudSunIncident(CloudLightingContext context)
{
    return max(context.sunIncident, 0.0.xxx);
}

float ComputeInteractionFraction(float density, float viewStepLength)
{
    float safeDensity = max(density, 0.0);
    float safeLength = max(viewStepLength, 0.0);
    float safeExtinction = max(extinctionCoefficient, 0.0);
    float stepTransmittance = exp(-safeDensity * safeExtinction * safeLength);
    return saturate(1.0 - stepTransmittance);
}

#if defined(VCLOUD_TEST_POWDER_STRENGTH)
// Powder 원인 분리 실험: 낮은 밀도의 순광 표본만 상대적으로 억제한다.
// cosTheta +1은 역광이므로 효과0. ds를 곱하지 않아 step 길이와 분리한다.
// 곡선4는 이번 비교의 고정 후보이며 프로젝트의 최종 승인 상수가 아니다.
float EvaluatePowderWeight(float density, float cosTheta, float strength)
{
    float densityResponse = saturate(2.0 * (1.0 - exp(-4.0 * max(density, 0.0))));
    float facingWeight = 1.0 - smoothstep(-0.5, 0.5, clamp(cosTheta, -1.0, 1.0));
    return lerp(1.0, densityResponse, saturate(strength) * facingWeight);
}
#endif

// [다중 산란 근사] 1. 기존 Light tau를 재사용하며 새 ray를 만들지 않는다.
// 2. octave마다 tau 배율/방향성/에너지를 줄여 더 깊이 퍼진 빛을 근사한다.
// 3. interior blend는 1-Tsun으로 내부에 에너지를 한정한다.
// 완전한 물리 다중 산란 해가 아니다. attenuation을 올리면 과노출될 수 있다.
float ComputeMultipleScatteringFactor(float lightOpticalDepth,
                                      float lightTransmittance,
                                      float phaseFactor)
{
    bool enabled = multipleScatteringEnabled >= 0.5 &&
                   multipleScatteringOctaves > 0u;
    uint octaveCount = enabled ? min(multipleScatteringOctaves, 4u) : 0u;
    float energy = saturate(multipleScatteringAttenuation);
    float extinctionScale = saturate(multipleScatteringExtinctionFactor);
    float phaseScale = saturate(multipleScatteringPhaseFactor);
    float safeDepth = max(lightOpticalDepth, 0.0);
    float safePhase = clamp(phaseFactor, 0.0, kMaxPhaseFactor);
    float result = 0.0;
    [unroll]
    for (uint octave = 0u; octave < 4u; ++octave)
    {
        if (octave < octaveCount)
        {
            float octaveLight = exp(-safeDepth * extinctionScale);
            float octavePhase = lerp(1.0, safePhase, phaseScale);
            result += energy * octaveLight * max(octavePhase, 0.0);
            energy *= saturate(multipleScatteringAttenuation);
            extinctionScale *= saturate(multipleScatteringExtinctionFactor);
            phaseScale *= saturate(multipleScatteringPhaseFactor);
        }
    }
    float interiorWeight = lerp(
        1.0, 1.0 - saturate(lightTransmittance),
        saturate(multipleScatteringInteriorBlend));
    return max(result * interiorWeight, 0.0);
}

// [표본 조명 순서] 1. local height로 sky/ground 비율을 만든다.
// 2. exp(-density*AO)와 Tsun^ambientExponent로 fill 가시성을 만든다.
// 3. Tview*albedo*(1-Tstep)를 공통 적분 가중치로 사용.
// 4. Direct/Sky/Ground/Multiple RGB를 별도 반환해 Raymarch가 더한다.
// SilverLining은 Direct의 일부를 진단한 값이므로 최종 합에 다시 더하면 이중 계산이다.
EnvironmentLightingSample EvaluateEnvironmentLighting(
    CloudDensitySample densitySample, LightMarchResult lightResult,
    PhaseSample phase, float viewTransmittance, float viewStepLength,
    CloudLightingContext context)
{
    EnvironmentLightingSample result = (EnvironmentLightingSample)0;
    float density = max(densitySample.finalDensity, 0.0);
    // 컬럼별 local bottom/top을 기준으로 하늘·지면 가중치를 계산한다.
    float height = saturate(densitySample.localHeightFraction);
    float localVisibility = exp(
        -density * max(ambientOcclusionStrength, 0.0));
    float safeAmbientExponent = clamp(ambientShadowExponent, 0.1, 8.0);
    if (!(safeAmbientExponent >= 0.1 && safeAmbientExponent <= 8.0))
        safeAmbientExponent = 1.0;
    float directionalVisibility = pow(
        saturate(lightResult.transmittance), safeAmbientExponent);
    float visibility = localVisibility * lerp(
        1.0, directionalVisibility, saturate(ambientShadowCoupling));
    float skyWeight = lerp(1.0, height, saturate(ambientHeightInfluence));
    float groundWeight = 1.0 - height;
    float interactionFraction = ComputeInteractionFraction(
        density, viewStepLength);
    float common = saturate(viewTransmittance) *
                   saturate(singleScatteringAlbedo) * interactionFraction;

    // Off 프리셋에서 단계 7과 같은 함수·연산 순서를 사용해 직접광을 보존한다.
    DirectLightingResponse directResponse = EvaluateDirectLightingResponse(
        lightResult.transmittance, phase.phaseFactor, phase.rimPhaseFactor);
#if defined(VCLOUD_TEST_RIM_PATH)
    // 테스트 전용: 태양 노출과 양쪽 광로 두께의 차이를 분리한다.
    // jointT는 같은 표본으로 이어지는 꺾인 광로이며 전체 View chord는 아니다.
    float testWeight = 1.0;
#if VCLOUD_TEST_RIM_PATH == 2
    float midpointViewT = saturate(viewTransmittance) *
        exp(-0.5 * max(density, 0.0) * max(extinctionCoefficient, 0.0) * max(viewStepLength, 0.0));
    float jointT = midpointViewT * saturate(lightResult.transmittance);
    testWeight = lerp(1.0, pow(jointT, edgeOpticalDepthScale * rimDepthScale), saturate(edgeInfluence));
#endif
    directResponse.rimPhase = clamp(rimIntensity, 0.0, 4.0) *
        max(phase.rimPhaseFactor - 1.0, 0.0) * testWeight;
    directResponse.scopedPhase = directResponse.basePhase + directResponse.rimPhase;
#endif
    float3 directInteraction = ComputeDirectInteractionColor(
        density, viewTransmittance, viewStepLength,
        CloudSunIncident(context));
    result.direct = directInteraction * directResponse.shapedTransmittance *
                    max(directResponse.scopedPhase, 0.0);
#if defined(VCLOUD_TEST_POWDER_STRENGTH)
    float powderWeight = EvaluatePowderWeight(density, phase.cosTheta, VCLOUD_TEST_POWDER_STRENGTH);
    result.direct = directInteraction * directResponse.shapedTransmittance *
        max(directResponse.basePhase * powderWeight + directResponse.rimPhase, 0.0);
#endif
#if defined(VCLOUD_TEST_BASE_DIRECT_SCALE)
    // 테스트 전용: 태양 세기가 아니라 기본 직접광 합산 비중만 조절한다.
    // 림/Multiple 및 태양·시선 투과율은 그대로다.
    result.direct = directInteraction * directResponse.shapedTransmittance *
        max(directResponse.basePhase * VCLOUD_TEST_BASE_DIRECT_SCALE + directResponse.rimPhase, 0.0);
#endif
    result.silverLiningContribution = directInteraction *
        directResponse.shapedTransmittance *
        directResponse.rimPhase;
    // SkyIrradiance는 상반구의 입사광이다. 태양 한 방향이 가려졌다는 이유로
    // 하늘 전체를 차폐하지 않는다. 높이와 국소 AO는 기존대로 유지한다.
    float skyVisibility = localVisibility;
#if defined(VCLOUD_TEST_LEGACY_SKY_OCCLUSION)
    skyVisibility = visibility;
#endif
    result.skyAmbient = common * max(context.skyIncident, 0.0.xxx) *
                        skyWeight * skyVisibility;
    result.groundBounce = common * context.groundIncident * groundWeight *
                          visibility;
    float multipleFactor = ComputeMultipleScatteringFactor(
        lightResult.opticalDepth, lightResult.transmittance,
        phase.phaseFactor);
    float3 multipleIncident = CloudSunIncident(context);
    result.multipleScattering = common * multipleIncident * multipleFactor;
    result.shapedSunVisibility = directResponse.shapedTransmittance;
    result.ambientVisibility = saturate(visibility);
    result.diagnosticWeight = common;
    return result;
}

#endif
