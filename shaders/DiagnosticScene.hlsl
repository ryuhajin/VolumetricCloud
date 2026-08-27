// ============================================================================
//  DiagnosticScene.hlsl - 구름보다 먼저 그리는 단계 1 불투명 진단 장면
// ----------------------------------------------------------------------------
//  렌더링 순서
//  1. CPU가 지면과 두 박스의 정점/인덱스, 카메라 viewProj를 준비한다.
//  2. VSMain이 월드 위치(m)를 clip 공간으로 투영한다.
//  3. rasterizer가 가장 가까운 표면 깊이를 D32_FLOAT에 자동 기록한다.
//  4. PSMain이 대기 태양·하늘광으로 Lambert 조명한 HDR 색을 기록한다.
//  5. 다음 VolumetricClouds 패스가 두 결과를 t0/t1로 읽어 안개 뒤 배경과
//     레이 마칭의 최대 거리로 사용한다.
// ============================================================================

// CPU Renderer::SceneCB와 같은 64바이트 b0 상수버퍼.
cbuffer cbScene : register(b0)
{
    float4x4 viewProj; // CPU viewProj. 월드 공간(m)을 D3D clip 공간으로 변환, 현재 사용.
};

#include "Stage12Shadow.hlsli"
#include "Stage14Atmosphere.hlsli"

// Renderer가 만든 DiagnosticSceneVertex의 GPU 입력 배치.
struct VSInput
{
    float3 position : POSITION; // 월드 공간 정점 위치(m).
    float3 color : COLOR;       // 조명 없는 진단용 linear RGB 색.
    float3 normal : NORMAL;     // 박스는 면별 flat normal, 지면은 +Y.
    uint materialId : MATERIALID; // 0=지면 preset, 1=건물 콘크리트.
};

// 정점 셰이더가 rasterizer와 픽셀 셰이더에 전달하는 값.
struct VSOutput
{
    float4 position : SV_POSITION; // clip 위치. rasterizer가 장치 깊이 [0,1]도 계산한다.
    float3 color : COLOR;           // 삼각형 안에서 보간할 linear RGB.
    float3 worldPosition : TEXCOORD0;
    float3 normal : TEXCOORD1;
    nointerpolation uint materialId : TEXCOORD2;
};

// 월드 정점을 현재 카메라 화면으로 투영한다.
// 입력 위치는 meter 단위 월드 좌표, 출력 위치는 단위 없는 clip 좌표다.
// 일반적인 유한 viewProj 행렬만 사용하므로 별도의 나눗셈이나 NaN 위험은 없다.
VSOutput VSMain(VSInput input)
{
    // 1. 월드 위치를 homogeneous 좌표로 만들고 view-projection을 적용한다.
    VSOutput output;
    output.position = mul(float4(input.position, 1.0), viewProj);

    // 2. 표면 구분용 고정색을 그대로 다음 단계에 전달한다.
    output.color = input.color;
    output.worldPosition = input.position;
    output.normal = input.normal;
    output.materialId = input.materialId;
    return output;
}

// 불투명 진단 표면의 색을 Scene Color에 기록한다.
// 깊이는 반환하지 않아도 고정 기능 rasterizer가 SV_POSITION에서 D32에 기록한다.
float4 PSMain(VSOutput input) : SV_TARGET
{
    if (modeFlags.x != kAtmosphereModePhysical)
        return float4(input.color, 1.0);

    float3 normal = normalize(input.normal);
    float3 sunDirection = AtmosphereSunDirection();
    float altitudeKm = max(input.worldPosition.y, 0.0) * 0.001;
    float3 atmosphereSun = SampleAtmosphereSunRadiance(
        altitudeKm, sunDirection);
    float surfaceT = Stage12SurfaceFactor(
        Stage12SurfaceTransmittance(input.worldPosition));
    float3 surfaceDirect = atmosphereSun *
        saturate(dot(normal, sunDirection)) * surfaceT;

    float heightFraction = saturate(altitudeKm /
        max(AtmosphereTopRadiusKm() - AtmosphereBottomRadiusKm(), 1.0e-6));
    float2 skyUv = float2(sunDirection.y * 0.5 + 0.5, heightFraction);
    float3 skyIrradiance = atmosphereSkyIrradianceLut.SampleLevel(
        atmosphereLinearClampSampler, skyUv, 0).rgb;
    float skyNormalWeight = saturate(normal.y * 0.5 + 0.5);
    float3 surfaceSky = skyIrradiance * skyNormalWeight;
    if (modeFlags.z == 10u)
        return float4(surfaceDirect, 1.0);
    if (modeFlags.z == 11u)
        return float4(surfaceSky, 1.0);

    float3 albedo = input.materialId == 0u
        ? max(groundAlbedoAndDebugExposure.xyz, 0.0.xxx)
        : float3(0.18, 0.18, 0.18);
    float3 surfaceColor = albedo * (surfaceDirect + surfaceSky) /
                          kAtmospherePi;
    return float4(max(surfaceColor, 0.0.xxx), 1.0);
}
