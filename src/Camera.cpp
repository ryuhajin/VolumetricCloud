#include "Camera.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

Camera::Camera()
    : m_yaw(0.0f)
    , m_pitch(0.2f)
    , m_distance(8.0f)
    , m_target(0.0f, 0.0f, 0.0f)
    , m_aspect(16.0f / 9.0f)
    , m_fovY(XMConvertToRadians(60.0f))
    , m_nearZ(0.1f)
    , m_farZ(60000.0f)
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

void Camera::Zoom(float wheelDelta, CameraZoomSpeed speed)
{
    // 지수식은 여러 notch가 한 메시지로 들어와도 음수 거리를 만들지 않는다.
    // 일반은 한 notch마다 1.25배, Shift 고속은 2배 거리 비율을 사용한다.
    const float notches = wheelDelta / 120.0f;
    const float base = speed == CameraZoomSpeed::Fast ? 2.0f : 1.25f;
    const float factor = std::pow(base, -notches);
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

void Camera::SetLookAt(const XMFLOAT3& position, const XMFLOAT3& target)
{
    const float offsetX = position.x - target.x;
    const float offsetY = position.y - target.y;
    const float offsetZ = position.z - target.z;
    const float distance = std::sqrt(
        offsetX * offsetX + offsetY * offsetY + offsetZ * offsetZ);
    if (!std::isfinite(distance) || distance < 1e-5f)
        return;

    m_target = target;
    m_distance = std::clamp(distance, 1.5f, 100000.0f);
    m_yaw = std::atan2(offsetX, offsetZ);
    m_pitch = std::asin(std::clamp(offsetY / distance, -1.0f, 1.0f));
    const float limit = XM_PIDIV2 - 0.01f;
    m_pitch = std::clamp(m_pitch, -limit, limit);
}

void Camera::TranslateRigLocal(float forwardMeters, float rightMeters)
{
    if (!std::isfinite(forwardMeters) || !std::isfinite(rightMeters))
        return;

    const XMFLOAT3 position = GetPosition();
    XMVECTOR forward = XMVectorSubtract(
        XMLoadFloat3(&m_target), XMLoadFloat3(&position));
    const float forwardLength = XMVectorGetX(XMVector3Length(forward));
    if (!std::isfinite(forwardLength) || forwardLength < 1e-5f)
        return;
    forward = XMVectorScale(forward, 1.0f / forwardLength);

    const XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMVECTOR right = XMVector3Cross(worldUp, forward);
    const float rightLength = XMVectorGetX(XMVector3Length(right));
    if (rightLength > 1e-5f)
        right = XMVectorScale(right, 1.0f / rightLength);
    else
        right = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);

    const XMVECTOR delta = XMVectorAdd(
        XMVectorScale(forward, forwardMeters),
        XMVectorScale(right, rightMeters));
    XMVECTOR target = XMVectorAdd(XMLoadFloat3(&m_target), delta);
    XMStoreFloat3(&m_target, target);
}

void Camera::SetClipPlanes(float nearZ, float farZ)
{
    m_nearZ = std::max(nearZ, 1e-4f);
    m_farZ = std::max(farZ, m_nearZ + 1e-3f);
}

void Camera::SetFovYDegrees(float degrees)
{
    const float safeDegrees = std::isfinite(degrees) ? degrees : 60.0f;
    m_fovY = XMConvertToRadians(std::clamp(safeDegrees, 20.0f, 120.0f));
}

float Camera::GetFovYDegrees() const
{
    return XMConvertToDegrees(m_fovY);
}

void Camera::SetDebugName(const wchar_t* name)
{
    m_debugName = name ? name : L"카메라";
    m_manuallyAdjusted = false;
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

DirectX::XMMATRIX Camera::GetInvProjection() const
{
    const XMMATRIX projection = XMMatrixPerspectiveFovLH(
        m_fovY, m_aspect, m_nearZ, m_farZ);
    return XMMatrixInverse(nullptr, projection);
}

DirectX::XMMATRIX Camera::GetInvViewRotation() const
{
    const XMFLOAT3 position = GetPosition();
    const XMVECTOR eye = XMLoadFloat3(&position);
    const XMVECTOR at = XMLoadFloat3(&m_target);
    const XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMMATRIX inverseView = XMMatrixInverse(
        nullptr, XMMatrixLookAtLH(eye, at, up));
    inverseView.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
    return inverseView;
}
