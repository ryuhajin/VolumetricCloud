// ============================================================================
//  WeatherMapCompute.hlsl - 8x8 GPU Weather RGBA8 생성
// ============================================================================
#define VCLOUD_WEATHER_TEST_BIAS 0.0

struct ChannelParameters
{
    uint seed;
    uint macroPeriod;
    uint detailPeriod;
    float detailWeight;
    float bias;
    float contrast;
    float2 padding;
};

cbuffer WeatherMapBuildCB : register(b0)
{
    ChannelParameters channels[4];
    float coverageThreshold;
    float coverageSoftness;
    float densityCoverageInfluence;
    float thicknessCoverageInfluence;
    uint weatherPreset;
    uint weatherWidth;
    uint weatherHeight;
    uint weatherPadding0;
};

RWTexture2D<float4> outputWeatherMap : register(u0);

uint HashLattice(int x, int y, uint seed)
{
    uint hash = seed ^ 0x9e3779b9u;
    hash ^= asuint(x) * 0x85ebca6bu;
    hash = (hash << 13u) | (hash >> 19u);
    hash ^= asuint(y) * 0xc2b2ae35u;
    hash ^= hash >> 16u; hash *= 0x7feb352du;
    hash ^= hash >> 15u; hash *= 0x846ca68bu;
    return hash ^ (hash >> 16u);
}

float GradientDot(uint hash, float2 p)
{
    const float d = 0.7071067811865475;
    switch (hash & 7u)
    {
    case 0u: return p.x; case 1u: return -p.x;
    case 2u: return p.y; case 3u: return -p.y;
    case 4u: return (p.x + p.y) * d;
    case 5u: return (p.x - p.y) * d;
    case 6u: return (-p.x + p.y) * d;
    default: return (-p.x - p.y) * d;
    }
}

int WrapLattice(int value, int period)
{ int r = value % period; return r < 0 ? r + period : r; }

float Perlin(float2 uv, uint inputPeriod, uint seed)
{
    int period = clamp((int)inputPeriod, 1, 16);
    float2 p = uv * period;
    int2 p0 = (int2)floor(p);
    float2 f = frac(p);
    int2 w0 = int2(WrapLattice(p0.x, period), WrapLattice(p0.y, period));
    int2 w1 = int2(WrapLattice(p0.x + 1, period), WrapLattice(p0.y + 1, period));
    float n00 = GradientDot(HashLattice(w0.x,w0.y,seed), f);
    float n10 = GradientDot(HashLattice(w1.x,w0.y,seed), f-float2(1,0));
    float n01 = GradientDot(HashLattice(w0.x,w1.y,seed), f-float2(0,1));
    float n11 = GradientDot(HashLattice(w1.x,w1.y,seed), f-float2(1,1));
    float2 t = f*f*f*(f*(f*6.0-15.0)+10.0);
    return saturate(0.5 + lerp(lerp(n00,n10,t.x),lerp(n01,n11,t.x),t.y)
                    * 0.7071067811865475);
}

float SampleChannel(float2 uv, ChannelParameters p)
{
    float macro = Perlin(uv, p.macroPeriod, p.seed);
    float detail = Perlin(uv, p.detailPeriod, p.seed ^ 0x9e3779b9u);
    float field = lerp(macro, detail, p.detailWeight);
    return saturate((field - 0.5) * p.contrast + 0.5 + p.bias);
}

float TiledDistance(float2 uv, float2 center)
{ float2 d = min(abs(uv-center), 1.0-abs(uv-center)); return length(d); }

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= weatherWidth || id.y >= weatherHeight) return;
    float2 uv = (float2(id.xy) + 0.5) / float2(weatherWidth, weatherHeight);
    float4 result = float4(1.0, 0.5, 0.5, 1.0);
    if (weatherPreset == 1u)
    {
        float coverageField = SampleChannel(uv, channels[0]);
        float halfSoftness = coverageSoftness * 0.5;
        result.r = smoothstep(coverageThreshold-halfSoftness,
                              coverageThreshold+halfSoftness, coverageField);
        // RGBA8에서 사실상 비어 있는 경계는 CPU 기준과 동일하게 0으로 둔다.
        if (round(saturate(result.r) * 255.0) > 1.0)
        {
            result.g = SampleChannel(uv, channels[1]);
            float density = SampleChannel(uv, channels[2]);
            result.b = saturate(lerp(density, result.r,
                                     densityCoverageInfluence));
            float thickness = SampleChannel(uv, channels[3]);
            float core = smoothstep(0.05, 0.95, result.r);
            result.a = saturate(lerp(thickness, core,
                                     thicknessCoverageInfluence));
        }
        else result = float4(result.r, 0.5, 0.5, 0.0);
    }
    else if (weatherPreset == 2u)
    {
        float largeIsland = 1.0-smoothstep(0.16,0.29,TiledDistance(uv,float2(0.30,0.34)));
        float smallIsland = 1.0-smoothstep(0.11,0.23,TiledDistance(uv,float2(0.73,0.69)));
        result.r = max(largeIsland,smallIsland);
        result.g = uv.y < 1.0/3.0 ? 0.0 : (uv.y < 2.0/3.0 ? 0.5 : 1.0);
        result.b = uv.x < 1.0/3.0 ? 0.0 : (uv.x < 2.0/3.0 ? 0.5 : 1.0);
        result.a = result.r > 0.0 ? (smallIsland > largeIsland ? 0.85 : 0.35) : 0.0;
    }
    // 복사본 기반 hot-reload smoke가 실제 texture 교체와 rollback을 검증하는 hook이다.
    result.g = saturate(result.g + VCLOUD_WEATHER_TEST_BIAS);
    outputWeatherMap[id.xy] = result;
}
