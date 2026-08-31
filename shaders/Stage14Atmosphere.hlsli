// ============================================================================
//  Stage14Atmosphere.hlsli - 단계 14 b13, LUT 계약과 공통 대기 함수
// ============================================================================
#ifndef VCLOUD_STAGE14_ATMOSPHERE_HLSLI
#define VCLOUD_STAGE14_ATMOSPHERE_HLSLI

static const float kAtmospherePi = 3.14159265358979323846;

// CPU stage14::GpuParameters와 같은 14레지스터(224바이트)다.
cbuffer Stage14CB : register(b9)
{
    float4 planetRadiiDensityHeights;
    float4 rayleighScatteringAndScale;
    float4 mieScatteringExtinctionGAbsorption;
    float4 ozoneAbsorptionAndScale;
    float4 ozoneLayerTurbidityAerialDistance;
    float4 solarIrradianceAndMultiplier;
    float4 sunDirectionAndCameraHeight;
    float4 sunTintAndGroundBounce;
    float4 groundAlbedoAndDebugExposure;
    float4 toneAndTime;
    uint4 renderFlags;
    float4 transmittanceMultiSize;
    float4 skyViewIrradianceSize;
    uint4 aerialDebugGeneration;
};

Texture2D<float4> atmosphereTransmittanceLut : register(t8);
Texture2D<float4> atmosphereMultiScatteringLut : register(t9);
Texture2D<float4> atmosphereSkyViewLut : register(t10);
Texture2D<float4> atmosphereSkyIrradianceLut : register(t11);
Texture3D<float4> atmosphereAerialRadianceLut : register(t12);
Texture3D<float4> atmosphereAerialTransmittanceLut : register(t13);
SamplerState atmosphereLinearClampSampler : register(s3);

float AtmosphereBottomRadiusKm() { return planetRadiiDensityHeights.x; }
float AtmosphereTopRadiusKm() { return planetRadiiDensityHeights.y; }
float RayleighScaleHeightKm() { return planetRadiiDensityHeights.z; }
float MieScaleHeightKm() { return planetRadiiDensityHeights.w; }
float3 AtmosphereSunDirection() { return normalize(sunDirectionAndCameraHeight.xyz); }

float2 AtmosphereRaySphere(float3 origin, float3 direction, float radius)
{
    float b = dot(origin, direction);
    float c = dot(origin, origin) - radius * radius;
    float discriminant = b * b - c;
    if (discriminant < 0.0)
        return -1.0.xx;
    float root = sqrt(max(discriminant, 0.0));
    return float2(-b - root, -b + root);
}

float AtmosphereDistanceToBoundary(float3 position, float3 direction,
                                   out bool hitsGround)
{
    float2 top = AtmosphereRaySphere(
        position, direction, AtmosphereTopRadiusKm());
    float2 bottom = AtmosphereRaySphere(
        position, direction, AtmosphereBottomRadiusKm());
    float topDistance = top.y >= 0.0 ? max(top.y, 0.0) : 0.0;
    float groundDistance = bottom.x >= 0.0 ? bottom.x : 1.0e30;
    hitsGround = groundDistance < topDistance;
    return hitsGround ? groundDistance : topDistance;
}

struct AtmosphereMedium
{
    float3 rayleighScattering;
    float3 mieScattering;
    float3 scattering;
    float3 extinction;
};

AtmosphereMedium SampleAtmosphereMedium(float3 position)
{
    float altitude = max(length(position) - AtmosphereBottomRadiusKm(), 0.0);
    float rayleighDensity = exp(-altitude / max(RayleighScaleHeightKm(), 1.0e-4));
    float mieDensity = exp(-altitude / max(MieScaleHeightKm(), 1.0e-4));
    float ozoneDensity = saturate(1.0 - abs(altitude - ozoneLayerTurbidityAerialDistance.x) /
                                  max(ozoneLayerTurbidityAerialDistance.y, 1.0e-4));
    float rayleighScale = max(rayleighScatteringAndScale.w, 0.0);
    float turbidity = max(ozoneLayerTurbidityAerialDistance.z, 0.0);
    float mieScattering = max(mieScatteringExtinctionGAbsorption.x, 0.0) *
                          turbidity * mieDensity;
    float baseMieExtinction = max(mieScatteringExtinctionGAbsorption.y,
                                  mieScatteringExtinctionGAbsorption.x);
    float baseMieAbsorption = max(baseMieExtinction -
                                  mieScatteringExtinctionGAbsorption.x, 0.0);
    float mieExtinction = (max(mieScatteringExtinctionGAbsorption.x, 0.0) +
                           baseMieAbsorption *
                           max(mieScatteringExtinctionGAbsorption.w, 0.0)) *
                          turbidity * mieDensity;
    AtmosphereMedium medium;
    medium.rayleighScattering = max(rayleighScatteringAndScale.xyz, 0.0.xxx) *
                                rayleighScale * rayleighDensity;
    medium.mieScattering = mieScattering.xxx;
    medium.scattering = medium.rayleighScattering + medium.mieScattering;
    medium.extinction = medium.rayleighScattering + mieExtinction.xxx +
        max(ozoneAbsorptionAndScale.xyz, 0.0.xxx) *
        max(ozoneAbsorptionAndScale.w, 0.0) * ozoneDensity;
    return medium;
}

