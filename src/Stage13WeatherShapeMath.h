// ============================================================================
//  Stage13WeatherShapeMath.h - 단계 13-4E Weather 기반 물리 두께와 세로 형상 CPU 기준
// ============================================================================
#pragma once

#include <algorithm>
#include <cmath>

namespace stage13shape
{
inline constexpr double kLayerBottomMeters = 1500.0;
inline constexpr double kLayerThicknessMeters = 6000.0;
inline constexpr double kStratusMinimumThicknessMeters = 1500.0;
inline constexpr double kStratusMaximumThicknessMeters = 2500.0;
inline constexpr double kCumulusMinimumThicknessMeters = 3000.0;
inline constexpr double kCumulusMaximumThicknessMeters = 6000.0;

inline double SaturateFinite(double value)
{
    return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 0.0;
}

inline double Smoothstep(double edge0, double edge1, double value)
{
    if (!std::isfinite(edge0) || !std::isfinite(edge1) || edge1 <= edge0)
        return value >= edge1 ? 1.0 : 0.0;
    const double x = std::clamp((value - edge0) / (edge1 - edge0), 0.0, 1.0);
    return x * x * (3.0 - 2.0 * x);
}

struct ThicknessRange
{
    double minimumMeters = kStratusMinimumThicknessMeters;
    double maximumMeters = kStratusMaximumThicknessMeters;
};

struct HorizontalOffset
{
    double x = 0.0;
    double z = 0.0;
};

struct SamplePosition
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

inline HorizontalOffset EvaluatePhysicalCloudAdvectionOffset(
    double directionX, double directionZ, double speedMetersPerSecond,
    double timeSeconds)
{
    if (!std::isfinite(directionX) || !std::isfinite(directionZ) ||
        !std::isfinite(speedMetersPerSecond) ||
        !std::isfinite(timeSeconds))
        return {};
    const double directionLength = std::sqrt(
        directionX * directionX + directionZ * directionZ);
    if (directionLength <= 1e-6 || speedMetersPerSecond <= 0.0 ||
        timeSeconds <= 0.0)
        return {};
    const double distance = speedMetersPerSecond * timeSeconds;
    return { directionX / directionLength * distance,
             directionZ / directionLength * distance };
}

inline SamplePosition EvaluatePhysicalCloudSamplePosition(
    double worldX, double worldY, double worldZ,
    double directionX, double directionZ, double speedMetersPerSecond,
    double timeSeconds)
{
    const HorizontalOffset offset = EvaluatePhysicalCloudAdvectionOffset(
        directionX, directionZ, speedMetersPerSecond, timeSeconds);
    return { worldX - offset.x, worldY, worldZ - offset.z };
}

inline double AdvanceEffectiveTime(double currentTime, double deltaSeconds,
                                   double timeScale, bool paused)
{
    const double current = std::isfinite(currentTime)
        ? std::max(currentTime, 0.0) : 0.0;
    if (paused)
        return current;
    const double delta = std::isfinite(deltaSeconds)
        ? std::clamp(deltaSeconds, 0.0, 0.25) : 0.0;
    const double scale = std::isfinite(timeScale)
        ? std::max(timeScale, 0.0) : 0.0;
    return std::max(current + delta * scale, 0.0);
}

inline ThicknessRange EvaluateThicknessRange(double cloudType)
{
    const double type = SaturateFinite(cloudType);
    return {
        kStratusMinimumThicknessMeters +
            (kCumulusMinimumThicknessMeters - kStratusMinimumThicknessMeters) * type,
        kStratusMaximumThicknessMeters +
            (kCumulusMaximumThicknessMeters - kStratusMaximumThicknessMeters) * type
    };
}

inline double EvaluateLocalThicknessMeters(double thicknessPotential,
                                           double cloudType)
{
    const ThicknessRange range = EvaluateThicknessRange(cloudType);
    return range.minimumMeters + (range.maximumMeters - range.minimumMeters) *
        SaturateFinite(thicknessPotential);
}

inline double EvaluateLocalHeight(double worldY, double thicknessMeters,
                                  double layerBottomMeters = kLayerBottomMeters)
{
    const double safeThickness = std::max(
        std::isfinite(thicknessMeters) ? thicknessMeters : 0.0, 1.0);
    return (worldY - layerBottomMeters) / safeThickness;
}

inline double EvaluateEnvelope(double height, double bottomFadeEnd,
                               double topFadeStart)
{
    const double h = SaturateFinite(height);
    return SaturateFinite(
        Smoothstep(0.0, bottomFadeEnd, h) *
        (1.0 - Smoothstep(topFadeStart, 1.0, h)));
}

inline double EvaluateStratusProfile(double height)
{
    return EvaluateEnvelope(height, 0.06, 0.65);
}

inline double EvaluateMixedProfile(double height)
{
    return EvaluateEnvelope(height, 0.10, 0.86);
}

inline double EvaluateCumulusProfile(double height)
{
    const double h = SaturateFinite(height);
    const double upperMass = 0.65 + 0.35 * Smoothstep(0.08, 0.70, h);
    return SaturateFinite(EvaluateEnvelope(h, 0.08, 0.93) * upperMass);
}

inline double EvaluateTypedVerticalProfile(double height, double cloudType)
{
    const double type = SaturateFinite(cloudType);
    const double stratus = EvaluateStratusProfile(height);
    const double mixed = EvaluateMixedProfile(height);
    const double cumulus = EvaluateCumulusProfile(height);
    return type <= 0.5
        ? stratus + (mixed - stratus) * (type * 2.0)
        : mixed + (cumulus - mixed) * ((type - 0.5) * 2.0);
}

inline double EvaluateContinuousFootprintCutoff(double height,
                                                double bottomCutoff,
                                                double middleCutoff,
                                                double topCutoff,
                                                double peakHeight)
{
    const double h = SaturateFinite(height);
    const double peak = std::clamp(peakHeight, 0.01, 0.99);
    return h <= peak
        ? bottomCutoff + (middleCutoff - bottomCutoff) *
            Smoothstep(0.0, peak, h)
        : middleCutoff + (topCutoff - middleCutoff) *
            Smoothstep(peak, 1.0, h);
}

inline double EvaluateFootprintScale(double cutoff, double middleCutoff)
{
    return SaturateFinite(
        (1.0 - cutoff) / std::max(1.0 - middleCutoff, 1e-9));
}

inline double EvaluateTypedFootprintScale(double height, double cloudType)
{
    const double h = SaturateFinite(height);
    const double type = SaturateFinite(cloudType);
    const double stratus = EvaluateFootprintScale(
        EvaluateContinuousFootprintCutoff(h, 0.16, 0.08, 0.22, 0.45), 0.08);
    const double mixed = EvaluateFootprintScale(
        EvaluateContinuousFootprintCutoff(h, 0.22, 0.04, 0.38, 0.50), 0.04);
    const double cumulus = EvaluateFootprintScale(
        EvaluateContinuousFootprintCutoff(h, 0.32, 0.03, 0.62, 0.58), 0.03);
    return type <= 0.5
        ? stratus + (mixed - stratus) * (type * 2.0)
        : mixed + (cumulus - mixed) * ((type - 0.5) * 2.0);
}

inline double EvaluateTypedShapeProfile(double height, double cloudType)
{
    return SaturateFinite(
        EvaluateTypedVerticalProfile(height, cloudType) *
        EvaluateTypedFootprintScale(height, cloudType));
}

inline double EvaluateEffectiveShapeCoverage(double globalCoverage,
                                             double weatherCoverage,
                                             double typedFootprintScale)
{
    const double weatherFactor = 0.70 + 0.30 * SaturateFinite(weatherCoverage);
    const double footprintFactor = 0.80 +
        0.20 * SaturateFinite(typedFootprintScale);
    return SaturateFinite(SaturateFinite(globalCoverage) *
                          weatherFactor * footprintFactor);
}

inline double EvaluateWeatherSupport(double weatherCoverage)
{
    return Smoothstep(0.02, 0.20, SaturateFinite(weatherCoverage));
}

inline double RemapCoverage(double rawNoise, double coverage)
{
    const double safeCoverage = SaturateFinite(coverage);
    if (safeCoverage <= 1e-4)
        return 0.0;
    return SaturateFinite(
        (SaturateFinite(rawNoise) - (1.0 - safeCoverage)) / safeCoverage);
}

inline double EvaluatePhysicalBaseShape(double rawNoise,
                                        double globalCoverage,
                                        double weatherCoverage,
                                        double height,
                                        double cloudType)
{
    const double verticalProfile = EvaluateTypedVerticalProfile(
        height, cloudType);
    const double footprintScale = EvaluateTypedFootprintScale(
        height, cloudType);
    const double shapeCoverage = EvaluateEffectiveShapeCoverage(
        globalCoverage, weatherCoverage, footprintScale);
    return EvaluateWeatherSupport(weatherCoverage) *
        RemapCoverage(rawNoise, shapeCoverage) * verticalProfile;
}
}
