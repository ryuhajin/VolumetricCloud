// [학습 지도] t2 Weather/t3 Base/t4 Detail + b1/b6/b7/b10 → 밀도 표본 → View/Light/Deep Shadow. 좌표 m→cycle, rho 무차원.
// [수정 안내] [직접 조절]의 CPU/UI 원본을 수정한다. 강제 범위는 입력 계약이며 화질 보장이 아니다.
// 별도 권장 구간이 없는 값은 표시된 기본값을 비교 출발점으로 삼는다. b/t/u/s는 버퍼/읽기/쓰기/샘플러 슬롯.
// ============================================================================
//  Noise.hlsli - 구름 렌더와 Noise Lab이 함께 사용하는 단계 5 밀도 함수
// ----------------------------------------------------------------------------
//  데이터 흐름
//  1. 월드 위치(m)를 바람이 이동시킨 noise 좌표로 바꾼다.
//  2. Base Texture3D와 coverage로 기본 덩어리 밀도를 만든다.
//  3. PlanarLayer 안의 로컬 컬럼 높이와 부드러운 타입 profile을 계산한다.
//  4. Weather R/G/B로 threshold, 구름 종류 높이와 밀도 배율을 조절한다.
//  5. Weather가 적용된 큰 구름 형태를 Base Density로 확정한다.
//  6. Base가 존재할 때만 별도 고주파 Detail Noise를 샘플링해 밀도를 깎는다.
//
//  Base는 Perlin-Worley RGBA Texture3D, Detail은 Worley RGBA Texture3D다.
//  Light 경로는 비용을 제한하기 위해 Detail이 아닌 Base Density를 적분한다.
// ============================================================================
#ifndef VCLOUD_NOISE_HLSLI
#define VCLOUD_NOISE_HLSLI

#include "CloudParameters.hlsli"
#include "NoiseVolumeParameters.hlsli"
#include "HighCloudQuality.hlsli"
#include "CloudAdvection.hlsli"

Texture3D<float4> baseNoiseVolumeTexture : register(t3);
Texture3D<float4> detailNoiseVolumeTexture : register(t4);

#ifndef VCLOUD_NOISE_TEST_BIAS
#define VCLOUD_NOISE_TEST_BIAS 0.0
#endif

// 사용자 채택: 일반 실행은 정규화 형상 remap(2).
// 과거 비교 실행기의 0/1/2와 옛 코어 실험은 시험 정의로 보존한다.
#ifndef VCLOUD_TEST_NEAR_CLARITY
#if defined(VCLOUD_TEST_DETAIL_CORE_MODE)
#define VCLOUD_TEST_NEAR_CLARITY 0
#else
#define VCLOUD_TEST_NEAR_CLARITY 2
#endif
#endif

struct CloudDensitySample
{
    float rawNoise;          // threshold 전 Base Texture3D 조합값(0~1).
    float thresholdDensity; // coverage만 적용한 단계 2 기본 밀도(0~1).
    float weatherThresholdDensity; // Weather R까지 적용한 threshold 밀도.
    float heightFraction;    // PlanarLayer 바닥=0, 천장=1인 정규화 월드 Y 높이.
    float localThicknessMeters;// Weather A/G가 정한 이 XZ 기둥의 물리 두께(m).
    float localBaseLiftMeters;// Weather A가 전역 바닥에서 올린 로컬 바닥(m).
    float localHeightFraction;// 로컬 바닥=0, 로컬 상단=1인 정규화 높이.
    float heightProfile;     // 위·아래 경계를 부드럽게 지우는 마스크(0~1).
    float typedShapeProfile; // 높이/Type이 정한 shape threshold 마스크(0~1).
    float effectiveShapeCoverage;// coverage × lerp(0.70,1,R) × footprintFactor, [0,1].
    float baseSupport;       // Detail/밀도 배율 전 Base shape가 존재하면 1.
    float weatherCoverage;   // Weather Map R 채널(0~1).
    float cloudType;         // b10 선택을 적용한 유효 타입(0~1); 저장된 G와 다를 수 있다.
    float weatherDensityModifier; // Weather B를 0.5~1.5로 바꾼 배율.
    float weatherThicknessPotential;// Weather A 로컬 두께 보간값(0~1).
    float baseDensity;       // 배율까지 적용한 큰 형태. 비음수이며 1을 넘을 수 있다.
    float detailNoise;       // 실제 샘플한 고주파 침식 noise(0~1).
    float erosion;           // 일반 remap의 형상 문턱 e. 시험0에서는 밀도 감산량.
    float finalDensity;      // Detail 적용 시 A*remap(S,e,1); 생략 시 Base 그대로(1 초과 가능). 이후 공통 shaping.
    float detailSampled;     // Detail 함수를 호출했으면 1, 생략했으면 0.
    float3 noiseUvw;         // Base Texture3D의 연속 좌표(cycle).
    float3 detailNoiseUvw;   // Detail Texture3D의 연속 좌표(cycle), 생략 시 0.
    float2 weatherUv;        // 반복되는 2D Weather Map 조회 좌표(0~1).
    float4 baseNoiseChannels;// Base Texture3D RGBA.
    float4 detailNoiseChannels;// Detail Texture3D RGBA.
};

