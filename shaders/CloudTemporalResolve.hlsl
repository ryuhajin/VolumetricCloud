// ============================================================================
//  CloudTemporalResolve.hlsl - 단계 11 Full 공간 복원·재투영·history 합성
// ============================================================================

cbuffer cbCamera : register(b0)
{
    float4x4 invViewProj;
    float4x4 invProjection;
    float4x4 invViewRotation;
    float3 cameraPos;
    float time;
    float2 renderSize;
    float nearPlane;
    float farPlane;
};

#include "CloudParameters.hlsli"
#include "Stage10UpsamplingParameters.hlsli"
#include "Stage11TemporalParameters.hlsli"
#include "Stage12Shadow.hlsli"
#include "Stage14Atmosphere.hlsli"

Texture2D<float4> sceneColorTexture : register(t0);
Texture2D<float> sceneDepthTexture : register(t1);
Texture2D<float4> currentCloudTexture : register(t2);
Texture2D<float2> currentCloudAuxTexture : register(t3);
Texture2D<float4> historyCloudTexture : register(t4);
Texture2D<float2> historyCloudAuxTexture : register(t5);
SamplerState linearClampSampler : register(s0);

struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

struct CloudTap
{
    float3 scattering;
    float transmittance;
    float cloudDepth;
    float sceneLimit;
};

#include "CloudSpatialResolve.hlsli"

struct SpatialReconstructionResult
{
    CloudTap value;
    uint currentValid;
    uint rejectionReason;
    uint acceptedTapCount;
    float sceneAcceptance;
    float cloudDepthAcceptance;
    float transmittanceAcceptance;
};

struct CurrentNeighborhood
{
    float4 minimumValue;
    float4 maximumValue;
    float minimumCloudDepth;
    float maximumCloudDepth;
    uint cloudDepthValid;
};

struct TemporalOutput
{
    float4 historyCloud : SV_TARGET0;
    float2 historyAux : SV_TARGET1;
    float4 composite : SV_TARGET2;
    // R=실제로 accepted된 history 비율, G=최종 적용 weight.
    // CPU는 mip 평균 1x1을 비동기 readback해 overlay에 표시한다.
    float2 historyStatistics : SV_TARGET3;
};

float2 UvToNdc(float2 uv)
{
    return float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
}

float3 ReconstructWorldRay(float2 uv)
{
    float4 viewH = mul(float4(UvToNdc(uv), 1.0, 1.0), invProjection);
    float safeW = abs(viewH.w) > 1e-6 ? viewH.w :
        (viewH.w < 0.0 ? -1e-6 : 1e-6);
    return normalize(mul(float4(normalize(viewH.xyz / safeW), 0.0),
                         invViewRotation).xyz);
}

float3 ReconstructWorldPosition(float2 uv, float deviceDepth)
{
    float4 worldH = mul(float4(UvToNdc(uv), deviceDepth, 1.0), invViewProj);
    float safeW = abs(worldH.w) > 1e-6 ? worldH.w :
        (worldH.w < 0.0 ? -1e-6 : 1e-6);
    return worldH.xyz / safeW;
}

float3 SkyColor(float3 rayDirection)
{
    float height = saturate(rayDirection.y * 0.5 + 0.5);
    return lerp(float3(0.55, 0.63, 0.72),
                float3(0.12, 0.27, 0.52), height);
}

float3 ApplyLdrHighlightShoulder(float3 color)
{
    float3 safeColor = max(color, 0.0.xxx);
    float peak = max(safeColor.r, max(safeColor.g, safeColor.b));
    const float knee = 0.80;
    if (peak <= knee)
        return safeColor;
    float excess = peak - knee;
    float mappedPeak = knee + excess * (1.0 - knee) /
        (excess + (1.0 - knee));
    return safeColor * (mappedPeak / max(peak, 1e-6));
}

CloudTap LoadCurrent(int2 pixel, uint2 dimensions)
{
    int2 p = clamp(pixel, int2(0, 0), int2(dimensions) - 1);
    float4 st = currentCloudTexture.Load(int3(p, 0));
    float2 aux = currentCloudAuxTexture.Load(int3(p, 0));
    CloudTap tap;
    // finite/range 검사는 Full Scene guide와 함께 IsSourceValid에서 수행한다.
    // 여기서 먼저 saturate/max하면 NaN이나 음수 source를 구분할 수 없다.
    tap.scattering = st.rgb;
    tap.transmittance = st.a;
    tap.cloudDepth = aux.x;
    tap.sceneLimit = aux.y;
    return tap;
}

