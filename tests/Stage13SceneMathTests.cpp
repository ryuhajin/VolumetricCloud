#include "Stage13SceneMath.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "Stage13SceneMath failure: " << message << '\n';
        std::exit(1);
    }
}
}

int main()
{
    using namespace stage13scene;
    Require(kGroundSizeMeters == 10000.0f &&
            kGroundHalfSizeMeters == 5000.0f,
            "ground contract must be 10km square");
    Require(kBuildingWidthMeters == 20.0f &&
            kBuildingHeightMeters == 60.0f &&
            kBuildingDepthMeters == 20.0f,
            "building contract must be 20x60x20m");
    Require(kSupportedSkyRadiusMeters == 50000.0f,
            "analytic sky and cloud support radius must be 50km");
    Require(SanitizeMoveSpeed(-1.0f) == kMinimumMoveSpeedMetersPerSecond &&
            SanitizeMoveSpeed(99999.0f) == kMaximumMoveSpeedMetersPerSecond &&
            SanitizeMoveSpeed(NAN) == kMoveSpeedMetersPerSecond,
            "movement speed must clamp and reject non-finite input");
    Require(MovementDistance(1.0f, false) == 100.0f &&
            MovementDistance(1.0f, true) == 400.0f,
            "movement delta clamp and Shift multiplier must be deterministic");
    Require(IsCameraMovementKey('W') && IsCameraMovementKey('A') &&
            IsCameraMovementKey('S') && IsCameraMovementKey('D') &&
            !IsCameraMovementKey('Q'),
            "only WASD are scene movement letters");
    for (std::uint32_t key = static_cast<std::uint32_t>('A');
         key <= static_cast<std::uint32_t>('Z'); ++key)
    {
        const bool expected = IsCameraMovementKey(key);
        Require(IsPortfolioGlobalKey(key) == expected,
                "removed diagnostic letters must remain unhandled");
    }
    for (std::uint32_t key = 0x70u; key <= 0x77u; ++key)
        Require(IsPortfolioGlobalKey(key), "F1-F8 must remain handled");
    Require(DeveloperUiPanelFromVirtualKey(0x70u) == 0 &&
            DeveloperUiPanelFromVirtualKey(0x71u) == 1 &&
            DeveloperUiPanelFromVirtualKey(0x73u) == 3 &&
            DeveloperUiPanelFromVirtualKey(0x74u) == -1,
            "F1-F4 must map to all four reopenable developer panels");
    for (std::uint32_t key = 0x78u; key <= 0x7bu; ++key)
        Require(!IsPortfolioGlobalKey(key), "F9-F12 must remain unhandled");
    Require(DebugModeFromDigit(0) == CloudDebugMode::Composite &&
            DebugModeFromDigit(1) == CloudDebugMode::RawNoise &&
            DebugModeFromDigit(9) == CloudDebugMode::LightTransmittance &&
            DebugModeFromDigit(42) == CloudDebugMode::Composite,
            "digit table and invalid-input sanitization must be explicit");
    for (std::int32_t removed = 1; removed <= 7; ++removed)
        Require(SanitizeDebugMode(static_cast<CloudDebugMode>(removed)) ==
                    CloudDebugMode::Composite,
                "removed HLSL IDs 1-7 must sanitize to Composite");
    Require(SanitizeDebugMode(CloudDebugMode::LocalBaseOffset) ==
                CloudDebugMode::LocalBaseOffset &&
            SanitizeDebugMode(CloudDebugMode::CloudDepth) ==
                CloudDebugMode::CloudDepth,
            "High-only diagnostics must be preserved");
    for (std::int32_t removed = 64; removed <= 73; ++removed)
        Require(SanitizeDebugMode(static_cast<CloudDebugMode>(removed)) ==
                    CloudDebugMode::Composite,
                "removed legacy diagnostics stay invalid");
    for (std::int32_t removed = 78; removed <= 80; ++removed)
        Require(SanitizeDebugMode(static_cast<CloudDebugMode>(removed)) ==
                    CloudDebugMode::Composite,
                "removed comparison diagnostics stay invalid");
    Require(SanitizeDebugMode(static_cast<CloudDebugMode>(82)) ==
                CloudDebugMode::Composite,
            "withdrawn boundary diagnostic ID 82 must stay reserved");
    Require(SanitizeDebugMode(static_cast<CloudDebugMode>(83)) ==
                CloudDebugMode::Composite,
            "removed rim diagnostics must sanitize");
    std::cout << "[UNIFIED-SCENE][MATH] GROUND=10000 BUILDING=20x60x20 RADIUS=50000 PASS\n";
    return 0;
}
