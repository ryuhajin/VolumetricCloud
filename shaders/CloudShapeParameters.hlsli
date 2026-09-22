#ifndef VCLOUD_CLOUD_SHAPE_PARAMETERS_HLSLI
#define VCLOUD_CLOUD_SHAPE_PARAMETERS_HLSLI
// 공통 profile b7=48B. 높이는 local 0~1, lowerDensityScale은 무차원 밀도 배율.
cbuffer CloudShapeCB : register(b7)
{
 float bottomFadeEnd;
 float topFadeStart;
 float lowerDensityScale;
 float upperTransitionStart;
 float upperTransitionEnd;
 float shapePadding0;
 float shapePadding2;
 float shapePadding3;
 float shapePadding4;
 float footprintCoverageInfluence;
 float densityShaping;
 float detailCoreProtection;
};
// CPU EvaluateCommonVerticalProfile과 같은 공통 곡선. 표본 간격과 무관하다.
float EvaluateCommonVerticalProfile(float heightFraction)
{
    float h=saturate(heightFraction);
    float envelope=smoothstep(0.0,bottomFadeEnd,h)*(1.0-smoothstep(topFadeStart,1.0,h));
    return envelope*lerp(lowerDensityScale,1.0,smoothstep(upperTransitionStart,upperTransitionEnd,h));
}

// Detail 침식은 raw Base로 먼저 계산한다. 그 후 View/Light에 같은 곡선을 쓴다.
float ShapeCloudDensity(float q)
{
    if (densityShaping > 0.0) q = lerp(q, smoothstep(0.0, 0.4, q), densityShaping);
    return q;
}

#endif
