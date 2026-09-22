// [학습 지도] CPU stage14::GpuParameters b9(224B) + t8~t13/s3 → 대기 매질/조회/합성 → Scene/Cloud/Tone. 대기 km/1km, HDR RGB.
// [수정 안내] [직접 조절]의 CPU/UI 원본을 수정한다. 강제 범위는 입력 계약이며 화질 보장이 아니다.
// 별도 권장 구간이 없는 값은 표시된 기본값을 비교 출발점으로 삼는다. b/t/u/s는 버퍼/읽기/쓰기/샘플러 슬롯.
// ============================================================================
//  Stage14Atmosphere.hlsli - 단계 14 b9, LUT 계약과 공통 대기 함수
// ============================================================================
#ifndef VCLOUD_STAGE14_ATMOSPHERE_HLSLI
#define VCLOUD_STAGE14_ATMOSPHERE_HLSLI

static const float kAtmospherePi = 3.14159265358979323846;

// CPU stage14::GpuParameters와 같은 14레지스터(224바이트)다.
cbuffer Stage14CB : register(b9)
{
    // [파생 값] Atmosphere 원본 → x=행성 반지름 km [1,100000], y=대기 반지름 [x+1,x+1000], z=Rayleigh 높이 [4,16], w=Mie 높이 [0.5,4]. Earth (6360,6460,8,2.0).
    float4 planetRadiiDensityHeights;
    // [파생 값] xyz=Rayleigh RGB 1/km >=0, w=scale [0.25,4]. 기본 (0.005802,0.013558,0.033100,1).
    float4 rayleighScatteringAndScale;
    // [파생 값] x=Mie 산란 1/km >=0, y=소멸 1/km >=x, z=g [0,0.95], w=흡수 배율 [0,4]. 기본 (0.003996,0.004440,0.3,1).
    float4 mieScatteringExtinctionGAbsorption;
    // [파생 값] xyz=오존 RGB 흡수 1/km >=0, w=scale [0,4]. 기본 (0.000650,0.001881,0.000085,1).
    float4 ozoneAbsorptionAndScale;
    // [파생 값] x=오존 중심 km [0,100], y=반폭 km [0.1,100], z=turbidity [0.25,4], w=Aerial 거리 128km 고정. 기본 (25,15,1,128).
    float4 ozoneLayerTurbidityAerialDistance;
    // [파생 값] xyz=Atmosphere 태양 RGB 기준 >=0(기본 1), w=Light.sunIntensity >=0(기본 1). 음수/비유한은 CPU 보정.
    float4 solarIrradianceAndMultiplier;
    // [파생 값] xyz=대기 표본→태양 단위벡터(각도에서 생성), w=max(camera.y,0)*0.001 km. 위치 m와 혼용 금지.
    float4 sunDirectionAndCameraHeight;
    // [파생 값] xyz=Light.sunColor 선형 RGB >=0, w=Ground.bounceMultiplier [0,2]. 기본 (1,0.95,0.85,1).
    float4 sunTintAndGroundBounce;
    // [파생 값] xyz=Ground RGB 반사율 [0,1], w=대기 진단 노출 [0.001,128]. 기본 (0.18,0.18,0.18,1).
    float4 groundAlbedoAndDebugExposure;
    // [파생 값] x=EV [-8,8], y=백색점 K [3500,10000], z=시각 hour [5.5,19.5], w=태양 고도 도 [-6,90]. 초기 (0,6500,7.5,18).
    float4 toneAndTime;
    // [파생 값] x=구름 거리 대기 진단 0/91~95, y=Tone, z=대기 진단, w=채널. 기본 모두 0.
    uint4 renderFlags;
    // [고정 품질] x/y=Transmittance 폭/높이 256/64, z/w=Multi 폭/높이 32/32 texel.
    float4 transmittanceMultiSize;
    // [고정 품질] x/y=SkyView 폭/높이 192/108, z/w=SkyIrradiance 폭/높이 64/16 texel.
    float4 skyViewIrradianceSize;
    // [파생 값] x=Aerial 축 32 고정, y=진단 slice uint [0,31], z=최대 LUT generation의 하위 32bit, w=0 예약.
    uint4 aerialDebugGeneration;
};

