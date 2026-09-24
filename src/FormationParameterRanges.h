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
// 근경 미세 Detail(E19, 타입별 저장). tile m/반복, strength는 Detail 표준편차 대비 배수, mean은 구운 텍스처 기준 중심값,
// warp는 좌표 비틀기 표준편차(tile 단위), warpFrequency는 비틀기 무늬 주파수(cycle/tile).
inline constexpr float microTileMin=200.f,microTileMax=2000.f,microStrengthMax=2.f;
inline constexpr float microMeanMin=.30f,microMeanMax=.60f,microWarpMax=1.f;
inline constexpr float microWarpFrequencyMin=.05f,microWarpFrequencyMax=1.f;
}
