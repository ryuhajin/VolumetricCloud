#include "Camera.h"
#include <algorithm>

using namespace DirectX;

Camera::Camera()
    : m_yaw(0.0f)
    , m_pitch(0.2f)
    , m_distance(8.0f)
    , m_target(0.0f, 0.0f, 0.0f)
    , m_aspect(16.0f / 9.0f)
    , m_fovY(XMConvertToRadians(60.0f))
    , m_nearZ(0.1f)
    , m_farZ(1000.0f)
{
}

void Camera::Rotate(float dxPixels, float dyPixels)
{
    // 픽셀 이동량을 라디안으로 (감도 상수)
    const float sensitivity = 0.005f;
    m_yaw   += dxPixels * sensitivity;
    m_pitch += dyPixels * sensitivity;

    // 짐벌 뒤집힘 방지: pitch를 거의 ±90° 안쪽으로 제한
    const float limit = XM_PIDIV2 - 0.01f;
    m_pitch = std::clamp(m_pitch, -limit, limit);
}

void Camera::Zoom(float wheelDelta)
{
    // 휠 한 칸(120) 당 일정 비율로 거리 조절
    float factor = 1.0f - (wheelDelta / 120.0f) * 0.1f;
    m_distance = std::clamp(m_distance * factor, 1.5f, 100000.0f);
}

void Camera::SetOrbit(float yaw, float pitch, float distance,
                      const XMFLOAT3& target)
{
    m_yaw = yaw;
    m_pitch = std::clamp(pitch, -XM_PIDIV2 + 0.01f, XM_PIDIV2 - 0.01f);
    m_distance = std::clamp(distance, 1.5f, 100000.0f);
    m_target = target;
}

DirectX::XMFLOAT3 Camera::GetPosition() const
{
    // 구면 좌표 → 데카르트 좌표 (타깃 기준 오프셋)
    float cosP = cosf(m_pitch);
    XMFLOAT3 pos;
    pos.x = m_target.x + m_distance * cosP * sinf(m_yaw);
    pos.y = m_target.y + m_distance * sinf(m_pitch);
    pos.z = m_target.z + m_distance * cosP * cosf(m_yaw);
    return pos;
}

DirectX::XMMATRIX Camera::GetViewProj() const
{
    XMFLOAT3 posf = GetPosition();
    XMVECTOR eye = XMLoadFloat3(&posf);
    XMVECTOR at  = XMLoadFloat3(&m_target);
    XMVECTOR up  = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    XMMATRIX view = XMMatrixLookAtLH(eye, at, up);
    XMMATRIX proj = XMMatrixPerspectiveFovLH(m_fovY, m_aspect, m_nearZ, m_farZ);
    return XMMatrixMultiply(view, proj);
}

DirectX::XMMATRIX Camera::GetInvViewProj() const
{
    XMMATRIX vp = GetViewProj();
    return XMMatrixInverse(nullptr, vp);
}