Texture2D<float4> atmosphereTransmittanceLut : register(t8);
Texture2D<float4> atmosphereMultiScatteringLut : register(t9);
Texture2D<float4> atmosphereSkyViewLut : register(t10);
Texture2D<float4> atmosphereSkyIrradianceLut : register(t11);
Texture3D<float4> atmosphereAerialRadianceLut : register(t12);
Texture3D<float4> atmosphereAerialTransmittanceLut : register(t13);
SamplerState atmosphereLinearClampSampler : register(s3);

float AtmosphereBottomRadiusKm() { return planetRadiiDensityHeights.x; }
float AtmosphereTopRadiusKm() { return planetRadiiDensityHeights.y; }
float RayleighScaleHeightKm() { return planetRadiiDensityHeights.z; }
float MieScaleHeightKm() { return planetRadiiDensityHeights.w; }
float3 AtmosphereSunDirection() { return normalize(sunDirectionAndCameraHeight.xyz); }

// [대기 좌표] 행성 중심 원점의 km 좌표, 정규화 방향을 사용한다(구름은 m).
// 1. |origin+t*direction|^2=radius^2의 이차방정식 근을 반환.
// 판별식<0은 miss (-1,-1), 양수 근 중 실제 진행 방향 경계를 선택한다.
float2 AtmosphereRaySphere(float3 origin, float3 direction, float radius)
{
    float b = dot(origin, direction);
    float c = dot(origin, origin) - radius * radius;
    float discriminant = b * b - c;
    if (discriminant < 0.0)
        return -1.0.xx;
    float root = sqrt(max(discriminant, 0.0));
    return float2(-b - root, -b + root);
}

float AtmosphereDistanceToBoundary(float3 position, float3 direction,
                                   out bool hitsGround)
{
    float2 top = AtmosphereRaySphere(
        position, direction, AtmosphereTopRadiusKm());
    float2 bottom = AtmosphereRaySphere(
        position, direction, AtmosphereBottomRadiusKm());
    float topDistance = top.y >= 0.0 ? max(top.y, 0.0) : 0.0;
    float groundDistance = bottom.x >= 0.0 ? bottom.x : 1.0e30;
    hitsGround = groundDistance < topDistance;
    return hitsGround ? groundDistance : topDistance;
}

struct AtmosphereMedium
{
    // [파생 값] xyz=RGB 분자 산란 1/km, 현재 고도 밀도와 scale 반영, >=0.
    float3 rayleighScattering;
    // [파생 값] xyz 동일한 에어로졸 산란 1/km, 현재 고도/turbidity 반영, >=0.
    float3 mieScattering;
    // [파생 값] xyz=RGB 총 산란 1/km, Rayleigh+Mie.
    float3 scattering;
    // [파생 값] xyz=RGB 총 소멸 1/km, 산란+Mie 흡수+오존 흡수.
    float3 extinction;
};

