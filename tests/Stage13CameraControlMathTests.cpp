#include "Camera.h"
#include "Stage13CameraPresets.h"
#include "Stage13CloudDomainMath.h"
#include "Stage13SceneMath.h"

#include <DirectXMath.h>

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "Stage13CameraControlMath failure: " << message << '\n';
        std::exit(1);
    }
}

bool Near(float a, float b, float epsilon = 1e-3f)
{
    return std::abs(a - b) <= epsilon;
}

bool Finite(const DirectX::XMFLOAT3& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

DirectX::XMFLOAT3 Direction(const Stage13CameraPreset& preset)
{
    using namespace DirectX;
    XMFLOAT3 result;
    XMStoreFloat3(&result, XMVector3Normalize(XMVectorSubtract(
        XMLoadFloat3(&preset.target), XMLoadFloat3(&preset.position))));
    return result;
}

bool InsideDiagnosticBuilding(const DirectX::XMFLOAT3& position)
{
    return position.x >= -10.0f && position.x <= 10.0f &&
           position.y >= 0.0f && position.y <= 60.0f &&
           position.z >= -10.0f && position.z <= 10.0f;
}

bool FiniteMatrix(DirectX::FXMMATRIX matrix)
{
    for (std::size_t row = 0; row < 4; ++row)
    {
        DirectX::XMFLOAT4 value;
        DirectX::XMStoreFloat4(&value, matrix.r[row]);
        if (!std::isfinite(value.x) || !std::isfinite(value.y) ||
            !std::isfinite(value.z) || !std::isfinite(value.w))
            return false;
    }
    return true;
}
}

