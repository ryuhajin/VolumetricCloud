// ============================================================================
//  VolumetricClouds.hlsl - 단계 3 높이 프로파일 밀도장과 합성
// ----------------------------------------------------------------------------
//  한 프레임의 렌더링 순서
//  1. CPU가 카메라와 CloudParameters를 b0/b1 상수버퍼에 복사한다.
//  2. 앞선 DiagnosticScene 패스가 불투명 Scene Color와 Scene Depth를 만든다.
//  3. 이 풀스크린 PS가 UV → 월드 레이 → 깊이 거리 순으로 복원한다.
//  4. 레이와 AABB의 교차 구간을 구하고 Scene Depth보다 뒤를 잘라 낸다.
//  5. 각 월드 샘플에서 noise 기본 밀도와 상·하단 높이 프로파일을 계산한다.
//  6. 위치별 밀도를 적분해 산란광과 투과율을 만든다.
//  7. 디버그 모드면 중간 값을, 모드 0이면 장면과 구름 합성을 출력한다.
//
//  단계 4 Detail Noise, 단계 5 Weather, 단계 6 Light와 단계 9 Early Exit는
//  의도적으로 없다. 지금은 저주파 noise 하나와 월드 Y 높이 마스크만 사용한다.
// ============================================================================

#include "Ray.hlsli"
#include "Noise.hlsli"

// CPU Renderer::CameraCB와 같은 96바이트 b0 상수버퍼다.
cbuffer cbCamera : register(b0)
{
    float4x4 invViewProj; // CPU invViewProj. UV/깊이를 월드 공간(m)으로 되돌린다.
    float3 cameraPos;     // CPU cameraPos. 월드 공간(m), 모든 레이의 원점.
    float time;           // CPU time. 초 단위, 단계 2 바람 이동에 사용한다.
    float2 renderSize;    // CPU renderSize. 현재 백버퍼 크기(pixel), 현재 예약 값.
    float nearPlane;      // CPU nearPlane. 카메라 근평면 거리(m), 현재 예약 값.
    float farPlane;       // CPU farPlane. 하늘 픽셀의 최대 추적 거리(m).
};

// t0: 앞선 DiagnosticScene PS가 R16G16B16A16_FLOAT에 쓴 linear RGB 장면색.
Texture2D<float4> sceneColorTexture : register(t0);
// t1: 같은 패스의 D32 depth를 R32_FLOAT SRV로 읽는 장치 깊이(near=0, far=1).
Texture2D<float> sceneDepthTexture : register(t1);
// s0: 깊이와 장면색을 픽셀 경계에서 섞지 않고 읽는 point+clamp sampler.
SamplerState pointClampSampler : register(s0);

// Fullscreen.hlsl의 정점 셰이더가 넘기는 화면 전체 삼각형 출력.
struct VSOut
{
    float4 position : SV_POSITION; // rasterizer가 정한 화면 픽셀 위치.
    float2 uv : TEXCOORD0;         // 좌상단 (0,0), 우하단 (1,1)의 화면 UV.
};

// 구름 패스가 이후 단계까지 유지할 합성 결과.
struct CloudResult
{
    float3 scattering;       // 안개가 카메라 쪽으로 새로 더한 linear RGB 빛.
    float transmittance;     // 뒤 배경빛의 생존 비율. 1=완전 투명, 0=완전 불투명.
    float representativeDepth; // 적분 구간 대표 거리(m), 이후 temporal/upsample용.
};

