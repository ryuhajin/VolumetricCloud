// ============================================================================

#ifndef VCLOUD_DISABLE_RIM
#define VCLOUD_DISABLE_RIM 0
#endif
//  CloudComposite.hlsl - Stage 15B full-resolution cloud/scene/atmosphere 합성
// ----------------------------------------------------------------------------
//  예상 Renderer binding 계약
//    b0  CameraCB, b1 CloudCB, b3 LightCB, b12 Stage12ShadowCB,
//    b13 Stage14CB, b10 CloudRimCB (Composite pass-local reuse)
//    t0  scene HDR color, t1 scene depth,
//    t2  resolved cloud float4(scattering.rgb, transmittance),
//    t3  resolved aux float2(cloud representative depth, scene limit),
//    t6~t7 Stage12 deep shadow, t8~t13 Stage14 atmosphere LUT
//    s0  resolved pair linear-clamp, s2 Stage12, s3 Stage14
//
//  Rim은 resolve/history가 끝난 뒤 여기에서만 계산한다. 따라서 rim 자체는
//  history에 들어가지 않으며, Temporal On/Off가 같은 resolved pair를 넘기면
//  동일한 full-resolution 결과를 낸다.
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
#include "LightParameters.hlsli"
#include "Stage12Shadow.hlsli"
#include "Stage14Atmosphere.hlsli"
#include "CloudRimParameters.hlsli"

Texture2D<float4> sceneColorTexture : register(t0);
Texture2D<float> sceneDepthTexture : register(t1);
Texture2D<float4> resolvedCloudTexture : register(t2);
Texture2D<float2> resolvedCloudAuxTexture : register(t3);
SamplerState resolvedLinearClampSampler : register(s0);

struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

struct ResolvedCloudSample
{
    float3 scattering;
    float transmittance;
    float cloudDepth;
    float sceneLimit;
};

struct RimResult
{
    float mask;
    float3 contribution;
};

float2 CloudCompositeUvToNdc(float2 uv)
{
    return float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
}

float3 CloudCompositeWorldRay(float2 uv)
{
    float4 viewH = mul(
        float4(CloudCompositeUvToNdc(uv), 1.0, 1.0), invProjection);
    float safeW = abs(viewH.w) > 1.0e-6
        ? viewH.w : (viewH.w < 0.0 ? -1.0e-6 : 1.0e-6);
    float3 viewDirection = normalize(viewH.xyz / safeW);
    return normalize(mul(float4(viewDirection, 0.0), invViewRotation).xyz);
}

float3 CloudCompositeWorldPosition(float2 uv, float deviceDepth)
{
    float4 worldH = mul(
        float4(CloudCompositeUvToNdc(uv), deviceDepth, 1.0), invViewProj);
    float safeW = abs(worldH.w) > 1.0e-6
        ? worldH.w : (worldH.w < 0.0 ? -1.0e-6 : 1.0e-6);
    return worldH.xyz / safeW;
}

ResolvedCloudSample CloudCompositeLoad(int2 pixel, uint2 dimensions)
{
    int2 p = clamp(pixel, int2(0, 0), int2(dimensions) - 1);
    float4 cloud = resolvedCloudTexture.Load(int3(p, 0));
    float2 aux = resolvedCloudAuxTexture.Load(int3(p, 0));
    ResolvedCloudSample result;
    result.scattering = max(cloud.rgb, 0.0.xxx);
    result.transmittance = saturate(cloud.a);
    result.cloudDepth = max(aux.x, 0.0);
    result.sceneLimit = max(aux.y, 0.0);
    return result;
}

ResolvedCloudSample CloudCompositeSample(float2 uv)
{
    float4 cloud = resolvedCloudTexture.SampleLevel(
        resolvedLinearClampSampler, saturate(uv), 0);
    float2 aux = resolvedCloudAuxTexture.SampleLevel(
        resolvedLinearClampSampler, saturate(uv), 0);
    ResolvedCloudSample result;
    result.scattering = max(cloud.rgb, 0.0.xxx);
    result.transmittance = saturate(cloud.a);
    result.cloudDepth = max(aux.x, 0.0);
    result.sceneLimit = max(aux.y, 0.0);
    return result;
}

