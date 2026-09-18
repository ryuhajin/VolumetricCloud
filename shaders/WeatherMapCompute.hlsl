// [학습 지도] CPU Weather generator b0(160B) → 8x8 compute u0 → 256² RGBA8 → Weather t2. 타일 UV, 채널 [0,1].
// [수정 안내] [직접 조절]의 CPU/UI 원본을 수정한다. 강제 범위는 입력 계약이며 화질 보장이 아니다.
// 별도 권장 구간이 없는 값은 표시된 기본값을 비교 출발점으로 삼는다. b/t/u/s는 버퍼/읽기/쓰기/샘플러 슬롯.
// ============================================================================
//  WeatherMapCompute.hlsl - 8x8 GPU Weather RGBA8 생성
// ============================================================================
// [패스 지도] CPU WeatherMapGeneratorSettings → b0 → 8x8 CS → u0 RGBA8.
// 초기화/설정 변경/핫 리로드 때 생성하며 매 프레임 생성하지 않는다.
// 출력은 Weather.hlsli(t2)가 읽어 View/Light/Deep Shadow의 동일한 배치를 만든다.
// UV는 한 반복 [0,1], 채널 값은 무차원 [0,1]. 월드 m 변환은 조회 쪽 책임이다.
// [직접 조절] CPU WeatherMap.h 및 내장 formation/F2가 원본이다. cbuffer는
// CPU 편지를 읽는 칸이므로 여기 선언에 기본값을 써도 CPU 값을 바꾸지 못한다.
// 아래 TEST_BIAS는 테스트 복사본 전용이며 화질 튜닝값이 아니다.
#define VCLOUD_WEATHER_TEST_BIAS 0.0

struct ChannelParameters
{
    // [직접 조절] CPU generator의 재현 가능한 무늬 식별자. uint [0,4294967295], 구조체 초기 0. 권장 고정 seed로 비교; 크기와 구름 양은 무관하다.
    uint seed;
    // [직접 조절] F2/Weather generator의 한 타일 큰 격자 수. [강제 범위] 정수 [1,8], 초기 2. 증가하면 큰 무늬가 잘게 나뉜다.
    uint macroPeriod;
    // [직접 조절] F2/Weather generator의 작은 격자 수. [강제 범위] 정수 [2,16], 초기 5. 증가하면 미세한 지역 변화가 촘촘해진다.
    uint detailPeriod;
    // [직접 조절] F2/Weather generator. [강제 범위]/권장 UI [0,1], 초기 0.25. 0=macro만, 1=detail만.
    float detailWeight;
    // [직접 조절] F2/Weather generator 무차원 offset. [강제 범위] [-0.5,0.5], 초기 0. 증가하면 해당 채널이 밝아진다.
    float bias;
    // [직접 조절] F2/Weather generator. [강제 범위] [0.25,3], 초기 1. UI [0.1,4] 입력도 이 범위로 보정; 증가하면 0.5 주변 대비가 커진다.
    float contrast;
    // [패딩] 16바이트 packing을 위한 예약 칸, 0 유지. 화면 효과 없음; 삭제/순서 변경 금지.
    float2 padding;
};

cbuffer WeatherMapBuildCB : register(b0)
{
    // [파생 값] generator → 배열 0/1/2/3=R/G/B/A. 각 원소 32B, CPU/HLSL 순서를 맞춘다.
    ChannelParameters channels[4];
    // [직접 조절] F2/Weather generator. [강제 범위]/권장 UI [0,1], 초기 0.56. 증가하면 R 영역이 줄어든다.
    float coverageThreshold;
    // [직접 조절] F2/Weather generator. [강제 범위] [0.02,0.8], 초기 0.14. 증가하면 R 문턱 경계가 넓고 완만해진다.
    float coverageSoftness;
    // [직접 조절] F2/Weather generator. [강제 범위]/권장 UI [0,1], 초기 0.35. 증가하면 B가 독립 noise보다 R 배치를 더 따른다.
    float densityCoverageInfluence;
    // [직접 조절] F2/Weather generator. [강제 범위]/권장 UI [0,1], 초기 0.20. 증가하면 A 두께 재료가 coverage 중심부를 더 따른다.
    float thicknessCoverageInfluence;
    // [파생 값] Stage5WeatherPreset: 0 균일/1 Perlin/2 진단. 최종 기본 1.
    uint weatherPreset;
    // [고정 품질] CPU width=256 texel, 출력 texture와 같아야 한다.
    uint weatherWidth;
    // [고정 품질] CPU height=256 texel, dispatch 범위 검사에 사용.
    uint weatherHeight;
    // [패딩] 16바이트 packing을 위한 예약 칸, 0 유지. 화면 효과 없음; 삭제/순서 변경 금지.
    uint weatherPadding0;
};

RWTexture2D<float4> outputWeatherMap : register(u0);

