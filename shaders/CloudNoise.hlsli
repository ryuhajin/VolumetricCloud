// ============================================================================
//  CloudNoise.hlsli  —  periodic 3D noise와 구름 밀도 공통 함수
// ============================================================================
#ifndef CLOUD_NOISE_HLSLI
#define CLOUD_NOISE_HLSLI

cbuffer CloudCB : register(b1)
{
    float noiseWorldScale;
    int basePeriod;
    int detailPeriod;
    float densityMultiplier;

    float noiseCutoffThreshold;
    float erosionStrength;
    float bottomFade;
    float topFade;

    float2 windDirection;
    float windSpeed;
    float seed;

    int baseOctaves;
    int detailOctaves;
    int renderMode;
    int useTextureCache;

    int showBounds;
    int lightSteps;
    float sunAzimuth;
    float sunElevation;

    float sunIntensity;
    float ambientIntensity;
    float phaseG;
    float lightAbsorption;
};

Texture3D<float4> baseNoiseTexture : register(t0);
Texture3D<float4> detailNoiseTexture : register(t1);
SamplerState noiseVolumeSampler : register(s0);

float3 WrapCell(float3 cell, float period)
{
    return cell - floor(cell / period) * period;
}

float Hash31(float3 p)
{
    p = frac((p + seed * 0.071) * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return frac((p.x + p.y) * p.z);
}

float PeriodicValueNoise3D(float3 p, float period)
{
    float3 cell = floor(p);
    float3 f = frac(p);
    float3 u = f * f * (3.0 - 2.0 * f);

    float n000 = Hash31(WrapCell(cell + float3(0, 0, 0), period));
    float n100 = Hash31(WrapCell(cell + float3(1, 0, 0), period));
    float n010 = Hash31(WrapCell(cell + float3(0, 1, 0), period));
    float n110 = Hash31(WrapCell(cell + float3(1, 1, 0), period));
    float n001 = Hash31(WrapCell(cell + float3(0, 0, 1), period));
    float n101 = Hash31(WrapCell(cell + float3(1, 0, 1), period));
    float n011 = Hash31(WrapCell(cell + float3(0, 1, 1), period));
    float n111 = Hash31(WrapCell(cell + float3(1, 1, 1), period));

    float x00 = lerp(n000, n100, u.x);
    float x10 = lerp(n010, n110, u.x);
    float x01 = lerp(n001, n101, u.x);
    float x11 = lerp(n011, n111, u.x);
    return lerp(lerp(x00, x10, u.y), lerp(x01, x11, u.y), u.z);
}

float PeriodicFBM(float3 p, int octaves, float period)
{
    float sum = 0.0;
    float amplitude = 0.5;
    float norm = 0.0;
    [unroll]
    for (int i = 0; i < 5; ++i)
    {
        if (i >= octaves) break;
        sum += PeriodicValueNoise3D(p, period) * amplitude;
        norm += amplitude;
        p *= 2.0;
        period *= 2.0;
        amplitude *= 0.5;
    }
    return sum / max(norm, 0.0001);
}

float PeriodicWorley3D(float3 p, float period)
{
    float3 cell = floor(p);
    float3 local = frac(p);
    float nearest = 2.0;

    [unroll]
    for (int z = -1; z <= 1; ++z)
    [unroll]
    for (int y = -1; y <= 1; ++y)
    [unroll]
    for (int x = -1; x <= 1; ++x)
    {
        float3 offset = float3(x, y, z);
        float3 wrappedCell = WrapCell(cell + offset, period);
        float3 feature = float3(
            Hash31(wrappedCell + 1.7),
            Hash31(wrappedCell + 9.2),
            Hash31(wrappedCell + 17.1));
        nearest = min(nearest, length(offset + feature - local));
    }
    return saturate(nearest / 1.15);
}

float PeriodicWorleyFBM(float3 p, int octaves, float period)
{
    float sum = 0.0;
    float amplitude = 0.6;
    float norm = 0.0;
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        if (i >= octaves) break;
        sum += PeriodicWorley3D(p, period) * amplitude;
        norm += amplitude;
        p *= 2.0;
        period *= 2.0;
        amplitude *= 0.45;
    }
    return sum / max(norm, 0.0001);
}

float PeriodicPerlinWorley(float3 p, float period)
{
    float perlin = PeriodicFBM(p, baseOctaves, period);
    float cellularMass = 1.0 - PeriodicWorley3D(p, period);
    return saturate(lerp(perlin, perlin * cellularMass + perlin * 0.35, 0.42));
}

float EffectiveBasePeriod()
{
    return max(1.0, (float)basePeriod * round(max(noiseWorldScale, 1.0)));
}

float EffectiveDetailPeriod()
{
    return max(1.0, (float)detailPeriod * round(max(noiseWorldScale, 1.0)));
}

float BaseNoiseAt(float3 tileUVW)
{
    float period = EffectiveBasePeriod();
    return PeriodicPerlinWorley(tileUVW * period, period);
}

