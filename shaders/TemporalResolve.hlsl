// Half-resolution cloud scattering/metadata를 외곽 보존 복원 후 full-resolution history와 결합한다.

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

cbuffer cbCamera : register(b0)
{
    float4x4 invViewProj;
    float3 cameraPos;
    float time;
    float2 rayJitterNdc;
    float2 renderSize;
    uint temporalOutput;
    uint3 _cameraPad;
};

cbuffer TemporalCB : register(b2)
{
    float4x4 previousViewProj;
    float3 previousCameraPos;
    float previousTime;
    float2 windDeltaWorld;
    float historyWeight;
    uint historyValid;
    uint temporalDebugMode;
    uint3 _temporalPad;
};

Texture2D<float3> currentScattering : register(t0);
Texture2D<float2> currentMetadata : register(t1);
Texture2D<float3> previousScattering : register(t2);
Texture2D<float2> previousMetadata : register(t3);
SamplerState linearClampSampler : register(s0);

struct ResolveOutput
{
    float4 scattering : SV_TARGET0;
    float2 metadata : SV_TARGET1;
};

float3 CurrentRay(float2 uv)
{
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0) + rayJitterNdc;
    float4 nearH = mul(float4(ndc, 0.0, 1.0), invViewProj);
    float4 farH = mul(float4(ndc, 1.0, 1.0), invViewProj);
    return normalize(farH.xyz / farH.w - nearH.xyz / nearH.w);
}

void ReconstructCurrent(float2 uv, out float3 scattering, out float2 metadata, out int2 anchorPixel)
{
    uint width, height;
    currentScattering.GetDimensions(width, height);
    int2 size = int2(width, height);
    float2 texelPosition = uv * float2(size) - 0.5;
    int2 basePixel = int2(floor(texelPosition));
    float2 fraction = frac(texelPosition);
    anchorPixel = clamp(int2(round(texelPosition)), int2(0, 0), size - 1);
    float2 anchorMetadata = currentMetadata.Load(int3(anchorPixel, 0));
    float anchorOpacity = 1.0 - anchorMetadata.y;

    scattering = 0.0;
    metadata = 0.0;
    float totalWeight = 0.0;
    [unroll]
    for (int y = 0; y < 2; ++y)
    {
        [unroll]
        for (int x = 0; x < 2; ++x)
        {
            int2 pixel = clamp(basePixel + int2(x, y), int2(0, 0), size - 1);
            float2 sampleMetadata = currentMetadata.Load(int3(pixel, 0));
            float sampleOpacity = 1.0 - sampleMetadata.y;
            float2 axisWeight = 1.0 - abs(float2(x, y) - fraction);
            float weight = axisWeight.x * axisWeight.y;
            weight *= exp(-abs(sampleOpacity - anchorOpacity) * 32.0);

            bool anchorCloud = anchorMetadata.x > 0.0;
            bool sampleCloud = sampleMetadata.x > 0.0;
            if (anchorCloud != sampleCloud)
                weight *= 0.001;
            else if (anchorCloud)
            {
                float tolerance = max(0.1, anchorMetadata.x * 0.05);
                weight *= exp(-abs(sampleMetadata.x - anchorMetadata.x) /
                              max(tolerance, 1.0e-4) * 4.0);
            }

            scattering += currentScattering.Load(int3(pixel, 0)) * weight;
            metadata += sampleMetadata * weight;
            totalWeight += weight;
        }
    }

    if (totalWeight <= 1.0e-5)
    {
        scattering = currentScattering.Load(int3(anchorPixel, 0));
        metadata = anchorMetadata;
    }
    else
    {
        scattering /= totalWeight;
        metadata /= totalWeight;
    }
}

ResolveOutput main(VSOut input)
{
    float2 jitterUv = float2(rayJitterNdc.x * 0.5, -rayJitterNdc.y * 0.5);
    float2 currentUv = saturate(input.uv - jitterUv);
    float3 current;
    float2 metadata;
    int2 halfPixel;
    ReconstructCurrent(currentUv, current, metadata, halfPixel);
    float depth = metadata.x;

    uint halfWidth, halfHeight;
    currentScattering.GetDimensions(halfWidth, halfHeight);
    int2 halfSize = int2(halfWidth, halfHeight);
    float2 halfUv = (float2(halfPixel) + 0.5) / float2(halfSize);
    float3 neighborhoodMin = float3(65504.0, 65504.0, 65504.0);
    float3 neighborhoodMax = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            int2 samplePixel = clamp(halfPixel + int2(x, y), int2(0, 0), halfSize - 1);
            float3 sampleColor = currentScattering.Load(int3(samplePixel, 0));
            neighborhoodMin = min(neighborhoodMin, sampleColor);
            neighborhoodMax = max(neighborhoodMax, sampleColor);
        }
    }

    float valid = historyValid != 0 && depth > 0.0 ? 1.0 : 0.0;
    float3 worldPoint = cameraPos + CurrentRay(halfUv) * depth;
    worldPoint.xz += windDeltaWorld;
    float4 previousClip = mul(float4(worldPoint, 1.0), previousViewProj);
    float2 previousUv = previousClip.xy / max(previousClip.w, 1.0e-5);
    previousUv = previousUv * float2(0.5, -0.5) + 0.5;
    valid *= step(0.0, previousClip.w);
    valid *= step(0.0, previousUv.x) * step(previousUv.x, 1.0);
    valid *= step(0.0, previousUv.y) * step(previousUv.y, 1.0);

    uint historyWidth, historyHeight;
    previousMetadata.GetDimensions(historyWidth, historyHeight);
    int2 historySize = int2(historyWidth, historyHeight);
    int2 historyPixel = clamp(int2(previousUv * historySize), int2(0, 0), historySize - 1);
    float2 storedMetadata = previousMetadata.Load(int3(historyPixel, 0));
    float expectedDepth = length(worldPoint - previousCameraPos);
    float depthTolerance = max(0.1, expectedDepth * 0.05);
    valid *= step(abs(storedMetadata.x - expectedDepth), depthTolerance);

    float opacityDelta = abs(metadata.y - storedMetadata.y);
    float confidence = valid * saturate(1.0 - opacityDelta * 4.0);
    float blendWeight = saturate(historyWeight) * confidence;
    float3 history = previousScattering.SampleLevel(linearClampSampler, previousUv, 0);
    history = clamp(history, neighborhoodMin, neighborhoodMax);

    ResolveOutput output;
    output.scattering = float4(lerp(current, history, blendWeight), 1.0);
    if (temporalDebugMode == 22)
        output.scattering = float4(confidence.xxx, 1.0);
    output.metadata = float2(depth, lerp(metadata.y, storedMetadata.y, blendWeight));
    return output;
}
