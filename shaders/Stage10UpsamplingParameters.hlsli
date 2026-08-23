// ============================================================================
//  Stage10UpsamplingParameters.hlsli - CPU의 32바이트 b10과 동일한 계약
// ============================================================================
#ifndef STAGE10_UPSAMPLING_PARAMETERS_HLSLI
#define STAGE10_UPSAMPLING_PARAMETERS_HLSLI

cbuffer UpsamplingCB : register(b10)
{
    float resolutionScale;
    uint upsampleFilterMode;
    float sceneDepthRelativeSigma;
    float cloudDepthRelativeSigma;

    float transmittanceSigma;
    float minimumUpsampleWeight;
    float upsamplingPadding0;
    float upsamplingPadding1;
};

#endif
