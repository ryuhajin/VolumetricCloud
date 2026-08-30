// ============================================================================
//  Stage15CirrusMath.h - Cirrus 방향 좌표·세로 profile CPU 회귀 기준
// ============================================================================
#pragma once

#include "CloudShapeParameters.h"

#include <DirectXMath.h>
#include <algorithm>
#include <cmath>

namespace stage15cirrus
{
struct Basis
{
    DirectX::XMFLOAT2 along;
    DirectX::XMFLOAT2 across;
};

inline Basis DirectionBasis(const CloudShapeParameters& shape)
{
    const CloudShapeParameters safe = SanitizeCloudShapeParameters(shape);
    return {
        safe.cirrusFlowDirectionXZ,
        { -safe.cirrusFlowDirectionXZ.y, safe.cirrusFlowDirectionXZ.x }
    };
}

inline float Frac(float value)
{
    return value - std::floor(value);
}

inline DirectX::XMFLOAT3 DirectionalUvw(
    const DirectX::XMFLOAT3& stationaryWorld,
    const CloudShapeParameters& shape, bool detail)
{
    const CloudShapeParameters safe = SanitizeCloudShapeParameters(shape);
    const Basis basis = DirectionBasis(safe);
    const float along = stationaryWorld.x * basis.along.x +
                        stationaryWorld.z * basis.along.y;
    const float across = stationaryWorld.x * basis.across.x +
                         stationaryWorld.z * basis.across.y;
    const float alongScale = detail ? safe.cirrusDetailAlongScaleMeters
                                    : safe.cirrusBaseAlongScaleMeters;
    const float acrossScale = detail ? safe.cirrusDetailAcrossScaleMeters
                                     : safe.cirrusBaseAcrossScaleMeters;
    const float verticalScale = detail ? safe.cirrusDetailVerticalScaleMeters
                                       : safe.cirrusBaseVerticalScaleMeters;
    return { Frac(along / alongScale),
             Frac(stationaryWorld.y / verticalScale),
             Frac(across / acrossScale) };
}

inline DirectX::XMFLOAT2 WeatherUv(
    const DirectX::XMFLOAT3& stationaryWorld, float worldSizeMeters,
    const CloudShapeParameters& shape)
{
    const CloudShapeParameters safe = SanitizeCloudShapeParameters(shape);
    const Basis basis = DirectionBasis(safe);
    const float along = stationaryWorld.x * basis.along.x +
                        stationaryWorld.z * basis.along.y;
    const float across = stationaryWorld.x * basis.across.x +
                         stationaryWorld.z * basis.across.y;
    const float safeWorld = std::max(worldSizeMeters, 1.0f);
    const float aspect = safe.cirrusBaseAcrossScaleMeters /
                         safe.cirrusBaseAlongScaleMeters;
    return { Frac(along / safeWorld),
             Frac(across / std::max(safeWorld * aspect, 1.0f)) };
}

inline float LocalThickness(float potential,
                            const CloudShapeParameters& shape)
{
    const CloudShapeParameters safe = SanitizeCloudShapeParameters(shape);
    return safe.cirrusMinimumThicknessMeters +
        (safe.cirrusMaximumThicknessMeters -
         safe.cirrusMinimumThicknessMeters) * std::clamp(potential, 0.0f, 1.0f);
}

inline float LocalHeightFraction(
    float worldY, float layerBottom, float layerTop,
    float localThickness, const CloudShapeParameters& shape)
{
    const CloudShapeParameters safe = SanitizeCloudShapeParameters(shape);
    const float layerThickness = std::max(layerTop - layerBottom, 1.0f);
    const float center = layerBottom +
        layerThickness * safe.cirrusVerticalProfileCenter;
    const float bottom = center - localThickness * 0.5f;
    return (worldY - bottom) / std::max(localThickness, 1.0f);
}

inline float Smoothstep(float edge0, float edge1, float value)
{
    const float t = std::clamp(
        (value - edge0) / std::max(edge1 - edge0, 1.0e-6f), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline float VerticalProfile(float localHeight,
                             const CloudShapeParameters& shape)
{
    if (localHeight < 0.0f || localHeight > 1.0f)
        return 0.0f;
    const CloudShapeParameters safe = SanitizeCloudShapeParameters(shape);
    const float centered = std::abs(localHeight * 2.0f - 1.0f);
    return 1.0f - Smoothstep(
        safe.cirrusVerticalProfileHalfWidth, 1.0f, centered);
}
}