// [매질 순서] 1. 반지름-행성 반지름=고도(km).
// 2. Rayleigh/Mie는 exp(-고도/scaleHeight), 오존은 중심 주변 삼각형 분포.
// 3. 산란과 흡수를 나눠 extinction=산란+흡수를 만든다(모두 1/km).
// scale height를 올리면 높은 곳에도 매질이 남는다. km에 1/m 계수를 쓰면 천 배 오류다.
AtmosphereMedium SampleAtmosphereMedium(float3 position)
{
    float altitude = max(length(position) - AtmosphereBottomRadiusKm(), 0.0);
    float rayleighDensity = exp(-altitude / max(RayleighScaleHeightKm(), 1.0e-4));
    float mieDensity = exp(-altitude / max(MieScaleHeightKm(), 1.0e-4));
    float ozoneDensity = saturate(1.0 - abs(altitude - ozoneLayerTurbidityAerialDistance.x) /
                                  max(ozoneLayerTurbidityAerialDistance.y, 1.0e-4));
    // [직접 조절] F3 분자 산란 배율. [강제 범위]/UI [0.25,4], 기본 1 권장. 증가하면 푸른 하늘/대기 영향이 강해진다.
    float rayleighScale = max(rayleighScatteringAndScale.w, 0.0);
    // [직접 조절] F3 Mie 산란/소멸 배율. [강제 범위]/UI [0.25,4], 기본/권장 맑음 1/연무 2.5. 증가하면 원경 연무가 강해진다.
    float turbidity = max(ozoneLayerTurbidityAerialDistance.z, 0.0);
    float mieScattering = max(mieScatteringExtinctionGAbsorption.x, 0.0) *
                          turbidity * mieDensity;
    float baseMieExtinction = max(mieScatteringExtinctionGAbsorption.y,
                                  mieScatteringExtinctionGAbsorption.x);
    float baseMieAbsorption = max(baseMieExtinction -
                                  mieScatteringExtinctionGAbsorption.x, 0.0);
    float mieExtinction = (max(mieScatteringExtinctionGAbsorption.x, 0.0) +
                           baseMieAbsorption *
                           max(mieScatteringExtinctionGAbsorption.w, 0.0)) *
                          turbidity * mieDensity;
    AtmosphereMedium medium;
    medium.rayleighScattering = max(rayleighScatteringAndScale.xyz, 0.0.xxx) *
                                rayleighScale * rayleighDensity;
    medium.mieScattering = mieScattering.xxx;
    medium.scattering = medium.rayleighScattering + medium.mieScattering;
    medium.extinction = medium.rayleighScattering + mieExtinction.xxx +
        max(ozoneAbsorptionAndScale.xyz, 0.0.xxx) *
        max(ozoneAbsorptionAndScale.w, 0.0) * ozoneDensity;
    return medium;
}

// 분자 산란 각도 분포 3(1+cos^2)/(16*pi), 단위 sr^-1.
// 구름의 상대 HG 배율과 달리 구면 적분이 1인 정규화를 포함한다.
float AtmosphereRayleighPhase(float cosine)
{
    float mu = clamp(cosine, -1.0, 1.0);
    return 3.0 * (1.0 + mu * mu) / (16.0 * kAtmospherePi);
}

// 에어로졸의 전방 집중 위상 근사. cosine [-1,1], g [0,0.95].
// g가 커지면 태양 주변 봉우리가 좁고 밝아진다. 분모 하한은 0 나눗셈 방어다.
float AtmosphereMiePhase(float cosine)
{
    float g = clamp(mieScatteringExtinctionGAbsorption.z, 0.0, 0.95);
    float mu = clamp(cosine, -1.0, 1.0);
    float k = 3.0 * (1.0 - g * g) /
              (8.0 * kAtmospherePi * (2.0 + g * g));
    float denominator = max(1.0 + g * g - 2.0 * g * mu, 1.0e-6);
    return k * (1.0 + mu * mu) /
           (denominator * sqrt(denominator));
}

// [한 구간의 해] 일정 source를 ds 동안 감쇠시키며 적분하면
// source*(1-exp(-sigma*ds))/sigma. segmentT가 이미 exp를 담는다.
// RGB별로 계산하며 sigma 하한 1e-6은 수치 보호다. 무조건 source*ds로 바꾸면 두꺼운 대기에서 오차가 난다.
float3 AtmosphereIntegrateSource(float3 source, float3 extinction,
                                 float3 segmentTransmittance)
{
    float3 safeExtinction = max(extinction, 1.0e-6.xxx);
    return source * (1.0.xxx - segmentTransmittance) / safeExtinction;
}

