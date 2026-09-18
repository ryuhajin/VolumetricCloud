// [학습 지도] b9(+Aerial b0)/선행 LUT → compute u0/u1 → 6개 RGBA16F LUT → Scene/Cloud/Tone. 대기 좌표 km.
// [수정 안내] [직접 조절]의 CPU/UI 원본을 수정한다. 강제 범위는 입력 계약이며 화질 보장이 아니다.
// 별도 권장 구간이 없는 값은 표시된 기본값을 비교 출발점으로 삼는다. b/t/u/s는 버퍼/읽기/쓰기/샘플러 슬롯.
// ============================================================================
//  Stage14AtmosphereLut.hlsl - 단계 14 여섯 RGBA16F LUT compute 생성
// ============================================================================
// [패스 지도] b9(+Aerial b0)와 선행 t8/t9 → u0/u1 RGBA16F → t8~t13.
// Transmittance → Multi → SkyView/SkyIrradiance/Aerial 순으로 의존한다.
// 2D는 8x8, 3D는 4x4x4; Aerial CS 하나가 radiance와 transmittance 두 LUT를 쓴다.
// 좌표/거리/소멸 단위는 행성 중심 km/1km. dirty일 때만 다시 만든다.
#include "Stage14Atmosphere.hlsli"

RWTexture2D<float4> output2D : register(u0);
RWTexture3D<float4> output3DRadiance : register(u0);
RWTexture3D<float4> output3DTransmittance : register(u1);

struct AtmosphereIntegration
{
    // [파생 값] ray를 따라 카메라에 들어오는 누적 linear HDR RGB, >=0.
    float3 radiance;
    // [파생 값] ray 끝의 빛이 살아남는 RGB 비율 [0,1]. 색별 흡수가 달라 성분별로 계산.
    float3 transmittance;
};

// [공기 적분] 1. 대기/지면 경계와 maximumDistance 중 짧은 거리 선택(km).
// 2. stepCount 구간의 중점을 표본화. 3. 태양 T와 분자/Mie phase로 source 생성.
// 4. 누적 T*구간 source 적분을 radiance에 더한 뒤 T를 갱신.
// 5. includeGround일 때만 도달한 지면 반사광 추가. 모든 출력은 선형 HDR/RGB T.
AtmosphereIntegration IntegrateAtmosphere(
    float3 position, float3 direction, float3 sunDirection,
    uint stepCount, bool includeGround, bool useMultipleScattering,
    float maximumDistance)
{
    AtmosphereIntegration result;
    result.radiance = 0.0.xxx;
    result.transmittance = 1.0.xxx;
    bool hitsGround = false;
    float boundaryDistance = AtmosphereDistanceToBoundary(
        position, direction, hitsGround);
    float distance = min(boundaryDistance, maximumDistance);
    uint safeSteps = max(stepCount, 1u);
    float stepLength = distance / (float)safeSteps;
    float phaseCosine = dot(direction, sunDirection);
    float rayleighPhase = AtmosphereRayleighPhase(phaseCosine);
    float miePhase = AtmosphereMiePhase(phaseCosine);
    [loop]
    for (uint index = 0u; index < safeSteps; ++index)
    {
        float sampleDistance = ((float)index + 0.5) * stepLength;
        float3 samplePosition = position + direction * sampleDistance;
        AtmosphereMedium medium = SampleAtmosphereMedium(samplePosition);
        float3 segmentT = exp(-medium.extinction * stepLength);
        float3 transmittanceToSun = SampleAtmosphereTransmittanceToSun(
            samplePosition, sunDirection);
        float3 directSource = max(solarIrradianceAndMultiplier.xyz, 0.0.xxx) *
            transmittanceToSun *
            (medium.rayleighScattering * rayleighPhase +
             medium.mieScattering * miePhase);
        float3 multipleSource = useMultipleScattering
            ? SampleAtmosphereMultipleScattering(samplePosition, sunDirection) *
              medium.scattering : 0.0.xxx;
        result.radiance += result.transmittance * AtmosphereIntegrateSource(
            directSource + multipleSource, medium.extinction, segmentT);
        result.transmittance *= segmentT;
    }
    if (includeGround && hitsGround && boundaryDistance <= maximumDistance)
    {
        float3 groundPosition = position + direction * boundaryDistance;
        float3 normal = normalize(groundPosition);
        float nDotL = saturate(dot(normal, sunDirection));
        float3 groundSun = max(solarIrradianceAndMultiplier.xyz, 0.0.xxx) *
            SampleAtmosphereTransmittanceToSun(groundPosition, sunDirection);
        result.radiance += result.transmittance * groundSun * nDotL *
            max(groundAlbedoAndDebugExposure.xyz, 0.0.xxx) / kAtmospherePi;
    }
    return result;
}

