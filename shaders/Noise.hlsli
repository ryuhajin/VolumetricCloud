// ============================================================================
//  Noise.hlsli - 구름 렌더와 Noise Lab이 함께 사용하는 단계 5 밀도 함수
// ----------------------------------------------------------------------------
//  데이터 흐름
//  1. 월드 위치(m)를 바람이 이동시킨 noise 좌표로 바꾼다.
//  2. value noise와 coverage로 단계 2의 기본 덩어리 밀도를 만든다.
//  3. AABB 바닥/천장 사이의 높이 비율과 부드러운 높이 마스크를 계산한다.
//  4. Weather R/G/B로 threshold, 구름 종류 높이와 밀도 배율을 조절한다.
//  5. Weather가 적용된 큰 구름 형태를 Base Density로 확정한다.
//  6. Base가 존재할 때만 별도 고주파 Detail Noise를 샘플링해 밀도를 깎는다.
//
//  이번 단계는 단일 Value Noise만 사용한다. 이후 fBm/Worley는
//  SampleDetailErosionNoise 내부만 교체하고 레이마칭 인터페이스는 유지한다.
//  단계 6 Light는 아직 적용하지 않는다.
// ============================================================================
#ifndef VCLOUD_NOISE_HLSLI
#define VCLOUD_NOISE_HLSLI

#include "CloudParameters.hlsli"
#include "NoiseVolumeParameters.hlsli"
#include "CloudLodParameters.hlsli"
#include "OptimizationParameters.hlsli"
#include "CloudAdvection.hlsli"

Texture3D<float4> baseNoiseVolumeTexture : register(t3);
Texture3D<float4> detailNoiseVolumeTexture : register(t4);

#ifndef VCLOUD_NOISE_TEST_BIAS
#define VCLOUD_NOISE_TEST_BIAS 0.0
#endif

struct CloudDensitySample
{
    float rawNoise;          // threshold 전 원본 value noise(0~1).
    float thresholdDensity; // coverage만 적용한 단계 2 기본 밀도(0~1).
    float weatherThresholdDensity; // Weather R까지 적용한 threshold 밀도.
    float heightFraction;    // AABB 바닥=0, 천장=1인 정규화 월드 Y 높이.
    float localThicknessMeters;// Weather A/G가 정한 이 XZ 기둥의 물리 두께(m).
    float localBaseLiftMeters;// Weather A가 전역 바닥에서 올린 로컬 바닥(m).
    float localHeightFraction;// 로컬 바닥=0, 로컬 상단=1인 정규화 높이.
    float heightProfile;     // 위·아래 경계를 부드럽게 지우는 마스크(0~1).
    float typedShapeProfile; // 높이/Type이 정한 shape threshold 마스크(0~1).
    float effectiveShapeCoverage;// global R × Weather R × shape profile.
    float baseSupport;       // Detail/밀도 배율 전 Base shape가 존재하면 1.
    float weatherCoverage;   // Weather Map R 채널(0~1).
    float cloudType;         // Weather Map G 채널(0~1).
    float weatherDensityModifier; // Weather B를 0.5~1.5로 바꾼 배율.
    float weatherThicknessPotential;// Weather A 로컬 두께 보간값(0~1).
    float baseDensity;       // 단계 3까지의 큰 구름 형태(0~1).
    float detailNoise;       // 실제 샘플한 고주파 침식 noise(0~1).
    float erosion;           // detailNoise × detailErosionStrength.
    float finalDensity;      // saturate(baseDensity - erosion), 적분 입력.
    float detailSampled;     // Detail 함수를 호출했으면 1, 생략했으면 0.
    float detailLodFactor;   // 1=원본 Detail, 0=실제 volume 평균만 사용.
    float3 noiseUvw;         // Base value noise의 연속 좌표(cycle).
    float3 detailNoiseUvw;   // Detail value noise의 연속 좌표(cycle), 생략 시 0.
    float2 weatherUv;        // 반복되는 2D Weather Map 조회 좌표(0~1).
    float4 baseNoiseChannels;// Texture3D Base RGBA 또는 legacy value 복제.
    float4 detailNoiseChannels;// Texture3D Detail RGBA 또는 legacy value 복제.
    float supportPrecheckSkipped;// Weather/높이만으로 Base fetch를 생략하면 1.
};

