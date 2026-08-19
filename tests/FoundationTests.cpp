#include "Camera.h"

#include <DirectXMath.h>

#include <cmath>
#include <cstdio>

using namespace DirectX;

namespace
{
bool NearlyEqual(float a, float b, float tolerance = 1e-4f)
{
    return std::abs(a - b) <= tolerance;
}

bool FiniteVector(FXMVECTOR value)
{
    XMFLOAT4 result;
    XMStoreFloat4(&result, value);
    return std::isfinite(result.x) && std::isfinite(result.y) &&
           std::isfinite(result.z) && std::isfinite(result.w);
}

int Fail(const char* message)
{
    std::fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}
}

int main()
{
    Camera camera;
    camera.SetAspect(16.0f / 9.0f);
    camera.SetOrbit(0.55f, 0.30f, 12.0f, { 0.0f, -0.2f, 0.0f });

    const XMMATRIX viewProj = camera.GetViewProj();
    const XMMATRIX invViewProj = camera.GetInvViewProj();
    const XMVECTOR target = XMVectorSet(0.0f, -0.2f, 0.0f, 1.0f);
    XMVECTOR ndc = XMVector3TransformCoord(target, viewProj);
    const XMVECTOR reconstructed = XMVector3TransformCoord(ndc, invViewProj);
    const XMVECTOR error = XMVectorAbs(XMVectorSubtract(reconstructed, target));
    XMFLOAT3 errorValue;
    XMStoreFloat3(&errorValue, error);
    if (errorValue.x > 1e-3f || errorValue.y > 1e-3f || errorValue.z > 1e-3f)
        return Fail("View-Projection inverse did not reconstruct the world point");

    const XMVECTOR farNdc = XMVectorSet(0.0f, 0.0f, 1.0f, 1.0f);
    const XMVECTOR farWorld = XMVector3TransformCoord(farNdc, invViewProj);
    const XMFLOAT3 cameraPositionValue = camera.GetPosition();
    const XMVECTOR cameraPosition = XMLoadFloat3(&cameraPositionValue);
    const XMVECTOR centerRay = XMVector3Normalize(
        XMVectorSubtract(farWorld, cameraPosition));
    const XMVECTOR expectedRay = XMVector3Normalize(
        XMVectorSubtract(target, cameraPosition));
    const float alignment = XMVectorGetX(XMVector3Dot(centerRay, expectedRay));
    if (!NearlyEqual(alignment, 1.0f, 1e-4f))
        return Fail("Center ray does not point at the orbit target");

    const XMVECTOR corners[] = {
        XMVectorSet(-1.0f, -1.0f, 1.0f, 1.0f),
        XMVectorSet( 1.0f, -1.0f, 1.0f, 1.0f),
        XMVectorSet(-1.0f,  1.0f, 1.0f, 1.0f),
        XMVectorSet( 1.0f,  1.0f, 1.0f, 1.0f),
    };
    for (FXMVECTOR corner : corners)
    {
        const XMVECTOR world = XMVector3TransformCoord(corner, invViewProj);
        const XMVECTOR ray = XMVector3Normalize(XMVectorSubtract(world, cameraPosition));
        if (!FiniteVector(ray) || !NearlyEqual(XMVectorGetX(XMVector3Length(ray)), 1.0f))
            return Fail("Corner ray is not finite and normalized");
    }

    std::puts("Foundation math tests passed");
    return 0;
}