float AtmosphereRayleighPhase(float cosine)
{
    float mu = clamp(cosine, -1.0, 1.0);
    return 3.0 * (1.0 + mu * mu) / (16.0 * kAtmospherePi);
}

float AtmosphereMiePhase(float cosine)
{
    float g = clamp(mieScatteringExtinctionGAbsorption.z, 0.0, 0.95);
    float mu = clamp(cosine, -1.0, 1.0);
    float k = 3.0 * (1.0 - g * g) /
              (8.0 * kAtmospherePi * (2.0 + g * g));
    float denominator = max(1.0 + g * g - 2.0 * g * mu, 1.0e-6);
    return k * (1.0 + mu * mu) /
           (denominator * sqrt(denominator));
}

float3 AtmosphereIntegrateSource(float3 source, float3 extinction,
                                 float3 segmentTransmittance)
{
    float3 safeExtinction = max(extinction, 1.0e-6.xxx);
    return source * (1.0.xxx - segmentTransmittance) / safeExtinction;
}

float2 AtmosphereTransmittanceParamsToUv(float viewHeight, float viewCosine)
{
    float bottom = AtmosphereBottomRadiusKm();
    float top = AtmosphereTopRadiusKm();
    float height = clamp(viewHeight, bottom, top);
    float mu = clamp(viewCosine, -1.0, 1.0);
    float H = sqrt(max(top * top - bottom * bottom, 0.0));
    float rho = sqrt(max(height * height - bottom * bottom, 0.0));
    float distance = -height * mu + sqrt(max(
        height * height * (mu * mu - 1.0) + top * top, 0.0));
    float minimumDistance = top - height;
    float maximumDistance = rho + H;
    return saturate(float2(
        (distance - minimumDistance) /
            max(maximumDistance - minimumDistance, 1.0e-6),
        rho / max(H, 1.0e-6)));
}

void AtmosphereUvToTransmittanceParams(float2 uv, out float viewHeight,
                                       out float viewCosine)
{
    float bottom = AtmosphereBottomRadiusKm();
    float top = AtmosphereTopRadiusKm();
    float H = sqrt(max(top * top - bottom * bottom, 0.0));
    float rho = H * saturate(uv.y);
    viewHeight = sqrt(rho * rho + bottom * bottom);
    float minimumDistance = top - viewHeight;
    float maximumDistance = rho + H;
    float distance = minimumDistance + saturate(uv.x) *
                     (maximumDistance - minimumDistance);
    viewCosine = distance <= 1.0e-6 ? 1.0 :
        (H * H - rho * rho - distance * distance) /
        (2.0 * viewHeight * distance);
    viewCosine = clamp(viewCosine, -1.0, 1.0);
}

float3 SampleAtmosphereTransmittanceToSun(float3 position,
                                          float3 sunDirection)
{
    float2 planetHit = AtmosphereRaySphere(
        position, sunDirection, AtmosphereBottomRadiusKm());
    if (planetHit.x >= 0.0)
        return 0.0.xxx;
    float height = length(position);
    float cosine = dot(position / max(height, 1.0e-6), sunDirection);
    float2 uv = AtmosphereTransmittanceParamsToUv(height, cosine);
    return atmosphereTransmittanceLut.SampleLevel(
        atmosphereLinearClampSampler, uv, 0).rgb;
}

float2 AtmosphereMultiScatteringUv(float3 position, float3 sunDirection)
{
    float height = length(position);
    float sunCosine = dot(position / max(height, 1.0e-6), sunDirection);
    return saturate(float2(sunCosine * 0.5 + 0.5,
        (height - AtmosphereBottomRadiusKm()) /
        max(AtmosphereTopRadiusKm() - AtmosphereBottomRadiusKm(), 1.0e-6)));
}

float3 SampleAtmosphereMultipleScattering(float3 position,
                                          float3 sunDirection)
{
    return atmosphereMultiScatteringLut.SampleLevel(
        atmosphereLinearClampSampler,
        AtmosphereMultiScatteringUv(position, sunDirection), 0).rgb;
}

