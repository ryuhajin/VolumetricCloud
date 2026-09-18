#pragma once
#include "LightParameters.h"
#include "Stage7PhaseMath.h"

// 06 CPU 기준. phase<1의 기본 명암과 phase>1의 림을 구분한다.
namespace cloudrim {
struct Response { float basePhase, rimPhase, shapedTransmittance; };
inline Response Evaluate(float sunT, float phase, float rawPhase,
                         const LightParameters& input, float cap=2.5f) {
    const auto p=stage6light::Sanitize(input);
    const float t=std::clamp(sunT,0.f,1.f);
    const float s0=1+(std::pow(t,p.edgeOpticalDepthScale)-1)*p.edgeInfluence;
    const float sr=1+(std::pow(t,p.edgeOpticalDepthScale*p.rimDepthScale)-1)*p.edgeInfluence;
    return {1+std::min(phase-1,0.f)*s0,
        p.rimIntensity*std::max(std::clamp(rawPhase,0.f,cap)-1,0.f)*sr,
        std::pow(t,p.shadowExponent)};
}
}
