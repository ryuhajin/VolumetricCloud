// ============================================================================
//  Stage14ToneMap.hlsl - HDR composite를 최종 R8G8B8A8_UNORM으로 출력
// ============================================================================
#include "Stage14Atmosphere.hlsli"

Texture2D<float4> hdrCompositeTexture : register(t0);
SamplerState toneLinearClampSampler : register(s0);

struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float2 CctToXy(float kelvin)
{
    float t = clamp(kelvin, 1667.0, 25000.0);
    float x = t <= 4000.0
        ? -0.2661239e9 / (t * t * t) - 0.2343580e6 / (t * t) +
          0.8776956e3 / t + 0.179910
        : -3.0258469e9 / (t * t * t) + 2.1070379e6 / (t * t) +
          0.2226347e3 / t + 0.240390;
    float y;
    if (t <= 2222.0)
        y = -1.1063814 * x * x * x - 1.34811020 * x * x +
            2.18555832 * x - 0.20219683;
    else if (t <= 4000.0)
        y = -0.9549476 * x * x * x - 1.37418593 * x * x +
            2.09137015 * x - 0.16748867;
    else
        y = 3.0817580 * x * x * x - 5.87338670 * x * x +
            3.75112997 * x - 0.37001483;
    return float2(x, y);
}

float3 XyToXyz(float2 xy)
{
    return float3(xy.x / max(xy.y, 1.0e-6), 1.0,
                  (1.0 - xy.x - xy.y) / max(xy.y, 1.0e-6));
}

float3 BradfordWhiteBalance(float3 linearRgb, float kelvin)
{
    const float3x3 rgbToXyz = float3x3(
        0.4124564, 0.3575761, 0.1804375,
        0.2126729, 0.7151522, 0.0721750,
        0.0193339, 0.1191920, 0.9503041);
    const float3x3 xyzToRgb = float3x3(
         3.2404542, -1.5371385, -0.4985314,
        -0.9692660,  1.8760108,  0.0415560,
         0.0556434, -0.2040259,  1.0572252);
    const float3x3 bradford = float3x3(
         0.8951,  0.2664, -0.1614,
        -0.7502,  1.7135,  0.0367,
         0.0389, -0.0685,  1.0296);
    const float3x3 inverseBradford = float3x3(
         0.9869929, -0.1470543,  0.1599627,
         0.4323053,  0.5183603,  0.0492912,
        -0.0085287,  0.0400428,  0.9684867);
    float3 sourceCone = mul(bradford, XyToXyz(CctToXy(kelvin)));
    float3 targetCone = mul(bradford, XyToXyz(CctToXy(6500.0)));
    float3 cone = mul(bradford, mul(rgbToXyz, linearRgb));
    cone *= targetCone / max(sourceCone, 1.0e-6.xxx);
    return mul(xyzToRgb, mul(inverseBradford, cone));
}

float3 AcesFitted(float3 color)
{
    float3 x = max(color, 0.0.xxx);
    return saturate((x * (2.51 * x + 0.03)) /
                    (x * (2.43 * x + 0.59) + 0.14));
}

float3 LegacyShoulder(float3 color)
{
    float3 safeColor = max(color, 0.0.xxx);
    float peak = max(safeColor.r, max(safeColor.g, safeColor.b));
    if (peak <= 0.8)
        return safeColor;
    float excess = peak - 0.8;
    float mappedPeak = 0.8 + excess * 0.2 / (excess + 0.2);
    return safeColor * (mappedPeak / max(peak, 1.0e-6));
}

float3 LinearToSrgb(float3 linearColor)
{
    float3 low = 12.92 * linearColor;
    float3 high = 1.055 * pow(max(linearColor, 0.0.xxx), 1.0 / 2.4) - 0.055;
    return lerp(high, low, linearColor <= 0.0031308.xxx);
}

float DitherNoise(uint2 pixel)
{
    uint value = pixel.x * 1664525u + pixel.y * 1013904223u + 374761393u;
    value = (value ^ (value >> 13u)) * 1274126177u;
    return (float)(value & 1023u) / 1023.0 - 0.5;
}

float3 SelectDebugChannel(float3 color)
{
    if (modeFlags.w == 1u)
        return color.rrr;
    if (modeFlags.w == 2u)
        return color.ggg;
    if (modeFlags.w == 3u)
        return color.bbb;
    return color;
}

float3 ValidateAndExposeDebug(float3 raw, bool bounded)
{
    if (any(isnan(raw)) || any(isinf(raw)))
        return float3(1.0, 0.0, 1.0);
    if (any(raw < 0.0.xxx))
        return float3(1.0, 0.0, 0.0);
    if (any(raw > 65504.0.xxx))
        return float3(1.0, 1.0, 0.0);
    float3 selected = SelectDebugChannel(raw);
    return bounded ? saturate(selected) :
        1.0.xxx - exp(-max(selected, 0.0.xxx) *
                      max(groundAlbedoAndDebugExposure.w, 0.001));
}