// [Transmittance 주소] 입력은 중심에서의 높이 km와 천정각 cosine [-1,1].
// H/rho는 구면 지평선 기하 길이; 경계까지 거리로 u, 고도 관련 rho/H로 v를 만든다.
// 단순 위도/경도 UV가 아니다. 생성의 역함수와 같이 수정해야 LUT가 일치한다.
float2 AtmosphereTransmittanceParamsToUv(float viewHeight, float viewCosine)
{
    float bottom = AtmosphereBottomRadiusKm();
    float top = AtmosphereTopRadiusKm();
    float height = clamp(viewHeight, bottom, top);
    float mu = clamp(viewCosine, -1.0, 1.0);
    float H = sqrt(max(top * top - bottom * bottom, 0.0));
    float rho = sqrt(max(height * height - bottom * bottom, 0.0));
    float distance = -height * mu + sqrt(max(
        height * height * (mu * mu - 1.0) + top * top, 0.0));
    float minimumDistance = top - height;
    float maximumDistance = rho + H;
    return saturate(float2(
        (distance - minimumDistance) /
            max(maximumDistance - minimumDistance, 1.0e-6),
        rho / max(H, 1.0e-6)));
}

// 위 주소 변환의 역: UV → 중심 높이(km)/cosine. 생성 CS가 각 texel의 ray를 복원한다.
// distance≈0은 천정 방향 cosine=1로 처리해 0으로 나누지 않는다.
void AtmosphereUvToTransmittanceParams(float2 uv, out float viewHeight,
                                       out float viewCosine)
{
    float bottom = AtmosphereBottomRadiusKm();
    float top = AtmosphereTopRadiusKm();
    float H = sqrt(max(top * top - bottom * bottom, 0.0));
    float rho = H * saturate(uv.y);
    viewHeight = sqrt(rho * rho + bottom * bottom);
    float minimumDistance = top - viewHeight;
    float maximumDistance = rho + H;
    float distance = minimumDistance + saturate(uv.x) *
                     (maximumDistance - minimumDistance);
    viewCosine = distance <= 1.0e-6 ? 1.0 :
        (H * H - rho * rho - distance * distance) /
        (2.0 * viewHeight * distance);
    viewCosine = clamp(viewCosine, -1.0, 1.0);
}

// 태양 ray가 행성을 먼저 만나면 빛은 0. 아니면 위치 반지름/태양 cosine로 t8을 조회.
// 결과 RGB [0,1]은 대기만 통과한 태양 생존율이며 구름 차폐는 별도로 적용한다.
float3 SampleAtmosphereTransmittanceToSun(float3 position,
                                          float3 sunDirection)
{
    float2 planetHit = AtmosphereRaySphere(
        position, sunDirection, AtmosphereBottomRadiusKm());
    if (planetHit.x >= 0.0)
        return 0.0.xxx;
    float height = length(position);
    float cosine = dot(position / max(height, 1.0e-6), sunDirection);
    float2 uv = AtmosphereTransmittanceParamsToUv(height, cosine);
    return atmosphereTransmittanceLut.SampleLevel(
        atmosphereLinearClampSampler, uv, 0).rgb;
}

// Multi LUT의 x는 태양 천정각 cosine을 [0,1]로 옮긴 값, y는 정규화 고도.
// Transmittance LUT와 UV 뜻이 다르므로 같은 UV를 재사용하면 잘못된 하늘색이 나온다.
float2 AtmosphereMultiScatteringUv(float3 position, float3 sunDirection)
{
    float height = length(position);
    float sunCosine = dot(position / max(height, 1.0e-6), sunDirection);
    return saturate(float2(sunCosine * 0.5 + 0.5,
        (height - AtmosphereBottomRadiusKm()) /
        max(AtmosphereTopRadiusKm() - AtmosphereBottomRadiusKm(), 1.0e-6)));
}

float3 SampleAtmosphereMultipleScattering(float3 position,
                                          float3 sunDirection)
{
    return atmosphereMultiScatteringLut.SampleLevel(
        atmosphereLinearClampSampler,
        AtmosphereMultiScatteringUv(position, sunDirection), 0).rgb;
}

