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

    float coverage;
    float baseErosion;
    float powderStrength;
    float multiScatterStrength;

    float silverLiningStrength;
    float jitterStrength;
    int viewSteps;
    float skyExposure;

    float cloudBaseHeight;
    float cloudThickness;
    float cloudNoiseWorldSize;
    float maxMarchDistance;

    float weatherWorldSize;
    float weatherCoverageStrength;
    float weatherTypeBias;
    float heightVariation;

    float thicknessVariation;
    float horizonFadeStart;
    float horizonFadeEnd;
    float weatherSeed;

    float detailNoiseWorldSize;
    float baseNoiseVerticalSize;
    float detailNoiseVerticalSize;
    float maxViewStepLength;

    float localLightDistance;
    float cumulusGrowth;
    float anvilStrength;
    float detailErosionWidth;

    int farLightSteps;
    int boundaryRefineSteps;
    float _cloudPad0;
    float _cloudPad1;
};

Texture3D<float4> baseNoiseTexture : register(t0);
Texture3D<float4> detailNoiseTexture : register(t1);
Texture2D<float4> weatherMapTexture : register(t2);
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

float4 BaseNoiseChannels(float3 tileUVW)
{
    float period = EffectiveBasePeriod();
    float3 p = tileUVW * period;
    return float4(
        PeriodicPerlinWorley(p, period),
        PeriodicWorley3D(p, period),
        PeriodicWorley3D(p * 2.0, period * 2.0),
        PeriodicWorley3D(p * 4.0, period * 4.0));
}

float BaseNoiseAt(float3 tileUVW)
{
    return BaseNoiseChannels(tileUVW).r;
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
    // 적운은 하단이 좁고 중단에서 부풀며 상단에서 다시 부드럽게 사라진다.
    float lowerTaper = lerp(0.58, 1.0, smoothstep(0.0, 0.38, height01));
    return saturate(bottom * top * lowerTaper);
}

float BaseShapeFromChannels(float4 baseChannels, float height01, float coverageValue)
{
    float coverageOffset = (0.5 - saturate(coverageValue)) * 0.70;
    // 하단과 꼭대기를 좁히고 중단을 넓혀 수직으로 납작한 안개 모양을 피한다.
    float profileBias = lerp(0.14, -0.045, smoothstep(0.0, 0.52, height01));
    profileBias += smoothstep(0.72, 1.0, height01) * 0.12;
    float threshold = saturate(noiseCutoffThreshold + coverageOffset + profileBias);
    float shaped = saturate((baseChannels.r - threshold) / max(1.0 - threshold, 0.001));

    float worleyBands = dot(baseChannels.gba, float3(0.625, 0.25, 0.125));
    float macroBoundary = 1.0 - smoothstep(0.55, 0.95, shaped);
    return saturate(shaped - worleyBands * baseErosion * macroBoundary);
}

float4 ShapeCloudComponents(float4 baseChannels, float detail, float height, float height01)
{
    float baseShape = BaseShapeFromChannels(baseChannels, height01, coverage);
    // 고주파 detail은 코어가 아니라 경계에 집중해 실루엣을 보존하면서 솜털을 만든다.
    float boundary = 1.0 - smoothstep(0.45, 0.92, baseShape);
    float shaped = saturate(baseShape - detail * erosionStrength * boundary);
    return float4(baseShape, detail, height, shaped * height);
}

float4 GenerateWeatherMap(float2 uv)
{
    float2 p = uv;
    float low = PeriodicFBM(float3(p * 4.0 + weatherSeed * 0.013, weatherSeed * 0.031), 4, 4.0);
    float mid = PeriodicFBM(float3(p * 8.0 + float2(7.1, 3.7), weatherSeed * 0.047), 3, 8.0);
    float type = PeriodicFBM(float3(p * 3.0 + float2(2.9, 5.3), weatherSeed * 0.059), 3, 3.0);
    float baseHeight = PeriodicValueNoise3D(
        float3(p * 2.0 + float2(11.0, 17.0), weatherSeed * 0.071), 2.0);
    float thickness = PeriodicFBM(
        float3(p * 2.0 + float2(19.0, 13.0), weatherSeed * 0.083), 3, 2.0);
    float weatherCoverage = smoothstep(0.30, 0.76, low * 0.72 + mid * 0.28);
    return saturate(float4(weatherCoverage, type, baseHeight, thickness));
}

