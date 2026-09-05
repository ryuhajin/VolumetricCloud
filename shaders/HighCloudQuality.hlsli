// ============================================================================
//  HighCloudQuality.hlsli - CPU HighCloudQuality.h와 같은 최종 High 상수
// ============================================================================
#ifndef VCLOUD_HIGH_CLOUD_QUALITY_HLSLI
#define VCLOUD_HIGH_CLOUD_QUALITY_HLSLI

static const float kHighViewStepMeters = 100.0;
static const uint kHighMaximumViewSteps = 512u;
static const float kHighTransmittanceThreshold = 0.01;

static const uint kHighEmptySamplesBeforeCoarse = 3u;
static const float kHighBaseDensityEpsilon = 0.0001;
static const float kHighCoarseStepMultiplier = 2.0;
static const float kHighMaximumSearchStepMeters = 200.0;

static const float kHighDistanceStepStartMeters = 24000.0;
static const float kHighDistanceStepEndMeters = 50000.0;
static const float kHighFarStepMultiplier = 1.25;

static const uint kHighConeSampleCount = 8u;
static const float kHighConeAngleDegrees = 2.0;
static const float kHighLightFarSampleFraction = 0.77;
static const float kHighLightRayBiasMeters = 1.0;

#endif
