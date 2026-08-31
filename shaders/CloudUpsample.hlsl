// ============================================================================
//  CloudUpsample.hlsl - 단계 10 Full-resolution 공간 업샘플링과 장면 합성
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
#include "Stage12Shadow.hlsli"
#include "Stage14Atmosphere.hlsli"

Texture2D<float4> sceneColorTexture : register(t0);
Texture2D<float> sceneDepthTexture : register(t1);
Texture2D<float4> cloudScatteringTransmittance : register(t2);
Texture2D<float2> cloudDepthSceneLimit : register(t3);

struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

struct SpatialResolveOutput
{
    float4 resolvedCloud : SV_Target0;
    float2 resolvedAux : SV_Target1;
    // 기존 resolve 진단 전용 출력. Composite/83/84는 Renderer가 t0/t1의
    // full-resolution pair를 별도 CloudComposite pass로 합성한다.
    float4 directOutput : SV_Target2;
};

struct CloudTap
{
    float3 scattering;
    float transmittance;
    float cloudDepth;
    float sceneLimit;
};

#include "CloudSpatialResolve.hlsli"

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

CloudTap LoadCloudTap(int2 pixel, uint2 dimensions)
{
    int2 clampedPixel = clamp(pixel, int2(0, 0), int2(dimensions) - 1);
    float4 colorT = cloudScatteringTransmittance.Load(
        int3(clampedPixel, 0));
    float2 depths = cloudDepthSceneLimit.Load(int3(clampedPixel, 0));
    CloudTap tap;
    tap.scattering = max(colorT.rgb, 0.0.xxx);
    tap.transmittance = saturate(colorT.a);
    tap.cloudDepth = max(depths.x, 0.0);
    tap.sceneLimit = max(depths.y, 0.0);
    return tap;
}

float GaussianWeight(float delta, float sigma)
{
    float normalized = delta / max(sigma, 1e-6);
    return exp(-0.5 * normalized * normalized);
}

float RelativeDepthWeight(float referenceDepth, float candidateDepth,
                          float relativeSigma)
{
    float scale = max(max(abs(referenceDepth), abs(candidateDepth)), 1.0);
    return GaussianWeight(abs(referenceDepth - candidateDepth),
                          max(relativeSigma * scale, 1e-6));
}

float SceneLimitForPixel(float2 uv, float deviceDepth, float3 rayDirection,
                         out bool hasGeometry)
{
    hasGeometry = deviceDepth < 0.999999;
    if (!hasGeometry)
        return farPlane;
    return length(ReconstructWorldPosition(uv, deviceDepth) - cameraPos);
}

CloudTap NearestCloud(float2 uv, uint2 dimensions)
{
    int2 pixel = int2(floor(uv * float2(dimensions)));
    return LoadCloudTap(pixel, dimensions);
}

CloudTap BilinearCloud(float2 uv, uint2 dimensions)
{
    float2 sourcePosition = uv * float2(dimensions) - 0.5;
    int2 basePixel = int2(floor(sourcePosition));
    float2 fractionValue = frac(sourcePosition);
    CloudTap taps[4] = {
        LoadCloudTap(basePixel, dimensions),
        LoadCloudTap(basePixel + int2(1, 0), dimensions),
        LoadCloudTap(basePixel + int2(0, 1), dimensions),
        LoadCloudTap(basePixel + int2(1, 1), dimensions)
    };
    float4 weights = float4(
        (1.0 - fractionValue.x) * (1.0 - fractionValue.y),
        fractionValue.x * (1.0 - fractionValue.y),
        (1.0 - fractionValue.x) * fractionValue.y,
        fractionValue.x * fractionValue.y);
    CloudTap result = (CloudTap)0;
    [unroll] for (int index = 0; index < 4; ++index)
    {
        result.scattering += taps[index].scattering * weights[index];
        result.transmittance += taps[index].transmittance * weights[index];
        result.cloudDepth += taps[index].cloudDepth * weights[index];
        result.sceneLimit += taps[index].sceneLimit * weights[index];
    }
    return result;
}

CloudTap TransparentCloud(float targetSceneLimit)
{
    CloudTap transparent = (CloudTap)0;
    transparent.transmittance = 1.0;
    transparent.cloudDepth = targetSceneLimit;
    transparent.sceneLimit = targetSceneLimit;
    return transparent;
}

