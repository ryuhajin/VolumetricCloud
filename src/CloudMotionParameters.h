// ============================================================================
//  CloudMotionParameters.h - 타입/프리셋과 무관한 세션 전역 구름 이동
// ============================================================================
#pragma once

#include <DirectXMath.h>
#include "FormationParameterRanges.h"
#include <algorithm>
#include <cmath>

struct CloudMotionParameters
{
    // [직접 조절] F1/세션 전역 xyz 월드 방향. UI XZ [-1,1]를 길이 1로 정규화하고 Y=0; 0길이/비유한은 기본값 복원. 방향 전환만 담당.
    DirectX::XMFLOAT3 direction = { 0.9701425f, 0.0f, 0.2425356f };
    // [직접 조절] F1 m/s. [강제 범위] [0,1000], [UI 범위] [0,1000], 기본 12. 0은 정지; 타입/콘셉트/Custom 전환에도 유지.
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
        0.0f, formationrange::motionSpeedMax);
    return result;
}
