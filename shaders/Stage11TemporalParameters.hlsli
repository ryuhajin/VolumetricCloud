// ============================================================================
//  Stage11TemporalParameters.hlsli - CPU의 144바이트 b11과 동일한 계약
// ============================================================================
#ifndef STAGE11_TEMPORAL_PARAMETERS_HLSLI
#define STAGE11_TEMPORAL_PARAMETERS_HLSLI

cbuffer TemporalCB : register(b11)
{
    float4x4 previousViewProjection;

    float3 previousCameraPosition;
    float temporalDeltaTimeSeconds;

    float2 jitterOffsetLowResTexels;
    uint temporalFrameIndex;
    uint temporalHistoryValid;

    uint temporalEnabled;
    uint temporalJitterEnabled;
    uint neighborhoodClampingEnabled;
    uint temporalResetReason;

    float temporalHistoryWeight;
    float temporalSceneDepthRelativeThreshold;
    float temporalCloudDepthRelativeThreshold;
    float temporalTransmittanceThreshold;

    float nearHistoryFadeStartMeters;
    float nearHistoryFadeEndMeters;
    float maxReprojectionMotionPixels;
    float temporalClipGamma;
};

#endif
