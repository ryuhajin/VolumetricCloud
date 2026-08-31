// ============================================================================
//  Weather.hlsli - 단계 5 월드 XZ Weather Map과 구름 종류 높이 프로파일
// ----------------------------------------------------------------------------
//  1. 월드 XZ(m)를 반복 가능한 0~1 Weather UV로 바꾼다.
//  2. CPU가 만든 RGBA8 Texture2D의 R/G/B/A 채널을 선형 샘플링한다.
//  3. R은 coverage, G는 cloud type, B는 0.5~1.5 밀도 배율, A는 로컬 두께다.
//  4. Open World는 A/G를 1~6km 물리 두께와 타입별 세로 프로파일로 바꾼다.
//
//  t2는 Renderer가 만든 256² DXGI_FORMAT_R8G8B8A8_UNORM Weather Map이고,
//  s1은 반복 경계를 부드럽게 잇는 linear-wrap sampler다. A는 로컬 두께 채널이다.
// ============================================================================
#ifndef VCLOUD_WEATHER_HLSLI
#define VCLOUD_WEATHER_HLSLI

#include "CloudParameters.hlsli"
#include "CloudShapeParameters.hlsli"
#include "CloudAdvection.hlsli"

Texture2D<float4> weatherMapTexture : register(t2);
SamplerState weatherMapSampler : register(s1);

struct WeatherSample
{
    float coverage;          // R: 이 위치에 구름이 생길 수 있는 비율(0~1).
    float cloudType;         // G: 0 층운, 0.5 기존 혼합형, 1 적운.
    float densityModifier;   // B를 0.5~1.5로 바꾼 최종 밀도 배율.
    float localThicknessPotential;// A: 타입별 물리 두께 범위 안의 보간값(0~1).
    float2 uv;               // 실제 조회한 반복 Weather UV(0~1).
};

// Weather A/G와 전역 도메인으로부터 한 번 계산한 로컬 컬럼 계약이다.
// View, support precheck, Light Ray와 Deep Shadow가 이 값을 공유해야 같은
// 위치에서 구름이 시작하고 끝난다.
struct PhysicalColumnGeometry
{
    float localThicknessMeters;
    float localBaseLiftMeters;
    float localBottomMeters;
    float localTopMeters;
    float localHeightFraction;
};

// 8-bit UNORM에서 0.5는 정확히 저장되지 않고 128/255가 된다. 디버그 맵의
// 0/0.5/1 기준값을 다시 정확히 복원해 F2의 Type 0.5와 B 중립 1.0을 보장한다.
float DecodeCanonicalWeatherChannel(float value)
{
    float result = saturate(value);
    if (abs(value - 0.5) <= (1.0 / 255.0 + 1e-6))
        result = 0.5;
    else if (value <= (0.5 / 255.0))
        result = 0.0;
    else if (value >= (254.5 / 255.0))
        result = 1.0;
    return result;
}

#ifndef VCLOUD_CIRRUS_VARIANT
#define VCLOUD_CIRRUS_VARIANT -1
#endif

bool IsCirrusCloudShape()
{
#if VCLOUD_CIRRUS_VARIANT == 1
    return true;
#elif VCLOUD_CIRRUS_VARIANT == 0
    return false;
#else
    return cloudShapeMode == kCloudShapeCirrusPhysicalLayer;
#endif
}

bool UsesPhysicalCloudShape()
{
#if VCLOUD_CIRRUS_VARIANT == 1
    return true;
#elif VCLOUD_CIRRUS_VARIANT == 0
    return cloudShapeMode == kCloudShapeWeatherPhysicalThickness;
#else
    return cloudShapeMode == kCloudShapeWeatherPhysicalThickness ||
           IsCirrusCloudShape();
#endif
}