float GaussianWeight(float delta, float sigma)
{
    float normalized = delta / max(sigma, 1e-6);
    return exp(-0.5 * normalized * normalized);
}

float RelativeDepthWeight(float a, float b, float sigma)
{
    return GaussianWeight(abs(a - b),
        max(max(max(abs(a), abs(b)), 1.0) * sigma, 1e-6));
}

float2 CurrentSourceJitterLowResTexels()
{
    return temporalJitterEnabled != 0u
        ? jitterOffsetLowResTexels : 0.0.xx;
}

void FindNearestValid(
    float2 sourcePosition, int2 centerPixel, uint2 dimensions,
    float targetSceneLimit, inout CloudScenePlaneGuide targetGuide,
    inout SpatialReconstructionResult result)
{
    result = (SpatialReconstructionResult)0;
    result.value.scattering = 0.0.xxx;
    result.value.transmittance = 1.0;
    result.value.cloudDepth = targetSceneLimit;
    result.value.sceneLimit = targetSceneLimit;
    result.currentValid = 0u;
    result.rejectionReason = 3u;
    float bestDistanceSquared = 1.0e30;

    int2 clampedCenter = clamp(centerPixel, int2(0, 0),
                               int2(dimensions) - 1);
    CloudTap center = LoadCurrent(clampedCenter, dimensions);
    uint centerReason = CloudSpatialSourceRejectionReason(
        center, clampedCenter, dimensions, targetSceneLimit,
        CurrentSourceJitterLowResTexels(), targetGuide);
    if (centerReason == 0u)
    {
        result.value = center;
        result.currentValid = 1u;
        result.rejectionReason = 0u;
        result.acceptedTapCount = 1u;
        result.sceneAcceptance = 1.0;
        result.cloudDepthAcceptance = 1.0;
        result.transmittanceAcceptance = 1.0;
        return;
    }
    result.rejectionReason = centerReason;

    // 최근접이 invalid일 때만 주변 8개를 찾는다. y-major/x-major 순서와
    // strict < 비교로 같은 거리의 tie를 결정적으로 고정한다.
    // SpatialReconstruct는 Full 3x3 neighborhood 안에서도 호출된다.
    // 강제 unroll하면 이 fallback까지 9회 복제되어 shader compile/code size가
    // 폭증하므로 표본 수는 그대로 두고 실제 loop로 유지한다.
    [loop] for (int y = -1; y <= 1; ++y)
    [loop] for (int x = -1; x <= 1; ++x)
    {
        if (x == 0 && y == 0)
            continue;
        int2 candidatePixel = clamp(centerPixel + int2(x, y), int2(0, 0),
                                    int2(dimensions) - 1);
        CloudTap candidate = LoadCurrent(candidatePixel, dimensions);
        uint reason = CloudSpatialSourceRejectionReason(
            candidate, candidatePixel, dimensions, targetSceneLimit,
            CurrentSourceJitterLowResTexels(), targetGuide);
        if (reason != 0u)
        {
            result.rejectionReason = CloudSpatialMergeSourceFailure(
                result.rejectionReason, reason);
            continue;
        }

        float2 delta = float2(candidatePixel) - sourcePosition;
        float distanceSquared = dot(delta, delta);
        if (distanceSquared < bestDistanceSquared)
        {
            bestDistanceSquared = distanceSquared;
            result.value = candidate;
            result.currentValid = 1u;
            result.rejectionReason = 0u;
            result.acceptedTapCount = 1u;
            result.sceneAcceptance = 1.0;
            result.cloudDepthAcceptance = 1.0;
            result.transmittanceAcceptance = 1.0;
        }
    }
}

