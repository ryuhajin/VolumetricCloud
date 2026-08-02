// ============================================================================
//  VolumetricClouds.hlsl - 단계 0 카메라 레이/깊이/합성 진단 패스
// ----------------------------------------------------------------------------
//  아직 볼륨 교차, 밀도, 레이 마칭과 조명을 수행하지 않는다.
// ============================================================================

cbuffer cbCamera : register(b0)
{
    float4x4 invViewProj;
    float3 cameraPos;
    float time;
    float2 renderSize;
    float nearPlane;
    float farPlane;
};

cbuffer CloudCB : register(b1)
{
    float3 cloudBoundsMin;
    float cloudDensity;
    float3 cloudBoundsMax;
    float stepSize;
    uint maxViewSteps;
    float extinctionCoefficient;
    float transmittanceThreshold;
    int debugMode;
};

Texture2D<float4> sceneColorTexture : register(t0);
Texture2D<float> sceneDepthTexture : register(t1);
SamplerState pointClampSampler : register(s0);

struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

struct CloudResult
{
    float3 scattering;
    float transmittance;
    float representativeDepth;
};

float2 UvToNdc(float2 uv)
{
    return float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
}

// 입력: 화면 UV. 출력: 정규화된 월드 공간 카메라 레이 방향.
float3 ReconstructWorldRay(float2 uv)
{
    float2 ndc = UvToNdc(uv);
    float4 farH = mul(float4(ndc, 1.0, 1.0), invViewProj);
    float safeW = abs(farH.w) > 1e-6 ? farH.w : 1e-6;
    float3 farPosition = farH.xyz / safeW;
    return normalize(farPosition - cameraPos);
}

// 입력: 화면 UV와 D3D 깊이 [0,1]. 출력: meter 단위 월드 위치.
float3 ReconstructWorldPosition(float2 uv, float deviceDepth)
{
    float4 worldH = mul(float4(UvToNdc(uv), deviceDepth, 1.0), invViewProj);
    float safeW = abs(worldH.w) > 1e-6 ? worldH.w : 1e-6;
    return worldH.xyz / safeW;
}

float3 SkyColor(float3 rayDirection)
{
    float height = saturate(rayDirection.y * 0.5 + 0.5);
    return lerp(float3(0.55, 0.63, 0.72), float3(0.12, 0.27, 0.52), height);
}

// 단계 0의 합성 경로를 눈으로 확인하는 임시 반투명 원판이다.
// 밀도나 구름 경계를 뜻하지 않으며 단계 1의 AABB 결과로 교체된다.
CloudResult EvaluateFoundationOverlay(float2 uv)
{
    float radius = length((uv - 0.5) * float2(renderSize.x / max(renderSize.y, 1.0), 1.0));
    float mask = 1.0 - smoothstep(0.19, 0.205, radius);

    CloudResult result;
    result.scattering = float3(0.02, 0.15, 0.20) * mask;
    result.transmittance = lerp(1.0, 0.72, mask);
    result.representativeDepth = farPlane;
    return result;
}

float4 main(VSOut input) : SV_TARGET
{
    float2 uv = saturate(input.uv);
    float deviceDepth = sceneDepthTexture.SampleLevel(pointClampSampler, uv, 0);
    bool hasGeometry = deviceDepth < 0.999999;
    float3 rayDirection = ReconstructWorldRay(uv);
    float3 worldPosition = hasGeometry
        ? ReconstructWorldPosition(uv, deviceDepth)
        : cameraPos + rayDirection * farPlane;
    float sceneDistance = hasGeometry
        ? length(worldPosition - cameraPos)
        : farPlane;

    if (debugMode == 1)
        return float4(rayDirection * 0.5 + 0.5, 1.0);
    if (debugMode == 2)
        return float4(saturate(sceneDistance / 30.0).xxx, 1.0);
    if (debugMode == 3)
    {
        float3 positionBands = frac(abs(worldPosition) * 0.2);
        return float4(hasGeometry ? positionBands : 0.0.xxx, 1.0);
    }
    if (debugMode == 4)
        return float4(uv, 0.0, 1.0);

    float3 background = hasGeometry
        ? sceneColorTexture.SampleLevel(pointClampSampler, uv, 0).rgb
        : SkyColor(rayDirection);
    CloudResult cloud = EvaluateFoundationOverlay(uv);
    float3 composite = cloud.scattering + background * cloud.transmittance;
    return float4(composite, 1.0);
}
