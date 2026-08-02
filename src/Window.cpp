#include "Window.h"
#include "Camera.h"
#include "Renderer.h"

#include <windowsx.h> // GET_X_LPARAM / GET_Y_LPARAM

#include <cwchar>

static const wchar_t* kClassName = L"VolumetricCloudWindowClass";

Window::Window(HINSTANCE hInstance, int width, int height, const wchar_t* title,
               bool showWindow)
    : m_width(width), m_height(height)
{
    // ---- 윈도우 클래스 등록 ----
    WNDCLASSEX wc = {};
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProcStatic;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    RegisterClassEx(&wc);

    // 클라이언트 영역이 정확히 width x height 가 되도록 창 크기 보정
    RECT rect = { 0, 0, width, height };
    DWORD style = WS_OVERLAPPEDWINDOW;
    AdjustWindowRect(&rect, style, FALSE);

    m_hwnd = CreateWindowEx(
        0, kClassName, title, style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, hInstance, this); // 마지막 인자로 this 전달

    if (showWindow)
    {
        ShowWindow(m_hwnd, SW_SHOW);
        UpdateWindow(m_hwnd);
    }
}

Window::~Window()
{
    if (m_hwnd)
        DestroyWindow(m_hwnd);
}

bool Window::ProcessMessages()
{
    MSG msg = {};
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_QUIT)
            return false;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return true;
}

void Window::UpdateStage1Title()
{
    if (!m_renderer || !m_hwnd)
        return;

    static const wchar_t* debugNames[] = {
        L"0 합성", L"1 월드 레이", L"2 Scene Depth", L"3 월드 위치", L"4 화면 UV",
        L"5 AABB 진입", L"6 제한 이탈", L"7 Step 수", L"8 투과율", L"9 상수 밀도"
    };
    static const wchar_t* presetNames[] = {
        L"Q 기본 볼륨", L"W 얇은 Z", L"E 두꺼운 Z", L"R Fine 0.025m", L"T Coarse 0.5m"
    };

    const int debugIndex = static_cast<int>(m_renderer->DebugMode());
    const int presetIndex = static_cast<int>(m_renderer->ValidationPreset());
    wchar_t title[256] = {};
    swprintf_s(title, L"VolumetricCloud - Stage 1 | %ls | %ls | %ls",
               debugNames[(debugIndex >= 0 && debugIndex <= 9) ? debugIndex : 0],
               presetNames[(presetIndex >= 0 && presetIndex <= 4) ? presetIndex : 0],
               m_cameraPresetName);
    SetWindowTextW(m_hwnd, title);
}

// 정적 콜백: GWLP_USERDATA 에 저장한 인스턴스로 라우팅
LRESULT CALLBACK Window::WndProcStatic(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    Window* self = nullptr;
    if (msg == WM_NCCREATE)
    {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = reinterpret_cast<Window*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else
    {
        self = reinterpret_cast<Window*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (self)
        return self->WndProc(hwnd, msg, wParam, lParam);
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT Window::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_SIZE:
    {
        m_width  = LOWORD(lParam);
        m_height = HIWORD(lParam);
        if (m_renderer && m_width > 0 && m_height > 0)
            m_renderer->Resize(m_width, m_height);
        return 0;
    }

    case WM_LBUTTONDOWN:
        m_dragging   = true;
        m_lastMouseX = GET_X_LPARAM(lParam);
        m_lastMouseY = GET_Y_LPARAM(lParam);
        SetCapture(hwnd); // 창 밖으로 나가도 드래그 유지
        return 0;

    case WM_LBUTTONUP:
        m_dragging = false;
        ReleaseCapture();
        return 0;

    case WM_MOUSEMOVE:
        if (m_dragging && m_camera)
        {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            int dx = x - m_lastMouseX;
            int dy = y - m_lastMouseY;
            m_camera->Rotate(static_cast<float>(dx), static_cast<float>(dy));
            m_lastMouseX = x;
            m_lastMouseY = y;
        }
        return 0;

    case WM_MOUSEWHEEL:
        if (m_camera)
            m_camera->Zoom(static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)));
        return 0;

    case WM_KEYDOWN:
        if (m_renderer && wParam >= '0' && wParam <= '9')
        {
            m_renderer->SetDebugMode(
                static_cast<CloudDebugMode>(static_cast<int>(wParam - '0')));
            UpdateStage1Title();
            return 0;
        }
        if (m_camera && wParam == VK_F5)
        {
            m_camera->SetOrbit(0.55f, 0.30f, 12.0f, { 0.0f, -0.2f, 0.0f });
            m_cameraPresetName = L"외부 기본(F5)";
            UpdateStage1Title();
            return 0;
        }
        if (m_camera && wParam == VK_F6)
        {
            m_camera->SetOrbit(-0.75f, 0.05f, 10.0f, { 0.0f, -0.5f, 0.0f });
            m_cameraPresetName = L"낮은 외부(F6)";
            UpdateStage1Title();
            return 0;
        }
        if (m_camera && wParam == VK_F7)
        {
            m_camera->SetOrbit(0.0f, 0.65f, 14.0f, { 0.0f, -0.5f, 0.0f });
            m_cameraPresetName = L"높은 외부(F7)";
            UpdateStage1Title();
            return 0;
        }
        if (m_camera && wParam == VK_F8)
        {
            // orbit의 눈 위치가 원점이 되도록 target을 -Z로 옮긴다. 따라서 얇은 W
            // 프리셋에서도 카메라는 AABB 내부이고 raw tNear가 음수인 경로를 검증한다.
            m_camera->SetOrbit(0.0f, 0.0f, 1.5f, { 0.0f, 0.0f, -1.5f });
            m_cameraPresetName = L"AABB 내부(F8)";
            UpdateStage1Title();
            return 0;
        }
        if (m_renderer && wParam == 'Q')
        {
            m_renderer->ApplyStage1ValidationPreset(Stage1ValidationPreset::DefaultVolume);
            UpdateStage1Title();
            return 0;
        }
        if (m_renderer && wParam == 'W')
        {
            m_renderer->ApplyStage1ValidationPreset(Stage1ValidationPreset::ThinVolume);
            UpdateStage1Title();
            return 0;
        }
        if (m_renderer && wParam == 'E')
        {
            m_renderer->ApplyStage1ValidationPreset(Stage1ValidationPreset::ThickVolume);
            UpdateStage1Title();
            return 0;
        }
        if (m_renderer && wParam == 'R')
        {
            m_renderer->ApplyStage1ValidationPreset(Stage1ValidationPreset::FineStep);
            UpdateStage1Title();
            return 0;
        }
        if (m_renderer && wParam == 'T')
        {
            m_renderer->ApplyStage1ValidationPreset(Stage1ValidationPreset::CoarseStep);
            UpdateStage1Title();
            return 0;
        }
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}