void SpatialReconstruct(
    float2 uv, int2 targetPixel, float targetDeviceDepth,
    float targetSceneLimit,
    uint2 dimensions, out SpatialReconstructionResult result)
{
    result = (SpatialReconstructionResult)0;
    result.value.transmittance = 1.0;
    result.value.cloudDepth = targetSceneLimit;
    result.value.sceneLimit = targetSceneLimit;
    result.rejectionReason = 3u;
    // Cloud Data texel i는 (i + 0.5 + jitter) / dimensions의 위치를
    // raymarch했다. Full UV에서 그 texel index를 다시 찾을 때는 같은
    // low-res texel 단위 jitter를 빼야 phase마다 경계가 왕복하지 않는다.
    float2 currentJitter = resolutionScale >= 0.9999
        ? 0.0.xx : CurrentSourceJitterLowResTexels();
    float2 sourcePosition = uv * float2(dimensions) - 0.5 - currentJitter;
    CloudScenePlaneGuide targetGuide = (CloudScenePlaneGuide)0;
    targetGuide.targetPixel = targetPixel;
    targetGuide.deviceDepth = targetDeviceDepth;
    targetGuide.hasGeometry =
        CloudSpatialGeometryDepth(targetDeviceDepth) ? 1u : 0u;
    int2 nearestPixel = int2(floor(sourcePosition + 0.5));
    // Full RT는 공간 복원과 주변 탐색을 완전히 우회한다. source 한 점이
    // invalid이면 투명 current를 유지하고 Temporal history 판정에 맡긴다.
    if (resolutionScale >= 0.9999)
    {
        int2 directPixel = clamp(nearestPixel, int2(0, 0),
                                 int2(dimensions) - 1);
        CloudTap direct = LoadCurrent(directPixel, dimensions);
        uint reason = CloudSpatialSourceRejectionReason(
            direct, directPixel, dimensions, targetSceneLimit,
            0.0.xx, targetGuide);
        if (reason == 0u)
        {
            result.value = direct;
            result.currentValid = 1u;
            result.rejectionReason = 0u;
            result.acceptedTapCount = 1u;
            result.sceneAcceptance = 1.0;
            result.cloudDepthAcceptance = 1.0;
            result.transmittanceAcceptance = 1.0;
        }
        else
        {
            result.rejectionReason = reason;
        }
        return;
    }
    if (upsampleFilterMode == 0u)
    {
        FindNearestValid(sourcePosition, nearestPixel, dimensions,
                         targetSceneLimit, targetGuide, result);
        return;
    }

    int2 basePixel = int2(floor(sourcePosition));
    float2 fractionValue = frac(sourcePosition);
    if (upsampleFilterMode == 1u)
    {
        result = (SpatialReconstructionResult)0;
        result.currentValid = 0u;
        result.rejectionReason = 3u;
        float weightSum = 0.0;
        [loop] for (int y = 0; y < 2; ++y)
        [loop] for (int x = 0; x < 2; ++x)
        {
            float weight = (x == 0 ? 1.0 - fractionValue.x : fractionValue.x) *
                           (y == 0 ? 1.0 - fractionValue.y : fractionValue.y);
            CloudTap tap = LoadCurrent(basePixel + int2(x, y), dimensions);
            int2 tapPixel = clamp(basePixel + int2(x, y), int2(0, 0),
                                  int2(dimensions) - 1);
            uint reason = CloudSpatialSourceRejectionReason(
                tap, tapPixel, dimensions, targetSceneLimit,
                CurrentSourceJitterLowResTexels(), targetGuide);
            if (reason != 0u)
            {
                result.rejectionReason = CloudSpatialMergeSourceFailure(
                    result.rejectionReason, reason);
                continue;
            }
            result.value.scattering += tap.scattering * weight;
            result.value.transmittance += tap.transmittance * weight;
            result.value.cloudDepth += tap.cloudDepth * weight;
            result.value.sceneLimit += tap.sceneLimit * weight;
            weightSum += weight;
            ++result.acceptedTapCount;
            result.sceneAcceptance += weight;
        }
        if (weightSum > 1e-6)
        {
            result.value.scattering /= weightSum;
            result.value.transmittance /= weightSum;
            result.value.cloudDepth /= weightSum;
            result.value.sceneLimit /= weightSum;
            result.currentValid = 1u;
            result.rejectionReason = 0u;
            result.sceneAcceptance = saturate(result.sceneAcceptance);
            result.cloudDepthAcceptance = result.sceneAcceptance;
            result.transmittanceAcceptance = result.sceneAcceptance;
            return;
        }
        FindNearestValid(sourcePosition, nearestPixel, dimensions,
                         targetSceneLimit, targetGuide, result);
        return;
    }

    const bool nineTap = upsampleFilterMode == 3u;
    int2 anchor = nineTap ? nearestPixel : basePixel;
    result = (SpatialReconstructionResult)0;
    result.value.transmittance = 1.0;
    result.value.cloudDepth = targetSceneLimit;
    result.value.sceneLimit = targetSceneLimit;
    result.currentValid = 0u;
    result.rejectionReason = 3u;
    CloudTap candidates[9];
    uint rejectionReasons[9];
    float spatialWeights[9];
    uint candidateCount = 0u;
    float bestDistanceSquared = 1.0e30;
    CloudTap center = result.value;
    float weightSum = 0.0;
    float diagnosticSpatialWeight = 0.0;
    [loop] for (int y = nineTap ? -1 : 0; y <= 1; ++y)
    [loop] for (int x = nineTap ? -1 : 0; x <= 1; ++x)
    {
        int2 tapPixel = clamp(anchor + int2(x, y), int2(0, 0),
                              int2(dimensions) - 1);
        CloudTap tap = LoadCurrent(tapPixel, dimensions);
        uint reason = CloudSpatialSourceRejectionReason(
            tap, tapPixel, dimensions, targetSceneLimit,
            CurrentSourceJitterLowResTexels(), targetGuide);
        // sourcePosition은 texel 중심을 정수 i로 표현하므로 tap에도 +0.5를
        // 더하지 않는다. 기존 혼용은 Joint에 반 texel 편향을 만들었다.
        float2 spatialDelta = float2(tapPixel) - sourcePosition;
        float spatial = exp(-0.5 * dot(spatialDelta, spatialDelta));
        candidates[candidateCount] = tap;
        rejectionReasons[candidateCount] = reason;
        spatialWeights[candidateCount] = spatial;
        ++candidateCount;
        diagnosticSpatialWeight += spatial;
        if (reason != 0u)
        {
            result.rejectionReason = CloudSpatialMergeSourceFailure(
                result.rejectionReason, reason);
            continue;
        }
        ++result.acceptedTapCount;
        result.sceneAcceptance += spatial;
        float distanceSquared = dot(spatialDelta, spatialDelta);
        if (distanceSquared < bestDistanceSquared)
        {
            bestDistanceSquared = distanceSquared;
            center = tap;
        }
    }
    float inverseDiagnosticWeight = 1.0 /
        max(diagnosticSpatialWeight, 1e-6);
    result.sceneAcceptance *= inverseDiagnosticWeight;
    if (result.acceptedTapCount == 0u)
        return;

    // Hard-valid 후보 중 target에 가장 가까운 tap을 Cloud Depth/T guide로 쓴다.
    // 두 soft weight는 Sky/Sky에서도 유지되어 작은 구름 외곽을 보존한다.
    result.value = (CloudTap)0;
    [loop] for (uint index = 0u; index < candidateCount; ++index)
    {
        if (rejectionReasons[index] != 0u)
            continue;
        CloudTap tap = candidates[index];
        float spatial = spatialWeights[index];
        float cloudDepthWeight = RelativeDepthWeight(
            center.cloudDepth, tap.cloudDepth, cloudDepthRelativeSigma);
        float transmittanceWeight = GaussianWeight(
            abs(center.transmittance - tap.transmittance),
            transmittanceSigma);
        float weight = spatial *
            RelativeDepthWeight(targetSceneLimit, tap.sceneLimit,
                                sceneDepthRelativeSigma) *
            cloudDepthWeight * transmittanceWeight;
        result.value.scattering += tap.scattering * weight;
        result.value.transmittance += tap.transmittance * weight;
        result.value.cloudDepth += tap.cloudDepth * weight;
        result.value.sceneLimit += tap.sceneLimit * weight;
        weightSum += weight;
        result.cloudDepthAcceptance += spatial * cloudDepthWeight;
        result.transmittanceAcceptance += spatial * transmittanceWeight;
    }
    result.cloudDepthAcceptance *= inverseDiagnosticWeight;
    result.transmittanceAcceptance *= inverseDiagnosticWeight;
    if (weightSum >= minimumUpsampleWeight)
    {
        result.value.scattering /= weightSum;
        result.value.transmittance /= weightSum;
        result.value.cloudDepth /= weightSum;
        result.value.sceneLimit /= weightSum;
        result.currentValid = 1u;
        result.rejectionReason = 0u;
        return;
    }
    // Soft weight만 수치 임계값 아래이면 hard-valid 최근접 tap으로 fallback한다.
    result.value = center;
    result.currentValid = 1u;
    result.rejectionReason = 0u;
}

