// ============================================================================
//  main.cpp  —  진입점 (WinMain)
// ----------------------------------------------------------------------------
//  창(Window) · 카메라(Camera) · 렌더러(Renderer)를 생성·연결하고,
//  메인 루프에서 진단 장면과 단계 1 상수 밀도 AABB 패스를 그린다.
// ============================================================================
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "Window.h"
#include "Camera.h"
#include "Renderer.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR commandLine, int)
{
    const int kWidth  = 1280;
    const int kHeight = 720;

    const bool smokeTest = commandLine &&
        wcsstr(commandLine, L"--foundation-smoke-test") != nullptr;
    const bool stage1SmokeTest = commandLine &&
        wcsstr(commandLine, L"--stage1-smoke-test") != nullptr;

    // ---- 객체 생성 ----
    Window   window(hInstance, kWidth, kHeight,
                    L"VolumetricCloud - Stage 1 | 0 합성 | Q 기본 볼륨 | 외부 기본(F5)",
                    !smokeTest && !stage1SmokeTest);
    Camera   camera;
    Renderer renderer;

    camera.SetAspect(static_cast<float>(kWidth) / kHeight);

    if (!renderer.Init(window.GetHandle(), kWidth, kHeight))
        return -1; // 초기화 실패 (오류 메시지는 Renderer가 표시)

    // 입력/리사이즈 연결
    window.SetCamera(&camera);
    window.SetRenderer(&renderer);

    if (smokeTest)
    {
        for (int frame = 0; frame < 3; ++frame)
            renderer.Render(camera, static_cast<float>(frame) / 60.0f);
        return renderer.HasDebugLayerErrors() ? 2 : 0;
    }

    if (stage1SmokeTest)
    {
        // 단계 1의 모든 분기 셰이더가 실제 D3D11 draw에서 오류 없이 실행되는지 검사한다.
        for (int mode = 0; mode <= 9; ++mode)
        {
            renderer.SetDebugMode(static_cast<CloudDebugMode>(mode));
            renderer.Render(camera, static_cast<float>(mode) / 60.0f);
        }
        return renderer.HasDebugLayerErrors() ? 2 : 0;
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