void ComputeCirrusBasis(out float2 alongDirection, out float2 acrossDirection)
{
#if VCLOUD_CIRRUS_VARIANT == 0
    alongDirection = float2(0.9396926, 0.3420201);
#else
    float flowLength = length(cirrusFlowDirectionXZ);
    alongDirection = flowLength > 1e-6
        ? cirrusFlowDirectionXZ / flowLength
        : float2(0.9396926, 0.3420201);
#endif
    acrossDirection = float2(-alongDirection.y, alongDirection.x);
}

float ComputeCirrusWeatherAspect()
{
#if VCLOUD_CIRRUS_VARIANT == 0
    return 1.0;
#else
    return max(cirrusBaseAcrossScaleMeters /
               max(cirrusBaseAlongScaleMeters, 1.0), 1e-4);
#endif
}

// 기존 3D noise와 같은 양의 바람 방향으로 지도가 이동하도록 현재 월드 위치에서
// wind*time을 뺀다. XZ 바람이 0이면 나누지 않고 정지한다. world size가 0 또는
// 음수여도 1e-4m로 보정해 NaN/Inf가 화면 전체로 번지는 것을 막는다.
float2 ComputeWeatherUv(float3 worldPosition, float timeSeconds)
{
    float2 result = float2(0.0, 0.0);
    if (UsesPhysicalCloudShape())
    {
        float3 stationaryWorld = ComputePhysicalCloudSamplePosition(
            worldPosition, timeSeconds);
        if (IsCirrusCloudShape())
        {
            float2 alongDirection;
            float2 acrossDirection;
            ComputeCirrusBasis(alongDirection, acrossDirection);
            float along = dot(stationaryWorld.xz, alongDirection);
            float across = dot(stationaryWorld.xz, acrossDirection);
            float aspect = ComputeCirrusWeatherAspect();
            result = frac(float2(
                along / max(weatherMapWorldSize, 1e-4),
                across / max(weatherMapWorldSize * aspect, 1e-4)) +
                weatherMapOffset);
        }
        else
        {
            result = frac(stationaryWorld.xz /
                max(weatherMapWorldSize, 1e-4) + weatherMapOffset);
        }
    }
    else
    {
        float2 windXZ = windDirection.xz;
        float windLength = length(windXZ);
        float2 safeWind = windLength > 1e-6 ? windXZ / windLength : 0.0.xx;
        float safeWorldSize = max(weatherMapWorldSize, 1e-4);
        float safeTime = max(timeSeconds, 0.0);
        float2 stationaryWorld = worldPosition.xz -
            safeWind * max(weatherMapWindSpeed, 0.0) * safeTime;
        result = frac(stationaryWorld / safeWorldSize + weatherMapOffset);
    }
    return result;
}

WeatherSample SampleWeatherMap(float3 worldPosition, float timeSeconds)
{
    WeatherSample result = (WeatherSample)0;
    result.uv = ComputeWeatherUv(worldPosition, timeSeconds);
    float4 channels = saturate(
        weatherMapTexture.SampleLevel(weatherMapSampler, result.uv, 0.0));
    result.coverage = channels.r;
    result.cloudType = DecodeCanonicalWeatherChannel(channels.g);
    result.densityModifier = 0.5 + DecodeCanonicalWeatherChannel(channels.b);
    result.localThicknessPotential = DecodeCanonicalWeatherChannel(channels.a);
    return result;
}

float EvaluateLocalTopFraction(float localHeightPotential, float cloudType)
{
    float minimumThickness = clamp(minimumLocalThicknessFraction, 0.10, 1.0);
    float rawTop = max(minimumThickness, saturate(localHeightPotential));
    float cumulusAmount = smoothstep(0.5, 1.0, saturate(cloudType));
    float typedTop = lerp(rawTop, 1.0,
        saturate(cumulusTopBoost) * cumulusAmount);
    return lerp(1.0, typedTop, saturate(localHeightVariation));
}

