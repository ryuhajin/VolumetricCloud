// 공통 높이 프로파일. 모든 높이는 지역 바닥0~상단1의 비율이다.
#pragma once
#include "FormationParameterRanges.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
// 근경 미세 Detail 기본값. 구운 64³ 미세 Worley 텍스처의 실측 texel 평균(0.456036)을 중심으로 둔다.
// strength 기본 0은 미세 대역을 끈 상태이며, 필드가 없는 옛 프리셋도 기존 화면 그대로 읽힌다.
namespace nearmicro {
inline constexpr float kDefaultStrength=0.0f;
inline constexpr float kDefaultMean=0.456036f;
inline constexpr float kDefaultWarp=0.15f;
inline constexpr float kDefaultWarpFrequency=0.2f;
inline constexpr float kDefaultTileMeters=570.0f;
}
struct alignas(16) CloudShapeParameters
{
    float bottomFadeEnd=0.10f;
    float topFadeStart=0.86f;
    float lowerDensityScale=1.0f;
    float upperTransitionStart=0.08f;
    float upperTransitionEnd=0.70f;
    // [직접 조절] F2 Near micro strength, Detail 표준편차 대비 배수 [0,2]. 0이면 근경 미세 대역을 계산하지 않는다(offset20, 옛 패딩).
    float nearMicroStrength=nearmicro::kDefaultStrength;
    // [직접 조절] F2 Near micro mean [0.30,0.60]. 미세 텍스처 값에서 빼는 중심. 낮추면 근경이 더 깎이고 올리면 채워진다(offset24).
    float nearMicroMean=nearmicro::kDefaultMean;
    // [직접 조절] F2 Near micro warp [0,1], 좌표 비틀기 표준편차(tile 단위). 0이면 비틀지 않는다(offset28).
    float nearMicroWarp=nearmicro::kDefaultWarp;
    // [직접 조절] F2 Near micro warp freq [0.05,1] cycle/tile. 클수록 잘게 휜다(offset32).
    float nearMicroWarpFrequency=nearmicro::kDefaultWarpFrequency;
    float footprintCoverageInfluence=0.40f;
    float densityShaping=0.0f;
    // offset44 예약. 2026-09-24 Detail core protection 제거(모든 저장값 0이라 미사용). 항상 0.
    float reserved44=0.0f;
};
static_assert(offsetof(CloudShapeParameters,reserved44)==44, "reserved offset44");
static_assert(sizeof(CloudShapeParameters)==48, "CloudShapeCB size");
static_assert(offsetof(CloudShapeParameters,footprintCoverageInfluence)==36, "footprint offset");
static_assert(offsetof(CloudShapeParameters,densityShaping)==40, "density shaping offset");
static_assert(offsetof(CloudShapeParameters,nearMicroStrength)==20, "near micro strength offset");
static_assert(offsetof(CloudShapeParameters,nearMicroWarpFrequency)==32, "near micro warp frequency offset");
inline float ShapeCloudDensity(float q, float strength)
{
    if (strength > 0.0f) {
        const float t = std::clamp(q / 0.4f, 0.0f, 1.0f);
        const float shaped = t * t * (3.0f - 2.0f * t);
        q = q + (shaped - q) * strength;
    }
    return q;
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
    v.reserved44=0.0f;
    v.nearMicroStrength=std::clamp(finite(v.nearMicroStrength,nearmicro::kDefaultStrength),0.f,formationrange::microStrengthMax);
    v.nearMicroMean=std::clamp(finite(v.nearMicroMean,nearmicro::kDefaultMean),formationrange::microMeanMin,formationrange::microMeanMax);
    v.nearMicroWarp=std::clamp(finite(v.nearMicroWarp,nearmicro::kDefaultWarp),0.f,formationrange::microWarpMax);
    v.nearMicroWarpFrequency=std::clamp(finite(v.nearMicroWarpFrequency,nearmicro::kDefaultWarpFrequency),
        formationrange::microWarpFrequencyMin,formationrange::microWarpFrequencyMax);
    return v;
}
inline float EvaluateCommonVerticalProfile(float h,const CloudShapeParameters& v)
{
    const auto smooth=[](float a,float b,float x){float t=std::clamp((x-a)/(b-a),0.f,1.f);return t*t*(3.f-2.f*t);};
    h=std::clamp(h,0.f,1.f);
    return smooth(0.f,v.bottomFadeEnd,h)*(1.f-smooth(v.topFadeStart,1.f,h))*
        (v.lowerDensityScale+(1.f-v.lowerDensityScale)*smooth(v.upperTransitionStart,v.upperTransitionEnd,h));
}