// 서로 다른 노이즈 알고리즘도 동일한 값+좌표 인터페이스로 연결하기 위한 표본이다.
// 단계 4는 value만 사용하지만 이후 fBm/Worley도 이 구조를 반환하게 한다.
struct NoiseFieldSample
{
    float value;
    float3 uvw;
    float4 channels;
};

// 월드 Y 위치(m)를 구름층 안의 0~1 높이로 바꾼다.
// cloudBoundsMax.y <= cloudBoundsMin.y인 잘못된 AABB는 두께가 없으므로 0을 반환한다.
// 이 분기는 0 나눗셈과 NaN이 검정 화면이나 번쩍임으로 번지는 것을 막는다.
float EvaluateHeightFraction(float worldY)
{
    float cloudThickness = cloudBoundsMax.y - cloudBoundsMin.y;
    float validThickness = cloudThickness > 1e-6 ? 1.0 : 0.0;
    float safeThickness = max(cloudThickness, 1e-6);
    return saturate((worldY - cloudBoundsMin.y) / safeThickness) * validThickness;
}

// 정규화 높이에서 바닥 fade와 꼭대기 fade를 곱해 구름층 마스크를 만든다.
// bottomFadeEnd가 커지면 바닥의 흐린 구간이 넓어지고, topFadeStart가 작아지면
// 꼭대기의 흐린 구간이 넓어진다. 두 값이 교차해도 곱은 유효하지만 중앙의
// 완전한 밀도(plateau)가 사라진다. 0/1 경계는 smoothstep의 동일 edge를 피한다.
float EvaluateHeightProfileFromFraction(float heightFraction)
{
    float safeBottomEnd = clamp(bottomFadeEnd, 0.01, 0.99);
    float safeTopStart = clamp(topFadeStart, 0.01, 0.99);
    float bottomFade = smoothstep(0.0, safeBottomEnd, saturate(heightFraction));
    float topFade = 1.0 - smoothstep(safeTopStart, 1.0, saturate(heightFraction));
    return saturate(bottomFade * topFade);
}

// 높이 기본 프로파일을 정의한 뒤 Weather가 이를 cloud type=0.5 기준으로 사용한다.
#include "Weather.hlsli"

float RemapCoverage(float rawNoise, float coverageValue)
{
    float safeCoverage = saturate(coverageValue);
    float validCoverage = safeCoverage > 1e-4 ? 1.0 : 0.0;
    float safeDivisor = max(safeCoverage, 1e-4);
    return saturate((rawNoise - (1.0 - safeCoverage)) / safeDivisor) *
        validCoverage;
}

// 정수 격자 모서리를 재현 가능한 0~1 난수로 바꾼다.
// 이 함수나 아래 보간식을 저장하면 Cloud PS와 Noise Lab PS가 함께 핫리로드된다.
float HashNoiseCorner(float3 latticePoint)
{
    float3 scrambled = frac(latticePoint * 0.1031);
    scrambled += dot(scrambled, scrambled.yzx + 33.33);
    return frac((scrambled.x + scrambled.y) * scrambled.z);
}