float EvaluateLocalHeightFraction(float globalHeightFraction,
                                  float localTopFraction)
{
    return saturate(globalHeightFraction) / max(saturate(localTopFraction), 1e-4);
}

float2 EvaluatePhysicalThicknessRange(float cloudType)
{
    float type = saturate(cloudType);
    return float2(
        lerp(stratusMinimumThicknessMeters,
             cumulusMinimumThicknessMeters, type),
        lerp(stratusMaximumThicknessMeters,
             cumulusMaximumThicknessMeters, type));
}

float EvaluatePhysicalLocalThickness(float thicknessPotential, float cloudType)
{
    float2 rangeMeters = EvaluatePhysicalThicknessRange(cloudType);
    return lerp(rangeMeters.x, rangeMeters.y, saturate(thicknessPotential));
}

float EvaluatePhysicalLocalHeight(float worldY, float localThicknessMeters)
{
    return (worldY - cloudBoundsMin.y) / max(localThicknessMeters, 1.0);
}

float EvaluatePhysicalLocalBaseLift(float thicknessPotential,
                                    float localThicknessMeters,
                                    float typeScale)
{
    float weakColumn = 1.0 - saturate(thicknessPotential);
    float desiredLift = max(localBaseLiftMaxMeters, 0.0) *
        saturate(typeScale) * pow(weakColumn, 1.5);
    return min(desiredLift, max(localThicknessMeters, 0.0) * 0.25);
}

float EvaluateCirrusLocalThickness(float thicknessPotential)
{
#if VCLOUD_CIRRUS_VARIANT == 0
    return 1.0;
#else
    return lerp(cirrusMinimumThicknessMeters,
                cirrusMaximumThicknessMeters,
                saturate(thicknessPotential));
#endif
}

float EvaluateCirrusLocalBottom(float localThicknessMeters)
{
#if VCLOUD_CIRRUS_VARIANT == 0
    return cloudBoundsMin.y;
#else
    float layerThickness = max(cloudBoundsMax.y - cloudBoundsMin.y, 1.0);
    float center = cloudBoundsMin.y +
        layerThickness * saturate(cirrusVerticalProfileCenter);
    return center - localThicknessMeters * 0.5;
#endif
}

float EvaluateCirrusLocalHeight(float worldY, float localThicknessMeters)
{
    return (worldY - EvaluateCirrusLocalBottom(localThicknessMeters)) /
        max(localThicknessMeters, 1.0);
}

PhysicalColumnGeometry EvaluatePhysicalColumnGeometry(
    float worldY, WeatherSample weather)
{
    PhysicalColumnGeometry result = (PhysicalColumnGeometry)0;
    if (IsCirrusCloudShape())
    {
        result.localThicknessMeters = EvaluateCirrusLocalThickness(
            weather.localThicknessPotential);
        result.localBottomMeters = EvaluateCirrusLocalBottom(
            result.localThicknessMeters);
        result.localTopMeters = result.localBottomMeters +
            result.localThicknessMeters;
        // Cirrus는 전역 바닥 기준 lift가 아니라 중심형 얇은 층을 사용한다.
        result.localBaseLiftMeters = 0.0;
    }
    else
    {
        result.localThicknessMeters = EvaluatePhysicalLocalThickness(
            weather.localThicknessPotential, weather.cloudType);
        float typeScale = lerp(0.15, 1.0, saturate(weather.cloudType));
        result.localBaseLiftMeters = EvaluatePhysicalLocalBaseLift(
            weather.localThicknessPotential, result.localThicknessMeters,
            typeScale);
        result.localBottomMeters = cloudBoundsMin.y +
            result.localBaseLiftMeters;
        result.localTopMeters = result.localBottomMeters +
            result.localThicknessMeters;
    }
    result.localHeightFraction = (worldY - result.localBottomMeters) /
        max(result.localThicknessMeters, 1.0);
    return result;
}

