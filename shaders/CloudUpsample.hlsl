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

Texture2D<float4> sceneColorTexture : register(t0);
Texture2D<float> sceneDepthTexture : register(t1);
Texture2D<float4> cloudScatteringTransmittance : register(t2);
Texture2D<float2> cloudDepthSceneLimit : register(t3);

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
    float sourceSceneLimit;
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
    tap.sourceSceneLimit = max(depths.y, 0.0);
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
        result.sourceSceneLimit += taps[index].sourceSceneLimit * weights[index];
    }
    return result;
}

CloudTap JointCloud(float2 uv, uint2 dimensions, float targetSceneLimit,
                    bool targetHasGeometry, uint tapCount,
                    out float sceneAcceptance,
                    out float cloudDepthAcceptance,
                    out float transmittanceAcceptance)
{
    float2 sourcePosition = uv * float2(dimensions) - 0.5;
    int2 centerPixel = int2(floor(sourcePosition + 0.5));
    int2 anchorPixel = tapCount == 4u
        ? int2(floor(sourcePosition)) : centerPixel;
    CloudTap center = LoadCloudTap(centerPixel, dimensions);
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
            int2 offset = int2(x, y);
            CloudTap candidate = LoadCloudTap(anchorPixel + offset, dimensions);
            bool sourceHasGeometry = candidate.sourceSceneLimit < farPlane * 0.999;
            float classWeight = sourceHasGeometry == targetHasGeometry ? 1.0 : 0.0;
            float sceneWeight = classWeight * RelativeDepthWeight(
                targetSceneLimit, candidate.sourceSceneLimit,
                sceneDepthRelativeSigma);
            float cloudWeight = RelativeDepthWeight(
                center.cloudDepth, candidate.cloudDepth,
                cloudDepthRelativeSigma);
            float tWeight = GaussianWeight(
                abs(center.transmittance - candidate.transmittance),
                transmittanceSigma);
            float2 samplePosition = float2(anchorPixel + offset) + 0.5;
            float2 delta = samplePosition - sourcePosition;
            float spatialWeight = exp(-0.5 * dot(delta, delta));
            float weight = spatialWeight * sceneWeight * cloudWeight * tWeight;
            accumulated.scattering += candidate.scattering * weight;
            accumulated.transmittance += candidate.transmittance * weight;
            accumulated.cloudDepth += candidate.cloudDepth * weight;
            accumulated.sourceSceneLimit += candidate.sourceSceneLimit * weight;
            weightSum += weight;
            sceneAcceptance += spatialWeight * sceneWeight;
            cloudDepthAcceptance += spatialWeight * cloudWeight;
            transmittanceAcceptance += spatialWeight * tWeight;
            diagnosticSpatialWeight += spatialWeight;
        }
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
        accumulated.sourceSceneLimit /= weightSum;
        return accumulated;
    }

    // 모든 후보가 거부되면 물체에는 구름을 투명하게 해 번짐을 막고,
    // 하늘에는 가장 가까운 유효 저해상도 표본을 사용한다.
    if (targetHasGeometry)
    {
        CloudTap transparent = (CloudTap)0;
        transparent.transmittance = 1.0;
        transparent.cloudDepth = targetSceneLimit;
        transparent.sourceSceneLimit = targetSceneLimit;
        return transparent;
    }
    return center;
}

float4 main(VSOut input) : SV_TARGET
{
    float2 uv = saturate(input.uv);
    uint cloudWidth = 1u;
    uint cloudHeight = 1u;
    cloudScatteringTransmittance.GetDimensions(cloudWidth, cloudHeight);
    uint2 cloudDimensions = uint2(max(cloudWidth, 1u), max(cloudHeight, 1u));
    float deviceDepth = sceneDepthTexture.Load(
        int3(clamp(int2(input.position.xy), int2(0, 0),
                   int2(renderSize) - 1), 0));
    float3 rayDirection = ReconstructWorldRay(uv);
    bool hasGeometry = false;
    float targetSceneLimit = SceneLimitForPixel(
        uv, deviceDepth, rayDirection, hasGeometry);

    float sceneAcceptance = 1.0;
    float cloudAcceptance = 1.0;
    float transmittanceAcceptance = 1.0;
    CloudTap cloud;
    if (resolutionScale >= 0.9999 || upsampleFilterMode == 0u)
        cloud = NearestCloud(uv, cloudDimensions);
    else if (upsampleFilterMode == 1u)
        cloud = BilinearCloud(uv, cloudDimensions);
    else
        cloud = JointCloud(uv, cloudDimensions, targetSceneLimit, hasGeometry,
                           upsampleFilterMode == 2u ? 4u : 9u,
                           sceneAcceptance, cloudAcceptance,
                           transmittanceAcceptance);

    if (debugMode == 64)
    {
        float2 grid = abs(frac(uv * float2(cloudDimensions)) - 0.5);
        float gridLine = 1.0 - smoothstep(0.45, 0.49,
                                         max(grid.x, grid.y));
        return float4(lerp(float3(0.03, 0.05, 0.08),
                           float3(0.1, 0.9, 1.0), gridLine), 1.0);
    }
    if (debugMode == 65)
        return float4(saturate(sceneAcceptance).xxx, 1.0);
    if (debugMode == 66)
        return float4(saturate(cloudAcceptance).xxx, 1.0);
    if (debugMode == 67)
        return float4(saturate(transmittanceAcceptance).xxx, 1.0);

    float3 background = hasGeometry
        ? sceneColorTexture.Load(int3(int2(input.position.xy), 0)).rgb
        : SkyColor(rayDirection);
    if (hasGeometry && stage12SurfaceShadowEnabled != 0u)
    {
        float3 worldPosition = ReconstructWorldPosition(uv, deviceDepth);
        background *= Stage12SurfaceFactor(
            Stage12SurfaceTransmittance(worldPosition));
    }
    float3 composite = cloud.scattering + background * cloud.transmittance;
    return float4(ApplyLdrHighlightShoulder(composite), 1.0);
}
