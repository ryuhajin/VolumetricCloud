// ============================================================================
//  CloudShapeParameters.hlsli - CPU와 공유하는 물리 컬럼 b7 계약
// ============================================================================
#ifndef VCLOUD_CLOUD_SHAPE_PARAMETERS_HLSLI
#define VCLOUD_CLOUD_SHAPE_PARAMETERS_HLSLI

cbuffer CloudShapeCB : register(b7)
{
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
    float localBaseLiftMaxMeters;
    float footprintCoverageInfluence;
    float cloudShapePadding0;
};

#endif