float4 SampleWeather(float2 worldXZ)
{
    float size = max(weatherWorldSize, 1.0);
    // Showcase 원점이 충분한 coverage 영역에 오도록 결정적 월드 오프셋을 둔다.
    return weatherMapTexture.SampleLevel(
        noiseVolumeSampler, frac(worldXZ / size + float2(0.3125, 0.171875)), 0);
}

void LocalCloudLayerBounds(float4 weather, out float localBase, out float localTop)
{
    localBase = cloudBaseHeight + (weather.b * 2.0 - 1.0) * max(heightVariation, 0.0);
    float localThickness = max(cloudThickness, 0.1) *
        max(0.2, 1.0 + (weather.a * 2.0 - 1.0) * saturate(thicknessVariation));
    float cloudType = smoothstep(
        0.35, 0.75, saturate(weather.g + weatherTypeBias - 0.5));
    localThickness *= lerp(1.0, max(cumulusGrowth, 1.0), cloudType);
    localTop = localBase + localThickness;
}

float WeatherCoverage(float4 weather)
{
    return saturate(coverage + (weather.r - 0.5) * weatherCoverageStrength);
}

float WeatherPotential(float4 weather)
{
    return smoothstep(0.28, 0.48, WeatherCoverage(weather));
}

float2 WindOffsetWorld(float sampleTime)
{
    float2 direction = normalize(windDirection + 0.0001);
    return direction * (sampleTime * windSpeed);
}

float3 WorldToBaseUVW(float3 worldPosition, float4 weather, float sampleTime)
{
    float localBase, localTop;
    LocalCloudLayerBounds(weather, localBase, localTop);
    float horizontalSize = max(cloudNoiseWorldSize, 0.1);
    float verticalSize = max(baseNoiseVerticalSize, 0.1);
    float2 animatedXZ = worldPosition.xz + WindOffsetWorld(sampleTime);
    return frac(float3(animatedXZ.x / horizontalSize,
                       (worldPosition.y - localBase) / verticalSize,
                       animatedXZ.y / horizontalSize));
}

float3 WorldToDetailUVW(float3 worldPosition, float4 weather, float sampleTime)
{
    float localBase, localTop;
    LocalCloudLayerBounds(weather, localBase, localTop);
    float horizontalSize = max(detailNoiseWorldSize, 0.05);
    float verticalSize = max(detailNoiseVerticalSize, 0.05);
    float2 animatedXZ = worldPosition.xz + WindOffsetWorld(sampleTime);
    return frac(float3(animatedXZ.x / horizontalSize,
                       (worldPosition.y - localBase) / verticalSize,
                       animatedXZ.y / horizontalSize));
}

float3 WorldToLayerUVW(float3 worldPosition, float4 weather)
{
    return WorldToBaseUVW(worldPosition, weather, 0.0);
}

float LayerHeightGradient(float height01, float cloudType)
{
    float stratus = smoothstep(0.0, max(bottomFade, 0.001), height01) *
        (1.0 - smoothstep(1.0 - max(topFade, 0.001), 1.0, height01));
    float cumulus = HeightGradient(height01);
    float type = saturate(cloudType + weatherTypeBias - 0.5);
    return lerp(stratus, cumulus, type);
}

float3 AnimatedUVW(float3 uvw, float sampleTime);

float4 EvaluateLayerCloudComponents(float3 worldPosition, float4 weather, float sampleTime)
{
    float localBase, localTop;
    LocalCloudLayerBounds(weather, localBase, localTop);
    float height01 = saturate((worldPosition.y - localBase) / max(localTop - localBase, 0.1));
    float insideLayer = step(localBase, worldPosition.y) * step(worldPosition.y, localTop);
    float potential = WeatherPotential(weather) * insideLayer;
    if (potential <= 0.0)
        return 0.0;

    float3 baseUVW = WorldToBaseUVW(worldPosition, weather, sampleTime);
    float4 baseChannels = baseNoiseTexture.SampleLevel(noiseVolumeSampler, baseUVW, 0);
    float localCoverage = WeatherCoverage(weather);
    float baseShape = BaseShapeFromChannels(baseChannels, height01, localCoverage);
    float cloudType = smoothstep(
        0.35, 0.75, saturate(weather.g + weatherTypeBias - 0.5));
    float anvilBand = smoothstep(0.52, 0.76, height01) *
        (1.0 - smoothstep(0.90, 1.0, height01));
    baseShape = saturate(baseShape +
        anvilBand * cloudType * saturate(anvilStrength) * (1.0 - baseShape) * 0.45);
    float height = LayerHeightGradient(height01, weather.g);
    float macroDensity = baseShape * height * potential;
    if (macroDensity <= 0.0)
        return float4(baseShape * potential, 0.0, height * potential, 0.0);

    float3 detailUVW = WorldToDetailUVW(worldPosition, weather, sampleTime);
    float detail = DetailErosionFromChannels(
        detailNoiseTexture.SampleLevel(noiseVolumeSampler, detailUVW, 0));
    float erosionStart = saturate(1.0 - max(detailErosionWidth, 0.05));
    float boundary = 1.0 - smoothstep(erosionStart, 0.95, baseShape);
    float shaped = saturate(baseShape - detail * erosionStrength * boundary);
    return float4(baseShape * potential, detail * potential, height * potential,
                  shaped * height * potential);
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
        BaseNoiseChannels(animated),
        DetailNoiseAt(animated),
        HeightGradient(uvw.y),
        uvw.y);
}

