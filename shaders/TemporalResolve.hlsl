// Half-resolution 구름 결과를 full-resolution history와 결합한다.

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
};

cbuffer TemporalCB : register(b2)
{
    float4x4 previousViewProj;
    float3 previousCameraPos;
    float previousTime;
    float2 windDeltaWorld;
    float historyWeight;
    uint historyValid;
};

Texture2D<float3> currentColor : register(t0);
Texture2D<float> currentDepth : register(t1);
Texture2D<float3> previousColor : register(t2);
Texture2D<float> previousDepth : register(t3);
SamplerState linearClampSampler : register(s0);

struct ResolveOutput
{
    float4 color : SV_TARGET0;
    float depth : SV_TARGET1;
};

float3 CurrentRay(float2 uv)
{
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0) + rayJitterNdc;
    float4 nearH = mul(float4(ndc, 0.0, 1.0), invViewProj);
    float4 farH = mul(float4(ndc, 1.0, 1.0), invViewProj);
    return normalize(farH.xyz / farH.w - nearH.xyz / nearH.w);
}

ResolveOutput main(VSOut input)
{
    uint halfWidth, halfHeight;
    currentColor.GetDimensions(halfWidth, halfHeight);
    int2 halfSize = int2(halfWidth, halfHeight);
    int2 halfPixel = clamp(int2(input.uv * halfSize), int2(0, 0), halfSize - 1);
    float2 halfUv = (float2(halfPixel) + 0.5) / float2(halfSize);

    float3 current = currentColor.SampleLevel(linearClampSampler, input.uv, 0);
    float depth = currentDepth.Load(int3(halfPixel, 0));
    float3 neighborhoodMin = float3(65504.0, 65504.0, 65504.0);
    float3 neighborhoodMax = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            int2 samplePixel = clamp(halfPixel + int2(x, y), int2(0, 0), halfSize - 1);
            float3 sampleColor = currentColor.Load(int3(samplePixel, 0));
            neighborhoodMin = min(neighborhoodMin, sampleColor);
            neighborhoodMax = max(neighborhoodMax, sampleColor);
        }
    }

    float valid = historyValid != 0 && depth > 0.0 ? 1.0 : 0.0;
    // depth는 half pixel의 jittered ray에서 기록됐으므로 같은 ray로 위치를 복원한다.
    float3 worldPoint = cameraPos + CurrentRay(halfUv) * depth;
    worldPoint.xz += windDeltaWorld;
    float4 previousClip = mul(float4(worldPoint, 1.0), previousViewProj);
    float2 previousUv = previousClip.xy / max(previousClip.w, 1.0e-5);
    previousUv = previousUv * float2(0.5, -0.5) + 0.5;
    valid *= step(0.0, previousClip.w);
    valid *= step(0.0, previousUv.x) * step(previousUv.x, 1.0);
    valid *= step(0.0, previousUv.y) * step(previousUv.y, 1.0);

    uint historyWidth, historyHeight;
    previousDepth.GetDimensions(historyWidth, historyHeight);
    int2 historySize = int2(historyWidth, historyHeight);
    int2 historyPixel = clamp(int2(previousUv * historySize), int2(0, 0), historySize - 1);
    float storedDepth = previousDepth.Load(int3(historyPixel, 0));
    float expectedDepth = length(worldPoint - previousCameraPos);
    float depthTolerance = max(0.1, expectedDepth * 0.05);
    valid *= step(abs(storedDepth - expectedDepth), depthTolerance);

    float3 history = previousColor.SampleLevel(linearClampSampler, previousUv, 0);
    history = clamp(history, neighborhoodMin, neighborhoodMax);
    ResolveOutput output;
    output.color = float4(lerp(current, history, saturate(historyWeight) * valid), 1.0);
    output.depth = depth;
    return output;
}
