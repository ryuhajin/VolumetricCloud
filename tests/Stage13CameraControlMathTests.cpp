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
    Require(Near(stage13scene::WheelMovementDistance(
                     120.0f, false, 100.0f), 25.0f) &&
            Near(stage13scene::WheelMovementDistance(
                     -120.0f, false, 100.0f), -25.0f) &&
            Near(stage13scene::WheelMovementDistance(
                     120.0f, true, 100.0f), 100.0f) &&
            Near(stage13scene::WheelMovementDistance(
                     NAN, true, 100.0f), 0.0f),
            "wheel must move 0.25 seconds per notch and Shift must remain 4x");
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
        const DirectX::XMFLOAT3 actualForward = camera.GetForward();
        const DirectX::XMFLOAT3 expectedForward = Direction(preset);
        Require(Near(actual.x, preset.position.x, 0.02f) &&
                Near(actual.y, preset.position.y, 0.02f) &&
                Near(actual.z, preset.position.z, 0.02f),
                "SetLookAt must reconstruct the requested camera position");
        Require(Near(actualForward.x, expectedForward.x, 1e-4f) &&
                Near(actualForward.y, expectedForward.y, 1e-4f) &&
                Near(actualForward.z, expectedForward.z, 1e-4f),
                "SetLookAt must preserve each preset center direction");
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

    Camera lookCamera;
    lookCamera.SetLookAt(f5.position, f5.target);
    const DirectX::XMFLOAT3 lookPosition = lookCamera.GetPosition();
    const float lookDistance = lookCamera.GetDistance();
    const DirectX::XMFLOAT3 forwardBeforeRight = lookCamera.GetForward();
    lookCamera.Rotate(40.0f, 0.0f);
    const DirectX::XMFLOAT3 forwardAfterRight = lookCamera.GetForward();
    Require(Near(lookCamera.GetPosition().x, lookPosition.x) &&
            Near(lookCamera.GetPosition().y, lookPosition.y) &&
            Near(lookCamera.GetPosition().z, lookPosition.z) &&
            Near(lookCamera.GetDistance(), lookDistance),
            "free look must not orbit or change reference distance");
    Require(forwardAfterRight.x < forwardBeforeRight.x,
            "dragging right from the F5 -Z view must turn toward screen right");
    const float forwardYBeforeUp = forwardAfterRight.y;
    lookCamera.Rotate(0.0f, -40.0f);
    Require(lookCamera.GetForward().y > forwardYBeforeUp,
            "dragging upward must raise the FPS view direction");
    lookCamera.Rotate(0.0f, -100000.0f);
    Require(lookCamera.GetPitchDegrees() < 90.0f &&
            lookCamera.GetPitchDegrees() > 89.0f &&
            Near(lookCamera.GetPosition().y, f5.position.y),
            "zenith free look must clamp pitch without moving F5 underground");
    Camera f6ZenithCamera;
    f6ZenithCamera.SetLookAt(f6.position, f6.target);
    f6ZenithCamera.Rotate(0.0f, -100000.0f);
    Require(Near(f6ZenithCamera.GetPosition().y, f6.position.y) &&
            f6ZenithCamera.GetForward().y > 0.999f,
            "zenith free look must keep the F6 ground-horizon altitude");
    for (int index = 0; index < 10000; ++index)
        f6ZenithCamera.Rotate(1000.0f, 0.0f);
    Require(Finite(f6ZenithCamera.GetForward()) &&
            FiniteMatrix(f6ZenithCamera.GetViewProj()),
            "wrapped FPS yaw must remain finite after repeated rotation");
    lookCamera.Rotate(NAN, INFINITY);
    Require(Finite(lookCamera.GetForward()),
            "invalid look input must preserve a finite direction");

    Camera orbitCompatibility;
    orbitCompatibility.SetOrbit(0.55f, 0.30f, 12.0f,
                                { 0.0f, -0.2f, 0.0f });
    const DirectX::XMFLOAT3 orbitPosition = orbitCompatibility.GetPosition();
    Require(Near(orbitPosition.x, 12.0f * std::cos(0.30f) * std::sin(0.55f)) &&
            Near(orbitPosition.y, -0.2f + 12.0f * std::sin(0.30f)) &&
            Near(orbitPosition.z, 12.0f * std::cos(0.30f) * std::cos(0.55f)),
            "legacy SetOrbit must preserve its original starting pose");

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
    moveCamera.MoveLocal(2.0f, -1.0f);
    const DirectX::XMFLOAT3 positionAfter = moveCamera.GetPosition();
    const DirectX::XMFLOAT3 targetAfter = moveCamera.GetTarget();
    Require(Near(positionAfter.x - positionBefore.x,
                 targetAfter.x - targetBefore.x) &&
            Near(positionAfter.y - positionBefore.y,
                 targetAfter.y - targetBefore.y) &&
            Near(positionAfter.z - positionBefore.z,
                 targetAfter.z - targetBefore.z) &&
            Near(moveCamera.GetDistance(), distanceBefore),
            "FPS translation must move position and reference target equally");
    const DirectX::XMFLOAT3 wheelStart = moveCamera.GetPosition();
    const DirectX::XMFLOAT3 wheelForward = moveCamera.GetForward();
    const float wheelDistance = stage13scene::WheelMovementDistance(
        120.0f, false, 100.0f);
    moveCamera.MoveLocal(wheelDistance, 0.0f);
    const DirectX::XMFLOAT3 wheelEnd = moveCamera.GetPosition();
    Require(Near(wheelEnd.x - wheelStart.x,
                 wheelForward.x * wheelDistance) &&
            Near(wheelEnd.y - wheelStart.y,
                 wheelForward.y * wheelDistance) &&
            Near(wheelEnd.z - wheelStart.z,
                 wheelForward.z * wheelDistance) &&
            Near(moveCamera.GetDistance(), distanceBefore),
            "wheel travel must move along full pitched forward without zooming");
    moveCamera.MoveLocal(-wheelDistance, 0.0f);
    const DirectX::XMFLOAT3 wheelRoundTrip = moveCamera.GetPosition();
    Require(Near(wheelRoundTrip.x, wheelStart.x) &&
            Near(wheelRoundTrip.y, wheelStart.y) &&
            Near(wheelRoundTrip.z, wheelStart.z),
            "opposite wheel deltas must restore the FPS position");
    const DirectX::XMFLOAT3 finiteBefore = moveCamera.GetPosition();
    moveCamera.MoveLocal(NAN, INFINITY);
    const DirectX::XMFLOAT3 finiteAfter = moveCamera.GetPosition();
    Require(Near(finiteBefore.x, finiteAfter.x) &&
            Near(finiteBefore.y, finiteAfter.y) &&
            Near(finiteBefore.z, finiteAfter.z),
            "invalid movement input must leave the camera unchanged");

    std::cout << "[CAMERA][PRESETS] F5_F6_BELOW=1 F7_INSIDE=1 F8_ABOVE=1 BUILDING_CLEAR=1 PASS\n";
    std::cout << "[CAMERA][FPS] FREE_LOOK_POSITION_FIXED=1 WHEEL_SECONDS=0.25 SHIFT=4 PASS\n";
    std::cout << "[CAMERA][F4-UI] FOV_DEG=20-120 DEBUG_NAME=1 MANUAL_MARK=1 PASS\n";
    std::cout << "[CAMERA][UNIFIED-SCENE] PRESETS=4 DIGITS=10 WASD_FREE_FLY=1 PASS\n";
    return 0;
}