CloudTap JointCloud(float2 uv, int2 targetPixel, float targetDeviceDepth,
                    uint2 dimensions, float targetSceneLimit, uint tapCount,
                    out float sceneAcceptance,
                    out float cloudDepthAcceptance,
                    out float transmittanceAcceptance,
                    out uint acceptedTapCount)
{
    float2 sourcePosition = uv * float2(dimensions) - 0.5;
    int2 centerPixel = int2(floor(sourcePosition + 0.5));
    int2 anchorPixel = tapCount == 4u
        ? int2(floor(sourcePosition)) : centerPixel;
    CloudScenePlaneGuide targetGuide = (CloudScenePlaneGuide)0;
    targetGuide.targetPixel = targetPixel;
    targetGuide.deviceDepth = targetDeviceDepth;
    targetGuide.hasGeometry =
        CloudSpatialGeometryDepth(targetDeviceDepth) ? 1u : 0u;
    CloudTap candidates[9];
    uint rejectionReasons[9];
    float spatialWeights[9];
    uint candidateCount = 0u;
    acceptedTapCount = 0u;
    float bestDistanceSquared = 1.0e30;
    CloudTap center = TransparentCloud(targetSceneLimit);
    CloudTap accumulated = (CloudTap)0;
    float weightSum = 0.0;
    float diagnosticSpatialWeight = 0.0;
    sceneAcceptance = 0.0;
    cloudDepthAcceptance = 0.0;
    transmittanceAcceptance = 0.0;

    [loop] for (int y = -1; y <= 1; ++y)
    {
        [loop] for (int x = -1; x <= 1; ++x)
        {
            if (tapCount == 4u && (x < 0 || y < 0))
                continue;
            int2 tapPixel = clamp(anchorPixel + int2(x, y), int2(0, 0),
                                  int2(dimensions) - 1);
            CloudTap candidate = LoadCloudTap(tapPixel, dimensions);
            uint reason = CloudSpatialSourceRejectionReason(
                candidate, tapPixel, dimensions, targetSceneLimit, 0.0.xx,
                targetGuide);
            float2 delta = float2(tapPixel) - sourcePosition;
            float spatialWeight = exp(-0.5 * dot(delta, delta));
            candidates[candidateCount] = candidate;
            rejectionReasons[candidateCount] = reason;
            spatialWeights[candidateCount] = spatialWeight;
            ++candidateCount;
            diagnosticSpatialWeight += spatialWeight;
            if (reason != 0u)
                continue;
            ++acceptedTapCount;
            sceneAcceptance += spatialWeight;
            float distanceSquared = dot(delta, delta);
            if (distanceSquared < bestDistanceSquared)
            {
                bestDistanceSquared = distanceSquared;
                center = candidate;
            }
        }
    }

    // Hard-valid tap이 하나도 없으면 Geometry/Sky 어느 쪽에서도 반대 class를
    // 끌어오지 않는다. 투명 fallback은 Temporal resolve와 동일하다.
    if (acceptedTapCount == 0u)
    {
        float inverseDiagnosticWeight = 1.0 /
            max(diagnosticSpatialWeight, 1e-6);
        sceneAcceptance *= inverseDiagnosticWeight;
        return TransparentCloud(targetSceneLimit);
    }

    [loop] for (uint index = 0u; index < candidateCount; ++index)
    {
        if (rejectionReasons[index] != 0u)
            continue;
        CloudTap candidate = candidates[index];
        float spatialWeight = spatialWeights[index];
        float cloudWeight = RelativeDepthWeight(
            center.cloudDepth, candidate.cloudDepth,
            cloudDepthRelativeSigma);
        float tWeight = GaussianWeight(
            abs(center.transmittance - candidate.transmittance),
            transmittanceSigma);
        float weight = spatialWeight *
            RelativeDepthWeight(targetSceneLimit, candidate.sceneLimit,
                                sceneDepthRelativeSigma) *
            cloudWeight * tWeight;
        accumulated.scattering += candidate.scattering * weight;
        accumulated.transmittance += candidate.transmittance * weight;
        accumulated.cloudDepth += candidate.cloudDepth * weight;
        accumulated.sceneLimit += candidate.sceneLimit * weight;
        weightSum += weight;
        cloudDepthAcceptance += spatialWeight * cloudWeight;
        transmittanceAcceptance += spatialWeight * tWeight;
    }

    float inverseDiagnosticWeight = 1.0 /
        max(diagnosticSpatialWeight, 1e-6);
    sceneAcceptance *= inverseDiagnosticWeight;
    cloudDepthAcceptance *= inverseDiagnosticWeight;
    transmittanceAcceptance *= inverseDiagnosticWeight;

    if (weightSum >= minimumUpsampleWeight)
    {
        accumulated.scattering /= weightSum;
        accumulated.transmittance /= weightSum;
        accumulated.cloudDepth /= weightSum;
        accumulated.sceneLimit /= weightSum;
        return accumulated;
    }

    // Soft weight가 수치 임계값 아래여도 hard-valid 최근접 표본은 유지한다.
    return center;
}

