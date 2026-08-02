// 절차식 노이즈를 재사용 가능한 3D 텍스처로 캐시한다.
#include "CloudNoise.hlsli"

// base/detail 모두 RGBA8이며, base는 Perlin-Worley와 Worley 밴드 3개를 저장한다.
RWTexture3D<float4> outputBase   : register(u0); // R8G8B8A8_UNORM
RWStructuredBuffer<uint> seamTestResult : register(u1); // CSSeamTest 전용 (RunCodeTests가 u1에 바인딩)
RWTexture3D<float4> outputDetail : register(u2); // R8G8B8A8_UNORM (옥타브 R/G/B/A)
RWTexture2D<float4> outputWeather : register(u3); // R8G8B8A8_UNORM (coverage/type/base/thickness)
RWTexture2D<float4> outputPlacement : register(u4); // R8G8B8A8_UNORM (support/radius/height/profile)

cbuffer NoiseVolumeGenerationCB : register(b2)
{
    uint volumeSize;
    uint generationKind; // (사용 안 함: 엔트리 자체가 base/detail 구분) — volumeSize만 사용
    uint2 _generationPad;
};

// base: Perlin-Worley + 저/중/고주파 Worley 밴드
[numthreads(4, 4, 4)]
void CSBase(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= volumeSize)) return;
    float3 uvw = (float3(id) + 0.5) / volumeSize;
    outputBase[id] = BaseNoiseChannels(uvw);
}

// detail: 침식용 Worley 옥타브 4개를 채널로 분리 저장 (렌더 시점에 가중치로 재합성)
[numthreads(4, 4, 4)]
void CSDetail(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= volumeSize)) return;
    float3 uvw = (float3(id) + 0.5) / volumeSize;
    outputDetail[id] = DetailNoiseOctaves(uvw);
}

[numthreads(8, 8, 1)]
void CSWeather(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= volumeSize)) return;
    float2 uv = (float2(id.xy) + 0.5) / volumeSize;
    outputWeather[id.xy] = GenerateWeatherMap(uv);
    outputPlacement[id.xy] = GeneratePlacementMap(uv);
}

[numthreads(64, 1, 1)]
void CSSeamTest(uint3 id : SV_DispatchThreadID)
{
    float3 p = frac(float3(
        id.x * 0.61803398875,
        id.x * 0.41421356237 + 0.17,
        id.x * 0.73205080757 + 0.31));
    float baseTestPeriod = max(1.0, (float)basePeriod);
    float detailTestPeriod = max(1.0, (float)detailPeriod);
    float3 basePoint = p * baseTestPeriod;
    float3 detailPoint = p * detailTestPeriod;
    float error = EvaluatePeriodicSeamError(p);
    error = max(error, abs(PeriodicValueNoise3D(basePoint, baseTestPeriod) -
                           PeriodicValueNoise3D(basePoint + float3(baseTestPeriod, 0, 0), baseTestPeriod)));
    error = max(error, abs(PeriodicWorley3D(detailPoint, detailTestPeriod) -
                           PeriodicWorley3D(detailPoint + float3(0, 0, detailTestPeriod), detailTestPeriod)));
    error = max(error, abs(PeriodicFBM(basePoint, baseOctaves, baseTestPeriod) -
                           PeriodicFBM(basePoint + float3(baseTestPeriod, 0, 0), baseOctaves, baseTestPeriod)));
    error = max(error, abs(PeriodicWorleyFBM(detailPoint, detailOctaves, detailTestPeriod) -
                           PeriodicWorleyFBM(detailPoint + float3(0, 0, detailTestPeriod), detailOctaves, detailTestPeriod)));
    float4 weather = GenerateWeatherMap(p.xy);
    float4 weatherX = GenerateWeatherMap(p.xy + float2(1.0, 0.0));
    float4 weatherY = GenerateWeatherMap(p.xy + float2(0.0, 1.0));
    error = max(error, max(max(abs(weather.r - weatherX.r), abs(weather.g - weatherX.g)),
                           max(abs(weather.b - weatherX.b), abs(weather.a - weatherX.a))));
    error = max(error, max(max(abs(weather.r - weatherY.r), abs(weather.g - weatherY.g)),
                           max(abs(weather.b - weatherY.b), abs(weather.a - weatherY.a))));
    float4 placement = GeneratePlacementMap(p.xy);
    float4 placementX = GeneratePlacementMap(p.xy + float2(1.0, 0.0));
    float4 placementY = GeneratePlacementMap(p.xy + float2(0.0, 1.0));
    error = max(error, max(max(abs(placement.r - placementX.r), abs(placement.g - placementX.g)),
                           max(abs(placement.b - placementX.b), abs(placement.a - placementX.a))));
    error = max(error, max(max(abs(placement.r - placementY.r), abs(placement.g - placementY.g)),
                           max(abs(placement.b - placementY.b), abs(placement.a - placementY.a))));
    uint errorBits = asuint(error);
    InterlockedMax(seamTestResult[0], errorBits);
}
