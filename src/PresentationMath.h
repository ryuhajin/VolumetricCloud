// ============================================================================
//  PresentationMath.h - VSync/tearing Present 인수의 순수 CPU 계약
// ============================================================================
#pragma once

#include <cstdint>

namespace presentation
{
// DXGI_PRESENT_ALLOW_TEARING. Renderer.cpp에서 SDK 상수와 일치하는지 검사한다.
constexpr std::uint32_t kAllowTearingPresentFlag = 0x00000200u;

struct PresentParameters
{
    std::uint32_t syncInterval = 1u;
    std::uint32_t flags = 0u;
};

constexpr PresentParameters ResolvePresentParameters(
    bool vsyncEnabled, bool tearingSupported, bool windowed = true)
{
    if (vsyncEnabled)
        return { 1u, 0u };
    return {
        0u,
        tearingSupported && windowed ? kAllowTearingPresentFlag : 0u
    };
}
}
