// 테스트 전용: 실제 PS 바인딩에서 얻은 b3/b8/b9를 같은 슬롯으로 전달받아 GPU에서 읽는다.
// 런타임에는 등록하지 않는다. 기존 include를 사용해 ABI 선언 복제를 피한다.
#include "../shaders/LightParameters.hlsli"
#include "../shaders/Stage12ShadowParameters.hlsli"
#include "../shaders/Stage14Atmosphere.hlsli"

RWStructuredBuffer<float4> probe : register(u0);
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    probe[0] = float4(directionToSun, 0);
    probe[1] = float4(stage12LightForward, stage12CacheReady);
    probe[2] = float4(stage12LightRight, 0);
    probe[3] = float4(stage12LightUp, 0);
    probe[4] = float4(AtmosphereSunDirection(), 0);
}
