// 절차식 노이즈를 재사용 가능한 3D 텍스처로 캐시한다.
#include "CloudNoise.hlsli"

// base 볼륨은 단일 값(R8), detail 볼륨은 Worley 옥타브 4개(RGBA8)를 채운다.
// 형식이 다르므로 UAV 타입도 다르고, 각각 별도 엔트리로 컴파일해 볼륨별로 바인딩한다.
RWTexture3D<float>  outputBase   : register(u0); // R8_UNORM
RWStructuredBuffer<uint> seamTestResult : register(u1); // CSSeamTest 전용 (RunCodeTests가 u1에 바인딩)
RWTexture3D<float4> outputDetail : register(u2); // R8G8B8A8_UNORM (옥타브 R/G/B/A)

cbuffer NoiseVolumeGenerationCB : register(b2)
{
    uint volumeSize;
    uint generationKind; // (사용 안 함: 엔트리 자체가 base/detail 구분) — volumeSize만 사용
    uint2 _generationPad;
};

// base: 구름 실루엣용 Perlin-Worley 단일 값
[numthreads(4, 4, 4)]
void CSBase(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= volumeSize)) return;
    float3 uvw = (float3(id) + 0.5) / volumeSize;
    outputBase[id] = BaseNoiseAt(uvw);
}

// detail: 침식용 Worley 옥타브 4개를 채널로 분리 저장 (렌더 시점에 가중치로 재합성)
[numthreads(4, 4, 4)]
void CSDetail(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= volumeSize)) return;
    float3 uvw = (float3(id) + 0.5) / volumeSize;
    outputDetail[id] = DetailNoiseOctaves(uvw);
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
    uint errorBits = asuint(error);
    InterlockedMax(seamTestResult[0], errorBits);
}
