// ============================================================================
//  NoiseVolume.hlsl - 단계 13-4 periodic Base/Detail RGBA8 Texture3D 생성
// ============================================================================
#include "NoiseVolumeParameters.hlsli"

RWTexture3D<float4> outputVolume : register(u0);

uint WrapCell(int value, uint period)
{
    int safePeriod = (int)max(period, 1u);
    int result = value - (int)floor((float)value / (float)safePeriod) * safePeriod;
    return (uint)(result < 0 ? result + safePeriod : result);
}

uint HashNoiseCell(int3 cell, uint seed)
{
    uint value = seed ^ 0x9e3779b9u;
    uint3 coordinates = (uint3)cell;
    [unroll]
    for (uint index = 0u; index < 3u; ++index)
    {
        value ^= coordinates[index] + 0x9e3779b9u + (value << 6u) +
                 (value >> 2u);
        value ^= value >> 16u;
        value *= 0x7feb352du;
        value ^= value >> 15u;
        value *= 0x846ca68bu;
        value ^= value >> 16u;
    }
    return value;
}

float HashUnit(int3 cell, uint seed)
{
    return (float)(HashNoiseCell(cell, seed) & 0x00ffffffu) / 16777216.0;
}

float3 GradientFromHash(uint hash)
{
    uint selector = hash & 15u;
    float3 gradient = selector == 0u ? float3( 1, 1, 0) :
        selector == 1u ? float3(-1, 1, 0) :
        selector == 2u ? float3( 1,-1, 0) :
        selector == 3u ? float3(-1,-1, 0) :
        selector == 4u ? float3( 1, 0, 1) :
        selector == 5u ? float3(-1, 0, 1) :
        selector == 6u ? float3( 1, 0,-1) :
        selector == 7u ? float3(-1, 0,-1) :
        selector == 8u ? float3( 0, 1, 1) :
        selector == 9u ? float3( 0,-1, 1) :
        selector == 10u ? float3(0, 1,-1) : float3(0,-1,-1);
    return gradient * 0.70710678118;
}

float GradientCorner(int3 cell, float3 local, int3 offset,
                     uint period, uint seed)
{
    int3 wrapped = int3(
        WrapCell(cell.x + offset.x, period),
        WrapCell(cell.y + offset.y, period),
        WrapCell(cell.z + offset.z, period));
    return dot(GradientFromHash(HashNoiseCell(wrapped, seed)),
               local - (float3)offset);
}

float PeriodicGradientNoise(float3 p, uint period, uint seed)
{
    int3 cell = (int3)floor(p);
    float3 local = frac(p);
    float3 fade = local * local * local *
        (local * (local * 6.0 - 15.0) + 10.0);
    float x00 = lerp(GradientCorner(cell, local, int3(0,0,0), period, seed),
                     GradientCorner(cell, local, int3(1,0,0), period, seed), fade.x);
    float x10 = lerp(GradientCorner(cell, local, int3(0,1,0), period, seed),
                     GradientCorner(cell, local, int3(1,1,0), period, seed), fade.x);
    float x01 = lerp(GradientCorner(cell, local, int3(0,0,1), period, seed),
                     GradientCorner(cell, local, int3(1,0,1), period, seed), fade.x);
    float x11 = lerp(GradientCorner(cell, local, int3(0,1,1), period, seed),
                     GradientCorner(cell, local, int3(1,1,1), period, seed), fade.x);
    return lerp(lerp(x00, x10, fade.y), lerp(x01, x11, fade.y), fade.z);
}

float PeriodicWorleyDistance(float3 p, uint period, uint seed)
{
    int3 cell = (int3)floor(p);
    float3 local = frac(p);
    float nearestSquared = 4.0;
    [unroll]
    for (int z = -1; z <= 1; ++z)
    [unroll]
    for (int y = -1; y <= 1; ++y)
    [unroll]
    for (int x = -1; x <= 1; ++x)
    {
        int3 wrapped = int3(
            WrapCell(cell.x + x, period), WrapCell(cell.y + y, period),
            WrapCell(cell.z + z, period));
        float3 feature = float3(
            HashUnit(wrapped, seed + 17u), HashUnit(wrapped, seed + 59u),
            HashUnit(wrapped, seed + 101u));
        float3 delta = float3(x, y, z) + feature - local;
        nearestSquared = min(nearestSquared, dot(delta, delta));
    }
    return saturate(sqrt(nearestSquared) / 1.15);
}

float BasePerlinWorley(float3 uvw)
{
    float sum = 0.0;
    float normalization = 0.0;
    float amplitude = 0.5;
    [unroll]
    for (uint octave = 0u; octave < 4u; ++octave)
    {
        uint frequency = max(baseVolumeFrequencies[octave], 1u);
        sum += PeriodicGradientNoise(
            uvw * frequency, frequency,
            noiseVolumeSeed + octave * 173u) * amplitude;
        normalization += amplitude;
        amplitude *= 0.5;
    }
    float perlin = saturate(sum / max(normalization, 1e-6) * 0.5 + 0.5);
    uint frequency = max(baseVolumeFrequencies.x, 1u);
    float cellularMass = 1.0 - PeriodicWorleyDistance(
        uvw * frequency, frequency, noiseVolumeSeed + 211u);
    return saturate(lerp(
        perlin, perlin * cellularMass + perlin * 0.35, 0.42));
}

[numthreads(4, 4, 4)]
void CSBase(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= baseVolumeResolution.xxx))
        return;
    float3 uvw = ((float3)id + 0.5) / (float)baseVolumeResolution;
    float4 result;
    result.r = BasePerlinWorley(uvw);
    [unroll]
    for (uint channel = 0u; channel < 3u; ++channel)
    {
        uint frequency = max(baseVolumeFrequencies[channel], 1u);
        result[channel + 1u] = PeriodicWorleyDistance(
            uvw * frequency, frequency,
            noiseVolumeSeed + 307u + channel * 131u);
    }
    outputVolume[id] = saturate(result);
}

[numthreads(4, 4, 4)]
void CSDetail(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= detailVolumeResolution.xxx))
        return;
    float3 uvw = ((float3)id + 0.5) / (float)detailVolumeResolution;
    float4 result;
    [unroll]
    for (uint channel = 0u; channel < 4u; ++channel)
    {
        uint frequency = max(detailVolumeFrequencies[channel], 1u);
        result[channel] = PeriodicWorleyDistance(
            uvw * frequency + 19.0, frequency,
            noiseVolumeSeed + 911u + channel * 173u);
    }
    outputVolume[id] = saturate(result);
}