// 셀 여덟 모서리의 hash 값을 Hermite 곡선과 삼선형 보간으로 연결한다.
float SampleValueNoise3D(float3 noiseUvw)
{
    float3 cell = floor(noiseUvw);
    float3 local = frac(noiseUvw);
    float3 smoothLocal = local * local * (3.0 - 2.0 * local);

    float n000 = HashNoiseCorner(cell + float3(0.0, 0.0, 0.0));
    float n100 = HashNoiseCorner(cell + float3(1.0, 0.0, 0.0));
    float n010 = HashNoiseCorner(cell + float3(0.0, 1.0, 0.0));
    float n110 = HashNoiseCorner(cell + float3(1.0, 1.0, 0.0));
    float n001 = HashNoiseCorner(cell + float3(0.0, 0.0, 1.0));
    float n101 = HashNoiseCorner(cell + float3(1.0, 0.0, 1.0));
    float n011 = HashNoiseCorner(cell + float3(0.0, 1.0, 1.0));
    float n111 = HashNoiseCorner(cell + float3(1.0, 1.0, 1.0));

    float x00 = lerp(n000, n100, smoothLocal.x);
    float x10 = lerp(n010, n110, smoothLocal.x);
    float x01 = lerp(n001, n101, smoothLocal.x);
    float x11 = lerp(n011, n111, smoothLocal.x);
    float y0 = lerp(x00, x10, smoothLocal.y);
    float y1 = lerp(x01, x11, smoothLocal.y);
    return saturate(lerp(y0, y1, smoothLocal.z) + VCLOUD_NOISE_TEST_BIAS);
}

// 0 벡터 바람은 정규화하지 않아 NaN을 막는다. Base와 Detail이 같은 방향을
// 공유하되 각자의 속도로 이동하므로 표면이 큰 덩어리 위에서 천천히 미끄러질 수 있다.
float3 SafeWindDirection()
{
    float windLength = length(windDirection);
    return windLength > 1e-6 ? windDirection / windLength : 0.0.xxx;
}

float3 ComputeCirrusNoiseUvw(float3 stationaryWorld, bool detail)
{
#if VCLOUD_CIRRUS_VARIANT == 0
    return 0.0.xxx;
#else
    float2 alongDirection;
    float2 acrossDirection;
    ComputeCirrusBasis(alongDirection, acrossDirection);
    float along = dot(stationaryWorld.xz, alongDirection);
    float across = dot(stationaryWorld.xz, acrossDirection);
    float alongScale = detail ? cirrusDetailAlongScaleMeters
                              : cirrusBaseAlongScaleMeters;
    float acrossScale = detail ? cirrusDetailAcrossScaleMeters
                               : cirrusBaseAcrossScaleMeters;
    float verticalScale = detail ? cirrusDetailVerticalScaleMeters
                                 : cirrusBaseVerticalScaleMeters;
    return frac(float3(
        along / max(alongScale, 1.0),
        stationaryWorld.y / max(verticalScale, 1.0),
        across / max(acrossScale, 1.0)));
#endif
}

// 단계 13-4 Open World는 주기 Texture3D를, Similarity 회귀는 단계 2 value noise를 쓴다.
NoiseFieldSample SampleBaseShapeNoise(float3 worldPosition, float timeSeconds)
{
    NoiseFieldSample result = (NoiseFieldSample)0;
    bool physicalShape = UsesPhysicalCloudShape();
    float3 stationaryWorld = physicalShape
        ? ComputePhysicalCloudSamplePosition(worldPosition, timeSeconds)
        : worldPosition - SafeWindDirection() *
            max(windSpeed, 0.0) * max(timeSeconds, 0.0);
    if (noiseSource == kNoiseSourceTexture3D)
    {
        if (IsCirrusCloudShape())
        {
            result.uvw = frac(ComputeCirrusNoiseUvw(
                stationaryWorld, false) + noiseOffset.xxx);
        }
        else
        {
            result.uvw.xz = frac(stationaryWorld.xz /
                max(baseVolumeWorldSizeMeters, 1.0) + noiseOffset.xx);
            result.uvw.y = frac((stationaryWorld.y - cloudBoundsMin.y) /
                max(baseVolumeVerticalWorldSizeMeters, 1.0) + noiseOffset);
        }
        result.channels = baseNoiseVolumeTexture.SampleLevel(
            weatherMapSampler, result.uvw, 0);
        float worleyFbm = dot(result.channels.gba, baseVolumeWeights.xyz);
        float lowerBound = -(1.0 - worleyFbm);
        result.value = saturate(
            (result.channels.r - lowerBound) / max(1.0 - lowerBound, 1e-4));
    }
    else
    {
        result.uvw = IsCirrusCloudShape()
            ? ComputeCirrusNoiseUvw(stationaryWorld, false) + noiseOffset.xxx
            : stationaryWorld * max(baseNoiseScale, 1e-4) + noiseOffset.xxx;
        result.value = SampleValueNoise3D(result.uvw);
        result.channels = result.value.xxxx;
    }
    return result;
}

