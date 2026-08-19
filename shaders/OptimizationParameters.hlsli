// ============================================================================
//  OptimizationParameters.hlsli - CPU OptimizationParameters와 같은 b9
// ============================================================================
#ifndef VCLOUD_OPTIMIZATION_PARAMETERS_HLSLI
#define VCLOUD_OPTIMIZATION_PARAMETERS_HLSLI

cbuffer OptimizationCB : register(b9)
{
    uint supportPrecheckEnabled;
    uint emptySpaceSkippingEnabled;
    uint viewEarlyExitEnabled;
    uint distanceStepEnabled;

    uint emptySamplesBeforeCoarse;
    float baseDensityEpsilon;
    float coarseStepMultiplier;
    float maxSearchStepMeters;

    float distanceStepStartMeters;
    float distanceStepEndMeters;
    float farStepMultiplier;
    float optimizationPadding0;

    uint lightSamplingMode;
    uint coneSampleCount;
    float coneAngleDegrees;
    float lightFarSampleFraction;
};

static const uint kLightSamplingStraightRay = 0u;
static const uint kLightSamplingDeterministicCone = 1u;

#endif
