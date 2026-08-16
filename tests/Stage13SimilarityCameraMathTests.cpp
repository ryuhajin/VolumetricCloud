#include "Camera.h"

#include <DirectXMath.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

namespace
{
constexpr int kWidth = 320;
constexpr int kHeight = 180;
constexpr double kYaw = 0.55;
constexpr double kPitch = 0.30;
constexpr double kFovY = 60.0 * 3.14159265358979323846 / 180.0;

struct D3
{
    double x;
    double y;
    double z;
};

D3 operator+(D3 a, D3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
D3 operator-(D3 a, D3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
D3 operator*(D3 value, double scale)
{
    return { value.x * scale, value.y * scale, value.z * scale };
}

double Dot(D3 a, D3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
D3 Cross(D3 a, D3 b)
{
    return { a.y * b.z - a.z * b.y,
             a.z * b.x - a.x * b.z,
             a.x * b.y - a.y * b.x };
}
D3 Normalize(D3 value)
{
    const double length = std::sqrt(std::max(Dot(value, value), 1e-300));
    return value * (1.0 / length);
}

D3 AnalyticRay(double scale, int x, int y)
{
    const D3 target{ 40.0 * scale, 0.5 * scale, 0.0 };
    const double distance = 12.0 * scale;
    const double cosPitch = std::cos(kPitch);
    const D3 eye{
        target.x + distance * cosPitch * std::sin(kYaw),
        target.y + distance * std::sin(kPitch),
        target.z + distance * cosPitch * std::cos(kYaw)
    };
    const D3 forward = Normalize(target - eye);
    const D3 right = Normalize(Cross({ 0.0, 1.0, 0.0 }, forward));
    const D3 up = Cross(forward, right);
    const double ndcX = ((static_cast<double>(x) + 0.5) / kWidth) * 2.0 - 1.0;
    const double ndcY = 1.0 - ((static_cast<double>(y) + 0.5) / kHeight) * 2.0;
    const double tanHalfFov = std::tan(kFovY * 0.5);
    const double aspect = static_cast<double>(kWidth) / kHeight;
    return Normalize(forward + right * (ndcX * tanHalfFov * aspect) +
                     up * (ndcY * tanHalfFov));
}

DirectX::XMFLOAT3 FloatTranslationFreeRay(
    const Camera& camera, int x, int y)
{
    using namespace DirectX;
    const float ndcX = ((static_cast<float>(x) + 0.5f) / kWidth) * 2.0f - 1.0f;
    const float ndcY = 1.0f - ((static_cast<float>(y) + 0.5f) / kHeight) * 2.0f;
    XMVECTOR viewH = XMVector4Transform(
        XMVectorSet(ndcX, ndcY, 1.0f, 1.0f), camera.GetInvProjection());
    const float w = XMVectorGetW(viewH);
    viewH = XMVectorScale(viewH, 1.0f / w);
    XMVECTOR viewDirection = XMVector3Normalize(viewH);
    viewDirection = XMVectorSetW(viewDirection, 0.0f);
    XMVECTOR direction = XMVector3Normalize(XMVector4Transform(
        viewDirection, camera.GetInvViewRotation()));
    XMFLOAT3 result;
    XMStoreFloat3(&result, direction);
    return result;
}

double AngularErrorDegrees(D3 expected, const DirectX::XMFLOAT3& actual)
{
    const D3 actualDouble = Normalize({ actual.x, actual.y, actual.z });
    const double sine = std::sqrt(std::max(
        Dot(Cross(expected, actualDouble), Cross(expected, actualDouble)), 0.0));
    const double cosine = std::clamp(Dot(expected, actualDouble), -1.0, 1.0);
    return std::atan2(sine, cosine) * 180.0 / 3.14159265358979323846;
}
}

int main()
{
    bool passed = true;
    for (float scale : { 1.0f, 10.0f, 100.0f, 1000.0f })
    {
        Camera camera;
        camera.SetAspect(static_cast<float>(kWidth) / kHeight);
        camera.SetClipPlanes(0.1f, 60000.0f);
        camera.SetOrbit(static_cast<float>(kYaw), static_cast<float>(kPitch),
                        12.0f * scale,
                        { 40.0f * scale, 0.5f * scale, 0.0f });

        double maximumError = 0.0;
        std::uint64_t collapsedPairs = 0;
        std::vector<DirectX::XMFLOAT3> previousRow(kWidth);
        for (int y = 0; y < kHeight; ++y)
        {
            DirectX::XMFLOAT3 previous{};
            for (int x = 0; x < kWidth; ++x)
            {
                const DirectX::XMFLOAT3 ray =
                    FloatTranslationFreeRay(camera, x, y);
                maximumError = std::max(
                    maximumError,
                    AngularErrorDegrees(AnalyticRay(scale, x, y), ray));
                const auto same = [](const DirectX::XMFLOAT3& a,
                                     const DirectX::XMFLOAT3& b)
                {
                    return a.x == b.x && a.y == b.y && a.z == b.z;
                };
                if (x > 0 && same(previous, ray))
                    ++collapsedPairs;
                if (y > 0 && same(previousRow[x], ray))
                    ++collapsedPairs;
                previous = ray;
                previousRow[x] = ray;
            }
        }

        const bool scalePassed =
            maximumError <= 0.01 && collapsedPairs == 0;
        std::cout << std::fixed << std::setprecision(8)
                  << "[GATE][" << static_cast<int>(scale)
                  << "x][CameraRayMath] MAX_DEG=" << maximumError
                  << " COLLAPSED_PAIRS=" << collapsedPairs << ' '
                  << (scalePassed ? "PASS" : "FAIL") << '\n';
        passed = passed && scalePassed;
    }
    return passed ? 0 : 1;
}
