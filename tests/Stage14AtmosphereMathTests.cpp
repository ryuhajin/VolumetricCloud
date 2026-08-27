#include "AtmosphereParameters.h"
#include "GroundLightingParameters.h"
#include "Stage14AtmosphereMath.h"
#include "Stage14Parameters.h"
#include "ToneMappingParameters.h"

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

bool Near(float a, float b, float epsilon = 1.0e-4f)
{
    return std::abs(a - b) <= epsilon;
}
}

int main()
{
    using namespace stage14math;
    bool passed = true;

    const SphereInterval outside = IntersectSphere(
        { 0.0f, 0.0f, 7000.0f }, { 0.0f, 0.0f, -1.0f }, 6460.0f);
    passed &= Require(outside.hit && Near(outside.nearDistance, 540.0f, 0.01f),
                      "sphere intersection finds top atmosphere entry");
    const SphereInterval miss = IntersectSphere(
        { 0.0f, 0.0f, 7000.0f }, { 1.0f, 0.0f, 0.0f }, 6460.0f);
    passed &= Require(!miss.hit, "sphere intersection rejects a tangent miss");

    passed &= Require(Near(ExponentialDensity(0.0f, 8.0f), 1.0f) &&
                      Near(ExponentialDensity(8.0f, 8.0f), std::exp(-1.0f)),
                      "Rayleigh exponential density uses kilometer scale height");
    passed &= Require(Near(OzoneDensity(25.0f, 25.0f, 15.0f), 1.0f) &&
                      Near(OzoneDensity(10.0f, 25.0f, 15.0f), 0.0f),
                      "ozone triangular layer peaks at 25 km");

    passed &= Require(Near(RayleighPhase(1.0f), RayleighPhase(-1.0f)) &&
                      RayleighPhase(1.0f) > RayleighPhase(0.0f),
                      "Rayleigh phase is symmetric and stronger at the poles");
    passed &= Require(CornetteShanksMiePhase(0.8f, 1.0f) >
                      CornetteShanksMiePhase(0.8f, 0.0f),
                      "Mie phase has a forward lobe");

    const Float3 transmittance = BeerLambert({ 0.0f, 1.0f, 10.0f });
    passed &= Require(Near(transmittance.x, 1.0f) &&
                      Near(transmittance.y, std::exp(-1.0f)) &&
                      transmittance.z < transmittance.y,
                      "Beer-Lambert is finite and monotonically decreasing");
    const Float3 zenithClear = EarthClearTransmittanceAtUv({ 0.0f, 0.0f });
    const Float3 horizonClear = EarthClearTransmittanceAtUv({ 1.0f, 0.0f });
    passed &= Require(zenithClear.x > horizonClear.x &&
                      zenithClear.y > horizonClear.y &&
                      zenithClear.z > horizonClear.z,
                      "Earth Clear zenith transmittance exceeds horizon");

    const float testHeights[] = { 6360.0f, 6380.0f, 6459.0f };
    // Transmittance LUT는 지면을 먼저 맞히지 않고 대기 상단으로 나가는
    // 방향을 저장한다. 지표에서 음의 cosine은 이 정의역 밖이다.
    const float testCosines[] = { 0.0f, 0.25f, 0.95f };
    for (float height : testHeights)
    {
        for (float cosine : testCosines)
        {
            const Float2 uv = TransmittanceParamsToUv(
                6360.0f, 6460.0f, height, cosine);
            float roundTripHeight = 0.0f;
            float roundTripCosine = 0.0f;
            UvToTransmittanceParams(6360.0f, 6460.0f, uv,
                                    roundTripHeight, roundTripCosine);
            passed &= Require(Near(height, roundTripHeight, 0.02f) &&
                              Near(cosine, roundTripCosine, 2.0e-3f),
                              "Transmittance LUT mapping round-trips");
        }
    }

    const SunAngles dawn = TimeOfDayPath(5.5f);
    const SunAngles noon = TimeOfDayPath(12.5f);
    const SunAngles dusk = TimeOfDayPath(19.5f);
    passed &= Require(Near(dawn.azimuthDegrees, -60.0f) &&
                      Near(dawn.elevationDegrees, -6.0f) &&
                      Near(noon.elevationDegrees, 70.0f) &&
                      Near(dusk.azimuthDegrees, 120.0f) &&
                      Near(dusk.elevationDegrees, -6.0f),
                      "fixed time path matches the Stage 14 contract");
    passed &= Require(DirectionFromAngles(0.0f, -6.0f).y < 0.0f,
                      "sun direction supports civil twilight below horizon");

    passed &= Require(Near(AdvanceLoopingTimeOfDay(7.5f, 1.0f, 30.0f),
                           8.0f),
                      "30 simulated min/s advances half an hour");
    passed &= Require(Near(AdvanceLoopingTimeOfDay(19.25f, 1.0f, 30.0f),
                           5.75f),
                      "time playback always wraps 19:30 to 05:30");
    passed &= Require(Near(AdvanceLoopingTimeOfDay(7.5f, 1.0f, 0.0f),
                           8.0f),
                      "time playback clamps speed below 30 simulated min/s");
    passed &= Require(Near(AdvanceLoopingTimeOfDay(7.5f, 1.0f, 240.0f),
                           9.5f),
                      "time playback clamps speed above 120 simulated min/s");

    AtmosphereParameters playbackParameters;
    playbackParameters.timePlaybackMinutesPerSecond = 0.0f;
    playbackParameters.timePlaybackEnabled = true;
    playbackParameters.timeLoopEnabled = false;
    playbackParameters.sunControlMode = SunControlMode::Angles;
    playbackParameters = stage14atmosphere::Sanitize(playbackParameters);
    passed &= Require(Near(playbackParameters.timePlaybackMinutesPerSecond,
                           30.0f) &&
                      playbackParameters.timeLoopEnabled &&
                      playbackParameters.sunControlMode ==
                          SunControlMode::TimeOfDay,
                      "atmosphere sanitize enforces playback contract");
    playbackParameters.timePlaybackEnabled = false;
    playbackParameters.sunControlMode = SunControlMode::TimeOfDay;
    playbackParameters.timeOfDayHours = 17.0f;
    const SunAngles pausedPath = TimeOfDayPath(
        playbackParameters.timeOfDayHours);
    playbackParameters.sunAzimuthDegrees = pausedPath.azimuthDegrees;
    playbackParameters.sunElevationDegrees = pausedPath.elevationDegrees;
    playbackParameters = stage14atmosphere::Sanitize(playbackParameters);
    passed &= Require(playbackParameters.sunControlMode ==
                          SunControlMode::Angles &&
                      Near(playbackParameters.sunAzimuthDegrees,
                           pausedPath.azimuthDegrees) &&
                      Near(playbackParameters.sunElevationDegrees,
                           pausedPath.elevationDegrees),
                      "paused time hands its current direction to angles");

    GroundLightingParameters ground;
    stage14ground::ApplyPreset(ground, GroundMaterialPreset::Snow);
    passed &= Require(Near(ground.albedo.x, 0.80f) &&
                      Near(ground.albedo.y, 0.85f) &&
                      Near(ground.albedo.z, 0.90f),
                      "Snow ground preset uses the specified linear albedo");
    stage14ground::ApplyPreset(ground, GroundMaterialPreset::Grass);
    passed &= Require(ground.albedo.y > ground.albedo.x &&
                      ground.albedo.y > ground.albedo.z,
                      "Grass ground preset is green dominant");

    const Float3 neutral = BradfordWhiteBalance({ 0.4f, 0.5f, 0.6f }, 6500.0f);
    passed &= Require(Near(neutral.x, 0.4f, 2.0e-3f) &&
                      Near(neutral.y, 0.5f, 2.0e-3f) &&
                      Near(neutral.z, 0.6f, 2.0e-3f),
                      "6500 K Bradford adaptation is neutral");
    const Float3 mapped = AcesFitted({ 0.0f, 1.0f, 16.0f });
    passed &= Require(mapped.x == 0.0f && mapped.y > 0.0f &&
                      mapped.z >= mapped.y && mapped.z <= 1.0f,
                      "ACES fitted output is finite, ordered, and bounded");

    passed &= Require(sizeof(stage14::GpuParameters) == 224u,
                      "Stage14 b13 ABI remains 224 bytes");

    if (passed)
        std::cout << "Stage 14 atmosphere math tests passed\n";
    return passed ? 0 : 1;
}