// 단계 1 교차, 단계 2 noise와 단계 3 높이 적분이 사용한 대표값 진단 자료.
struct CloudMarchDebug
{
    float entryDistance;   // Scene Depth 제한 전 AABB 진입을 0 이상으로 자른 거리(m).
    float exitDistance;    // Scene Depth로 제한된 실제 이탈 거리(m).
    float stepCount;       // 실제 반복 횟수. 색 출력 편의를 위해 float로 보관.
    float sampledDensity;  // 대표 중간 위치의 최종 noise 밀도. hit가 없으면 0.
    float hit;             // 유효 적분 구간이면 1, 아니면 0.
    float rawNoise;        // 대표 중간 위치의 threshold 전 value noise(0~1).
    float thresholdDensity;// coverage threshold와 remap만 적용한 밀도(0~1).
    float heightFraction;  // 대표 위치의 AABB 정규화 높이. 바닥 0, 천장 1.
    float heightProfile;   // 대표 위치의 상·하단 fade 곱(0~1).
    float3 noiseUvw;       // 대표 중간 위치의 연속 noise 좌표(cycle).
};

// 화면 UV를 DirectX NDC로 바꾼다.
// 입력/출력은 단위 없는 좌표이며 UV의 아래 방향 Y를 NDC의 위 방향 Y로 뒤집는다.
float2 UvToNdc(float2 uv)
{
    return float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
}

// 화면 UV가 가리키는 월드 레이 방향을 복원한다.
// 출력은 길이 1인 월드 방향이므로 이후 tStart/tEnd가 meter 거리가 된다.
// homogeneous w가 0에 가까울 때 안전값을 사용해 NaN을 막는다.
float3 ReconstructWorldRay(float2 uv)
{
    // 1. far plane의 NDC 점을 inverse view-projection으로 월드에 되돌린다.
    float2 ndc = UvToNdc(uv);
    float4 farH = mul(float4(ndc, 1.0, 1.0), invViewProj);
    float safeW = abs(farH.w) > 1e-6 ? farH.w : (farH.w < 0.0 ? -1e-6 : 1e-6);
    float3 farPosition = farH.xyz / safeW;

    // 2. 카메라에서 그 점으로 향하는 화살표를 정규화한다.
    return normalize(farPosition - cameraPos);
}

// 장치 깊이와 화면 UV에서 불투명 표면의 월드 위치(m)를 복원한다.
// deviceDepth는 D3D 범위 [0,1]이며 비선형이므로 farPlane을 단순히 곱하지 않는다.
float3 ReconstructWorldPosition(float2 uv, float deviceDepth)
{
    // 1. 같은 픽셀의 NDC X/Y와 깊이를 한 점으로 만든다.
    float4 worldH = mul(float4(UvToNdc(uv), deviceDepth, 1.0), invViewProj);

    // 2. 원근 나눗셈을 안전하게 수행해 월드 위치를 얻는다.
    float safeW = abs(worldH.w) > 1e-6 ? worldH.w : (worldH.w < 0.0 ? -1e-6 : 1e-6);
    return worldH.xyz / safeW;
}

// 불투명 장면이 없는 픽셀 뒤에 사용할 간단한 배경 하늘색.
float3 SkyColor(float3 rayDirection)
{
    float height = saturate(rayDirection.y * 0.5 + 0.5);
    return lerp(float3(0.55, 0.63, 0.72), float3(0.12, 0.27, 0.52), height);
}

// 현재 CloudCB의 월드 AABB와 레이를 교차하고 실제 적분 구간을 만든다.
// sceneDistance(m)는 첫 불투명 표면까지 거리이며 그 뒤쪽 안개를 보이지 않게 자른다.
// 카메라가 박스 안이면 raw tNear가 음수이므로 tStart를 0으로 고정한다.
bool IntersectCloudVolume(float3 rayOrigin, float3 rayDirection,
                          float sceneDistance,
                          out float tStart, out float tEnd)
{
    tStart = 0.0;
    tEnd = 0.0;

    // 1. AABB 자체의 진입/이탈 거리를 구한다.
    float tNear = 0.0;
    float tFar = 0.0;
    bool intersectsAabb = IntersectRayAABB(
        rayOrigin, rayDirection, cloudBoundsMin, cloudBoundsMax, tNear, tFar);

    // 2. 뒤쪽 진입은 카메라 위치부터, 이탈은 첫 불투명 물체까지만 허용한다.
    //    tStart가 커지면 안개가 더 멀리서 시작하고, tEnd가 작아지면 보이는 두께가 줄어든다.
    if (intersectsAabb)
    {
        tStart = max(tNear, 0.0);
        tEnd = min(tFar, sceneDistance);
    }

    // 3. 물체가 볼륨 앞에 있거나 접점뿐이면 계산할 부피가 없다.
    return intersectsAabb && tEnd > tStart;
}

