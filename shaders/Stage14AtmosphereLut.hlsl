// ============================================================================
//  Stage14AtmosphereLut.hlsl - 단계 14 여섯 RGBA16F LUT compute 생성
// ============================================================================
#include "Stage14Atmosphere.hlsli"

RWTexture2D<float4> output2D : register(u0);
RWTexture3D<float4> output3DRadiance : register(u0);
RWTexture3D<float4> output3DTransmittance : register(u1);

struct AtmosphereIntegration
{
    float3 radiance;
    float3 transmittance;
};

AtmosphereIntegration IntegrateAtmosphere(
    float3 position, float3 direction, float3 sunDirection,
    uint stepCount, bool includeGround, bool useMultipleScattering,
    float maximumDistance)
{
    AtmosphereIntegration result;
    result.radiance = 0.0.xxx;
    result.transmittance = 1.0.xxx;
    bool hitsGround = false;
    float boundaryDistance = AtmosphereDistanceToBoundary(
        position, direction, hitsGround);
    float distance = min(boundaryDistance, maximumDistance);
    uint safeSteps = max(stepCount, 1u);
    float stepLength = distance / (float)safeSteps;
    float phaseCosine = dot(direction, sunDirection);
    float rayleighPhase = AtmosphereRayleighPhase(phaseCosine);
    float miePhase = AtmosphereMiePhase(phaseCosine);
    [loop]
    for (uint index = 0u; index < safeSteps; ++index)
    {
        float sampleDistance = ((float)index + 0.5) * stepLength;
        float3 samplePosition = position + direction * sampleDistance;
        AtmosphereMedium medium = SampleAtmosphereMedium(samplePosition);
        float3 segmentT = exp(-medium.extinction * stepLength);
        float3 transmittanceToSun = SampleAtmosphereTransmittanceToSun(
            samplePosition, sunDirection);
        float3 directSource = max(solarIrradianceAndMultiplier.xyz, 0.0.xxx) *
            transmittanceToSun *
            (medium.rayleighScattering * rayleighPhase +
             medium.mieScattering * miePhase);
        float3 multipleSource = useMultipleScattering
            ? SampleAtmosphereMultipleScattering(samplePosition, sunDirection) *
              medium.scattering : 0.0.xxx;
        result.radiance += result.transmittance * AtmosphereIntegrateSource(
            directSource + multipleSource, medium.extinction, segmentT);
        result.transmittance *= segmentT;
    }
    if (includeGround && hitsGround && boundaryDistance <= maximumDistance)
    {
        float3 groundPosition = position + direction * boundaryDistance;
        float3 normal = normalize(groundPosition);
        float nDotL = saturate(dot(normal, sunDirection));
        float3 groundSun = max(solarIrradianceAndMultiplier.xyz, 0.0.xxx) *
            SampleAtmosphereTransmittanceToSun(groundPosition, sunDirection);
        result.radiance += result.transmittance * groundSun * nDotL *
            max(groundAlbedoAndDebugExposure.xyz, 0.0.xxx) / kAtmospherePi;
    }
    return result;
}

[numthreads(8, 8, 1)]
void CSTransmittance(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint)transmittanceMultiSize.x ||
        id.y >= (uint)transmittanceMultiSize.y)
        return;
    float2 uv = (float2(id.xy) + 0.5) / transmittanceMultiSize.xy;
    float viewHeight;
    float viewCosine;
    AtmosphereUvToTransmittanceParams(uv, viewHeight, viewCosine);
    float3 position = float3(0.0, viewHeight, 0.0);
    float3 direction = float3(sqrt(saturate(1.0 - viewCosine * viewCosine)),
                              viewCosine, 0.0);
    bool hitsGround = false;
    float distance = AtmosphereDistanceToBoundary(position, direction, hitsGround);
    float3 opticalDepth = 0.0.xxx;
    const uint stepCount = 40u;
    float stepLength = distance / (float)stepCount;
    [loop]
    for (uint index = 0u; index < stepCount; ++index)
    {
        float sampleDistance = ((float)index + 0.5) * stepLength;
        opticalDepth += SampleAtmosphereMedium(
            position + direction * sampleDistance).extinction * stepLength;
    }
    output2D[id.xy] = float4(exp(-opticalDepth), 1.0);
}

