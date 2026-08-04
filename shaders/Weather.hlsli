// ============================================================================
//  Weather.hlsli - 단계 5 월드 XZ Weather Map과 구름 종류 높이 프로파일
// ----------------------------------------------------------------------------
//  1. 월드 XZ(m)를 반복 가능한 0~1 Weather UV로 바꾼다.
//  2. CPU가 만든 RGBA8 Texture2D의 R/G/B/A 채널을 선형 샘플링한다.
//  3. R은 coverage, G는 cloud type, B는 0.5~1.5 밀도 배율로 해석한다.
//  4. G=0/0.5/1을 층운/단계 3 혼합형/적운 프로파일로 보간한다.
//
//  t2는 Renderer가 만든 256² DXGI_FORMAT_R8G8B8A8_UNORM Weather Map이고,
//  s1은 반복 경계를 부드럽게 잇는 linear-wrap sampler다. A는 예약 채널이다.
// ============================================================================
#ifndef VCLOUD_WEATHER_HLSLI
#define VCLOUD_WEATHER_HLSLI

#include "CloudParameters.hlsli"

Texture2D<float4> weatherMapTexture : register(t2);
SamplerState weatherMapSampler : register(s1);

struct WeatherSample
{
    float coverage;          // R: 이 위치에 구름이 생길 수 있는 비율(0~1).
    float cloudType;         // G: 0 층운, 0.5 기존 혼합형, 1 적운.
    float densityModifier;   // B를 0.5~1.5로 바꾼 최종 밀도 배율.
    float reserved;          // A: 이후 precipitation 등을 위한 예약값.
    float2 uv;               // 실제 조회한 반복 Weather UV(0~1).
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
    float2 windXZ = windDirection.xz;
    float windLength = length(windXZ);
    float2 safeWind = windLength > 1e-6 ? windXZ / windLength : 0.0.xx;
    float safeWorldSize = max(weatherMapWorldSize, 1e-4);
    float safeTime = max(timeSeconds, 0.0);
    float2 stationaryWorld = worldPosition.xz -
        safeWind * max(weatherMapWindSpeed, 0.0) * safeTime;
    return frac(stationaryWorld / safeWorldSize + weatherMapOffset);
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
    result.reserved = channels.a;
    return result;
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
