#pragma once
// F1/F2와 설정 검증이 공유하는 기존 허용 범위. 렌더링 기본값은 별도 소유.
namespace formationrange {
inline constexpr float motionSpeedMax=1000.f;
inline constexpr unsigned macroMin=1, macroMax=8, detailMin=2, detailMax=16;
inline constexpr float biasMin=-.5f,biasMax=.5f,contrastMin=.25f,contrastMax=3.f;
inline constexpr float softnessMin=.02f,softnessMax=.8f;
inline constexpr float densityMax=5.f,extinctionMin=.000001f,extinctionMax=.01f;
inline constexpr float bottomMin=-100000.f,bottomMax=100000.f,domainMax=100000.f;
inline constexpr float baseSizeMax=200000.f,detailSizeMax=100000.f;
inline constexpr float thicknessMin=1.f,thicknessMax=6000.f,liftMax=2000.f;
}
