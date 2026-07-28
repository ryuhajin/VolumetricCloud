// Temporal resolve 결과를 swap-chain back buffer로 복사한다.

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

Texture2D<float3> resolvedColor : register(t0);
SamplerState linearClampSampler : register(s0);

float4 main(VSOut input) : SV_TARGET
{
    return float4(resolvedColor.SampleLevel(linearClampSampler, input.uv, 0), 1.0);
}
