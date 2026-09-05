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
    float effectiveShapeCoverage;// global R × Weather R × shape profile.
    float baseSupport;       // Detail/밀도 배율 전 Base shape가 존재하면 1.
    float weatherCoverage;   // Weather Map R 채널(0~1).
    float cloudType;         // Weather Map G 채널(0~1).
    float storedRegionalType;// 선택 정책 적용 전 Weather Map G 원본.
    float weatherDensityModifier; // Weather B를 0.5~1.5로 바꾼 배율.
    float weatherThicknessPotential;// Weather A 로컬 두께 보간값(0~1).
    float baseDensity;       // 단계 3까지의 큰 구름 형태(0~1).
    float detailNoise;       // 실제 샘플한 고주파 침식 noise(0~1).
    float erosion;           // detailNoise × detailErosionStrength.
    float finalDensity;      // saturate(baseDensity - erosion), 적분 입력.
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
    float value;
    float3 uvw;
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

float RemapCoverage(float rawNoise, float coverageValue)
{
    float safeCoverage = saturate(coverageValue);
    float validCoverage = safeCoverage > 1e-4 ? 1.0 : 0.0;
    float safeDivisor = max(safeCoverage, 1e-4);
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
    sample.storedRegionalType = weather.storedRegionalType;
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
        sample.localHeightFraction, mixedBottomFadeEnd, mixedTopFadeStart);
    {
        float typedVerticalProfile = EvaluatePhysicalTypedVerticalProfile(
            sample.localHeightFraction, sample.cloudType);
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
CloudDensitySample EvaluateBaseCloudDensity(
    float3 worldPosition, float timeSeconds)
{
    CloudDensitySample result = (CloudDensitySample)0;
    WeatherSample weather = SampleWeatherMap(worldPosition, timeSeconds);
    PhysicalColumnGeometry geometry = EvaluatePhysicalColumnGeometry(
        worldPosition.y, weather);
    float localThickness = geometry.localThicknessMeters;
    float localHeight = geometry.localHeightFraction;
    float verticalProfile = EvaluatePhysicalTypedVerticalProfile(
        localHeight, weather.cloudType);
    float weatherSupport = smoothstep(0.02, 0.20, weather.coverage);
    bool definitelyEmpty = localHeight < 0.0 || localHeight > 1.0 ||
        verticalProfile <= 0.0 || weatherSupport <= 0.0 ||
        coverage <= 0.0 || densityMultiplier <= 0.0;
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
        result.storedRegionalType = weather.storedRegionalType;
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
        float typedVerticalProfile = EvaluatePhysicalTypedVerticalProfile(
            localHeightFraction, weather.cloudType);
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
    return lightDensity;
}

// Base Shape 뒤에 선택적으로 Detail Erosion을 적용한다.
// sampleDetail=false, 빈 Base, strength=0 경로는 Detail 함수 자체를 호출하지 않는다.
// 이 조기 반환은 단계 4의 기능 요구이며 단계 9의 레이 스텝 최적화와는 별개다.
CloudDensitySample SampleCloudDensity(float3 worldPosition, float timeSeconds,
                                      bool sampleDetail)
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
    }
    if (shouldApplyDetail)
    {
        float boundary = 1.0 - smoothstep(0.45, 0.90, sample.baseDensity);
        sample.erosion = sample.detailNoise * max(detailErosionStrength, 0.0) *
            boundary;
        sample.finalDensity = saturate(sample.baseDensity - sample.erosion);
    }
    return sample;
}


// 두 인자 호출은 모든 거리에서 Detail Texture3D를 사용하는 High 경로다.
CloudDensitySample SampleCloudDensity(float3 worldPosition, float timeSeconds)
{
    return SampleCloudDensity(worldPosition, timeSeconds, true);
}

#endif
