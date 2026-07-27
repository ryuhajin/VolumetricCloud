// ============================================================================
//  main.cpp  —  진입점 (WinMain)
// ----------------------------------------------------------------------------
//  창(Window) · 카메라(Camera) · 렌더러(Renderer)를 생성·연결하고,
//  메인 루프에서 매 프레임 박스 볼륨을 레이마칭으로 그린다.
// ============================================================================
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cwchar>

#include "Window.h"
#include "Camera.h"
#include "Renderer.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int)
{
    const int kWidth  = 1280;
    const int kHeight = 720;

    // ---- 객체 생성 ----
    Window   window(hInstance, kWidth, kHeight, L"VolumetricCloud - F1: Cloud Debug");
    Camera   camera;
    Renderer renderer;
    const bool buildDefaultCache = wcsstr(GetCommandLineW(), L"--build-default-cache") != nullptr;
    const bool cacheSmokeTest = wcsstr(GetCommandLineW(), L"--cache-smoke-test") != nullptr;
    const bool runCodeTests = wcsstr(GetCommandLineW(), L"--run-code-tests") != nullptr;

    camera.SetAspect(static_cast<float>(kWidth) / kHeight);

    if (!renderer.Init(window.GetHandle(), kWidth, kHeight, buildDefaultCache))
        return -1; // 초기화 실패 (오류 메시지는 Renderer가 표시)

    // 입력/리사이즈 연결
    window.SetCamera(&camera);
    window.SetRenderer(&renderer);

    if (buildDefaultCache)
    {
        ShowWindow(window.GetHandle(), SW_HIDE);
        renderer.Render(camera, 0.0f);
        return renderer.SaveDefaultNoiseCache() ? 0 : 2;
    }
    if (cacheSmokeTest)
    {
        ShowWindow(window.GetHandle(), SW_HIDE);
        renderer.Render(camera, 0.0f);
        return renderer.RuntimeCompileCount() == 0 && renderer.NoiseDispatchCount() == 0 ? 0 : 3;
    }
    if (runCodeTests)
    {
        ShowWindow(window.GetHandle(), SW_HIDE);
        renderer.Render(camera, 0.0f);
        return renderer.RunCodeTests() ? 0 : 4;
    }

    // ---- 고해상도 타이머 준비 ----
    LARGE_INTEGER freq, start;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);

    // ---- 메인 루프 ----
    while (window.ProcessMessages())
    {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        float elapsed = static_cast<float>(now.QuadPart - start.QuadPart) / freq.QuadPart;

        // 창이 리사이즈되면 종횡비를 갱신
        if (window.GetHeight() > 0)
            camera.SetAspect(static_cast<float>(window.GetWidth()) / window.GetHeight());

        renderer.Render(camera, elapsed);
    }

    return 0;
}