void GatherCurrentNeighborhood(float2 uv, int2 targetPixel,
                               float targetDeviceDepth,
                               float targetSceneLimit, CloudTap current,
                               uint2 dimensions,
                               out CurrentNeighborhood neighborhood)
{
    float4 centerValue = float4(current.scattering, current.transmittance);
    neighborhood.minimumValue = centerValue;
    neighborhood.maximumValue = centerValue;
    neighborhood.minimumCloudDepth = current.cloudDepth;
    neighborhood.maximumCloudDepth = current.cloudDepth;
    neighborhood.cloudDepthValid = current.transmittance < 0.99 ? 1u : 0u;

    float2 currentJitter = resolutionScale >= 0.9999
        ? 0.0.xx : CurrentSourceJitterLowResTexels();
    float2 sourcePosition = uv * float2(dimensions) - 0.5 - currentJitter;
    int2 sourceCenter = int2(floor(sourcePosition + 0.5));
    CloudScenePlaneGuide targetGuide = (CloudScenePlaneGuide)0;
    targetGuide.targetPixel = targetPixel;
    targetGuide.deviceDepth = targetDeviceDepth;
    targetGuide.hasGeometry =
        CloudSpatialGeometryDepth(targetDeviceDepth) ? 1u : 0u;
    // Full 3x3의 각 픽셀에서 Joint4를 다시 계산하면 최대 32개 low-res tap과
    // 여덟 번의 world-position 복원이 필요하다. 같은 유효 source 영역을
    // low-res 3x3에서 직접 모으면 9 tap으로 줄고 Scene 경계 검사는 유지된다.
    [loop] for (int y = -1; y <= 1; ++y)
    [loop] for (int x = -1; x <= 1; ++x)
    {
        if (x == 0 && y == 0)
            continue;
        int2 sourcePixel = clamp(sourceCenter + int2(x, y), int2(0, 0),
                                 int2(dimensions) - 1);
        CloudTap neighbor = LoadCurrent(sourcePixel, dimensions);
        if (CloudSpatialSourceRejectionReason(
                neighbor, sourcePixel, dimensions, targetSceneLimit,
                currentJitter, targetGuide) != 0u)
            continue;
        float4 value = float4(neighbor.scattering,
                              neighbor.transmittance);
        neighborhood.minimumValue = min(neighborhood.minimumValue, value);
        neighborhood.maximumValue = max(neighborhood.maximumValue, value);
        if (neighbor.transmittance < 0.99)
        {
            if (neighborhood.cloudDepthValid == 0u)
            {
                neighborhood.minimumCloudDepth = neighbor.cloudDepth;
                neighborhood.maximumCloudDepth = neighbor.cloudDepth;
                neighborhood.cloudDepthValid = 1u;
            }
            else
            {
                neighborhood.minimumCloudDepth = min(
                    neighborhood.minimumCloudDepth, neighbor.cloudDepth);
                neighborhood.maximumCloudDepth = max(
                    neighborhood.maximumCloudDepth, neighbor.cloudDepth);
            }
        }
    }
}

