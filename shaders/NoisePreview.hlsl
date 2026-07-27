// 3D 절차식 노이즈를 선택한 축의 2D 단면으로 렌더링한다.
#include "CloudNoise.hlsli"

cbuffer NoisePreviewCB : register(b2)
{
    int previewAxis;
    float previewSlice;
    float previewTime;
    int previewSource;
};

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

VSOut VSMain(uint vertexId : SV_VertexID)
{
    VSOut output;
    float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(-1.0,  3.0),
        float2( 3.0, -1.0)
    };
    float2 uvs[3] = {
        float2(0.0, 1.0),
        float2(0.0, -1.0),
        float2(2.0, 1.0)
    };
    output.pos = float4(positions[vertexId], 0.0, 1.0);
    output.uv = uvs[vertexId];
    return output;
}

struct PreviewOutput
{
    float4 baseShape   : SV_TARGET0;
    float4 detailNoise : SV_TARGET1;
    float4 heightMask  : SV_TARGET2;
    float4 finalDensity: SV_TARGET3;
};

PreviewOutput PSMain(VSOut input)
{
    float2 uv = saturate(input.uv);
    float3 uvw;
    if (previewAxis == 0)      uvw = float3(uv.x, 1.0 - uv.y, previewSlice); // XY
    else if (previewAxis == 1) uvw = float3(uv.x, previewSlice, 1.0 - uv.y); // XZ
    else                       uvw = float3(previewSlice, 1.0 - uv.y, uv.x); // YZ

    float4 components = EvaluatePreviewCloudComponents(uvw, previewTime);
    PreviewOutput output;
    output.baseShape    = float4(components.xxx, 1.0);
    output.detailNoise  = float4(components.yyy, 1.0);
    output.heightMask   = float4(components.zzz, 1.0);
    output.finalDensity = float4(saturate(components.www * densityMultiplier), 1.0);
    return output;
}
