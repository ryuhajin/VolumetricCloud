// ============================================================================
//  Window.h  —  Win32 윈도우 생성 및 입력 처리
// ----------------------------------------------------------------------------
//  창을 만들고 메시지 루프를 돌린다. 마우스 드래그/휠 입력은 Camera로,
//  창 크기 변경(WM_SIZE)은 Renderer로 전달한다.
// ============================================================================
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

class Camera;   // 전방 선언 (구현은 Window.cpp 에서 포함)
class Renderer;

class Window
{
public:
    Window(HINSTANCE hInstance, int width, int height, const wchar_t* title,
           bool showWindow = true);
    ~Window();

    // 입력을 받을 대상 연결
    void SetCamera(Camera* camera)     { m_camera = camera; }
    void SetRenderer(Renderer* renderer) { m_renderer = renderer; }

    // 대기 중인 메시지를 모두 처리. 종료(WM_QUIT) 시 false 반환.
    bool ProcessMessages();

    HWND GetHandle() const { return m_hwnd; }
    int  GetWidth()  const { return m_width; }
    int  GetHeight() const { return m_height; }

private:
    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);
    void UpdateStage1Title();

    HWND      m_hwnd    = nullptr;
    int       m_width   = 0;
    int       m_height  = 0;

    Camera*   m_camera   = nullptr;
    Renderer* m_renderer = nullptr;
    const wchar_t* m_cameraPresetName = L"외부 기본(F5)";

    // 마우스 드래그 상태
    bool m_dragging  = false;
    int  m_lastMouseX = 0;
    int  m_lastMouseY = 0;
};
