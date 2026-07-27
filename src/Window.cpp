#include "Window.h"
#include "Camera.h"
#include "Renderer.h"

#include <windowsx.h> // GET_X_LPARAM / GET_Y_LPARAM

static const wchar_t* kClassName = L"VolumetricCloudWindowClass";

Window::Window(HINSTANCE hInstance, int width, int height, const wchar_t* title)
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

    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);
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
    const bool uiHandled = m_renderer && m_renderer->HandleWindowMessage(hwnd, msg, wParam, lParam);

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
        if (uiHandled || (m_renderer && m_renderer->WantsMouseCapture())) return 0;
        m_dragging   = true;
        m_lastMouseX = GET_X_LPARAM(lParam);
        m_lastMouseY = GET_Y_LPARAM(lParam);
        SetCapture(hwnd); // 창 밖으로 나가도 드래그 유지
        return 0;

    case WM_LBUTTONUP:
        if (uiHandled || (m_renderer && m_renderer->WantsMouseCapture())) return 0;
        m_dragging = false;
        ReleaseCapture();
        return 0;

    case WM_MOUSEMOVE:
        if (uiHandled || (m_renderer && m_renderer->WantsMouseCapture())) return 0;
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
        if (uiHandled || (m_renderer && m_renderer->WantsMouseCapture())) return 0;
        if (m_camera)
            m_camera->Zoom(static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)));
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_F1 && m_renderer)
        {
            m_renderer->ToggleDebugUI();
            return 0;
        }
        if (wParam == VK_F2 && m_renderer)
        {
            m_renderer->ToggleTelemetry();
            return 0;
        }
        if (uiHandled || (m_renderer && m_renderer->WantsKeyboardCapture())) return 0;
        break;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}
