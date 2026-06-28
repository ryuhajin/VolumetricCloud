// ============================================================================
//  Camera.h  —  마우스 오빗(궤도) 카메라
// ----------------------------------------------------------------------------
//  타깃(보통 볼륨 중심)을 중심으로 yaw/pitch 만큼 회전하고 distance 만큼
//  떨어진 위치에서 타깃을 바라본다. 셰이더의 레이 생성을 위해 역 뷰-투영 행렬
//  (invViewProj)을 제공한다.
// ============================================================================
#pragma once

#include <DirectXMath.h>

class Camera
{
public:
    Camera();

    // 입력 → 카메라 상태 갱신
    void Rotate(float dxPixels, float dyPixels); // 마우스 드래그
    void Zoom(float wheelDelta);                 // 마우스 휠

    void SetAspect(float aspect) { m_aspect = aspect; }

    // 행렬 계산 (열은 row-major DirectXMath 기준)
    DirectX::XMMATRIX GetViewProj() const;
    DirectX::XMMATRIX GetInvViewProj() const;    // 셰이더 레이 생성용
    DirectX::XMFLOAT3 GetPosition() const;       // 레이 원점

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
};
