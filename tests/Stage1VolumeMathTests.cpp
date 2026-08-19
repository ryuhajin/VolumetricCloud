#include "Stage1VolumeMath.h"

#include <cmath>
#include <cstdio>

namespace
{
using stage1::Float3;
using stage1::MarchConstantDensity;
using stage1::MarchResult;

constexpr Float3 kBoundsMin = { -2.0f, -1.0f, -2.0f };
constexpr Float3 kBoundsMax = { 2.0f, 2.0f, 2.0f };

bool NearlyEqual(float a, float b, float tolerance = 1e-5f)
{
    return std::abs(a - b) <= tolerance;
}

int Fail(const char* message)
{
    std::fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

MarchResult March(const Float3& origin, const Float3& direction,
                  float sceneDistance = 1000.0f, float stepSize = 0.1f,
                  const Float3& boundsMin = kBoundsMin,
                  const Float3& boundsMax = kBoundsMax)
{
    return MarchConstantDensity(origin, direction, boundsMin, boundsMax,
                                sceneDistance, 0.35f, 1.0f, stepSize, 128);
}
}

int main()
{
    // 외부 카메라: z=10에서 -Z로 진행하면 z=2 진입, z=-2 이탈이다.
    const MarchResult outside = March({ 0.0f, 0.0f, 10.0f }, { 0.0f, 0.0f, -1.0f });
    if (!outside.hit || !NearlyEqual(outside.entryDistance, 8.0f) ||
        !NearlyEqual(outside.exitDistance, 12.0f))
        return Fail("outside camera entry/exit distances are incorrect");

    // 내부 카메라는 음수 raw near 대신 현재 카메라 위치 t=0부터 시작한다.
    const MarchResult inside = March({ 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f });
    if (!inside.hit || !NearlyEqual(inside.entryDistance, 0.0f) ||
        !NearlyEqual(inside.exitDistance, 2.0f))
        return Fail("inside camera did not clamp entry to zero");

    // X/Y에 평행한 -Z 레이는 slab 안이면 교차하고 밖이면 miss여야 한다.
    if (!March({ 1.0f, 1.0f, 10.0f }, { 0.0f, 0.0f, -1.0f }).hit)
        return Fail("parallel ray inside slabs should hit");
    if (March({ 3.0f, 0.0f, 10.0f }, { 0.0f, 0.0f, -1.0f }).hit)
        return Fail("parallel ray outside a slab should miss");

    // 모서리 한 점만 스치는 tangent와 카메라 뒤의 교차는 부피가 아니다.
    if (March({ -3.0f, 1.0f, 0.0f },
              { 0.70710678f, 0.70710678f, 0.0f }).hit)
        return Fail("tangent contact should not be treated as a volume");
    if (March({ 0.0f, 0.0f, 10.0f }, { 0.0f, 0.0f, 1.0f }).hit)
        return Fail("intersection behind the camera should miss");

    // Scene Depth가 볼륨 중간이면 tEnd가 그 물체 거리에서 끝난다.
    const MarchResult clipped = March(
        { 0.0f, 0.0f, 10.0f }, { 0.0f, 0.0f, -1.0f }, 10.0f);
    if (!clipped.hit || !NearlyEqual(clipped.entryDistance, 8.0f) ||
        !NearlyEqual(clipped.exitDistance, 10.0f))
        return Fail("scene depth did not clip the volume exit");

    // 물체가 볼륨 앞이면 산란 없는 중립 결과(transmittance=1)가 된다.
    const MarchResult occluded = March(
        { 0.0f, 0.0f, 10.0f }, { 0.0f, 0.0f, -1.0f }, 6.0f);
    if (occluded.hit || !NearlyEqual(occluded.transmittance, 1.0f))
        return Fail("occluder before volume should return a neutral result");

    const MarchResult thin = March(
        { 0.0f, 0.0f, 10.0f }, { 0.0f, 0.0f, -1.0f }, 1000.0f, 0.1f,
        { -2.0f, -1.0f, -0.5f }, { 2.0f, 2.0f, 0.5f });
    const MarchResult thick = March(
        { 0.0f, 0.0f, 10.0f }, { 0.0f, 0.0f, -1.0f }, 1000.0f, 0.1f,
        { -2.0f, -1.0f, -4.0f }, { 2.0f, 2.0f, 4.0f });
    if (!thin.hit || !thick.hit || !(thick.transmittance < thin.transmittance))
        return Fail("thicker volume should have lower transmittance");

    // 상수 밀도는 step 수와 무관하게 분석식 exp(-density*extinction*length)과 같다.
    const MarchResult fine = March(
        { 0.0f, 0.0f, 10.0f }, { 0.0f, 0.0f, -1.0f }, 1000.0f, 0.025f);
    const MarchResult coarse = March(
        { 0.0f, 0.0f, 10.0f }, { 0.0f, 0.0f, -1.0f }, 1000.0f, 0.5f);
    const float analytic = std::exp(-0.35f * 1.0f * 4.0f);
    if (!NearlyEqual(fine.transmittance, analytic, 2e-5f) ||
        !NearlyEqual(coarse.transmittance, analytic, 2e-5f) ||
        !(fine.stepCount > coarse.stepCount))
        return Fail("fine/coarse integration does not match Beer-Lambert");

    const MarchResult results[] = { outside, inside, clipped, occluded, thin, thick, fine, coarse };
    for (const MarchResult& result : results)
    {
        if (!std::isfinite(result.entryDistance) || !std::isfinite(result.exitDistance) ||
            !std::isfinite(result.transmittance) || result.transmittance < 0.0f ||
            result.transmittance > 1.0f)
            return Fail("all outputs must be finite and transmittance must be in [0,1]");
    }

    std::puts("Stage 1 volume math tests passed");
    return 0;
}
