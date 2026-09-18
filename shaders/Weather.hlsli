// [학습 지도] GPU WeatherMap t2/s1 + b1/b7/b10 → RGBA 해석/물리 기둥 → Noise/Shadow. UV [0,1), 높이/두께 m.
// [수정 안내] [직접 조절]의 CPU/UI 원본을 수정한다. 강제 범위는 입력 계약이며 화질 보장이 아니다.
// 별도 권장 구간이 없는 값은 표시된 기본값을 비교 출발점으로 삼는다. b/t/u/s는 버퍼/읽기/쓰기/샘플러 슬롯.
// ============================================================================
//  Weather.hlsli - 단계 5 월드 XZ Weather Map과 구름 종류 높이 프로파일
// ----------------------------------------------------------------------------
//  1. 월드 XZ(m)를 반복 가능한 0~1 Weather UV로 바꾼다.
//  2. GPU WeatherMapCompute가 만든 RGBA8의 R/G/B/A를 선형 샘플링한다.
//  3. R은 coverage, G는 cloud type, B는 0.5~1.5 밀도 배율, A는 로컬 두께다.
//  4. Open World는 A를 공통 물리 두께로 바꾸고 공통 세로 프로파일을 적용한다.
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
    float densityModifier;   // B를 0.5~1.5로 바꾼 최종 밀도 배율.
    float localThicknessPotential;// A: 공통 물리 두께 범위 안의 보간값(0~1).
    float2 uv;               // 실제 조회한 반복 Weather UV(0~1).
};

// Weather A/G와 전역 도메인으로부터 한 번 계산한 로컬 컬럼 계약이다.
// View, support precheck, Light Ray와 Deep Shadow가 이 값을 공유해야 같은
// 위치에서 구름이 시작하고 끝난다.
struct PhysicalColumnGeometry
{
    // [파생 값] 타입/A로 결정한 기둥 두께 m, 설정 계약 [1,6000].
    float localThicknessMeters;
    // [파생 값] 전역 바닥에서 상승한 거리 m, [0,min(maxLift,두께*0.25)].
    float localBaseLiftMeters;
    // [파생 값] global bottom+lift, 절대 월드 Y(m).
    float localBottomMeters;
    // [파생 값] local bottom+두께, 절대 월드 Y(m).
    float localTopMeters;
    // [파생 값] (worldY-localBottom)/두께. 0 미만/1 초과는 기둥 밖; clamp하지 않은 값.
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

// [조회 순서] 1. 이동 보정 월드 XZ/타일 크기 → UV.
// 2. t2/s1로 RGBA를 보간. 3. G/A/B의 8bit 중립값을 복원.
// 4. b10 선택을 적용해 유효 타입을 만든다. R=0은 support를 지우고 A=0은 최소 두께다.
WeatherSample SampleWeatherMap(float3 worldPosition, float timeSeconds)
{
    WeatherSample result = (WeatherSample)0;
    result.uv = ComputeWeatherUv(worldPosition, timeSeconds);
    float4 channels = saturate(
        weatherMapTexture.SampleLevel(weatherMapSampler, result.uv, 0.0));
    result.coverage = channels.r;
    // G에는 항상 지역별 원본을 저장하고 b10 선택 정책으로 유효 타입을 계산한다.
    result.cloudType = ResolveEffectiveCloudType();
    result.densityModifier = 0.5 + DecodeCanonicalWeatherChannel(channels.b);
    result.localThicknessPotential = DecodeCanonicalWeatherChannel(channels.a);
    return result;
}

// 1. 타입과 독립적인 공통 min/max 두께(m)다.
// 타입은 footprint/lift에만 쓰이며 이 두께 범위를 변경하지 않는다.
float2 EvaluatePhysicalThicknessRange()
{
    return float2(minimumThicknessMeters, maximumThicknessMeters);
}

// 2. Weather A [0,1]로 앞에서 정한 min/max 사이의 실제 두께(m)를 선택한다.
// A를 올리면 상단이 높아지지만 최대 domain을 넘어가도록 설정하면 적용이 거부된다.
float EvaluatePhysicalLocalThickness(float thicknessPotential, float cloudType)
{
    float2 rangeMeters = EvaluatePhysicalThicknessRange();
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

// 3. 타입/두께 재료로 lift를 구해 localBottom=globalBottom+lift,
// localTop=localBottom+thickness, localHeight=(Y-localBottom)/thickness를 만든다.
// 높이 비율은 여기서 clamp하지 않는다: 0 미만/1 초과를 빈 공간으로 판정해야 한다.
// View와 Shadow가 같은 기둥을 사용해야 구름과 그림자의 위치가 일치한다.
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

// 4. 기저에서 올라오는 smoothstep과 상단에서 내려오는 smoothstep을 곱한다.
// 출력 [0,1]은 수직 밀도 마스크다. 바닥/천장을 hard cut하면 판처럼 평평해진다.
float EvaluateProfileEnvelope(float heightFraction, float bottomFadeEndValue,
                              float topFadeStartValue)
{
    float h = saturate(heightFraction);
    return saturate(smoothstep(0.0, bottomFadeEndValue, h) *
        (1.0 - smoothstep(topFadeStartValue, 1.0, h)));
}

// 5. 모든 타입이 동일한 공통 envelope와 밀도 분포를 사용한다.
// 함수의 타입 인자는 기존 호출 호환용이며 곡선에는 영향이 없다.


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

// 6. 높이별 XZ 폭의 상대 배율 [0,1]. cutoff가 높을수록 그 높이의 면적이 줄어든다.
// 현재 타입별 숫자는 검증된 형상식 시작값이며 별도 CPU clamp 대상이 아니다.
// 세로 profile과 수평 coverage를 구분해 적용해야 상단을 두 번 잘라내지 않는다.
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
        EvaluateCommonVerticalProfile(heightFraction) *
        EvaluatePhysicalTypedFootprintScale(heightFraction, cloudType));
}

#endif