// 서로 다른 노이즈 알고리즘도 동일한 값+좌표 인터페이스로 연결하기 위한 표본이다.
// 단계 4는 value만 사용하지만 이후 fBm/Worley도 이 구조를 반환하게 한다.
struct NoiseFieldSample
{
    // [파생 값] RGBA 조합 뒤 [0,1] noise scalar. 아직 최종 밀도가 아니다.
    float value;
    // [파생 값] xyz 반복 texture 좌표 cycle; 현재 조회 함수에서는 frac된 [0,1).
    float3 uvw;
    // [파생 값] x/y/z/w=샘플한 R/G/B/A [0,1]; 생성 규격과 weights를 함께 해석.
    float4 channels;
};

// 월드 Y 위치(m)를 PlanarLayer의 전역 0~1 높이로 바꾼다.
// cloudBounds Y는 b5 Planar bottom/top의 mirror다. 두께가 없으면 0을 반환한다.
// 이 분기는 0 나눗셈과 NaN이 검정 화면이나 번쩍임으로 번지는 것을 막는다.
float EvaluateHeightFraction(float worldY)
{
    float cloudThickness = cloudBoundsMax.y - cloudBoundsMin.y;
    float validThickness = cloudThickness > 1e-6 ? 1.0 : 0.0;
    float safeThickness = max(cloudThickness, 1e-6);
    return saturate((worldY - cloudBoundsMin.y) / safeThickness) * validThickness;
}

// Weather와 물리 컬럼 형상 함수를 가져온다.
#include "Weather.hlsli"

// [문턱 재매핑] coverage=0.4이면 noise 0.6 이하를 비우고 나머지를 0~1로 펼친다.
// coverage가 커질수록 더 많은 noise가 살아남는다. 0 근처는 나눗셈을 막고 0을 반환한다.
float RemapCoverage(float rawNoise, float coverageValue)
{
    float safeCoverage = saturate(coverageValue);
    float validCoverage = safeCoverage > 1e-4 ? 1.0 : 0.0;
    float safeDivisor = max(safeCoverage, 1e-4);
#if defined(VCLOUD_TEST_BASE_THRESHOLD_OFFSET)
    // 비교 전용: 분모를 유지해 문턱 이동과 대비 변경을 분리한다.
    rawNoise -= VCLOUD_TEST_BASE_THRESHOLD_OFFSET;
#endif
    return saturate((rawNoise - (1.0 - safeCoverage)) / safeDivisor) *
        validCoverage;
}

