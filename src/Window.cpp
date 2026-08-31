#include "Window.h"
#include "Camera.h"
#include "Renderer.h"
#include "Stage13CameraPresets.h"
#include "Stage13SceneMath.h"

#include <windowsx.h> // GET_X_LPARAM / GET_Y_LPARAM

#include <algorithm>
#include <cmath>
#include <cwchar>

static const wchar_t* kClassName = L"VolumetricCloudWindowClass";
static constexpr int kInteractiveClientWidth = 1920;
static constexpr int kInteractiveClientHeight = 1080;

Window::Window(HINSTANCE hInstance, int width, int height, const wchar_t* title,
               bool showWindow, bool useInteractiveStartupPlacement)
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

    // PMv2에서는 생성 시점의 DPI를 명시해야 client가 정확한 물리 픽셀 크기가 된다.
    // CreateWindow 후에도 실제 client를 다시 재서 모니터 선택에 따른 DPI 차이를 보정한다.
    RECT rect = { 0, 0, width, height };
    DWORD style = WS_OVERLAPPEDWINDOW;
    const DWORD exStyle = 0;
    m_dpi = std::max<UINT>(GetDpiForSystem(), 96);
    if (!AdjustWindowRectExForDpi(&rect, style, FALSE, exStyle, m_dpi))
        AdjustWindowRectEx(&rect, style, FALSE, exStyle);

    m_hwnd = CreateWindowEx(
        exStyle, kClassName, title, style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, hInstance, this); // 마지막 인자로 this 전달

    if (m_hwnd)
    {
        m_dpi = std::max<UINT>(GetDpiForWindow(m_hwnd), 96);
        int physicalWidth = 0;
        int physicalHeight = 0;
        if (useInteractiveStartupPlacement)
        {
            ApplyInteractiveStartupPlacement();
        }
        else if (!QueryPhysicalClientExtent(physicalWidth, physicalHeight) ||
                 physicalWidth != width || physicalHeight != height)
        {
            ResizeClientArea(width, height, m_dpi);
        }
        UpdatePhysicalClientExtent(false);
    }

    if (showWindow && m_hwnd)
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

bool Window::QueryPhysicalClientExtent(int& width, int& height) const
{
    width = 0;
    height = 0;
    if (!m_hwnd)
        return false;

    RECT clientRect = {};
    if (!GetClientRect(m_hwnd, &clientRect))
        return false;
    width = std::max(0L, clientRect.right - clientRect.left);
    height = std::max(0L, clientRect.bottom - clientRect.top);
    return true;
}

bool Window::UpdatePhysicalClientExtent(bool notifyRenderer)
{
    int width = 0;
    int height = 0;
    if (!QueryPhysicalClientExtent(width, height))
        return false;
    if (width == m_width && height == m_height)
        return false;

    m_width = width;
    m_height = height;
    if (notifyRenderer && m_renderer && width > 0 && height > 0)
        m_renderer->Resize(width, height);
    return true;
}

bool Window::ResizeClientArea(int width, int height, UINT dpi)
{
    if (!m_hwnd || width <= 0 || height <= 0)
        return false;

    const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(m_hwnd, GWL_STYLE));
    const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE));
    RECT outerRect = { 0, 0, width, height };
    if (!AdjustWindowRectExForDpi(
            &outerRect, style, FALSE, exStyle, std::max<UINT>(dpi, 96)))
    {
        return false;
    }

    return SetWindowPos(
               m_hwnd, nullptr, 0, 0,
               outerRect.right - outerRect.left,
               outerRect.bottom - outerRect.top,
               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE |
                   SWP_NOOWNERZORDER | SWP_FRAMECHANGED) != FALSE;
}