float CloudCompositeRelativeDifference(float a, float b)
{
    return abs(a - b) / max(max(abs(a), abs(b)), 1.0);
}

float CloudCompositeDepthAcceptance(
    ResolvedCloudSample center, ResolvedCloudSample neighbor,
    float centerOpacity, float neighborOpacity)
{
    // 완전히 투명한 이웃의 aux.x는 scene limit fallback일 수 있다. 그것을
    // 실제 구름 깊이처럼 비교하면 진짜 silhouette까지 모두 거부하므로,
    // 두 표본에 모두 유의미한 구름이 있을 때만 깊이 불연속을 검사한다.
    float accepted = 1.0;
    if (cloudRimDepthRejectionThreshold > 0.0 &&
        neighborOpacity > max(cloudRimOpacityThreshold -
                              cloudRimOpacitySoftness, 0.001) &&
        centerOpacity > 0.001)
    {
        accepted = center.cloudDepth > 0.0 && neighbor.cloudDepth > 0.0 &&
            CloudCompositeRelativeDifference(
                center.cloudDepth, neighbor.cloudDepth) <=
            cloudRimDepthRejectionThreshold ? 1.0 : 0.0;
    }
    return accepted;
}

float CloudCompositeSceneTexelAcceptance(
    int2 pixel, float weight, uint2 dimensions, bool centerGeometry,
    float centerSceneLimit)
{
    float accepted = 1.0;
    if (weight > 1.0e-5)
    {
        int2 clampedPixel = clamp(
            pixel, int2(0, 0), int2(dimensions) - 1);
        bool geometry = sceneDepthTexture.Load(
            int3(clampedPixel, 0)) < 0.999999;
        accepted = geometry == centerGeometry ? 1.0 : 0.0;
        if (accepted > 0.0 && centerGeometry &&
            cloudRimDepthRejectionThreshold > 0.0)
        {
            float sceneLimit = max(resolvedCloudAuxTexture.Load(
                int3(clampedPixel, 0)).y, 0.0);
            accepted = CloudCompositeRelativeDifference(
                centerSceneLimit, sceneLimit) <=
                cloudRimDepthRejectionThreshold ? 1.0 : 0.0;
        }
    }
    return accepted;
}

float CloudCompositeSceneFootprintAcceptance(
    float2 sampleUv, uint2 dimensions, bool centerGeometry,
    float centerSceneLimit)
{
    // resolved pair는 fractional rim width를 linear sampling한다. 같은 위치의
    // scene gate를 round한 한 pixel만으로 판정하면 bilinear footprint의 다른
    // 쪽에 있는 건물/하늘이 opacity에 섞여도 놓칠 수 있다. 실제 bilinear
    // footprint 네 texel(축 방향에서는 둘이 중복됨)을 모두 검사해 장면 경계를
    // 가로지르는 rim 표본을 보수적으로 거부한다.
    float2 texelPosition =
        saturate(sampleUv) * float2(max(dimensions, 1u)) - 0.5;
    int2 basePixel = int2(floor(texelPosition));
    float2 fraction = frac(texelPosition);
    float accepted = 1.0;
    accepted *= CloudCompositeSceneTexelAcceptance(
        basePixel, (1.0 - fraction.x) * (1.0 - fraction.y),
        dimensions, centerGeometry, centerSceneLimit);
    accepted *= CloudCompositeSceneTexelAcceptance(
        basePixel + int2(1, 0), fraction.x * (1.0 - fraction.y),
        dimensions, centerGeometry, centerSceneLimit);
    accepted *= CloudCompositeSceneTexelAcceptance(
        basePixel + int2(0, 1), (1.0 - fraction.x) * fraction.y,
        dimensions, centerGeometry, centerSceneLimit);
    accepted *= CloudCompositeSceneTexelAcceptance(
        basePixel + int2(1, 1), fraction.x * fraction.y,
        dimensions, centerGeometry, centerSceneLimit);
    return accepted;
}

