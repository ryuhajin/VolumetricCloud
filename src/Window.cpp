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

void Window::UpdateDebugTitle()
{
    if (!m_renderer || !m_hwnd)
        return;

    static const wchar_t* debugNames[] = {
        L"0 합성", L"1 월드 레이", L"2 Scene Depth", L"3 월드 위치", L"4 화면 UV",
        L"5 AABB 진입", L"6 제한 이탈", L"7 Step 수", L"8 투과율", L"9 샘플 밀도",
        L"Z 원본 Noise", L"X Threshold", L"C 최종 밀도", L"V Noise UVW",
        L"B 높이 비율", L"M 높이 Profile", L"J Base 밀도", L"L Detail Noise",
        L"P Erosion", L"U Detail Sample", L"I Weather Coverage", L"O Cloud Type",
        L"Shift+I Weather Threshold", L"Shift+O Typed Height"
    };
    static const wchar_t* presetNames[] = {
        L"Q 기본 볼륨", L"Y 넓은 볼륨", L"W 얇은 Z", L"E 두꺼운 Z",
        L"R Fine 0.025m", L"T Coarse 0.5m"
    };
    static const wchar_t* noisePresetNames[] = {
        L"N 기본 Noise", L"A Sparse", L"S Dense", L"D 큰 덩어리",
        L"F 작은 덩어리", L"G 바람 정지", L"H 빠른 바람", L"K Noise Offset",
        L"UI Custom"
    };
    static const wchar_t* detailPresetNames[] = {
        L"F9 Detail Off", L"F10 기본 Detail", L"F11 Fine Detail",
        L"F12 Strong Erosion", L"UI Custom Detail"
    };
    static const wchar_t* weatherPresetNames[] = {
        L"F2 Uniform Weather", L"F3 Periodic Perlin", L"F4 Channel Debug"
    };

    const int debugIndex = static_cast<int>(m_renderer->DebugMode());
    const int presetIndex = static_cast<int>(m_renderer->ValidationPreset());
    const int noisePresetIndex = static_cast<int>(m_renderer->NoisePreset());
    const int detailPresetIndex = static_cast<int>(m_renderer->DetailPreset());
    const int weatherPresetIndex = static_cast<int>(m_renderer->WeatherPreset());
    wchar_t title[512] = {};
    swprintf_s(title, L"VolumetricCloud - Stage 5 | %ls | %ls | %ls | %ls | %ls | %ls",
               debugNames[(debugIndex >= 0 && debugIndex <= 23) ? debugIndex : 0],
               presetNames[(presetIndex >= 0 && presetIndex <= 5) ? presetIndex : 0],
               noisePresetNames[(noisePresetIndex >= 0 && noisePresetIndex <= 8) ? noisePresetIndex : 0],
               detailPresetNames[(detailPresetIndex >= 0 && detailPresetIndex <= 4) ? detailPresetIndex : 1],
               weatherPresetNames[(weatherPresetIndex >= 0 && weatherPresetIndex <= 2) ? weatherPresetIndex : 2],
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
    // Weather 비교 키는 Noise Lab이 기본으로 열린 상태에서도 즉시 작동해야 한다.
    // Shift 조합은 같은 중간값의 "입력 지도"와 "적용 결과"를 짝으로 보여 준다.
    if (msg == WM_KEYDOWN && m_renderer && (wParam == 'I' || wParam == 'O'))
    {
        const bool shifted = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (wParam == 'I')
            m_renderer->SetDebugMode(shifted
                ? CloudDebugMode::WeatherThresholdDensity
                : CloudDebugMode::WeatherCoverage);
        else
            m_renderer->SetDebugMode(shifted
                ? CloudDebugMode::TypedHeightProfile
                : CloudDebugMode::CloudType);
        UpdateDebugTitle();
        return 0;
    }

    // Noise Lab은 기본으로 열려 있고 ImGui가 키보드를 캡처할 수 있다. F2~F12는
    // 텍스트 편집에 쓰이지 않는 전역 검증 단축키이므로 UI보다 먼저 처리한다.
    // 특히 F9~F12가 ImGui에 막히면 Detail 프리셋을 다시 선택할 수 없다.
    // Windows는 F10을 메뉴 활성화 키로 취급해 WM_SYSKEYDOWN으로 보낼 수
    // 있으므로 일반 키와 시스템 키 경로를 모두 받는다.
    if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) &&
        wParam >= VK_F2 && wParam <= VK_F12)
    {
        if (m_renderer && wParam == VK_F2)
            m_renderer->ApplyStage5WeatherPreset(Stage5WeatherPreset::UniformLegacy);
        else if (m_renderer && wParam == VK_F3)
            m_renderer->ApplyStage5WeatherPreset(Stage5WeatherPreset::PeriodicPerlin);
        else if (m_renderer && wParam == VK_F4)
            m_renderer->ApplyStage5WeatherPreset(Stage5WeatherPreset::ChannelDebug);
        else if (m_camera && wParam == VK_F5)
        {
            m_camera->SetOrbit(0.55f, 0.30f, 12.0f, { 0.0f, -0.2f, 0.0f });
            m_cameraPresetName = L"외부 기본(F5)";
        }
        else if (m_camera && wParam == VK_F6)
        {
            m_camera->SetOrbit(-0.75f, 0.05f, 10.0f, { 0.0f, -0.5f, 0.0f });
            m_cameraPresetName = L"낮은 외부(F6)";
        }
        else if (m_camera && wParam == VK_F7)
        {
            m_camera->SetOrbit(0.0f, 0.65f, 14.0f, { 0.0f, -0.5f, 0.0f });
            m_cameraPresetName = L"높은 외부(F7)";
        }
        else if (m_camera && wParam == VK_F8)
        {
            // orbit의 눈 위치가 원점이 되도록 target을 -Z로 옮긴다. 따라서 얇은 W
            // 프리셋에서도 카메라는 AABB 내부이고 raw tNear가 음수인 경로를 검증한다.
            m_camera->SetOrbit(0.0f, 0.0f, 1.5f, { 0.0f, 0.0f, -1.5f });
            m_cameraPresetName = L"AABB 내부(F8)";
        }
        else if (m_renderer && wParam == VK_F9)
            m_renderer->ApplyStage4DetailPreset(Stage4DetailPreset::DetailOff);
        else if (m_renderer && wParam == VK_F10)
            m_renderer->ApplyStage4DetailPreset(Stage4DetailPreset::DefaultDetail);
        else if (m_renderer && wParam == VK_F11)
            m_renderer->ApplyStage4DetailPreset(Stage4DetailPreset::FineDetail);
        else if (m_renderer && wParam == VK_F12)
            m_renderer->ApplyStage4DetailPreset(Stage4DetailPreset::StrongErosion);
        else
            return 0;

        UpdateDebugTitle();
        return 0;
    }

    if (m_renderer && m_renderer->HandleWindowMessage(hwnd, msg, wParam, lParam))
        return 0;

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
            UpdateDebugTitle();
            return 0;
        }
        if (m_renderer && wParam == 'Z')
        {
            m_renderer->SetDebugMode(CloudDebugMode::RawNoise);
            UpdateDebugTitle();
            return 0;
        }
        if (m_renderer && wParam == 'X')
        {
            m_renderer->SetDebugMode(CloudDebugMode::ThresholdDensity);
            UpdateDebugTitle();
            return 0;
        }
        if (m_renderer && wParam == 'C')
        {
            m_renderer->SetDebugMode(CloudDebugMode::FinalDensity);
            UpdateDebugTitle();
            return 0;
        }
        if (m_renderer && wParam == 'V')
        {
            m_renderer->SetDebugMode(CloudDebugMode::NoiseUvw);
            UpdateDebugTitle();
            return 0;
        }
        if (m_renderer && wParam == 'B')
        {
            m_renderer->SetDebugMode(CloudDebugMode::HeightFraction);
            UpdateDebugTitle();
            return 0;
        }
        if (m_renderer && wParam == 'M')
        {
            m_renderer->SetDebugMode(CloudDebugMode::HeightProfile);
            UpdateDebugTitle();
            return 0;
        }
        if (m_renderer && wParam == 'J')
        {
            m_renderer->SetDebugMode(CloudDebugMode::BaseDensity);
            UpdateDebugTitle();
            return 0;
        }
        if (m_renderer && wParam == 'L')
        {
            m_renderer->SetDebugMode(CloudDebugMode::DetailNoise);
            UpdateDebugTitle();
            return 0;
        }
        if (m_renderer && wParam == 'P')
        {
            m_renderer->SetDebugMode(CloudDebugMode::Erosion);
            UpdateDebugTitle();
            return 0;
        }
        if (m_renderer && wParam == 'U')
        {
            m_renderer->SetDebugMode(CloudDebugMode::DetailSampleMask);
            UpdateDebugTitle();
            return 0;
        }
        if (m_renderer && wParam == 'Q')
        {
            m_renderer->ApplyStage1ValidationPreset(Stage1ValidationPreset::DefaultVolume);
            UpdateDebugTitle();
            return 0;
        }
        if (m_renderer && wParam == 'Y')
        {
            m_renderer->ApplyStage1ValidationPreset(Stage1ValidationPreset::WideVolume);
            UpdateDebugTitle();
            return 0;
        }
        if (m_renderer && wParam == 'W')
        {
            m_renderer->ApplyStage1ValidationPreset(Stage1ValidationPreset::ThinVolume);
            UpdateDebugTitle();
            return 0;
        }
        if (m_renderer && wParam == 'E')
        {
            m_renderer->ApplyStage1ValidationPreset(Stage1ValidationPreset::ThickVolume);
            UpdateDebugTitle();
            return 0;
        }
        if (m_renderer && wParam == 'R')
        {
            m_renderer->ApplyStage1ValidationPreset(Stage1ValidationPreset::FineStep);
            UpdateDebugTitle();
            return 0;
        }
        if (m_renderer && wParam == 'T')
        {
            m_renderer->ApplyStage1ValidationPreset(Stage1ValidationPreset::CoarseStep);
            UpdateDebugTitle();
            return 0;
        }
        if (m_renderer && wParam == 'N')
        {
            m_renderer->ApplyStage2NoisePreset(Stage2NoisePreset::DefaultNoise);
            UpdateDebugTitle();
            return 0;
        }
        if (m_renderer && wParam == 'A')
        {
            m_renderer->ApplyStage2NoisePreset(Stage2NoisePreset::SparseCoverage);
            UpdateDebugTitle();
            return 0;
        }
        if (m_renderer && wParam == 'S')
        {
            m_renderer->ApplyStage2NoisePreset(Stage2NoisePreset::DenseCoverage);
            UpdateDebugTitle();
            return 0;
        }
        if (m_renderer && wParam == 'D')
        {
            m_renderer->ApplyStage2NoisePreset(Stage2NoisePreset::LargeBlobs);
            UpdateDebugTitle();
            return 0;
        }
        if (m_renderer && wParam == 'F')
        {
            m_renderer->ApplyStage2NoisePreset(Stage2NoisePreset::SmallBlobs);
            UpdateDebugTitle();
            return 0;
        }
        if (m_renderer && wParam == 'G')
        {
            m_renderer->ApplyStage2NoisePreset(Stage2NoisePreset::StoppedWind);
            UpdateDebugTitle();
            return 0;
        }
        if (m_renderer && wParam == 'H')
        {
            m_renderer->ApplyStage2NoisePreset(Stage2NoisePreset::FastWind);
            UpdateDebugTitle();
            return 0;
        }
        if (m_renderer && wParam == 'K')
        {
            m_renderer->ApplyStage2NoisePreset(Stage2NoisePreset::OffsetNoise);
            UpdateDebugTitle();
            return 0;
        }
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}
