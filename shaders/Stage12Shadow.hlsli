// [학습 지도] b8 + t6/t7 R32 tau/s2 clamp → T와 tau → 구름/지면 조명. 월드 m를 태양 평면 UV로 투영.
// [수정 안내] [직접 조절]의 CPU/UI 원본을 수정한다. 강제 범위는 입력 계약이며 화질 보장이 아니다.
// 별도 권장 구간이 없는 값은 표시된 기본값을 비교 출발점으로 삼는다. b/t/u/s는 버퍼/읽기/쓰기/샘플러 슬롯.
// ============================================================================
//  Stage12Shadow.hlsli - Deep optical-depth cache 공용 조회
// ============================================================================
#ifndef VCLOUD_STAGE12_SHADOW_HLSLI
#define VCLOUD_STAGE12_SHADOW_HLSLI

#include "Stage12ShadowParameters.hlsli"

Texture2DArray<float> stage12NearOpticalDepth : register(t6);
Texture2DArray<float> stage12FarOpticalDepth : register(t7);
SamplerState stage12LinearClampSampler : register(s2);

struct Stage12ShadowSample
{
    // [파생 값] Near/Far 혼합 태양 생존율 [0,1], 미준비=1.
    float transmittance;
    // [파생 값] -ln(T), [0,9.21034037]. 다중 산란에서 재사용.
    float opticalDepth;
    // [파생 값] Near 배열 높이 보간 tau [0,9.21034037].
    float nearOpticalDepth;
    // [파생 값] Far 배열 높이 보간 tau [0,9.21034037].
    float farOpticalDepth;
    // [파생 값] Near 혼합 비율 [0,1], 중심=1/밖=0.
    float nearWeight;
    // [파생 값] Far 경계 fade 비율 [0,1]. cache 전체 valid와 다르다.
    float farValidity;
    // [파생 값] cache 사용 가능 0/1. 0이면 Cloud가 cone fallback.
    float valid;
};

// [입력/출력] t6/t7 R32 배열 tau + b8 → T/tau/캐시 진단 → Cloud와 Scene.
// 1. 월드 차이(m)를 태양 평면의 right/up에 투영하고 폭으로 나눈 뒤 0.5 이동.
// 태양 축 방향 이동은 UV를 바꾸지 않으므로 같은 광선이 같은 texel을 공유한다.
float2 Stage12CacheUv(float3 position, float3 center, float widthMeters)
{
    float3 delta = position - center;
    float invWidth = rcp(max(widthMeters, 1.0));
    return float2(dot(delta, stage12LightRight) * invWidth,
                  dot(delta, stage12LightUp) / Stage12CacheUpWidth(widthMeters)) + 0.5;
}

float Stage12CascadeEdge(float2 uv)
{
    return max(abs(uv.x - 0.5), abs(uv.y - 0.5)) * 2.0;
}

// 2. 월드 Y를 [0,1] 높이로 정규화하고 인접 두 slice의 tau를 보간한다.
// Texture2DArray는 배열 축을 자동 선형 보간하지 않으므로 두 번 읽어 lerp한다.
float Stage12SampleTau(Texture2DArray<float> cacheTexture, float2 uv,
                       float worldHeight, uint sliceCount)
{
    float heightFraction = saturate(
        (worldHeight - stage12CloudBottomMeters) /
        max(stage12CloudTopMeters - stage12CloudBottomMeters, 1.0));
    float slice = heightFraction * (float)(max(sliceCount, 2u) - 1u);
    uint lower = (uint)floor(slice);
    uint upper = min(lower + 1u, max(sliceCount, 2u) - 1u);
    float lowerTau = cacheTexture.SampleLevel(
        stage12LinearClampSampler, float3(uv, (float)lower), 0);
    float upperTau = cacheTexture.SampleLevel(
        stage12LinearClampSampler, float3(uv, (float)upper), 0);
#if defined(VCLOUD_TEST_MONOTONE_HEIGHT)
    // 후보: 인접 네 높이의 단조 Hermite 보간. 새 극값을 만들지 않는다.
    uint last = max(sliceCount, 2u) - 1u;
    float previousTau = cacheTexture.SampleLevel(stage12LinearClampSampler,
        float3(uv, (float)(lower > 0u ? lower - 1u : 0u)), 0);
    float nextTau = cacheTexture.SampleLevel(stage12LinearClampSampler,
        float3(uv, (float)min(upper + 1u, last)), 0);
    float d = upperTau - lowerTau;
    float before = lowerTau - previousTau;
    float after = nextTau - upperTau;
    float m0 = lower == 0u ? d :
        (before * d > 0.0 ? 2.0 * before * d / (before + d) : 0.0);
    float m1 = upper == last ? d :
        (d * after > 0.0 ? 2.0 * d * after / (d + after) : 0.0);
    float t = frac(slice);
    float t2 = t*t, t3 = t2*t;
    float interpolated = (2*t3-3*t2+1)*lowerTau + (t3-2*t2+t)*m0
        + (-2*t3+3*t2)*upperTau + (t3-t2)*m1;
    return clamp(interpolated, min(lowerTau, upperTau), max(lowerTau, upperTau));
#endif
    return clamp(lerp(lowerTau, upperTau, frac(slice)),
                 0.0, stage12MaximumOpticalDepth);
}

