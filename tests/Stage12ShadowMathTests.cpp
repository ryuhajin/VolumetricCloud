#include "Stage12ShadowMath.h"

#include <cmath>
#include <iostream>

namespace
{
bool Require(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}
}

int main()
{
    using namespace stage12shadow;
    bool passed = true;

    const LightBasis basis = BuildLightBasis({ 0.35f, 0.55f, -0.25f });
    passed &= Require(basis.valid &&
        std::abs(Length(basis.right) - 1.0f) < 1.0e-5f &&
        std::abs(Length(basis.up) - 1.0f) < 1.0e-5f &&
        std::abs(Length(basis.forward) - 1.0f) < 1.0e-5f &&
        std::abs(Dot(basis.right, basis.up)) < 1.0e-5f &&
        std::abs(Dot(basis.right, basis.forward)) < 1.0e-5f &&
        std::abs(Dot(basis.up, basis.forward)) < 1.0e-5f,
        "light basis is orthonormal");

    const DirectX::XMFLOAT3 center{ 200.0f, 4500.0f, -800.0f };
    const DirectX::XMFLOAT3 point{ 1200.0f, 3700.0f, 900.0f };
    const DirectX::XMFLOAT2 uv0 = CacheUv(point, center, basis, 24000.0f);
    const DirectX::XMFLOAT2 uv1 = CacheUv(
        Add(point, Scale(basis.forward, 12345.0f)), center, basis, 24000.0f);
    passed &= Require(std::abs(uv0.x - uv1.x) < 1.0e-5f &&
                      std::abs(uv0.y - uv1.y) < 1.0e-5f,
                      "UV is invariant along a sun ray");

    const DirectX::XMFLOAT3 snapped = SnappedCenter(
        center, basis, kNearWidthMeters, kBalancedResolution);
    const float texel = kNearWidthMeters / kBalancedResolution;
    passed &= Require(
        std::abs(Dot(snapped, basis.right) -
                 std::round(Dot(snapped, basis.right) / texel) * texel) < 1.0e-3f &&
        std::abs(Dot(snapped, basis.up) -
                 std::round(Dot(snapped, basis.up) / texel) * texel) < 1.0e-3f,
        "cache center is snapped in light space");

    passed &= Require(NearWeight(0.80f) == 1.0f &&
                      NearWeight(0.95f) == 0.0f &&
                      FarValidity(0.90f) == 1.0f &&
                      FarValidity(1.00f) == 0.0f,
                      "cascade blend and far fade boundaries");

    const SliceInterpolation bottom = HeightToSlices(0.0f, 1500.0f, 7500.0f, 80);
    const SliceInterpolation middle = HeightToSlices(4500.0f, 1500.0f, 7500.0f, 80);
    const SliceInterpolation top = HeightToSlices(9000.0f, 1500.0f, 7500.0f, 80);
    passed &= Require(bottom.lower == 0 && bottom.upper == 1 &&
                      middle.lower == 39 && middle.upper == 40 &&
                      std::abs(middle.fraction - 0.5f) < 1.0e-6f &&
                      top.lower == 79 && top.upper == 79,
                      "height maps to adjacent cache slices");

    passed &= Require(std::abs(Transmittance(1.0f) - std::exp(-1.0f)) < 1.0e-6f &&
                      std::abs(Transmittance(100.0f) - 0.0001f) < 1.0e-6f,
                      "Beer-Lambert and tau clamp");
    passed &= Require(std::abs(SunPathLength(6000.0f, 18.0f) - 19416.4f) < 1.0f &&
                      std::abs(SunPathLength(6000.0f, 70.0f) - 6385.1f) < 1.0f,
                      "18 and 70 degree sun path lengths");
    passed &= Require(std::abs(SurfaceFactor(0.0f, 0.35f, 1.0f) - 0.35f) < 1.0e-6f &&
                      SurfaceFactor(0.0f, 0.35f, 0.0f) == 1.0f,
                      "surface diagnostic composite contract");

    Stage12ShadowParameters parameters{};
    ApplyPreset(parameters, Stage12ShadowPreset::Fast256);
    parameters.surfaceShadowStrength = NAN;
    parameters.cacheDebugExposure = INFINITY;
    parameters.debugNearSlice = 999u;
    parameters.debugFarSlice = 999u;
    parameters = Sanitize(parameters);
    passed &= Require(sizeof(parameters) == 160 &&
                      parameters.nearResolution == 256 &&
                      parameters.farResolution == 256 &&
                      parameters.surfaceShadowStrength == 1.0f &&
                      parameters.cacheDebugExposure == 4.0f &&
                      parameters.debugNearSlice == 79u &&
                      parameters.debugFarSlice == 39u &&
                      CacheBytes(Stage12ShadowPreset::Fast256) == 30ull * 1024ull * 1024ull &&
                      CacheBytes(Stage12ShadowPreset::Balanced512) == 120ull * 1024ull * 1024ull,
                      "b12 ABI, preset sizes and sanitization");
    passed &= Require(ModeFromSnapshot(33u, 1u) ==
                          Stage12ShadowMode::DirectReference &&
                      ModeFromSnapshot(34u, 1u) == Stage12ShadowMode::DeepCache,
                      "schema 33 preserves Stage 11 direct lighting");

    const DirectX::XMFLOAT3 fullCenter = SnappedCenter(
        center, basis, kNearWidthMeters, kBalancedResolution);
    const DirectX::XMFLOAT3 halfCenter = SnappedCenter(
        center, basis, kNearWidthMeters, kBalancedResolution);
    passed &= Require(Length(Add(fullCenter, Scale(halfCenter, -1.0f))) < 1.0e-6f,
                      "Full and 50 percent use the same world cache");

    std::cout << (passed ? "Stage12 shadow math tests passed\n"
                         : "Stage12 shadow math tests failed\n");
    return passed ? 0 : 1;
}
