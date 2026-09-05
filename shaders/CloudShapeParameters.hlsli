// ============================================================================
//  CloudShapeParameters.hlsli - CPU와 공유하는 수직 프로필 b7 계약
// ============================================================================
#ifndef VCLOUD_CLOUD_SHAPE_PARAMETERS_HLSLI
#define VCLOUD_CLOUD_SHAPE_PARAMETERS_HLSLI

cbuffer CloudShapeCB : register(b7)
{
    // local height 0~1의 기저 상승 끝/상단 소멸 시작. 간격이 넓으면 몸통이 두꺼워진다.
    float stratusBottomFadeEnd;
    float stratusTopFadeStart;
    float mixedBottomFadeEnd;
    float mixedTopFadeStart;

    float cumulusBottomFadeEnd;
    float cumulusTopFadeStart;
    float cumulusUpperMassBottom;
    float cumulusUpperMassStart;

    // upper mass 구간은 적운의 둥근 상부 질량을, footprint는 수평 윤곽 영향을 정한다.
    float cumulusUpperMassEnd;
    float footprintCoverageInfluence;
    float cloudShapePadding0;
    float cloudShapePadding1;
};

#endif
