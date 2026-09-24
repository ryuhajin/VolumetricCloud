// [학습 지도] CPU NoiseVolumeParameters b6 → CSBase/CSDetail u0 → RGBA8 Base 128³/Detail 64³ → Noise t3/t4. UVW cycle.
// [수정 안내] [직접 조절]의 CPU/UI 원본을 수정한다. 강제 범위는 입력 계약이며 화질 보장이 아니다.
// 별도 권장 구간이 없는 값은 표시된 기본값을 비교 출발점으로 삼는다. b/t/u/s는 버퍼/읽기/쓰기/샘플러 슬롯.
// ============================================================================
//  NoiseVolume.hlsl - 단계 13-4 periodic Base/Detail RGBA8 Texture3D 생성
// ============================================================================
#include "NoiseVolumeParameters.hlsli"

RWTexture3D<float4> outputVolume : register(u0);

// [패스 지도] b6 생성 규격 → CSBase/CSDetail → u0 RGBA8 Texture3D → Noise.hlsli(t3/t4).
// 좌표는 타일 UVW 또는 격자 cell, 월드 m 매핑은 조회 때 수행한다. 초기화/재생성 때만 실행.
// 1. WrapCell은 음수 좌표도 [0,period) 주소로 감아 타일 양 끝의 gradient/feature를 공유한다.
uint WrapCell(int value, uint period)
{
    int safePeriod = (int)max(period, 1u);
    int result = value - (int)floor((float)value / (float)safePeriod) * safePeriod;
    return (uint)(result < 0 ? result + safePeriod : result);
}

// 2. xyz 격자와 seed를 uint hash로 혼합한다. 정수 overflow는 의도적인 32bit 연산이다.
// 같은 주소/seed는 같은 무늬를 재현하며 seed의 대소는 밀도·해상도와 무관하다.
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

// 3. 하위 24bit를 2^24로 나누어 [0,1) 위치를 만든다. Worley의 cell 내부 점 위치다.
float HashUnit(int3 cell, uint seed)
{
    return (float)(HashNoiseCell(cell, seed) & 0x00ffffffu) / 16777216.0;
}

// 4. 3D 대각선 방향 중 하나를 골라 1/sqrt(2)로 길이를 맞춘다.
// gradient는 색이 아니라 각 corner가 만드는 국소 기울기이며, dot 결과는 부호가 있다.
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

// 5. cell+corner offset을 wrap한 뒤 gradient와 corner→표본 벡터를 내적한다.
// 주소만 wrap하고 local offset은 유지해야 경계에서 위치가 갑자기 튀지 않는다.
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

// [3D Perlin 순서] 1. floor(p)=cell, frac(p)=칸 내부 [0,1) 위치.
// 2. quintic fade로 cell 경계의 미분을 부드럽게 만든다.
// 3. 8 corner 기여를 x 네 번 → y 두 번 → z 한 번 보간한다.
// 출력은 아직 signed noise. 2D Perlin과 같은 원리를 z축까지 확장한 것이다.
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

