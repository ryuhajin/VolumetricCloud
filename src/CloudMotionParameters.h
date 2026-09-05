// ============================================================================
//  CloudMotionParameters.h - 타입/프리셋과 무관한 세션 전역 구름 이동
// ============================================================================
#pragma once

#include <DirectXMath.h>
#include <algorithm>
#include <cmath>

struct CloudMotionParameters
{
    DirectX::XMFLOAT3 direction = { 0.9701425f, 0.0f, 0.2425356f };
    float speedMetersPerSecond = 12.0f;
};

inline CloudMotionParameters SanitizeCloudMotionParameters(
    const CloudMotionParameters& value)
{
    CloudMotionParameters result = value;
    const float lengthSquared = result.direction.x * result.direction.x +
        result.direction.z * result.direction.z;
    if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-12f)
        result.direction = { 0.9701425f, 0.0f, 0.2425356f };
    else
    {
        const float inverseLength = 1.0f / std::sqrt(lengthSquared);
        result.direction.x *= inverseLength;
        result.direction.y = 0.0f;
        result.direction.z *= inverseLength;
    }
    result.speedMetersPerSecond = std::clamp(
        std::isfinite(result.speedMetersPerSecond)
            ? result.speedMetersPerSecond : 12.0f,
        0.0f, 1000.0f);
    return result;
}