float2 CloudCompositeProjectedSunDirection()
{
    // invViewRotation은 view->world 직교 회전이므로 matrix*vector가 역방향
    // world->view 변환이다. invProjection 대각의 역수로 화면 종횡비까지
    // 반영하고, NDC +Y와 texture UV +Y가 반대이므로 y 부호를 뒤집는다.
    // 픽셀마다 네 ray를 추가 복원하는 수치 미분보다 같은 방향을 훨씬 싸게
    // 구할 수 있어 full-resolution pass 예산을 지킨다.
    float3 viewSun = mul(
        (float3x3)invViewRotation, normalize(directionToSun));
    float projectionX = rcp(max(abs(invProjection[0][0]), 1.0e-6));
    float projectionY = rcp(max(abs(invProjection[1][1]), 1.0e-6));
    return float2(viewSun.x * projectionX, -viewSun.y * projectionY);
}

float3 CloudCompositeRimSunRadiance(float3 rayDirection, float cloudDepth)
{
    float3 radiance = max(sunColor, 0.0.xxx) * max(sunIntensity, 0.0);
    if (modeFlags.x == kAtmosphereModePhysical)
    {
        float3 cloudPosition = cameraPos + rayDirection * max(cloudDepth, 0.0);
        radiance = SampleAtmosphereSunRadiance(
            max(cloudPosition.y, 0.0) * 0.001, directionToSun);
    }
    return radiance;
}

RimResult EvaluateCloudRim(
    float2 uv, int2 pixel, uint2 dimensions, ResolvedCloudSample center,
    float3 rayDirection)
{
    RimResult result;
    result.mask = 0.0;
    result.contribution = 0.0.xxx;
#if VCLOUD_DISABLE_RIM
    // Low/Reference 및 rim Off 콘셉트는 별도 variant를 사용한다. D3D11의
    // pass-local b10 교대 바인딩 여부와 무관하게 mask/contribution이 컴파일
    // 타임에 0이므로 비용과 출력이 모두 확실히 사라진다.
    return result;
#else
    if (cloudRimEnabled == 0u || cloudRimWidthPixels <= 0.0 ||
        cloudRimIntensity <= 0.0)
    {
        return result;
    }

    const float centerOpacity = saturate(1.0 - center.transmittance);
    const float softness = max(cloudRimOpacitySoftness, 0.001);
    const float inside = smoothstep(
        cloudRimOpacityThreshold - softness,
        cloudRimOpacityThreshold + softness, centerOpacity);
    if (inside <= 0.0)
        return result;

    const float2 texelSize = rcp(float2(max(dimensions, 1u)));
    const float2 offsets[4] = {
        float2(-1.0, 0.0), float2(1.0, 0.0),
        float2(0.0, -1.0), float2(0.0, 1.0)
    };
    const bool centerGeometry =
        sceneDepthTexture.Load(int3(pixel, 0)) < 0.999999;

    float2 outward = 0.0.xx;
    float edgeStrength = 0.0;
    [unroll] for (uint index = 0u; index < 4u; ++index)
    {
        float2 sampleUv = saturate(
            uv + offsets[index] * texelSize * cloudRimWidthPixels);
        ResolvedCloudSample neighbor = CloudCompositeSample(sampleUv);
        float neighborOpacity = saturate(1.0 - neighbor.transmittance);

        // 장면 geometry/sky 경계를 건너 샘플링하지 않는다. 이 거부가 없으면
        // 건물 silhouette가 구름 rim처럼 보이는 depth leak가 생긴다.
        // geometry/sky class와 같은 geometry 안의 scene-limit 불연속을 실제
        // linear sampling footprint 전체에서 확인한다.
        float accepted = CloudCompositeSceneFootprintAcceptance(
            sampleUv, dimensions, centerGeometry, center.sceneLimit);
        accepted *= CloudCompositeDepthAcceptance(
            center, neighbor, centerOpacity, neighborOpacity);

        float outside = 1.0 - smoothstep(
            cloudRimOpacityThreshold - softness,
            cloudRimOpacityThreshold + softness, neighborOpacity);
        float contrast = saturate(
            (centerOpacity - neighborOpacity) / softness);
        float weight = accepted * outside * contrast;
        outward += offsets[index] * weight;
        edgeStrength = max(edgeStrength, weight);
    }

    float outwardLength = length(outward);
    if (edgeStrength <= 0.0 || outwardLength <= 1.0e-6)
        return result;
    float2 outwardDirection = outward / outwardLength;

    float2 projectedSun = CloudCompositeProjectedSunDirection();
    float projectedSunLength = length(projectedSun);
    float alignment = 1.0;
    if (projectedSunLength > 1.0e-6)
    {
        float cosine = dot(outwardDirection,
                           projectedSun / projectedSunLength);
        alignment = saturate(
            (cosine - cloudRimSunAlignment) /
            max(1.0 - cloudRimSunAlignment, 0.001));
        alignment = pow(alignment, max(cloudRimSunPower, 0.001));
    }

    result.mask = saturate(inside * edgeStrength * alignment);
    float3 sunRadiance = CloudCompositeRimSunRadiance(
        rayDirection, center.cloudDepth);
    // opacity를 곱한 premultiplied cloud scattering 항으로 추가한다.
    float3 contribution = max(cloudRimTint, 0.0.xxx) *
        max(sunRadiance, 0.0.xxx) * max(cloudRimIntensity, 0.0) *
        result.mask * centerOpacity;
    result.contribution = min(
        contribution, max(cloudRimRadianceClamp, 0.0).xxx);
    return result;
#endif
}

