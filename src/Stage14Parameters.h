// ============================================================================
//  Stage14Parameters.h - 단계 14 정규화 GPU 상수버퍼(b9), 정확히 224바이트
// ============================================================================
#pragma once

#include "AtmosphereParameters.h"
#include "GroundLightingParameters.h"
#include "ToneMappingParameters.h"

#include <DirectXMath.h>

#include <cstdint>

namespace stage14
{
constexpr std::uint32_t kTransmittanceWidth = 256u;
constexpr std::uint32_t kTransmittanceHeight = 64u;
constexpr std::uint32_t kMultiScatteringWidth = 32u;
constexpr std::uint32_t kMultiScatteringHeight = 32u;
constexpr std::uint32_t kSkyViewWidth = 192u;
constexpr std::uint32_t kSkyViewHeight = 108u;
constexpr std::uint32_t kSkyIrradianceWidth = 64u;
constexpr std::uint32_t kSkyIrradianceHeight = 16u;
constexpr std::uint32_t kAerialSize = 32u;

// HLSL Stage14CB(b9)의 14개 16바이트 레지스터와 순서가 같다.
struct alignas(16) GpuParameters
{
    // [파생 값] Atmosphere 원본 → x=행성 반지름 km [1,100000], y=대기 반지름 [x+1,x+1000], z=Rayleigh 높이 [4,16], w=Mie 높이 [0.5,4]. Earth (6360,6460,8,1.2).
    DirectX::XMFLOAT4 planetRadiiDensityHeights;
    // [파생 값] xyz=Rayleigh RGB 1/km >=0, w=scale [0.25,4]. 기본 (0.005802,0.013558,0.033100,1).
    DirectX::XMFLOAT4 rayleighScatteringAndScale;
    // [파생 값] x=Mie 산란 1/km >=0, y=소멸 1/km >=x, z=g [0,0.95], w=흡수 배율 [0,4]. 기본 (0.003996,0.004440,0.8,1).
    DirectX::XMFLOAT4 mieScatteringExtinctionGAbsorption;
    // [파생 값] xyz=오존 RGB 흡수 1/km >=0, w=scale [0,4]. 기본 (0.000650,0.001881,0.000085,1).
    DirectX::XMFLOAT4 ozoneAbsorptionAndScale;
    // [파생 값] x=오존 중심 km [0,100], y=반폭 km [0.1,100], z=turbidity [0.25,4], w=Aerial 거리 128km 고정. 기본 (25,15,1,128).
    DirectX::XMFLOAT4 ozoneLayerTurbidityAerialDistance;
    // [파생 값] xyz=Atmosphere 태양 RGB 기준 >=0(기본 1), w=Light.sunIntensity >=0(기본 1). 음수/비유한은 CPU 보정.
    DirectX::XMFLOAT4 solarIrradianceAndMultiplier;
    // [파생 값] xyz=대기 표본→태양 단위벡터(각도에서 생성), w=max(camera.y,0)*0.001 km. 위치 m와 혼용 금지.
    DirectX::XMFLOAT4 sunDirectionAndCameraHeight;
    // [파생 값] xyz=Light.sunColor 선형 RGB >=0, w=Ground.bounceMultiplier [0,2]. 기본 (1,0.95,0.85,1).
    DirectX::XMFLOAT4 sunTintAndGroundBounce;
    // [파생 값] xyz=Ground RGB 반사율 [0,1], w=대기 진단 노출 [0.001,128]. 기본 (0.18,0.18,0.18,1).
    DirectX::XMFLOAT4 groundAlbedoAndDebugExposure;
    // [파생 값] x=EV [-8,8], y=백색점 K [3500,10000], z=시각 hour [5.5,19.5], w=태양 고도 도 [-6,90]. 초기 (0,6500,7.5,18).
    DirectX::XMFLOAT4 toneAndTime;
    // [파생 값] x=구름 거리 대기 진단 0/91/92, y=Tone, z=대기 진단, w=채널. 기본 모두 0.
    DirectX::XMUINT4 renderFlags;
    // [고정 품질] x/y=Transmittance 폭/높이 256/64, z/w=Multi 폭/높이 32/32 texel.
    DirectX::XMFLOAT4 transmittanceMultiSize;
    // [고정 품질] x/y=SkyView 폭/높이 192/108, z/w=SkyIrradiance 폭/높이 64/16 texel.
    DirectX::XMFLOAT4 skyViewIrradianceSize;
    // [파생 값] x=Aerial 축 32 고정, y=진단 slice uint [0,31], z=최대 LUT generation의 하위 32bit, w=0 예약.
    DirectX::XMUINT4 aerialDebugGeneration;
};

static_assert(sizeof(GpuParameters) == 224,
              "Stage14 GPU cbuffer must be exactly 224 bytes");
}
