// 단계 0 깊이 복원을 검증하기 위한 불투명 평면/박스 장면.
cbuffer cbScene : register(b0)
{
    float4x4 viewProj;
};

struct VSInput
{
    float3 position : POSITION;
    float3 color : COLOR;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 color : COLOR;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position = mul(float4(input.position, 1.0), viewProj);
    output.color = input.color;
    return output;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    return float4(input.color, 1.0);
}