[numthreads(8, 8, 1)]
// [LUT 1] UV를 고도/천정각으로 복원 → 40개 중점의 extinction*ds 누적 → exp(-tau).
// 256x64 RGB 투과율 [0,1]; A=1. 후속 모든 대기 조명의 태양 감쇠 기반이다.
void CSTransmittance(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint)transmittanceMultiSize.x ||
        id.y >= (uint)transmittanceMultiSize.y)
        return;
    float2 uv = (float2(id.xy) + 0.5) / transmittanceMultiSize.xy;
    float viewHeight;
    float viewCosine;
    AtmosphereUvToTransmittanceParams(uv, viewHeight, viewCosine);
    float3 position = float3(0.0, viewHeight, 0.0);
    float3 direction = float3(sqrt(saturate(1.0 - viewCosine * viewCosine)),
                              viewCosine, 0.0);
    bool hitsGround = false;
    float distance = AtmosphereDistanceToBoundary(position, direction, hitsGround);
    float3 opticalDepth = 0.0.xxx;
    const uint stepCount = 40u;
    float stepLength = distance / (float)stepCount;
    [loop]
    for (uint index = 0u; index < stepCount; ++index)
    {
        float sampleDistance = ((float)index + 0.5) * stepLength;
        opticalDepth += SampleAtmosphereMedium(
            position + direction * sampleDistance).extinction * stepLength;
    }
    output2D[id.xy] = float4(exp(-opticalDepth), 1.0);
}

[numthreads(8, 8, 1)]
// [LUT 2] 태양 cosine/고도 texel에서 구면 64방향*20구간을 평균한다.
// scatteringRatio=f를 구해 L/(1-f)로 반복 산란 급수를 근사한다.
// f<0.999와 분모>=0.001은 발산 방지; 이를 없애면 하늘이 Inf/과노출될 수 있다.
// 32x32 HDR 결과, 선행 Transmittance가 필요하다.
void CSMultiScattering(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint)transmittanceMultiSize.z ||
        id.y >= (uint)transmittanceMultiSize.w)
        return;
    float2 uv = (float2(id.xy) + 0.5) / transmittanceMultiSize.zw;
    float sunCosine = uv.x * 2.0 - 1.0;
    float3 sunDirection = float3(sqrt(saturate(1.0 - sunCosine * sunCosine)),
                                 sunCosine, 0.0);
    float viewHeight = lerp(AtmosphereBottomRadiusKm() + 0.01,
                            AtmosphereTopRadiusKm() - 0.01, uv.y);
    float3 position = float3(0.0, viewHeight, 0.0);
    float3 luminance = 0.0.xxx;
    float3 scatteringRatio = 0.0.xxx;
    [loop]
    for (uint directionIndex = 0u; directionIndex < 64u; ++directionIndex)
    {
        float2 xi = (float2(directionIndex % 8u, directionIndex / 8u) + 0.5) / 8.0;
        float phi = 2.0 * kAtmospherePi * xi.x;
        float cosine = 1.0 - 2.0 * xi.y;
        float sine = sqrt(saturate(1.0 - cosine * cosine));
        float3 direction = float3(cos(phi) * sine, cosine, sin(phi) * sine);
        bool hitsGround = false;
        float distance = AtmosphereDistanceToBoundary(position, direction, hitsGround);
        const uint stepCount = 20u;
        float stepLength = distance / (float)stepCount;
        float3 throughput = 1.0.xxx;
        [loop]
        for (uint stepIndex = 0u; stepIndex < stepCount; ++stepIndex)
        {
            float3 samplePosition = position + direction *
                (((float)stepIndex + 0.5) * stepLength);
            AtmosphereMedium medium = SampleAtmosphereMedium(samplePosition);
            float3 segmentT = exp(-medium.extinction * stepLength);
            float3 integral = (1.0.xxx - segmentT) /
                              max(medium.extinction, 1.0e-6.xxx);
            float3 sunT = SampleAtmosphereTransmittanceToSun(
                samplePosition, sunDirection);
            luminance += throughput * max(solarIrradianceAndMultiplier.xyz,
                0.0.xxx) * sunT * medium.scattering *
                (1.0 / (4.0 * kAtmospherePi)) * integral;
            scatteringRatio += throughput * medium.scattering * integral;
            throughput *= segmentT;
        }
        if (hitsGround)
        {
            float3 groundPosition = position + direction * distance;
            float3 normal = normalize(groundPosition);
            float3 sunT = SampleAtmosphereTransmittanceToSun(
                groundPosition, sunDirection);
            luminance += throughput * max(solarIrradianceAndMultiplier.xyz,
                0.0.xxx) * sunT * saturate(dot(normal, sunDirection)) *
                max(groundAlbedoAndDebugExposure.xyz, 0.0.xxx) /
                kAtmospherePi;
        }
    }
    luminance /= 64.0;
    scatteringRatio = min(scatteringRatio / 64.0, 0.999.xxx);
    output2D[id.xy] = float4(luminance /
        max(1.0.xxx - scatteringRatio, 1.0e-3.xxx), 1.0);
}