// [SkyView 주소 순서] 1. 시선/태양의 수평 상대각을 계산한다.
// 2. 구면 지평선 각도를 기준으로 하늘/지면 방향을 v의 앞/뒤 절반에 나눈다.
// 3. sqrt 비선형 분배로 급격히 바뀌는 지평선 주변에 표본을 배치한다.
// 천정처럼 수평 길이가 0인 방향은 중립 상대각으로 처리한다.
float2 AtmosphereSkyViewDirectionToUv(float3 viewDirection,
                                      float3 sunDirection,
                                      float viewHeight, bool hitsGround)
{
    float3 up = float3(0.0, 1.0, 0.0);
    float viewCosine = clamp(dot(viewDirection, up), -1.0, 1.0);
    float3 viewHorizontal = viewDirection - up * viewCosine;
    float3 sunHorizontal = sunDirection - up * dot(sunDirection, up);
    float viewHorizontalLength = length(viewHorizontal);
    float sunHorizontalLength = length(sunHorizontal);
    float lightViewCosine = viewHorizontalLength > 1.0e-5 &&
                            sunHorizontalLength > 1.0e-5
        ? dot(viewHorizontal / viewHorizontalLength,
              sunHorizontal / sunHorizontalLength) : 1.0;
    float bottom = AtmosphereBottomRadiusKm();
    float horizon = sqrt(max(viewHeight * viewHeight - bottom * bottom, 0.0));
    float beta = acos(clamp(horizon / max(viewHeight, 1.0e-6), -1.0, 1.0));
    float zenithHorizon = kAtmospherePi - beta;
    float2 uv;
    if (!hitsGround)
    {
        float coordinate = acos(viewCosine) / max(zenithHorizon, 1.0e-6);
        coordinate = 1.0 - sqrt(saturate(1.0 - coordinate));
        uv.y = coordinate * 0.5;
    }
    else
    {
        float coordinate = (acos(viewCosine) - zenithHorizon) /
                           max(beta, 1.0e-6);
        uv.y = sqrt(saturate(coordinate)) * 0.5 + 0.5;
    }
    uv.x = sqrt(saturate(-lightViewCosine * 0.5 + 0.5));
    return saturate(uv);
}

float3 SampleAtmosphereSkyView(float3 viewDirection, float3 sunDirection,
                               float cameraHeightKm)
{
    float viewHeight = AtmosphereBottomRadiusKm() + max(cameraHeightKm, 0.0);
    float3 position = float3(0.0, viewHeight, 0.0);
    float2 planetHit = AtmosphereRaySphere(
        position, viewDirection, AtmosphereBottomRadiusKm());
    float2 uv = AtmosphereSkyViewDirectionToUv(
        viewDirection, sunDirection, viewHeight, planetHit.x >= 0.0);
    return atmosphereSkyViewLut.SampleLevel(
        atmosphereLinearClampSampler, uv, 0).rgb;
}

// 대기 상단 태양 RGB*T대기*Light tint*intensity → 현 위치 직접 입사광.
// 이 함수의 출력은 cloud T를 아직 곱하지 않는다. intensity/tint는 b3 원본을 b9로 재패킹해 읽는다.
float3 SampleAtmosphereSunRadiance(float altitudeKm, float3 sunDirection)
{
    float3 position = float3(0.0, AtmosphereBottomRadiusKm() +
                             max(altitudeKm, 0.0), 0.0);
    float3 physical = max(solarIrradianceAndMultiplier.xyz, 0.0.xxx) *
        SampleAtmosphereTransmittanceToSun(position, sunDirection);
    return physical * max(sunTintAndGroundBounce.xyz, 0.0.xxx) *
           max(solarIrradianceAndMultiplier.w, 0.0);
}