// Base shape는 High 경로의 주기 Texture3D만 사용한다.
NoiseFieldSample SampleBaseShapeNoise(float3 worldPosition, float timeSeconds)
{
    NoiseFieldSample result = (NoiseFieldSample)0;
    float3 stationaryWorld = ComputePhysicalCloudSamplePosition(
        worldPosition, timeSeconds);
    result.uvw.xz = frac(stationaryWorld.xz /
        max(baseVolumeWorldSizeMeters, 1.0) + noiseOffset.xx);
    result.uvw.y = frac((stationaryWorld.y - cloudBoundsMin.y) /
        max(baseVolumeVerticalWorldSizeMeters, 1.0) + noiseOffset);
    result.channels = baseNoiseVolumeTexture.SampleLevel(
        weatherMapSampler, result.uvw, 0);
    float worleyFbm = dot(result.channels.gba, baseVolumeWeights.xyz);
    float lowerBound = -(1.0 - worleyFbm);
    result.value = saturate(
        (result.channels.r - lowerBound) / max(1.0 - lowerBound, 1e-4) +
        VCLOUD_NOISE_TEST_BIAS);
#if defined(VCLOUD_TEST_BASE_CONTRAST)
    // 비교 전용: 생성 텍스처는 그대로 두고 조회 조합값의 분포만 넓힌다.
    result.value = saturate(0.65 + (result.value - 0.65) * VCLOUD_TEST_BASE_CONTRAST);
#endif
    return result;
}

// 표면 침식은 네 주기 Worley 대역을 가진 Detail Texture3D를 사용한다.
NoiseFieldSample SampleDetailErosionNoise(float3 worldPosition, float timeSeconds)
{
    NoiseFieldSample result = (NoiseFieldSample)0;
    float3 stationaryWorld = ComputePhysicalCloudSamplePosition(
        worldPosition, timeSeconds);
    result.uvw = frac(stationaryWorld /
        max(detailVolumeWorldSizeMeters, 1.0) + detailNoiseOffset.xxx);
    result.channels = detailNoiseVolumeTexture.SampleLevel(
        weatherMapSampler, result.uvw, 0);
    result.value = saturate(dot(result.channels, detailVolumeWeights));
    return result;
}

// [근경 미세 Detail, E19] 가까운 표본에서만 Detail에 평균0 섭동을 더해 작은 굴곡을 만든다.
// 값은 선택한 구름 타입의 Formation(F2 슬라이더, 타입별 Save Preset)에서 온다: tile=b6 nearMicroTileMeters,
// strength/mean/warp/warp freq=b7. strength 0이면 호출부가 조회 자체를 건너뛴다.
// 1. p = 바람 적용 월드 위치 / tile.
// 2. warp>0이면 부드러운 gradient noise 벡터로 좌표를 비튼다(domain warp). 같은 위치는 항상 같은 offset이다.
// 3. 전용 미세 Worley 64³(t14)를 p와, 다른 회전·0.731배 좌표로 두 번 읽어 평균한다(DUAL).
//    두 반복 주기가 나눠떨어지지 않아 TILE 주기 격자가 드러나지 않는다.
// 4. (평균 − mean) × Detail 표준편차 정규화 배율 × √2를 반환한다. strength의 의미는 "Detail 표준편차의 몇 배"다.
Texture3D<float> nearMicroVolume : register(t14);

// 구운 64³ texel 표준편차 0.107505(seed 1337+5003 고정 실측)를 Detail 가중합 표준편차 0.056369에 맞춘다.
// 두 조회 평균은 표준편차가 1/√2로 줄므로 √2를 곱한다(두 조회 상관은 미측정, 근사).
static const float kNearMicroScale = 0.056369 / 0.107505 * 1.41421356;
// warp offset 필드의 단일 gradient noise 표준편차(tests/NearMicroNoiseStats.py 30만 표본).
static const float kNearMicroWarpNoiseStd = 0.190687;

uint3 NearMicroHash(uint3 v)
{
    // PCG3D 정수 해시. 음수 격자 좌표는 2의 보수 비트 그대로 사용한다.
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    return v;
}

