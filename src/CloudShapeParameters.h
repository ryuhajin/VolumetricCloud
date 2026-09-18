// 공통 높이 프로파일. 모든 높이는 지역 바닥0~상단1의 비율이다.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
struct alignas(16) CloudShapeParameters
{
    float bottomFadeEnd=0.10f;
    float topFadeStart=0.86f;
    float lowerDensityScale=1.0f;
    float upperTransitionStart=0.08f;
    float upperTransitionEnd=0.70f;
    float padding0=0.0f;
    float padding2=0.0f;
    float padding3=0.0f;
    float padding4=0.0f;
    float footprintCoverageInfluence=0.40f;
    float densityShaping=0.0f;
    float padding1=0.0f;
};
static_assert(sizeof(CloudShapeParameters)==48, "CloudShapeCB size");
static_assert(offsetof(CloudShapeParameters,footprintCoverageInfluence)==36, "footprint offset");
static_assert(offsetof(CloudShapeParameters,densityShaping)==40, "density shaping offset");
inline float ShapeCloudDensity(float q, float strength)
{
    if (strength <= 0.0f) return q;
    const float t = std::clamp(q / 0.4f, 0.0f, 1.0f);
    const float shaped = t * t * (3.0f - 2.0f * t);
    return q + (shaped - q) * strength;
}

inline CloudShapeParameters SanitizeCloudShapeParameters(CloudShapeParameters v)
{
    const auto finite=[](float x,float f){return std::isfinite(x)?x:f;};
    v.bottomFadeEnd=std::clamp(finite(v.bottomFadeEnd,.10f),.01f,.99f);
    v.topFadeStart=std::clamp(finite(v.topFadeStart,.86f),v.bottomFadeEnd,.99f);
    v.lowerDensityScale=std::clamp(finite(v.lowerDensityScale,1.f),0.f,1.f);
    v.upperTransitionStart=std::clamp(finite(v.upperTransitionStart,.08f),0.f,.99f);
    v.upperTransitionEnd=std::clamp(finite(v.upperTransitionEnd,.70f),v.upperTransitionStart+.01f,1.f);
    v.footprintCoverageInfluence=std::clamp(finite(v.footprintCoverageInfluence,.40f),0.f,1.f);
    v.densityShaping=std::clamp(finite(v.densityShaping,0.f),0.f,1.f);
    v.padding0=v.padding1=v.padding2=v.padding3=v.padding4=0.f;
    return v;
}
inline float EvaluateCommonVerticalProfile(float h,const CloudShapeParameters& v)
{
    const auto smooth=[](float a,float b,float x){float t=std::clamp((x-a)/(b-a),0.f,1.f);return t*t*(3.f-2.f*t);};
    h=std::clamp(h,0.f,1.f);
    return smooth(0.f,v.bottomFadeEnd,h)*(1.f-smooth(v.topFadeStart,1.f,h))*
        (v.lowerDensityScale+(1.f-v.lowerDensityScale)*smooth(v.upperTransitionStart,v.upperTransitionEnd,h));
}