// 표면 침식 노이즈의 유일한 교체 지점이다. Texture3D는 네 Worley 대역을 결합하고,
// 회귀 경로만 고주파 단일 Value Noise를 유지한다.
NoiseFieldSample SampleDetailErosionNoise(float3 worldPosition, float timeSeconds)
{
    NoiseFieldSample result = (NoiseFieldSample)0;
    bool physicalShape = UsesPhysicalCloudShape();
    float3 stationaryWorld = physicalShape
        ? ComputePhysicalCloudSamplePosition(worldPosition, timeSeconds)
        : worldPosition - SafeWindDirection() *
            max(detailWindSpeed, 0.0) * max(timeSeconds, 0.0);
    if (noiseSource == kNoiseSourceTexture3D)
    {
        result.uvw = IsCirrusCloudShape()
            ? frac(ComputeCirrusNoiseUvw(stationaryWorld, true) +
                   detailNoiseOffset.xxx)
            : frac(stationaryWorld /
                   max(detailVolumeWorldSizeMeters, 1.0) +
                   detailNoiseOffset.xxx);
        result.channels = detailNoiseVolumeTexture.SampleLevel(
            weatherMapSampler, result.uvw, 0);
        result.value = saturate(dot(result.channels, detailVolumeWeights));
    }
    else
    {
        result.uvw = IsCirrusCloudShape()
            ? ComputeCirrusNoiseUvw(stationaryWorld, true) +
                detailNoiseOffset.xxx
            : stationaryWorld * max(detailNoiseScale, 1e-4) +
                detailNoiseOffset.xxx;
        result.value = SampleValueNoise3D(result.uvw);
        result.channels = result.value.xxxx;
    }
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
    sample.weatherDensityModifier = weather.densityModifier;
    sample.weatherThicknessPotential = weather.localThicknessPotential;
    sample.heightFraction = EvaluateHeightFraction(worldPosition.y);
    float localTopFraction = EvaluateLocalTopFraction(
        weather.localThicknessPotential, sample.cloudType);
    bool physicalShape = UsesPhysicalCloudShape();
    bool cirrusShape = IsCirrusCloudShape();
    if (physicalShape)
    {
        PhysicalColumnGeometry geometry = EvaluatePhysicalColumnGeometry(
            worldPosition.y, weather);
        sample.localThicknessMeters = geometry.localThicknessMeters;
        sample.localBaseLiftMeters = geometry.localBaseLiftMeters;
        sample.localHeightFraction = geometry.localHeightFraction;
    }
    else
    {
        sample.localThicknessMeters = localTopFraction *
            max(cloudBoundsMax.y - cloudBoundsMin.y, 1.0);
        sample.localBaseLiftMeters = 0.0;
        sample.localHeightFraction = EvaluateLocalHeightFraction(
            sample.heightFraction, localTopFraction);
    }
    float insideLocalColumn = sample.localHeightFraction >= 0.0 &&
        sample.localHeightFraction <= 1.0 ? 1.0 : 0.0;
    // 높이 마스크가 없으면 AABB 바닥과 천장이 칼로 자른 듯 보인다. 단계 3은
    // X/Z 덩어리 위치를 바꾸지 않고 Y 경계에서만 밀도를 0으로 부드럽게 줄인다.
    sample.heightProfile = cirrusShape
        ? EvaluateCirrusVerticalProfile(sample.localHeightFraction)
        : physicalShape
        ? EvaluateProfileEnvelope(sample.localHeightFraction,
            mixedBottomFadeEnd, mixedTopFadeStart)
        : EvaluateHeightProfileFromFraction(sample.localHeightFraction);
    if (cirrusShape)
    {
        float weatherSupport = smoothstep(0.02, 0.20, sample.weatherCoverage);
        float weatherFactor = lerp(0.65, 1.0, sample.weatherCoverage);
        sample.typedShapeProfile = sample.heightProfile;
        sample.effectiveShapeCoverage = saturate(coverage * weatherFactor);
        sample.weatherThresholdDensity = RemapCoverage(
            sample.rawNoise, sample.effectiveShapeCoverage);
        sample.baseDensity = insideLocalColumn * weatherSupport *
            sample.weatherThresholdDensity * sample.heightProfile *
            max(densityMultiplier, 0.0) * sample.weatherDensityModifier;
    }
    else if (physicalShape)
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
    else
    {
        // Similarity/구형 회귀는 Weather R 높이 remap 뒤 profile을 밀도에 곱하는
        // 기존 단계 5 경로를 정확히 유지한다.
        float shapedWeatherCoverage = EvaluateTypedWeatherCoverage(
            sample.weatherCoverage, sample.localHeightFraction, sample.cloudType);
        sample.effectiveShapeCoverage = saturate(coverage) * shapedWeatherCoverage;
        sample.weatherThresholdDensity = RemapCoverage(
            sample.rawNoise, sample.effectiveShapeCoverage);
        sample.typedShapeProfile = EvaluateTypedHeightProfile(
            sample.localHeightFraction, sample.cloudType, sample.heightProfile);
        sample.baseDensity = insideLocalColumn * saturate(
            sample.weatherThresholdDensity * sample.typedShapeProfile *
            max(densityMultiplier, 0.0) * sample.weatherDensityModifier);
    }
    sample.baseSupport = insideLocalColumn &&
        sample.weatherThresholdDensity > 0.0 ? 1.0 : 0.0;
    sample.finalDensity = sample.baseDensity;
    return sample;
}

