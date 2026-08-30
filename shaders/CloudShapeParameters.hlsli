// ============================================================================
//  CloudShapeParameters.hlsli - CPU CloudShapeParameters와 공유하는 112바이트 b7
// ============================================================================
#ifndef VCLOUD_CLOUD_SHAPE_PARAMETERS_HLSLI
#define VCLOUD_CLOUD_SHAPE_PARAMETERS_HLSLI

static const uint kCloudShapeLegacy = 0u;
static const uint kCloudShapeWeatherPhysicalThickness = 1u;
static const uint kCloudShapeCirrusPhysicalLayer = 2u;

#ifndef VCLOUD_CIRRUS_VARIANT
#define VCLOUD_CIRRUS_VARIANT -1
#endif

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

#if VCLOUD_CIRRUS_VARIANT != 0
    float2 cirrusFlowDirectionXZ;
    float cirrusBaseAlongScaleMeters;
    float cirrusBaseAcrossScaleMeters;

    float cirrusBaseVerticalScaleMeters;
    float cirrusDetailAlongScaleMeters;
    float cirrusDetailAcrossScaleMeters;
    float cirrusDetailVerticalScaleMeters;

    float cirrusMinimumThicknessMeters;
    float cirrusMaximumThicknessMeters;
    float cirrusVerticalProfileCenter;
    float cirrusVerticalProfileHalfWidth;
#endif
};

#endif