// AABB의 유효 구간을 noise × 높이 프로파일 밀도로 레이 마칭한다.
// 입력은 월드 위치(m), 정규화 월드 방향, 장면 거리(m)이고 출력은 합성 가능한
// CloudResult와 관찰용 CloudMarchDebug다. 모든 실패 경로는 산란 0, 투과율 1의
// 중립 결과를 반환해 배경을 바꾸지 않는다.
CloudResult RaymarchCloud(float3 rayOrigin, float3 rayDirection,
                          float sceneDistance,
                          out CloudMarchDebug debugData)
{
    CloudResult result = (CloudResult)0;
    result.transmittance = 1.0;
    result.representativeDepth = sceneDistance;

    debugData = (CloudMarchDebug)0;

    // 1. 물체 폐색까지 반영된 AABB 구간을 구한다.
    float tStart = 0.0;
    float tEnd = 0.0;
    bool hasSegment = IntersectCloudVolume(
        rayOrigin, rayDirection, sceneDistance, tStart, tEnd);
    if (hasSegment)
    {
        debugData.entryDistance = tStart;
        debugData.exitDistance = tEnd;

        // 2. stepSize는 목표 간격이고, 실제 간격은 전체 구간을 빠짐없이 덮도록 다시 나눈다.
        float segmentLength = tEnd - tStart;
        float safeStepSize = max(stepSize, 1e-4);
        uint safeMaxSteps = max(maxViewSteps, 1u);
        uint stepCount = min(safeMaxSteps, (uint)ceil(segmentLength / safeStepSize));
        float actualStepLength = segmentLength / (float)stepCount;

        // 3. 고정 산란색은 유지하고 위치별 noise 밀도만 Beer-Lambert에 연결한다.
        float extinction = max(extinctionCoefficient, 0.0);
        const float3 fixedFogColor = float3(0.82, 0.86, 0.92);

        // 디버그 모드는 같은 대표 위치에서 raw→threshold→final→UVW를 비교한다.
        float representativeDistance = (tStart + tEnd) * 0.5;
        float3 representativePosition =
            rayOrigin + rayDirection * representativeDistance;
        CloudDensitySample representativeSample =
            SampleCloudDensity(representativePosition, time);
        debugData.rawNoise = representativeSample.rawNoise;
        debugData.thresholdDensity = representativeSample.thresholdDensity;
        debugData.heightFraction = representativeSample.heightFraction;
        debugData.heightProfile = representativeSample.heightProfile;
        debugData.sampledDensity = representativeSample.finalDensity;
        debugData.noiseUvw = representativeSample.noiseUvw;

        // 4. 각 구간 중앙에서 noise와 높이 프로파일이 결합된 최종 밀도를 평가한다.
        //    단계 4는 이 finalDensity 앞에 작은 detail erosion을 추가할 예정이다.
        [loop]
        for (uint stepIndex = 0u; stepIndex < stepCount; ++stepIndex)
        {
            float sampleDistance = tStart + ((float)stepIndex + 0.5) * actualStepLength;
            float3 samplePosition = rayOrigin + rayDirection * sampleDistance;
            CloudDensitySample densitySample = SampleCloudDensity(samplePosition, time);
            float sampledDensity = densitySample.finalDensity;
            float sampledStepTransmittance = exp(
                -sampledDensity * extinction * actualStepLength);

            // 아직 살아남은 빛의 비율만큼 이 step의 고정 안개색을 더한다.
            result.scattering += result.transmittance * fixedFogColor *
                                 (1.0 - sampledStepTransmittance);
            result.transmittance *= sampledStepTransmittance;
            // 단계 9 Early Exit 자리: 현재는 transmittanceThreshold를 사용하지 않고
            // 항상 stepCount 전체를 돌아 fine/coarse 적분의 동일성을 먼저 검증한다.
        }

        // 5. 디버그와 이후 temporal 단계가 사용할 최종 값을 기록한다.
        result.transmittance = saturate(result.transmittance);
        result.representativeDepth = (tStart + tEnd) * 0.5;
        debugData.stepCount = (float)stepCount;
        debugData.hit = 1.0;
    }
    return result;
}

