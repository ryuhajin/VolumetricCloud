// ============================================================================
//  NoiseLab.hlsl - 단계 5 Weather/Base/Detail의 XY/XZ/YZ 고정 단면 출력
// ----------------------------------------------------------------------------
//  1. CPU NoiseLabCB의 정규화 단면 위치를 받는다.
//  2. 화면 UV를 현재 PlanarLayer shape 검사 범위의 월드 위치(m)로 바꾼다.
//  3. 실제 구름과 같은 SampleCloudDensity를 호출한다.
//  4. 선택한 Base/Detail/erosion 중간값을 회색조로 출력한다.
// ============================================================================
#include "Noise.hlsli"

cbuffer NoiseLabCB : register(b2)
{
    float3 normalizedSlicePosition; // CPU slice XYZ. 검사 범위의 정규화 위치(0~1).
    uint noiseOutputMode;           // CPU NoiseOutputMode. 표시할 CloudDensitySample 필드.
    uint noiseSliceAxis;            // CPU NoiseSliceAxis. 0=XY, 1=XZ, 2=YZ.
    float effectiveTime;            // CPU Noise Lab 시간(s). 구름 바람과 동일한 시간.
    float2 noiseLabPadding;         // 16바이트 정렬용 예약 값.
};

struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float3 SliceWorldPosition(float2 uv)
{
    // 이미지 위쪽이 각 수직축의 최댓값을 가리키도록 Y를 뒤집는다.
    float2 plane = float2(uv.x, 1.0 - uv.y);
    float3 normalizedPosition = normalizedSlicePosition;
    if (noiseSliceAxis == 0u)      // XY, Z 고정
        normalizedPosition.xy = plane;
    else if (noiseSliceAxis == 1u) // XZ, Y 고정
        normalizedPosition.xz = plane;
    else                           // YZ, X 고정: 가로 Z, 세로 Y
        normalizedPosition.zy = plane;
    return lerp(cloudBoundsMin, cloudBoundsMax, saturate(normalizedPosition));
}

float4 main(VSOut input) : SV_TARGET
{
    // Base/Height 전용 출력은 sampleDetail=false로 Detail 함수 자체를 생략한다.
    // Final/Detail/Erosion/Mask만 실제 침식 결과가 필요하다.
    bool requiresDetail = noiseOutputMode == 2u ||
                          (noiseOutputMode >= 6u && noiseOutputMode <= 8u) ||
                          (noiseOutputMode >= 20u && noiseOutputMode <= 24u);
    CloudDensitySample sample = SampleCloudDensity(
        SliceWorldPosition(saturate(input.uv)), effectiveTime, requiresDetail);
    float value = sample.rawNoise;
    if (noiseOutputMode == 1u)
        value = sample.thresholdDensity;
    else if (noiseOutputMode == 2u)
        value = sample.finalDensity;
    else if (noiseOutputMode == 3u)
        value = sample.heightFraction;
    else if (noiseOutputMode == 4u)
        value = sample.heightProfile;
    else if (noiseOutputMode == 5u)
        value = sample.baseDensity;
    else if (noiseOutputMode == 6u)
        value = sample.detailNoise;
    else if (noiseOutputMode == 7u)
        value = sample.erosion;
    else if (noiseOutputMode == 8u)
        value = sample.detailSampled;
    else if (noiseOutputMode == 9u)
        value = sample.weatherCoverage;
    else if (noiseOutputMode == 10u)
        value = sample.storedRegionalType;
    else if (noiseOutputMode == 11u)
        value = saturate((sample.weatherDensityModifier - 0.5) / 1.0);
    else if (noiseOutputMode == 12u)
        value = sample.weatherThresholdDensity;
    else if (noiseOutputMode == 13u)
        value = sample.typedShapeProfile;
    else if (noiseOutputMode == 14u)
        return float4(sample.weatherUv, 0.0, 1.0);
    else if (noiseOutputMode >= 15u && noiseOutputMode <= 18u)
        value = sample.baseNoiseChannels[noiseOutputMode - 15u];
    else if (noiseOutputMode == 19u)
        value = sample.rawNoise;
    else if (noiseOutputMode >= 20u && noiseOutputMode <= 23u)
        value = sample.detailNoiseChannels[noiseOutputMode - 20u];
    else if (noiseOutputMode == 24u)
        value = sample.detailNoise;
    else if (noiseOutputMode == 25u)
        value = sample.weatherThicknessPotential;
    else if (noiseOutputMode == 26u)
        value = saturate(sample.localThicknessMeters / 6000.0);
    else if (noiseOutputMode == 27u)
        value = sample.localHeightFraction <= 1.0
            ? sample.localHeightFraction : 0.0;
    else if (noiseOutputMode == 28u)
        value = sample.effectiveShapeCoverage;
    else if (noiseOutputMode == 29u)
        value = sample.baseSupport;
    else if (noiseOutputMode == 30u)
        value = saturate(sample.localBaseLiftMeters /
                         max(maximumBaseLiftMeters, 1.0));
    return float4(value.xxx, 1.0);
}
