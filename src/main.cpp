// 일반 앱 진입점. 자동 검증/비교 CLI는 로컬 tests/TestMain.cpp에서만 제공한다.
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <objbase.h>
#include <algorithm>
#include "Camera.h"
#include "Renderer.h"
#include "Stage13CameraPresets.h"
#include "Window.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Window window(instance, 1280, 720, L"VolumetricCloud - High", true, true);
    if (!window.GetHandle()) { CoUninitialize(); return 1; }
    Camera camera;
    camera.SetAspect(static_cast<float>(window.GetWidth()) / window.GetHeight());
    camera.SetClipPlanes(stage13camera::kNearPlaneMeters, stage13camera::kFarPlaneMeters);
    camera.SetFovYDegrees(stage13camera::kFovYDegrees);
    const auto preset = stage13camera::Get(Stage13CameraPresetId::HeroDepth);
    camera.SetLookAt(preset.position, preset.target);
    Renderer renderer;
    if (!renderer.Init(window.GetHandle(), window.GetWidth(), window.GetHeight(), true, true) ||
        !renderer.ApplyStage15Defaults())
    {
        CoUninitialize();
        return 1;
    }
    renderer.LoadUserPresetDefaults();
    window.SetCamera(&camera);
    window.SetRenderer(&renderer);
    window.ApplyInitialPortfolioCamera();
    LARGE_INTEGER frequency{}, previous{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&previous);
    float timeSeconds = 0.0f;
    while (window.ProcessMessages())
    {
        LARGE_INTEGER current{};
        QueryPerformanceCounter(&current);
        const float deltaSeconds = static_cast<float>(
            static_cast<double>(current.QuadPart - previous.QuadPart) / frequency.QuadPart);
        previous = current;
        window.UpdateCameraMovement(deltaSeconds);
        timeSeconds += std::clamp(deltaSeconds, 0.0f, 0.25f);
        renderer.Render(camera, timeSeconds);
    }
    CoUninitialize();
    return 0;
}