// [Aerial 깊이] m→km 후 sqrt(distance/128km). 생성의 distance=128*z^2와 역관계.
// 작은 거리에서 더 촘촘한 slice를 쓴다. 범위 밖은 [0,1] 끝 slice로 제한한다.
float AtmosphereAerialTextureDepth(float distanceMeters)
{
    float distanceKm = max(distanceMeters, 0.0) * 0.001;
    return sqrt(saturate(distanceKm /
        max(ozoneLayerTurbidityAerialDistance.w, 1.0e-6)));
}

float3 SampleAtmosphereAerialRadiance(float2 uv, float distanceMeters)
{
    return atmosphereAerialRadianceLut.SampleLevel(
        atmosphereLinearClampSampler,
        float3(saturate(uv), AtmosphereAerialTextureDepth(distanceMeters)),
        0).rgb;
}

float3 SampleAtmosphereAerialTransmittance(float2 uv, float distanceMeters)
{
    return atmosphereAerialTransmittanceLut.SampleLevel(
        atmosphereLinearClampSampler,
        float3(saturate(uv), AtmosphereAerialTextureDepth(distanceMeters)),
        0).rgb;
}

// [HDR 합성 순서] 1. 물체면 공기 산란+공기T*표면빛, 하늘이면 SkyView로 배경을 만든다.
// 2. 구름 대표 깊이까지의 공기 산란 cloudAir와 공기T를 읽는다.
// 3. cloudAir + airT*cloud빛 + cloudT*(배경-cloudAir).
// 배경에서 앞쪽 공기를 빼고 cloudT를 적용해 같은 공기를 두 번 세지 않는다.
// 구름이 없으면 cloudT=1/cloud빛=0으로 원래 배경이 복원된다. 대표 깊이 기반 근사다.
#if defined(VCLOUD_TEST_AERIAL_SCALE_RUNTIME)
static float testAerialScale=1;
#endif
// 2026-09-22 사용자 승인: 구름 앞 공기만 거리 2배로 조회한다.
// 진단 A/B/C/D는 물리 거리 1배를 보존하고 후보 정의는 절대 배율이다.
float CloudAerialLookupDepth(float depth)
{
#if defined(VCLOUD_TEST_AERIAL_SCALE_RUNTIME)
    return max(depth, 0.0) * testAerialScale;
#elif defined(VCLOUD_TEST_AERIAL_SCALE)
    return max(depth, 0.0) * VCLOUD_TEST_AERIAL_SCALE;
#elif defined(VCLOUD_TEST_AERIAL_COMPOSITION)
    return max(depth, 0.0);
#else
    return max(depth, 0.0) * 2.0;
#endif
}
float3 ComposeStage14Atmosphere(
    float2 uv, float3 rayDirection, bool hasGeometry,
    float3 litSurface, float sceneDistance,
    float3 cloudScattering, float cloudTransmittance,
    float cloudRepresentativeDepth, bool omitCloudAerial = false)
{
    float3 clearBackground;
    if (hasGeometry)
    {
        float3 sceneAir = SampleAtmosphereAerialRadiance(uv, sceneDistance);
        float3 sceneAirT = SampleAtmosphereAerialTransmittance(
            uv, sceneDistance);
        clearBackground = sceneAir + sceneAirT * max(litSurface, 0.0.xxx);
    }
    else
    {
        clearBackground = SampleAtmosphereSkyView(
            rayDirection, AtmosphereSunDirection(),
            sunDirectionAndCameraHeight.w);
    }

    if (omitCloudAerial)
        return max(cloudScattering, 0.0.xxx) + saturate(cloudTransmittance) * clearBackground;
    float safeCloudDepth = CloudAerialLookupDepth(cloudRepresentativeDepth);
    float3 cloudAir = SampleAtmosphereAerialRadiance(uv, safeCloudDepth);
    float3 cloudAirT = SampleAtmosphereAerialTransmittance(uv, safeCloudDepth);
    float safeCloudT = saturate(cloudTransmittance);
    return cloudAir + cloudAirT * max(cloudScattering, 0.0.xxx) +
           safeCloudT * (clearBackground - cloudAir);
}

#endif