int main()
{
    using namespace stage13camera;
    Require(stage13scene::DebugDigitFromVirtualKey('0') == 0 &&
            stage13scene::DebugDigitFromVirtualKey('9') == 9 &&
            stage13scene::DebugDigitFromVirtualKey(0x60u) == 0 &&
            stage13scene::DebugDigitFromVirtualKey(0x69u) == 9 &&
            stage13scene::DebugDigitFromVirtualKey('X') == -1,
            "top-row and numpad debug digits must share one mapping");
    Require(!stage13scene::ShouldBlockSceneKeyboard(false, false) &&
            stage13scene::ShouldBlockSceneKeyboard(true, false) &&
            stage13scene::ShouldBlockSceneKeyboard(false, true),
            "only text input or an actively edited UI item may block scene keys");
    Require(Near(stage13scene::MovementDistance(0.1f, false, 20.0f), 2.0f) &&
            Near(stage13scene::MovementDistance(0.1f, false, 1000.0f), 100.0f) &&
            Near(stage13scene::MovementDistance(0.1f, true, 1000.0f), 400.0f),
            "unified defaults and Shift multiplier must produce exact distances");
    const CloudDebugMode digitModes[] = {
        CloudDebugMode::Composite, CloudDebugMode::RawNoise,
        CloudDebugMode::WeatherCoverage, CloudDebugMode::BaseDensity,
        CloudDebugMode::DetailNoise, CloudDebugMode::FinalDensity,
        CloudDebugMode::ViewOpticalDepth,
        CloudDebugMode::AccumulatedDirectLighting,
        CloudDebugMode::Transmittance, CloudDebugMode::LightTransmittance };
    for (int digit = 0; digit <= 9; ++digit)
        Require(stage13scene::DebugModeFromDigit(digit) == digitModes[digit],
                "digit debug table must preserve the explicit HLSL IDs");
    Require(stage13scene::DebugModeFromDigit(-1) == CloudDebugMode::Composite &&
            stage13scene::DebugModeFromDigit(10) == CloudDebugMode::Composite,
            "invalid digit must sanitize to Composite");
    const Stage13CameraPreset& f5 = Get(Stage13CameraPresetId::HeroDepth);
    const Stage13CameraPreset& f6 = Get(Stage13CameraPresetId::GroundHorizon);
    const Stage13CameraPreset& f7 = Get(Stage13CameraPresetId::InsideLayer);
    const Stage13CameraPreset& f8 = Get(Stage13CameraPresetId::AboveLayer);

    Require(f5.position.y < 1500.0f && f6.position.y < 1500.0f,
            "F5/F6 must start below the Open World cloud layer");
    Require(f7.position.y > 1500.0f && f7.position.y < 7500.0f,
            "F7 must start inside the Open World cloud layer");
    Require(f8.position.y > 7500.0f,
            "F8 must start above the Open World cloud layer");
    for (const Stage13CameraPreset& preset : kOpenWorldPresets)
    {
        Require(Finite(preset.position) && Finite(preset.target),
                "preset position and target must be finite");
        Require(!InsideDiagnosticBuilding(preset.position),
                "unified scene camera must not start inside the building");

        Camera camera;
        camera.SetClipPlanes(kNearPlaneMeters, kFarPlaneMeters);
        camera.SetLookAt(preset.position, preset.target);
        const DirectX::XMFLOAT3 actual = camera.GetPosition();
        Require(Near(actual.x, preset.position.x, 0.02f) &&
                Near(actual.y, preset.position.y, 0.02f) &&
                Near(actual.z, preset.position.z, 0.02f),
                "SetLookAt must reconstruct the requested camera position");
        Require(FiniteMatrix(camera.GetViewProj()),
                "preset View-Projection matrix must be finite");
    }
    const float f5BuildingT = f5.position.z /
        (f5.position.z - f5.target.z);
    const float f5BuildingY = f5.position.y +
        (f5.target.y - f5.position.y) * f5BuildingT;
    Require(f5BuildingT > 0.0f && f5BuildingT < 1.0f &&
            f5BuildingY >= 0.0f && f5BuildingY <= 60.0f,
            "F5 center ray must intersect the 20x60x20m building");

    const DirectX::XMFLOAT3 f6Direction = Direction(f6);
    const auto f6Interval = stage13domain::IntersectPlanarLayer(
        f6.position.y, f6Direction.y, 1500.0, 6000.0, 60000.0, 50000.0);
    Require(f6Interval.hit && f6Interval.start < 50000.0,
            "F6 center ray must reach the cloud layer inside the View budget");
    const DirectX::XMFLOAT3 f8Direction = Direction(f8);
    const auto f8Interval = stage13domain::IntersectPlanarLayer(
        f8.position.y, f8Direction.y, 1500.0, 6000.0, 60000.0, 50000.0);
    Require(f8Direction.y < 0.0f && f8Interval.hit,
            "F8 center ray must look downward and enter the cloud layer");

    Camera zoomCamera;
    zoomCamera.SetLookAt({ 0.0f, 0.0f, 1000.0f }, { 0.0f, 0.0f, 0.0f });
    zoomCamera.Zoom(120.0f, CameraZoomSpeed::Normal);
    Require(Near(zoomCamera.GetDistance(), 800.0f),
            "normal wheel-in must divide distance by 1.25");
    zoomCamera.Zoom(-120.0f, CameraZoomSpeed::Normal);
    Require(Near(zoomCamera.GetDistance(), 1000.0f),
            "normal wheel round trip must restore distance");
    zoomCamera.Zoom(120.0f, CameraZoomSpeed::Fast);
    Require(Near(zoomCamera.GetDistance(), 500.0f),
            "Shift+wheel-in must divide distance by 2");
    zoomCamera.Zoom(-120.0f, CameraZoomSpeed::Fast);
    Require(Near(zoomCamera.GetDistance(), 1000.0f),
            "fast wheel round trip must restore distance");
    zoomCamera.Zoom(12000.0f, CameraZoomSpeed::Fast);
    Require(Near(zoomCamera.GetDistance(), 1.5f),
            "large wheel-in delta must clamp at the minimum distance");
    zoomCamera.Zoom(-12000.0f, CameraZoomSpeed::Fast);
    Require(Near(zoomCamera.GetDistance(), 100000.0f),
            "large wheel-out delta must clamp at the maximum distance");

    Camera fovCamera;
    fovCamera.SetFovYDegrees(75.0f);
    Require(Near(fovCamera.GetFovYDegrees(), 75.0f),
            "F4 camera FOV must round-trip in degrees");
    fovCamera.SetFovYDegrees(200.0f);
    Require(Near(fovCamera.GetFovYDegrees(), 120.0f),
            "F4 camera FOV must clamp at 120 degrees");
    fovCamera.SetFovYDegrees(-20.0f);
    Require(Near(fovCamera.GetFovYDegrees(), 20.0f),
            "F4 camera FOV must clamp at 20 degrees");
    fovCamera.SetDebugName(L"F4 saved camera");
    Require(fovCamera.GetDebugName() == L"F4 saved camera" &&
            !fovCamera.WasManuallyAdjusted(),
            "named camera preset must clear the manual-adjustment marker");
    fovCamera.MarkManuallyAdjusted();
    Require(fovCamera.WasManuallyAdjusted(),
            "F4 manual camera edit must be visible to the debug title");

    Camera moveCamera;
    moveCamera.SetLookAt({ 0.0f, 18.0f, 130.0f }, { 0.0f, 28.0f, -20.0f });
    const DirectX::XMFLOAT3 positionBefore = moveCamera.GetPosition();
    const DirectX::XMFLOAT3 targetBefore = moveCamera.GetTarget();
    const float distanceBefore = moveCamera.GetDistance();
    moveCamera.TranslateRigLocal(2.0f, -1.0f);
    const DirectX::XMFLOAT3 positionAfter = moveCamera.GetPosition();
    const DirectX::XMFLOAT3 targetAfter = moveCamera.GetTarget();
    Require(Near(positionAfter.x - positionBefore.x,
                 targetAfter.x - targetBefore.x) &&
            Near(positionAfter.y - positionBefore.y,
                 targetAfter.y - targetBefore.y) &&
            Near(positionAfter.z - positionBefore.z,
                 targetAfter.z - targetBefore.z) &&
            Near(moveCamera.GetDistance(), distanceBefore),
            "WASD rig translation must move eye and target equally without changing orbit distance");
    const DirectX::XMFLOAT3 finiteBefore = moveCamera.GetPosition();
    moveCamera.TranslateRigLocal(NAN, INFINITY);
    const DirectX::XMFLOAT3 finiteAfter = moveCamera.GetPosition();
    Require(Near(finiteBefore.x, finiteAfter.x) &&
            Near(finiteBefore.y, finiteAfter.y) &&
            Near(finiteBefore.z, finiteAfter.z),
            "invalid movement input must leave the camera unchanged");

    std::cout << "[CAMERA][PRESETS] F5_F6_BELOW=1 F7_INSIDE=1 F8_ABOVE=1 BUILDING_CLEAR=1 PASS\n";
    std::cout << "[CAMERA][ZOOM] NORMAL_RATIO=1.25 FAST_RATIO=2.00 RANGE_M=1.5-100000 PASS\n";
    std::cout << "[CAMERA][F4-UI] FOV_DEG=20-120 DEBUG_NAME=1 MANUAL_MARK=1 PASS\n";
    std::cout << "[CAMERA][UNIFIED-SCENE] PRESETS=4 DIGITS=10 WASD_RIG_TRANSLATION=1 PASS\n";
    return 0;
}
