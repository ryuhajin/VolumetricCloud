// ============================================================================
//  CloudParameters.hlsli - CPU CloudParameters와 공유하는 80바이트 CloudCB
// ============================================================================
#ifndef VCLOUD_CLOUD_PARAMETERS_HLSLI
#define VCLOUD_CLOUD_PARAMETERS_HLSLI

cbuffer CloudCB : register(b1)
{
    float3 cloudBoundsMin;
    float densityMultiplier;
    float3 cloudBoundsMax;
    float stepSize;
    uint maxViewSteps;
    float extinctionCoefficient;
    float transmittanceThreshold;
    int debugMode;
    float baseNoiseScale;
    float coverage;
    float windSpeed;
    float noiseOffset;
    float3 windDirection;
    float cloudPadding;
};

#endif
