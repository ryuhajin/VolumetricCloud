// ============================================================================
//  CloudShapeParameters.h - 타입별 수직 프로필 전용 48바이트 b7 계약
// ============================================================================
#pragma once

#include <algorithm>
#include <cstddef>
#include <cmath>

struct alignas(16) CloudShapeParameters
{
    // bottom은 기저가 차오르는 높이, top은 상단 소멸 시작 높이(모두 local 0~1).
    float stratusBottomFadeEnd = 0.06f;
    float stratusTopFadeStart = 0.65f;
    float mixedBottomFadeEnd = 0.10f;
    float mixedTopFadeStart = 0.86f;

    float cumulusBottomFadeEnd = 0.08f;
    float cumulusTopFadeStart = 0.93f;
    float cumulusUpperMassBottom = 0.65f;
    float cumulusUpperMassStart = 0.08f;

    // 적운 upper mass 구간이 넓어지면 둥근 상부 질량과 세로 발달이 강해진다.
    float cumulusUpperMassEnd = 0.70f;
    // 타입별 footprint가 coverage에 미치는 비율. 높이면 수평 윤곽 차이가 커진다.
    float footprintCoverageInfluence = 0.40f;
    float padding0 = 0.0f;
    float padding1 = 0.0f;
};

static_assert(sizeof(CloudShapeParameters) == 48,
              "CloudShapeParameters must match CloudShapeCB");
static_assert(offsetof(CloudShapeParameters, footprintCoverageInfluence) == 36,
              "CloudShapeParameters footprint influence ABI changed");

inline CloudShapeParameters SanitizeCloudShapeParameters(
    const CloudShapeParameters& value)
{
    CloudShapeParameters result = value;
    const auto finiteOr = [](float input, float fallback)
    {
        return std::isfinite(input) ? input : fallback;
    };
    result.stratusBottomFadeEnd = std::clamp(
        finiteOr(result.stratusBottomFadeEnd, 0.06f), 0.01f, 0.99f);
    result.stratusTopFadeStart = std::clamp(
        finiteOr(result.stratusTopFadeStart, 0.65f),
        result.stratusBottomFadeEnd, 0.99f);
    result.mixedBottomFadeEnd = std::clamp(
        finiteOr(result.mixedBottomFadeEnd, 0.10f), 0.01f, 0.99f);
    result.mixedTopFadeStart = std::clamp(
        finiteOr(result.mixedTopFadeStart, 0.86f),
        result.mixedBottomFadeEnd, 0.99f);
    result.cumulusBottomFadeEnd = std::clamp(
        finiteOr(result.cumulusBottomFadeEnd, 0.08f), 0.01f, 0.99f);
    result.cumulusTopFadeStart = std::clamp(
        finiteOr(result.cumulusTopFadeStart, 0.93f),
        result.cumulusBottomFadeEnd, 0.99f);
    result.cumulusUpperMassBottom = std::clamp(
        finiteOr(result.cumulusUpperMassBottom, 0.65f), 0.0f, 1.0f);
    result.cumulusUpperMassStart = std::clamp(
        finiteOr(result.cumulusUpperMassStart, 0.08f), 0.0f, 0.99f);
    result.cumulusUpperMassEnd = std::clamp(
        finiteOr(result.cumulusUpperMassEnd, 0.70f),
        result.cumulusUpperMassStart + 0.01f, 1.0f);
    result.footprintCoverageInfluence = std::clamp(
        finiteOr(result.footprintCoverageInfluence, 0.40f), 0.0f, 1.0f);
    result.padding0 = 0.0f;
    result.padding1 = 0.0f;
    return result;
}