// SkyViewDirectionToUv의 역. v=0.5 지평선 기준 두 구간과 u 제곱을 되돌린다.
// 실제 태양 방위 대신 상대 수평각 좌표계를 사용하므로 방향 변경과 lookup이 같이 동작한다.
void SkyViewUvToDirection(float2 uv, float viewHeight,
                          out float3 direction)
{
    float bottom = AtmosphereBottomRadiusKm();
    float horizon = sqrt(max(viewHeight * viewHeight - bottom * bottom, 0.0));
    float beta = acos(clamp(horizon / max(viewHeight, 1.0e-6), -1.0, 1.0));
    float zenithHorizon = kAtmospherePi - beta;
    float viewCosine;
    if (uv.y < 0.5)
    {
        float coordinate = 1.0 - 2.0 * uv.y;
        coordinate = 1.0 - coordinate * coordinate;
        viewCosine = cos(zenithHorizon * coordinate);
    }
    else
    {
        float coordinate = 2.0 * uv.y - 1.0;
        viewCosine = cos(zenithHorizon + beta * coordinate * coordinate);
    }
    float horizontalCosine = -(uv.x * uv.x * 2.0 - 1.0);
    float viewSine = sqrt(saturate(1.0 - viewCosine * viewCosine));
    direction = normalize(float3(
        viewSine * horizontalCosine, viewCosine,
        viewSine * sqrt(saturate(1.0 - horizontalCosine * horizontalCosine))));
}

[numthreads(8, 8, 1)]
// [LUT 3] 카메라 고도/태양 상대 방향에 대해 30구간 대기 적분 → 192x108 HDR 하늘.
// Transmittance/Multi를 읽고 카메라 고도나 태양이 바뀌면 재생성한다.
void CSSkyView(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint)skyViewIrradianceSize.x ||
        id.y >= (uint)skyViewIrradianceSize.y)
        return;
    float2 uv = (float2(id.xy) + 0.5) / skyViewIrradianceSize.xy;
    float viewHeight = AtmosphereBottomRadiusKm() +
                       max(sunDirectionAndCameraHeight.w, 0.001);
    float3 position = float3(0.0, viewHeight, 0.0);
    float3 direction;
    SkyViewUvToDirection(uv, viewHeight, direction);
    float sunCosine = clamp(AtmosphereSunDirection().y, -1.0, 1.0);
    float3 sunDirection = float3(sqrt(saturate(1.0 - sunCosine * sunCosine)),
                                 sunCosine, 0.0);
    AtmosphereIntegration integrated = IntegrateAtmosphere(
        position, direction, sunDirection, 30u, false, true, 1.0e30);
    output2D[id.xy] = float4(integrated.radiance, 1.0);
}