float4 main(VSOut input) : SV_TARGET
{
    uint width = 1u;
    uint height = 1u;
    resolvedCloudTexture.GetDimensions(width, height);
    uint2 dimensions = uint2(max(width, 1u), max(height, 1u));
    int2 pixel = clamp(int2(input.position.xy), int2(0, 0),
                       int2(dimensions) - 1);
    float2 uv = saturate(input.uv);
    float3 rayDirection = CloudCompositeWorldRay(uv);
    ResolvedCloudSample cloud = CloudCompositeLoad(pixel, dimensions);
    RimResult rim = EvaluateCloudRim(
        uv, pixel, dimensions, cloud, rayDirection);

    // Stage 15B에서 enum 뒤에 append되는 두 진단 ID. 다른 기존 진단은
    // Renderer가 예전 direct-output 경로를 유지하며 이 pass를 우회한다.
    if (debugMode == 83)
        return float4(rim.mask.xxx, 1.0);
    if (debugMode == 84)
        return float4(max(rim.contribution, 0.0.xxx), 1.0);

    float deviceDepth = sceneDepthTexture.Load(int3(pixel, 0));
    bool hasGeometry = deviceDepth < 0.999999;
    float3 background = hasGeometry
        ? sceneColorTexture.Load(int3(pixel, 0)).rgb : 0.0.xxx;
    if (modeFlags.x != kAtmosphereModePhysical && hasGeometry &&
        stage12SurfaceShadowEnabled != 0u)
    {
        float3 worldPosition = CloudCompositeWorldPosition(uv, deviceDepth);
        background *= Stage12SurfaceFactor(
            Stage12SurfaceTransmittance(worldPosition));
    }

    float3 cloudScattering = cloud.scattering + rim.contribution;
    float3 composite = ComposeStage14Atmosphere(
        uv, rayDirection, hasGeometry, background, cloud.sceneLimit,
        cloudScattering, cloud.transmittance, cloud.cloudDepth);
    return float4(max(composite, 0.0.xxx), 1.0);
}
