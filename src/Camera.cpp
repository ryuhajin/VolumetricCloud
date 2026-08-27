#include "Camera.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

Camera::Camera()
    : m_position(0.0f, 0.0f, 0.0f)
    , m_yaw(0.0f)
    , m_pitch(0.0f)
    , m_referenceDistance(8.0f)
    , m_aspect(16.0f / 9.0f)
    , m_fovY(XMConvertToRadians(60.0f))
    , m_nearZ(0.1f)
    , m_farZ(60000.0f)
{
    // 기존 기본 orbit pose를 그대로 FPS position/forward로 변환한다.
    SetOrbit(0.0f, 0.2f, 8.0f, { 0.0f, 0.0f, 0.0f });
}

void Camera::Rotate(float dxPixels, float dyPixels)
{
    // 픽셀 이동량을 라디안으로 (감도 상수)
    const float sensitivity = 0.005f;
    if (!std::isfinite(dxPixels) || !std::isfinite(dyPixels))
        return;
    m_yaw += dxPixels * sensitivity;
    m_yaw = std::remainder(m_yaw, XM_2PI);
    // 화면 위로 드래그(dy<0)하면 시선도 위로 향한다.
    m_pitch -= dyPixels * sensitivity;

    // 짐벌 뒤집힘 방지: pitch를 거의 ±90° 안쪽으로 제한
    const float limit = XM_PIDIV2 - 0.01f;
    m_pitch = std::clamp(m_pitch, -limit, limit);
}

void Camera::SetOrbit(float yaw, float pitch, float distance,
                      const XMFLOAT3& target)
{
    if (!std::isfinite(yaw) || !std::isfinite(pitch) ||
        !std::isfinite(distance) || !std::isfinite(target.x) ||
        !std::isfinite(target.y) || !std::isfinite(target.z))
        return;
    const float safePitch = std::clamp(
        pitch, -XM_PIDIV2 + 0.01f, XM_PIDIV2 - 0.01f);
    const float safeDistance = std::clamp(distance, 1.5f, 100000.0f);
    const float cosPitch = std::cos(safePitch);
    const XMFLOAT3 position = {
        target.x + safeDistance * cosPitch * std::sin(yaw),
        target.y + safeDistance * std::sin(safePitch),
        target.z + safeDistance * cosPitch * std::cos(yaw)
    };
    SetLookAt(position, target);
}

void Camera::SetLookAt(const XMFLOAT3& position, const XMFLOAT3& target)
{
    if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
        !std::isfinite(position.z) || !std::isfinite(target.x) ||
        !std::isfinite(target.y) || !std::isfinite(target.z))
        return;
    const float offsetX = target.x - position.x;
    const float offsetY = target.y - position.y;
    const float offsetZ = target.z - position.z;
    const float distance = std::sqrt(
        offsetX * offsetX + offsetY * offsetY + offsetZ * offsetZ);
    if (!std::isfinite(distance) || distance < 1e-5f)
        return;

    m_position = position;
    m_referenceDistance = std::clamp(distance, 1.5f, 100000.0f);
    m_yaw = std::atan2(offsetX, offsetZ);
    m_pitch = std::asin(std::clamp(offsetY / distance, -1.0f, 1.0f));
    const float limit = XM_PIDIV2 - 0.01f;
    m_pitch = std::clamp(m_pitch, -limit, limit);
}

void Camera::MoveLocal(float forwardMeters, float rightMeters)
{
    if (!std::isfinite(forwardMeters) || !std::isfinite(rightMeters))
        return;

    const XMFLOAT3 forwardValue = GetForward();
    XMVECTOR forward = XMLoadFloat3(&forwardValue);

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
    XMVECTOR position = XMVectorAdd(XMLoadFloat3(&m_position), delta);
    XMStoreFloat3(&m_position, position);
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
    return m_position;
}

DirectX::XMFLOAT3 Camera::GetForward() const
{
    const float cosPitch = std::cos(m_pitch);
    return {
        cosPitch * std::sin(m_yaw),
        std::sin(m_pitch),
        cosPitch * std::cos(m_yaw)
    };
}

DirectX::XMFLOAT3 Camera::GetTarget() const
{
    const XMFLOAT3 forward = GetForward();
    return {
        m_position.x + forward.x * m_referenceDistance,
        m_position.y + forward.y * m_referenceDistance,
        m_position.z + forward.z * m_referenceDistance
    };
}

float Camera::GetYawDegrees() const
{
    return XMConvertToDegrees(m_yaw);
}

float Camera::GetPitchDegrees() const
{
    return XMConvertToDegrees(m_pitch);
}

DirectX::XMMATRIX Camera::GetViewProj() const
{
    const XMFLOAT3 target = GetTarget();
    XMVECTOR eye = XMLoadFloat3(&m_position);
    XMVECTOR at  = XMLoadFloat3(&target);
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
    const XMFLOAT3 target = GetTarget();
    const XMVECTOR eye = XMLoadFloat3(&m_position);
    const XMVECTOR at = XMLoadFloat3(&target);
    const XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMMATRIX inverseView = XMMatrixInverse(
        nullptr, XMMatrixLookAtLH(eye, at, up));
    inverseView.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
    return inverseView;
}
