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
 float nearMicroStrength;      // offset20 근경 미세 세기 [0,2], 0=끔(F2, 타입별 저장)
 float nearMicroMean;          // offset24 미세 텍스처 중심값 [0.30,0.60]
 float nearMicroWarp;          // offset28 좌표 비틀기 표준편차 tile 단위 [0,1]
 float nearMicroWarpFrequency; // offset32 비틀기 무늬 주파수 cycle/tile [0.05,1]
 float footprintCoverageInfluence;
 float densityShaping;
 float cloudShapeReserved44;   // offset44 예약(2026-09-24 detailCoreProtection 제거, 48B 유지)
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
