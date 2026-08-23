#include "Stage11TemporalMath.h"

#include <DirectXMath.h>

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
    using namespace DirectX;
    using namespace stage11temporal;
    bool passed = true;

    XMFLOAT2 sum{};
    for (std::uint32_t index = 0; index < 4; ++index)
    {
        const XMFLOAT2 jitter = JitterForFrame(index);
        sum.x += jitter.x;
        sum.y += jitter.y;
        passed &= Require(jitter.x == kJitterPhases[index].x &&
                          jitter.y == kJitterPhases[index].y,
                          "four-phase jitter order");
    }
    passed &= Require(sum.x == 0.0f && sum.y == 0.0f &&
                      JitterForFrame(4).x == JitterForFrame(0).x,
                      "jitter mean zero and repeats");

    const XMFLOAT2 sourceExtent{ 960.0f, 540.0f };
    const XMFLOAT2 sourceTexel{ 317.0f, 129.0f };
    for (std::uint32_t index = 0; index < 4; ++index)
    {
        const XMFLOAT2 jitter = JitterForFrame(index);
        const XMFLOAT2 sampledUv{
            (sourceTexel.x + 0.5f + jitter.x) / sourceExtent.x,
            (sourceTexel.y + 0.5f + jitter.y) / sourceExtent.y,
        };
        const XMFLOAT2 reconstructed = ReconstructionSourcePosition(
            sampledUv, sourceExtent, jitter, true);
        passed &= Require(
            std::abs(reconstructed.x - sourceTexel.x) < 1.0e-4f &&
            std::abs(reconstructed.y - sourceTexel.y) < 1.0e-4f,
            "jittered sample returns to its low-resolution texel");
    }
    const XMFLOAT2 noJitterUv{ 0.25f, 0.75f };
    const XMFLOAT2 noJitterPosition = ReconstructionSourcePosition(
        noJitterUv, sourceExtent, { 0.25f, -0.25f }, false);
    passed &= Require(
        std::abs(noJitterPosition.x -
                 (noJitterUv.x * sourceExtent.x - 0.5f)) < 1.0e-6f &&
        std::abs(noJitterPosition.y -
                 (noJitterUv.y * sourceExtent.y - 0.5f)) < 1.0e-6f,
        "disabled jitter matches stage 10 source position");
    const XMFLOAT2 xPositive = ReconstructionSourcePosition(
        { 0.5f, 0.5f }, sourceExtent, { 0.25f, 0.0f }, true);
    const XMFLOAT2 xNegative = ReconstructionSourcePosition(
        { 0.5f, 0.5f }, sourceExtent, { -0.25f, 0.0f }, true);
    const XMFLOAT2 yPositive = ReconstructionSourcePosition(
        { 0.5f, 0.5f }, sourceExtent, { 0.0f, 0.25f }, true);
    passed &= Require(
        std::abs((xNegative.x - xPositive.x) - 0.5f) < 1.0e-6f &&
        std::abs(yPositive.x - (sourceExtent.x * 0.5f - 0.5f)) < 1.0e-6f &&
        std::abs(yPositive.y - (sourceExtent.y * 0.5f - 0.75f)) < 1.0e-6f,
        "jitter inverse sign and independent axes");
    passed &= Require(
        std::abs(kJitterPhases[0].x * 2.0f + 0.5f) < 1.0e-6f &&
        std::abs(kJitterPhases[1].x * 2.0f - 0.5f) < 1.0e-6f,
        "quarter low-resolution texel is half a full-resolution pixel");

    const XMINT2 lowExtent{ 960, 540 };
    const XMINT2 fullExtent{ 1920, 1080 };
    const XMINT2 lowTap{ 317, 129 };
    const XMINT2 expectedGuidePixels[] = {
        { 634, 258 }, { 635, 259 }, { 635, 258 }, { 634, 259 },
    };
    for (std::uint32_t phase = 0; phase < 4; ++phase)
    {
        const XMINT2 guidePixel = SourceGuidePixelForTap(
            lowTap, lowExtent, fullExtent, JitterForFrame(phase), true);
        passed &= Require(
            guidePixel.x == expectedGuidePixels[phase].x &&
            guidePixel.y == expectedGuidePixels[phase].y,
            "jittered low-resolution tap maps to its exact full guide pixel");
    }
    const XMINT2 borderGuide = SourceGuidePixelForTap(
        { 959, 539 }, lowExtent, fullExtent, { 0.25f, 0.25f }, true);
    passed &= Require(borderGuide.x == 1919 && borderGuide.y == 1079,
                      "source guide mapping clamps the full-resolution border");

    float selectedSlope = 0.0f;
    passed &= Require(SelectStableSlope(
        0.70000f, 0.69999f, true, 0.71000f, true, selectedSlope) &&
        std::abs(selectedSlope - 0.00001f) < 1.0e-7f,
        "surface guide selects the smaller one-sided slope at a depth edge");
    passed &= Require(SelectStableSlope(
        0.70000f, 1.0f, false, 0.70002f, true, selectedSlope) &&
        std::abs(selectedSlope - 0.00002f) < 1.0e-7f &&
        !SelectStableSlope(
            0.70000f, 1.0f, false, 1.0f, false, selectedSlope),
        "surface guide uses one valid side and reports a missing axis");

    ScreenDepthPlaneGuide planeGuide{};
    planeGuide.deviceDepth = 0.75f;
    planeGuide.slopeX = 1.0e-5f;
    planeGuide.slopeY = -2.0e-5f;
    planeGuide.slopeXValid = true;
    planeGuide.slopeYValid = true;
    const float affineSourceDepth = planeGuide.deviceDepth +
        planeGuide.slopeX * 3.0f + planeGuide.slopeY * -2.0f;
    passed &= Require(
        FastSourceDepthAccepted(1005.0f, 1000.0f) &&
        FastSourceDepthAccepted(1010.0f, 1000.0f) &&
        !FastSourceDepthAccepted(1010.01f, 1000.0f),
        "nearby geometry keeps the meter fast path before plane validation");
    passed &= Require(
        ValidateCurrentSourcePlane(
            1200.0f, 1500.0f, 60000.0f, true, true,
            planeGuide, 3, -2, affineSourceDepth) ==
            CurrentSourceRejectionReason::Valid &&
        ValidateCurrentSourcePlane(
            1200.0f, 1500.0f, 60000.0f, true, true,
            planeGuide, -3, 2, planeGuide.deviceDepth -
                planeGuide.slopeX * 3.0f + planeGuide.slopeY * 2.0f) ==
            CurrentSourceRejectionReason::Valid &&
        ValidateCurrentSourcePlane(
            1200.0f, 1500.0f, 60000.0f, true, true,
            planeGuide, 3, -2, affineSourceDepth + 1.0e-4f) ==
            CurrentSourceRejectionReason::GeometryDepthMismatch &&
        std::abs(PlaneDepthTolerance(3, -2) - 1.8e-6f) < 1.0e-10f,
        "affine device-depth plane accepts oblique surfaces and rejects a depth step");

    ScreenDepthPlaneGuide missingSlopeGuide = planeGuide;
    missingSlopeGuide.slopeYValid = false;
    passed &= Require(
        ValidateCurrentSourcePlane(
            1200.0f, 1500.0f, 60000.0f, true, false,
            planeGuide, 1, 0, 1.0f) ==
            CurrentSourceRejectionReason::SceneClassMismatch &&
        ValidateCurrentSourcePlane(
            1200.0f, 60000.0f, 60000.0f, false, false,
            planeGuide, 1, 1, 1.0f) ==
            CurrentSourceRejectionReason::Valid,
        "geometry and sky class remains a hard source gate");
    passed &= Require(
        ValidateCurrentSourcePlane(
            NAN, 1500.0f, 60000.0f, true, true,
            planeGuide, 0, 0, 0.75f) ==
            CurrentSourceRejectionReason::InvalidOrNoCandidate &&
        ValidateCurrentSourcePlane(
            1200.0f, INFINITY, 60000.0f, true, true,
            planeGuide, 0, 0, 0.75f) ==
            CurrentSourceRejectionReason::InvalidOrNoCandidate &&
        ValidateCurrentSourcePlane(
            1200.0f, 70000.0f, 60000.0f, true, true,
            planeGuide, 0, 0, 0.75f) ==
            CurrentSourceRejectionReason::InvalidOrNoCandidate &&
        ValidateCurrentSourcePlane(
            1200.0f, 1500.0f, 60000.0f, true, true,
            missingSlopeGuide, 0, 1, affineSourceDepth) ==
            CurrentSourceRejectionReason::InvalidOrNoCandidate,
        "non-finite source and missing plane axis are rejected");

    passed &= Require(
        NeighborhoodCloudDepthAccepted(
            1000.0f, 1200.0f, true, 0.5f, 0.6f, 1224.0f, 0.02f) &&
        !NeighborhoodCloudDepthAccepted(
            1000.0f, 1200.0f, true, 0.5f, 0.6f, 1224.01f, 0.02f) &&
        NeighborhoodCloudDepthAccepted(
            1000.0f, 1200.0f, true, 0.995f, 0.5f, 5000.0f, 0.02f) &&
        NeighborhoodCloudDepthAccepted(
            1000.0f, 1200.0f, true, 0.5f, 0.995f, 5000.0f, 0.02f),
        "history cloud depth uses current neighborhood and skips transparent samples");

    std::array<float, 4> normalizedWeights{};
    passed &= Require(NormalizeValidWeights<4>(
        { 0.4f, 0.3f, 0.2f, 0.1f }, { true, false, true, false },
        normalizedWeights) &&
        std::abs(normalizedWeights[0] - 2.0f / 3.0f) < 1.0e-6f &&
        std::abs(normalizedWeights[2] - 1.0f / 3.0f) < 1.0e-6f,
        "bilinear invalid taps are removed and valid weights renormalized");
    passed &= Require(!NormalizeValidWeights<4>(
        { 0.4f, 0.3f, 0.2f, 0.1f }, { false, false, false, false },
        normalizedWeights),
        "all invalid bilinear taps report no current");

    const std::array<XMFLOAT2, 4> candidatePositions = {{
        { 0.0f, 0.0f }, { 2.0f, 0.0f }, { 0.0f, 2.0f }, { 2.0f, 2.0f }
    }};
    passed &= Require(NearestValidCandidate<4>(
        { 1.0f, 0.0f }, candidatePositions,
        { true, true, false, false }) == 0,
        "nearest valid candidate tie keeps deterministic first traversal");
    passed &= Require(NearestValidCandidate<4>(
        { 1.8f, 1.8f }, candidatePositions,
        { false, false, false, true }) == 3,
        "nearest valid candidate skips invalid sources");
    const XMFLOAT2 jointDelta = JointSpatialDelta({ 12.0f, 8.0f },
                                                   { 11.75f, 8.25f });
    passed &= Require(std::abs(jointDelta.x - 0.25f) < 1.0e-6f &&
                      std::abs(jointDelta.y + 0.25f) < 1.0e-6f,
                      "joint spatial distance uses integer texel centers");
    passed &= Require(
        ResolveInvalidCurrent(true, false) ==
            InvalidCurrentResolution::UseCurrent &&
        ResolveInvalidCurrent(false, true) ==
            InvalidCurrentResolution::HoldHistory &&
        ResolveInvalidCurrent(false, false) ==
            InvalidCurrentResolution::Transparent,
        "invalid current holds accepted history or starts transparent");

    XMFLOAT4X4 identity{};
    XMStoreFloat4x4(&identity, XMMatrixIdentity());
    XMFLOAT2 uv{};
    passed &= Require(ReprojectWorldToUv(
        { 0.0f, 0.0f, 0.5f }, { 1.0f, 0.0f, 0.0f }, 0.0f, 0.016f,
        identity, uv) && std::abs(uv.x - 0.5f) < 1.0e-6f &&
        std::abs(uv.y - 0.5f) < 1.0e-6f,
        "identity reprojection");
    passed &= Require(ReprojectWorldToUv(
        { 0.2f, 0.0f, 0.5f }, { 1.0f, 0.0f, 0.0f }, 1.0f, 0.2f,
        identity, uv) && std::abs(uv.x - 0.5f) < 1.0e-6f,
        "wind inverse displacement");
    passed &= Require(!ReprojectWorldToUv(
        { NAN, 0.0f, 1.0f }, {}, 0.0f, 0.0f, identity, uv),
        "non-finite world rejected");

    passed &= Require(RelativeDifferenceAccepted(1000.0f, 1005.0f, 0.01f) &&
                      !RelativeDifferenceAccepted(1000.0f, 1020.0f, 0.01f),
                      "relative threshold acceptance");
    passed &= Require(NearHistoryWeight(500.0f, 1000.0f, 3000.0f) == 0.0f &&
                      NearHistoryWeight(4000.0f, 1000.0f, 3000.0f) == 1.0f,
                      "near cloud history fade");
    passed &= Require(ClipHistory(2.0f, 0.25f, 0.75f, 1.0f) == 0.75f &&
                      std::abs(Accumulate(0.0f, 1.0f, 0.9f) - 0.9f) < 1e-6f,
                      "neighborhood clip and EMA");

    Stage11TemporalParameters parameters{};
    parameters.historyWeight = NAN;
    parameters.deltaTimeSeconds = 10.0f;
    parameters.nearHistoryFadeEndMeters = 0.0f;
    parameters = Sanitize(parameters);
    passed &= Require(sizeof(parameters) == 144 &&
                      parameters.historyWeight == 0.90f &&
                      parameters.deltaTimeSeconds == 0.25f &&
                      parameters.nearHistoryFadeEndMeters >
                          parameters.nearHistoryFadeStartMeters,
                      "b11 ABI and sanitization");

    ApplyMode(parameters, Stage11TemporalMode::Off);
    passed &= Require(parameters.temporalEnabled == 0u &&
                      parameters.jitterEnabled == 0u,
                      "runtime default off");
    passed &= Require(ModeFromSnapshot(32u, 1u) == Stage11TemporalMode::Off &&
                      ModeFromSnapshot(33u, 1u) ==
                          Stage11TemporalMode::Stable4Phase,
                      "schema 32 restores temporal off and schema 33 decodes");

    HistoryState state{};
    CommitHistoryState(state, true);
    passed &= Require(state.valid && state.readIndex == 1u &&
                      state.accumulatedFrames == 1u,
                      "successful frame commits ping-pong history");
    const Stage11HistoryResetReason resetReasons[] = {
        Stage11HistoryResetReason::Resize,
        Stage11HistoryResetReason::ResolutionOrFilter,
        Stage11HistoryResetReason::TemporalToggle,
        Stage11HistoryResetReason::CameraCut,
        Stage11HistoryResetReason::ParametersChanged,
        Stage11HistoryResetReason::TimeDiscontinuity,
        Stage11HistoryResetReason::ShaderReload,
        Stage11HistoryResetReason::ResourcesRecreated,
        Stage11HistoryResetReason::Manual,
    };
    for (Stage11HistoryResetReason reason : resetReasons)
    {
        ResetHistoryState(state, reason);
        passed &= Require(!state.valid && state.readIndex == 0u &&
                          state.accumulatedFrames == 0u &&
                          state.resetReason == reason,
                          "history reset trigger clears state");
        CommitHistoryState(state, true);
    }
    return passed ? 0 : 1;
}
