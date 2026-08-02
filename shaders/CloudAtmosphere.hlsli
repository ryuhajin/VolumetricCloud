#ifndef CLOUD_ATMOSPHERE_HLSLI
#define CLOUD_ATMOSPHERE_HLSLI

float3 CloudSunDirection(float azimuthDegrees, float elevationDegrees)
{
    float azimuth = radians(azimuthDegrees);
    float elevation = radians(elevationDegrees);
    float cosElevation = cos(elevation);
    return normalize(float3(
        cosElevation * cos(azimuth),
        sin(elevation),
        cosElevation * sin(azimuth)));
}

float3 CloudSkyColor(float3 rayDirection, float3 sunDirection)
{
    float up = saturate(rayDirection.y);
    float horizonFactor = exp(-up * 5.5);
    float3 zenith = float3(0.12, 0.30, 0.58);
    float3 horizon = float3(0.58, 0.75, 0.92);
    float3 sky = lerp(zenith, horizon, horizonFactor);
    float sunAmount = saturate(dot(rayDirection, sunDirection));
    float glow = pow(sunAmount, 64.0) * 0.65 + pow(sunAmount, 1024.0) * 8.0;
    return sky + float3(1.0, 0.78, 0.52) * glow;
}

float3 CloudToneMap(float3 color)
{
    return 1.0 - exp(-max(color, 0.0));
}

#endif