float EvaluateFootprintCoverageFactor(float typedFootprintScale)
{
    float influence = saturate(footprintCoverageInfluence);
    return lerp(1.0 - influence, 1.0,
                saturate(typedFootprintScale));
}

float EvaluateCirrusVerticalProfile(float localHeightFraction)
{
#if VCLOUD_CIRRUS_VARIANT == 0
    return 0.0;
#else
    if (localHeightFraction < 0.0 || localHeightFraction > 1.0)
        return 0.0;
    float centeredDistance = abs(localHeightFraction * 2.0 - 1.0);
    return 1.0 - smoothstep(
        saturate(cirrusVerticalProfileHalfWidth), 1.0, centeredDistance);
#endif
}

float EvaluateProfileEnvelope(float heightFraction, float bottomFadeEndValue,
                              float topFadeStartValue)
{
    float h = saturate(heightFraction);
    return saturate(smoothstep(0.0, bottomFadeEndValue, h) *
        (1.0 - smoothstep(topFadeStartValue, 1.0, h)));
}

float EvaluatePhysicalTypedVerticalProfile(float heightFraction, float cloudType)
{
    float h = saturate(heightFraction);
    float type = saturate(cloudType);
    float stratus = EvaluateProfileEnvelope(
        h, stratusBottomFadeEnd, stratusTopFadeStart);
    float mixed = EvaluateProfileEnvelope(
        h, mixedBottomFadeEnd, mixedTopFadeStart);
    float cumulusEnvelope = EvaluateProfileEnvelope(
        h, cumulusBottomFadeEnd, cumulusTopFadeStart);
    float upperMass = lerp(cumulusUpperMassBottom, 1.0,
        smoothstep(cumulusUpperMassStart, cumulusUpperMassEnd, h));
    float cumulus = saturate(cumulusEnvelope * upperMass);
    return type <= 0.5
        ? lerp(stratus, mixed, type * 2.0)
        : lerp(mixed, cumulus, (type - 0.5) * 2.0);
}

// 각 타입의 바닥→최대 폭→상단 cutoff를 전체 높이에 걸쳐 연결한다.
// 기존 footprint처럼 중간 구간을 같은 값으로 유지하지 않으므로 세로 옆면이
// 긴 직선으로 남지 않는다. cutoff가 작을수록 그 높이에서 더 넓은 XZ 영역을 허용한다.
float EvaluateContinuousFootprintCutoff(float heightFraction,
                                        float bottomCutoff,
                                        float middleCutoff,
                                        float topCutoff,
                                        float peakHeight)
{
    float h = saturate(heightFraction);
    float safePeak = clamp(peakHeight, 0.01, 0.99);
    return h <= safePeak
        ? lerp(bottomCutoff, middleCutoff, smoothstep(0.0, safePeak, h))
        : lerp(middleCutoff, topCutoff, smoothstep(safePeak, 1.0, h));
}

float EvaluateFootprintScale(float cutoff, float middleCutoff)
{
    return saturate((1.0 - cutoff) / max(1.0 - middleCutoff, 1e-4));
}

float EvaluatePhysicalTypedFootprintScale(float heightFraction, float cloudType)
{
    float h = saturate(heightFraction);
    float type = saturate(cloudType);
    float stratus = EvaluateFootprintScale(
        EvaluateContinuousFootprintCutoff(h, 0.16, 0.08, 0.22, 0.45), 0.08);
    float mixed = EvaluateFootprintScale(
        EvaluateContinuousFootprintCutoff(h, 0.22, 0.04, 0.38, 0.50), 0.04);
    float cumulus = EvaluateFootprintScale(
        EvaluateContinuousFootprintCutoff(h, 0.32, 0.03, 0.62, 0.58), 0.03);
    return type <= 0.5
        ? lerp(stratus, mixed, type * 2.0)
        : lerp(mixed, cumulus, (type - 0.5) * 2.0);
}