// quintic gradient noise 벡터(비주기, 2차 미분 연속). 모서리마다 해시 1회, 성분 순서만 바꾼 세 gradient로 x/y/z를 만든다.
float3 NearMicroWarpNoise(float3 p)
{
    float3 cell = floor(p);
    float3 f = p - cell;
    float3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    int3 c = int3(cell);
    float3 corners[8];
    [unroll] for (int i = 0; i < 8; ++i)
    {
        int3 o = int3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        float3 g = float3(NearMicroHash(asuint(c + o))) * (2.0 / 4294967295.0) - 1.0;
        float3 d = f - float3(o);
        corners[i] = float3(dot(g, d), dot(g.yzx, d), dot(g.zxy, d));
    }
    float3 x00 = lerp(corners[0], corners[1], u.x), x10 = lerp(corners[2], corners[3], u.x);
    float3 x01 = lerp(corners[4], corners[5], u.x), x11 = lerp(corners[6], corners[7], u.x);
    return lerp(lerp(x00, x10, u.y), lerp(x01, x11, u.y), u.z);
}

float NearMicroTileMeters()
{
    return max(nearMicroTileMeters, 1.0);
}

// 반환값은 평균0 섭동이며 호출부가 strength × 거리 가중치를 곱한다.
float SampleNearMicroPerturbation(float3 worldPosition, float timeSeconds)
{
    float3 p = ComputePhysicalCloudSamplePosition(worldPosition, timeSeconds) /
        NearMicroTileMeters();
    if (nearMicroWarp > 0.0)
        p += (nearMicroWarp / kNearMicroWarpNoiseStd) *
            NearMicroWarpNoise(p * nearMicroWarpFrequency + 31.7);
    float3 q = mul(float3x3(0.36, 0.48, -0.80, -0.80, 0.60, 0.00, 0.48, 0.64, 0.60), p) * 0.731 +
        float3(0.213, 0.577, 0.891);
    float v = 0.5 * (nearMicroVolume.SampleLevel(weatherMapSampler, frac(p), 0) +
                     nearMicroVolume.SampleLevel(weatherMapSampler, frac(q), 0));
    return (v - nearMicroMean) * kNearMicroScale;
}

// [밀도 조립] 입력은 월드 m, Base [0,1], Weather RGBA 해석값이다.
// 1. 기둥의 두께/lift/정규화 높이를 계산한다.
// 2. Weather support로 없는 지역을 지우고 footprint로 수평 문턱을 조절한다.
// 3. 문턱 통과 밀도*수직 profile*densityMultiplier*Weather B 배율을 만든다.
// Base는 1을 넘을 수 있다. Detail 생략 경로에 무조건 [0,1]이라고 가정하지 않는다.
CloudDensitySample ComposeBaseCloudDensity(
    float3 worldPosition, NoiseFieldSample baseNoise, WeatherSample weather)
{
    CloudDensitySample sample = (CloudDensitySample)0;
    sample.noiseUvw = baseNoise.uvw;
    sample.baseNoiseChannels = baseNoise.channels;
    sample.rawNoise = baseNoise.value;

    sample.thresholdDensity = RemapCoverage(sample.rawNoise, coverage);
    sample.weatherUv = weather.uv;
    sample.weatherCoverage = weather.coverage;
    sample.cloudType = weather.cloudType;
    sample.weatherDensityModifier = weather.densityModifier;
    sample.weatherThicknessPotential = weather.localThicknessPotential;
    sample.heightFraction = EvaluateHeightFraction(worldPosition.y);
    PhysicalColumnGeometry geometry = EvaluatePhysicalColumnGeometry(
        worldPosition.y, weather);
    sample.localThicknessMeters = geometry.localThicknessMeters;
    sample.localBaseLiftMeters = geometry.localBaseLiftMeters;
    sample.localHeightFraction = geometry.localHeightFraction;
    float insideLocalColumn = sample.localHeightFraction >= 0.0 &&
        sample.localHeightFraction <= 1.0 ? 1.0 : 0.0;
    // 높이 마스크가 없으면 local column 바닥과 천장이 칼로 자른 듯 보인다.
    // X/Z 덩어리 위치를 바꾸지 않고 Y 경계에서만 밀도를 0으로 부드럽게 줄인다.
    sample.heightProfile = EvaluateProfileEnvelope(
        sample.localHeightFraction, bottomFadeEnd, topFadeStart);
    {
        float typedVerticalProfile = EvaluateCommonVerticalProfile(sample.localHeightFraction);
        float typedFootprintScale = EvaluatePhysicalTypedFootprintScale(
            sample.localHeightFraction, sample.cloudType);
        sample.typedShapeProfile = typedVerticalProfile * typedFootprintScale;
        // 13-4E: Weather와 세로 profile을 noise threshold에 다시 곱하면
        // 상단/약한 Weather가 이중으로 잘려 납작하고 성긴 구름이 된다.
        // Weather는 support와 완만한 수평 coverage로, profile은 최종 밀도로
        // 각각 한 번만 반영한다.
        float weatherSupport = smoothstep(
            0.02, 0.20, sample.weatherCoverage);
        float weatherFactor = lerp(
            0.70, 1.00, sample.weatherCoverage);
        float footprintFactor = EvaluateFootprintCoverageFactor(
            typedFootprintScale);
        sample.effectiveShapeCoverage = saturate(
            coverage * weatherFactor * footprintFactor);
        sample.weatherThresholdDensity = RemapCoverage(
            sample.rawNoise, sample.effectiveShapeCoverage);
        sample.baseDensity = insideLocalColumn * weatherSupport *
            sample.weatherThresholdDensity * typedVerticalProfile *
            max(densityMultiplier, 0.0) * sample.weatherDensityModifier;
    }
    sample.baseSupport = insideLocalColumn &&
        sample.weatherThresholdDensity > 0.0 ? 1.0 : 0.0;
    sample.finalDensity = sample.baseDensity;
    return sample;
}

