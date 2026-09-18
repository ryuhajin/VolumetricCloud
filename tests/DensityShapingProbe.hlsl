// 테스트 전용. 실제 Cloud PS b7를 받아 런타임과 같은 함수를 실행한다.
#include "../shaders/CloudShapeParameters.hlsli"
RWStructuredBuffer<float4> probe : register(u0);
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    float q = float(id.x) / 256.0;
    probe[id.x] = float4(ShapeCloudDensity(q),
        ShapeCloudDensity(saturate(q - 0.12)), EvaluateCommonVerticalProfile(float(id.x)/1024.0), densityShaping);
}
