// ============================================================================
//  WeatherMap.h - GPU Weather Map 계약과 CPU 2-scale periodic Perlin 검증 기준
// ----------------------------------------------------------------------------
//  런타임 Compute와 CPU 검증 기준이 공유하는 256² RGBA8 생성 계약이다.
// ============================================================================
#pragma once

#include "CloudParameters.h"
#include "WeatherColumnParameters.h"

#include <cstdint>
#include <cstddef>
#include <vector>

constexpr std::uint32_t kWeatherMapSize = 256;

struct PeriodicChannelSettings
{
    // [직접 조절] CPU generator의 재현 가능한 무늬 식별자. uint [0,4294967295], 구조체 초기 0. 권장 고정 seed로 비교; 크기와 구름 양은 무관하다.
    std::uint32_t seed = 0;
    // [직접 조절] F2/Weather generator의 한 타일 큰 격자 수. [강제 범위] 정수 [1,8], 초기 2. 증가하면 큰 무늬가 잘게 나뉜다.
    std::uint32_t macroPeriod = 2;
    // [직접 조절] F2/Weather generator의 작은 격자 수. [강제 범위] 정수 [2,16], 초기 5. 증가하면 미세한 지역 변화가 촘촘해진다.
    std::uint32_t detailPeriod = 5;
    // [직접 조절] F2/Weather generator. [강제 범위]/권장 UI [0,1], 초기 0.25. 0=macro만, 1=detail만.
    float detailWeight = 0.25f;
    // [직접 조절] F2/Weather generator 무차원 offset. [강제 범위] [-0.5,0.5], 초기 0. 증가하면 해당 채널이 밝아진다.
    float bias = 0.0f;
    // [직접 조절] F2/Weather generator. [강제 범위] [0.25,3], 초기 1. UI [0.1,4] 입력도 이 범위로 보정; 증가하면 0.5 주변 대비가 커진다.
    float contrast = 1.0f;
};

struct WeatherMapGeneratorSettings
{
    // [직접 조절] F2 R 배치 생성기. 아래 순서는 seed/macro/detail/weight/bias/contrast; 이 초기 조합을 비교 시작값으로 권장.
    PeriodicChannelSettings coverage = {
        1013u, 4u, 11u, 0.42f, -0.02f, 1.15f
    };
    // [직접 조절] F2 B 밀도 재료 생성기. 조회 시 0.5~1.5 배율; 밝게 만들면 같은 형상이 더 조밀해진다.
    PeriodicChannelSettings density = {
        3041u, 3u, 6u, 0.25f, 0.0f, 0.75f
    };
    // [직접 조절] F2 A 두께 재료 생성기. 0~1을 공통 min/max m 범위로 바꾼다. A=0도 최소 두께이지 빈 공간이 아니다.
    PeriodicChannelSettings localThickness = {
        4051u, 2u, 5u, 0.20f, 0.0f, 0.90f
    };
    // [직접 조절] F2/Weather generator. [강제 범위]/권장 UI [0,1], 초기 0.56. 증가하면 R 영역이 줄어든다.
    float coverageThreshold = 0.56f;
    // [직접 조절] F2/Weather generator. [강제 범위] [0.02,0.8], 초기 0.14. 증가하면 R 문턱 경계가 넓고 완만해진다.
    float coverageSoftness = 0.14f;
    // [직접 조절] F2/Weather generator. [강제 범위]/권장 UI [0,1], 초기 0.35. 증가하면 B가 독립 noise보다 R 배치를 더 따른다.
    float densityCoverageInfluence = 0.35f;
    // [직접 조절] F2/Weather generator. [강제 범위]/권장 UI [0,1], 초기 0.20. 증가하면 A 두께 재료가 coverage 중심부를 더 따른다.
    float thicknessCoverageInfluence = 0.20f;
};

struct WeatherMapDefinition
{
    // RGBA 생성만 소유하며 렌더 타입 선택과 이동은 포함하지 않는다.
    // [직접 조절] RGBA 제작 원본. 이 필드 변경은 GPU 재생성 대상; 타입 선택/Motion은 여기 속하지 않는다.
    WeatherMapGeneratorSettings generator = {};
    // [직접 조절] RGBA를 두께/lift로 읽는 설정. b10에 재패킹하며 텍스처 내용 자체는 바뀌지 않는다.
    WeatherColumnSettings column = {};
    // [직접 조절] m/타일, 기본/권장 64000. 일반 sanitize [1000,1000000], Formation [3000,160000]. 증가하면 지역 배치가 커진다.
    float worldSizeMeters = 64000.0f;
};

