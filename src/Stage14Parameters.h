// ============================================================================
//  Stage14Parameters.h - 단계 14 정규화 GPU 상수버퍼(b13), 정확히 224바이트
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
    DirectX::XMFLOAT4 planetRadiiDensityHeights;
    DirectX::XMFLOAT4 rayleighScatteringAndScale;
    DirectX::XMFLOAT4 mieScatteringExtinctionGAbsorption;
    DirectX::XMFLOAT4 ozoneAbsorptionAndScale;
    DirectX::XMFLOAT4 ozoneLayerTurbidityAerialDistance;
    DirectX::XMFLOAT4 solarIrradianceAndMultiplier;
    DirectX::XMFLOAT4 sunDirectionAndCameraHeight;
    DirectX::XMFLOAT4 sunTintAndGroundBounce;
    DirectX::XMFLOAT4 groundAlbedoAndDebugExposure;
    DirectX::XMFLOAT4 toneAndTime;
    DirectX::XMUINT4 renderFlags;
    DirectX::XMFLOAT4 transmittanceMultiSize;
    DirectX::XMFLOAT4 skyViewIrradianceSize;
    DirectX::XMUINT4 aerialDebugGeneration;
};

static_assert(sizeof(GpuParameters) == 224,
              "Stage14 GPU cbuffer must be exactly 224 bytes");
}