float DetailNoiseAt(float3 tileUVW)
{
    float period = EffectiveDetailPeriod();
    return PeriodicWorleyFBM(tileUVW * period + 19.0, detailOctaves, period);
}

// 캐시 굽기용: Worley 옥타브 4개를 채널로 분리해 반환 (R/G/B/A = 옥타브 0..3).
// PeriodicWorleyFBM 의 내부 진행(p*=2, period*=2)과 동일한 주파수를 채널마다 보존.
float4 DetailNoiseOctaves(float3 tileUVW)
{
    float period = EffectiveDetailPeriod();
    float3 p = tileUVW * period + 19.0;
    float o0 = PeriodicWorley3D(p,       period);
    float o1 = PeriodicWorley3D(p * 2.0, period * 2.0);
    float o2 = PeriodicWorley3D(p * 4.0, period * 4.0);
    float o3 = PeriodicWorley3D(p * 8.0, period * 8.0);
    return float4(o0, o1, o2, o3);
}

// 렌더 시점 옥타브 재합성. 기본값은 기존 PeriodicWorleyFBM(진폭 0.6, ×0.45)과 동일한 룩.
// 이 상수만 바꾸면 재굽기 없이 굵은 침식/미세 필라멘트 비율을 실시간으로 조절할 수 있다.
static const float4 kDetailOctaveAmplitude = float4(0.6, 0.27, 0.1215, 0.054675);
float DetailErosionFromChannels(float4 octaves)
{
    // detailOctaves 개수만큼만 사용 → 절차식 경로와 룩을 맞춘다.
    float4 amp = kDetailOctaveAmplitude;
    if (detailOctaves < 4) amp.w = 0.0;
    if (detailOctaves < 3) amp.z = 0.0;
    if (detailOctaves < 2) amp.y = 0.0;
    float norm = amp.x + amp.y + amp.z + amp.w;
    return dot(octaves, amp) / max(norm, 1e-4);
}

float HeightGradient(float height01)
{
    float bottom = smoothstep(0.0, max(bottomFade, 0.001), height01);
    float top = 1.0 - smoothstep(1.0 - max(topFade, 0.001), 1.0, height01);
    return saturate(bottom * top);
}

float4 ShapeCloudComponents(float base, float detail, float height)
{
    float shaped = saturate((base - noiseCutoffThreshold) /
                            max(1.0 - noiseCutoffThreshold, 0.001));
    shaped = saturate(shaped - detail * erosionStrength * (0.35 + 0.65 * shaped));
    return float4(base, detail, height, shaped * height);
}

float3 AnimatedUVW(float3 uvw, float sampleTime)
{
    float2 direction = normalize(windDirection + 0.0001);
    float2 wind = direction * (sampleTime * windSpeed);
    float3 animated = uvw + float3(wind.x, 0.0, wind.y);
    animated.xz = frac(animated.xz);
    return animated;
}

float4 EvaluateProceduralCloudComponents(float3 uvw, float sampleTime)
{
    float3 animated = AnimatedUVW(uvw, sampleTime);
    return ShapeCloudComponents(
        BaseNoiseAt(animated),
        DetailNoiseAt(animated),
        HeightGradient(uvw.y));
}

float4 EvaluateCachedCloudComponents(float3 uvw, float sampleTime)
{
    float3 animated = AnimatedUVW(uvw, sampleTime);
    float base = baseNoiseTexture.SampleLevel(noiseVolumeSampler, animated, 0).r;   // R8: 실루엣
    float4 detailOctaves = detailNoiseTexture.SampleLevel(noiseVolumeSampler, animated, 0); // RGBA8: 옥타브
    float detail = DetailErosionFromChannels(detailOctaves);
    return ShapeCloudComponents(base, detail, HeightGradient(uvw.y));
}

float4 EvaluateCloudComponents(float3 uvw, float sampleTime)
{
    return useTextureCache != 0
        ? EvaluateCachedCloudComponents(uvw, sampleTime)
        : EvaluateProceduralCloudComponents(uvw, sampleTime);
}

float EvaluateCloudDensity(float3 uvw, float sampleTime)
{
    return EvaluateCloudComponents(uvw, sampleTime).w * densityMultiplier;
}

float EvaluatePeriodicSeamError(float3 uvw)
{
    float base = BaseNoiseAt(uvw);
    float detail = DetailNoiseAt(uvw);
    float error = 0.0;
    error = max(error, abs(base - BaseNoiseAt(uvw + float3(1, 0, 0))));
    error = max(error, abs(base - BaseNoiseAt(uvw + float3(0, 0, 1))));
    error = max(error, abs(detail - DetailNoiseAt(uvw + float3(1, 0, 0))));
    error = max(error, abs(detail - DetailNoiseAt(uvw + float3(0, 0, 1))));
    return error;
}

float HenyeyGreenstein(float cosTheta)
{
    float g = clamp(phaseG, -0.9, 0.9);
    float g2 = g * g;
    return (1.0 - g2) / max(pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5), 0.001);
}

#endif
