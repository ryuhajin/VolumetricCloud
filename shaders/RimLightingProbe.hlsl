// 실제 b3와 생산 HLSL 함수를 사용한 수치/상한 검사. 런타임 manifest에는 등록하지 않는다.
// 아래 probe는 빛 레이를 실행하지 않는다. include 안 미사용 march 함수의 시간 심벌만 제공한다.
static const float time = 0;
#include "CloudLighting.hlsli"
RWStructuredBuffer<float4> probe : register(u0);
[numthreads(64,1,1)]
void main(uint3 id: SV_DispatchThreadID) {
    if (id.x>=1024) return;
#if defined(VCLOUD_TEST_BOUNDARY_SPHERE)
    // 직교 역광 균일 구: R=1m, 중심 tau=8. K=albedo(태양/phase=1).
    uint group=id.x/256;
    uint count=group==0?8:group==1?32:group==2?128:4096;
    float b=(id.x%256)/255.0;
    float lengthMeters=2*sqrt(saturate(1-b*b));
    float ds=lengthMeters/count;
    float density=4/max(extinctionCoefficient,1e-10);
    float viewT=1, sum=0;
    [loop] for(uint i=0;i<count;++i) {
        float sunT=exp(-4*(lengthMeters-(i+.5)*ds));
        sum+=ComputeDirectInteractionColor(density,viewT,ds,1.0.xxx).x*sunT;
        viewT*=exp(-4*ds);
    }
    probe[id.x]=float4(sum,viewT,4*lengthMeters,b);
    return;
#endif
    float mu=-1.0+2.0*(id.x%128)/127.0;
    float t=(id.x/128)/7.0;
    PhaseSample p=EvaluateDualLobePhase(float3(sqrt(saturate(1-mu*mu)),mu,0),float3(0,1,0));
    DirectLightingResponse r=EvaluateDirectLightingResponse(t,p.phaseFactor,p.rimPhaseFactor);
    probe[id.x]=float4(r.basePhase,r.rimPhase,r.shapedTransmittance,p.unboundedPhaseFactor);
}
