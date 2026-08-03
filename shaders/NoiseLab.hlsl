// ============================================================================
//  NoiseLab.hlsl - 절차적 3D noise의 XY/XZ/YZ 고정 단면 출력
// ============================================================================
#include "Noise.hlsli"

cbuffer NoiseLabCB : register(b2)
{
    float3 normalizedSlicePosition;
    uint noiseOutputMode;
    uint noiseSliceAxis;
    float effectiveTime;
    float2 noiseLabPadding;
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
    CloudDensitySample sample = SampleCloudDensity(
        SliceWorldPosition(saturate(input.uv)), effectiveTime);
    float value = sample.rawNoise;
    if (noiseOutputMode == 1u)
        value = sample.thresholdDensity;
    else if (noiseOutputMode == 2u)
        value = sample.finalDensity;
    return float4(value.xxx, 1.0);
}