uint HashLattice(int x, int y, uint seed)
{
    // 1. 격자 주소와 seed를 섞어 재현 가능한 uint를 만든다(의도적 32bit wrap).
    // seed는 난수의 시작 표식이지 밀도/품질 배율이 아니다. 큰 seed가 더 촘촘하지 않다.
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
    // 2. hash 하위 3bit로 8방향 중 하나를 고르고 corner→표본 벡터와 내적한다.
    // 대각선은 1/sqrt(2)로 정규화해 축 방향보다 기여가 커지지 않게 한다.
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
    // 1. 한 타일을 period x period 격자로 나눈다. GPU 방어 범위는 [1,16].
    // CPU macro는 [1,8], detail은 [2,16]이므로 두 대역의 범위와 구분한다.
    int period = clamp((int)inputPeriod, 1, 16);
    // 2. 예: uv=(0.3,0.6), period=4 → p=(1.2,2.4).
    // floor(p)=(1,2)는 격자 칸 주소, frac(p)=(0.2,0.4)는 칸 내부 비율이다.
    float2 p = uv * period;
    int2 p0 = (int2)floor(p);
    float2 f = frac(p);
    // 3. 양 끝 격자를 같은 주소로 감아 UV 0/1 경계를 이어 붙인다.
    // period=4의 주소는 ...0,1,2,3,0...; 빠뜨리면 반복 경계가 선으로 보인다.
    int2 w0 = int2(WrapLattice(p0.x, period), WrapLattice(p0.y, period));
    int2 w1 = int2(WrapLattice(p0.x + 1, period), WrapLattice(p0.y + 1, period));
    // 4. 네 corner의 기울기와 corner→현재 점 offset을 내적한다.
    // n00=(0,0), n10=(1,0), n01=(0,1), n11=(1,1); UV y의 화면 위/아래와
    // 수학 좌표의 위/아래를 혼동하지 않는다. 이 값은 아직 음수도 가능하다.
    float n00 = GradientDot(HashLattice(w0.x,w0.y,seed), f);
    float n10 = GradientDot(HashLattice(w1.x,w0.y,seed), f-float2(1,0));
    float n01 = GradientDot(HashLattice(w0.x,w1.y,seed), f-float2(0,1));
    float n11 = GradientDot(HashLattice(w1.x,w1.y,seed), f-float2(1,1));
    // 5. fade(x)=6x^5-15x^4+10x^3. 양 끝 기울기를 평평하게 만들어 격자 무늬를 줄인다.
    float2 t = f*f*f*(f*(f*6.0-15.0)+10.0);
    // 6. x 보간 두 번 → y 보간 한 번. 진폭을 조절하고 0.5 중심으로 옮긴 뒤
    // saturate(=clamp 0~1)한다. 통계적으로 완벽한 정규분포/균등분포는 아니다.
    return saturate(0.5 + lerp(lerp(n00,n10,t.x),lerp(n01,n11,t.x),t.y)
                    * 0.7071067811865475);
}

float SampleChannel(float2 uv, ChannelParameters p)
{
    // 1. 큰 무늬 macro와 작은 무늬 detail을 같은 타일에서 만든다.
    // 2. detailWeight가 0이면 macro만, 1이면 detail만 사용한다.
    // 3. contrast는 0.5를 중심으로 명암을 벌리고 bias는 전체를 위/아래로 민다.
    // 채널마다 같은 함수라도 R/G/B/A의 소비 의미가 달라 화면 효과가 다르다.
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
    // 1. 스레드 하나가 texel 하나를 소유한다. +0.5는 texel 중심 표본이다.
    if (id.x >= weatherWidth || id.y >= weatherHeight) return;
    float2 uv = (float2(id.xy) + 0.5) / float2(weatherWidth, weatherHeight);
    float4 result = float4(1.0, 0.5, 0.5, 1.0);
    // 2. preset 0은 균일 검증 맵, 1은 실제 Perlin 배치, 2는 채널 진단 섬이다.
    if (weatherPreset == 1u)
    {
        float coverageField = SampleChannel(uv, channels[0]);
        // 3. threshold를 올리면 R이 줄고 빈 하늘이 늘어난다. softness는 문턱의
        // 전이 폭이며 올리면 경계가 완만해진다(구름 양이 항상 단조 증가하지는 않는다).
        float halfSoftness = coverageSoftness * 0.5;
        result.r = smoothstep(coverageThreshold-halfSoftness,
                              coverageThreshold+halfSoftness, coverageField);
        // RGBA8에서 사실상 비어 있는 경계는 CPU 기준과 동일하게 0으로 둔다.
        if (round(saturate(result.r) * 255.0) > 1.0)
        {
            // 4. G는 지역 타입 원본이다. Fixed 타입도 G 생성은 유지하며 b10에서 무시한다.
            // G는0.5 예약값. 지역 타입 노이즈 조회는 제거했다.
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
        result.g = 0.5;
        result.b = uv.x < 1.0/3.0 ? 0.0 : (uv.x < 2.0/3.0 ? 0.5 : 1.0);
        result.a = result.r > 0.0 ? (smallIsland > largeIsland ? 0.85 : 0.35) : 0.0;
    }
    // 복사본 기반 hot-reload smoke가 실제 texture 교체와 rollback을 검증하는 hook이다.
    result.r = saturate(result.r + VCLOUD_WEATHER_TEST_BIAS);
    // 5. UNORM 저장으로 0~1을 0~255에 양자화한다. A는 m가 아니라 두께 보간 재료다.
    outputWeatherMap[id.xy] = result;
}