[numthreads(8, 8, 1)]
// [LUT 4] 고도/태양 cosine마다 하늘 반구 32방향*16구간을 적분한다.
// cosine-weighted 표본의 PDF=cos/pi이므로 평균*pi가 입사 irradiance다.
// 64x16 HDR; 지면/구름 환경광이 사용하며 특정 카메라 방향에 종속되지 않는다.
void CSSkyIrradiance(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint)skyViewIrradianceSize.z ||
        id.y >= (uint)skyViewIrradianceSize.w)
        return;
    float2 uv = (float2(id.xy) + 0.5) / skyViewIrradianceSize.zw;
    float sunCosine = uv.x * 2.0 - 1.0;
    float3 sunDirection = float3(sqrt(saturate(1.0 - sunCosine * sunCosine)),
                                 sunCosine, 0.0);
    float viewHeight = lerp(AtmosphereBottomRadiusKm() + 0.01,
                            AtmosphereTopRadiusKm() - 0.01, uv.y);
    float3 position = float3(0.0, viewHeight, 0.0);
    float3 irradiance = 0.0.xxx;
    [loop]
    for (uint directionIndex = 0u; directionIndex < 32u; ++directionIndex)
    {
        float2 xi = (float2(directionIndex % 8u, directionIndex / 8u) + 0.5) /
                    float2(8.0, 4.0);
        float cosine = sqrt(1.0 - xi.y);
        float sine = sqrt(xi.y);
        float phi = 2.0 * kAtmospherePi * xi.x;
        float3 direction = float3(cos(phi) * sine, cosine, sin(phi) * sine);
        AtmosphereIntegration integrated = IntegrateAtmosphere(
            position, direction, sunDirection, 16u, false, true, 1.0e30);
        irradiance += integrated.radiance;
    }
    // cosine-weighted hemisphere sampling의 PDF=cos/pi이므로 평균*pi다.
    output2D[id.xy] = float4(irradiance * (kAtmospherePi / 32.0), 1.0);
}

cbuffer cbCamera : register(b0)
{
    // [파생 값] CPU transpose된 역 view-projection. 화면 UV/device depth [0,1] → 월드 위치(m). 직접 수정하면 깊이와 구름 가림이 어긋난다.
    float4x4 invViewProj;
    // [파생 값] 역 투영 행렬. NDC 방향 → view ray; 큰 월드에서 translation 없이 정밀한 레이를 복원.
    float4x4 invProjection;
    // [파생 값] 역 view 회전 행렬. view 방향 → 월드 방향, w=0으로 translation 제외.
    float4x4 invViewRotation;
    // [파생 값] xyz 카메라 월드 m, ray 시작점. F5~F8/이동에서 생성.
    float3 cameraPos;
    // [파생 값] 유효 구름 시간 s. 일반 실행은 실제 delta 누적, 테스트는 고정 입력; Weather/Base/Shadow에 동일 값.
    float atmosphereTime;
    // [파생 값] xy=전체 화면 가로/세로 pixel, 각각 >=1. Full-resolution ray/LUT 계약.
    float2 atmosphereRenderSize;
    // [파생 값] 카메라 near clip 거리 m, 양수. 깊이/투영 계약에서 생성.
    float atmosphereNearPlane;
    // [파생 값] 카메라 far clip 거리 m, near보다 큼. 하늘 ray 외부 한계; 구름 최대 거리는 b5도 제한.
    float atmosphereFarPlane;
};

[numthreads(4, 4, 4)]
// [LUT 5/6] 화면 UV로 월드 ray 복원 → z^2*128km 거리까지 4구간 적분.
// u0=추가 공기 RGB, u1=남은 배경 RGB 비율. 각 32³, A=1.
// 카메라 회전/투영/고도 변화가 dirty를 만든다. 생성과 조회의 z 제곱/sqrt가 반드시 대응해야 한다.
void CSAerialPerspective(uint3 id : SV_DispatchThreadID)
{
    uint size = aerialDebugGeneration.x;
    if (any(id >= size.xxx))
        return;
    float2 uv = (float2(id.xy) + 0.5) / (float)size;
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 viewH = mul(float4(ndc, 1.0, 1.0), invProjection);
    float3 viewDirection = normalize(viewH.xyz / max(abs(viewH.w), 1.0e-6));
    float3 worldDirection = normalize(
        mul(float4(viewDirection, 0.0), invViewRotation).xyz);
    float slice = ((float)id.z + 0.5) / (float)size;
    float distanceKm = ozoneLayerTurbidityAerialDistance.w * slice * slice;
    float cameraHeightKm = max(sunDirectionAndCameraHeight.w, 0.001);
    float3 position = float3(0.0, AtmosphereBottomRadiusKm() +
                             cameraHeightKm, 0.0);
    AtmosphereIntegration integrated = IntegrateAtmosphere(
        position, worldDirection, AtmosphereSunDirection(), 4u, false, true,
        distanceKm);
    output3DRadiance[id] = float4(integrated.radiance, 1.0);
    output3DTransmittance[id] = float4(integrated.transmittance, 1.0);
}