[numthreads(8, 8, 1)]
void CSMultiScattering(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint)transmittanceMultiSize.z ||
        id.y >= (uint)transmittanceMultiSize.w)
        return;
    float2 uv = (float2(id.xy) + 0.5) / transmittanceMultiSize.zw;
    float sunCosine = uv.x * 2.0 - 1.0;
    float3 sunDirection = float3(sqrt(saturate(1.0 - sunCosine * sunCosine)),
                                 sunCosine, 0.0);
    float viewHeight = lerp(AtmosphereBottomRadiusKm() + 0.01,
                            AtmosphereTopRadiusKm() - 0.01, uv.y);
    float3 position = float3(0.0, viewHeight, 0.0);
    float3 luminance = 0.0.xxx;
    float3 scatteringRatio = 0.0.xxx;
    [loop]
    for (uint directionIndex = 0u; directionIndex < 64u; ++directionIndex)
    {
        float2 xi = (float2(directionIndex % 8u, directionIndex / 8u) + 0.5) / 8.0;
        float phi = 2.0 * kAtmospherePi * xi.x;
        float cosine = 1.0 - 2.0 * xi.y;
        float sine = sqrt(saturate(1.0 - cosine * cosine));
        float3 direction = float3(cos(phi) * sine, cosine, sin(phi) * sine);
        bool hitsGround = false;
        float distance = AtmosphereDistanceToBoundary(position, direction, hitsGround);
        const uint stepCount = 20u;
        float stepLength = distance / (float)stepCount;
        float3 throughput = 1.0.xxx;
        [loop]
        for (uint stepIndex = 0u; stepIndex < stepCount; ++stepIndex)
        {
            float3 samplePosition = position + direction *
                (((float)stepIndex + 0.5) * stepLength);
            AtmosphereMedium medium = SampleAtmosphereMedium(samplePosition);
            float3 segmentT = exp(-medium.extinction * stepLength);
            float3 integral = (1.0.xxx - segmentT) /
                              max(medium.extinction, 1.0e-6.xxx);
            float3 sunT = SampleAtmosphereTransmittanceToSun(
                samplePosition, sunDirection);
            luminance += throughput * max(solarIrradianceAndMultiplier.xyz,
                0.0.xxx) * sunT * medium.scattering *
                (1.0 / (4.0 * kAtmospherePi)) * integral;
            scatteringRatio += throughput * medium.scattering * integral;
            throughput *= segmentT;
        }
        if (hitsGround)
        {
            float3 groundPosition = position + direction * distance;
            float3 normal = normalize(groundPosition);
            float3 sunT = SampleAtmosphereTransmittanceToSun(
                groundPosition, sunDirection);
            luminance += throughput * max(solarIrradianceAndMultiplier.xyz,
                0.0.xxx) * sunT * saturate(dot(normal, sunDirection)) *
                max(groundAlbedoAndDebugExposure.xyz, 0.0.xxx) /
                kAtmospherePi;
        }
    }
    luminance /= 64.0;
    scatteringRatio = min(scatteringRatio / 64.0, 0.999.xxx);
    output2D[id.xy] = float4(luminance /
        max(1.0.xxx - scatteringRatio, 1.0e-3.xxx), 1.0);
}

void SkyViewUvToDirection(float2 uv, float viewHeight,
                          out float3 direction)
{
    float bottom = AtmosphereBottomRadiusKm();
    float horizon = sqrt(max(viewHeight * viewHeight - bottom * bottom, 0.0));
    float beta = acos(clamp(horizon / max(viewHeight, 1.0e-6), -1.0, 1.0));
    float zenithHorizon = kAtmospherePi - beta;
    float viewCosine;
    if (uv.y < 0.5)
    {
        float coordinate = 1.0 - 2.0 * uv.y;
        coordinate = 1.0 - coordinate * coordinate;
        viewCosine = cos(zenithHorizon * coordinate);
    }
    else
    {
        float coordinate = 2.0 * uv.y - 1.0;
        viewCosine = cos(zenithHorizon + beta * coordinate * coordinate);
    }
    float horizontalCosine = -(uv.x * uv.x * 2.0 - 1.0);
    float viewSine = sqrt(saturate(1.0 - viewCosine * viewCosine));
    direction = normalize(float3(
        viewSine * horizontalCosine, viewCosine,
        viewSine * sqrt(saturate(1.0 - horizontalCosine * horizontalCosine))));
}

