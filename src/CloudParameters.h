// ============================================================================
//  CloudParameters.h - 단계별 구름 렌더러가 CPU/GPU에서 공유하는 설정
// ============================================================================
#pragma once

#include <DirectXMath.h>
#include <cstdint>

enum class CloudDebugMode : std::int32_t
{
    Composite = 0,
    RayDirection = 1,
    SceneDepth = 2,
    WorldPosition = 3,
    ScreenUv = 4,
};

// HLSL CloudCB와 16바이트 묶음 순서가 정확히 일치해야 한다.
struct alignas(16) CloudParameters
{
    DirectX::XMFLOAT3 cloudBoundsMin = { -2.0f, -1.0f, -2.0f };
    float cloudDensity = 0.0f;

    DirectX::XMFLOAT3 cloudBoundsMax = { 2.0f, 2.0f, 2.0f };
    float stepSize = 0.1f;

    std::uint32_t maxViewSteps = 0;
    float extinctionCoefficient = 0.0f;
    float transmittanceThreshold = 0.01f;
    std::int32_t debugMode = static_cast<std::int32_t>(CloudDebugMode::Composite);
};

static_assert(sizeof(CloudParameters) == 48, "CloudParameters must match CloudCB");
