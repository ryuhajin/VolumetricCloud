// ============================================================================
//  Camera.h  —  FPS 자유 시점 카메라
// ----------------------------------------------------------------------------
//  위치와 yaw/pitch를 분리해 마우스 회전이 카메라 위치를 바꾸지 않는다.
//  기존 position/target 프리셋과 저장 파일은 reference distance로 호환하며,
//  셰이더의 레이 생성을 위한 역 뷰-투영 행렬(invViewProj)을 제공한다.
// ============================================================================
#pragma once

#include <DirectXMath.h>
#include <string>

class Camera
{
public:
    Camera();

    // 입력 → 카메라 상태 갱신
    void Rotate(float dxPixels, float dyPixels); // 마우스 드래그
    void SetOrbit(float yaw, float pitch, float distance,
                  const DirectX::XMFLOAT3& target);
    void SetLookAt(const DirectX::XMFLOAT3& position,
                   const DirectX::XMFLOAT3& target);
    void MoveLocal(float forwardMeters, float rightMeters);

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
    DirectX::XMFLOAT3 GetForward() const;
    DirectX::XMFLOAT3 GetTarget() const;
    float GetDistance() const { return m_referenceDistance; }
    float GetYawDegrees() const;
    float GetPitchDegrees() const;
    float GetNearPlane() const { return m_nearZ; }
    float GetFarPlane() const { return m_farZ; }
    float GetFovYDegrees() const;
    const std::wstring& GetDebugName() const { return m_debugName; }
    bool WasManuallyAdjusted() const { return m_manuallyAdjusted; }

private:
    DirectX::XMFLOAT3 m_position; // 실제 카메라 위치
    float m_yaw;                  // 월드 +Z 기준 좌우 시선 (라디안)
    float m_pitch;                // 수평 기준 상하 시선 (라디안)
    // 기존 target 저장·표시와 SetOrbit 호환에만 사용하는 시선 앞 거리다.
    float m_referenceDistance;

    float m_aspect;    // 화면 종횡비 (width/height)
    float m_fovY;      // 수직 시야각 (라디안)
    float m_nearZ;
    float m_farZ;
    std::wstring m_debugName = L"카메라";
    bool m_manuallyAdjusted = false;
};