bool Window::ApplyInteractiveStartupPlacement()
{
    if (!m_hwnd)
        return false;

    // 일반 제작 실행은 primary 작업영역 중앙의 1920x1080 physical client를
    // 기준으로 한다. 작업 표시줄을 제외한 영역에 바깥 창까지 들어가지 않을 때만
    // non-client frame을 뺀 뒤 가장 큰 정수 16:9 client로 축소한다.
    const POINT primaryPoint = { 0, 0 };
    const HMONITOR monitor = MonitorFromPoint(
        primaryPoint, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO monitorInfo = { sizeof(MONITORINFO) };
    if (!monitor || !GetMonitorInfoW(monitor, &monitorInfo))
        return false;

    const DWORD style = static_cast<DWORD>(
        GetWindowLongPtrW(m_hwnd, GWL_STYLE));
    const DWORD exStyle = static_cast<DWORD>(
        GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE));
    const UINT dpi = std::max<UINT>(GetDpiForWindow(m_hwnd), 96);
    const auto outerSizeForClient = [&](int clientWidth, int clientHeight,
                                        int& outerWidth, int& outerHeight)
    {
        RECT outerRect = { 0, 0, clientWidth, clientHeight };
        if (!AdjustWindowRectExForDpi(
                &outerRect, style, FALSE, exStyle, dpi) &&
            !AdjustWindowRectEx(&outerRect, style, FALSE, exStyle))
        {
            return false;
        }
        outerWidth = outerRect.right - outerRect.left;
        outerHeight = outerRect.bottom - outerRect.top;
        return outerWidth > 0 && outerHeight > 0;
    };

    const int workWidth =
        monitorInfo.rcWork.right - monitorInfo.rcWork.left;
    const int workHeight =
        monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;
    int clientWidth = kInteractiveClientWidth;
    int clientHeight = kInteractiveClientHeight;
    int outerWidth = 0;
    int outerHeight = 0;
    if (!outerSizeForClient(
            clientWidth, clientHeight, outerWidth, outerHeight))
        return false;

    if (outerWidth > workWidth || outerHeight > workHeight)
    {
        const int nonClientWidth = outerWidth - clientWidth;
        const int nonClientHeight = outerHeight - clientHeight;
        const int availableClientWidth = workWidth - nonClientWidth;
        const int availableClientHeight = workHeight - nonClientHeight;
        const int aspectUnit = std::min(
            availableClientWidth / 16, availableClientHeight / 9);
        if (aspectUnit <= 0)
            return false;
        clientWidth = aspectUnit * 16;
        clientHeight = aspectUnit * 9;
        if (!outerSizeForClient(
                clientWidth, clientHeight, outerWidth, outerHeight) ||
            outerWidth > workWidth || outerHeight > workHeight)
        {
            return false;
        }
    }

    const int x = monitorInfo.rcWork.left + (workWidth - outerWidth) / 2;
    const int y = monitorInfo.rcWork.top + (workHeight - outerHeight) / 2;
    if (!SetWindowPos(
            m_hwnd, nullptr, x, y, outerWidth, outerHeight,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER |
                SWP_FRAMECHANGED))
    {
        return false;
    }

    int actualClientWidth = 0;
    int actualClientHeight = 0;
    return QueryPhysicalClientExtent(actualClientWidth, actualClientHeight) &&
        actualClientWidth == clientWidth &&
        actualClientHeight == clientHeight;
}