// Weather와 로컬 형상만으로 확실히 빈 표본이면 Base Texture3D를 읽지 않는다.
// [support precheck] Texture3D를 읽기 전에 Weather와 높이만으로 확실한 빈 공간을 찾는다.
// 이 판정은 빛/밀도를 새로 만드는 것이 아니라 비싼 fetch를 생략한다.
// 진단 필드는 빈 곳에서도 Weather/기둥 정보를 남겨 어떤 단계에서 사라졌는지 보여 준다.
CloudDensitySample EvaluateBaseCloudDensity(
    float3 worldPosition, float timeSeconds)
{
    CloudDensitySample result = (CloudDensitySample)0;
    WeatherSample weather = SampleWeatherMap(worldPosition, timeSeconds);
    PhysicalColumnGeometry geometry = EvaluatePhysicalColumnGeometry(
        worldPosition.y, weather);
    float localThickness = geometry.localThicknessMeters;
    float localHeight = geometry.localHeightFraction;
    float verticalProfile = EvaluateCommonVerticalProfile(localHeight);
    float weatherSupport = smoothstep(0.02, 0.20, weather.coverage);
    bool definitelyEmpty = localHeight < 0.0 || localHeight > 1.0 ||
        verticalProfile <= 0.0 || weatherSupport <= 0.0 ||
        coverage <= 0.0 || densityMultiplier <= 0.0;
#if defined(VCLOUD_TEST_NEAR_FAR_NO_PRECHECK)
    definitelyEmpty = false; // 참조 진단: 동일 조립식으로 0 여부를 직접 계산한다.
#endif
    if (!definitelyEmpty)
    {
        NoiseFieldSample baseNoise = SampleBaseShapeNoise(
            worldPosition, timeSeconds);
        result = ComposeBaseCloudDensity(worldPosition, baseNoise, weather);
    }
    else
    {
        result.weatherUv = weather.uv;
        result.weatherCoverage = weather.coverage;
        result.cloudType = weather.cloudType;
        result.weatherDensityModifier = weather.densityModifier;
        result.weatherThicknessPotential = weather.localThicknessPotential;
        result.localThicknessMeters = localThickness;
        result.localBaseLiftMeters = geometry.localBaseLiftMeters;
        result.localHeightFraction = localHeight;
        result.typedShapeProfile = max(verticalProfile, 0.0);
    }
    return result;
}

