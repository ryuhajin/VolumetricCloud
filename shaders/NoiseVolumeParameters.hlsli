// ============================================================================
//  NoiseVolumeParameters.hlsli - 단계 13-4 CPU/HLSL 공유 Texture3D 계약
// ============================================================================
#ifndef VCLOUD_NOISE_VOLUME_PARAMETERS_HLSLI
#define VCLOUD_NOISE_VOLUME_PARAMETERS_HLSLI

cbuffer NoiseVolumeCB : register(b6)
{
    uint baseVolumeResolution;
    uint detailVolumeResolution;
    uint noiseVolumeSeed;
    uint noiseVolumePaddingUint0;

    float baseVolumeWorldSizeMeters;
    float detailVolumeWorldSizeMeters;
    float baseVolumeVerticalWorldSizeMeters;
    float noiseVolumePadding0;

    uint4 baseVolumeFrequencies;
    uint4 detailVolumeFrequencies;
    float4 baseVolumeWeights;
    float4 detailVolumeWeights;
};

#endif
