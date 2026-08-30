// ============================================================================
//  CloudSpatialResolve.hlsli - 공간 복원의 장면 경계 hard rejection 공통 계약
// ============================================================================
#ifndef CLOUD_SPATIAL_RESOLVE_HLSLI
#define CLOUD_SPATIAL_RESOLVE_HLSLI

// 이 파일을 include하는 셰이더는 CloudTap, sceneDepthTexture, renderSize,
// farPlane을 먼저 선언한다. CloudTap은 scattering/T/cloudDepth/sceneLimit을
// 가져야 한다. Joint4와 Temporal resolve가 같은 경계를 판정하기 위한 파일이다.

struct CloudScenePlaneGuide
{
    int2 targetPixel;
    float deviceDepth;
    float2 slope;
    uint slopeMask;
    uint hasGeometry;
    uint guideBuilt;
};

bool CloudSpatialGeometryDepth(float deviceDepth)
{
    return isfinite(deviceDepth) && deviceDepth >= 0.0 &&
        deviceDepth < 0.999999;
}

bool CloudSpatialSelectStableSlope(float centerDepth, float negativeDepth,
                                   float positiveDepth, out float slope)
{
    bool negativeValid = CloudSpatialGeometryDepth(negativeDepth);
    bool positiveValid = CloudSpatialGeometryDepth(positiveDepth);
    float negativeSlope = centerDepth - negativeDepth;
    float positiveSlope = positiveDepth - centerDepth;
    if (negativeValid && positiveValid)
    {
        slope = abs(negativeSlope) <= abs(positiveSlope)
            ? negativeSlope : positiveSlope;
        return true;
    }
    if (negativeValid)
    {
        slope = negativeSlope;
        return true;
    }
    if (positiveValid)
    {
        slope = positiveSlope;
        return true;
    }
    slope = 0.0;
    return false;
}

CloudScenePlaneGuide CloudSpatialBuildScenePlaneGuide(
    int2 targetPixel, float deviceDepth)
{
    CloudScenePlaneGuide guide = (CloudScenePlaneGuide)0;
    int2 fullDimensions = max(int2(renderSize), int2(1, 1));
    guide.targetPixel = clamp(targetPixel, int2(0, 0), fullDimensions - 1);
    guide.deviceDepth = deviceDepth;
    guide.hasGeometry = CloudSpatialGeometryDepth(deviceDepth) ? 1u : 0u;
    guide.guideBuilt = 1u;
    if (guide.hasGeometry == 0u)
        return guide;

    int2 left = max(guide.targetPixel - int2(1, 0), int2(0, 0));
    int2 right = min(guide.targetPixel + int2(1, 0), fullDimensions - 1);
    int2 up = max(guide.targetPixel - int2(0, 1), int2(0, 0));
    int2 down = min(guide.targetPixel + int2(0, 1), fullDimensions - 1);
    float leftDepth = left.x != guide.targetPixel.x
        ? sceneDepthTexture.Load(int3(left, 0)) : 1.0;
    float rightDepth = right.x != guide.targetPixel.x
        ? sceneDepthTexture.Load(int3(right, 0)) : 1.0;
    float upDepth = up.y != guide.targetPixel.y
        ? sceneDepthTexture.Load(int3(up, 0)) : 1.0;
    float downDepth = down.y != guide.targetPixel.y
        ? sceneDepthTexture.Load(int3(down, 0)) : 1.0;
    if (CloudSpatialSelectStableSlope(
            deviceDepth, leftDepth, rightDepth, guide.slope.x))
        guide.slopeMask |= 1u;
    if (CloudSpatialSelectStableSlope(
            deviceDepth, upDepth, downDepth, guide.slope.y))
        guide.slopeMask |= 2u;
    return guide;
}

int2 CloudSpatialSourceGuidePixel(int2 tapPixel, uint2 dimensions,
                                  float2 sourceJitterLowResTexels)
{
    float2 sourceSampleUv = saturate(
        (float2(tapPixel) + 0.5 + sourceJitterLowResTexels) /
        max(float2(dimensions), 1.0.xx));
    int2 fullDimensions = max(int2(renderSize), int2(1, 1));
    return clamp(int2(sourceSampleUv * renderSize), int2(0, 0),
                 fullDimensions - 1);
}

// 반환값: 0=valid, 1=Scene class mismatch, 2=Geometry plane mismatch,
// 3=non-finite/range 또는 plane guide 부족. Sky/Sky는 scene plane을 보지
// 않고 Cloud Depth와 T의 soft weight만 이후 Joint 단계에서 적용한다.
uint CloudSpatialSourceRejectionReason(
    CloudTap source, int2 tapPixel, uint2 dimensions,
    float targetSceneLimit, float2 sourceJitterLowResTexels,
    inout CloudScenePlaneGuide targetGuide)
{
    if (!all(isfinite(float4(source.scattering, source.transmittance))) ||
        !all(isfinite(float2(source.cloudDepth, source.sceneLimit))) ||
        source.cloudDepth < 0.0 || source.cloudDepth > farPlane ||
        source.sceneLimit < 0.0 || source.sceneLimit > farPlane)
        return 3u;

    bool sourceHasGeometry = source.sceneLimit < farPlane * 0.999;
    if (sourceHasGeometry != (targetGuide.hasGeometry != 0u))
        return 1u;
    if (targetGuide.hasGeometry == 0u)
        return 0u;

    // 가까운 같은 표면은 추가 Full Depth fetch 없이 통과시킨다. 큰 meter
    // 차이는 screen-space D32 평면으로 판정해 사선 지면은 보존하고 실제
    // depth discontinuity는 hard reject한다.
    float fastDifference = clamp(targetSceneLimit * 0.01, 1.0, 10.0);
    if (abs(source.sceneLimit - targetSceneLimit) <= fastDifference)
        return 0u;

    if (targetGuide.guideBuilt == 0u)
        targetGuide = CloudSpatialBuildScenePlaneGuide(
            targetGuide.targetPixel, targetGuide.deviceDepth);
    int2 sourcePixel = CloudSpatialSourceGuidePixel(
        tapPixel, dimensions, sourceJitterLowResTexels);
    float sourceDeviceDepth = sceneDepthTexture.Load(int3(sourcePixel, 0));
    sourceHasGeometry = CloudSpatialGeometryDepth(sourceDeviceDepth);
    if (sourceHasGeometry != (targetGuide.hasGeometry != 0u))
        return 1u;

    int2 pixelDelta = sourcePixel - targetGuide.targetPixel;
    if (all(pixelDelta == int2(0, 0)))
        return 0u;
    if ((pixelDelta.x != 0 && (targetGuide.slopeMask & 1u) == 0u) ||
        (pixelDelta.y != 0 && (targetGuide.slopeMask & 2u) == 0u))
        return 3u;
    float predictedDepth = targetGuide.deviceDepth +
        targetGuide.slope.x * float(pixelDelta.x) +
        targetGuide.slope.y * float(pixelDelta.y);
    float manhattanDistance = abs(float(pixelDelta.x)) +
        abs(float(pixelDelta.y));
    float tolerance = 8.0e-7 + 2.0e-7 * manhattanDistance;
    return isfinite(sourceDeviceDepth) && isfinite(predictedDepth) &&
        abs(sourceDeviceDepth - predictedDepth) <= tolerance ? 0u : 2u;
}

uint CloudSpatialMergeSourceFailure(uint accumulated, uint candidate)
{
    if (candidate == 2u)
        return 2u;
    if (candidate == 1u && accumulated != 2u)
        return 1u;
    return accumulated;
}

#endif
