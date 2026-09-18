// [학습 지도] Camera/Cloud/Domain/Noise/Shape/Shadow/Column CB + t2/t3 → u0 R32 tau 배열 → Cloud/Scene(t6/t7). 월드 m.
// [수정 안내] [직접 조절]의 CPU/UI 원본을 수정한다. 강제 범위는 입력 계약이며 화질 보장이 아니다.
// 별도 권장 구간이 없는 값은 표시된 기본값을 비교 출발점으로 삼는다. b/t/u/s는 버퍼/읽기/쓰기/샘플러 슬롯.
// ============================================================================
//  CloudDeepShadow.hlsl - 단계 12 Near/Far 누적 광학 깊이 생성
// ============================================================================

cbuffer cbCamera : register(b0)
{
    // [파생 값] CPU transpose된 역 view-projection. 화면 UV/device depth [0,1] → 월드 위치(m). 직접 수정하면 깊이와 구름 가림이 어긋난다.
    float4x4 invViewProj;
    // [파생 값] 역 투영 행렬. NDC 방향 → view ray; 큰 월드에서 translation 없이 정밀한 레이를 복원.
    float4x4 invProjection;
    // [파생 값] 역 view 회전 행렬. view 방향 → 월드 방향, w=0으로 translation 제외.
    float4x4 invViewRotation;
    // [파생 값] xyz 카메라 월드 m, ray 시작점. F5~F8/이동에서 생성.
    float3 cameraPos;
    // [파생 값] 유효 구름 시간 s. 일반 실행은 실제 delta 누적, 테스트는 고정 입력; Weather/Base/Shadow에 동일 값.
    float time;
    // [파생 값] xy=전체 화면 가로/세로 pixel, 각각 >=1. Full-resolution ray/LUT 계약.
    float2 renderSize;
    // [파생 값] 카메라 near clip 거리 m, 양수. 깊이/투영 계약에서 생성.
    float nearPlane;
    // [파생 값] 카메라 far clip 거리 m, near보다 큼. 하늘 ray 외부 한계; 구름 최대 거리는 b5도 제한.
    float farPlane;
};

#include "CloudDomainParameters.hlsli"
#include "Noise.hlsli"
#include "Stage12ShadowParameters.hlsli"

RWTexture2DArray<float> stage12OpticalDepthOutput : register(u0);

[numthreads(8, 8, 1)]
// [Deep Cache 순서] b0/b1/b5/b6/b7/b8/b10 + t2/t3 → u0 R32 배열.
// 8x8 thread 하나가 태양 평면 texel 하나의 모든 높이 slice를 기록한다.
// 1. cascade 규격과 texel 중심을 선택. 2. 태양 평면 위치(m)를 만든다.
// 3. 맨 위 slice tau=0에서 아래로 누적. 4. 각 높이 간격을 sun.y로 나눠 실제
// 광선 길이(m)를 구한다. 5. 250m 이하 중점 구간으로 적분/상한 제한 후 저장.
// 아래 slice일수록 태양까지 통과할 구름이 많다. 저장값은 밝기가 아니라 tau다.
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    bool nearCascade = stage12DispatchCascade == 0u;
    uint resolution = nearCascade
        ? stage12NearResolution : stage12FarResolution;
    uint sliceCount = nearCascade
        ? stage12NearSliceCount : stage12FarSliceCount;
    if (dispatchThreadId.x >= resolution || dispatchThreadId.y >= resolution)
        return;

    float widthMeters = nearCascade
        ? stage12NearWidthMeters : stage12FarWidthMeters;
    float3 center = nearCascade ? stage12NearCenter : stage12FarCenter;
    float2 planeOffset =
        ((float2(dispatchThreadId.xy) + 0.5) / (float)resolution - 0.5) *
        float2(widthMeters, Stage12CacheUpWidth(widthMeters));
    float3 planePosition = center +
        stage12LightRight * planeOffset.x +
        stage12LightUp * planeOffset.y;

    const uint topSlice = max(sliceCount, 2u) - 1u;
    stage12OpticalDepthOutput[uint3(dispatchThreadId.xy, topSlice)] = 0.0;
    float opticalDepth = 0.0;
    float verticalInterval =
        (stage12CloudTopMeters - stage12CloudBottomMeters) /
        (float)topSlice;
    float rayInterval = verticalInterval /
        max(stage12LightForward.y, stage12MinimumSunY);

    // 저장 slice 수는 유지한다. 같은 높이 간격도 태양이 낮으면 긴 광선이 되므로
    // 빛 적분만 세분화한다. Near/Far의 모든 texel에 동일한 meter 기준을 사용한다.
#if defined(VCLOUD_TEST_LIGHT_SUBSTEPS)
    uint substeps = VCLOUD_TEST_LIGHT_SUBSTEPS;
#elif defined(VCLOUD_TEST_LEGACY_SHADOW)
    uint substeps = 1u;
#else
    const float maximumIntegrationStepMeters = 250.0;
    uint substeps = max((uint)ceil(rayInterval / maximumIntegrationStepMeters), 1u);
#endif

    [loop]
    for (int slice = (int)topSlice - 1; slice >= 0; --slice)
    {
        if (opticalDepth >= stage12MaximumOpticalDepth)
        {
            stage12OpticalDepthOutput[
                uint3(dispatchThreadId.xy, (uint)slice)] =
                stage12MaximumOpticalDepth;
            continue;
        }
        float density = 0.0;
        [loop] for (uint sub = 0; sub < substeps; ++sub)
        {
            float h = ((float)slice + (sub + 0.5) / (float)substeps) / (float)topSlice;
            float y = lerp(stage12CloudBottomMeters, stage12CloudTopMeters, h);
            float3 p = planePosition + stage12LightForward *
                ((y - planePosition.y) / max(stage12LightForward.y, stage12MinimumSunY));
            float sampleDensity = EvaluateLightCloudDensity(p, time);
#if defined(VCLOUD_TEST_CACHE_DETAIL)
            // 06 후보 진단 전용. 일반 셰이더에는 이 조회가 포함되지 않는다.
            sampleDensity = SampleCloudDensity(p, time, true).finalDensity;
#endif
            density += sampleDensity / (float)substeps;
        }
#if defined(VCLOUD_TEST_CONSTANT_DENSITY)
        // 알려진 균일 매질의 해석해와 실제 누적/조회 경로를 비교한다.
        density = 0.25;
#endif
        opticalDepth = min(stage12MaximumOpticalDepth,
            opticalDepth + max(density, 0.0) *
            max(extinctionCoefficient, 0.0) * rayInterval);
        stage12OpticalDepthOutput[
            uint3(dispatchThreadId.xy, (uint)slice)] = opticalDepth;
    }
}
