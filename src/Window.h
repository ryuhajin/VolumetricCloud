// ============================================================================
//  Window.h  —  Win32 윈도우 생성 및 입력 처리
// ----------------------------------------------------------------------------
//  창을 만들고 메시지 루프를 돌린다. 마우스 드래그/휠 입력은 Camera로,
//  창 크기 변경(WM_SIZE)은 Renderer로 전달한다.
// ============================================================================
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "Stage13CameraPresets.h"

class Camera;   // 전방 선언 (구현은 Window.cpp 에서 포함)
class Renderer;

// Native 1080p 전환은 UI가 실패 원인을 구분할 수 있도록 bool보다 자세한
// 결과를 돌려준다. AlreadyActive도 요청이 충족된 성공 상태다.
enum class Native1080pResult
{
    Success,
    AlreadyActive,
    WindowUnavailable,
    MonitorTooSmall,
    Win32Failure,
};

class Window
{
public:
    Window(HINSTANCE hInstance, int width, int height, const wchar_t* title,
           bool showWindow = true);
    ~Window();

    // 입력을 받을 대상 연결
    void SetCamera(Camera* camera)     { m_camera = camera; }
    void SetRenderer(Renderer* renderer) { m_renderer = renderer; }
    void ApplyInitialPortfolioCamera();

    // 대기 중인 메시지를 모두 처리. 종료(WM_QUIT) 시 false 반환.
    bool ProcessMessages();
    void UpdateCameraMovement(float deltaSeconds);

    HWND GetHandle() const { return m_hwnd; }
    int  GetWidth()  const { return m_width; }
    int  GetHeight() const { return m_height; }
    UINT GetDpi() const { return m_dpi; }
    float GetDpiScale() const
    {
        return static_cast<float>(m_dpi) / 96.0f;
    }
    bool IsPerMonitorV2DpiAware() const;
    bool QueryPhysicalClientExtent(int& width, int& height) const;

    // 현재 모니터 안에 정확한 1920x1080 client를 갖는 borderless 창을 만든다.
    // RestoreWindowed()는 전환 직전 style/placement/client 크기로 되돌린다.
    Native1080pResult EnterNative1080p();
    bool RestoreWindowed();
    bool IsNative1080p() const { return m_native1080pActive; }
    void InjectWindowedRestoreFailureForTest()
    {
        m_failNextWindowedRestoreForTest = true;
    }

    void RefreshDebugTitle() { UpdateDebugTitle(); }

private:
    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);
    void UpdateDebugTitle();
    bool HandleGlobalDebugShortcut(WPARAM virtualKey);
    void ApplyCameraPreset(Stage13CameraPresetId id,
                           const wchar_t* displayName);
    void SetCameraPresetName(const wchar_t* displayName);
    void MarkCameraManuallyAdjusted();
    bool UpdatePhysicalClientExtent(bool notifyRenderer);
    bool ResizeClientArea(int width, int height, UINT dpi);
    bool ApplySavedWindowedState();

    struct WindowedRestoreState
    {
        bool valid = false;
        LONG_PTR style = 0;
        LONG_PTR exStyle = 0;
        WINDOWPLACEMENT placement = { sizeof(WINDOWPLACEMENT) };
        int clientWidth = 0;
        int clientHeight = 0;
        bool wasVisible = false;
    };

    HWND      m_hwnd    = nullptr;
    int       m_width   = 0;
    int       m_height  = 0;
    UINT      m_dpi     = 96;
    bool      m_native1080pActive = false;
    bool      m_failNextWindowedRestoreForTest = false;
    WindowedRestoreState m_windowedRestore;

    Camera*   m_camera   = nullptr;
    Renderer* m_renderer = nullptr;
    // 마우스 드래그 상태
    bool m_dragging  = false;
    int  m_lastMouseX = 0;
    int  m_lastMouseY = 0;
};