bool NeighborhoodCloudDepthAccepted(CurrentNeighborhood neighborhood,
                                    float currentTransmittance,
                                    float historyTransmittance,
                                    float historyCloudDepth)
{
    // 대표 깊이가 의미 없는 거의 투명한 표본은 T 검사와 clipping에 맡긴다.
    if (currentTransmittance >= 0.99 || historyTransmittance >= 0.99)
        return true;
    if (neighborhood.cloudDepthValid == 0u || !isfinite(historyCloudDepth))
        return false;
    float margin = temporalCloudDepthRelativeThreshold *
        max(neighborhood.maximumCloudDepth, 1.0);
    return historyCloudDepth >= neighborhood.minimumCloudDepth - margin &&
        historyCloudDepth <= neighborhood.maximumCloudDepth + margin;
}

bool RelativeAccepted(float current, float previous, float threshold)
{
    float scale = max(max(abs(current), abs(previous)), 1.0);
    return abs(current - previous) <= threshold * scale;
}

float3 HistoryValidityColor(bool accepted, bool historyAvailable,
                            float rejectionReason)
{
    if (accepted)
        return float3(0.10, 1.00, 0.20); // 허용: 초록
    if (!historyAvailable || rejectionReason < 0.5)
        return float3(0.20, 0.20, 0.20); // history 없음: 회색
    if (rejectionReason < 1.5)
        return float3(1.00, 0.00, 1.00); // clip.w: 자홍
    if (rejectionReason < 2.5)
        return float3(0.00, 0.35, 1.00); // UV 밖: 파랑
    if (rejectionReason < 3.5)
        return float3(0.00, 1.00, 1.00); // 최대 motion: 청록
    if (rejectionReason < 4.5)
        return float3(1.00, 0.45, 0.00); // Scene 불일치: 주황
    if (rejectionReason < 5.5)
        return float3(1.00, 1.00, 0.00); // Cloud Depth: 노랑
    if (rejectionReason < 6.5)
        return float3(1.00, 0.00, 0.00); // T: 빨강
    if (rejectionReason < 7.5)
        return float3(0.55, 0.20, 1.00); // near fade: 보라
    return float3(1.00, 0.10, 0.50);     // non-finite: 분홍
}

