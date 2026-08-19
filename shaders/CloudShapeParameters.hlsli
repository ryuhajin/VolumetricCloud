// ============================================================================
//  CloudShapeParameters.hlsli - CPU CloudShapeParameters와 공유하는 64바이트 b7
// ============================================================================
#ifndef VCLOUD_CLOUD_SHAPE_PARAMETERS_HLSLI
#define VCLOUD_CLOUD_SHAPE_PARAMETERS_HLSLI

static const uint kCloudShapeLegacy = 0u;
static const uint kCloudShapeWeatherPhysicalThickness = 1u;

cbuffer CloudShapeCB : register(b7)
{
    uint cloudShapeMode;
    float stratusMinimumThicknessMeters;
    float stratusMaximumThicknessMeters;
    float cumulusMinimumThicknessMeters;

    float cumulusMaximumThicknessMeters;
    float stratusBottomFadeEnd;
    float stratusTopFadeStart;
    float mixedBottomFadeEnd;

    float mixedTopFadeStart;
    float cumulusBottomFadeEnd;
    float cumulusTopFadeStart;
    float cumulusUpperMassBottom;

    float cumulusUpperMassStart;
    float cumulusUpperMassEnd;
    float2 cloudShapePadding;
};

#endif
