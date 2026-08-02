// ============================================================================
//  Stage1VolumeMath.h - 단계 1 CPU 수치 회귀 검사용 기준 구현
// ----------------------------------------------------------------------------
//  GPU 셰이더와 같은 slab 교차 및 상수 밀도 Beer-Lambert 계산을 CPU에서
//  독립적으로 검사한다. 렌더링에는 사용하지 않으며 테스트의 기대값을 명확히 한다.
// ============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace stage1
{
struct Float3
{
    float x;
    float y;
    float z;
};

struct MarchResult
{
    float entryDistance = 0.0f;
    float exitDistance = 0.0f;
    std::uint32_t stepCount = 0;
    float transmittance = 1.0f;
    bool hit = false;
};

inline bool IntersectRayAabb(const Float3& origin, const Float3& direction,
                             const Float3& boundsMin, const Float3& boundsMax,
                             float& tNear, float& tFar)
{
    constexpr float parallelEpsilon = 1e-6f;
    tNear = -INFINITY;
    tFar = INFINITY;

    const float origins[3] = { origin.x, origin.y, origin.z };
    const float directions[3] = { direction.x, direction.y, direction.z };
    const float minimums[3] = { boundsMin.x, boundsMin.y, boundsMin.z };
    const float maximums[3] = { boundsMax.x, boundsMax.y, boundsMax.z };

    for (int axis = 0; axis < 3; ++axis)
    {
        if (std::abs(directions[axis]) < parallelEpsilon)
        {
            if (origins[axis] < minimums[axis] || origins[axis] > maximums[axis])
                return false;
            continue;
        }

        float axisNear = (minimums[axis] - origins[axis]) / directions[axis];
        float axisFar = (maximums[axis] - origins[axis]) / directions[axis];
        if (axisNear > axisFar)
            std::swap(axisNear, axisFar);
        tNear = std::max(tNear, axisNear);
        tFar = std::min(tFar, axisFar);
        if (tFar < tNear)
            return false;
    }

    // 접점만 있는 경우와 카메라 뒤쪽에만 있는 구간은 부피가 없으므로 제외한다.
    return tFar > std::max(tNear, 0.0f);
}

inline MarchResult MarchConstantDensity(const Float3& origin,
                                        const Float3& direction,
                                        const Float3& boundsMin,
                                        const Float3& boundsMax,
                                        float sceneDistance,
                                        float density,
                                        float extinction,
                                        float targetStepSize,
                                        std::uint32_t maxSteps)
{
    MarchResult result;
    float tNear = 0.0f;
    float tFar = 0.0f;
    if (!IntersectRayAabb(origin, direction, boundsMin, boundsMax, tNear, tFar))
        return result;

    result.entryDistance = std::max(tNear, 0.0f);
    result.exitDistance = std::min(tFar, sceneDistance);
    const float segmentLength = result.exitDistance - result.entryDistance;
    if (!(segmentLength > 0.0f) || !(targetStepSize > 0.0f) || maxSteps == 0)
        return result;

    result.stepCount = std::min(
        maxSteps,
        static_cast<std::uint32_t>(std::ceil(segmentLength / targetStepSize)));
    const float actualStepLength = segmentLength / static_cast<float>(result.stepCount);
    const float stepTransmittance =
        std::exp(-std::max(density, 0.0f) * std::max(extinction, 0.0f) *
                 actualStepLength);
    for (std::uint32_t step = 0; step < result.stepCount; ++step)
        result.transmittance *= stepTransmittance;

    result.transmittance = std::clamp(result.transmittance, 0.0f, 1.0f);
    result.hit = true;
    return result;
}
}