// [Worley 순서] 1. 주변 3x3x3 cell마다 hash로 feature point를 배치한다.
// 2. 현재 점에서 feature까지 제곱거리를 비교해 가장 가까운 것을 고른다.
// 3. 마지막 한 번 sqrt하여 거리를 구하고 /1.15 후 [0,1]로 제한한다.
// feature 근처는 검고 멀면 밝다. 1-거리로 뒤집으면 세포 중심에 질량이 모인다.
// 이웃 탐색/wrap이 틀리면 격자 선이나 타일 이음매가 생긴다.
float PeriodicWorleyDistance(float3 p, uint period, uint seed)
{
    int3 cell = (int3)floor(p);
    float3 local = frac(p);
    float nearestSquared = 4.0;
#if defined(VCLOUD_TEST_DETAIL_SPECTRUM) && VCLOUD_TEST_DETAIL_SPECTRUM
    // Detail fBm의 추가 호출이 FXC 펼침 한도를 넘지 않도록 이웃 탐색 루프 유지.
    [loop]
#else
    [unroll]
#endif
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

// [Base R 조립] 1. 4옥타브 gradient noise를 0.5,0.25,0.125,0.0625로 합친다.
// 2. 합계 가중치로 나누고 signed 값을 [0,1] 중심으로 이동한다.
// 3. 가장 낮은 주파수 Worley 질량과 혼합해 둥근 덩어리를 만든다.
// 0.35/0.42는 현행 형상식 계수(자동 clamp 없음). 바꾸면 Base 전체 분포가 바뀌므로
// 먼저 기존 값을 기준으로 한 항만 비교하고 R/최종 밀도 진단을 함께 본다.
float BasePerlinWorley(float3 uvw)
{
    float sum = 0.0;
    float normalization = 0.0;
    float amplitude = 0.5;
    [unroll]
    for (uint octave = 0u; octave < 4u; ++octave)
    {
        uint frequency = max(baseVolumeFrequencies[octave], 1u);
        float weight = amplitude * ((octave == 1u || octave == 2u)
            ? 1.0 + baseMidOctaveExtra : 1.0);
#if defined(VCLOUD_TEST_BASE_MID_WEIGHT)
        // 비교 전용: 주파수/seed/해상도는 유지하고 중간 두 옥타브 진폭만 변경한다.
        weight = amplitude * ((octave == 1u || octave == 2u) ? VCLOUD_TEST_BASE_MID_WEIGHT : 1.0);
#endif
        sum += PeriodicGradientNoise(
            uvw * frequency, frequency,
            noiseVolumeSeed + octave * 173u) * weight;
        normalization += weight;
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
// [Base 출력] 4x4x4 thread에서 voxel 중심을 계산한다. R=Perlin-Worley,
// G/B/A=첫 세 주파수 Worley 거리. 전 채널 [0,1] UNORM; 빈 voxel도 반드시 기록한다.
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
// [Detail 출력] RGBA 각각 다른 기저 주파수/seed의 Worley fBm을 저장한다.
// Renderer가 CSDetail에만 정의1을 전달한다. 정의0은 역사적 단일 Worley 비교용이다.
// 조회 단계의 detailWeights와 erosion이 파임을 만든다. seed/frequency 수정 뒤 재생성이 필요하다.
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
#if defined(VCLOUD_TEST_DETAIL_SPECTRUM) && VCLOUD_TEST_DETAIL_SPECTRUM
        // 승인 fBm: 같은 타일/seed/기저 대역에 2배·4배 주파수 추가. 가중치 합1.
        float middle=PeriodicWorleyDistance(uvw*(frequency*2u)+19.0,frequency*2u,
            noiseVolumeSeed+911u+channel*173u);
        float fine=PeriodicWorleyDistance(uvw*(frequency*4u)+19.0,frequency*4u,
            noiseVolumeSeed+911u+channel*173u);
        result[channel]=result[channel]*.625+middle*.25+fine*.125;
#endif
    }
    outputVolume[id] = saturate(result);
}

#if defined(VCLOUD_NEAR_MICRO_BAKE_RESOLUTION)
// [근경 미세 Detail] 전용 Worley fBm을 단일 채널(R8) 64³로 굽는다. u1은 이 경로만 사용한다.
// 기저 3 cycle/tile, 1×/2×/4× .625/.25/.125, 거리 sqrt(d²)/1.15로 Detail 생성기와 같은 Worley 정의를 쓴다.
// 반복 텍스처라 Detail과 같은 주기 Worley(PeriodicWorleyDistance)를 쓰고 seed는 Detail과 다르다.
// 해상도는 컴파일 정의(Renderer는 64)로 정한다. b6은 읽지 않는다.
RWTexture3D<unorm float> nearMicroOutput : register(u1);

[numthreads(4, 4, 4)]
void CSNearMicro(uint3 id : SV_DispatchThreadID)
{
    const uint resolution = VCLOUD_NEAR_MICRO_BAKE_RESOLUTION;
    if (any(id >= resolution.xxx))
        return;
    float3 uvw = ((float3)id + 0.5) / (float)resolution;
    const uint seed = 1337u + 5003u;
    float n = 0.625 * PeriodicWorleyDistance(uvw * 3.0 + 19.0, 3u, seed) +
              0.25 * PeriodicWorleyDistance(uvw * 6.0 + 57.1, 6u, seed + 71u) +
              0.125 * PeriodicWorleyDistance(uvw * 12.0 + 93.7, 12u, seed + 149u);
    nearMicroOutput[id] = saturate(n);
}
#endif
