#include "Stage10UpsamplingMath.h"

#include <array>
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
    using namespace stage10upsampling;
    bool passed = true;
    passed &= Require(
        sizeof(kRuntimeResolutionCandidates) /
                sizeof(kRuntimeResolutionCandidates[0]) == 2u &&
            kRuntimeResolutionCandidates[0] == Stage10ResolutionPreset::Half &&
            kRuntimeResolutionCandidates[1] == Stage10ResolutionPreset::Full,
        "runtime resolution candidates are Half then Full");
    passed &= Require(
        sizeof(kRuntimeFilterCandidates) /
                sizeof(kRuntimeFilterCandidates[0]) == 3u &&
            kRuntimeFilterCandidates[0] == Stage10UpsampleFilter::Nearest &&
            kRuntimeFilterCandidates[1] == Stage10UpsampleFilter::Bilinear &&
            kRuntimeFilterCandidates[2] == Stage10UpsampleFilter::Joint4,
        "runtime filters exclude Joint9 and keep cost order");
    const Stage10UpsamplingParameters defaults{};
    passed &= Require(
        defaults.resolutionScale == 1.0f &&
            defaults.filterMode ==
                static_cast<std::uint32_t>(Stage10UpsampleFilter::Nearest),
        "stage 10 starts Full with Nearest candidate selected");
    passed &= Require(ScaledExtent(1920, kHalfScale) == 960 &&
                      ScaledExtent(1080, kHalfScale) == 540,
                      "half resolution extent");
    passed &= Require(ScaledExtent(1920, kTwoThirdsScale) == 1280 &&
                      ScaledExtent(1080, kTwoThirdsScale) == 720,
                      "two-thirds resolution extent");
    passed &= Require(ScaledExtent(1920, kThreeQuartersScale) == 1440 &&
                      ScaledExtent(1080, kThreeQuartersScale) == 810,
                      "three-quarters resolution extent");
    passed &= Require(ScaledExtent(1, kHalfScale) == 1 &&
                      ScaledExtent(0, kHalfScale) == 1,
                      "minimum one-pixel extent");
    passed &= Require(std::abs(PixelCenterUv(0, 4) - 0.125f) < 1.0e-6f &&
                      std::abs(PixelCenterUv(3, 4) - 0.875f) < 1.0e-6f,
                      "pixel-center UV mapping");
    passed &= Require(std::abs(OpacityWeightedDepth(600.0f, 0.3f, 5000.0f) -
                               2000.0f) < 1.0e-3f,
                      "opacity-weighted cloud depth");
    passed &= Require(OpacityWeightedDepth(0.0f, 0.0f, 4200.0f) == 4200.0f,
                      "empty ray stores scene limit");
    passed &= Require(RelativeDepthWeight(1000.0f, 1000.0f, 0.01f) > 0.999f &&
                      RelativeDepthWeight(1000.0f, 1100.0f, 0.01f) < 1.0e-4f,
                      "relative depth rejection");
    passed &= Require(TransmittanceWeight(0.5f, 0.5f, 0.1f) > 0.999f &&
                      TransmittanceWeight(0.5f, 0.9f, 0.1f) < 0.001f,
                      "transmittance rejection");
    const auto normalized = NormalizeWeight(1.5f, 3.0f, 1.0e-4f);
    const auto rejected = NormalizeWeight(1.0f, 1.0e-8f, 1.0e-4f);
    passed &= Require(normalized.second && std::abs(normalized.first - 0.5f) <
                      1.0e-6f && !rejected.second &&
                      std::isfinite(rejected.first),
                      "normalization and finite fallback");

    using HardReason = JointHardRejectionReason;
    passed &= Require(
        ClassifyJointHardTap(true, true, false, 0.5f, 0.5f, 1.0e-6f) ==
            HardReason::SceneClassMismatch &&
        ClassifyJointHardTap(true, false, true, 0.5f, 0.5f, 1.0e-6f) ==
            HardReason::SceneClassMismatch,
        "Joint4 must hard reject Geometry/Sky class crossings");
    passed &= Require(
        ClassifyJointHardTap(true, true, true, 0.5000004f, 0.5f,
                             1.0e-6f) == HardReason::Accepted &&
        ClassifyJointHardTap(true, true, true, 0.500004f, 0.5f,
                             1.0e-6f) == HardReason::GeometryPlaneMismatch,
        "Joint4 must preserve a matching geometry plane and reject a depth discontinuity");
    passed &= Require(
        ClassifyJointHardTap(true, false, false, NAN, NAN, -1.0f) ==
            HardReason::Accepted,
        "Sky/Sky must skip scene-plane rejection and use soft cloud guides");
    const std::array<HardReason, 4> mixedReasons = {
        HardReason::Accepted, HardReason::SceneClassMismatch,
        HardReason::Accepted, HardReason::GeometryPlaneMismatch };
    const std::array<HardReason, 4> rejectedReasons = {
        HardReason::SceneClassMismatch, HardReason::GeometryPlaneMismatch,
        HardReason::InvalidSource, HardReason::SceneClassMismatch };
    const std::array<HardReason, 4> acceptedReasons = {
        HardReason::Accepted, HardReason::Accepted,
        HardReason::Accepted, HardReason::Accepted };
    passed &= Require(CountJoint4HardAccepted(acceptedReasons) == 4u &&
                      CountJoint4HardAccepted(mixedReasons) == 2u &&
                      CountJoint4HardAccepted(rejectedReasons) == 0u,
                      "accepted tap count must report the exact hard-valid Joint4 taps");
    passed &= Require(
        SelectJointFallback(2u, 0.5f, 1.0e-4f) ==
            JointFallbackMode::WeightedResult &&
        SelectJointFallback(2u, 1.0e-8f, 1.0e-4f) ==
            JointFallbackMode::NearestHardValid &&
        SelectJointFallback(0u, 1.0f, 1.0e-4f) ==
            JointFallbackMode::Transparent,
        "Joint fallback must never cross a hard-rejected scene boundary");

    Stage10UpsamplingParameters invalid{};
    invalid.resolutionScale = NAN;
    invalid.filterMode = 99u;
    invalid.minimumWeight = -1.0f;
    const Stage10UpsamplingParameters sanitized = Sanitize(invalid);
    passed &= Require(sanitized.resolutionScale == 1.0f &&
                      sanitized.filterMode == 3u &&
                      sanitized.minimumWeight > 0.0f,
                      "parameter sanitization");
    return passed ? 0 : 1;
}