// 디버그용 결합 profile이다. 실제 density에서는 vertical은 최종 Base 밀도에
// 한 번, footprint는 footprintCoverageInfluence만큼 horizontal threshold에 반영한다.
float EvaluatePhysicalTypedShapeProfile(float heightFraction, float cloudType)
{
    return saturate(
        EvaluatePhysicalTypedVerticalProfile(heightFraction, cloudType) *
        EvaluatePhysicalTypedFootprintScale(heightFraction, cloudType));
}

// 낮고 평평한 층운: 바닥에서 빠르게 생기고 구름층 중간 전에 사라진다.
float EvaluateStratusProfile(float heightFraction)
{
    float h = saturate(heightFraction);
    return saturate(smoothstep(0.0, 0.08, h) *
        (1.0 - smoothstep(0.38, 0.55, h)));
}

// 위로 발달한 적운: 단계 3보다 상단 fade 시작을 늦추고, 낮은 부분보다 높은
// 부분의 상대 밀도를 키운다. bottom/top UI는 적운에도 안전한 범위로 반영한다.
float EvaluateCumulusProfile(float heightFraction)
{
    float h = saturate(heightFraction);
    float safeBottom = max(clamp(bottomFadeEnd, 0.01, 0.99), 0.12);
    float safeTop = max(clamp(topFadeStart, 0.01, 0.99), 0.88);
    float envelope = smoothstep(0.0, safeBottom, h) *
        (1.0 - smoothstep(safeTop, 1.0, h));
    float upperMass = lerp(0.55, 1.0, smoothstep(0.10, 0.65, h));
    return saturate(envelope * upperMass);
}

// cloudType=0.5에서 mixedProfile을 그대로 반환하는 구간별 보간이다.
// 따라서 F2 Uniform Legacy의 G=0.5는 승인된 단계 3 높이 결과와 정확히 같다.
float EvaluateTypedHeightProfile(float heightFraction, float cloudType,
                                 float mixedProfile)
{
    float type = saturate(cloudType);
    float stratus = EvaluateStratusProfile(heightFraction);
    float cumulus = EvaluateCumulusProfile(heightFraction);
    return type <= 0.5
        ? lerp(stratus, mixedProfile, type * 2.0)
        : lerp(mixedProfile, cumulus, (type - 0.5) * 2.0);
}

float EvaluateMixedFootprintCutoff(float heightFraction)
{
    float h = saturate(heightFraction);
    float lower = lerp(0.22, 0.04, smoothstep(0.0, 0.35, h));
    return lerp(lower, 0.38, smoothstep(0.65, 1.0, h));
}

float EvaluateCumulusFootprintCutoff(float heightFraction)
{
    float h = saturate(heightFraction);
    float lower = lerp(0.32, 0.03, smoothstep(0.0, 0.38, h));
    return lerp(lower, 0.62, smoothstep(0.62, 1.0, h));
}

float EvaluateTypedFootprintCutoff(float heightFraction, float cloudType)
{
    float type = saturate(cloudType);
    float stratus = 0.10;
    float mixed = EvaluateMixedFootprintCutoff(heightFraction);
    float cumulus = EvaluateCumulusFootprintCutoff(heightFraction);
    return type <= 0.5
        ? lerp(stratus, mixed, type * 2.0)
        : lerp(mixed, cumulus, (type - 0.5) * 2.0);
}

// R의 부드러운 가장자리를 높이와 종류에 따라 안쪽으로 밀어 적운·혼합형의
// 상하 수평 폭을 줄인다. R=1은 정확히 1이라 Uniform Legacy가 변하지 않는다.
float EvaluateTypedWeatherCoverage(float weatherCoverage,
                                   float heightFraction, float cloudType)
{
    float cutoff = EvaluateTypedFootprintCutoff(heightFraction, cloudType);
    return saturate((saturate(weatherCoverage) - cutoff) /
        max(1.0 - cutoff, 1e-4));
}

#endif
