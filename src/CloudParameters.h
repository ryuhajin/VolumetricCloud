// ============================================================================
//  CloudParameters.h - High 물리 구름 CPU/GPU 공유 설정
// ============================================================================
#pragma once

#include <DirectXMath.h>
#include <cstdint>

enum class CloudDebugMode : std::int32_t
{
    Composite = 0,
    Transmittance = 8,
    ConstantDensity = 9,
    RawNoise = 10,
    ThresholdDensity = 11,
    FinalDensity = 12,
    NoiseUvw = 13,
    HeightFraction = 14,
    HeightProfile = 15,
    BaseDensity = 16,
    DetailNoise = 17,
    Erosion = 18,
    DetailSampleMask = 19,
    WeatherCoverage = 20,
    CloudType = 21,
    WeatherThresholdDensity = 22,
    TypedShapeProfile = 23,
    LightTransmittance = 24,
    LightOpticalDepth = 25,
    DirectSingleScattering = 27,
    PhaseCosTheta = 28,
    ForwardPhaseLobe = 29,
    BackwardPhaseLobe = 30,
    DualPhaseFactor = 31,
    AccumulatedDirectLighting = 32,
    CloudSegmentLength = 33,
    CloudHitMask = 35,
    BaseVolumeR = 36,
    BaseVolumeG = 37,
    BaseVolumeB = 38,
    BaseVolumeA = 39,
    BaseVolumeCombined = 40,
    DetailVolumeR = 41,
    DetailVolumeG = 42,
    DetailVolumeB = 43,
    DetailVolumeA = 44,
    DetailVolumeCombined = 45,
    TextureWrapDifference = 46,
    WeatherThicknessPotential = 47,
    LocalThickness = 48,
    LocalHeightFraction = 49,
    EffectiveShapeCoverage = 50,
    BaseSupportBeforeDensity = 51,
    ViewOpticalDepth = 52,
    AccumulatedSkyAmbient = 53,
    AccumulatedGroundBounce = 54,
    AccumulatedMultipleScattering = 55,
    CloudDepth = 56,
    SilverLiningContribution = 57,
    ShapedSunVisibility = 58,
    AmbientVisibility = 59,
    // Tview*(1-Tstep)로 가중한 원본 태양 T. 24의 구간 중점 한 표본과 구분한다.
    VisibleSunTransmittance = 60,
    Stage12NearOpticalDepth = 74,
    Stage12FarOpticalDepth = 75,
    Stage12CascadeSelection = 76,
    Stage12SurfaceTransmittance = 77,
    // Weather A/G가 전역 바닥에서 올린 로컬 컬럼 바닥 높이다.
    LocalBaseOffset = 81,
    CloudWithoutAerial = 90,
    AirTransmittanceAtCloud = 91,
    AirRadianceAtCloud = 92,
};

enum class Stage5WeatherPreset : std::int32_t
{
    UniformLegacy,
    PeriodicPerlin,
    ChannelDebug,
};

// HLSL CloudCB와 16바이트 묶음 순서가 정확히 일치해야 한다.
struct alignas(16) CloudParameters
{
    // [파생 값] xyz 월드 m; x/z=장면 검사 footprint, y=Planar 바닥. 최종 교차는 b5이며 유한 XZ 박스가 아니다.
    DirectX::XMFLOAT3 cloudBoundsMin = { -8.0f, -1.0f, -8.0f };
    // [직접 조절] Formation/F1 밀도 배율. [강제 범위] [0,5]. [권장 범위] 내장 1.10~1.25; 증가하면 광학적으로 두꺼워진다.
    float densityMultiplier = 1.0f;

    // [파생 값] xyz 월드 m; x/z=검사 footprint, y=바닥+층 두께. Formation 적용 시 동기화한다.
    DirectX::XMFLOAT3 cloudBoundsMax = { 8.0f, 2.0f, 8.0f };
    // [직접 조절] Formation.extinctionPerMeter/F1, 단위 1/m. [강제 범위] Formation [0.000001,0.01]. [권장 범위] 내장 0.00035~0.00046; 증가하면 T가 작아진다.
    float extinctionCoefficient = 1.0f;

    // [직접 조절] F4/숫자 키의 CloudDebugMode 열거값(기본 Composite=0). 연속 수치가 아니며 키 숫자와 enum 값은 다르다.
    std::int32_t debugMode = static_cast<std::int32_t>(CloudDebugMode::Composite);
    // [직접 조절] Formation/F1, 무차원 [강제 범위] [0,1]. [권장 범위] 내장 0.38~0.90; 증가하면 noise 문턱이 낮아져 구름이 넓어진다.
    float coverage = 0.55f;
    // ABI mirror only. Renderer가 세션 전역 CloudMotionParameters(기본 12m/s)로 패킹한다.
    // [파생 값] CloudMotion.speedMetersPerSecond에서 복사. m/s [0,1000], F1 권장 [0,400], 기본 12. Formation/Custom 저장 밖이다.
    float windSpeed = 12.0f;
    // [직접 조절] CloudParameters 코드 원본; Base xyz에 같은 cycle offset, 초기 0. 유한 실수에 별도 clamp 없음; 권장 초기값 유지, 이동은 Motion 사용.
    float noiseOffset = 0.0f;

    // [파생 값] CloudMotion.direction의 xyz 월드 단위벡터, y=0. 기본 (0.9701425,0,0.2425356). 방향만 바꾸며 속력은 windSpeed가 소유한다.
    DirectX::XMFLOAT3 windDirection = { 0.9701425f, 0.0f, 0.2425356f };
    // [직접 조절] Formation.detailErosion/F1. [강제 범위] [0,1]. [권장 범위] 내장 0.10~0.24; 증가하면 얇은 경계가 더 깎인다.
    float detailErosionStrength = 0.25f;

    // [직접 조절] CloudParameters 코드 원본; Detail xyz cycle offset, 초기 17.3. 별도 clamp 없음; 권장 초기값 유지, 패턴 위치만 바뀐다.
    float detailNoiseOffset = 17.3f;
    // [직접 조절] Formation.weather.worldSizeMeters. m/반복, 최종 기본 64000. Formation [3000,160000], 일반 Weather sanitize [1000,1000000]. 늘리면 배치가 커진다.
    float weatherMapWorldSize = 16.0f;
    // [직접 조절] CloudParameters 코드 원본; xy=월드 XZ의 cycle offset, 초기 (0,0). clamp 없음, frac으로 반복. 권장 초기값 유지.
    DirectX::XMFLOAT2 weatherMapOffset = { 0.0f, 0.0f };
};

static_assert(sizeof(CloudParameters) == 80, "CloudParameters must match CloudCB");