[numthreads(8, 8, 1)]
void CSSkyView(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint)skyViewIrradianceSize.x ||
        id.y >= (uint)skyViewIrradianceSize.y)
        return;
    float2 uv = (float2(id.xy) + 0.5) / skyViewIrradianceSize.xy;
    float viewHeight = AtmosphereBottomRadiusKm() +
                       max(sunDirectionAndCameraHeight.w, 0.001);
    float3 position = float3(0.0, viewHeight, 0.0);
    float3 direction;
    SkyViewUvToDirection(uv, viewHeight, direction);
    float sunCosine = clamp(AtmosphereSunDirection().y, -1.0, 1.0);
    float3 sunDirection = float3(sqrt(saturate(1.0 - sunCosine * sunCosine)),
                                 sunCosine, 0.0);
    AtmosphereIntegration integrated = IntegrateAtmosphere(
        position, direction, sunDirection, 30u, false, true, 1.0e30);
    output2D[id.xy] = float4(integrated.radiance, 1.0);
}

[numthreads(8, 8, 1)]
void CSSkyIrradiance(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint)skyViewIrradianceSize.z ||
        id.y >= (uint)skyViewIrradianceSize.w)
        return;
    float2 uv = (float2(id.xy) + 0.5) / skyViewIrradianceSize.zw;
    float sunCosine = uv.x * 2.0 - 1.0;
    float3 sunDirection = float3(sqrt(saturate(1.0 - sunCosine * sunCosine)),
                                 sunCosine, 0.0);
    float viewHeight = lerp(AtmosphereBottomRadiusKm() + 0.01,
                            AtmosphereTopRadiusKm() - 0.01, uv.y);
    float3 position = float3(0.0, viewHeight, 0.0);
    float3 irradiance = 0.0.xxx;
    [loop]
    for (uint directionIndex = 0u; directionIndex < 32u; ++directionIndex)
    {
        float2 xi = (float2(directionIndex % 8u, directionIndex / 8u) + 0.5) /
                    float2(8.0, 4.0);
        float cosine = sqrt(1.0 - xi.y);
        float sine = sqrt(xi.y);
        float phi = 2.0 * kAtmospherePi * xi.x;
        float3 direction = float3(cos(phi) * sine, cosine, sin(phi) * sine);
        AtmosphereIntegration integrated = IntegrateAtmosphere(
            position, direction, sunDirection, 16u, false, true, 1.0e30);
        irradiance += integrated.radiance;
    }
    // cosine-weighted hemisphere sampling의 PDF=cos/pi이므로 평균*pi다.
    output2D[id.xy] = float4(irradiance * (kAtmospherePi / 32.0), 1.0);
}

cbuffer cbCamera : register(b0)
{
    float4x4 invViewProj;
    float4x4 invProjection;
    float4x4 invViewRotation;
    float3 cameraPos;
    float atmosphereTime;
    float2 atmosphereRenderSize;
    float atmosphereNearPlane;
    float atmosphereFarPlane;
};

[numthreads(4, 4, 4)]
void CSAerialPerspective(uint3 id : SV_DispatchThreadID)
{
    uint size = aerialDebugGeneration.x;
    if (any(id >= size.xxx))
        return;
    float2 uv = (float2(id.xy) + 0.5) / (float)size;
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 viewH = mul(float4(ndc, 1.0, 1.0), invProjection);
    float3 viewDirection = normalize(viewH.xyz / max(abs(viewH.w), 1.0e-6));
    float3 worldDirection = normalize(
        mul(float4(viewDirection, 0.0), invViewRotation).xyz);
    float slice = ((float)id.z + 0.5) / (float)size;
    float distanceKm = ozoneLayerTurbidityAerialDistance.w * slice * slice;
    float cameraHeightKm = max(sunDirectionAndCameraHeight.w, 0.001);
    float3 position = float3(0.0, AtmosphereBottomRadiusKm() +
                             cameraHeightKm, 0.0);
    AtmosphereIntegration integrated = IntegrateAtmosphere(
        position, worldDirection, AtmosphereSunDirection(), 4u, false, true,
        distanceKm);
    output3DRadiance[id] = float4(integrated.radiance, 1.0);
    output3DTransmittance[id] = float4(integrated.transmittance, 1.0);
}