struct alignas(16) WeatherMapChannelGpuParameters
{
    // [직접 조절] CPU generator의 재현 가능한 무늬 식별자. uint [0,4294967295], 구조체 초기 0. 권장 고정 seed로 비교; 크기와 구름 양은 무관하다.
    std::uint32_t seed = 0;
    // [직접 조절] F2/Weather generator의 한 타일 큰 격자 수. [강제 범위] 정수 [1,8], 초기 2. 증가하면 큰 무늬가 잘게 나뉜다.
    std::uint32_t macroPeriod = 2;
    // [직접 조절] F2/Weather generator의 작은 격자 수. [강제 범위] 정수 [2,16], 초기 5. 증가하면 미세한 지역 변화가 촘촘해진다.
    std::uint32_t detailPeriod = 5;
    // [직접 조절] F2/Weather generator. [강제 범위]/권장 UI [0,1], 초기 0.25. 0=macro만, 1=detail만.
    float detailWeight = 0.25f;
    // [직접 조절] F2/Weather generator 무차원 offset. [강제 범위] [-0.5,0.5], 초기 0. 증가하면 해당 채널이 밝아진다.
    float bias = 0.0f;
    // [직접 조절] F2/Weather generator. [강제 범위] [0.25,3], 초기 1. UI [0.1,4] 입력도 이 범위로 보정; 증가하면 0.5 주변 대비가 커진다.
    float contrast = 1.0f;
    // [패딩] 16바이트 packing을 위한 예약 칸, 0 유지. 화면 효과 없음; 삭제/순서 변경 금지.
    float padding0 = 0.0f;
    // [패딩] 16바이트 packing을 위한 예약 칸, 0 유지. 화면 효과 없음; 삭제/순서 변경 금지.
    float padding1 = 0.0f;
};

struct alignas(16) WeatherMapComputeParameters
{
    // [파생 값] generator → 배열 0/1/2/3=R/예약/B/A. 각 원소 32B, CPU/HLSL 순서를 맞춘다.
    WeatherMapChannelGpuParameters channels[4] = {};
    // [직접 조절] F2/Weather generator. [강제 범위]/권장 UI [0,1], 초기 0.56. 증가하면 R 영역이 줄어든다.
    float coverageThreshold = 0.56f;
    // [직접 조절] F2/Weather generator. [강제 범위] [0.02,0.8], 초기 0.14. 증가하면 R 문턱 경계가 넓고 완만해진다.
    float coverageSoftness = 0.14f;
    // [직접 조절] F2/Weather generator. [강제 범위]/권장 UI [0,1], 초기 0.35. 증가하면 B가 독립 noise보다 R 배치를 더 따른다.
    float densityCoverageInfluence = 0.35f;
    // [직접 조절] F2/Weather generator. [강제 범위]/권장 UI [0,1], 초기 0.20. 증가하면 A 두께 재료가 coverage 중심부를 더 따른다.
    float thicknessCoverageInfluence = 0.20f;
    // [파생 값] Stage5WeatherPreset: 0 균일/1 Perlin/2 진단. 최종 런타임 시작은 1; 생성 알고리즘 선택값.
    std::uint32_t preset = 0;
    // [고정 품질] Weather 가로 256 texel. 출력 할당과 dispatch가 같은 값을 사용.
    std::uint32_t width = kWeatherMapSize;
    // [고정 품질] Weather 세로 256 texel. 한 texel은 4개 UNORM 채널.
    std::uint32_t height = kWeatherMapSize;
    // [패딩] 16바이트 packing을 위한 예약 칸, 0 유지. 화면 효과 없음; 삭제/순서 변경 금지.
    std::uint32_t padding0 = 0;
};

static_assert(sizeof(WeatherMapComputeParameters) == 160,
              "WeatherMap compute cbuffer ABI changed");

WeatherMapComputeParameters BuildWeatherMapComputeParameters(
    Stage5WeatherPreset preset, const WeatherMapGeneratorSettings& settings);

struct WeatherMapData
{
    std::uint32_t width = kWeatherMapSize;
    std::uint32_t height = kWeatherMapSize;
    std::vector<std::uint8_t> rgba;
};

WeatherMapGeneratorSettings SanitizeWeatherMapGeneratorSettings(
    const WeatherMapGeneratorSettings& settings);
bool WeatherMapGeneratorSettingsEqual(const WeatherMapGeneratorSettings& a,
                                      const WeatherMapGeneratorSettings& b);
WeatherMapDefinition SanitizeWeatherMapDefinition(
    const WeatherMapDefinition& definition);
float SamplePeriodicPerlin2D(float u, float v, std::uint32_t period,
                             std::uint32_t seed);
WeatherMapData BuildWeatherMap(
    Stage5WeatherPreset preset,
    const WeatherMapGeneratorSettings& settings = {});
bool IsValidWeatherMapData(const WeatherMapData& map);
std::uint64_t HashWeatherMap(const WeatherMapData& map);