bool Window::IsPerMonitorV2DpiAware() const
{
    if (!m_hwnd)
        return false;
    const DPI_AWARENESS_CONTEXT context = GetWindowDpiAwarenessContext(m_hwnd);
    return AreDpiAwarenessContextsEqual(
               context, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != FALSE;
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

void Window::ApplyInitialPortfolioCamera()
{
    if (!m_camera)
        return;
    ApplyCameraPreset(
        Stage13CameraPresetId::HeroDepth, L"포트폴리오 Hero/Depth(F5)");
    UpdateDebugTitle();
}

void Window::ApplyCameraPreset(Stage13CameraPresetId id,
                               const wchar_t* displayName)
{
    if (!m_camera)
        return;
    const Stage13CameraPreset& preset = stage13camera::Get(id);
    m_camera->SetClipPlanes(
        stage13camera::kNearPlaneMeters, stage13camera::kFarPlaneMeters);
    m_camera->SetFovYDegrees(60.0f);
    m_camera->SetLookAt(preset.position, preset.target);
    SetCameraPresetName(displayName);
}

void Window::UpdateCameraMovement(float deltaSeconds)
{
    if (!m_camera || !m_renderer || m_renderer->DeveloperUiWantsKeyboard())
        return;
    const float moveDistance = stage13scene::MovementDistance(
        deltaSeconds, (GetKeyState(VK_SHIFT) & 0x8000) != 0,
        m_renderer->CameraMoveSpeed());
    float forward = ((GetKeyState('W') & 0x8000) ? 1.0f : 0.0f) -
                    ((GetKeyState('S') & 0x8000) ? 1.0f : 0.0f);
    float right = ((GetKeyState('D') & 0x8000) ? 1.0f : 0.0f) -
                  ((GetKeyState('A') & 0x8000) ? 1.0f : 0.0f);
    const float inputLength = std::sqrt(forward * forward + right * right);
    if (inputLength <= 1e-5f || moveDistance <= 0.0f)
        return;
    forward /= inputLength;
    right /= inputLength;
    m_camera->MoveLocal(
        forward * moveDistance, right * moveDistance);
    MarkCameraManuallyAdjusted();
}

void Window::SetCameraPresetName(const wchar_t* displayName)
{
    if (m_camera)
        m_camera->SetDebugName(displayName);
}

void Window::MarkCameraManuallyAdjusted()
{
    if (!m_camera || m_camera->WasManuallyAdjusted())
        return;
    m_camera->MarkManuallyAdjusted();
    UpdateDebugTitle();
}

void Window::UpdateDebugTitle()
{
    if (!m_renderer || !m_hwnd)
        return;
    static const wchar_t* weatherPresetNames[] = {
        L"Uniform Legacy", L"Periodic Perlin", L"Channel Debug"
    };
    static const wchar_t* sunPresetNames[] = {
        L"Noon Sun", L"Low East Sun", L"Low West Sun", L"Custom Sun"
    };
    static const wchar_t* phasePresetNames[] = {
        L"Phase Off", L"Balanced Phase", L"Silver Lining Phase",
        L"Backscatter Check", L"Custom Phase"
    };
    static const wchar_t* environmentPresetNames[] = {
        L"Environment Off", L"Balanced Ambient", L"Strong Fill",
        L"Ground Check", L"Portfolio Ambient", L"Custom Environment"
    };

    const int weatherPresetIndex = static_cast<int>(m_renderer->WeatherPreset());
    const int sunPresetIndex = static_cast<int>(m_renderer->SunPreset());
    const int phasePresetIndex = static_cast<int>(m_renderer->PhasePreset());
    const int environmentPresetIndex =
        static_cast<int>(m_renderer->EnvironmentPreset());
    const wchar_t* noiseSourceName = L"Texture3D";
    const wchar_t* atmosphereModeName = L"Physical Atmosphere LUT";
    wchar_t title[512] = {};
    swprintf_s(title, L"VolumetricCloud - Stage 15 | %ls | %ls | Unified 50km Portfolio Scene | %ls | %ls | %ls | %ls | %ls | %ls | WASD %.0f m/s Shift 4x",
               stage13scene::DebugModeName(m_renderer->DebugMode()),
               atmosphereModeName,
               noiseSourceName,
               weatherPresetNames[(weatherPresetIndex >= 0 && weatherPresetIndex <= 2) ? weatherPresetIndex : 1],
               sunPresetNames[(sunPresetIndex >= 0 && sunPresetIndex <= 3) ? sunPresetIndex : 3],
               phasePresetNames[(phasePresetIndex >= 0 && phasePresetIndex <= 4) ? phasePresetIndex : 0],
               environmentPresetNames[(environmentPresetIndex >= 0 && environmentPresetIndex <= 5) ? environmentPresetIndex : 1],
               m_camera
                   ? (m_camera->GetDebugName() +
                      (m_camera->WasManuallyAdjusted() ? L" · 수동 조정" : L"")).c_str()
                    : L"카메라 없음",
               m_renderer->CameraMoveSpeed());
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
        self->m_hwnd = hwnd;
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

bool Window::HandleGlobalDebugShortcut(WPARAM virtualKey)
{
    if (!m_renderer)
        return false;
    const int debugDigit = stage13scene::DebugDigitFromVirtualKey(
        static_cast<std::uint32_t>(virtualKey));
    if (debugDigit < 0)
        return false;
    m_renderer->SetDebugMode(stage13scene::DebugModeFromDigit(debugDigit));
    UpdateDebugTitle();
    return true;
}

LRESULT Window::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    const bool sceneKeyboardBlocked = m_renderer &&
        m_renderer->DeveloperUiWantsKeyboard();
    // F1~F4는 독립 개발 UI 창이다. F1은 NoiseLab 메시지 경로가 처리하고,
    // F2~F4는 기존 Weather 프리셋 키 대신 Weather/Lighting/Camera 창을 토글한다.
    if (msg == WM_KEYDOWN && m_renderer && !sceneKeyboardBlocked &&
        wParam >= VK_F2 && wParam <= VK_F4)
    {
        if ((lParam & (1ll << 30)) == 0)
        {
            m_renderer->ToggleDeveloperUiPanel(static_cast<DeveloperUiPanel>(
                static_cast<std::size_t>(wParam - VK_F1)));
        }
        return 0;
    }

    // F5~F8은 단일 씬의 네 고정 카메라다.
    if (msg == WM_KEYDOWN &&
        !sceneKeyboardBlocked &&
        wParam >= VK_F5 && wParam <= VK_F8)
    {
        if (m_camera && wParam == VK_F5)
            ApplyCameraPreset(Stage13CameraPresetId::HeroDepth,
                              L"포트폴리오 Hero/Depth(F5)");
        else if (m_camera && wParam == VK_F6)
            ApplyCameraPreset(Stage13CameraPresetId::GroundHorizon,
                              L"지상 수평선(F6)");
        else if (m_camera && wParam == VK_F7)
            ApplyCameraPreset(Stage13CameraPresetId::InsideLayer,
                              L"구름 내부(F7)");
        else if (m_camera && wParam == VK_F8)
            ApplyCameraPreset(Stage13CameraPresetId::AboveLayer,
                              L"구름 위 하향(F8)");
        else
            return 0;

        UpdateDebugTitle();
        return 0;
    }

    if (msg == WM_KEYDOWN && !sceneKeyboardBlocked &&
        !stage13scene::IsCameraMovementKey(
            static_cast<std::uint32_t>(wParam)) &&
        HandleGlobalDebugShortcut(wParam))
        return 0;

    if (m_renderer && m_renderer->HandleWindowMessage(hwnd, msg, wParam, lParam))
        return 0;

    switch (msg)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_DPICHANGED:
    {
        m_dpi = HIWORD(wParam);
        const RECT* suggestedRect = reinterpret_cast<const RECT*>(lParam);
        if (!suggestedRect)
            return 0;

        SetWindowPos(
            hwnd, nullptr, suggestedRect->left, suggestedRect->top,
            suggestedRect->right - suggestedRect->left,
            suggestedRect->bottom - suggestedRect->top,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        UpdatePhysicalClientExtent(true);
        return 0;
    }

    case WM_SIZE:
        // lParam은 API 계약상 client 크기지만 PMv2의 단일 기준은 GetClientRect로
        // 직접 측정한 물리 픽셀이다. 동일 크기 알림은 Renderer까지 전달하지 않는다.
        UpdatePhysicalClientExtent(true);
        return 0;

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
            if (dx != 0 || dy != 0)
                MarkCameraManuallyAdjusted();
            m_lastMouseX = x;
            m_lastMouseY = y;
        }
        return 0;

    case WM_MOUSEWHEEL:
        if (m_camera)
        {
            const float distance = stage13scene::WheelMovementDistance(
                static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)),
                (GetKeyState(VK_SHIFT) & 0x8000) != 0,
                m_renderer ? m_renderer->CameraMoveSpeed() :
                    stage13scene::kMoveSpeedMetersPerSecond);
            if (std::abs(distance) > 1e-5f)
            {
                m_camera->MoveLocal(distance, 0.0f);
                MarkCameraManuallyAdjusted();
            }
        }
        return 0;

    case WM_KEYDOWN:
        // WASD는 polling 기반 카메라 이동 전용이다.
        if (stage13scene::IsCameraMovementKey(
                static_cast<std::uint32_t>(wParam)))
            return 0;
        break;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}