// 최종 풀스크린 픽셀 셰이더.
// UV → 레이 → 깊이 → 교차 → 레이 마칭 → 디버그 → 합성 순서를 한곳에서 보여 준다.
float4 main(VSOut input) : SV_TARGET
{
    // 1. UV를 유효 범위로 제한하고 같은 픽셀의 Scene Depth를 읽는다.
    float2 uv = saturate(input.uv);
    float deviceDepth = sceneDepthTexture.SampleLevel(pointClampSampler, uv, 0);
    bool hasGeometry = deviceDepth < 0.999999;

    // 2. 카메라에서 픽셀로 나가는 월드 레이를 복원한다.
    float3 rayDirection = ReconstructWorldRay(uv);

    // 3. 깊이가 있으면 월드 표면과 meter 거리를 복원하고, 하늘이면 farPlane을 쓴다.
    float3 worldPosition = hasGeometry
        ? ReconstructWorldPosition(uv, deviceDepth)
        : cameraPos + rayDirection * farPlane;
    float sceneDistance = hasGeometry
        ? length(worldPosition - cameraPos)
        : farPlane;

    // 4. AABB 교차와 Scene Depth 제한 뒤 noise × 높이 프로파일 밀도를 적분한다.
    CloudMarchDebug marchDebug;
    CloudResult cloud = RaymarchCloud(
        cameraPos, rayDirection, sceneDistance, marchDebug);

    // 5. 단계 0 디버그 1~4는 그대로 유지한다.
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

    // 6. 단계 1 디버그 5~9는 교차·step·투과율·밀도를 각각 분리해 보여 준다.
    if (debugMode == 5)
        return float4((marchDebug.hit * saturate(marchDebug.entryDistance / 20.0)).xxx, 1.0);
    if (debugMode == 6)
        return float4((marchDebug.hit * saturate(marchDebug.exitDistance / 20.0)).xxx, 1.0);
    if (debugMode == 7)
        return float4((marchDebug.hit * saturate(marchDebug.stepCount / max((float)maxViewSteps, 1.0))).xxx, 1.0);
    if (debugMode == 8)
        return float4(cloud.transmittance.xxx, 1.0);
    if (debugMode == 9)
        return float4((marchDebug.sampledDensity * marchDebug.hit).xxx, 1.0);
    if (debugMode == 10)
        return float4((marchDebug.rawNoise * marchDebug.hit).xxx, 1.0);
    if (debugMode == 11)
        return float4((marchDebug.thresholdDensity * marchDebug.hit).xxx, 1.0);
    if (debugMode == 12)
        return float4((marchDebug.sampledDensity * marchDebug.hit).xxx, 1.0);
    if (debugMode == 13)
        return float4(frac(marchDebug.noiseUvw) * marchDebug.hit, 1.0);
    if (debugMode == 14)
        return float4((marchDebug.heightFraction * marchDebug.hit).xxx, 1.0);
    if (debugMode == 15)
        return float4((marchDebug.heightProfile * marchDebug.hit).xxx, 1.0);

    // 7. 모드 0: 안개가 더한 빛 + 안개를 통과한 배경빛으로 최종 합성한다.
    float3 background = hasGeometry
        ? sceneColorTexture.SampleLevel(pointClampSampler, uv, 0).rgb
        : SkyColor(rayDirection);
    float3 composite = cloud.scattering + background * cloud.transmittance;
    return float4(composite, 1.0);
}