// Light Ray는 최종 scalar Base Density만 필요하다. Physical Shape에서는 먼저
// Weather와 로컬 높이를 검사해 확실히 빈 표본이면 Base Texture3D fetch를 생략한다.
// 남은 표본은 EvaluateBaseCloudDensity와 같은 coverage/profile 식을 사용한다.
float EvaluateLightCloudDensity(float3 worldPosition, float timeSeconds)
{
    float lightDensity = 0.0;
    WeatherSample weather = SampleWeatherMap(worldPosition, timeSeconds);
    PhysicalColumnGeometry geometry = EvaluatePhysicalColumnGeometry(
        worldPosition.y, weather);
    float localHeightFraction = geometry.localHeightFraction;
    if (localHeightFraction >= 0.0 && localHeightFraction <= 1.0)
    {
        float typedVerticalProfile = EvaluateCommonVerticalProfile(localHeightFraction);
        float weatherSupport = smoothstep(0.02, 0.20, weather.coverage);
        if (typedVerticalProfile > 0.0 && weatherSupport > 0.0)
        {
            float typedFootprintScale = EvaluatePhysicalTypedFootprintScale(
                localHeightFraction, weather.cloudType);
            float weatherFactor = lerp(0.70, 1.00, weather.coverage);
            float footprintFactor = EvaluateFootprintCoverageFactor(
                typedFootprintScale);
            float effectiveShapeCoverage = saturate(
                coverage * weatherFactor * footprintFactor);
            NoiseFieldSample baseNoise = SampleBaseShapeNoise(
                worldPosition, timeSeconds);
            float thresholdDensity = RemapCoverage(
                baseNoise.value, effectiveShapeCoverage);
            lightDensity = weatherSupport * thresholdDensity *
                typedVerticalProfile * max(densityMultiplier, 0.0) *
                weather.densityModifier;
        }
    }
    return ShapeCloudDensity(lightDensity);
}

// 코어 근사. 진짜 표면 거리/SDF가 아니다. 높이 감쇠 전 shape와
// profile 안쪽을 함께 사용하며 Weather/높이 경계에서는 보호를 줄인다.
float DetailCoreForComparison(CloudDensitySample sample)
{
    return smoothstep(0.15,0.40,sample.weatherThresholdDensity) *
        smoothstep(0.15,0.75,EvaluateCommonVerticalProfile(sample.localHeightFraction)) *
        smoothstep(0.02,0.20,sample.weatherCoverage);
}