float4 Stage14DebugOutput(VSOut input, float3 hdr)
{
    uint view = modeFlags.z;
    float2 uv = saturate(input.uv);
    float3 raw = 0.0.xxx;
    bool bounded = false;
    float2 lookup = float2(0.5, 0.5);
    float2 lookupSize = float2(1.0, 1.0);
    if (view == 1u || view == 2u || view == 3u)
    {
        float3 transmittance = atmosphereTransmittanceLut.SampleLevel(
            atmosphereLinearClampSampler, uv, 0).rgb;
        raw = view == 1u ? transmittance :
              (view == 2u ? 1.0.xxx - transmittance :
               -log(max(transmittance, 1.0e-6.xxx)));
        bounded = view != 3u;
        float viewHeight = AtmosphereBottomRadiusKm() +
                           max(sunDirectionAndCameraHeight.w, 0.0);
        lookup = AtmosphereTransmittanceParamsToUv(
            viewHeight, AtmosphereSunDirection().y);
        lookupSize = transmittanceMultiSize.xy;
    }
    else if (view == 4u)
    {
        raw = atmosphereMultiScatteringLut.SampleLevel(
            atmosphereLinearClampSampler, uv, 0).rgb;
        lookup = float2(AtmosphereSunDirection().y * 0.5 + 0.5,
            sunDirectionAndCameraHeight.w /
            max(AtmosphereTopRadiusKm() - AtmosphereBottomRadiusKm(), 1.0e-6));
        lookupSize = transmittanceMultiSize.zw;
    }
    else if (view == 5u)
    {
        raw = atmosphereSkyViewLut.SampleLevel(
            atmosphereLinearClampSampler, uv, 0).rgb;
        lookup = float2(0.5, 0.5);
        lookupSize = skyViewIrradianceSize.xy;
    }
    else if (view == 6u)
    {
        raw = atmosphereSkyIrradianceLut.SampleLevel(
            atmosphereLinearClampSampler, uv, 0).rgb;
        lookup = float2(AtmosphereSunDirection().y * 0.5 + 0.5,
            sunDirectionAndCameraHeight.w /
            max(AtmosphereTopRadiusKm() - AtmosphereBottomRadiusKm(), 1.0e-6));
        lookupSize = skyViewIrradianceSize.zw;
    }
    else if (view == 7u || view == 8u || view == 12u)
    {
        float slice = (float(aerialDebugGeneration.y) + 0.5) /
                      max((float)aerialDebugGeneration.x, 1.0);
        raw = view == 8u
            ? atmosphereAerialTransmittanceLut.SampleLevel(
                atmosphereLinearClampSampler, float3(uv, slice), 0).rgb
            : atmosphereAerialRadianceLut.SampleLevel(
                atmosphereLinearClampSampler, float3(uv, slice), 0).rgb;
        bounded = view == 8u;
        lookupSize = float2((float)aerialDebugGeneration.x,
                            (float)aerialDebugGeneration.x);
    }
    else if (view == 9u)
    {
        raw = SampleAtmosphereSunRadiance(
            sunDirectionAndCameraHeight.w, AtmosphereSunDirection());
    }
    else if (view == 13u)
    {
        raw = hdr;
    }
    else
    {
        return float4(-1.0, -1.0, -1.0, 0.0);
    }
    float3 displayed = ValidateAndExposeDebug(raw, bounded);
    if (view >= 1u && view <= 8u)
    {
        float2 pixelDistance = abs(uv - saturate(lookup)) * lookupSize;
        if (pixelDistance.x < 0.75 || pixelDistance.y < 0.75)
            displayed = float3(1.0, 1.0, 1.0) - displayed;
    }
    return float4(displayed, 1.0);
}

float4 main(VSOut input) : SV_TARGET
{
    float3 hdr = max(hdrCompositeTexture.SampleLevel(
        toneLinearClampSampler, input.uv, 0).rgb, 0.0.xxx);
    float4 debugOutput = Stage14DebugOutput(input, hdr);
    if (debugOutput.a > 0.5)
        return debugOutput;
    hdr *= exp2(clamp(toneAndTime.x, -8.0, 8.0));
    hdr = max(BradfordWhiteBalance(
        hdr, clamp(toneAndTime.y, 3500.0, 10000.0)), 0.0.xxx);
    float3 mapped = modeFlags.y == 0u ? AcesFitted(hdr) :
                    (modeFlags.y == 2u ? LegacyShoulder(hdr) : hdr);
    float3 srgb = LinearToSrgb(saturate(mapped));
    srgb += DitherNoise((uint2)input.position.xy) / 255.0;
    return float4(saturate(srgb), 1.0);
}
