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
#include "WeatherColumnParameters.hlsli"
#include "CloudAdvection.hlsli"

Texture2D<float4> weatherMapTexture : register(t2);
SamplerState weatherMapSampler : register(s1);

struct WeatherSample
{
    float coverage;          // R: 이 위치에 구름이 생길 수 있는 비율(0~1).
    float cloudType;         // G: 0 층운, 0.5 기존 혼합형, 1 적운.
    float storedRegionalType;// texture G 원본. Fixed 모드에서도 보존/preview한다.
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

// 기존 3D noise와 같은 양의 바람 방향으로 지도가 이동하도록 현재 월드 위치에서
// wind*time을 뺀다. XZ 바람이 0이면 나누지 않고 정지한다. world size가 0 또는
// 음수여도 1e-4m로 보정해 NaN/Inf가 화면 전체로 번지는 것을 막는다.
float2 ComputeWeatherUv(float3 worldPosition, float timeSeconds)
{
    float3 stationaryWorld = ComputePhysicalCloudSamplePosition(
        worldPosition, timeSeconds);
    return frac(stationaryWorld.xz /
        max(weatherMapWorldSize, 1e-4) + weatherMapOffset);
}

WeatherSample SampleWeatherMap(float3 worldPosition, float timeSeconds)
{
    WeatherSample result = (WeatherSample)0;
    result.uv = ComputeWeatherUv(worldPosition, timeSeconds);
    float4 channels = saturate(
        weatherMapTexture.SampleLevel(weatherMapSampler, result.uv, 0.0));
    result.coverage = channels.r;
    // G에는 항상 지역별 원본을 저장하고 b10 선택 정책으로 유효 타입을 계산한다.
    result.storedRegionalType = DecodeCanonicalWeatherChannel(channels.g);
    result.cloudType = ResolveEffectiveCloudType(result.storedRegionalType);
    result.densityModifier = 0.5 + DecodeCanonicalWeatherChannel(channels.b);
    result.localThicknessPotential = DecodeCanonicalWeatherChannel(channels.a);
    return result;
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
    float desiredLift = max(maximumBaseLiftMeters, 0.0) *
        saturate(typeScale) * pow(weakColumn, 1.5);
    return min(desiredLift, max(localThicknessMeters, 0.0) * 0.25);
}

PhysicalColumnGeometry EvaluatePhysicalColumnGeometry(
    float worldY, WeatherSample weather)
{
    PhysicalColumnGeometry result = (PhysicalColumnGeometry)0;
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

#endif
