// ============================================================================
//  Camera.h  —  마우스 오빗(궤도) 카메라
// ----------------------------------------------------------------------------
//  타깃(보통 볼륨 중심)을 중심으로 yaw/pitch 만큼 회전하고 distance 만큼
//  떨어진 위치에서 타깃을 바라본다. 셰이더의 레이 생성을 위해 역 뷰-투영 행렬
//  (invViewProj)을 제공한다.
// ============================================================================
#pragma once

#include <DirectXMath.h>
#include <string>

enum class CameraZoomSpeed
{
    Normal,
    Fast,
};

class Camera
{
public:
    Camera();

    // 입력 → 카메라 상태 갱신
    void Rotate(float dxPixels, float dyPixels); // 마우스 드래그
    void Zoom(float wheelDelta,
              CameraZoomSpeed speed = CameraZoomSpeed::Normal); // 마우스 휠
    void SetOrbit(float yaw, float pitch, float distance,
                  const DirectX::XMFLOAT3& target);
    void SetLookAt(const DirectX::XMFLOAT3& position,
                   const DirectX::XMFLOAT3& target);
    void TranslateRigLocal(float forwardMeters, float rightMeters);

    void SetAspect(float aspect) { m_aspect = aspect; }
    void SetClipPlanes(float nearZ, float farZ);
    void SetFovYDegrees(float degrees);
    void SetDebugName(const wchar_t* name);
    void MarkManuallyAdjusted() { m_manuallyAdjusted = true; }

    // 행렬 계산 (열은 row-major DirectXMath 기준)
    DirectX::XMMATRIX GetViewProj() const;
    DirectX::XMMATRIX GetInvViewProj() const;    // 셰이더 레이 생성용
    DirectX::XMMATRIX GetInvProjection() const;
    DirectX::XMMATRIX GetInvViewRotation() const;
    DirectX::XMFLOAT3 GetPosition() const;       // 레이 원점
    DirectX::XMFLOAT3 GetTarget() const { return m_target; }
    float GetDistance() const { return m_distance; }
    float GetNearPlane() const { return m_nearZ; }
    float GetFarPlane() const { return m_farZ; }
    float GetFovYDegrees() const;
    const std::wstring& GetDebugName() const { return m_debugName; }
    bool WasManuallyAdjusted() const { return m_manuallyAdjusted; }

private:
    // 궤도 파라미터
    float m_yaw;       // 좌우 회전 (라디안)
    float m_pitch;     // 상하 회전 (라디안)
    float m_distance;  // 타깃과의 거리
    DirectX::XMFLOAT3 m_target;

    float m_aspect;    // 화면 종횡비 (width/height)
    float m_fovY;      // 수직 시야각 (라디안)
    float m_nearZ;
    float m_farZ;
    std::wstring m_debugName = L"카메라";
    bool m_manuallyAdjusted = false;
};
