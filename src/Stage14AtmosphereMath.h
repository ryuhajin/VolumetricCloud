// ============================================================================
//  Stage14AtmosphereMath.h - 대기 LUT와 HDR 출력의 CPU 회귀 기준
// ============================================================================
#pragma once

#include <algorithm>
#include <cmath>

namespace stage14math
{
constexpr float kPi = 3.14159265358979323846f;

struct Float2 { float x = 0.0f; float y = 0.0f; };
struct Float3 { float x = 0.0f; float y = 0.0f; float z = 0.0f; };

inline Float3 operator+(Float3 a, Float3 b)
{
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}
inline Float3 operator-(Float3 a, Float3 b)
{
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}
inline Float3 operator*(Float3 a, float b)
{
    return { a.x * b, a.y * b, a.z * b };
}
inline Float3 operator*(Float3 a, Float3 b)
{
    return { a.x * b.x, a.y * b.y, a.z * b.z };
}
inline Float3 operator/(Float3 a, float b)
{
    return { a.x / b, a.y / b, a.z / b };
}
inline float Dot(Float3 a, Float3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline float Length(Float3 value)
{
    return std::sqrt(std::max(Dot(value, value), 0.0f));
}
inline Float3 Normalize(Float3 value)
{
    const float length = Length(value);
    return length > 1.0e-8f ? value / length : Float3{ 0.0f, 1.0f, 0.0f };
}

struct SphereInterval
{
    bool hit = false;
    float nearDistance = 0.0f;
    float farDistance = 0.0f;
};

inline SphereInterval IntersectSphere(Float3 origin, Float3 direction,
                                      float radius)
{
    const Float3 safeDirection = Normalize(direction);
    const float b = Dot(origin, safeDirection);
    const float c = Dot(origin, origin) - radius * radius;
    const float discriminant = b * b - c;
    if (!std::isfinite(discriminant) || discriminant < 0.0f || radius <= 0.0f)
        return {};
    const float root = std::sqrt(std::max(discriminant, 0.0f));
    SphereInterval result;
    result.nearDistance = -b - root;
    result.farDistance = -b + root;
    result.hit = result.farDistance >= 0.0f;
    return result;
}

inline float ExponentialDensity(float altitudeKm, float scaleHeightKm)
{
    const float safeHeight = std::max(scaleHeightKm, 1.0e-4f);
    return std::exp(-std::max(altitudeKm, 0.0f) / safeHeight);
}

inline float OzoneDensity(float altitudeKm, float centerKm, float halfWidthKm)
{
    const float safeHalfWidth = std::max(halfWidthKm, 1.0e-4f);
    return std::clamp(1.0f - std::abs(altitudeKm - centerKm) / safeHalfWidth,
                      0.0f, 1.0f);
}

inline float RayleighPhase(float cosine)
{
    const float mu = std::clamp(cosine, -1.0f, 1.0f);
    return 3.0f * (1.0f + mu * mu) / (16.0f * kPi);
}

inline float CornetteShanksMiePhase(float g, float cosine)
{
    const float safeG = std::clamp(g, 0.0f, 0.95f);
    const float mu = std::clamp(cosine, -1.0f, 1.0f);
    const float k = 3.0f * (1.0f - safeG * safeG) /
                    (8.0f * kPi * (2.0f + safeG * safeG));
    const float denominator = std::max(
        1.0f + safeG * safeG - 2.0f * safeG * mu, 1.0e-6f);
    return k * (1.0f + mu * mu) /
           (denominator * std::sqrt(denominator));
}

inline Float3 BeerLambert(Float3 opticalDepth)
{
    return {
        std::exp(-std::max(opticalDepth.x, 0.0f)),
        std::exp(-std::max(opticalDepth.y, 0.0f)),
        std::exp(-std::max(opticalDepth.z, 0.0f)),
    };
}

// Bruneton 2017/Hillaire 2020 Transmittance LUT 좌표화.
inline Float2 TransmittanceParamsToUv(float bottomRadius, float topRadius,
                                      float viewHeight, float viewZenithCosine)
{
    const float bottom = std::max(bottomRadius, 1.0f);
    const float top = std::max(topRadius, bottom + 1.0e-3f);
    const float height = std::clamp(viewHeight, bottom, top);
    const float mu = std::clamp(viewZenithCosine, -1.0f, 1.0f);
    const float h = std::sqrt(std::max(top * top - bottom * bottom, 0.0f));
    const float rho = std::sqrt(std::max(height * height - bottom * bottom, 0.0f));
    const float distance = -height * mu + std::sqrt(std::max(
        height * height * (mu * mu - 1.0f) + top * top, 0.0f));
    const float minimumDistance = top - height;
    const float maximumDistance = rho + h;
    const float denominator = maximumDistance - minimumDistance;
    return {
        denominator > 1.0e-6f
            ? std::clamp((distance - minimumDistance) / denominator, 0.0f, 1.0f)
            : 0.0f,
        h > 1.0e-6f ? std::clamp(rho / h, 0.0f, 1.0f) : 0.0f,
    };
}

inline void UvToTransmittanceParams(float bottomRadius, float topRadius,
                                    Float2 uv, float& viewHeight,
                                    float& viewZenithCosine)
{
    const float bottom = std::max(bottomRadius, 1.0f);
    const float top = std::max(topRadius, bottom + 1.0e-3f);
    const float h = std::sqrt(std::max(top * top - bottom * bottom, 0.0f));
    const float rho = h * std::clamp(uv.y, 0.0f, 1.0f);
    viewHeight = std::sqrt(rho * rho + bottom * bottom);
    const float minimumDistance = top - viewHeight;
    const float maximumDistance = rho + h;
    const float distance = minimumDistance + std::clamp(uv.x, 0.0f, 1.0f) *
                           (maximumDistance - minimumDistance);
    viewZenithCosine = distance <= 1.0e-6f
        ? 1.0f
        : (h * h - rho * rho - distance * distance) /
          (2.0f * viewHeight * distance);
    viewZenithCosine = std::clamp(viewZenithCosine, -1.0f, 1.0f);
}

// Earth Clear 기본값과 GPU CSTransmittance의 40개 midpoint 적분을 그대로
// 재현한다. LUT readback 회귀에서 HLSL 계수·단위·좌표 계약을 함께 검사한다.
inline Float3 EarthClearTransmittanceAtUv(Float2 uv)
{
    constexpr float bottom = 6360.0f;
    constexpr float top = 6460.0f;
    float height = bottom;
    float cosine = 1.0f;
    UvToTransmittanceParams(bottom, top, uv, height, cosine);
    const Float3 position = { 0.0f, height, 0.0f };
    const Float3 direction = {
        std::sqrt(std::clamp(1.0f - cosine * cosine, 0.0f, 1.0f)),
        cosine, 0.0f
    };
    const SphereInterval topHit = IntersectSphere(position, direction, top);
    const float distance = topHit.hit ? std::max(topHit.farDistance, 0.0f) : 0.0f;
    constexpr int stepCount = 40;
    const float stepLength = distance / static_cast<float>(stepCount);
    Float3 opticalDepth = {};
    for (int index = 0; index < stepCount; ++index)
    {
        const Float3 sample = position + direction *
            ((static_cast<float>(index) + 0.5f) * stepLength);
        const float altitude = std::max(Length(sample) - bottom, 0.0f);
        const float rayleigh = ExponentialDensity(altitude, 8.0f);
        const float mie = ExponentialDensity(altitude, 1.2f);
        const float ozone = OzoneDensity(altitude, 25.0f, 15.0f);
        opticalDepth.x += (0.005802f * rayleigh + 0.004440f * mie +
                           0.000650f * ozone) * stepLength;
        opticalDepth.y += (0.013558f * rayleigh + 0.004440f * mie +
                           0.001881f * ozone) * stepLength;
        opticalDepth.z += (0.033100f * rayleigh + 0.004440f * mie +
                           0.000085f * ozone) * stepLength;
    }
    return BeerLambert(opticalDepth);
}

struct SunAngles { float azimuthDegrees; float elevationDegrees; };

inline float AdvanceLoopingTimeOfDay(float hour, float deltaSeconds,
                                     float simulatedMinutesPerSecond)
{
    constexpr float kStartHour = 5.5f;
    constexpr float kEndHour = 19.5f;
    constexpr float kDurationHours = kEndHour - kStartHour;
    const float safeHour = std::clamp(
        std::isfinite(hour) ? hour : 7.5f, kStartHour, kEndHour);
    const float safeDelta = std::max(
        std::isfinite(deltaSeconds) ? deltaSeconds : 0.0f, 0.0f);
    const float safeSpeed = std::clamp(
        std::isfinite(simulatedMinutesPerSecond)
            ? simulatedMinutesPerSecond : 30.0f,
        30.0f, 120.0f);
    const float advanced = safeHour + safeDelta * safeSpeed / 60.0f;
    if (advanced <= kEndHour)
        return advanced;
    return kStartHour + std::fmod(advanced - kStartHour, kDurationHours);
}

inline SunAngles TimeOfDayPath(float hour)
{
    const float safeHour = std::clamp(hour, 5.5f, 19.5f);
    const float u = (safeHour - 5.5f) / 14.0f;
    return {
        -60.0f + 180.0f * u,
        -6.0f + 76.0f * std::sin(kPi * u),
    };
}

inline Float3 DirectionFromAngles(float azimuthDegrees,
                                  float elevationDegrees)
{
    const float azimuth = std::clamp(azimuthDegrees, -180.0f, 180.0f) *
                          (kPi / 180.0f);
    const float elevation = std::clamp(elevationDegrees, -6.0f, 90.0f) *
                            (kPi / 180.0f);
    const float horizontal = std::cos(elevation);
    return Normalize({ horizontal * std::cos(azimuth), std::sin(elevation),
                       horizontal * std::sin(azimuth) });
}

inline Float3 AcesFitted(Float3 color)
{
    auto channel = [](float value)
    {
        const float x = std::max(value, 0.0f);
        return std::clamp((x * (2.51f * x + 0.03f)) /
                          (x * (2.43f * x + 0.59f) + 0.14f), 0.0f, 1.0f);
    };
    return { channel(color.x), channel(color.y), channel(color.z) };
}

inline Float2 CorrelatedColorTemperatureXy(float kelvin)
{
    const double temperature = std::clamp(
        static_cast<double>(kelvin), 1667.0, 25000.0);
    double x = 0.0;
    if (temperature <= 4000.0)
        x = -0.2661239e9 / std::pow(temperature, 3.0) -
             0.2343580e6 / std::pow(temperature, 2.0) +
             0.8776956e3 / temperature + 0.179910;
    else
        x = -3.0258469e9 / std::pow(temperature, 3.0) +
             2.1070379e6 / std::pow(temperature, 2.0) +
             0.2226347e3 / temperature + 0.240390;
    double y = 0.0;
    if (temperature <= 2222.0)
        y = -1.1063814 * x * x * x - 1.34811020 * x * x +
             2.18555832 * x - 0.20219683;
    else if (temperature <= 4000.0)
        y = -0.9549476 * x * x * x - 1.37418593 * x * x +
             2.09137015 * x - 0.16748867;
    else
        y = 3.0817580 * x * x * x - 5.87338670 * x * x +
            3.75112997 * x - 0.37001483;
    return { static_cast<float>(x), static_cast<float>(y) };
}

inline Float3 XyToXyz(Float2 xy)
{
    const float safeY = std::max(xy.y, 1.0e-6f);
    return { xy.x / safeY, 1.0f, (1.0f - xy.x - xy.y) / safeY };
}

inline Float3 Mul3x3(const float matrix[9], Float3 value)
{
    return {
        matrix[0] * value.x + matrix[1] * value.y + matrix[2] * value.z,
        matrix[3] * value.x + matrix[4] * value.y + matrix[5] * value.z,
        matrix[6] * value.x + matrix[7] * value.y + matrix[8] * value.z,
    };
}

inline Float3 BradfordWhiteBalance(Float3 linearRgb, float kelvin)
{
    static constexpr float kRgbToXyz[9] = {
        0.4124564f, 0.3575761f, 0.1804375f,
        0.2126729f, 0.7151522f, 0.0721750f,
        0.0193339f, 0.1191920f, 0.9503041f,
    };
    static constexpr float kXyzToRgb[9] = {
         3.2404542f, -1.5371385f, -0.4985314f,
        -0.9692660f,  1.8760108f,  0.0415560f,
         0.0556434f, -0.2040259f,  1.0572252f,
    };
    static constexpr float kBradford[9] = {
         0.8951f,  0.2664f, -0.1614f,
        -0.7502f,  1.7135f,  0.0367f,
         0.0389f, -0.0685f,  1.0296f,
    };
    static constexpr float kInverseBradford[9] = {
         0.9869929f, -0.1470543f,  0.1599627f,
         0.4323053f,  0.5183603f,  0.0492912f,
        -0.0085287f,  0.0400428f,  0.9684867f,
    };
    const Float3 sourceWhite = XyToXyz(CorrelatedColorTemperatureXy(kelvin));
    const Float3 targetWhite = XyToXyz(CorrelatedColorTemperatureXy(6500.0f));
    const Float3 sourceCone = Mul3x3(kBradford, sourceWhite);
    const Float3 targetCone = Mul3x3(kBradford, targetWhite);
    Float3 xyz = Mul3x3(kRgbToXyz, linearRgb);
    Float3 cone = Mul3x3(kBradford, xyz);
    cone.x *= targetCone.x / std::max(sourceCone.x, 1.0e-6f);
    cone.y *= targetCone.y / std::max(sourceCone.y, 1.0e-6f);
    cone.z *= targetCone.z / std::max(sourceCone.z, 1.0e-6f);
    xyz = Mul3x3(kInverseBradford, cone);
    return Mul3x3(kXyzToRgb, xyz);
}
}