// Reference는 승인된 Base -> Weather fetch 순서를 그대로 보존한다.
CloudDensitySample EvaluateBaseCloudDensity(float3 worldPosition, float timeSeconds)
{
    NoiseFieldSample baseNoise = SampleBaseShapeNoise(worldPosition, timeSeconds);
    WeatherSample weather = SampleWeatherMap(worldPosition, timeSeconds);
    return ComposeBaseCloudDensity(worldPosition, baseNoise, weather);
}

// 단계 9 View 전용 경로. Physical Shape의 Base 식에서 noise와 무관한 곱이
// 정확히 0인 경우만 반환하므로 승인 Reference에서 존재할 구름을 지우지 않는다.
CloudDensitySample EvaluateBaseCloudDensityOptimized(
    float3 worldPosition, float timeSeconds)
{
    CloudDensitySample result = (CloudDensitySample)0;
#if VCLOUD_CIRRUS_VARIANT == 0
    // non-Cirrus renderer는 Legacy(0)/WeatherPhysical(1)만 선택한다.
    // Stage 14와 같은 술어를 유지해야 FXC가 else를 Physical로 증명하고
    // 불필요한 shape 경로를 완전히 제거한다.
    bool useReference = supportPrecheckEnabled == 0u ||
        cloudShapeMode != kCloudShapeWeatherPhysicalThickness;
#elif VCLOUD_CIRRUS_VARIANT == 1
    bool useReference = supportPrecheckEnabled == 0u;
#else
    bool useReference = supportPrecheckEnabled == 0u ||
        cloudShapeMode == kCloudShapeLegacy;
#endif
    if (useReference)
    {
        result = EvaluateBaseCloudDensity(worldPosition, timeSeconds);
    }
    else
    {
        WeatherSample weather = SampleWeatherMap(worldPosition, timeSeconds);
        bool cirrusShape = IsCirrusCloudShape();
        PhysicalColumnGeometry geometry = EvaluatePhysicalColumnGeometry(
            worldPosition.y, weather);
        float localThickness = geometry.localThicknessMeters;
        float localHeight = geometry.localHeightFraction;
        float verticalProfile = cirrusShape
            ? EvaluateCirrusVerticalProfile(localHeight)
            : EvaluatePhysicalTypedVerticalProfile(localHeight, weather.cloudType);
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
            result.weatherDensityModifier = weather.densityModifier;
            result.weatherThicknessPotential = weather.localThicknessPotential;
            result.localThicknessMeters = localThickness;
            result.localBaseLiftMeters = geometry.localBaseLiftMeters;
            result.localHeightFraction = localHeight;
            result.typedShapeProfile = max(verticalProfile, 0.0);
            result.supportPrecheckSkipped = 1.0;
        }
    }

    return result;
}