float3 CurrentSourceValidityColor(uint currentValid, uint sourceReason,
                                  bool heldHistory)
{
    if (currentValid != 0u)
        return float3(0.10, 1.00, 0.20); // valid current: 초록
    if (heldHistory)
        return float3(0.35, 0.35, 0.35); // invalid current, history 유지: 회색
    if (sourceReason == 1u)
        return float3(1.00, 0.00, 0.00); // Scene class mismatch: 빨강
    if (sourceReason == 2u)
        return float3(1.00, 1.00, 0.00); // Geometry surface/plane mismatch: 노랑
    return float3(0.00, 0.20, 1.00);     // finite/valid 후보 없음: 파랑
}

TemporalOutput main(VSOut input)
{
    float2 uv = saturate(input.uv);
    int2 pixel = clamp(int2(input.position.xy), int2(0, 0),
                       int2(renderSize) - 1);
    float deviceDepth = sceneDepthTexture.Load(int3(pixel, 0));
    float3 rayDirection = ReconstructWorldRay(uv);
    bool hasGeometry = deviceDepth < 0.999999;
    float sceneLimit = hasGeometry
        ? length(ReconstructWorldPosition(uv, deviceDepth) - cameraPos)
        : farPlane;

    uint width = 1u;
    uint height = 1u;
    currentCloudTexture.GetDimensions(width, height);
    uint2 dimensions = uint2(max(width, 1u), max(height, 1u));
    SpatialReconstructionResult currentResult =
        (SpatialReconstructionResult)0;
    SpatialReconstruct(uv, pixel, deviceDepth, sceneLimit, dimensions,
                       currentResult);
    CloudTap current = currentResult.value;
    bool currentValid = currentResult.currentValid != 0u;

    float2 previousUv = uv;
    float2 motionPixels = 0.0.xx;
    float rejectionReason = 0.0;
    bool temporalHistoryAvailable = temporalEnabled != 0u &&
        temporalHistoryValid != 0u;
    bool projectionValid = temporalHistoryAvailable;
    bool accepted = temporalHistoryAvailable;
    // current가 invalid이면 그 대표 깊이로 reprojection하지 않는다. Geometry는
    // Full Scene surface, Sky는 far-plane point를 보수적인 history anchor로 쓴다.
    float reprojectionDepth = currentValid ? current.cloudDepth : sceneLimit;
    float3 currentWorld = cameraPos + rayDirection * reprojectionDepth;
    float windLength = length(windDirection);
    float3 previousWorld = currentWorld -
        (windLength > 1e-6 ? windDirection / windLength : 0.0.xxx) *
        max(windSpeed, 0.0) * temporalDeltaTimeSeconds;
    float4 previousClip = mul(float4(previousWorld, 1.0),
                              previousViewProjection);
    if (accepted && previousClip.w <= 1e-6)
    {
        accepted = false;
        projectionValid = false;
        rejectionReason = 1.0;
    }
    if (projectionValid)
    {
        float2 previousNdc = previousClip.xy / previousClip.w;
        previousUv = float2(previousNdc.x * 0.5 + 0.5,
                            0.5 - previousNdc.y * 0.5);
        if (any(previousUv < 0.0.xx) || any(previousUv > 1.0.xx))
        {
            accepted = false;
            projectionValid = false;
            rejectionReason = 2.0;
        }
        if (projectionValid)
        {
            motionPixels = (previousUv - uv) * renderSize;
            if (length(motionPixels) > maxReprojectionMotionPixels)
            {
                accepted = false;
                rejectionReason = 3.0;
            }
        }
    }

    float4 historySt = float4(current.scattering, current.transmittance);
    float2 historyAux = float2(current.cloudDepth, current.sceneLimit);
    float4 rawHistorySt = historySt;
    bool rawHistoryAvailable = false;
    CurrentNeighborhood neighborhood = (CurrentNeighborhood)0;
    bool neighborhoodReady = false;
    if (projectionValid)
    {
        float4 sampledHistorySt = historyCloudTexture.SampleLevel(
            linearClampSampler, previousUv, 0.0);
        float2 sampledHistoryAux = historyCloudAuxTexture.SampleLevel(
            linearClampSampler, previousUv, 0.0);
        if (!all(isfinite(sampledHistorySt)) ||
            !all(isfinite(sampledHistoryAux)) ||
            sampledHistoryAux.x < 0.0 || sampledHistoryAux.y < 0.0)
        {
            accepted = false;
            rejectionReason = 8.0;
        }
        else
        {
            rawHistorySt = sampledHistorySt;
            rawHistoryAvailable = true;
            historySt = sampledHistorySt;
            historyAux = sampledHistoryAux;
            bool previousHasGeometry = historyAux.y < farPlane * 0.999;
            if (accepted && (previousHasGeometry != hasGeometry ||
                !RelativeAccepted(sceneLimit, historyAux.y,
                                  temporalSceneDepthRelativeThreshold)))
            {
                accepted = false;
                rejectionReason = 4.0;
            }
            if (accepted && currentValid)
            {
                GatherCurrentNeighborhood(
                    uv, pixel, deviceDepth, sceneLimit,
                    current, dimensions, neighborhood);
                neighborhoodReady = true;
            }
            if (accepted && currentValid && !NeighborhoodCloudDepthAccepted(
                    neighborhood, current.transmittance, historySt.a,
                    historyAux.x))
            {
                accepted = false;
                rejectionReason = 5.0;
            }
            else if (accepted && currentValid &&
                     abs(current.transmittance - historySt.a) >
                     temporalTransmittanceThreshold)
            {
                accepted = false;
                rejectionReason = 6.0;
            }
        }
    }

    float nearFade = currentValid
        ? smoothstep(nearHistoryFadeStartMeters,
                     nearHistoryFadeEndMeters, current.cloudDepth)
        : 0.0;
    bool heldHistory = !currentValid && accepted;
    float finalWeight = heldHistory ? 1.0 :
        (accepted ? temporalHistoryWeight * nearFade : 0.0);
    if (currentValid && accepted && nearFade <= 0.0)
    {
        rejectionReason = 7.0;
        accepted = false;
    }

    if (currentValid && accepted && neighborhoodClampingEnabled != 0u)
    {
        if (!neighborhoodReady)
            GatherCurrentNeighborhood(
                uv, pixel, deviceDepth, sceneLimit,
                current, dimensions, neighborhood);
        float4 centerValue = 0.5 * (
            neighborhood.minimumValue + neighborhood.maximumValue);
        float4 halfRange = 0.5 * (
            neighborhood.maximumValue - neighborhood.minimumValue) *
                           temporalClipGamma;
        historySt = clamp(historySt, centerValue - halfRange,
                          centerValue + halfRange);
    }

    float4 currentSt = float4(max(current.scattering, 0.0.xxx),
                              saturate(current.transmittance));
    float4 resolved = lerp(currentSt, historySt, finalWeight);
    resolved.rgb = max(resolved.rgb, 0.0.xxx);
    resolved.a = saturate(resolved.a);

    TemporalOutput output;
    output.historyCloud = resolved;
    output.historyAux = float2(heldHistory ? historyAux.x : current.cloudDepth,
                               sceneLimit);
    output.historyStatistics = float2(accepted ? 1.0 : 0.0, finalWeight);

    // Composite와 rim 진단은 history pair가 확정된 뒤 별도 full-resolution
    // CloudComposite pass에서 계산한다. 여기서 대기 합성을 먼저 계산하면
    // rim을 history에 넣거나 같은 합성을 두 번 실행하게 된다.
    if (debugMode == 0 || debugMode == 83 || debugMode == 84)
    {
        output.composite = 0.0.xxxx;
        return output;
    }

    // Stage 15 자동 화질 검증은 최종 temporal resolve가 실제로 사용한 T를
    // 읽는다. 숫자 8의 기존 Transmittance 디버그 의미도 그대로 유지한다.
    if (debugMode == 8)
    {
        output.composite = float4(resolved.aaa, 1.0);
        return output;
    }
    if (debugMode == 79)
    {
        output.composite = float4(resolved.rgb, 1.0);
        return output;
    }

    // Stage 10의 64~67 진단은 Temporal 경로에서도 동일한 공간 복원 결과를
    // 표시한다. 예전 Composite fallthrough는 Joint4 거부 원인을 숨겼다.
    if (debugMode == 64)
    {
        float2 grid = abs(frac(uv * float2(dimensions)) - 0.5);
        float gridLine = 1.0 - smoothstep(0.45, 0.49,
                                         max(grid.x, grid.y));
        output.composite = float4(lerp(
            float3(0.03, 0.05, 0.08), float3(0.1, 0.9, 1.0), gridLine), 1.0);
        return output;
    }
    if (debugMode == 65)
    {
        output.composite = float4(
            saturate(currentResult.sceneAcceptance).xxx, 1.0);
        return output;
    }
    if (debugMode == 66)
    {
        output.composite = float4(
            saturate(currentResult.cloudDepthAcceptance).xxx, 1.0);
        return output;
    }
    if (debugMode == 67)
    {
        output.composite = float4(
            saturate(currentResult.transmittanceAcceptance).xxx, 1.0);
        return output;
    }
    if (debugMode == 80)
    {
        if (resolutionScale >= 0.9999)
        {
            output.composite = float4(0.18, 0.18, 0.18, 1.0);
            return output; // Full RT는 spatial resolve 자체가 N/A다.
        }
        output.composite = float4(
            saturate(float(currentResult.acceptedTapCount) * 0.25).xxx, 1.0);
        return output;
    }

    if (debugMode == 68)
    {
        const float3 phaseColors[4] = {
            float3(1.0, 0.2, 0.2), float3(0.2, 1.0, 0.2),
            float3(0.2, 0.4, 1.0), float3(1.0, 0.8, 0.2)
        };
        const float3 phaseColor = phaseColors[temporalFrameIndex & 3u];
        if (resolutionScale >= 0.9999 || temporalJitterEnabled == 0u)
        {
            // High Full은 4-phase를 사용하지 않는다. 회색은 의도적인 N/A다.
            output.composite = float4(0.18, 0.18, 0.18, 1.0);
            return output;
        }

        // 한 low texel이 현재 phase에서 실제로 raymarch한 Full 2x2 위치만
        // 밝게 칠한다. 네 프레임을 넘기면 빨강/초록/파랑/노랑이 같은 2x2의
        // 네 픽셀을 정확히 한 번씩 방문하는지 화면에서 확인할 수 있다.
        float2 sourcePosition = uv * float2(dimensions) - 0.5 -
            CurrentSourceJitterLowResTexels();
        int2 sourcePixel = clamp(
            int2(floor(sourcePosition + 0.5)), int2(0, 0),
            int2(dimensions) - 1);
        int2 sourceGuidePixel = CloudSpatialSourceGuidePixel(
            sourcePixel, dimensions, CurrentSourceJitterLowResTexels());
        float visited = all(sourceGuidePixel == pixel) ? 1.0 : 0.0;
        output.composite = float4(
            lerp(phaseColor * 0.035, phaseColor, visited), 1.0);
        return output;
    }
    if (debugMode == 69)
    {
        bool cloudPresent = current.transmittance < 0.999 ||
            max(current.scattering.r,
                max(current.scattering.g, current.scattering.b)) > 1e-5;
        if (!temporalHistoryAvailable || !cloudPresent)
        {
            output.composite = float4(0.05, 0.05, 0.05, 1.0);
        }
        else if (!projectionValid)
        {
            output.composite = float4(0.05, 0.05, 1.0, 1.0);
        }
        else
        {
            float2 signedMotion = 0.5 + 0.5 * clamp(
                motionPixels / 16.0, -1.0.xx, 1.0.xx);
            output.composite = float4(signedMotion, 0.5, 1.0);
        }
        return output;
    }
    if (debugMode == 70)
    {
        output.composite = float4(HistoryValidityColor(
            accepted, temporalHistoryAvailable, rejectionReason), 1.0);
        return output;
    }
    if (debugMode == 71)
    {
        output.composite = float4(finalWeight.xxx, 1.0);
        return output;
    }
    if (debugMode == 72)
    {
        if (!currentValid || !rawHistoryAvailable)
        {
            output.composite = float4(0.0, 0.0, 1.0, 1.0);
        }
        else
        {
            float3 scatteringDifference = abs(currentSt.rgb - rawHistorySt.rgb);
            float maximumScatteringDifference = max(
                scatteringDifference.r,
                max(scatteringDifference.g, scatteringDifference.b));
            float transmittanceDifference = abs(currentSt.a - rawHistorySt.a);
            output.composite = float4(
                saturate(maximumScatteringDifference * 4.0),
                saturate(transmittanceDifference * 4.0), 0.0, 1.0);
        }
        return output;
    }
    if (debugMode == 73)
    {
        output.composite = float4(CurrentSourceValidityColor(
            currentResult.currentValid, currentResult.rejectionReason,
            heldHistory), 1.0);
        return output;
    }

    float3 background = hasGeometry
        ? sceneColorTexture.Load(int3(pixel, 0)).rgb
        : 0.0.xxx;
    if (modeFlags.x != kAtmosphereModePhysical && hasGeometry &&
        stage12SurfaceShadowEnabled != 0u)
    {
        float3 worldPosition = ReconstructWorldPosition(uv, deviceDepth);
        background *= Stage12SurfaceFactor(
            Stage12SurfaceTransmittance(worldPosition));
    }
    output.composite = float4(max(ComposeStage14Atmosphere(
        uv, rayDirection, hasGeometry, background, sceneLimit,
        resolved.rgb, resolved.a, output.historyAux.x), 0.0.xxx), 1.0);
    return output;
}
