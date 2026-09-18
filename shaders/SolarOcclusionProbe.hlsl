// 테스트 전용: 실제 캐시 생성의 선언/밀도 함수를 재사용하되 생성 entry는 실행하지 않는다.
#define main UnusedCacheGeneration
#include "CloudDeepShadow.hlsl"
#undef main
#include "Stage12Shadow.hlsli"
RWStructuredBuffer<float4> results : register(u1);

float IntegrateSun(float3 p, float stepMeters)
{
    float lengthMeters = max(stage12CloudTopMeters - p.y, 0) / stage12LightForward.y;
    uint count = max((uint)ceil(lengthMeters / stepMeters), 1u);
    float ds = lengthMeters / count;
    float tau = 0;
    [loop] for (uint i = 0; i < count; ++i)
    {
        tau += max(EvaluateLightCloudDensity(p + stage12LightForward * ((i + .5) * ds), time), 0)
            * max(extinctionCoefficient, 0) * ds;
        if (tau >= stage12MaximumOpticalDepth) break;
    }
    return exp(-min(tau, stage12MaximumOpticalDepth));
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= 64 || id.y >= 32) return;
    // 카메라 시선과 독립인 월드 XY 단면. 중심은 F5 XZ, 폭16km, 높이 전체.
    float3 p = float3(cameraPos.x + ((id.x + .5) / 64.0 - .5) * 16000,
        lerp(stage12CloudBottomMeters, stage12CloudTopMeters, (id.y + .5) / 32.0), cameraPos.z);
    Stage12ShadowSample cached = SampleStage12DeepShadow(p);
    float coarseStep = (stage12CloudTopMeters - stage12CloudBottomMeters) /
        ((stage12NearSliceCount - 1) * stage12LightForward.y);
    uint index = (id.y * 64 + id.x) * 2;
    results[index] = float4(exp(-cached.nearOpticalDepth), IntegrateSun(p, 25),
        IntegrateSun(p, 12.5), IntegrateSun(p, coarseStep));
    results[index + 1] = float4(cached.transmittance, cached.nearWeight,
        EvaluateLightCloudDensity(p, time), coarseStep);
}