// Light Ray는 최종 scalar Base Density만 필요하다. Physical Shape에서는 먼저
// Weather와 로컬 높이를 검사해 확실히 빈 표본이면 Base Texture3D fetch를 생략한다.
// 남은 표본은 EvaluateBaseCloudDensity와 같은 coverage/profile 식을 사용한다.
float EvaluateLightCloudDensity(float3 worldPosition, float timeSeconds)
{
    float lightDensity = 0.0;

#if VCLOUD_CIRRUS_VARIANT == 0
    // 위 optimized View 경로와 같은 이유로 Stage 14의 분할 조건을 보존한다.
    if (cloudShapeMode != kCloudShapeWeatherPhysicalThickness)
#elif VCLOUD_CIRRUS_VARIANT == 1
    if (false)
#else
    if (cloudShapeMode == kCloudShapeLegacy)
#endif
    {
        CloudDensitySample legacySample = (CloudDensitySample)0;
        legacySample = EvaluateBaseCloudDensity(worldPosition, timeSeconds);
        lightDensity = legacySample.baseDensity;
    }
    else
    {
        WeatherSample weather = (WeatherSample)0;
        weather = SampleWeatherMap(worldPosition, timeSeconds);
        bool cirrusShape = IsCirrusCloudShape();
        PhysicalColumnGeometry geometry = EvaluatePhysicalColumnGeometry(
            worldPosition.y, weather);
        float localHeightFraction = geometry.localHeightFraction;

        if (localHeightFraction >= 0.0 && localHeightFraction <= 1.0)
        {
            float typedVerticalProfile = cirrusShape
                ? EvaluateCirrusVerticalProfile(localHeightFraction)
                : EvaluatePhysicalTypedVerticalProfile(
                    localHeightFraction, weather.cloudType);
            float weatherSupport = smoothstep(0.02, 0.20, weather.coverage);

            if (typedVerticalProfile > 0.0 && weatherSupport > 0.0)
            {
                float typedFootprintScale = cirrusShape ? 1.0
                    : EvaluatePhysicalTypedFootprintScale(
                        localHeightFraction, weather.cloudType);
                float weatherFactor = cirrusShape
                    ? lerp(0.65, 1.00, weather.coverage)
                    : lerp(0.70, 1.00, weather.coverage);
                float footprintFactor = cirrusShape ? 1.0
                    : EvaluateFootprintCoverageFactor(typedFootprintScale);
                float effectiveShapeCoverage = saturate(coverage *
                    weatherFactor * footprintFactor);
                NoiseFieldSample baseNoise = (NoiseFieldSample)0;
                baseNoise = SampleBaseShapeNoise(worldPosition, timeSeconds);
                float thresholdDensity = RemapCoverage(
                    baseNoise.value, effectiveShapeCoverage);
                lightDensity = weatherSupport * thresholdDensity *
                    typedVerticalProfile * max(densityMultiplier, 0.0) *
                    weather.densityModifier;
            }
        }
    }

    return lightDensity;
}