float2 AtmosphereSkyViewDirectionToUv(float3 viewDirection,
                                      float3 sunDirection,
                                      float viewHeight, bool hitsGround)
{
    float3 up = float3(0.0, 1.0, 0.0);
    float viewCosine = clamp(dot(viewDirection, up), -1.0, 1.0);
    float3 viewHorizontal = viewDirection - up * viewCosine;
    float3 sunHorizontal = sunDirection - up * dot(sunDirection, up);
    float viewHorizontalLength = length(viewHorizontal);
    float sunHorizontalLength = length(sunHorizontal);
    float lightViewCosine = viewHorizontalLength > 1.0e-5 &&
                            sunHorizontalLength > 1.0e-5
        ? dot(viewHorizontal / viewHorizontalLength,
              sunHorizontal / sunHorizontalLength) : 1.0;
    float bottom = AtmosphereBottomRadiusKm();
    float horizon = sqrt(max(viewHeight * viewHeight - bottom * bottom, 0.0));
    float beta = acos(clamp(horizon / max(viewHeight, 1.0e-6), -1.0, 1.0));
    float zenithHorizon = kAtmospherePi - beta;
    float2 uv;
    if (!hitsGround)
    {
        float coordinate = acos(viewCosine) / max(zenithHorizon, 1.0e-6);
        coordinate = 1.0 - sqrt(saturate(1.0 - coordinate));
        uv.y = coordinate * 0.5;
    }
    else
    {
        float coordinate = (acos(viewCosine) - zenithHorizon) /
                           max(beta, 1.0e-6);
        uv.y = sqrt(saturate(coordinate)) * 0.5 + 0.5;
    }
    uv.x = sqrt(saturate(-lightViewCosine * 0.5 + 0.5));
    return saturate(uv);
}

float3 SampleAtmosphereSkyView(float3 viewDirection, float3 sunDirection,
                               float cameraHeightKm)
{
    float viewHeight = AtmosphereBottomRadiusKm() + max(cameraHeightKm, 0.0);
    float3 position = float3(0.0, viewHeight, 0.0);
    float2 planetHit = AtmosphereRaySphere(
        position, viewDirection, AtmosphereBottomRadiusKm());
    float2 uv = AtmosphereSkyViewDirectionToUv(
        viewDirection, sunDirection, viewHeight, planetHit.x >= 0.0);
    return atmosphereSkyViewLut.SampleLevel(
        atmosphereLinearClampSampler, uv, 0).rgb;
}

float3 SampleAtmosphereSunRadiance(float altitudeKm, float3 sunDirection)
{
    float3 position = float3(0.0, AtmosphereBottomRadiusKm() +
                             max(altitudeKm, 0.0), 0.0);
    float3 physical = max(solarIrradianceAndMultiplier.xyz, 0.0.xxx) *
        SampleAtmosphereTransmittanceToSun(position, sunDirection);
    return physical * max(sunTintAndGroundBounce.xyz, 0.0.xxx) *
           max(solarIrradianceAndMultiplier.w, 0.0);
}

float AtmosphereAerialTextureDepth(float distanceMeters)
{
    float distanceKm = max(distanceMeters, 0.0) * 0.001;
    return sqrt(saturate(distanceKm /
        max(ozoneLayerTurbidityAerialDistance.w, 1.0e-6)));
}

float3 SampleAtmosphereAerialRadiance(float2 uv, float distanceMeters)
{
    return atmosphereAerialRadianceLut.SampleLevel(
        atmosphereLinearClampSampler,
        float3(saturate(uv), AtmosphereAerialTextureDepth(distanceMeters)),
        0).rgb;
}

float3 SampleAtmosphereAerialTransmittance(float2 uv, float distanceMeters)
{
    return atmosphereAerialTransmittanceLut.SampleLevel(
        atmosphereLinearClampSampler,
        float3(saturate(uv), AtmosphereAerialTextureDepth(distanceMeters)),
        0).rgb;
}

float3 ComposeStage14Atmosphere(
    float2 uv, float3 rayDirection, bool hasGeometry,
    float3 litSurface, float sceneDistance,
    float3 cloudScattering, float cloudTransmittance,
    float cloudRepresentativeDepth)
{
    float3 clearBackground;
    if (hasGeometry)
    {
        float3 sceneAir = SampleAtmosphereAerialRadiance(uv, sceneDistance);
        float3 sceneAirT = SampleAtmosphereAerialTransmittance(
            uv, sceneDistance);
        clearBackground = sceneAir + sceneAirT * max(litSurface, 0.0.xxx);
    }
    else
    {
        clearBackground = SampleAtmosphereSkyView(
            rayDirection, AtmosphereSunDirection(),
            sunDirectionAndCameraHeight.w);
    }

    float safeCloudDepth = max(cloudRepresentativeDepth, 0.0);
    float3 cloudAir = SampleAtmosphereAerialRadiance(uv, safeCloudDepth);
    float3 cloudAirT = SampleAtmosphereAerialTransmittance(uv, safeCloudDepth);
    float safeCloudT = saturate(cloudTransmittance);
    return cloudAir + cloudAirT * max(cloudScattering, 0.0.xxx) +
           safeCloudT * (clearBackground - cloudAir);
}

#endif