float4 EvaluateCachedCloudComponents(float3 uvw, float sampleTime)
{
    float3 animated = AnimatedUVW(uvw, sampleTime);
    float4 baseChannels = baseNoiseTexture.SampleLevel(noiseVolumeSampler, animated, 0);
    float4 detailOctaves = detailNoiseTexture.SampleLevel(noiseVolumeSampler, animated, 0); // RGBA8: 옥타브
    float detail = DetailErosionFromChannels(detailOctaves);
    return ShapeCloudComponents(baseChannels, detail, HeightGradient(uvw.y), uvw.y);
}

float4 EvaluateCloudComponents(float3 uvw, float sampleTime)
{
    // 메인 view/light loop는 항상 구운 볼륨을 사용한다. 다중 채널 절차식 Worley를
    // 이 루프에 인라인하면 fxc 컴파일 시간과 픽셀 비용이 폭증한다.
    return EvaluateCachedCloudComponents(uvw, sampleTime);
}

float4 EvaluatePreviewCloudComponents(float3 uvw, float sampleTime)
{
    return useTextureCache != 0
        ? EvaluateCachedCloudComponents(uvw, sampleTime)
        : EvaluateProceduralCloudComponents(uvw, sampleTime);
}

float4 EvaluateBaseChannels(float3 uvw, float sampleTime)
{
    float3 animated = AnimatedUVW(uvw, sampleTime);
    return useTextureCache != 0
        ? baseNoiseTexture.SampleLevel(noiseVolumeSampler, animated, 0)
        : BaseNoiseChannels(animated);
}

float EvaluateCloudDensity(float3 uvw, float sampleTime)
{
    return EvaluateCloudComponents(uvw, sampleTime).w * densityMultiplier;
}

float EvaluatePeriodicSeamError(float3 uvw)
{
    float4 base = BaseNoiseChannels(uvw);
    float4 baseX = BaseNoiseChannels(uvw + float3(1, 0, 0));
    float4 baseZ = BaseNoiseChannels(uvw + float3(0, 0, 1));
    float detail = DetailNoiseAt(uvw);
    float error = 0.0;
    error = max(error, max(max(abs(base.r - baseX.r), abs(base.g - baseX.g)),
                           max(abs(base.b - baseX.b), abs(base.a - baseX.a))));
    error = max(error, max(max(abs(base.r - baseZ.r), abs(base.g - baseZ.g)),
                           max(abs(base.b - baseZ.b), abs(base.a - baseZ.a))));
    error = max(error, abs(detail - DetailNoiseAt(uvw + float3(1, 0, 0))));
    error = max(error, abs(detail - DetailNoiseAt(uvw + float3(0, 0, 1))));
    return error;
}

float HenyeyGreenstein(float cosTheta)
{
    float g = clamp(phaseG, -0.9, 0.9);
    float g2 = g * g;
    return 0.07957747 * (1.0 - g2) /
        max(pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5), 0.001);
}

float DualLobePhase(float cosTheta)
{
    float forward = HenyeyGreenstein(cosTheta);
    float backwardG = -0.22;
    float backwardG2 = backwardG * backwardG;
    float backward = 0.07957747 * (1.0 - backwardG2) /
        max(pow(1.0 + backwardG2 - 2.0 * backwardG * cosTheta, 1.5), 0.001);
    return lerp(backward, forward, 0.82);
}

#endif