// Base Shape 뒤에 선택적으로 Detail Erosion을 적용한다.
// sampleDetail=false, 빈 Base, strength=0 경로는 Detail 함수 자체를 호출하지 않는다.
// 이 조기 반환은 단계 4의 기능 요구이며 단계 9의 레이 스텝 최적화와는 별개다.
CloudDensitySample SampleCloudDensity(float3 worldPosition, float timeSeconds,
                                      bool sampleDetail,
                                      float viewDistanceMeters)
{
    CloudDensitySample sample = EvaluateBaseCloudDensity(worldPosition, timeSeconds);
    float lodFactor = EvaluateDetailLodFactor(viewDistanceMeters);
    sample.detailLodFactor = lodFactor;
    bool outsideLod = detailLodEnabled != 0u &&
        viewDistanceMeters >= max(detailLodEndMeters, detailLodStartMeters + 1.0);
    bool shouldApplyDetail = sampleDetail && sample.baseDensity > 0.0 &&
                             detailErosionStrength > 0.0;
    bool shouldSampleDetail = shouldApplyDetail && !outsideLod;
    if (shouldSampleDetail)
    {
        NoiseFieldSample detail = SampleDetailErosionNoise(worldPosition, timeSeconds);
        sample.detailNoiseUvw = detail.uvw;
        sample.detailNoiseChannels = detail.channels;
        sample.detailNoise = lerp(saturate(detailNeutralValue),
                                  detail.value, lodFactor);
        sample.detailSampled = 1.0;
    }
    else if (shouldApplyDetail)
    {
        // LOD 끝 밖에서는 Texture3D를 읽지 않지만 평균 침식량은 유지한다.
        sample.detailNoise = saturate(detailNeutralValue);
    }
    if (shouldApplyDetail)
    {
        float boundary = noiseSource == kNoiseSourceTexture3D
            ? 1.0 - smoothstep(0.45, 0.90, sample.baseDensity)
            : 1.0;
        sample.erosion = sample.detailNoise * max(detailErosionStrength, 0.0) *
            boundary;
        sample.finalDensity = saturate(sample.baseDensity - sample.erosion);
    }
    return sample;
}

CloudDensitySample SampleCloudDensityOptimized(
    float3 worldPosition, float timeSeconds, bool sampleDetail,
    float viewDistanceMeters)
{
    CloudDensitySample sample = EvaluateBaseCloudDensityOptimized(
        worldPosition, timeSeconds);
    float lodFactor = EvaluateDetailLodFactor(viewDistanceMeters);
    sample.detailLodFactor = lodFactor;
    bool outsideLod = detailLodEnabled != 0u &&
        viewDistanceMeters >= max(detailLodEndMeters, detailLodStartMeters + 1.0);
    bool shouldApplyDetail = sampleDetail && sample.baseDensity > 0.0 &&
                             detailErosionStrength > 0.0;
    if (shouldApplyDetail && !outsideLod)
    {
        NoiseFieldSample detail = SampleDetailErosionNoise(worldPosition, timeSeconds);
        sample.detailNoiseUvw = detail.uvw;
        sample.detailNoiseChannels = detail.channels;
        sample.detailNoise = lerp(saturate(detailNeutralValue),
                                  detail.value, lodFactor);
        sample.detailSampled = 1.0;
    }
    else if (shouldApplyDetail)
    {
        sample.detailNoise = saturate(detailNeutralValue);
    }
    if (shouldApplyDetail)
    {
        float boundary = noiseSource == kNoiseSourceTexture3D
            ? 1.0 - smoothstep(0.45, 0.90, sample.baseDensity) : 1.0;
        sample.erosion = sample.detailNoise * max(detailErosionStrength, 0.0) *
            boundary;
        sample.finalDensity = saturate(sample.baseDensity - sample.erosion);
    }
    return sample;
}

CloudDensitySample SampleCloudDensity(float3 worldPosition, float timeSeconds,
                                      bool sampleDetail)
{
    return SampleCloudDensity(worldPosition, timeSeconds, sampleDetail, 0.0);
}

// 기존 호출부와 이후 View Ray는 기본적으로 Detail을 사용하는 편의 overload다.
CloudDensitySample SampleCloudDensity(float3 worldPosition, float timeSeconds)
{
    return SampleCloudDensity(worldPosition, timeSeconds, true, 0.0);
}

#endif