// 3. Near는 중심의 세밀한 그림자, Far는 넓은 그림자를 제공한다.
// Near/Far 각각 tau→exp(-tau)로 변환한 T를 혼합한다(tau 직접 혼합과 다름).
// Far 끝은 T=1로 fade. 최종 tau=-ln(T)로 다시 만들어 다중 산란에 전달한다.
// valid는 캐시 사용 가능 여부이지 해당 UV 안에 반드시 구름이 있다는 뜻이 아니다.
Stage12ShadowSample SampleStage12DeepShadow(float3 position)
{
    Stage12ShadowSample result;
    result.transmittance = 1.0;
    result.opticalDepth = 0.0;
    result.nearOpticalDepth = 0.0;
    result.farOpticalDepth = 0.0;
    result.nearWeight = 0.0;
    result.farValidity = 0.0;
    result.valid = 0.0;
    bool enabled = stage12CacheReady != 0u &&
        stage12LightForward.y >= stage12MinimumSunY;
    if (!enabled)
        return result;

    float2 nearUv = Stage12CacheUv(
        position, stage12NearCenter, stage12NearWidthMeters);
    float2 farUv = Stage12CacheUv(
        position, stage12FarCenter, stage12FarWidthMeters);
    float nearEdge = Stage12CascadeEdge(nearUv);
    float farEdge = Stage12CascadeEdge(farUv);
    result.nearWeight = 1.0 - smoothstep(
        stage12NearCoreEnd, stage12NearBlendEnd, nearEdge);
    result.farValidity = 1.0 - smoothstep(
        stage12FarFadeStart, stage12FarFadeEnd, farEdge);
    result.nearOpticalDepth = Stage12SampleTau(
        stage12NearOpticalDepth, nearUv, position.y,
        stage12NearSliceCount);
    result.farOpticalDepth = Stage12SampleTau(
        stage12FarOpticalDepth, farUv, position.y,
        stage12FarSliceCount);
    float nearT = exp(-result.nearOpticalDepth);
    float farT = lerp(1.0, exp(-result.farOpticalDepth), result.farValidity);
    result.transmittance = saturate(lerp(farT, nearT, result.nearWeight));
#if defined(VCLOUD_TEST_CASCADE_NEAR)
    result.transmittance = nearT;
#elif defined(VCLOUD_TEST_CASCADE_FAR)
    result.transmittance = exp(-result.farOpticalDepth);
#endif
    result.opticalDepth = min(-log(max(result.transmittance, 0.0001)),
                              stage12MaximumOpticalDepth);
    result.valid = 1.0;
    return result;
}

float Stage12SurfaceTransmittance(float3 surfacePosition)
{
    // 구름층 아래 표면은 bottom slice(전체 구름 기둥)를 읽는다.
    surfacePosition.y = stage12CloudBottomMeters;
    return lerp(1.0, SampleStage12DeepShadow(surfacePosition).transmittance,
                Stage12SunTransitionWeight());
}

float Stage12SurfaceFactor(float cloudTransmittance)
{
    float shadowed = stage12SurfaceAmbientFloor +
        (1.0 - stage12SurfaceAmbientFloor) * saturate(cloudTransmittance);
    return lerp(1.0, shadowed, stage12SurfaceShadowStrength);
}

// 카메라 레이와 무관하게 Texture2DArray의 한 slice를 화면 UV에 그대로 펼친다.
// tau/maxTau는 실제 값 대부분이 검게 뭉치므로 1-exp(-tau*exposure)로 시각화한다.
float4 Stage12DebugCacheTexture(float2 uv, bool nearCascade)
{
    if (stage12CacheReady == 0u)
        return float4(0.45, 0.0, 0.0, 1.0); // cache unavailable 표시

    uint slice = nearCascade
        ? min(stage12DebugNearSlice, max(stage12NearSliceCount, 2u) - 1u)
        : min(stage12DebugFarSlice, max(stage12FarSliceCount, 2u) - 1u);
    float tau = nearCascade
        ? stage12NearOpticalDepth.SampleLevel(
            stage12LinearClampSampler, float3(saturate(uv), (float)slice), 0)
        : stage12FarOpticalDepth.SampleLevel(
            stage12LinearClampSampler, float3(saturate(uv), (float)slice), 0);
    float exposed = 1.0 - exp(-max(tau, 0.0) * stage12CacheDebugExposure);
    return float4(saturate(exposed).xxx, 1.0);
}

#endif