// Base Shape 뒤에 선택적으로 Detail Erosion을 적용한다.
// sampleDetail=false, 빈 Base, strength=0 경로는 Detail 함수 자체를 호출하지 않는다.
// 이 조기 반환은 단계 4의 기능 요구이며 단계 9의 레이 스텝 최적화와는 별개다.
// [Detail 순서] 1. Base를 평가. 2. 필요하고 Base>0/strength>0일 때만 Detail fetch.
// 3. 밀도 배율 전 형상 S로 boundary와 침식량 e를 정한다.
// 4. [e,1]을 [0,1]로 remap한 뒤 높이/밀도 배율 A를 적용한다. e>=1은 완전 침식.
// sampleDetail=false는 Base 그대로 반환하므로 Shadow 경로와 View 최종 표면은 의도적으로 다르다.
// nearMicroWeight는 View 경로의 근경 미세 가중치(0~1)다. 0이면 기존 연산과 완전히 같다.
CloudDensitySample SampleCloudDensity(float3 worldPosition, float timeSeconds,
                                      bool sampleDetail, float nearMicroWeight)
{
    CloudDensitySample sample = EvaluateBaseCloudDensity(worldPosition, timeSeconds);
    bool shouldApplyDetail = sampleDetail && sample.baseDensity > 0.0 &&
                             detailErosionStrength > 0.0;
    if (shouldApplyDetail)
    {
        NoiseFieldSample detail = SampleDetailErosionNoise(worldPosition, timeSeconds);
        sample.detailNoiseUvw = detail.uvw;
        sample.detailNoiseChannels = detail.channels;
        sample.detailNoise = detail.value;
        sample.detailSampled = 1.0;
        // 근경 미세 Detail: 평균0 섭동. 가중치 0 또는 strength 0이면 조회 없이 기존 값 그대로다.
        if (nearMicroWeight > 0.0 && nearMicroStrength > 0.0)
            sample.detailNoise = saturate(sample.detailNoise +
                nearMicroStrength * nearMicroWeight *
                SampleNearMicroPerturbation(worldPosition, timeSeconds));
    }
    if (shouldApplyDetail)
    {
#if VCLOUD_TEST_NEAR_CLARITY == 0
        float boundary = 1.0 - smoothstep(0.45, 0.90, sample.baseDensity);
        sample.erosion = sample.detailNoise * max(detailErosionStrength, 0.0) *
            boundary;
#if VCLOUD_TEST_DETAIL_CORE_MODE == 1
        // 코어에서 기존 감산량의 35%만 적용. core=0인 외곽은 기존식 그대로.
        sample.erosion *= lerp(1.0,0.35,DetailCoreForComparison(sample));
#elif VCLOUD_TEST_DETAIL_CORE_MODE == 2
        // 코어에서 shaping 전 Base의 최대35% 제거. 빈 영역에 밀도를 더하지 않는다.
        float erosionLimit=sample.baseDensity*lerp(1.0,0.35,DetailCoreForComparison(sample));
        sample.erosion=min(sample.erosion,erosionLimit);
#endif
        sample.finalDensity = saturate(sample.baseDensity - sample.erosion);
#else
        // 정규화 형상을 먼저 침식하고 높이/밀도 배율은 뒤에서 한 번 적용한다.
        float shape=saturate(sample.weatherThresholdDensity);
        float amplitude=(sample.localHeightFraction>=0 && sample.localHeightFraction<=1 ? 1.0 : 0.0) *
            smoothstep(.02,.20,sample.weatherCoverage) * EvaluateCommonVerticalProfile(sample.localHeightFraction) *
            max(densityMultiplier,0.0) * sample.weatherDensityModifier;
        float erosion=sample.detailNoise*max(detailErosionStrength,0.0)*(1-smoothstep(.45,.90,shape));
#if VCLOUD_TEST_DETAIL_CORE_MODE == 1
        // 비교 전용: 과거 몸체 보호 .65 가중치. 일반 슬라이더는 2026-09-24 제거했다.
        erosion*=1-0.65*DetailCoreForComparison(sample);
#endif
#if defined(VCLOUD_TEST_DETAIL_EROSION_SCALE)
        erosion*=VCLOUD_TEST_DETAIL_EROSION_SCALE; // 원본 주파수 시험의 평균 제거량 일치 전용.
#endif
#if defined(VCLOUD_TEST_DETAIL_BANDS) && VCLOUD_TEST_DETAIL_BANDS > 0
        // 시험 전용: 같은 네 대역/가중치로 연속 remap. 추가 texture fetch 없음.
        float q=max(detailErosionStrength,0.0)*(1-smoothstep(.45,.90,shape));
        float4 band=saturate(VCLOUD_TEST_DETAIL_BAND_SCALE*q*max(detailVolumeWeights,0)*sample.detailNoiseChannels);
        float4 remain=1-band;
        erosion=1-remain.x*remain.y*remain.z*remain.w;
#endif
        float carved=max(shape-erosion,0.0);
#if VCLOUD_TEST_NEAR_CLARITY == 2
        carved=erosion>=1 ? 0 : carved/max(1-erosion,1e-6);
#endif
        sample.finalDensity=saturate(amplitude*saturate(carved));
        sample.erosion=erosion; // 정규화 형상에서 깎을 문턱. 밀도 단위 감산량이 아니다.
#endif
    }
    // 최종 밀도와 Base 태양 차폐에 동일한 기존 shaping을 적용한다.
    sample.baseDensity = ShapeCloudDensity(sample.baseDensity);
    sample.finalDensity = ShapeCloudDensity(sample.finalDensity);
    return sample;
}

CloudDensitySample SampleCloudDensity(float3 worldPosition, float timeSeconds,
                                      bool sampleDetail)
{
    return SampleCloudDensity(worldPosition, timeSeconds, sampleDetail, 0.0);
}


// 두 인자 호출은 모든 거리에서 Detail Texture3D를 사용하는 High 경로다.
CloudDensitySample SampleCloudDensity(float3 worldPosition, float timeSeconds)
{
    return SampleCloudDensity(worldPosition, timeSeconds, true);
}

#endif