SpatialResolveOutput main(VSOut input)
{
    float2 uv = saturate(input.uv);
    uint cloudWidth = 1u;
    uint cloudHeight = 1u;
    cloudScatteringTransmittance.GetDimensions(cloudWidth, cloudHeight);
    uint2 cloudDimensions = uint2(max(cloudWidth, 1u), max(cloudHeight, 1u));
    int2 pixel = clamp(int2(input.position.xy), int2(0, 0),
                       int2(renderSize) - 1);
    float deviceDepth = sceneDepthTexture.Load(int3(pixel, 0));
    float3 rayDirection = ReconstructWorldRay(uv);
    bool hasGeometry = false;
    float targetSceneLimit = SceneLimitForPixel(
        uv, deviceDepth, rayDirection, hasGeometry);

    float sceneAcceptance = 1.0;
    float cloudAcceptance = 1.0;
    float transmittanceAcceptance = 1.0;
    uint acceptedTapCount = 0u;
    CloudTap cloud;
    if (resolutionScale >= 0.9999 || upsampleFilterMode == 0u)
        cloud = NearestCloud(uv, cloudDimensions);
    else if (upsampleFilterMode == 1u)
        cloud = BilinearCloud(uv, cloudDimensions);
    else
        cloud = JointCloud(uv, pixel, deviceDepth, cloudDimensions,
                           targetSceneLimit,
                           upsampleFilterMode == 2u ? 4u : 9u,
                           sceneAcceptance, cloudAcceptance,
                           transmittanceAcceptance, acceptedTapCount);

    SpatialResolveOutput output;
    output.resolvedCloud = float4(
        max(cloud.scattering, 0.0.xxx), saturate(cloud.transmittance));
    output.resolvedAux = float2(max(cloud.cloudDepth, 0.0),
                                max(targetSceneLimit, 0.0));
    output.directOutput = 0.0.xxxx;

    // Stage 15 화질 readback은 장면/대기 합성 뒤 색이 아니라 실제로
    // resolve된 cloud scattering과 T를 비교한다. Temporal resolve와 같은
    // 숨김 79/기존 8 계약을 Spatial resolve에도 유지한다.
    if (debugMode == 8)
    {
        output.directOutput = float4(
            saturate(cloud.transmittance).xxx, 1.0);
        return output;
    }
    if (debugMode == 79)
    {
        output.directOutput = float4(
            max(cloud.scattering, 0.0.xxx), 1.0);
        return output;
    }

    if (debugMode == 64)
    {
        float2 grid = abs(frac(uv * float2(cloudDimensions)) - 0.5);
        float gridLine = 1.0 - smoothstep(0.45, 0.49,
                                         max(grid.x, grid.y));
        output.directOutput = float4(lerp(float3(0.03, 0.05, 0.08),
            float3(0.1, 0.9, 1.0), gridLine), 1.0);
        return output;
    }
    if (debugMode == 65)
    {
        output.directOutput = float4(saturate(sceneAcceptance).xxx, 1.0);
        return output;
    }
    if (debugMode == 66)
    {
        output.directOutput = float4(saturate(cloudAcceptance).xxx, 1.0);
        return output;
    }
    if (debugMode == 67)
    {
        output.directOutput = float4(
            saturate(transmittanceAcceptance).xxx, 1.0);
        return output;
    }
    if (debugMode == 80)
    {
        if (resolutionScale >= 0.9999)
            output.directOutput = float4(0.18, 0.18, 0.18, 1.0);
        else
            output.directOutput = float4(
                saturate(float(acceptedTapCount) * 0.25).xxx, 1.0);
        return output;
    }
    return output;
}
