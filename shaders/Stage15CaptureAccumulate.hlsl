// ============================================================================
//  Stage15CaptureAccumulate.hlsl - Capture Still HDR running-average 입력
// ----------------------------------------------------------------------------
//  blend state가 current/(n+1) + previous*n/(n+1)을 계산한다. 이 PS는
//  Tone Map 전 HDR 표본을 필터링 없이 같은 물리 픽셀로 전달한다.
// ============================================================================

Texture2D<float4> hdrSampleTexture : register(t0);

struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(VSOut input) : SV_TARGET
{
    uint width = 1u;
    uint height = 1u;
    hdrSampleTexture.GetDimensions(width, height);
    int2 pixel = clamp(int2(input.position.xy), int2(0, 0),
                       int2(max(width, 1u), max(height, 1u)) - 1);
    return max(hdrSampleTexture.Load(int3(pixel, 0)), 0.0.xxxx);
}
