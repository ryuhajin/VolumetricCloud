// ============================================================================
//  NoiseLab.hlsl - 단계 5 Weather/Base/Detail의 XY/XZ/YZ 고정 단면 출력
// ----------------------------------------------------------------------------
//  1. CPU NoiseLabCB의 정규화 단면 위치를 받는다.
//  2. 화면 UV를 카메라 중심 32km 미리보기와 평면층 높이로 바꾼다.
//  3. 실제 구름과 같은 SampleCloudDensity를 호출한다.
//  4. 선택한 Base/Detail/erosion 중간값을 회색조로 출력한다.
// ============================================================================
#include "Noise.hlsli"

cbuffer NoiseLabCB : register(b2)
{
    float3 normalizedSlicePosition; // CPU slice XYZ. AABB 내부의 정규화 위치(0~1).
    uint noiseOutputMode;           // CPU NoiseOutputMode. 표시할 CloudDensitySample 필드.
    uint noiseSliceAxis;            // CPU NoiseSliceAxis. 0=XY, 1=XZ, 2=YZ.
    float effectiveTime;            // CPU Noise Lab 시간(s). 구름 바람과 동일한 시간.
    float2 noiseLabPadding;         // 16바이트 정렬용 예약 값.
    float2 previewCenterXZ;         // CPU 카메라의 월드 XZ(m).
    float2 noiseLabPreviewPadding;  // 16바이트 정렬용 예약 값.
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
    float previewSize = max(noiseLabPreviewWorldSize, 1.0);
    float2 worldXZ = previewCenterXZ +
        (saturate(normalizedPosition.xz) - 0.5.xx) * previewSize;
    float worldY = cloudBottomAltitude +
        saturate(normalizedPosition.y) * max(cloudLayerThickness, 0.0);
    return float3(worldXZ.x, worldY, worldXZ.y);
}

float4 main(VSOut input) : SV_TARGET
{
    // Base/Height 전용 출력은 sampleDetail=false로 Detail 함수 자체를 생략한다.
    // Final/Detail/Erosion/Mask만 실제 침식 결과가 필요하다.
    bool requiresDetail = noiseOutputMode == 2u ||
                          (noiseOutputMode >= 6u && noiseOutputMode <= 8u);
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
        value = sample.cloudType;
    else if (noiseOutputMode == 11u)
        value = saturate((sample.weatherDensityModifier - 0.5) / 1.0);
    else if (noiseOutputMode == 12u)
        value = sample.weatherThresholdDensity;
    else if (noiseOutputMode == 13u)
        value = sample.typedHeightProfile;
    else if (noiseOutputMode == 14u)
        return float4(sample.weatherUv, 0.0, 1.0);
    return float4(value.xxx, 1.0);
}
