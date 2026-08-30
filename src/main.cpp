// ============================================================================
//  main.cpp  —  진입점 (WinMain)
// ----------------------------------------------------------------------------
//  창(Window) · 카메라(Camera) · 렌더러(Renderer)를 생성·연결하고,
//  메인 루프에서 진단 장면과 단계 8 환경광·다중 산란 구름 패스를 그린다.
// ============================================================================
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <objbase.h>

#include <cmath>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <filesystem>
#include <fstream>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "Window.h"
#include "Camera.h"
#include "Renderer.h"
#include "NoiseVolumeCache.h"
#include "Stage13CameraPresets.h"
#include "Stage13SimilarityDiagnostics.h"
#include "Stage13WeatherShapeMath.h"

namespace
{
bool WriteTextFile(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    return output.good();
}

bool ReadTextFile(const std::filesystem::path& path, std::string& text)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return false;
    text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return input.good() || input.eof();
}

void WriteDiagnosticLine(const std::string& line)
{
    const std::string output = line + "\n";
    const HANDLE standardOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    if (standardOutput && standardOutput != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        WriteFile(standardOutput, output.data(),
                  static_cast<DWORD>(output.size()), &written, nullptr);
    }
    OutputDebugStringA(output.c_str());
}

bool Native1080pSucceeded(Native1080pResult result)
{
    return result == Native1080pResult::Success ||
        result == Native1080pResult::AlreadyActive;
}

const char* Native1080pResultText(Native1080pResult result)
{
    switch (result)
    {
    case Native1080pResult::Success: return "Native 1920x1080 enabled";
    case Native1080pResult::AlreadyActive:
        return "Native 1920x1080 already active";
    case Native1080pResult::WindowUnavailable: return "Window unavailable";
    case Native1080pResult::MonitorTooSmall:
        return "Monitor is smaller than physical 1920x1080";
    case Native1080pResult::Win32Failure: return "Win32 transition failed";
    }
    return "Unknown Native 1080p result";
}

const char* DebugModeName(CloudDebugMode mode)
{
    switch (mode)
    {
    case CloudDebugMode::NoiseUvw: return "NoiseUvw";
    case CloudDebugMode::RawNoise: return "RawNoise";
    case CloudDebugMode::FinalDensity: return "FinalDensity";
    case CloudDebugMode::LightTransmittance: return "LightTransmittance";
    case CloudDebugMode::AccumulatedDirectLighting: return "AccumulatedDirectLighting";
    case CloudDebugMode::Composite: return "Composite";
    case CloudDebugMode::WeatherThicknessPotential: return "WeatherThicknessPotential";
    case CloudDebugMode::LocalThickness: return "LocalThickness";
    case CloudDebugMode::LocalHeightFraction: return "LocalHeightFraction";
    case CloudDebugMode::TypedShapeProfile: return "TypedShapeProfile";
    case CloudDebugMode::EffectiveShapeCoverage: return "EffectiveShapeCoverage";
    case CloudDebugMode::BaseSupportBeforeDensity: return "BaseSupportBeforeDensity";
    case CloudDebugMode::BaseDensity: return "BaseShapeBeforeDetail";
    case CloudDebugMode::ViewOpticalDepth: return "ViewOpticalDepth";
    case CloudDebugMode::AccumulatedSkyAmbient: return "AccumulatedSkyAmbient";
    case CloudDebugMode::AccumulatedGroundBounce: return "AccumulatedGroundBounce";
    case CloudDebugMode::AccumulatedMultipleScattering: return "AccumulatedMultipleScattering";
    case CloudDebugMode::DetailLodFactor: return "DetailLodFactor";
    case CloudDebugMode::SilverLiningContribution: return "SilverLiningContribution";
    case CloudDebugMode::ShapedSunVisibility: return "ShapedSunVisibility";
    case CloudDebugMode::AmbientVisibility: return "AmbientVisibility";
    case CloudDebugMode::LowResolutionGrid: return "LowResolutionGrid";
    case CloudDebugMode::UpsampleSceneRejection: return "UpsampleSceneRejection";
    case CloudDebugMode::UpsampleCloudDepthWeight: return "UpsampleCloudDepthWeight";
    case CloudDebugMode::UpsampleTransmittanceWeight: return "UpsampleTransmittanceWeight";
    case CloudDebugMode::TemporalJitterPhase: return "TemporalJitterPhase";
    case CloudDebugMode::TemporalReprojectionMotion: return "TemporalReprojectionMotion";
    case CloudDebugMode::TemporalHistoryValidity: return "TemporalHistoryValidity";
    case CloudDebugMode::TemporalHistoryWeight: return "TemporalHistoryWeight";
    case CloudDebugMode::TemporalCurrentHistoryDifference: return "TemporalCurrentHistoryDifference";
    case CloudDebugMode::TemporalCurrentSourceValidity: return "TemporalCurrentSourceValidity";
    case CloudDebugMode::Stage12NearOpticalDepth: return "Stage12NearCacheTexture";
    case CloudDebugMode::Stage12FarOpticalDepth: return "Stage12FarCacheTexture";
    case CloudDebugMode::Stage12CascadeSelection: return "Stage12CascadeSelection";
    case CloudDebugMode::Stage12SurfaceTransmittance: return "Stage12SurfaceTransmittance";
    case CloudDebugMode::Stage12DirectCacheError: return "Stage12DirectCacheError";
    case CloudDebugMode::Stage15ResolvedCloud: return "Stage15ResolvedCloud";
    default: return "Unknown";
    }
}

struct SimilarityModeSpec
{
    CloudDebugMode mode;
    stage13diagnostics::ComparisonKind kind;
    double mismatchThreshold;
    bool gated;
    bool densityMask;
};

bool PassSimilarityGate(CloudDebugMode mode,
                        const stage13diagnostics::MetricSummary& metric)
{
    switch (mode)
    {
    case CloudDebugMode::NoiseUvw:
        return metric.mae <= 0.0001 && metric.p99 <= 0.001;
    case CloudDebugMode::RawNoise:
        return metric.mae <= 0.002 && metric.p99 <= 0.01 &&
            metric.overThresholdRatio <= 0.001;
    case CloudDebugMode::FinalDensity:
        return metric.mae <= 0.003 &&
            metric.overThresholdRatio <= 0.0025 &&
            metric.densityMaskMismatchRatio <= 0.005 &&
            metric.largestComponentArea <= 32;
    default:
        return true;
    }
}

void PrintSimilarityMetric(const char* category, const char* cameraCondition,
                           float scale, CloudDebugMode mode,
                           const stage13diagnostics::MetricSummary& metric,
                           bool passed)
{
    std::ostringstream line;
    line << std::fixed << std::setprecision(8)
         << '[' << category << "]";
    if (cameraCondition && cameraCondition[0] != '\0')
        line << '[' << cameraCondition << ']';
    line << '[' << static_cast<int>(scale) << "x]"
         << '[' << DebugModeName(mode) << ']'
         << " MAE=" << metric.mae
         << " RMSE=" << metric.rmse
         << " P99=" << metric.p99
         << " MAX=" << metric.maximum
         << " OVER=" << metric.overThresholdRatio
         << " MASK_DIFF=" << metric.densityMaskMismatchRatio
         << " LARGEST_COMPONENT=" << metric.largestComponentArea
         << " BOUNDS=" << metric.largestComponentWidth << 'x'
         << metric.largestComponentHeight << ' ';
    if (std::string(category) == "REPORT")
        line << "REPORT";
    else
        line << (passed ? "PASS" : "FAIL");
    WriteDiagnosticLine(line.str());
}

int RunStage13SimilarityGpuTest(Renderer& renderer, Camera& camera)
{
    using stage13diagnostics::ComparisonKind;
    const SimilarityModeSpec modes[] = {
        { CloudDebugMode::NoiseUvw, ComparisonKind::CircularRgb, 0.05, true, false },
        { CloudDebugMode::RawNoise, ComparisonKind::Scalar, 0.05, true, false },
        { CloudDebugMode::FinalDensity, ComparisonKind::Scalar, 0.05, true, true },
        { CloudDebugMode::LightTransmittance, ComparisonKind::Scalar, 0.05, false, false },
        { CloudDebugMode::AccumulatedDirectLighting, ComparisonKind::Rgb, 0.05, false, false },
        { CloudDebugMode::Composite, ComparisonKind::Rgb, 0.05, false, false },
    };
    struct CameraCondition
    {
        const char* name;
        bool scaledClip;
        bool originCentered;
        bool gate;
    };
    const CameraCondition conditions[] = {
        { "Current", false, false, true },
        { "ScaledClip", true, false, false },
        { "Origin", true, true, false },
    };

    bool allGatesPassed = true;
    bool fixedNoisePassed = true;
    bool fixedDensityPassed = true;
    bool lightingReportGoalPassed = true;

    renderer.SetNoiseLabVisible(false);
    renderer.EnableNoiseLabPreviews(false);
    renderer.SetAutomatedRenderMode(true);
    renderer.SetVSyncEnabled(false);
    renderer.SetCloudDomainType(CloudDomainType::PlanarLayer);

    for (std::size_t conditionIndex = 0;
         conditionIndex < std::size(conditions); ++conditionIndex)
    {
        const CameraCondition& condition = conditions[conditionIndex];
        std::map<CloudDebugMode, CloudDiagnosticFrame> baseline;
        CloudDiagnosticFrame baselineHit;

        for (float scale : { 1.0f, 10.0f, 100.0f, 1000.0f })
        {
            if (!renderer.ApplyStage13SimilarityScale(scale))
                return 2;
            renderer.SetCloudDomainType(CloudDomainType::PlanarLayer);
            // 불투명 진단 장면은 meter 크기가 고정되어 배율 비교 대상이 아니다.
            // Scene Depth 회귀는 Stage13DomainSmoke가 담당하고 여기서는 구름만 비교한다.
            renderer.SetOpaqueSceneForTest(false);
            camera.SetClipPlanes(condition.scaledClip ? 0.1f * scale : 0.1f,
                                 condition.scaledClip ? 60.0f * scale : 60000.0f);
            const DirectX::XMFLOAT3 target = condition.originCentered
                ? DirectX::XMFLOAT3{ 0.0f, 0.5f * scale, 0.0f }
                : DirectX::XMFLOAT3{ 40.0f * scale, 0.5f * scale, 0.0f };
            camera.SetOrbit(0.55f, 0.30f, 12.0f * scale, target);

            CloudDiagnosticFrame hit;
            if (!renderer.CaptureCloudDiagnosticFrame(
                    camera, 0.0f, CloudDebugMode::CloudHitMask, hit))
            {
                WriteDiagnosticLine("[ERROR] CloudHitMask float readback failed");
                return 3;
            }

            std::map<CloudDebugMode, CloudDiagnosticFrame> frames;
            for (const SimilarityModeSpec& spec : modes)
            {
                if (!renderer.CaptureCloudDiagnosticFrame(
                        camera, 0.0f, spec.mode, frames[spec.mode]))
                {
                    WriteDiagnosticLine(std::string("[ERROR] float readback failed: ") +
                                        DebugModeName(spec.mode));
                    return 4;
                }
            }

            if (scale == 1.0f)
            {
                baseline = std::move(frames);
                baselineHit = std::move(hit);
                continue;
            }

            for (const SimilarityModeSpec& spec : modes)
            {
                const bool composite = spec.mode == CloudDebugMode::Composite;
                const auto metric = stage13diagnostics::CompareFrames(
                    baseline.at(spec.mode), frames.at(spec.mode),
                    composite ? nullptr : &baselineHit,
                    composite ? nullptr : &hit,
                    spec.kind, spec.mismatchThreshold, spec.densityMask);
                const bool metricPassed = PassSimilarityGate(spec.mode, metric);
                const bool gateThisMetric = condition.gate && spec.gated;
                PrintSimilarityMetric(
                    gateThisMetric ? "GATE" : "REPORT", condition.name,
                    scale, spec.mode, metric,
                    gateThisMetric ? metricPassed : true);

                if (condition.gate && spec.mode == CloudDebugMode::NoiseUvw)
                    fixedNoisePassed = fixedNoisePassed && metricPassed;
                if (condition.gate && spec.mode == CloudDebugMode::RawNoise)
                    fixedNoisePassed = fixedNoisePassed && metricPassed;
                if (condition.gate && spec.mode == CloudDebugMode::FinalDensity)
                    fixedDensityPassed = fixedDensityPassed && metricPassed;
                if (condition.gate && scale == 1000.0f &&
                    (spec.mode == CloudDebugMode::AccumulatedDirectLighting ||
                     spec.mode == CloudDebugMode::Composite))
                    lightingReportGoalPassed =
                        lightingReportGoalPassed && metric.mae <= 0.01;
                if (gateThisMetric)
                    allGatesPassed = allGatesPassed && metricPassed;
            }
        }
    }

    if (!fixedNoisePassed)
        WriteDiagnosticLine("[CAUSE] Noise failed: NOISE_COORDINATE_PRECISION");
    else if (!fixedDensityPassed)
        WriteDiagnosticLine("[CAUSE] Noise passed but Density failed: DENSITY_STAGE");
    else if (lightingReportGoalPassed)
        WriteDiagnosticLine("[CAUSE] Noise/Density gates passed; 1000x lighting/composite report goals met");
    else
        WriteDiagnosticLine("[CAUSE] Noise/Density gates passed; 1000x lighting/composite report goals exceeded");

    return allGatesPassed && !renderer.HasDebugLayerErrors() ? 0 : 1;
}

std::uint64_t HashDiagnosticFrame(const CloudDiagnosticFrame& frame);

int RunStage13OpenWorldSmokeTest(Renderer& renderer, Camera& camera)
{
    renderer.SetNoiseLabVisible(false);
    renderer.EnableNoiseLabPreviews(false);
    renderer.SetVSyncEnabled(false);
    renderer.SetOpaqueSceneForTest(false);

    if (!renderer.ApplyStage13SimilarityScale(100.0f) ||
        !renderer.ApplyStage13OpenWorldPreset())
        return 2;
    const CloudParameters firstCloud = renderer.CloudSettings();
    const CloudShapeParameters firstShape = renderer.ShapeSettings();
    const CloudDomainParameters firstDomain = renderer.DomainSettings();
    const NoiseVolumeParameters firstNoise = renderer.NoiseVolumeSettings();
    const LightParameters firstLight = renderer.LightSettings();
    const std::uint64_t firstWeatherHash = renderer.WeatherMapHash();

    WeatherMapGeneratorSettings changedWeatherSettings;
    changedWeatherSettings.coverage.seed = 9001u;
    changedWeatherSettings.cloudType.macroPeriod = 7u;
    changedWeatherSettings.density.detailWeight = 0.48f;
    if (!renderer.ApplyStage13SimilarityScale(10.0f) ||
        !renderer.ApplyWeatherGeneratorSettings(changedWeatherSettings) ||
        !renderer.ApplyStage13OpenWorldPreset() ||
        std::memcmp(&firstCloud, &renderer.CloudSettings(), sizeof(firstCloud)) != 0 ||
        std::memcmp(&firstShape, &renderer.ShapeSettings(), sizeof(firstShape)) != 0 ||
        std::memcmp(&firstDomain, &renderer.DomainSettings(), sizeof(firstDomain)) != 0 ||
        std::memcmp(&firstNoise, &renderer.NoiseVolumeSettings(), sizeof(firstNoise)) != 0 ||
        std::memcmp(&firstLight, &renderer.LightSettings(), sizeof(firstLight)) != 0 ||
        firstWeatherHash != renderer.WeatherMapHash() ||
        renderer.WeatherPreset() != Stage5WeatherPreset::PeriodicPerlin)
        return 3;

    const OpenWorldPipelinePreset pipelinePresets[] = {
        OpenWorldPipelinePreset::Legacy1000Baseline,
        OpenWorldPipelinePreset::Texture3D,
        OpenWorldPipelinePreset::PeriodicWeather,
        OpenWorldPipelinePreset::PhysicalShape,
        OpenWorldPipelinePreset::FullOpenWorld,
    };
    const CloudDebugMode pipelineModes[] = {
        CloudDebugMode::Composite,
        CloudDebugMode::WeatherCoverage,
        CloudDebugMode::CloudType,
        CloudDebugMode::LocalThickness,
        CloudDebugMode::BaseDensity,
        CloudDebugMode::FinalDensity,
    };
    const CloudDomainParameters comparisonDomain = renderer.DomainSettings();
    camera.SetClipPlanes(
        stage13camera::kNearPlaneMeters, stage13camera::kFarPlaneMeters);
    camera.SetLookAt(
        stage13camera::Get(Stage13CameraPresetId::GroundHorizon).position,
        stage13camera::Get(Stage13CameraPresetId::GroundHorizon).target);
    std::set<std::uint64_t> pipelineHashes;
    for (OpenWorldPipelinePreset preset : pipelinePresets)
    {
        if (!renderer.ApplyOpenWorldPipelinePreset(preset) ||
            std::memcmp(&comparisonDomain, &renderer.DomainSettings(),
                        sizeof(comparisonDomain)) != 0 ||
            renderer.PipelinePreset() != preset)
            return 9;
        if (preset >= OpenWorldPipelinePreset::Texture3D)
        {
            const NoiseVolumeParameters& noise = renderer.NoiseVolumeSettings();
            if (noise.baseWorldSizeMeters != 12000.0f ||
                noise.baseVerticalWorldSizeMeters != 12000.0f ||
                noise.baseFrequencies.x != 4u || noise.baseFrequencies.y != 9u ||
                noise.baseFrequencies.z != 17u || noise.baseFrequencies.w != 23u)
                return 9;
        }
        if (preset >= OpenWorldPipelinePreset::PeriodicWeather &&
            renderer.WeatherMapHash() != firstWeatherHash)
            return 9;
        for (CloudDebugMode mode : pipelineModes)
        {
            CloudDiagnosticFrame frame;
            if (!renderer.CaptureCloudDiagnosticFrame(
                    camera, 0.0f, mode, frame) || frame.pixels.empty())
                return 10;
            for (const DirectX::XMFLOAT4& pixel : frame.pixels)
                if (!std::isfinite(pixel.x) || !std::isfinite(pixel.y) ||
                    !std::isfinite(pixel.z) || !std::isfinite(pixel.w))
                    return 11;
            pipelineHashes.insert(HashDiagnosticFrame(frame));
        }
    }
    if (pipelineHashes.size() < 5u ||
        renderer.PipelinePreset() !=
            OpenWorldPipelinePreset::FullOpenWorld)
        return 12;
    WriteDiagnosticLine(
        "[OPEN-WORLD][PIPELINE] STEPS=5 OUTPUTS=6 DOMAIN_PRESERVED=1 PASS");

    const stage13openworld::Parameters value;
    {
        std::ostringstream line;
        line << std::fixed << std::setprecision(3)
             << "[OPEN-WORLD][SAMPLING] BASE_SPW="
             << stage13openworld::SamplesPerWavelength(
                    value.baseNoiseCyclesPerMeter, value.viewStepMeters)
             << " DETAIL_SPW="
             << stage13openworld::SamplesPerWavelength(
                    value.detailNoiseCyclesPerMeter, value.viewStepMeters)
             << " VIEW=" << stage13openworld::RequiredSteps(
                    value.maxViewTraceMeters, value.viewStepMeters)
             << '/' << value.maxViewSteps
             << " LIGHT=" << stage13openworld::RequiredSteps(
                    value.maxLightTraceMeters, value.lightStepMeters)
             << '/' << value.maxLightSteps << " PASS";
        WriteDiagnosticLine(line.str());
    }
    {
        std::ostringstream line;
        line << std::fixed << std::setprecision(6)
             << "[OPEN-WORLD][OPTICS] EXTINCTION="
             << value.extinctionPerMeter
             << " TAU_VERTICAL=" << std::setprecision(3)
             << stage13openworld::VerticalOpticalDepth(value)
             << " ALBEDO=" << value.singleScatteringAlbedo << " PASS";
        WriteDiagnosticLine(line.str());
    }
    {
        std::ostringstream line;
        line << std::fixed << std::setprecision(0)
             << "[OPEN-WORLD][WEATHER] SIZE_M=" << value.weatherWorldSizeMeters
             << " TEXEL_M=" << stage13openworld::WeatherTexelMeters(value)
             << " PRESET=PERIODIC_PERLIN PASS";
        WriteDiagnosticLine(line.str());
    }
    {
        const CloudShapeParameters& shape = renderer.ShapeSettings();
        std::ostringstream line;
        line << std::fixed << std::setprecision(0)
             << "[OPEN-WORLD][SHAPE] BOTTOM_M=" << value.layerBottomMeters
             << " LAYER_M=" << value.layerThicknessMeters
             << " STRATUS_M=" << shape.stratusMinimumThicknessMeters << '-'
             << shape.stratusMaximumThicknessMeters
             << " CUMULUS_M=" << shape.cumulusMinimumThicknessMeters << '-'
             << shape.cumulusMaximumThicknessMeters << " PASS";
        WriteDiagnosticLine(line.str());
    }

    const CloudDebugMode modes[] = {
        CloudDebugMode::CloudHitMask,
        CloudDebugMode::CloudSegmentLength,
        CloudDebugMode::ActualViewStepLength,
        CloudDebugMode::Transmittance,
        CloudDebugMode::Composite,
    };

    std::set<std::uint64_t> compositeHashes;
    for (const Stage13CameraPreset& preset : stage13camera::kOpenWorldPresets)
    {
        camera.SetClipPlanes(
            stage13camera::kNearPlaneMeters, stage13camera::kFarPlaneMeters);
        camera.SetLookAt(preset.position, preset.target);
        std::set<std::uint64_t> cameraHashes;
        for (CloudDebugMode mode : modes)
        {
            CloudDiagnosticFrame frame;
            if (!renderer.CaptureCloudDiagnosticFrame(camera, 0.0f, mode, frame) ||
                frame.pixels.empty())
                return 4;
            std::uint64_t hash = 1469598103934665603ull;
            for (const DirectX::XMFLOAT4& pixel : frame.pixels)
            {
                if (!std::isfinite(pixel.x) || !std::isfinite(pixel.y) ||
                    !std::isfinite(pixel.z) || !std::isfinite(pixel.w))
                    return 5;
                const auto* bytes = reinterpret_cast<const unsigned char*>(&pixel);
                for (std::size_t index = 0; index < sizeof(pixel); ++index)
                {
                    hash ^= bytes[index];
                    hash *= 1099511628211ull;
                }
            }
            cameraHashes.insert(hash);
            if (mode == CloudDebugMode::Composite)
                compositeHashes.insert(hash);
        }
        if (cameraHashes.size() < 3u)
            return 6;
        WriteDiagnosticLine(std::string("[OPEN-WORLD][CAMERA][") +
                            preset.diagnosticName + "] FINITE PASS");
    }
    if (compositeHashes.size() != stage13camera::kOpenWorldPresets.size())
        return 7;
    return renderer.HasDebugLayerErrors() ? 8 : 0;
}

double ScalarSsim(const CloudDiagnosticFrame& reference,
                  const CloudDiagnosticFrame& current)
{
    if (reference.pixels.empty() ||
        reference.pixels.size() != current.pixels.size())
        return 0.0;
    double referenceMean = 0.0;
    double currentMean = 0.0;
    for (std::size_t index = 0; index < reference.pixels.size(); ++index)
    {
        referenceMean += reference.pixels[index].x;
        currentMean += current.pixels[index].x;
    }
    referenceMean /= reference.pixels.size();
    currentMean /= current.pixels.size();
    double referenceVariance = 0.0;
    double currentVariance = 0.0;
    double covariance = 0.0;
    for (std::size_t index = 0; index < reference.pixels.size(); ++index)
    {
        const double referenceDelta =
            reference.pixels[index].x - referenceMean;
        const double currentDelta = current.pixels[index].x - currentMean;
        referenceVariance += referenceDelta * referenceDelta;
        currentVariance += currentDelta * currentDelta;
        covariance += referenceDelta * currentDelta;
    }
    const double denominator = std::max<std::size_t>(
        reference.pixels.size() - 1u, 1u);
    referenceVariance /= denominator;
    currentVariance /= denominator;
    covariance /= denominator;
    constexpr double c1 = 0.01 * 0.01;
    constexpr double c2 = 0.03 * 0.03;
    return ((2.0 * referenceMean * currentMean + c1) *
            (2.0 * covariance + c2)) /
        ((referenceMean * referenceMean + currentMean * currentMean + c1) *
         (referenceVariance + currentVariance + c2));
}

int RunStage9OptimizationSmokeTest(Renderer& renderer, Camera& camera)
{
    using stage13diagnostics::ComparisonKind;
    struct QualityResult
    {
        std::string appearance;
        std::string preset;
        double densitySsim = 0.0;
        double densityRmse = 0.0;
        double viewSsim = 0.0;
        double viewRmse = 0.0;
        double lightMae = 0.0;
        double lightP99 = 0.0;
        OptimizationParameters parameters;
        float earlyExitThreshold = 0.0f;
        bool qualityPassed = false;
    };
    std::vector<QualityResult> qualityResults;
    renderer.SetNoiseLabVisible(false);
    renderer.EnableNoiseLabPreviews(false);
    renderer.SetVSyncEnabled(false);
    renderer.SetOpaqueSceneForTest(false);
    if (!renderer.ApplyStage13OpenWorldPreset() ||
        renderer.OptimizationPreset() != Stage9OptimizationPreset::Balanced ||
        !renderer.ApplyCloudAppearancePreset(CloudAppearancePreset::Stratus) ||
        renderer.AppearancePreset() != CloudAppearancePreset::Stratus ||
        !renderer.ApplyCloudAppearancePreset(
            CloudAppearancePreset::DenseMixedDefault) ||
        renderer.AppearancePreset() != CloudAppearancePreset::DenseMixedDefault ||
        !renderer.ApplyCloudAppearancePreset(CloudAppearancePreset::Stratus) ||
        renderer.AppearancePreset() != CloudAppearancePreset::Stratus)
        return 2;

    camera.SetClipPlanes(
        stage13camera::kNearPlaneMeters, stage13camera::kFarPlaneMeters);
    const Stage13CameraPreset& horizon = stage13camera::Get(
        Stage13CameraPresetId::GroundHorizon);
    camera.SetLookAt(horizon.position, horizon.target);

    const struct AppearanceCase
    {
        const char* name;
        CloudAppearancePreset preset;
    } appearances[] = {
        { "Dense", CloudAppearancePreset::DenseMixedDefault },
        { "Stratus", CloudAppearancePreset::Stratus },
        { "Cumulus", CloudAppearancePreset::Cumulus },
    };
    const struct Candidate
    {
        const char* name;
        Stage9OptimizationPreset preset;
        std::uint32_t coneOverride;
        float angleOverride;
        float farFractionOverride;
    } candidates[] = {
        { "Balanced", Stage9OptimizationPreset::Balanced, 0u, 0.0f, 0.0f },
        { "BalancedCone8", Stage9OptimizationPreset::Balanced, 8u, 3.0f, 0.85f },
        { "BalancedCone12", Stage9OptimizationPreset::Balanced, 12u, 3.0f, 0.85f },
        { "Conservative", Stage9OptimizationPreset::Conservative, 0u, 0.0f, 0.0f },
    };

    bool finitePassed = true;
    CloudDiagnosticFrame denseReferenceDensity;
    CloudDiagnosticFrame stratusReferenceDensity;
    for (const AppearanceCase& appearance : appearances)
    {
        if (!renderer.ApplyCloudAppearancePreset(appearance.preset))
            return 3;
        renderer.ApplyStage9OptimizationPreset(
            Stage9OptimizationPreset::ApprovedReference);
        CloudDiagnosticFrame referenceDensity;
        CloudDiagnosticFrame referenceViewDepth;
        if (!renderer.CaptureCloudDiagnosticFrame(
                camera, 0.0f, CloudDebugMode::FinalDensity,
                referenceDensity) ||
            !renderer.CaptureCloudDiagnosticFrame(
                camera, 0.0f, CloudDebugMode::ViewOpticalDepth,
                referenceViewDepth))
            return 4;
        if (appearance.preset == CloudAppearancePreset::DenseMixedDefault)
            denseReferenceDensity = referenceDensity;
        else if (appearance.preset == CloudAppearancePreset::Stratus)
            stratusReferenceDensity = referenceDensity;
        renderer.ApplyStage9OptimizationPreset(
            Stage9OptimizationPreset::FineReference);
        CloudDiagnosticFrame referenceLight;
        if (!renderer.CaptureCloudDiagnosticFrame(
                camera, 0.0f, CloudDebugMode::LightTransmittance,
                referenceLight))
            return 4;

        for (const Candidate& candidate : candidates)
        {
            renderer.ApplyStage9OptimizationPreset(candidate.preset);
            if (candidate.coneOverride != 0u)
                renderer.ConfigureStage9ConeForValidation(
                    candidate.coneOverride, candidate.angleOverride,
                    candidate.farFractionOverride);
            CloudDiagnosticFrame density;
            CloudDiagnosticFrame viewDepth;
            CloudDiagnosticFrame light;
            if (!renderer.CaptureCloudDiagnosticFrame(
                    camera, 0.0f, CloudDebugMode::FinalDensity, density) ||
                !renderer.CaptureCloudDiagnosticFrame(
                    camera, 0.0f, CloudDebugMode::ViewOpticalDepth,
                    viewDepth) ||
                !renderer.CaptureCloudDiagnosticFrame(
                    camera, 0.0f, CloudDebugMode::LightTransmittance, light))
                return 5;
            for (const CloudDiagnosticFrame* frame : { &density, &viewDepth, &light })
                for (const DirectX::XMFLOAT4& pixel : frame->pixels)
                    finitePassed = finitePassed && std::isfinite(pixel.x) &&
                        std::isfinite(pixel.y) && std::isfinite(pixel.z) &&
                        std::isfinite(pixel.w);

            const auto densityMetric = stage13diagnostics::CompareFrames(
                referenceDensity, density, nullptr, nullptr,
                ComparisonKind::Scalar, 0.01, true);
            const auto viewMetric = stage13diagnostics::CompareFrames(
                referenceViewDepth, viewDepth, nullptr, nullptr,
                ComparisonKind::Scalar, 0.01, false);
            const auto lightMetric = stage13diagnostics::CompareFrames(
                referenceLight, light, nullptr, nullptr,
                ComparisonKind::Scalar, 0.03, false);
            const double densitySsim = ScalarSsim(referenceDensity, density);
            const double viewSsim = ScalarSsim(referenceViewDepth, viewDepth);
            const bool qualityCandidate = densitySsim >= 0.99 &&
                viewSsim >= 0.99 && densityMetric.rmse <= 0.01 &&
                viewMetric.rmse <= 0.01 && lightMetric.mae <= 0.01 &&
                lightMetric.p99 <= 0.03;
            std::ostringstream line;
            line << std::fixed << std::setprecision(8)
                 << "[STAGE9][" << appearance.name << "]["
                 << candidate.name << "] DENSITY_SSIM=" << densitySsim
                 << " DENSITY_RMSE=" << densityMetric.rmse
                 << " VIEW_SSIM=" << viewSsim
                 << " VIEW_RMSE=" << viewMetric.rmse
                 << " LIGHT_MAE=" << lightMetric.mae
                 << " LIGHT_P99=" << lightMetric.p99 << ' '
                 << (qualityCandidate ? "AUTO_QUALITY_PASS" : "REPORT");
            WriteDiagnosticLine(line.str());
            qualityResults.push_back({
                appearance.name, candidate.name, densitySsim,
                densityMetric.rmse, viewSsim, viewMetric.rmse,
                lightMetric.mae, lightMetric.p99,
                renderer.OptimizationSettings(),
                renderer.CloudSettings().transmittanceThreshold,
                qualityCandidate });
        }
    }
    const bool sameAppearanceFrameSize =
        denseReferenceDensity.width == stratusReferenceDensity.width &&
        denseReferenceDensity.height == stratusReferenceDensity.height &&
        denseReferenceDensity.pixels.size() ==
            stratusReferenceDensity.pixels.size() &&
        !denseReferenceDensity.pixels.empty();
    bool appearanceReferenceDifferent = false;
    if (sameAppearanceFrameSize)
    {
        for (std::size_t index = 0;
             index < denseReferenceDensity.pixels.size(); ++index)
        {
            const DirectX::XMFLOAT4& dense =
                denseReferenceDensity.pixels[index];
            const DirectX::XMFLOAT4& stratus =
                stratusReferenceDensity.pixels[index];
            if (std::abs(dense.x - stratus.x) > 1.0e-6f ||
                std::abs(dense.y - stratus.y) > 1.0e-6f ||
                std::abs(dense.z - stratus.z) > 1.0e-6f ||
                std::abs(dense.w - stratus.w) > 1.0e-6f)
            {
                appearanceReferenceDifferent = true;
                break;
            }
        }
    }
    if (!sameAppearanceFrameSize || !appearanceReferenceDifferent)
        return 8;
    WriteDiagnosticLine(
        "[STAGE9][APPEARANCE] Dense and Stratus reference frames differ");

    std::error_code error;
    const std::filesystem::path directory =
        std::filesystem::path(VCLOUD_SHADER_SOURCE_DIR).parent_path() /
        "captures" / "stage9";
    std::filesystem::create_directories(directory, error);
    if (error)
        return 6;
    std::ofstream csv(directory / "quality.csv",
                      std::ios::binary | std::ios::trunc);
    csv << "adapter,driver,appearance,preset,density_ssim,density_rmse,"
           "view_ssim,view_rmse,light_mae,light_p99,precheck,empty_search,"
           "early_exit,distance_step,coarse_multiplier,far_multiplier,"
           "cone_taps,cone_angle_degrees,cone_far_fraction,exit_threshold,quality_pass\n";
    for (const QualityResult& result : qualityResults)
        csv << '"' << renderer.AdapterName() << "\",\""
            << renderer.DriverVersion() << "\",\"" << result.appearance
            << "\",\"" << result.preset << "\"," << result.densitySsim
            << ',' << result.densityRmse << ',' << result.viewSsim << ','
            << result.viewRmse << ',' << result.lightMae << ','
            << result.lightP99 << ','
            << result.parameters.supportPrecheckEnabled << ','
            << result.parameters.emptySpaceSkippingEnabled << ','
            << result.parameters.viewEarlyExitEnabled << ','
            << result.parameters.distanceStepEnabled << ','
            << result.parameters.coarseStepMultiplier << ','
            << result.parameters.farStepMultiplier << ','
            << result.parameters.coneSampleCount << ','
            << result.parameters.coneAngleDegrees << ','
            << result.parameters.lightFarSampleFraction << ','
            << result.earlyExitThreshold << ','
            << (result.qualityPassed ? 1 : 0) << '\n';
    std::ofstream json(directory / "quality.json",
                       std::ios::binary | std::ios::trunc);
    json << "{\n  \"adapter\": \"" << renderer.AdapterName()
         << "\",\n  \"driver\": \"" << renderer.DriverVersion()
         << "\",\n  \"appearanceReferenceDifferent\": true,\n"
         << "  \"results\": [\n";
    for (std::size_t index = 0; index < qualityResults.size(); ++index)
    {
        const QualityResult& result = qualityResults[index];
        json << "    {\"appearance\": \"" << result.appearance
             << "\", \"preset\": \"" << result.preset
             << "\", \"densitySsim\": " << result.densitySsim
             << ", \"densityRmse\": " << result.densityRmse
             << ", \"viewSsim\": " << result.viewSsim
             << ", \"viewRmse\": " << result.viewRmse
             << ", \"lightMae\": " << result.lightMae
             << ", \"lightP99\": " << result.lightP99
             << ", \"parameters\": {\"precheck\": "
             << result.parameters.supportPrecheckEnabled
             << ", \"emptySearch\": "
             << result.parameters.emptySpaceSkippingEnabled
             << ", \"earlyExit\": "
             << result.parameters.viewEarlyExitEnabled
             << ", \"distanceStep\": "
             << result.parameters.distanceStepEnabled
             << ", \"coarseMultiplier\": "
             << result.parameters.coarseStepMultiplier
             << ", \"farMultiplier\": "
             << result.parameters.farStepMultiplier
             << ", \"coneTaps\": " << result.parameters.coneSampleCount
             << ", \"coneAngleDegrees\": "
             << result.parameters.coneAngleDegrees
             << ", \"coneFarFraction\": "
             << result.parameters.lightFarSampleFraction
             << ", \"exitThreshold\": " << result.earlyExitThreshold
             << "}, \"qualityPass\": "
             << (result.qualityPassed ? "true" : "false") << "}"
             << (index + 1u == qualityResults.size() ? "\n" : ",\n");
    }
    json << "  ]\n}\n";
    if (!csv.good() || !json.good())
        return 7;
    // 자동 측정 뒤에는 2026-08-19 사용자 승인 기본값으로 되돌린다.
    renderer.ApplyStage9OptimizationPreset(
        Stage9OptimizationPreset::Balanced);
    return finitePassed && !renderer.HasDebugLayerErrors() ? 0 : 1;
}

int RunStage9PerformanceTest(Renderer& renderer, Camera& camera)
{
    constexpr int kWarmupFrames = 120;
    constexpr std::size_t kSamples = 600u;
    constexpr int kMaximumAttempts = 1800;
    struct Result
    {
        std::string preset;
        std::string scene;
        double average = 0.0;
        double p50 = 0.0;
        double p95 = 0.0;
        OptimizationParameters parameters{};
        std::uint32_t maxViewSteps = 0u;
        float viewStepMeters = 0.0f;
        std::uint32_t maxLightSteps = 0u;
        float lightStepMeters = 0.0f;
        float exitThreshold = 0.0f;
    };
    const struct Scene
    {
        const char* name;
        CloudAppearancePreset appearance;
        Stage13CameraPresetId cameraPreset;
        bool zenith;
        bool opaque;
    } scenes[] = {
        { "DenseZenith", CloudAppearancePreset::DenseMixedDefault,
          Stage13CameraPresetId::HeroDepth, true, false },
        { "DenseHorizon", CloudAppearancePreset::DenseMixedDefault,
          Stage13CameraPresetId::GroundHorizon, false, false },
        { "StratusHorizon", CloudAppearancePreset::Stratus,
          Stage13CameraPresetId::GroundHorizon, false, false },
        { "CumulusHorizon", CloudAppearancePreset::Cumulus,
          Stage13CameraPresetId::GroundHorizon, false, false },
        { "CumulusInside", CloudAppearancePreset::Cumulus,
          Stage13CameraPresetId::InsideLayer, false, false },
        { "AboveLayer", CloudAppearancePreset::DenseMixedDefault,
          Stage13CameraPresetId::AboveLayer, false, false },
        { "DepthOccluded", CloudAppearancePreset::DenseMixedDefault,
          Stage13CameraPresetId::HeroDepth, false, true },
    };
    const struct PerformancePreset
    {
        const char* name;
        Stage9OptimizationPreset base;
        std::uint32_t coneOverride;
        float angleOverride;
        float farFractionOverride;
    } presets[] = {
        { "Balanced", Stage9OptimizationPreset::Balanced, 0u, 0.0f, 0.0f },
        { "Conservative", Stage9OptimizationPreset::Conservative, 0u, 0.0f, 0.0f },
        { "Approved Reference", Stage9OptimizationPreset::ApprovedReference, 0u, 0.0f, 0.0f },
    };

    renderer.SetNoiseLabVisible(false);
    renderer.EnableNoiseLabPreviews(false);
    renderer.SetVSyncEnabled(false);
    renderer.SetDebugMode(CloudDebugMode::Composite);
    camera.SetClipPlanes(
        stage13camera::kNearPlaneMeters, stage13camera::kFarPlaneMeters);
    if (!renderer.ApplyStage13OpenWorldPreset())
        return 2;

    std::vector<Result> results;
    for (const PerformancePreset& preset : presets)
    {
        renderer.ApplyStage9OptimizationPreset(preset.base);
        if (preset.coneOverride != 0u)
            renderer.ConfigureStage9ConeForValidation(
                preset.coneOverride, preset.angleOverride,
                preset.farFractionOverride);
        for (const Scene& scene : scenes)
        {
            if (!renderer.ApplyCloudAppearancePreset(scene.appearance))
                return 3;
            renderer.SetOpaqueSceneForTest(scene.opaque);
            const Stage13CameraPreset& cameraPreset =
                stage13camera::Get(scene.cameraPreset);
            if (scene.zenith)
                camera.SetLookAt(cameraPreset.position,
                    { cameraPreset.position.x, 7000.0f,
                      cameraPreset.position.z });
            else
                camera.SetLookAt(cameraPreset.position, cameraPreset.target);
            for (int frame = 0; frame < kWarmupFrames; ++frame)
                renderer.Render(camera, 0.0f);

            std::vector<double> samples;
            samples.reserve(kSamples);
            std::uint64_t lastIndex = renderer.TimingSnapshot().gpuSampleIndex;
            for (int attempt = 0;
                 attempt < kMaximumAttempts && samples.size() < kSamples;
                 ++attempt)
            {
                renderer.Render(camera, 0.0f);
                const FrameTimingSnapshot timing = renderer.TimingSnapshot();
                if (!timing.gpuValid || timing.gpuSampleIndex == lastIndex)
                {
                    Sleep(1);
                    continue;
                }
                lastIndex = timing.gpuSampleIndex;
                if (!std::isfinite(timing.rawGpuCloudMs) ||
                    timing.rawGpuCloudMs < 0.0)
                    return 4;
                samples.push_back(timing.rawGpuCloudMs);
            }
            if (samples.size() != kSamples)
                return 5;
            std::sort(samples.begin(), samples.end());
            Result result;
            result.preset = preset.name;
            result.scene = scene.name;
            result.average = std::accumulate(
                samples.begin(), samples.end(), 0.0) / samples.size();
            result.p50 = samples[samples.size() / 2u];
            result.p95 = samples[static_cast<std::size_t>(
                std::ceil(samples.size() * 0.95)) - 1u];
            result.parameters = renderer.OptimizationSettings();
            result.maxViewSteps = renderer.CloudSettings().maxViewSteps;
            result.viewStepMeters = renderer.CloudSettings().stepSize;
            result.maxLightSteps = renderer.LightSettings().maxLightSteps;
            result.lightStepMeters = renderer.LightSettings().lightStepSize;
            result.exitThreshold =
                renderer.CloudSettings().transmittanceThreshold;
            results.push_back(result);
        }
    }

    std::error_code error;
    const std::filesystem::path directory =
        std::filesystem::path(VCLOUD_SHADER_SOURCE_DIR).parent_path() /
        "captures" / "stage9";
    std::filesystem::create_directories(directory, error);
    if (error)
        return 6;
    std::ofstream csv(directory / "performance.csv",
                      std::ios::binary | std::ios::trunc);
    csv << "adapter,driver,preset,scene,samples,warmup,average_ms,p50_ms,p95_ms,"
           "precheck,empty_search,early_exit,distance_step,empty_samples,base_epsilon,"
           "coarse_multiplier,max_search_m,distance_start_m,distance_end_m,far_multiplier,"
           "light_mode,cone_taps,cone_angle_degrees,cone_far_fraction,max_view_steps,"
           "view_step_m,max_light_steps,light_step_m,exit_threshold\n";
    for (const Result& result : results)
        csv << '"' << renderer.AdapterName() << "\",\""
            << renderer.DriverVersion() << "\",\"" << result.preset
            << "\",\"" << result.scene << "\"," << kSamples << ','
            << kWarmupFrames << ',' << result.average << ',' << result.p50
            << ',' << result.p95 << ','
            << result.parameters.supportPrecheckEnabled << ','
            << result.parameters.emptySpaceSkippingEnabled << ','
            << result.parameters.viewEarlyExitEnabled << ','
            << result.parameters.distanceStepEnabled << ','
            << result.parameters.emptySamplesBeforeCoarse << ','
            << result.parameters.baseDensityEpsilon << ','
            << result.parameters.coarseStepMultiplier << ','
            << result.parameters.maxSearchStepMeters << ','
            << result.parameters.distanceStepStartMeters << ','
            << result.parameters.distanceStepEndMeters << ','
            << result.parameters.farStepMultiplier << ','
            << result.parameters.lightSamplingMode << ','
            << result.parameters.coneSampleCount << ','
            << result.parameters.coneAngleDegrees << ','
            << result.parameters.lightFarSampleFraction << ','
            << result.maxViewSteps << ',' << result.viewStepMeters << ','
            << result.maxLightSteps << ',' << result.lightStepMeters << ','
            << result.exitThreshold << '\n';
    if (!csv.good())
        return 7;

    const auto findP95 = [&](const std::string& preset,
                             const std::string& scene)
    {
        for (const Result& result : results)
            if (result.preset == preset && result.scene == scene)
                return result.p95;
        return std::numeric_limits<double>::infinity();
    };
    const std::string referenceName = "Approved Reference";
    std::map<std::string, bool> qualifies;
    for (const PerformancePreset& preset : presets)
    {
        const std::string name = preset.name;
        bool passed = findP95(name, "CumulusHorizon") <= 10.0 &&
            findP95(name, "CumulusHorizon") <=
                findP95(referenceName, "CumulusHorizon") * 0.85;
        for (const Scene& scene : scenes)
            if (std::string(scene.name) != "CumulusHorizon")
                passed = passed && findP95(name, scene.name) <=
                    findP95(referenceName, scene.name) * 1.03;
        qualifies[name] = passed;
    }
    std::ofstream json(directory / "performance.json",
                       std::ios::binary | std::ios::trunc);
    json << "{\n  \"adapter\": \"" << renderer.AdapterName()
         << "\",\n  \"driver\": \"" << renderer.DriverVersion()
         << "\",\n  \"resolution\": [1920, 1080],\n"
         << "  \"vsync\": false,\n  \"timeSeconds\": 0,\n"
         << "  \"samples\": " << kSamples << ",\n  \"warmup\": "
         << kWarmupFrames << ",\n  \"results\": [\n";
    for (std::size_t index = 0; index < results.size(); ++index)
    {
        const Result& result = results[index];
        json << "    {\"preset\": \"" << result.preset
             << "\", \"scene\": \"" << result.scene
             << "\", \"averageMs\": " << result.average
             << ", \"p50Ms\": " << result.p50
             << ", \"p95Ms\": " << result.p95
             << ", \"parameters\": {\"precheck\": "
             << result.parameters.supportPrecheckEnabled
             << ", \"emptySearch\": "
             << result.parameters.emptySpaceSkippingEnabled
             << ", \"earlyExit\": "
             << result.parameters.viewEarlyExitEnabled
             << ", \"distanceStep\": "
             << result.parameters.distanceStepEnabled
             << ", \"emptySamples\": "
             << result.parameters.emptySamplesBeforeCoarse
             << ", \"baseEpsilon\": "
             << result.parameters.baseDensityEpsilon
             << ", \"coarseMultiplier\": "
             << result.parameters.coarseStepMultiplier
             << ", \"maxSearchMeters\": "
             << result.parameters.maxSearchStepMeters
             << ", \"distanceStartMeters\": "
             << result.parameters.distanceStepStartMeters
             << ", \"distanceEndMeters\": "
             << result.parameters.distanceStepEndMeters
             << ", \"farMultiplier\": "
             << result.parameters.farStepMultiplier
             << ", \"lightMode\": "
             << result.parameters.lightSamplingMode
             << ", \"coneTaps\": " << result.parameters.coneSampleCount
             << ", \"coneAngleDegrees\": "
             << result.parameters.coneAngleDegrees
             << ", \"coneFarFraction\": "
             << result.parameters.lightFarSampleFraction
             << ", \"maxViewSteps\": " << result.maxViewSteps
             << ", \"viewStepMeters\": " << result.viewStepMeters
             << ", \"maxLightSteps\": " << result.maxLightSteps
             << ", \"lightStepMeters\": " << result.lightStepMeters
             << ", \"exitThreshold\": " << result.exitThreshold << "}}"
             << (index + 1u == results.size() ? "\n" : ",\n");
    }
    json << "  ],\n  \"candidates\": {\n";
    std::size_t presetIndex = 0;
    for (const PerformancePreset& preset : presets)
    {
        const std::string name = preset.name;
        json << "    \"" << name << "\": {\"performanceGate\": "
             << (qualifies[name] ? "true" : "false") << "}"
             << (++presetIndex == std::size(presets) ? "\n" : ",\n");
    }
    json << "  }\n}\n";
    if (!json.good())
        return 8;
    bool anyCandidatePassed = qualifies["Balanced"] ||
        qualifies["Conservative"];
    return anyCandidatePassed && !renderer.HasDebugLayerErrors() ? 0 : 1;
}

int RunStage13OpticsLightingSmokeTest(Renderer& renderer, Camera& camera)
{
    using stage13diagnostics::ComparisonKind;
    renderer.SetNoiseLabVisible(false);
    renderer.EnableNoiseLabPreviews(false);
    renderer.SetVSyncEnabled(false);
    renderer.SetOpaqueSceneForTest(false);
    if (!renderer.ApplyStage13OpenWorldPreset())
        return 2;
    // 단계 13의 광학 계약은 단계 9 생략 경로가 아닌 승인 Reference로 검사한다.
    renderer.ApplyStage9OptimizationPreset(
        Stage9OptimizationPreset::ApprovedReference);

    camera.SetClipPlanes(
        stage13camera::kNearPlaneMeters, stage13camera::kFarPlaneMeters);
    const Stage13CameraPreset& horizon = stage13camera::Get(
        Stage13CameraPresetId::GroundHorizon);
    camera.SetLookAt(horizon.position, horizon.target);

    const CloudDebugMode modes[] = {
        CloudDebugMode::LightTransmittance,
        CloudDebugMode::AccumulatedDirectLighting,
        CloudDebugMode::Composite,
    };
    bool qualityPassed = true;
    const struct Candidate
    {
        const char* name;
        std::uint32_t steps;
        float stepMeters;
        bool gated;
    } candidates[] = {
        { "Default", stage13optics::kDefaultLightSteps,
          static_cast<float>(stage13optics::kDefaultLightStepMeters), true },
        { "PreviousQuality", stage13optics::kPreviousQualityLightSteps,
          static_cast<float>(stage13optics::kPreviousQualityLightStepMeters), false },
    };
    const struct AppearanceCase
    {
        const char* name;
        CloudAppearancePreset preset;
    } appearances[] = {
        { "DenseMixed", CloudAppearancePreset::DenseMixedDefault },
        { "Stratus", CloudAppearancePreset::Stratus },
        { "Cumulus", CloudAppearancePreset::Cumulus },
    };
    for (const AppearanceCase& appearance : appearances)
    {
        if (!renderer.ApplyCloudAppearancePreset(appearance.preset))
            return 3;
        std::map<CloudDebugMode, CloudDiagnosticFrame> reference;
        renderer.SetLightSampling(
            stage13optics::kReferenceLightSteps,
            static_cast<float>(stage13optics::kReferenceLightStepMeters));
        for (CloudDebugMode mode : modes)
        {
            if (!renderer.CaptureCloudDiagnosticFrame(
                    camera, 0.0f, mode, reference[mode]))
                return 4;
        }
        for (const Candidate& candidate : candidates)
        {
            renderer.SetLightSampling(candidate.steps, candidate.stepMeters);
            for (CloudDebugMode mode : modes)
            {
                CloudDiagnosticFrame frame;
                if (!renderer.CaptureCloudDiagnosticFrame(
                        camera, 0.0f, mode, frame))
                    return 4;
                const bool rgb = mode != CloudDebugMode::LightTransmittance;
                const auto metric = stage13diagnostics::CompareFrames(
                    reference.at(mode), frame, nullptr, nullptr,
                    rgb ? ComparisonKind::Rgb : ComparisonKind::Scalar,
                    0.05, false);
                const bool passed = metric.mae <= 0.01 && metric.p99 <= 0.03;
                std::ostringstream line;
                line << std::fixed << std::setprecision(8)
                     << "[STAGE13-5][LIGHT][" << appearance.name << "]["
                     << candidate.name << "][" << DebugModeName(mode)
                     << "] MAE=" << metric.mae << " RMSE=" << metric.rmse
                     << " P99=" << metric.p99 << " MAX=" << metric.maximum
                     << ' ' << (candidate.gated
                         ? (passed ? "PASS" : "FAIL") : "REPORT");
                WriteDiagnosticLine(line.str());
                if (candidate.gated)
                    qualityPassed = qualityPassed && passed;
            }
        }
    }

    if (!renderer.ApplyCloudAppearancePreset(
            CloudAppearancePreset::DenseMixedDefault))
        return 3;
    renderer.SetLightSampling(
        stage13optics::kDefaultLightSteps,
        static_cast<float>(stage13optics::kDefaultLightStepMeters));
    renderer.SetCloudLodForValidation(false, 32000.0f, 48000.0f);
    CloudDiagnosticFrame lodOff;
    if (!renderer.CaptureCloudDiagnosticFrame(
            camera, 0.0f, CloudDebugMode::DetailLodFactor, lodOff))
        return 5;
    renderer.SetCloudLodForValidation(true, 32000.0f, 48000.0f);
    CloudDiagnosticFrame lodOn;
    if (!renderer.CaptureCloudDiagnosticFrame(
            camera, 0.0f, CloudDebugMode::DetailLodFactor, lodOn))
        return 6;
    const auto lodMetric = stage13diagnostics::CompareFrames(
        lodOff, lodOn, nullptr, nullptr, ComparisonKind::Scalar, 0.01, false);
    const CloudLodParameters& lod = renderer.LodSettings();
    const bool lodPassed = lodMetric.mae > 0.0 &&
        std::isfinite(lod.detailNeutralValue) &&
        lod.detailNeutralValue >= 0.0f && lod.detailNeutralValue <= 1.0f;
    {
        std::ostringstream line;
        line << std::fixed << std::setprecision(8)
             << "[STAGE13-5][DETAIL-LOD] MAE=" << lodMetric.mae
             << " NEUTRAL=" << lod.detailNeutralValue << ' '
             << (lodPassed ? "PASS" : "FAIL");
        WriteDiagnosticLine(line.str());
    }

    // 사용자 화면에서 확인한 두 계약을 GPU에서 고정한다.
    // 태양 방향은 View 광학 깊이를 바꾸지 않고 직접광만 바꿔야 하며,
    // Silver Lining 합성은 LDR 출력 범위를 넘어 흰색으로 잘리면 안 된다.
    renderer.ApplyPortfolioHeroLighting();
    CloudDiagnosticFrame eastDepth;
    CloudDiagnosticFrame westDepth;
    CloudDiagnosticFrame eastDirect;
    CloudDiagnosticFrame westDirect;
    CloudDiagnosticFrame eastComposite;
    CloudDiagnosticFrame westComposite;
    CloudDiagnosticFrame eastSilver;
    CloudDiagnosticFrame westSilver;
    CloudDiagnosticFrame eastSurface;
    CloudDiagnosticFrame eastAmbientVisibility;
    if (!renderer.CaptureCloudDiagnosticFrame(
            camera, 0.0f, CloudDebugMode::ViewOpticalDepth, eastDepth) ||
        !renderer.CaptureCloudDiagnosticFrame(
            camera, 0.0f, CloudDebugMode::AccumulatedDirectLighting,
            eastDirect) ||
        !renderer.CaptureCloudDiagnosticFrame(
            camera, 0.0f, CloudDebugMode::Composite, eastComposite) ||
        !renderer.CaptureCloudDiagnosticFrame(
            camera, 0.0f, CloudDebugMode::SilverLiningContribution,
            eastSilver) ||
        !renderer.CaptureCloudDiagnosticFrame(
            camera, 0.0f, CloudDebugMode::ShapedSunVisibility,
            eastSurface) ||
        !renderer.CaptureCloudDiagnosticFrame(
            camera, 0.0f, CloudDebugMode::AmbientVisibility,
            eastAmbientVisibility))
        return 7;
    renderer.ApplyStage6SunPreset(Stage6SunPreset::LowWest);
    if (!renderer.CaptureCloudDiagnosticFrame(
            camera, 0.0f, CloudDebugMode::ViewOpticalDepth, westDepth) ||
        !renderer.CaptureCloudDiagnosticFrame(
            camera, 0.0f, CloudDebugMode::AccumulatedDirectLighting,
            westDirect) ||
        !renderer.CaptureCloudDiagnosticFrame(
            camera, 0.0f, CloudDebugMode::Composite, westComposite) ||
        !renderer.CaptureCloudDiagnosticFrame(
            camera, 0.0f, CloudDebugMode::SilverLiningContribution,
            westSilver))
        return 8;

    const auto depthDirectionMetric = stage13diagnostics::CompareFrames(
        eastDepth, westDepth, nullptr, nullptr, ComparisonKind::Scalar,
        0.01, false);
    const auto directDirectionMetric = stage13diagnostics::CompareFrames(
        eastDirect, westDirect, nullptr, nullptr, ComparisonKind::Rgb,
        0.01, false);
    const auto silverDirectionMetric = stage13diagnostics::CompareFrames(
        eastSilver, westSilver, nullptr, nullptr, ComparisonKind::Rgb,
        0.01, false);
    const auto maximumRgb = [](const CloudDiagnosticFrame& frame)
    {
        float maximum = 0.0f;
        for (const DirectX::XMFLOAT4& pixel : frame.pixels)
            maximum = std::max(maximum,
                std::max(pixel.x, std::max(pixel.y, pixel.z)));
        return maximum;
    };
    const float eastMaximum = maximumRgb(eastComposite);
    const float westMaximum = maximumRgb(westComposite);
    std::size_t nearWhiteCount = 0u;
    for (const DirectX::XMFLOAT4& pixel : eastComposite.pixels)
    {
        if (std::max(pixel.x, std::max(pixel.y, pixel.z)) >= 0.98f)
            ++nearWhiteCount;
    }
    const double nearWhiteRatio = eastComposite.pixels.empty() ? 1.0 :
        static_cast<double>(nearWhiteCount) /
        static_cast<double>(eastComposite.pixels.size());

    double exposedSilver = 0.0;
    double interiorSilver = 0.0;
    std::size_t exposedCount = 0u;
    std::size_t interiorCount = 0u;
    double ambientMean = 0.0;
    std::size_t ambientCount = 0u;
    for (std::size_t index = 0; index < eastSilver.pixels.size(); ++index)
    {
        const auto& silverPixel = eastSilver.pixels[index];
        const auto& surfacePixel = eastSurface.pixels[index];
        const auto& ambientPixel = eastAmbientVisibility.pixels[index];
        const double silverValue = std::max(
            silverPixel.x, std::max(silverPixel.y, silverPixel.z));
        const double surfaceValue = surfacePixel.x;
        const double ambientValue = ambientPixel.x;
        if (ambientValue <= 1e-5)
            continue;
        ambientMean += ambientValue;
        ++ambientCount;
        if (surfaceValue >= 0.5)
        {
            exposedSilver += silverValue;
            ++exposedCount;
        }
        else
        {
            interiorSilver += silverValue;
            ++interiorCount;
        }
    }
    const double exposedSilverMean = exposedCount > 0u
        ? exposedSilver / static_cast<double>(exposedCount) : 0.0;
    const double interiorSilverMean = interiorCount > 0u
        ? interiorSilver / static_cast<double>(interiorCount) : 0.0;
    ambientMean = ambientCount > 0u
        ? ambientMean / static_cast<double>(ambientCount) : 0.0;
    const bool directionPassed = depthDirectionMetric.mae <= 1e-7 &&
        directDirectionMetric.mae > 1e-5 &&
        silverDirectionMetric.mae > 1e-6;
    const bool highlightPassed = std::isfinite(eastMaximum) &&
        std::isfinite(westMaximum) && eastMaximum <= 1.0f &&
        westMaximum <= 1.0f && nearWhiteRatio < 0.01;
    const bool edgeSelectivityPassed = exposedCount > 0u &&
        interiorCount > 0u && exposedSilverMean > 0.0 &&
        exposedSilverMean > interiorSilverMean;
    const bool ambientPassed = ambientCount > 0u &&
        std::isfinite(ambientMean) && ambientMean > 0.0 && ambientMean <= 1.0;
    {
        std::ostringstream line;
        line << std::fixed << std::setprecision(8)
             << "[STAGE13-5][DIRECTION] VIEW_TAU_MAE="
             << depthDirectionMetric.mae << " DIRECT_MAE="
             << directDirectionMetric.mae << " SILVER_MAE="
             << silverDirectionMetric.mae << ' '
             << (directionPassed ? "PASS" : "FAIL");
        WriteDiagnosticLine(line.str());
    }
    {
        std::ostringstream line;
        line << std::fixed << std::setprecision(8)
             << "[STAGE13-5][SILVER-LINING] EAST_MAX=" << eastMaximum
             << " WEST_MAX=" << westMaximum
             << " NEAR_WHITE_RATIO=" << nearWhiteRatio << ' '
             << (highlightPassed ? "PASS" : "FAIL");
        WriteDiagnosticLine(line.str());
    }
    {
        std::ostringstream line;
        line << std::fixed << std::setprecision(8)
             << "[STAGE13-5][EDGE-SELECTIVITY] EXPOSED_MEAN="
             << exposedSilverMean << " INTERIOR_MEAN=" << interiorSilverMean
             << " AMBIENT_MEAN=" << ambientMean << ' '
             << (edgeSelectivityPassed && ambientPassed ? "PASS" : "FAIL");
        WriteDiagnosticLine(line.str());
    }
    return qualityPassed && lodPassed && directionPassed && highlightPassed &&
        edgeSelectivityPassed && ambientPassed &&
        !renderer.HasDebugLayerErrors()
        ? 0 : 1;
}

int RunStage13LightingPerformanceTest(Renderer& renderer, Camera& camera)
{
    constexpr int kWarmupFrames = 180;
    constexpr std::size_t kMeasurementSamples = 600u;
    constexpr int kMaximumRenderAttempts = 1800;
    // 변경 전 HEAD 셰이더를 같은 실행 파일의 원시 timestamp 수집기에 넣어 얻은
    // 동일 조건 baseline이다. 과거 UI EMA 14.7525ms와 통계를 섞지 않는다.
    constexpr double kPreviousCumulusF6AverageMilliseconds = 13.688282;
    constexpr double kPreviousCumulusF6P95Milliseconds = 15.639552;
    constexpr double kMaximumRegressionRatio = 1.05;
    constexpr double kFrameBudgetMilliseconds = 16.67;

    renderer.SetNoiseLabVisible(false);
    renderer.EnableNoiseLabPreviews(false);
    renderer.SetVSyncEnabled(false);
    renderer.SetOpaqueSceneForTest(false);
    if (!renderer.ApplyStage13OpenWorldPreset() ||
        !renderer.ApplyCloudAppearancePreset(CloudAppearancePreset::Cumulus))
        return 2;
    renderer.ApplyStage6SunPreset(Stage6SunPreset::Noon);
    renderer.ApplyStage7PhasePreset(Stage7PhasePreset::Balanced);
    renderer.ApplyStage8EnvironmentPreset(Stage8EnvironmentPreset::Balanced);
    renderer.SetDebugMode(CloudDebugMode::Composite);

    camera.SetClipPlanes(
        stage13camera::kNearPlaneMeters, stage13camera::kFarPlaneMeters);
    const Stage13CameraPreset& horizon = stage13camera::Get(
        Stage13CameraPresetId::GroundHorizon);
    camera.SetLookAt(horizon.position, horizon.target);

    for (int frame = 0; frame < kWarmupFrames; ++frame)
        renderer.Render(camera, 0.0f);

    std::vector<double> cloudSamples;
    cloudSamples.reserve(kMeasurementSamples);
    std::uint64_t lastSampleIndex = renderer.TimingSnapshot().gpuSampleIndex;
    for (int attempt = 0;
         attempt < kMaximumRenderAttempts &&
         cloudSamples.size() < kMeasurementSamples;
         ++attempt)
    {
        renderer.Render(camera, 0.0f);
        const FrameTimingSnapshot timing = renderer.TimingSnapshot();
        if (!timing.gpuValid || timing.gpuSampleIndex == lastSampleIndex)
        {
            Sleep(1);
            continue;
        }
        lastSampleIndex = timing.gpuSampleIndex;
        if (!std::isfinite(timing.rawGpuCloudMs) ||
            timing.rawGpuCloudMs < 0.0 ||
            timing.rawGpuCloudMs > timing.rawGpuFrameMs)
            return 3;
        cloudSamples.push_back(timing.rawGpuCloudMs);
    }
    if (cloudSamples.size() != kMeasurementSamples)
        return 4;

    std::sort(cloudSamples.begin(), cloudSamples.end());
    const std::size_t percentileIndex = static_cast<std::size_t>(
        std::ceil(0.95 * static_cast<double>(cloudSamples.size()))) - 1u;
    const double p95 = cloudSamples[std::min(
        percentileIndex, cloudSamples.size() - 1u)];
    const double average = std::accumulate(
        cloudSamples.begin(), cloudSamples.end(), 0.0) /
        static_cast<double>(cloudSamples.size());
    const double averageRegressionLimit =
        kPreviousCumulusF6AverageMilliseconds * kMaximumRegressionRatio;
    const double p95RegressionLimit =
        kPreviousCumulusF6P95Milliseconds * kMaximumRegressionRatio;
    const bool passed = p95 <= kFrameBudgetMilliseconds &&
        p95 <= p95RegressionLimit && average <= averageRegressionLimit;
    std::ostringstream line;
    line << std::fixed << std::setprecision(6)
         << "[STAGE13-5][CUMULUS-F6-PERF] SAMPLES="
         << cloudSamples.size() << " GPU_CLOUD_AVG_MS=" << average
         << " GPU_CLOUD_P95_MS=" << p95
         << " AVG_REGRESSION_LIMIT_MS=" << averageRegressionLimit
         << " P95_REGRESSION_LIMIT_MS=" << p95RegressionLimit << ' '
         << (passed ? "PASS" : "FAIL");
    WriteDiagnosticLine(line.str());
    return passed && !renderer.HasDebugLayerErrors() ? 0 : 1;
}


int RunStage13UnifiedSceneSmokeTest(Renderer& renderer, Camera& camera)
{
    renderer.SetNoiseLabVisible(false);
    renderer.EnableNoiseLabPreviews(false);
    renderer.SetVSyncEnabled(false);
    if (!renderer.ApplyStage13OpenWorldPreset())
        return 2;

    const CloudParameters initialCloud = renderer.CloudSettings();
    const CloudDomainParameters initialDomain = renderer.DomainSettings();
    const float initialMoveSpeed = renderer.CameraMoveSpeed();
    if (renderer.DomainType() != CloudDomainType::PlanarLayer ||
        initialDomain.cloudBottomAltitude != 1500.0f ||
        initialDomain.cloudLayerThickness != 6000.0f ||
        initialDomain.maxViewTraceDistance != 50000.0f ||
        initialDomain.viewTraceFadeStartDistance != 40000.0f ||
        initialMoveSpeed != stage13scene::kMoveSpeedMetersPerSecond)
        return 3;

    const auto finiteHash = [&](CloudDebugMode mode, std::uint64_t& hash,
                                bool& nonBlack)
    {
        CloudDiagnosticFrame frame;
        if (!renderer.CaptureCloudDiagnosticFrame(camera, 0.0f, mode, frame) ||
            frame.pixels.empty())
            return false;
        hash = HashDiagnosticFrame(frame);
        nonBlack = false;
        for (const DirectX::XMFLOAT4& pixel : frame.pixels)
        {
            if (!std::isfinite(pixel.x) || !std::isfinite(pixel.y) ||
                !std::isfinite(pixel.z) || !std::isfinite(pixel.w))
                return false;
            nonBlack = nonBlack || pixel.x > 1e-6f || pixel.y > 1e-6f ||
                pixel.z > 1e-6f;
        }
        return hash != 0u;
    };

    std::set<std::uint64_t> cameraCompositeHashes;
    for (const Stage13CameraPreset& preset : stage13camera::kOpenWorldPresets)
    {
        camera.SetClipPlanes(stage13camera::kNearPlaneMeters,
                             stage13camera::kFarPlaneMeters);
        camera.SetFovYDegrees(60.0f);
        camera.SetLookAt(preset.position, preset.target);
        std::set<std::uint64_t> debugHashes;
        for (int digit = 0; digit <= 9; ++digit)
        {
            std::uint64_t hash = 0;
            bool nonBlack = false;
            const CloudDebugMode mode = stage13scene::DebugModeFromDigit(digit);
            const bool mustBeNonBlack = mode == CloudDebugMode::Composite ||
                mode == CloudDebugMode::RawNoise ||
                mode == CloudDebugMode::WeatherCoverage ||
                mode == CloudDebugMode::Transmittance ||
                mode == CloudDebugMode::LightTransmittance;
            if (!finiteHash(mode, hash, nonBlack) ||
                (mustBeNonBlack && !nonBlack))
            {
                WriteDiagnosticLine(std::string("[UNIFIED-SCENE][FAIL] CAMERA=") +
                    preset.diagnosticName + " DIGIT=" + std::to_string(digit) +
                    " MODE=" + DebugModeName(mode));
                return 4;
            }
            debugHashes.insert(hash);
            if (mode == CloudDebugMode::Composite)
                cameraCompositeHashes.insert(hash);
        }
        if (debugHashes.size() < 6u)
            return 5;
    }
    if (cameraCompositeHashes.size() != stage13camera::kOpenWorldPresets.size())
        return 6;

    // 13-4E 외형 프리셋은 렌더 입력만 바꾸고 scene/domain/light/sampling과
    // Weather의 공간 seed/period, wind를 보존해야 한다.
    const LightParameters appearanceLight = renderer.LightSettings();
    const EnvironmentParameters appearanceEnvironment =
        renderer.EnvironmentSettings();
    const CloudLodParameters appearanceLod = renderer.LodSettings();
    const WeatherMapGeneratorSettings appearanceWeather =
        renderer.WeatherGeneratorSettings();
    const float appearanceWind = renderer.CloudSettings().windSpeed;
    constexpr CloudDebugMode appearanceModes[] = {
        CloudDebugMode::Composite,
        CloudDebugMode::WeatherCoverage,
        CloudDebugMode::BaseDensity,
        CloudDebugMode::FinalDensity,
        CloudDebugMode::ViewOpticalDepth,
        CloudDebugMode::LightTransmittance,
    };
    constexpr CloudAppearancePreset appearancePresets[] = {
        CloudAppearancePreset::DenseMixedDefault,
        CloudAppearancePreset::Stratus,
        CloudAppearancePreset::Cumulus,
    };
    for (std::size_t cameraIndex = 0; cameraIndex < 2; ++cameraIndex)
    {
        const Stage13CameraPreset& cameraPreset =
            stage13camera::kOpenWorldPresets[cameraIndex];
        camera.SetLookAt(cameraPreset.position, cameraPreset.target);
        std::array<std::set<std::uint64_t>, std::size(appearanceModes)>
            modeHashes;
        for (CloudAppearancePreset appearance : appearancePresets)
        {
            if (!renderer.ApplyCloudAppearancePreset(appearance) ||
                std::memcmp(&appearanceLight, &renderer.LightSettings(),
                            sizeof(appearanceLight)) != 0 ||
                std::memcmp(&appearanceEnvironment,
                            &renderer.EnvironmentSettings(),
                            sizeof(appearanceEnvironment)) != 0 ||
                std::memcmp(&appearanceLod, &renderer.LodSettings(),
                            sizeof(appearanceLod)) != 0 ||
                renderer.CloudSettings().windSpeed != appearanceWind ||
                renderer.WeatherGeneratorSettings().coverage.seed !=
                    appearanceWeather.coverage.seed ||
                renderer.WeatherGeneratorSettings().coverage.macroPeriod !=
                    appearanceWeather.coverage.macroPeriod)
                return 12;
            for (std::size_t modeIndex = 0;
                 modeIndex < std::size(appearanceModes); ++modeIndex)
            {
                std::uint64_t hash = 0;
                bool nonBlack = false;
                if (!finiteHash(appearanceModes[modeIndex], hash, nonBlack) ||
                    !nonBlack)
                    return 13;
                modeHashes[modeIndex].insert(hash);
            }
        }
        for (const auto& hashes : modeHashes)
            if (hashes.size() != std::size(appearancePresets))
                return 14;
    }
    if (!renderer.ApplyStage13OpenWorldPreset())
        return 15;

    for (OpenWorldPipelinePreset preset : {
            OpenWorldPipelinePreset::Legacy1000Baseline,
            OpenWorldPipelinePreset::Texture3D,
            OpenWorldPipelinePreset::PeriodicWeather,
            OpenWorldPipelinePreset::PhysicalShape,
            OpenWorldPipelinePreset::FullOpenWorld })
    {
        if (!renderer.ApplyOpenWorldPipelinePreset(preset) ||
            std::memcmp(&initialCloud.cloudBoundsMin,
                        &renderer.CloudSettings().cloudBoundsMin,
                        sizeof(DirectX::XMFLOAT3)) != 0 ||
            std::memcmp(&initialCloud.cloudBoundsMax,
                        &renderer.CloudSettings().cloudBoundsMax,
                        sizeof(DirectX::XMFLOAT3)) != 0 ||
            std::memcmp(&initialDomain, &renderer.DomainSettings(),
                        sizeof(initialDomain)) != 0 ||
            renderer.CameraMoveSpeed() != initialMoveSpeed)
            return 7;
    }

    renderer.ApplyPortfolioHeroLighting();
    renderer.SetDebugMode(CloudDebugMode::Composite);
    renderer.Render(camera, 0.0f);
    const std::filesystem::path exportRoot =
        std::filesystem::temp_directory_path() /
        L"VolumetricCloudStage13UnifiedSceneSmoke";
    if (!renderer.ExportNoiseLabSnapshot(exportRoot))
        return 8;
    bool schemaPassed = false;
    std::error_code exportError;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             exportRoot, exportError))
    {
        if (exportError)
            return 9;
        if (entry.path().filename() != L"noise-settings.json")
            continue;
        std::string metadata;
        if (ReadTextFile(entry.path(), metadata) &&
            metadata.find("\"schemaVersion\": 37") != std::string::npos &&
            metadata.find("\"implementationStage\": \"15\"") != std::string::npos &&
            metadata.find("\"lightingLook\": \"portfolioHero\"") != std::string::npos &&
            metadata.find("\"edgeInfluence\"") != std::string::npos &&
            metadata.find("\"ambientShadowCoupling\"") != std::string::npos &&
            metadata.find("\"cloudAppearance\"") != std::string::npos &&
            metadata.find("\"sceneContract\"") != std::string::npos &&
            metadata.find("\"groundSizeMeters\": [10000.000000, 10000.000000]") != std::string::npos &&
            metadata.find("\"buildingSizeMeters\": [20.000000, 60.000000, 20.000000]") != std::string::npos &&
            metadata.find("\"supportedRadiusMeters\": 50000.000000") != std::string::npos &&
            metadata.find("\"cloudScene\"") == std::string::npos &&
            metadata.find("\"domainStates\"") == std::string::npos &&
            metadata.find("\"similarityScale\"") == std::string::npos &&
            metadata.find("\"diagnosticSceneEnabled\"") == std::string::npos)
            schemaPassed = true;
    }
    if (!schemaPassed)
        return 10;

    WriteDiagnosticLine(
        "[UNIFIED-SCENE][GPU] CAMERAS=4 DIGITS=10 APPEARANCES=3 F5F6=FINITE_DISTINCT PIPELINE_INVARIANT=1 SCHEMA=37 PASS");
    return renderer.HasDebugLayerErrors() ? 11 : 0;
}

int RunStage13NoiseVolumeSmokeTest(Renderer& renderer, Camera& camera)
{
    renderer.SetNoiseLabVisible(false);
    renderer.EnableNoiseLabPreviews(false);
    renderer.SetVSyncEnabled(false);
    renderer.SetOpaqueSceneForTest(false);
    const NoiseVolumeParameters parameters = renderer.NoiseVolumeSettings();
    const std::uint64_t baseHash = renderer.BaseNoiseVolumeHash();
    const std::uint64_t detailHash = renderer.DetailNoiseVolumeHash();
    if (baseHash == 0 || detailHash == 0 || baseHash == detailHash ||
        !renderer.RegenerateNoiseVolumes() ||
        baseHash != renderer.BaseNoiseVolumeHash() ||
        detailHash != renderer.DetailNoiseVolumeHash())
        return 20;

    std::vector<std::uint8_t> baseBytes;
    std::vector<std::uint8_t> detailBytes;
    if (!renderer.ReadNoiseVolumeBytes(true, baseBytes) ||
        !renderer.ReadNoiseVolumeBytes(false, detailBytes) ||
        baseBytes.size() != stage13noise::kBaseBytes ||
        detailBytes.size() != stage13noise::kDetailBytes)
        return 21;
    const auto validateChannels = [](const std::vector<std::uint8_t>& bytes)
    {
        std::array<std::uint64_t, 4> hashes = {};
        const std::size_t count = bytes.size() / 4u;
        for (std::size_t channel = 0; channel < 4; ++channel)
        {
            double sum = 0.0, squareSum = 0.0;
            std::uint64_t hash = 1469598103934665603ull;
            for (std::size_t texel = 0; texel < count; ++texel)
            {
                const std::uint8_t byte = bytes[texel * 4u + channel];
                const double value = static_cast<double>(byte) / 255.0;
                sum += value;
                squareSum += value * value;
                hash ^= byte;
                hash *= 1099511628211ull;
            }
            const double mean = sum / count;
            const double deviation = std::sqrt(std::max(
                squareSum / count - mean * mean, 0.0));
            if (!std::isfinite(deviation) || deviation < 0.03)
                return false;
            hashes[channel] = hash;
        }
        return std::set<std::uint64_t>(hashes.begin(), hashes.end()).size() == 4u;
    };
    if (!validateChannels(baseBytes) || !validateChannels(detailBytes))
        return 22;

    // 실제 GPU R8 복셀을 같은 seed/좌표의 double CPU 기준과 직접 대조한다.
    const DirectX::XMUINT3 referenceVoxels[] = {
        { 3u, 11u, 29u }, { 37u, 61u, 17u },
        { 79u, 23u, 103u }, { 126u, 97u, 53u }
    };
    for (const DirectX::XMUINT3& voxel : referenceVoxels)
    {
        const stage13noise::Float3 uvw = {
            (static_cast<double>(voxel.x) + 0.5) / parameters.baseResolution,
            (static_cast<double>(voxel.y) + 0.5) / parameters.baseResolution,
            (static_cast<double>(voxel.z) + 0.5) / parameters.baseResolution
        };
        const std::size_t byteIndex =
            ((static_cast<std::size_t>(voxel.z) * parameters.baseResolution +
              voxel.y) * parameters.baseResolution + voxel.x) * 4u;
        const double gpuValue = static_cast<double>(baseBytes[byteIndex]) / 255.0;
        const double cpuValue = stage13noise::BasePerlinWorley(uvw, parameters);
        if (std::abs(gpuValue - cpuValue) > 2.0 / 255.0)
            return 31;
    }

    const std::uint64_t parameterHash = noisevolumecache::HashBytes(
        &parameters, sizeof(parameters));
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        (L"VolumetricCloud-Stage13NoiseVolumeSmoke-" +
         std::to_wstring(GetCurrentProcessId()));
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    const std::filesystem::path basePath = directory / L"base.vcnoise3";
    const std::filesystem::path detailPath = directory / L"detail.vcnoise3";
    std::vector<std::uint8_t> loadedBase, loadedDetail;
    const bool cachePassed = !error && noisevolumecache::Save(
        basePath, noisevolumecache::Kind::Base, parameters.baseResolution,
        parameterHash, baseBytes) && noisevolumecache::Save(
        detailPath, noisevolumecache::Kind::Detail, parameters.detailResolution,
        parameterHash, detailBytes) && noisevolumecache::Load(
        basePath, noisevolumecache::Kind::Base, parameters.baseResolution,
        parameterHash, loadedBase) && noisevolumecache::Load(
        detailPath, noisevolumecache::Kind::Detail, parameters.detailResolution,
        parameterHash, loadedDetail) && loadedBase == baseBytes &&
        loadedDetail == detailBytes;
    std::filesystem::remove(basePath, error);
    std::filesystem::remove(detailPath, error);
    std::filesystem::remove(directory, error);
    if (!cachePassed)
        return 23;

    if (!renderer.ApplyStage13SimilarityScale(10.0f) ||
        renderer.CurrentNoiseSource() != NoiseSource::ProceduralLegacy ||
        !renderer.ApplyStage13OpenWorldPreset() ||
        renderer.CurrentNoiseSource() != NoiseSource::Texture3D)
        return 24;

    const std::array<Stage13CameraPreset, 5> cameras = {{
        stage13camera::Get(Stage13CameraPresetId::HeroDepth),
        stage13camera::Get(Stage13CameraPresetId::GroundHorizon),
        stage13camera::Get(Stage13CameraPresetId::InsideLayer),
        stage13camera::Get(Stage13CameraPresetId::AboveLayer),
        { "TileWrap",
          { stage13noise::kBaseHorizontalWorldSizeMeters - 1200.0f, 4500.0f, 0.0f },
          { stage13noise::kBaseHorizontalWorldSizeMeters, 4500.0f, 0.0f } },
    }};
    const CloudDebugMode modes[] = {
        CloudDebugMode::BaseVolumeR, CloudDebugMode::BaseVolumeCombined,
        CloudDebugMode::DetailVolumeCombined, CloudDebugMode::FinalDensity,
        CloudDebugMode::Composite, CloudDebugMode::WeatherThicknessPotential,
        CloudDebugMode::LocalThickness, CloudDebugMode::LocalHeightFraction
    };
    bool observedNonConstantLocalTop = false;
    for (const Stage13CameraPreset& preset : cameras)
    {
        camera.SetClipPlanes(0.1f, 60000.0f);
        camera.SetLookAt(preset.position, preset.target);
        std::set<std::uint64_t> hashes;
        std::set<std::uint64_t> heightHashes;
        for (CloudDebugMode mode : modes)
        {
            CloudDiagnosticFrame frame;
            if (!renderer.CaptureCloudDiagnosticFrame(camera, 0.0f, mode, frame))
                return 25;
            std::uint64_t hash = 1469598103934665603ull;
            for (const DirectX::XMFLOAT4& pixel : frame.pixels)
            {
                if (!std::isfinite(pixel.x) || !std::isfinite(pixel.y) ||
                    !std::isfinite(pixel.z) || !std::isfinite(pixel.w))
                    return 26;
                const auto* bytes = reinterpret_cast<const std::uint8_t*>(&pixel);
                for (std::size_t index = 0; index < sizeof(pixel); ++index)
                {
                    hash ^= bytes[index];
                    hash *= 1099511628211ull;
                }
            }
            hashes.insert(hash);
            if (mode == CloudDebugMode::WeatherThicknessPotential ||
                mode == CloudDebugMode::LocalThickness ||
                mode == CloudDebugMode::LocalHeightFraction)
                heightHashes.insert(hash);
            if (mode == CloudDebugMode::LocalThickness)
            {
                float minimum = 1.0f;
                float maximum = 0.0f;
                for (const DirectX::XMFLOAT4& pixel : frame.pixels)
                {
                    if (pixel.x > 0.0f)
                    {
                        minimum = std::min(minimum, pixel.x);
                        maximum = std::max(maximum, pixel.x);
                    }
                }
                observedNonConstantLocalTop |= maximum - minimum >= 0.05f;
            }
        }
        // F8 로컬 상공 구도에서는 빈 하늘을 나타내는 일부 density/detail 출력이
        // 같은 검정 hash일 수 있다. Height 3종은 반드시 구분하고 전체 8종 중
        // 최소 6종을 구분해 실제 Texture3D/Weather 경로가 살아 있음을 검사한다.
        if (hashes.size() < 6u || heightHashes.size() != 3u)
        {
            WriteDiagnosticLine(std::string("[NOISE3D][CAMERA][") +
                preset.diagnosticName + "] DISTINCT_HASHES=" +
                std::to_string(hashes.size()) + " HEIGHT_HASHES=" +
                std::to_string(heightHashes.size()) + " FAIL");
            return 27;
        }
        WriteDiagnosticLine(std::string("[NOISE3D][CAMERA][") + preset.diagnosticName +
                            "] FINITE DISTINCT PASS");
    }
    if (!observedNonConstantLocalTop)
        return 32;
    WriteDiagnosticLine("[NOISE3D][LOCAL-THICKNESS] RANGE>=0.05 PASS");
    camera.SetOrbit(-1.570796f, 0.0f, 1200.0f,
                    { stage13noise::kBaseHorizontalWorldSizeMeters, 4500, 0 });
    CloudDiagnosticFrame wrap;
    if (!renderer.CaptureCloudDiagnosticFrame(
            camera, 0.0f, CloudDebugMode::TextureWrapDifference, wrap))
        return 28;
    float wrapDebugMaximum = 0.0f;
    for (const DirectX::XMFLOAT4& pixel : wrap.pixels)
        wrapDebugMaximum = std::max(wrapDebugMaximum,
            std::max(pixel.x, std::max(pixel.y, pixel.z)));
    // 셰이더는 육안 진단을 위해 실제 seam 차이를 255배 확대해서 출력한다.
    const float wrapMaximum = wrapDebugMaximum / 255.0f;
    constexpr float kWrapTolerance = 1.0f / 1024.0f;
    {
        std::ostringstream wrapLine;
        wrapLine << std::fixed << std::setprecision(8)
                 << "[NOISE3D][SEAM] VALUE_MAX=" << wrapMaximum
                 << (wrapMaximum <= kWrapTolerance ? " PASS" : " FAIL");
        WriteDiagnosticLine(wrapLine.str());
    }
    if (wrapMaximum > kWrapTolerance)
        return 29;

    // 일반 실행과 같은 t2/t3/t4 + b1/b2/b6 Noise Lab 단면 바인딩도 한 프레임 실행한다.
    renderer.EnableNoiseLabPreviews(true);
    renderer.Render(camera, 0.0f);
    renderer.EnableNoiseLabPreviews(false);
    if (renderer.HasDebugLayerErrors())
        return 30;

    std::ostringstream baseLine;
    baseLine << "[NOISE3D][BASE] SIZE=128 FORMAT=RGBA8 WORLD_XZ_M="
             << parameters.baseWorldSizeMeters << " WORLD_Y_M="
             << parameters.baseVerticalWorldSizeMeters << " BYTES="
             << baseBytes.size() << " HASH=" << std::hex << baseHash << " PASS";
    WriteDiagnosticLine(baseLine.str());
    std::ostringstream detailLine;
    detailLine << "[NOISE3D][DETAIL] SIZE=32 FORMAT=RGBA8 WORLD_M=2000 BYTES="
               << detailBytes.size() << " HASH=" << std::hex << detailHash << " PASS";
    WriteDiagnosticLine(detailLine.str());
    WriteDiagnosticLine("[NOISE3D][CACHE] SAVE_LOAD_HASH_MATCH PASS");
    std::ostringstream gpuLine;
    gpuLine << std::fixed << std::setprecision(3)
            << "[NOISE3D][GPU] GENERATION_MS="
            << renderer.NoiseVolumeGenerationMilliseconds() << " REPORT";
    WriteDiagnosticLine(gpuLine.str());
    return 0;
}

double FramePercentile(std::vector<double> values, double percentile)
{
    if (values.empty())
        return 0.0;
    std::sort(values.begin(), values.end());
    const std::size_t index = static_cast<std::size_t>(std::clamp(
        percentile, 0.0, 1.0) * static_cast<double>(values.size() - 1));
    return values[index];
}

double FrameStandardDeviation(const std::vector<double>& values)
{
    if (values.empty())
        return 0.0;
    const double mean = std::accumulate(values.begin(), values.end(), 0.0) /
        static_cast<double>(values.size());
    double sum = 0.0;
    for (double value : values)
        sum += (value - mean) * (value - mean);
    return std::sqrt(sum / static_cast<double>(values.size()));
}

std::uint64_t HashDiagnosticFrame(const CloudDiagnosticFrame& frame)
{
    std::uint64_t hash = 1469598103934665603ull;
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(frame.pixels.data());
    const std::size_t size = frame.pixels.size() * sizeof(DirectX::XMFLOAT4);
    for (std::size_t index = 0; index < size; ++index)
    {
        hash ^= bytes[index];
        hash *= 1099511628211ull;
    }
    return hash;
}

double DiagnosticFrameMeanAbsoluteError(const CloudDiagnosticFrame& first,
                                        const CloudDiagnosticFrame& second)
{
    if (first.width != second.width || first.height != second.height ||
        first.pixels.size() != second.pixels.size() || first.pixels.empty())
        return std::numeric_limits<double>::infinity();
    double sum = 0.0;
    for (std::size_t index = 0; index < first.pixels.size(); ++index)
    {
        sum += std::abs(static_cast<double>(first.pixels[index].x) -
                       static_cast<double>(second.pixels[index].x));
        sum += std::abs(static_cast<double>(first.pixels[index].y) -
                       static_cast<double>(second.pixels[index].y));
        sum += std::abs(static_cast<double>(first.pixels[index].z) -
                       static_cast<double>(second.pixels[index].z));
    }
    return sum / static_cast<double>(first.pixels.size() * 3u);
}

int RunStage10UpsamplingSmokeTest(Renderer& renderer, Camera& camera)
{
    renderer.SetNoiseLabVisible(false);
    renderer.EnableNoiseLabPreviews(false);
    renderer.SetVSyncEnabled(false);
    renderer.SetOpaqueSceneForTest(true);
    if (!renderer.ApplyStage13OpenWorldPreset() ||
        !renderer.ApplyCloudAppearancePreset(CloudAppearancePreset::Stratus))
        return 2;
    renderer.ApplyStage9OptimizationPreset(Stage9OptimizationPreset::Balanced);
    camera.SetClipPlanes(0.1f, 60000.0f);
    camera.SetOrbit(0.0f, 0.0f, 32000.0f,
                    { 0.0f, 4500.0f, 0.0f });

    renderer.ApplyStage10ResolutionPreset(Stage10ResolutionPreset::Full);
    renderer.SetStage10UpsampleFilter(Stage10UpsampleFilter::Nearest);
    CloudDiagnosticFrame direct;
    CloudDiagnosticFrame split;
    if (!renderer.CaptureCloudDiagnosticFrame(
            camera, 0.0f, CloudDebugMode::Composite, direct, true) ||
        !renderer.CaptureCloudDiagnosticFrame(
            camera, 0.0f, CloudDebugMode::Composite, split))
        return 3;
    const double fullMae = DiagnosticFrameMeanAbsoluteError(direct, split);
    bool finite = std::isfinite(fullMae) && fullMae <= 0.01;
    std::set<std::uint64_t> hashes;

    for (Stage10ResolutionPreset resolution :
         stage10upsampling::kRuntimeResolutionCandidates)
    {
        renderer.ApplyStage10ResolutionPreset(resolution);
        for (Stage10UpsampleFilter filter :
             stage10upsampling::kRuntimeFilterCandidates)
        {
            renderer.SetStage10UpsampleFilter(filter);
            CloudDiagnosticFrame frame;
            if (!renderer.CaptureCloudDiagnosticFrame(
                    camera, 0.0f, CloudDebugMode::Composite, frame))
                return 4;
            for (const DirectX::XMFLOAT4& pixel : frame.pixels)
            {
                finite = finite && std::isfinite(pixel.x) &&
                    std::isfinite(pixel.y) && std::isfinite(pixel.z) &&
                    std::isfinite(pixel.w);
            }
            hashes.insert(HashDiagnosticFrame(frame));
        }
    }

    renderer.ApplyStage10ResolutionPreset(Stage10ResolutionPreset::Half);
    renderer.SetStage10UpsampleFilter(Stage10UpsampleFilter::Joint4);
    for (CloudDebugMode mode : {
             CloudDebugMode::LowResolutionGrid,
             CloudDebugMode::UpsampleSceneRejection,
             CloudDebugMode::UpsampleCloudDepthWeight,
             CloudDebugMode::UpsampleTransmittanceWeight,
             CloudDebugMode::UpsampleAcceptedTapCount })
    {
        CloudDiagnosticFrame frame;
        if (!renderer.CaptureCloudDiagnosticFrame(camera, 0.0f, mode, frame))
            return 5;
        for (const DirectX::XMFLOAT4& pixel : frame.pixels)
            finite = finite && std::isfinite(pixel.x) &&
                std::isfinite(pixel.y) && std::isfinite(pixel.z) &&
                std::isfinite(pixel.w);
    }

    // Geometry/Sky 경계의 hard rejection은 유지하되, 경계에서 충분히 떨어진
    // 순수 Sky는 Joint4 후보를 과도하게 버리지 않아야 한다. ID 80은
    // accepted tap 수를 0~1(=0~4 tap)로 내보낸다.
    SceneDepthDiagnosticFrame sceneDepth;
    CloudDiagnosticFrame acceptedTapFrame;
    if (!renderer.CaptureSceneDepthDiagnosticFrame(camera, sceneDepth) ||
        !renderer.CaptureCloudDiagnosticFrame(
            camera, 0.0f, CloudDebugMode::UpsampleAcceptedTapCount,
            acceptedTapFrame) ||
        sceneDepth.width != acceptedTapFrame.width ||
        sceneDepth.height != acceptedTapFrame.height)
        return 9;
    const auto isSky = [&](int x, int y)
    {
        const std::size_t index = static_cast<std::size_t>(y) *
            static_cast<std::size_t>(sceneDepth.width) +
            static_cast<std::size_t>(x);
        return sceneDepth.deviceDepth[index] >= 0.999999f;
    };
    std::size_t pureSkyPixels = 0;
    double acceptedTapSum = 0.0;
    for (int y = 2; y + 2 < sceneDepth.height; ++y)
    {
        for (int x = 2; x + 2 < sceneDepth.width; ++x)
        {
            bool pureSky = true;
            for (int oy = -2; oy <= 2 && pureSky; ++oy)
            {
                for (int ox = -2; ox <= 2; ++ox)
                {
                    if (!isSky(x + ox, y + oy))
                    {
                        pureSky = false;
                        break;
                    }
                }
            }
            if (!pureSky)
                continue;
            const std::size_t index = static_cast<std::size_t>(y) *
                static_cast<std::size_t>(acceptedTapFrame.width) +
                static_cast<std::size_t>(x);
            acceptedTapSum += static_cast<double>(
                acceptedTapFrame.pixels[index].x) * 4.0;
            ++pureSkyPixels;
        }
    }
    const double pureSkyAcceptedTapAverage = pureSkyPixels == 0
        ? 0.0 : acceptedTapSum / static_cast<double>(pureSkyPixels);
    const bool joint4SkyPass = pureSkyPixels >= 64u &&
        std::isfinite(pureSkyAcceptedTapAverage) &&
        pureSkyAcceptedTapAverage >= 2.0;
    const bool samplerContract = renderer.ValidateNoiseSamplerContract();

    // 비활성 2/3 enum도 schema 32 호환을 유지하며, 홀수 창 크기에서 ceil 축 크기와
    // RTV/SRV 재생성이 같은 프레임에 적용되는지 회귀한다.
    renderer.Resize(97, 55);
    camera.SetAspect(97.0f / 55.0f);
    renderer.ApplyStage10ResolutionPreset(
        Stage10ResolutionPreset::TwoThirds);
    CloudDiagnosticFrame resized;
    if (!renderer.CaptureCloudDiagnosticFrame(
            camera, 0.0f, CloudDebugMode::Composite, resized) ||
        renderer.CloudRenderWidth() != 65 ||
        renderer.CloudRenderHeight() != 37)
        return 7;
    for (const DirectX::XMFLOAT4& pixel : resized.pixels)
        finite = finite && std::isfinite(pixel.x) &&
            std::isfinite(pixel.y) && std::isfinite(pixel.z) &&
            std::isfinite(pixel.w);

    renderer.Resize(96, 54);
    camera.SetAspect(96.0f / 54.0f);
    renderer.ApplyStage10ResolutionPreset(Stage10ResolutionPreset::Half);
    CloudDiagnosticFrame restored;
    if (!renderer.CaptureCloudDiagnosticFrame(
            camera, 0.0f, CloudDebugMode::Composite, restored))
        return 8;

    std::ostringstream line;
    line << std::fixed << std::setprecision(6)
         << "STAGE10_UPSAMPLING FULL_MAE=" << fullMae
         << " DISTINCT=" << hashes.size()
         << " SKY_PIXELS=" << pureSkyPixels
         << " SKY_ACCEPTED_TAPS=" << pureSkyAcceptedTapAverage
         << " SAMPLER=" << (samplerContract ? "PASS" : "FAIL")
         << " TARGET=" << renderer.CloudRenderWidth() << 'x'
         << renderer.CloudRenderHeight() << ' '
         << (finite && hashes.size() >= 4 && joint4SkyPass &&
             samplerContract ? "PASS" : "FAIL");
    WriteDiagnosticLine(line.str());
    return finite && hashes.size() >= 4 && joint4SkyPass && samplerContract &&
        !renderer.HasDebugLayerErrors()
        ? 0 : 6;
}

int RunStage11TemporalSmokeTest(Renderer& renderer, Camera& camera)
{
    WriteDiagnosticLine("[STAGE11][SETUP] BEGIN");
    renderer.SetNoiseLabVisible(false);
    renderer.EnableNoiseLabPreviews(false);
    renderer.SetVSyncEnabled(false);
    renderer.SetOpaqueSceneForTest(true);
    if (!renderer.ApplyStage13OpenWorldPreset() ||
        !renderer.ApplyCloudAppearancePreset(CloudAppearancePreset::Stratus))
        return 2;
    renderer.ApplyStage9OptimizationPreset(Stage9OptimizationPreset::Balanced);
    renderer.ApplyStage10ResolutionPreset(Stage10ResolutionPreset::Half);
    renderer.SetStage10UpsampleFilter(Stage10UpsampleFilter::Joint4);
    const CloudParameters appearance = renderer.CloudSettings();
    renderer.SetCloudWindSpeedsForValidation(0.0f, 0.0f, 0.0f);
    camera.SetClipPlanes(
        stage13camera::kNearPlaneMeters, stage13camera::kFarPlaneMeters);
    const Stage13CameraPreset& hero = stage13camera::Get(
        Stage13CameraPresetId::HeroDepth);
    camera.SetLookAt(hero.position, hero.target);
    renderer.SetStage11TemporalMode(Stage11TemporalMode::Stable4Phase);

    bool finite = true;
    SceneDepthDiagnosticFrame sceneDepth;
    if (!renderer.CaptureSceneDepthDiagnosticFrame(camera, sceneDepth) ||
        sceneDepth.deviceDepth.empty())
        return 3;
    WriteDiagnosticLine("[STAGE11][SETUP] SCENE_DEPTH_READY");

    const std::size_t geometryPixelCount = static_cast<std::size_t>(std::count_if(
        sceneDepth.deviceDepth.begin(), sceneDepth.deviceDepth.end(),
        [](float depth) { return depth < 0.999999f; }));
    const auto geometryAt = [&](int x, int y)
    {
        const std::size_t index = static_cast<std::size_t>(y) *
            static_cast<std::size_t>(sceneDepth.width) +
            static_cast<std::size_t>(x);
        return sceneDepth.deviceDepth[index] < 0.999999f;
    };
    std::vector<double> firstGeometryRows;
    firstGeometryRows.reserve(static_cast<std::size_t>(sceneDepth.width));
    for (int x = 0; x < sceneDepth.width; ++x)
    {
        for (int y = 0; y < sceneDepth.height; ++y)
        {
            if (!geometryAt(x, y))
                continue;
            firstGeometryRows.push_back(static_cast<double>(y));
            break;
        }
    }
    // F5의 지면/하늘 horizon은 화면 전체에 걸친 가장 큰 경계다. 사용자가
    // 보고한 대상은 그보다 위로 솟은 진단 box이므로 각 열의 첫 geometry
    // 중앙값보다 위쪽인 silhouette만 건물+인접 하늘 경계 마스크로 삼는다.
    const int groundHorizonRow = static_cast<int>(std::lround(
        FramePercentile(firstGeometryRows, 0.5)));
    std::vector<std::size_t> boundaryPixels;
    for (int y = 1; y + 1 < sceneDepth.height; ++y)
    {
        for (int x = 1; x + 1 < sceneDepth.width; ++x)
        {
            const bool centerGeometry = geometryAt(x, y);
            // 실제 한 픽셀 좌우/상하 silhouette만 센다. 대각선만 닿는
            // 3x3 corner 확장은 건물 경계가 아닌 주변 픽셀까지 섞는다.
            const bool boundary =
                geometryAt(x - 1, y) != centerGeometry ||
                geometryAt(x + 1, y) != centerGeometry ||
                geometryAt(x, y - 1) != centerGeometry ||
                geometryAt(x, y + 1) != centerGeometry;
            if (boundary && y < groundHorizonRow - 2)
                boundaryPixels.push_back(static_cast<std::size_t>(y) *
                    static_cast<std::size_t>(sceneDepth.width) +
                    static_cast<std::size_t>(x));
        }
    }

    const auto capturePhaseSequence = [&](Stage10UpsampleFilter filter,
                                          CloudDebugMode mode,
                                          std::array<CloudDiagnosticFrame, 4>& frames)
    {
        renderer.SetStage10UpsampleFilter(filter);
        renderer.SetStage11TemporalMode(Stage11TemporalMode::Off);
        renderer.SetStage11TemporalMode(Stage11TemporalMode::Stable4Phase);
        renderer.ResetTemporalHistory(Stage11HistoryResetReason::Manual);
        renderer.SetDebugMode(CloudDebugMode::Composite);
        for (std::uint32_t frameIndex = 0; frameIndex < 12; ++frameIndex)
        {
            renderer.Render(camera, 0.0f);
            if ((frameIndex & 3u) == 3u)
            {
                std::ostringstream warmupProgress;
                warmupProgress << "[STAGE11][WARMUP] FILTER="
                    << static_cast<std::uint32_t>(filter)
                    << " DEBUG=" << static_cast<std::uint32_t>(mode)
                    << " FRAMES=" << (frameIndex + 1u);
                WriteDiagnosticLine(warmupProgress.str());
            }
        }
        for (CloudDiagnosticFrame& frame : frames)
            if (!renderer.CaptureCloudDiagnosticFrame(camera, 0.0f, mode, frame))
                return false;
        std::ostringstream progress;
        progress << "[STAGE11][CAPTURE] FILTER="
                 << static_cast<std::uint32_t>(filter)
                 << " DEBUG=" << static_cast<std::uint32_t>(mode)
                 << " PASS";
        WriteDiagnosticLine(progress.str());
        return true;
    };
    const auto rgbRangesForPixels = [](
        const std::array<CloudDiagnosticFrame, 4>& frames,
        const std::vector<std::size_t>& selectedPixels)
    {
        std::vector<double> ranges;
        ranges.reserve(selectedPixels.size());
        for (std::size_t index : selectedPixels)
        {
            double minimum[3] = {
                std::numeric_limits<double>::infinity(),
                std::numeric_limits<double>::infinity(),
                std::numeric_limits<double>::infinity(),
            };
            double maximum[3] = {
                -std::numeric_limits<double>::infinity(),
                -std::numeric_limits<double>::infinity(),
                -std::numeric_limits<double>::infinity(),
            };
            for (const CloudDiagnosticFrame& frame : frames)
            {
                const DirectX::XMFLOAT4& pixel = frame.pixels[index];
                const double values[3] = { pixel.x, pixel.y, pixel.z };
                for (std::size_t channel = 0; channel < 3; ++channel)
                {
                    minimum[channel] = std::min(minimum[channel], values[channel]);
                    maximum[channel] = std::max(maximum[channel], values[channel]);
                }
            }
            ranges.push_back(
                ((maximum[0] - minimum[0]) +
                 (maximum[1] - minimum[1]) +
                 (maximum[2] - minimum[2])) / 3.0);
        }
        return ranges;
    };
    const auto nearColor = [](const DirectX::XMFLOAT4& pixel,
                              const DirectX::XMFLOAT3& color)
    {
        return std::abs(pixel.x - color.x) <= 0.02f &&
            std::abs(pixel.y - color.y) <= 0.02f &&
            std::abs(pixel.z - color.z) <= 0.02f;
    };
    static constexpr std::array<DirectX::XMFLOAT3, 10> validityPalette = {{
        { 0.10f, 1.00f, 0.20f }, { 0.20f, 0.20f, 0.20f },
        { 1.00f, 0.00f, 1.00f }, { 0.00f, 0.35f, 1.00f },
        { 0.00f, 1.00f, 1.00f }, { 1.00f, 0.45f, 0.00f },
        { 1.00f, 1.00f, 0.00f }, { 1.00f, 0.00f, 0.00f },
        { 0.55f, 0.20f, 1.00f }, { 1.00f, 0.10f, 0.50f },
    }};
    static constexpr std::array<DirectX::XMFLOAT3, 5> currentPalette = {{
        { 0.10f, 1.00f, 0.20f }, { 1.00f, 0.00f, 0.00f },
        { 1.00f, 1.00f, 0.00f }, { 0.00f, 0.20f, 1.00f },
        { 0.35f, 0.35f, 0.35f },
    }};
    const Stage10UpsampleFilter filters[] = {
        Stage10UpsampleFilter::Nearest,
        Stage10UpsampleFilter::Bilinear,
        Stage10UpsampleFilter::Joint4,
    };
    double boundaryMean = 0.0;
    double boundaryP99 = 0.0;
    double weightP99 = 0.0;
    bool stationaryBoundaryPassed = boundaryPixels.size() >= 8u;
    bool weightPassed = true;
    bool currentPalettePassed = true;
    bool validityPalettePassed = true;
    bool differenceContractPassed = true;
    std::size_t acceptedPixels = 0;
    std::size_t heldHistoryPixels = 0;
    for (Stage10UpsampleFilter filter : filters)
    {
        std::array<CloudDiagnosticFrame, 4> frames;
        std::array<std::vector<float>, 4> boundaryWeights;
        for (std::vector<float>& phaseWeights : boundaryWeights)
            phaseWeights.reserve(boundaryPixels.size());
        std::array<std::size_t, 5> unstableCurrentColors{};
        std::array<std::size_t, 10> unstableHistoryColors{};
        if (!capturePhaseSequence(filter, CloudDebugMode::Composite, frames))
            return 4;
        const std::vector<double> ranges = rgbRangesForPixels(
            frames, boundaryPixels);
        const double mean = ranges.empty()
            ? std::numeric_limits<double>::infinity()
            : std::accumulate(ranges.begin(), ranges.end(), 0.0) /
              static_cast<double>(ranges.size());
        const double p99 = FramePercentile(ranges, 0.99);
        boundaryMean = std::max(boundaryMean, mean);
        boundaryP99 = std::max(boundaryP99, p99);
        stationaryBoundaryPassed = stationaryBoundaryPassed &&
            mean <= 0.01 && p99 <= 0.03;

        if (!capturePhaseSequence(
                filter, CloudDebugMode::TemporalHistoryWeight, frames))
            return 5;
        std::vector<double> weightRanges;
        weightRanges.reserve(boundaryPixels.size());
        for (std::size_t index : boundaryPixels)
        {
            double minimum = std::numeric_limits<double>::infinity();
            double maximum = -std::numeric_limits<double>::infinity();
            for (std::size_t phase = 0; phase < frames.size(); ++phase)
            {
                const CloudDiagnosticFrame& frame = frames[phase];
                minimum = std::min(minimum,
                    static_cast<double>(frame.pixels[index].x));
                maximum = std::max(maximum,
                    static_cast<double>(frame.pixels[index].x));
                boundaryWeights[phase].push_back(frame.pixels[index].x);
            }
            const int boundaryX = static_cast<int>(
                index % static_cast<std::size_t>(sceneDepth.width));
            const int boundaryY = static_cast<int>(
                index / static_cast<std::size_t>(sceneDepth.width));
            // Weight 안정성은 실제로 떨렸던 opaque 건물 픽셀에서 판정한다.
            // 인접 sky는 구름 자체의 depth/T disocclusion으로 0 weight가 되는
            // 것이 정상일 수 있으므로 Composite 양면 경계 검사와 분리한다.
            if (geometryAt(boundaryX, boundaryY))
                weightRanges.push_back(maximum - minimum);
        }
        const double filterWeightP99 = FramePercentile(weightRanges, 0.99);
        weightP99 = std::max(weightP99, filterWeightP99);
        weightPassed = weightPassed && filterWeightP99 <= 0.05;

        if (!capturePhaseSequence(
                filter, CloudDebugMode::TemporalCurrentSourceValidity, frames))
            return 6;
        for (const CloudDiagnosticFrame& frame : frames)
        for (const DirectX::XMFLOAT4& pixel : frame.pixels)
        {
            bool matched = false;
            for (const DirectX::XMFLOAT3& color : currentPalette)
                matched = matched || nearColor(pixel, color);
            currentPalettePassed = currentPalettePassed && matched;
            heldHistoryPixels += nearColor(pixel, currentPalette[4]) ? 1u : 0u;
            finite = finite && std::isfinite(pixel.x) &&
                std::isfinite(pixel.y) && std::isfinite(pixel.z) &&
                std::isfinite(pixel.w);
        }
        for (std::size_t phase = 0; phase < frames.size(); ++phase)
        for (std::size_t boundaryIndex = 0;
             boundaryIndex < boundaryPixels.size(); ++boundaryIndex)
        {
            float minimum = boundaryWeights[0][boundaryIndex];
            float maximum = minimum;
            for (std::size_t weightPhase = 1; weightPhase < 4; ++weightPhase)
            {
                minimum = std::min(minimum,
                    boundaryWeights[weightPhase][boundaryIndex]);
                maximum = std::max(maximum,
                    boundaryWeights[weightPhase][boundaryIndex]);
            }
            if (maximum - minimum <= 0.05f)
                continue;
            const DirectX::XMFLOAT4& pixel =
                frames[phase].pixels[boundaryPixels[boundaryIndex]];
            for (std::size_t color = 0; color < currentPalette.size(); ++color)
                unstableCurrentColors[color] +=
                    nearColor(pixel, currentPalette[color]) ? 1u : 0u;
        }

        if (!capturePhaseSequence(
                filter, CloudDebugMode::TemporalHistoryValidity, frames))
            return 7;
        for (const CloudDiagnosticFrame& frame : frames)
        for (const DirectX::XMFLOAT4& pixel : frame.pixels)
        {
            bool matched = false;
            for (const DirectX::XMFLOAT3& color : validityPalette)
                matched = matched || nearColor(pixel, color);
            validityPalettePassed = validityPalettePassed && matched;
            acceptedPixels += nearColor(pixel, validityPalette[0]) ? 1u : 0u;
        }
        for (std::size_t phase = 0; phase < frames.size(); ++phase)
        for (std::size_t boundaryIndex = 0;
             boundaryIndex < boundaryPixels.size(); ++boundaryIndex)
        {
            float minimum = boundaryWeights[0][boundaryIndex];
            float maximum = minimum;
            for (std::size_t weightPhase = 1; weightPhase < 4; ++weightPhase)
            {
                minimum = std::min(minimum,
                    boundaryWeights[weightPhase][boundaryIndex]);
                maximum = std::max(maximum,
                    boundaryWeights[weightPhase][boundaryIndex]);
            }
            if (maximum - minimum <= 0.05f)
                continue;
            const DirectX::XMFLOAT4& pixel =
                frames[phase].pixels[boundaryPixels[boundaryIndex]];
            for (std::size_t color = 0; color < validityPalette.size(); ++color)
                unstableHistoryColors[color] +=
                    nearColor(pixel, validityPalette[color]) ? 1u : 0u;
        }

        if (!capturePhaseSequence(filter,
                CloudDebugMode::TemporalCurrentHistoryDifference, frames))
            return 8;
        for (const CloudDiagnosticFrame& frame : frames)
        for (const DirectX::XMFLOAT4& pixel : frame.pixels)
        {
            finite = finite && std::isfinite(pixel.x) &&
                std::isfinite(pixel.y) && std::isfinite(pixel.z) &&
                std::isfinite(pixel.w);
            differenceContractPassed = differenceContractPassed &&
                pixel.x >= 0.0f && pixel.x <= 1.0f &&
                pixel.y >= 0.0f && pixel.y <= 1.0f &&
                (std::abs(pixel.z) <= 0.02f ||
                 std::abs(pixel.z - 1.0f) <= 0.02f);
        }
        std::ostringstream filterLine;
        filterLine << std::fixed << std::setprecision(6)
                   << "[STAGE11][FILTER] ID="
                   << static_cast<std::uint32_t>(filter)
                   << " BOUNDARY_MEAN=" << mean
                   << " BOUNDARY_P99=" << p99
                   << " WEIGHT_P99=" << filterWeightP99
                   << " CURRENT_GRY=" << unstableCurrentColors[4]
                   << " CURRENT_GRN=" << unstableCurrentColors[0]
                   << " HISTORY_GRN=" << unstableHistoryColors[0]
                   << " HISTORY_SCENE=" << unstableHistoryColors[5]
                   << " HISTORY_CLOUD=" << unstableHistoryColors[6]
                   << " HISTORY_T=" << unstableHistoryColors[7];
        WriteDiagnosticLine(filterLine.str());
    }
    validityPalettePassed = validityPalettePassed && acceptedPixels > 0u;

    // F5 silhouette만으로는 F8의 넓은 사선 평면에서 생기는 perspective
    // depth gradient를 재현하지 못했다. F8 기본 방향과 작은 yaw/pitch를
    // 별도 fixture로 고정해 같은 회귀가 다시 숨어들지 않게 한다.
    const Stage13CameraPreset& above = stage13camera::Get(
        Stage13CameraPresetId::AboveLayer);
    const auto setAboveStressCamera = [&](float yawDegrees,
                                           float pitchDegrees)
    {
        using namespace DirectX;
        const XMVECTOR position = XMLoadFloat3(&above.position);
        XMVECTOR direction = XMVectorSubtract(
            XMLoadFloat3(&above.target), position);
        const float distance = XMVectorGetX(XMVector3Length(direction));
        direction = XMVector3Normalize(direction);
        direction = XMVector3TransformNormal(direction,
            XMMatrixRotationY(XMConvertToRadians(yawDegrees)));
        XMVECTOR right = XMVector3Normalize(XMVector3Cross(
            XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), direction));
        direction = XMVector3Rotate(direction, XMQuaternionRotationAxis(
            right, XMConvertToRadians(pitchDegrees)));
        XMFLOAT3 target{};
        XMStoreFloat3(&target, XMVectorMultiplyAdd(
            direction, XMVectorReplicate(distance), position));
        camera.SetLookAt(above.position, target);
    };
    const auto buildPlanarInteriorMask = [&](const SceneDepthDiagnosticFrame& depth,
                                              std::vector<std::size_t>& pixels,
                                              std::vector<std::uint8_t>& mask)
    {
        pixels.clear();
        mask.assign(depth.deviceDepth.size(), 0u);
        const auto at = [&](int x, int y)
        {
            return depth.deviceDepth[static_cast<std::size_t>(y) *
                static_cast<std::size_t>(depth.width) +
                static_cast<std::size_t>(x)];
        };
        for (int y = 2; y + 2 < depth.height; ++y)
        for (int x = 2; x + 2 < depth.width; ++x)
        {
            bool geometry5x5 = true;
            for (int oy = -2; oy <= 2 && geometry5x5; ++oy)
            for (int ox = -2; ox <= 2; ++ox)
            {
                const float value = at(x + ox, y + oy);
                geometry5x5 = geometry5x5 && std::isfinite(value) &&
                    value >= 0.0f && value < 0.999999f;
            }
            if (!geometry5x5)
                continue;
            const float center = at(x, y);
            const float curvature =
                std::abs(at(x - 1, y) - 2.0f * center + at(x + 1, y)) +
                std::abs(at(x, y - 1) - 2.0f * center + at(x, y + 1));
            if (curvature > 4.0e-6f)
                continue;
            const std::size_t index = static_cast<std::size_t>(y) *
                static_cast<std::size_t>(depth.width) +
                static_cast<std::size_t>(x);
            pixels.push_back(index);
            mask[index] = 1u;
        }
    };
    const auto longestHorizontalRun = [](
        const CloudDiagnosticFrame& frame,
        const std::vector<std::uint8_t>& mask,
        const auto& predicate)
    {
        std::size_t longest = 0u;
        for (int y = 0; y < frame.height; ++y)
        {
            std::size_t run = 0u;
            for (int x = 0; x < frame.width; ++x)
            {
                const std::size_t index = static_cast<std::size_t>(y) *
                    static_cast<std::size_t>(frame.width) +
                    static_cast<std::size_t>(x);
                if (mask[index] != 0u && predicate(frame.pixels[index]))
                {
                    ++run;
                    longest = std::max(longest, run);
                }
                else
                {
                    run = 0u;
                }
            }
        }
        return longest;
    };

    struct F8Angle { float yaw; float pitch; };
    const F8Angle f8Angles[] = {
        { 0.0f, 0.0f }, { -3.0f, 0.0f }, { 3.0f, 0.0f },
        { 0.0f, -2.0f }, { 0.0f, 2.0f },
    };
    bool f8PlanePassed = true;
    double f8CompositeMean = 0.0;
    double f8CompositeP99 = 0.0;
    double f8NonGreenRatio = 0.0;
    std::size_t f8InvalidRun = 0u;
    for (const F8Angle& angle : f8Angles)
    {
        setAboveStressCamera(angle.yaw, angle.pitch);
        SceneDepthDiagnosticFrame f8Depth;
        if (!renderer.CaptureSceneDepthDiagnosticFrame(camera, f8Depth))
            return 11;
        std::vector<std::size_t> planarPixels;
        std::vector<std::uint8_t> planarMask;
        buildPlanarInteriorMask(f8Depth, planarPixels, planarMask);
        if (planarPixels.size() < 10000u)
            return 12;

        for (Stage10UpsampleFilter filter : filters)
        {
            std::array<CloudDiagnosticFrame, 4> frames;
            if (!capturePhaseSequence(filter, CloudDebugMode::Composite,
                                      frames))
                return 13;
            const std::vector<double> ranges = rgbRangesForPixels(
                frames, planarPixels);
            const double mean = std::accumulate(
                ranges.begin(), ranges.end(), 0.0) /
                static_cast<double>(ranges.size());
            const double p99 = FramePercentile(ranges, 0.99);
            f8CompositeMean = std::max(f8CompositeMean, mean);
            f8CompositeP99 = std::max(f8CompositeP99, p99);
            f8PlanePassed = f8PlanePassed && mean <= 0.01 && p99 <= 0.03;

            if (!capturePhaseSequence(filter,
                    CloudDebugMode::TemporalCurrentSourceValidity, frames))
                return 14;
            std::size_t nonGreen = 0u;
            for (const CloudDiagnosticFrame& frame : frames)
            {
                for (std::size_t index : planarPixels)
                    nonGreen += nearColor(frame.pixels[index],
                        currentPalette[0]) ? 0u : 1u;
                f8InvalidRun = std::max(f8InvalidRun,
                    longestHorizontalRun(frame, planarMask,
                        [&](const DirectX::XMFLOAT4& value)
                        {
                            return !nearColor(value, currentPalette[0]);
                        }));
            }
            const double nonGreenRatio = static_cast<double>(nonGreen) /
                static_cast<double>(planarPixels.size() * frames.size());
            f8NonGreenRatio = std::max(f8NonGreenRatio, nonGreenRatio);
            f8PlanePassed = f8PlanePassed && nonGreenRatio <= 0.001 &&
                f8InvalidRun <= 4u;
        }
    }

    // 가장 잘 재현되는 +3도 yaw에서 Weight/Diff/Validity와 Temporal Off의
    // current-only hole을 추가로 검사한다.
    setAboveStressCamera(3.0f, 0.0f);
    SceneDepthDiagnosticFrame f8StressDepth;
    if (!renderer.CaptureSceneDepthDiagnosticFrame(camera, f8StressDepth))
        return 15;
    std::vector<std::size_t> f8StressPixels;
    std::vector<std::uint8_t> f8StressMask;
    buildPlanarInteriorMask(f8StressDepth, f8StressPixels, f8StressMask);
    CloudDiagnosticFrame fullReference;
    renderer.SetStage11TemporalMode(Stage11TemporalMode::Off);
    renderer.ApplyStage10ResolutionPreset(Stage10ResolutionPreset::Full);
    if (!renderer.CaptureCloudDiagnosticFrame(
            camera, 0.0f, CloudDebugMode::Composite, fullReference, true))
        return 16;

    double f8WeightP99 = 0.0;
    double f8DiffBlueRatio = 0.0;
    std::size_t f8CloudDepthRun = 0u;
    std::size_t f8OffHoleRun = 0u;
    for (Stage10UpsampleFilter filter : filters)
    {
        std::array<CloudDiagnosticFrame, 4> frames;
        if (!capturePhaseSequence(
                filter, CloudDebugMode::TemporalHistoryWeight, frames))
            return 17;
        std::vector<double> weightRanges;
        weightRanges.reserve(f8StressPixels.size());
        for (std::size_t index : f8StressPixels)
        {
            float minimum = frames[0].pixels[index].x;
            float maximum = minimum;
            for (std::size_t phase = 1; phase < frames.size(); ++phase)
            {
                minimum = std::min(minimum, frames[phase].pixels[index].x);
                maximum = std::max(maximum, frames[phase].pixels[index].x);
            }
            weightRanges.push_back(maximum - minimum);
        }
        const double filterWeightP99 = FramePercentile(weightRanges, 0.99);
        f8WeightP99 = std::max(f8WeightP99, filterWeightP99);
        f8PlanePassed = f8PlanePassed && filterWeightP99 <= 0.05;

        if (!capturePhaseSequence(filter,
                CloudDebugMode::TemporalCurrentHistoryDifference, frames))
            return 18;
        std::size_t blue = 0u;
        for (const CloudDiagnosticFrame& frame : frames)
        for (std::size_t index : f8StressPixels)
            blue += nearColor(frame.pixels[index], { 0.0f, 0.0f, 1.0f })
                ? 1u : 0u;
        const double blueRatio = static_cast<double>(blue) /
            static_cast<double>(f8StressPixels.size() * frames.size());
        f8DiffBlueRatio = std::max(f8DiffBlueRatio, blueRatio);
        f8PlanePassed = f8PlanePassed && blueRatio <= 0.001;

        if (!capturePhaseSequence(filter,
                CloudDebugMode::TemporalHistoryValidity, frames))
            return 19;
        for (const CloudDiagnosticFrame& frame : frames)
            f8CloudDepthRun = std::max(f8CloudDepthRun,
                longestHorizontalRun(frame, f8StressMask,
                    [&](const DirectX::XMFLOAT4& value)
                    {
                        return nearColor(value, validityPalette[6]);
                    }));
        f8PlanePassed = f8PlanePassed && f8CloudDepthRun <= 4u;

        renderer.SetStage11TemporalMode(Stage11TemporalMode::Off);
        renderer.ApplyStage10ResolutionPreset(Stage10ResolutionPreset::Half);
        renderer.SetStage10UpsampleFilter(filter);
        CloudDiagnosticFrame halfOff;
        if (!renderer.CaptureCloudDiagnosticFrame(
                camera, 0.0f, CloudDebugMode::Composite, halfOff))
            return 20;
        std::vector<std::uint8_t> holeMask(f8StressMask.size(), 0u);
        for (int y = 1; y + 1 < halfOff.height; ++y)
        for (int x = 0; x < halfOff.width; ++x)
        {
            const std::size_t index = static_cast<std::size_t>(y) *
                static_cast<std::size_t>(halfOff.width) +
                static_cast<std::size_t>(x);
            if (f8StressMask[index] == 0u)
                continue;
            const auto errorAt = [&](std::size_t sample)
            {
                const DirectX::XMFLOAT4& a = halfOff.pixels[sample];
                const DirectX::XMFLOAT4& b = fullReference.pixels[sample];
                return (std::abs(a.x - b.x) + std::abs(a.y - b.y) +
                        std::abs(a.z - b.z)) / 3.0f;
            };
            const float error = errorAt(index);
            const float neighborError = 0.5f * (errorAt(
                index - static_cast<std::size_t>(halfOff.width)) + errorAt(
                index + static_cast<std::size_t>(halfOff.width)));
            holeMask[index] = error > 0.05f &&
                error - neighborError > 0.03f ? 1u : 0u;
        }
        f8OffHoleRun = std::max(f8OffHoleRun,
            longestHorizontalRun(halfOff, f8StressMask,
                [&](const DirectX::XMFLOAT4& value)
                {
                    const std::size_t index = &value - halfOff.pixels.data();
                    return holeMask[index] != 0u;
                }));
        f8PlanePassed = f8PlanePassed && f8OffHoleRun <= 4u;
    }
    {
        std::ostringstream f8Line;
        f8Line << std::fixed << std::setprecision(6)
               << "[STAGE11][F8-PLANE] COMPOSITE_MEAN=" << f8CompositeMean
               << " COMPOSITE_P99=" << f8CompositeP99
               << " NON_GREEN_RATIO=" << f8NonGreenRatio
               << " INVALID_RUN=" << f8InvalidRun
               << " WEIGHT_P99=" << f8WeightP99
               << " DIFF_BLUE_RATIO=" << f8DiffBlueRatio
               << " CLOUD_DEPTH_RUN=" << f8CloudDepthRun
               << " OFF_HOLE_RUN=" << f8OffHoleRun << ' '
               << (f8PlanePassed ? "PASS" : "FAIL");
        WriteDiagnosticLine(f8Line.str());
    }

    // 이후 기존 F5 motion/convergence/performance 계약을 같은 카메라에서 잰다.
    camera.SetLookAt(hero.position, hero.target);

    std::array<CloudDiagnosticFrame, 4> stationaryMotionFrames;
    if (!capturePhaseSequence(Stage10UpsampleFilter::Joint4,
            CloudDebugMode::TemporalReprojectionMotion,
            stationaryMotionFrames))
        return 9;
    std::size_t neutralMotionPixels = 0;
    std::size_t emptyMotionPixels = 0;
    for (const DirectX::XMFLOAT4& pixel : stationaryMotionFrames[0].pixels)
    {
        finite = finite && std::isfinite(pixel.x) && std::isfinite(pixel.y) &&
            std::isfinite(pixel.z) && std::isfinite(pixel.w);
        neutralMotionPixels += nearColor(pixel, { 0.5f, 0.5f, 0.5f }) ? 1u : 0u;
        emptyMotionPixels += nearColor(pixel, { 0.05f, 0.05f, 0.05f }) ? 1u : 0u;
    }
    const bool stationaryMotionPassed = neutralMotionPixels > 0u &&
        emptyMotionPixels > 0u;

    renderer.SetCloudWindSpeedsForValidation(
        appearance.windSpeed, appearance.weatherMapWindSpeed,
        appearance.detailWindSpeed);
    renderer.SetStage11TemporalMode(Stage11TemporalMode::Off);
    renderer.SetStage11TemporalMode(Stage11TemporalMode::Stable4Phase);
    std::set<std::uint64_t> convergenceHashes;
    for (std::uint32_t frameIndex = 0; frameIndex < 8; ++frameIndex)
    {
        CloudDiagnosticFrame frame;
        if (!renderer.CaptureCloudDiagnosticFrame(
                camera, static_cast<float>(frameIndex) / 60.0f,
                CloudDebugMode::Composite, frame))
            return 7;
        for (const DirectX::XMFLOAT4& pixel : frame.pixels)
            finite = finite && std::isfinite(pixel.x) &&
                std::isfinite(pixel.y) && std::isfinite(pixel.z) &&
                std::isfinite(pixel.w);
        convergenceHashes.insert(HashDiagnosticFrame(frame));
    }
    if (!renderer.TemporalHistoryValid() ||
        renderer.TemporalAccumulatedFrames() < 8u)
        return 8;

    for (CloudDebugMode mode : {
             CloudDebugMode::TemporalJitterPhase,
             CloudDebugMode::TemporalReprojectionMotion,
             CloudDebugMode::TemporalHistoryValidity,
             CloudDebugMode::TemporalHistoryWeight,
             CloudDebugMode::TemporalCurrentHistoryDifference,
             CloudDebugMode::TemporalCurrentSourceValidity })
    {
        CloudDiagnosticFrame frame;
        if (!renderer.CaptureCloudDiagnosticFrame(camera, 8.0f / 60.0f,
                                                   mode, frame))
            return 9;
        for (const DirectX::XMFLOAT4& pixel : frame.pixels)
            finite = finite && std::isfinite(pixel.x) &&
                std::isfinite(pixel.y) && std::isfinite(pixel.z) &&
                std::isfinite(pixel.w);
    }

    // 승인 성능 계약도 같은 Stratus/F5/50%/Stable 조건에서 raw timestamp로
    // 다시 잰다. UI EMA가 아니라 120프레임 warmup 뒤 서로 다른 GPU query
    // 600개만 모아 p95를 계산한다.
    renderer.SetDebugMode(CloudDebugMode::Composite);
    renderer.SetStage10UpsampleFilter(Stage10UpsampleFilter::Joint4);
    renderer.SetCloudWindSpeedsForValidation(0.0f, 0.0f, 0.0f);
    renderer.SetStage11TemporalMode(Stage11TemporalMode::Off);
    renderer.SetStage11TemporalMode(Stage11TemporalMode::Stable4Phase);
    for (std::uint32_t frameIndex = 0; frameIndex < 120u; ++frameIndex)
        renderer.Render(camera, 0.0f);
    std::vector<double> resolveSamples;
    std::vector<double> cloudTotalSamples;
    resolveSamples.reserve(600u);
    cloudTotalSamples.reserve(600u);
    std::uint64_t lastGpuSample = renderer.TimingSnapshot().gpuSampleIndex;
    for (std::uint32_t attempt = 0;
         attempt < 2400u && resolveSamples.size() < 600u; ++attempt)
    {
        renderer.Render(camera, 0.0f);
        const FrameTimingSnapshot timing = renderer.TimingSnapshot();
        if (!timing.gpuValid || timing.gpuSampleIndex == lastGpuSample)
        {
            Sleep(1);
            continue;
        }
        lastGpuSample = timing.gpuSampleIndex;
        if (!std::isfinite(timing.rawGpuUpsampleCompositeMs) ||
            !std::isfinite(timing.rawGpuCloudMs) ||
            timing.rawGpuUpsampleCompositeMs < 0.0 ||
            timing.rawGpuCloudMs < timing.rawGpuUpsampleCompositeMs)
        {
            finite = false;
            break;
        }
        resolveSamples.push_back(timing.rawGpuUpsampleCompositeMs);
        cloudTotalSamples.push_back(timing.rawGpuCloudMs);
    }
    const double resolveP95 = FramePercentile(resolveSamples, 0.95);
    const double cloudTotalP95 = FramePercentile(cloudTotalSamples, 0.95);
    bool performancePassed = resolveSamples.size() == 600u &&
        cloudTotalSamples.size() == 600u && resolveP95 <= 2.0 &&
        cloudTotalP95 <= 10.0;
    {
        std::ostringstream performanceLine;
        performanceLine << std::fixed << std::setprecision(6)
                        << "[STAGE11][PERF] SAMPLES=" << resolveSamples.size()
                        << " RESOLVE_P95_MS=" << resolveP95
                        << " CLOUD_TOTAL_P95_MS=" << cloudTotalP95 << ' '
                        << (performancePassed ? "PASS" : "FAIL");
        WriteDiagnosticLine(performanceLine.str());
    }

    setAboveStressCamera(3.0f, 0.0f);
    renderer.SetStage11TemporalMode(Stage11TemporalMode::Off);
    renderer.SetStage11TemporalMode(Stage11TemporalMode::Stable4Phase);
    renderer.SetStage10UpsampleFilter(Stage10UpsampleFilter::Joint4);
    for (std::uint32_t frameIndex = 0; frameIndex < 120u; ++frameIndex)
        renderer.Render(camera, 0.0f);
    std::vector<double> f8ResolveSamples;
    std::vector<double> f8CloudTotalSamples;
    f8ResolveSamples.reserve(600u);
    f8CloudTotalSamples.reserve(600u);
    lastGpuSample = renderer.TimingSnapshot().gpuSampleIndex;
    for (std::uint32_t attempt = 0;
         attempt < 2400u && f8ResolveSamples.size() < 600u; ++attempt)
    {
        renderer.Render(camera, 0.0f);
        const FrameTimingSnapshot timing = renderer.TimingSnapshot();
        if (!timing.gpuValid || timing.gpuSampleIndex == lastGpuSample)
        {
            Sleep(1);
            continue;
        }
        lastGpuSample = timing.gpuSampleIndex;
        if (!std::isfinite(timing.rawGpuUpsampleCompositeMs) ||
            !std::isfinite(timing.rawGpuCloudMs) ||
            timing.rawGpuUpsampleCompositeMs < 0.0 ||
            timing.rawGpuCloudMs < timing.rawGpuUpsampleCompositeMs)
        {
            finite = false;
            break;
        }
        f8ResolveSamples.push_back(timing.rawGpuUpsampleCompositeMs);
        f8CloudTotalSamples.push_back(timing.rawGpuCloudMs);
    }
    const double f8ResolveP95 = FramePercentile(f8ResolveSamples, 0.95);
    const double f8CloudTotalP95 = FramePercentile(
        f8CloudTotalSamples, 0.95);
    const bool f8PerformancePassed = f8ResolveSamples.size() == 600u &&
        f8CloudTotalSamples.size() == 600u && f8ResolveP95 <= 2.0 &&
        f8CloudTotalP95 <= 10.0;
    performancePassed = performancePassed && f8PerformancePassed;
    {
        std::ostringstream performanceLine;
        performanceLine << std::fixed << std::setprecision(6)
                        << "[STAGE11][F8-PERF] SAMPLES="
                        << f8ResolveSamples.size()
                        << " RESOLVE_P95_MS=" << f8ResolveP95
                        << " CLOUD_TOTAL_P95_MS=" << f8CloudTotalP95 << ' '
                        << (f8PerformancePassed ? "PASS" : "FAIL");
        WriteDiagnosticLine(performanceLine.str());
    }

    renderer.Resize(97, 55);
    camera.SetAspect(97.0f / 55.0f);
    const bool resizeReset = !renderer.TemporalHistoryValid() &&
        renderer.TemporalAccumulatedFrames() == 0u;
    CloudDiagnosticFrame resized;
    if (!renderer.CaptureCloudDiagnosticFrame(
            camera, 9.0f / 60.0f, CloudDebugMode::Composite, resized))
        return 10;
    renderer.SetStage11TemporalMode(Stage11TemporalMode::Off);
    const bool toggleReset = !renderer.TemporalHistoryValid() &&
        renderer.TemporalAccumulatedFrames() == 0u;

    std::ostringstream line;
    line << std::fixed << std::setprecision(6)
         << "STAGE11_TEMPORAL FRAMES=" << convergenceHashes.size()
         << " BOUNDARY_MEAN=" << boundaryMean
         << " BOUNDARY_P99=" << boundaryP99
         << " WEIGHT_P99=" << weightP99
         << " GEOMETRY=" << geometryPixelCount
         << " BOUNDARY=" << boundaryPixels.size()
         << " GROUND_ROW=" << groundHorizonRow
         << " FILTERS=" << std::size(filters)
         << " HISTORY_HOLD=" << heldHistoryPixels
         << " MOTION_NEUTRAL=" << neutralMotionPixels
         << " MOTION_EMPTY=" << emptyMotionPixels
         << " RESOLVE_P95_MS=" << resolveP95
         << " CLOUD_TOTAL_P95_MS=" << cloudTotalP95
         << " F8_RESOLVE_P95_MS=" << f8ResolveP95
         << " F8_CLOUD_TOTAL_P95_MS=" << f8CloudTotalP95
         << " F8_PLANE=" << (f8PlanePassed ? 1 : 0)
         << " RESIZE_RESET=" << (resizeReset ? 1 : 0)
         << " TOGGLE_RESET=" << (toggleReset ? 1 : 0) << ' '
         << (finite && stationaryBoundaryPassed && weightPassed &&
              f8PlanePassed &&
              stationaryMotionPassed && currentPalettePassed &&
              validityPalettePassed && differenceContractPassed &&
              performancePassed && resizeReset && toggleReset
              ? "PASS" : "FAIL");
    WriteDiagnosticLine(line.str());
    return finite && stationaryBoundaryPassed && weightPassed &&
        f8PlanePassed &&
        stationaryMotionPassed && currentPalettePassed &&
        validityPalettePassed && differenceContractPassed &&
        performancePassed && resizeReset && toggleReset &&
        !renderer.HasDebugLayerErrors() ? 0 : 7;
}

int RunStage12ShadowSmokeTest(Renderer& renderer, Camera& camera)
{
    constexpr std::uint32_t fixtureWidth = 96u;
    constexpr std::uint32_t fixtureHeight = 54u;
    renderer.SetNoiseLabVisible(false);
    renderer.EnableNoiseLabPreviews(false);
    renderer.SetVSyncEnabled(false);
    if (!renderer.ApplyStage13OpenWorldPreset() ||
        !renderer.ApplyCloudAppearancePreset(
            CloudAppearancePreset::DenseMixedDefault))
        return 2;
    renderer.ApplyStage9OptimizationPreset(Stage9OptimizationPreset::Balanced);
    renderer.SetStage11TemporalMode(Stage11TemporalMode::Off);
    if (!renderer.SetStage12ShadowPreset(Stage12ShadowPreset::Fast256))
        return 3;
    const Stage13CameraPreset& hero = stage13camera::Get(
        Stage13CameraPresetId::HeroDepth);
    camera.SetClipPlanes(stage13camera::kNearPlaneMeters,
                         stage13camera::kFarPlaneMeters);
    // PMv2 이후 숨은 Win32 테스트 창의 실제 최소 client 폭은 96보다 클 수 있다.
    // Stage 12 승인값은 창 크기가 아니라 고정 96x54 픽셀 모집단의 비교이므로,
    // 첫 Direct/Cache 캡처 전에 renderer fixture를 명시적으로 고정한다.
    renderer.Resize(fixtureWidth, fixtureHeight);
    camera.SetAspect(static_cast<float>(fixtureWidth) /
                     static_cast<float>(fixtureHeight));
    camera.SetLookAt(hero.position, hero.target);

    renderer.SetStage12ShadowMode(Stage12ShadowMode::DirectReference);
    CloudDiagnosticFrame direct;
    if (!renderer.CaptureCloudDiagnosticFrame(
            camera, 0.0f, CloudDebugMode::LightTransmittance, direct))
        return 4;
    renderer.SetStage12ShadowMode(Stage12ShadowMode::DeepCache);
    CloudDiagnosticFrame cached;
    if (!renderer.CaptureCloudDiagnosticFrame(
            camera, 0.0f, CloudDebugMode::LightTransmittance, cached))
        return 5;

    double sumError = 0.0;
    std::size_t finiteCount = 0;
    std::vector<double> errors;
    errors.reserve(direct.pixels.size());
    for (std::size_t index = 0; index < direct.pixels.size(); ++index)
    {
        const DirectX::XMFLOAT4& a = direct.pixels[index];
        const DirectX::XMFLOAT4& b = cached.pixels[index];
        if (!std::isfinite(a.x) || !std::isfinite(b.x))
            return 6;
        const double error = std::abs(static_cast<double>(a.x - b.x));
        sumError += error;
        errors.push_back(error);
        ++finiteCount;
    }
    const double mae = finiteCount > 0 ? sumError / finiteCount : 1.0;
    const double p99 = FramePercentile(errors, 0.99);
    const bool lightQualityPassed = mae <= 0.01 && p99 <= 0.03;

    const std::uintptr_t cacheIdentity = renderer.NearShadowCacheIdentity();
    renderer.ApplyStage10ResolutionPreset(Stage10ResolutionPreset::Half);
    const bool halfKeepsCache = cacheIdentity != 0 &&
        renderer.NearShadowCacheIdentity() == cacheIdentity;
    renderer.ApplyStage10ResolutionPreset(Stage10ResolutionPreset::Full);
    const bool fullKeepsCache =
        renderer.NearShadowCacheIdentity() == cacheIdentity;
    renderer.Resize(128, 72);
    const bool resizeKeepsCache =
        renderer.NearShadowCacheIdentity() == cacheIdentity;
    renderer.Resize(fixtureWidth, fixtureHeight);
    camera.SetAspect(static_cast<float>(fixtureWidth) /
                     static_cast<float>(fixtureHeight));

    constexpr CloudDebugMode diagnosticModes[] = {
        CloudDebugMode::Stage12NearOpticalDepth,
        CloudDebugMode::Stage12FarOpticalDepth,
        CloudDebugMode::Stage12CascadeSelection,
        CloudDebugMode::Stage12SurfaceTransmittance,
        CloudDebugMode::Stage12DirectCacheError,
    };
    bool surfaceShadowObserved = false;
    bool nearCacheTextureObserved = false;
    bool farCacheTextureObserved = false;
    for (CloudDebugMode mode : diagnosticModes)
    {
        CloudDiagnosticFrame frame;
        if (!renderer.CaptureCloudDiagnosticFrame(camera, 0.0f, mode, frame))
            return 7;
        float minimumValue = 1.0f;
        float maximumValue = 0.0f;
        for (const DirectX::XMFLOAT4& pixel : frame.pixels)
        {
            if (!std::isfinite(pixel.x) || !std::isfinite(pixel.y) ||
                !std::isfinite(pixel.z) || !std::isfinite(pixel.w))
                return 8;
            if (mode == CloudDebugMode::Stage12SurfaceTransmittance &&
                pixel.x > 0.0001f && pixel.x < 0.99f)
                surfaceShadowObserved = true;
            minimumValue = std::min(minimumValue, pixel.x);
            maximumValue = std::max(maximumValue, pixel.x);
        }
        const bool cacheTextureHasStructure = maximumValue > 0.02f &&
            maximumValue - minimumValue > 0.02f;
        if (mode == CloudDebugMode::Stage12NearOpticalDepth)
            nearCacheTextureObserved = cacheTextureHasStructure;
        if (mode == CloudDebugMode::Stage12FarOpticalDepth)
            farCacheTextureObserved = cacheTextureHasStructure;
    }

    std::ostringstream line;
    line << std::fixed << std::setprecision(6)
         << "STAGE12_SHADOW MAE=" << mae
         << " P99=" << p99
         << " FIXTURE=" << fixtureWidth << 'x' << fixtureHeight
         << " CACHE_MIB="
         << static_cast<double>(renderer.ShadowCacheBytes()) /
                (1024.0 * 1024.0)
         << " HALF_STABLE=" << (halfKeepsCache ? 1 : 0)
         << " FULL_STABLE=" << (fullKeepsCache ? 1 : 0) << ' '
         << " RESIZE_STABLE=" << (resizeKeepsCache ? 1 : 0)
         << " SURFACE=" << (surfaceShadowObserved ? 1 : 0) << ' '
         << " NEAR_TEXTURE=" << (nearCacheTextureObserved ? 1 : 0)
         << " FAR_TEXTURE=" << (farCacheTextureObserved ? 1 : 0) << ' '
         << (lightQualityPassed && halfKeepsCache && fullKeepsCache &&
             resizeKeepsCache && surfaceShadowObserved &&
             nearCacheTextureObserved && farCacheTextureObserved
                ? "PASS" : "FAIL");
    WriteDiagnosticLine(line.str());
    return lightQualityPassed && halfKeepsCache && fullKeepsCache &&
        resizeKeepsCache && surfaceShadowObserved &&
        nearCacheTextureObserved && farCacheTextureObserved &&
        !renderer.HasDebugLayerErrors() ? 0 : 9;
}

int RunStage14AtmosphereSmokeTest(Renderer& renderer, Camera& camera)
{
    renderer.SetNoiseLabVisible(false);
    renderer.EnableNoiseLabPreviews(false);
    renderer.EnableFrameHashCapture(true);
    renderer.SetVSyncEnabled(false);
    renderer.SetStage11TemporalMode(Stage11TemporalMode::Off);

    const Stage13CameraPreset& horizon = stage13camera::Get(
        Stage13CameraPresetId::GroundHorizon);
    camera.SetClipPlanes(stage13camera::kNearPlaneMeters,
                         stage13camera::kFarPlaneMeters);
    camera.SetLookAt(horizon.position, horizon.target);

    AtmosphereParameters& atmosphere = renderer.MutableAtmosphereSettings();
    atmosphere.mode = AtmosphereMode::Physical;
    stage14atmosphere::ApplyPreset(atmosphere, AtmospherePreset::EarthClear);
    atmosphere.sunAzimuthDegrees = -60.0f;
    atmosphere.sunElevationDegrees = 18.0f;
    GroundLightingParameters& ground = renderer.MutableGroundLightingSettings();
    stage14ground::ApplyPreset(ground, GroundMaterialPreset::Concrete);
    ToneMappingParameters& tone = renderer.MutableToneMappingSettings();
    tone.mode = ToneMappingMode::AcesFitted;
    tone.exposureEv = 0.0f;
    tone.whiteBalanceKelvin = 6500.0f;

    renderer.Render(camera, 0.0f);
    std::array<std::uint64_t, 6> initial = {};
    for (std::size_t index = 0; index < initial.size(); ++index)
        initial[index] = renderer.AtmosphereLutGeneration(index);
    const bool initialGenerated = std::all_of(
        initial.begin(), initial.end(),
        [](std::uint64_t value) { return value > 0u; });
    const std::uint64_t initialFrameHash = renderer.LastCloudFrameHash();
    Stage14LutValidationResult lutValidation;
    const bool lutReadback = renderer.ValidateStage14Luts(lutValidation) &&
        lutValidation.finiteNonNegative &&
        lutValidation.transmittanceMae <= 0.01 &&
        lutValidation.transmittanceP99 <= 0.03;

    renderer.Render(camera, 1.0f / 60.0f);
    bool staticStable = true;
    for (std::size_t index = 0; index < initial.size(); ++index)
        staticStable &= renderer.AtmosphereLutGeneration(index) == initial[index];

    tone.exposureEv = 1.0f;
    renderer.Render(camera, 2.0f / 60.0f);
    bool toneStable = true;
    for (std::size_t index = 0; index < initial.size(); ++index)
        toneStable &= renderer.AtmosphereLutGeneration(index) == initial[index];
    const bool toneChangedDisplay = renderer.LastCloudFrameHash() != 0u &&
        renderer.LastCloudFrameHash() != initialFrameHash;

    stage14ground::ApplyPreset(ground, GroundMaterialPreset::Snow);
    renderer.Render(camera, 3.0f / 60.0f);
    std::array<std::uint64_t, 6> afterGround = {};
    for (std::size_t index = 0; index < afterGround.size(); ++index)
        afterGround[index] = renderer.AtmosphereLutGeneration(index);
    bool groundInvalidation = afterGround[0] == initial[0];
    for (std::size_t index = 1; index < afterGround.size(); ++index)
        groundInvalidation &= afterGround[index] > initial[index];

    atmosphere.sunElevationDegrees = 3.0f;
    renderer.Render(camera, 4.0f / 60.0f);
    std::array<std::uint64_t, 6> afterSun = {};
    for (std::size_t index = 0; index < afterSun.size(); ++index)
        afterSun[index] = renderer.AtmosphereLutGeneration(index);
    const bool sunInvalidation = afterSun[0] == afterGround[0] &&
        afterSun[1] == afterGround[1] && afterSun[3] == afterGround[3] &&
        afterSun[2] > afterGround[2] && afterSun[4] > afterGround[4] &&
        afterSun[5] > afterGround[5];

    camera.SetLookAt(horizon.position,
        { horizon.target.x + 500.0f, horizon.target.y, horizon.target.z });
    renderer.Render(camera, 5.0f / 60.0f);
    std::array<std::uint64_t, 6> afterCamera = {};
    for (std::size_t index = 0; index < afterCamera.size(); ++index)
        afterCamera[index] = renderer.AtmosphereLutGeneration(index);
    const bool cameraInvalidation =
        afterCamera[0] == afterSun[0] && afterCamera[1] == afterSun[1] &&
        afterCamera[2] == afterSun[2] && afterCamera[3] == afterSun[3] &&
        afterCamera[4] > afterSun[4] && afterCamera[5] > afterSun[5];

    const bool passed = initialGenerated && initialFrameHash != 0u && lutReadback &&
        staticStable && toneStable && toneChangedDisplay &&
        groundInvalidation && sunInvalidation && cameraInvalidation &&
        !renderer.HasDebugLayerErrors();
    std::ostringstream line;
    line << "STAGE14_ATMOSPHERE INITIAL=" << (initialGenerated ? 1 : 0)
         << " LUT_FINITE=" << (lutValidation.finiteNonNegative ? 1 : 0)
         << std::fixed << std::setprecision(6)
         << " TRANS_MAE=" << lutValidation.transmittanceMae
         << " TRANS_P99=" << lutValidation.transmittanceP99
         << " STATIC=" << (staticStable ? 1 : 0)
         << " TONE_STABLE=" << (toneStable ? 1 : 0)
         << " TONE_DISPLAY=" << (toneChangedDisplay ? 1 : 0)
         << " GROUND_INVALIDATION=" << (groundInvalidation ? 1 : 0)
         << " SUN_INVALIDATION=" << (sunInvalidation ? 1 : 0)
         << " CAMERA_INVALIDATION=" << (cameraInvalidation ? 1 : 0) << ' '
         << (passed ? "PASS" : "FAIL");
    WriteDiagnosticLine(line.str());
    return passed ? 0 : 9;
}

int RunStage15PresetSmokeTest(Renderer& renderer, Camera& camera)
{
    struct Result
    {
        std::string concept;
        std::string quality;
        std::string camera;
        std::uint64_t weatherHash = 0u;
        std::uint64_t frameHash = 0u;
        bool finite = false;
        bool resources = false;
        bool historyReset = false;
    };

    renderer.SetNoiseLabVisible(false);
    renderer.EnableNoiseLabPreviews(false);
    renderer.SetAutomatedRenderMode(true);
    renderer.SetVSyncEnabled(false);
    renderer.SetOpaqueSceneForTest(true);
    renderer.EnableFrameHashCapture(true);
    camera.SetClipPlanes(
        stage13camera::kNearPlaneMeters, stage13camera::kFarPlaneMeters);
    if (!renderer.ApplyStage15Defaults())
        return 2;

    const bool overlayDefaultsContract =
        renderer.Stage15StatusOverlayVisible() &&
        renderer.PerformanceOverlayVisible();
    renderer.Render(camera, 0.0f);

    // 기본 Medium / Temporal On에서는 첫 T가 임시 Off를 만들고, 두 번째
    // T가 selection과 override bookkeeping까지 포함한 같은 canonical
    // fingerprint로 정확히 돌아와야 한다.
    const std::uint64_t defaultTemporalFingerprint =
        renderer.Stage15StateFingerprint();
    const Stage15QualityPreset defaultTemporalQuality =
        renderer.Stage15Quality();
    const Stage10ResolutionPreset defaultTemporalResolution =
        renderer.ResolutionPreset();
    renderer.ToggleStage15TemporalOverride();
    renderer.Render(camera, 0.0f);
    const bool defaultTemporalOverrideApplied =
        renderer.Stage15Quality() == Stage15QualityPreset::Medium &&
        renderer.TemporalMode() == Stage11TemporalMode::Off &&
        renderer.ResolutionPreset() == Stage10ResolutionPreset::Half &&
        renderer.Stage15TemporalOverrideActive();
    renderer.ToggleStage15TemporalOverride();
    renderer.Render(camera, 0.0f);
    const bool temporalOverrideRoundTripContract =
        defaultTemporalOverrideApplied &&
        renderer.Stage15StateFingerprint() == defaultTemporalFingerprint &&
        renderer.Stage15Quality() == defaultTemporalQuality &&
        renderer.TemporalMode() == Stage11TemporalMode::Stable4Phase &&
        renderer.ResolutionPreset() == defaultTemporalResolution &&
        !renderer.Stage15TemporalOverrideActive();

    // F1의 Temporal 직접 편집과 같은 즉시 setter는 활성 override를
    // 끝내고 Quality만 Custom으로 만들어야 한다.
    renderer.ToggleStage15TemporalOverride();
    renderer.Render(camera, 0.0f);
    const bool directEditStartedFromOverride =
        renderer.Stage15TemporalOverrideActive() &&
        renderer.TemporalMode() == Stage11TemporalMode::Off;
    renderer.SetStage11TemporalMode(Stage11TemporalMode::Stable4Phase);
    const bool temporalDirectEditClearsOverrideContract =
        directEditStartedFromOverride &&
        renderer.Stage15Quality() == Stage15QualityPreset::Custom &&
        renderer.TemporalMode() == Stage11TemporalMode::Stable4Phase &&
        renderer.ResolutionPreset() == Stage10ResolutionPreset::Half &&
        !renderer.Stage15TemporalOverrideActive();

    // 직접 편집한 Custom / Temporal Off / Full도 T가 해상도를 Half로
    // 바꾸지 않는다. 첫 T는 FullResolution, 두 번째 T는 Off로 돌아온다.
    renderer.SetStage11TemporalMode(Stage11TemporalMode::Off);
    renderer.ApplyStage10ResolutionPreset(Stage10ResolutionPreset::Full);
    renderer.Render(camera, 0.0f);
    const std::uint64_t customTemporalFingerprint =
        renderer.Stage15StateFingerprint();
    renderer.ToggleStage15TemporalOverride();
    renderer.Render(camera, 0.0f);
    const bool customTemporalOverrideApplied =
        renderer.Stage15Quality() == Stage15QualityPreset::Custom &&
        renderer.TemporalMode() == Stage11TemporalMode::FullResolution &&
        renderer.ResolutionPreset() == Stage10ResolutionPreset::Full &&
        renderer.Stage15TemporalOverrideActive();
    renderer.ToggleStage15TemporalOverride();
    renderer.Render(camera, 0.0f);
    const bool temporalOverrideCustomRoundTripContract =
        customTemporalOverrideApplied &&
        renderer.Stage15StateFingerprint() == customTemporalFingerprint &&
        renderer.Stage15Quality() == Stage15QualityPreset::Custom &&
        renderer.TemporalMode() == Stage11TemporalMode::Off &&
        renderer.ResolutionPreset() == Stage10ResolutionPreset::Full &&
        !renderer.Stage15TemporalOverrideActive();

    // 개발 UI에서 Early Exit/Upsampling 세부값을 바꾼 뒤 named Medium을
    // 다시 선택하면 이름만 Medium이 되는 것이 아니라 소유한 전체 값을
    // canonical descriptor로 복원해야 한다.
    renderer.ApplyStage9OptimizationPreset(Stage9OptimizationPreset::Fast);
    renderer.MutableUpsamplingSettings().sceneDepthRelativeSigma = 0.01f;
    renderer.MutableUpsamplingSettings().cloudDepthRelativeSigma = 0.04f;
    renderer.MutableUpsamplingSettings().transmittanceSigma = 0.20f;
    renderer.MutableUpsamplingSettings().minimumWeight = 1.0e-3f;
    renderer.MutableTemporalSettings().historyWeight = 0.80f;
    renderer.MutableTemporalSettings().nearHistoryFadeStartMeters = 250.0f;
    renderer.MutableTemporalSettings().nearHistoryFadeEndMeters = 1000.0f;
    renderer.MutableTemporalSettings().neighborhoodClampingEnabled = 0u;
    renderer.RequestStage15QualityPreset(Stage15QualityPreset::Medium);
    renderer.Render(camera, 0.0f);
    const Stage10UpsamplingParameters& canonicalUpsampling =
        renderer.UpsamplingSettings();
    const Stage11TemporalParameters& canonicalTemporal =
        renderer.TemporalSettings();
    const bool namedQualityCanonicalRestoreContract =
        renderer.Stage15Quality() == Stage15QualityPreset::Medium &&
        renderer.CloudSettings().transmittanceThreshold == 0.01f &&
        canonicalUpsampling.filterMode == static_cast<std::uint32_t>(
            Stage10UpsampleFilter::Joint4) &&
        canonicalUpsampling.resolutionScale == 0.5f &&
        canonicalUpsampling.sceneDepthRelativeSigma == 0.0025f &&
        canonicalUpsampling.cloudDepthRelativeSigma == 0.01f &&
        canonicalUpsampling.transmittanceSigma == 0.10f &&
        canonicalUpsampling.minimumWeight == 1.0e-4f &&
        canonicalTemporal.historyWeight == 0.90f &&
        canonicalTemporal.nearHistoryFadeStartMeters == 1000.0f &&
        canonicalTemporal.nearHistoryFadeEndMeters == 3000.0f &&
        canonicalTemporal.neighborhoodClampingEnabled == 1u;

    // 아래 48-case 행렬은 언제나 정식 기본 상태에서 시작한다.
    if (!renderer.ApplyStage15Defaults())
        return 2;

    const std::uintptr_t weatherTexture = renderer.WeatherTextureIdentity();
    const std::uintptr_t weatherSrv = renderer.WeatherSrvIdentity();
    if (weatherTexture == 0u || weatherSrv == 0u)
        return 3;

    constexpr Stage15ConceptPreset concepts[] = {
        Stage15ConceptPreset::UrbanFairWeather,
        Stage15ConceptPreset::MeadowBrokenClouds,
        Stage15ConceptPreset::DesertCirrus,
        Stage15ConceptPreset::SnowOvercast,
    };
    constexpr Stage15QualityPreset qualities[] = {
        Stage15QualityPreset::Low,
        Stage15QualityPreset::Medium,
        Stage15QualityPreset::High,
    };

    std::vector<Result> results;
    results.reserve(std::size(concepts) * std::size(qualities) *
                    stage13camera::kOpenWorldPresets.size());
    bool passed = overlayDefaultsContract &&
        temporalOverrideRoundTripContract &&
        temporalDirectEditClearsOverrideContract &&
        temporalOverrideCustomRoundTripContract &&
        namedQualityCanonicalRestoreContract;
    float timeSeconds = 0.0f;
    for (Stage15ConceptPreset concept : concepts)
    {
        const Stage15ConceptDescriptor conceptDescriptor =
            stage15::ResolveConcept(concept);
        const WeatherMapData expectedWeather = BuildWeatherMap(
            conceptDescriptor.weatherPreset, conceptDescriptor.weather);
        const std::uint64_t expectedWeatherHash =
            HashWeatherMap(expectedWeather);
        for (Stage15QualityPreset quality : qualities)
        {
            const Stage15QualityDescriptor qualityDescriptor =
                stage15::ResolveRealtimeQuality(quality);
            for (const Stage13CameraPreset& cameraPreset :
                 stage13camera::kOpenWorldPresets)
            {
                const bool presetStateChanges =
                    renderer.Stage15Concept() != concept ||
                    renderer.Stage15Quality() != quality;
                renderer.RequestStage15ConceptPreset(concept);
                renderer.RequestStage15QualityPreset(quality);
                camera.SetLookAt(cameraPreset.position, cameraPreset.target);
                renderer.Render(camera, timeSeconds);
                timeSeconds += 1.0f / 60.0f;

                CloudDiagnosticFrame density;
                const bool captured = renderer.CaptureCloudDiagnosticFrame(
                    camera, timeSeconds, CloudDebugMode::FinalDensity, density);
                bool finite = captured && !density.pixels.empty();
                for (const DirectX::XMFLOAT4& pixel : density.pixels)
                    finite = finite && std::isfinite(pixel.x) &&
                        std::isfinite(pixel.y) && std::isfinite(pixel.z) &&
                        std::isfinite(pixel.w);

                const bool resources =
                    renderer.CloudRenderWidth() ==
                        stage10upsampling::ScaledExtent(
                            density.width,
                            qualityDescriptor.resolutionPreset ==
                                    Stage10ResolutionPreset::Half
                                ? 0.5f : 1.0f) &&
                    renderer.CloudRenderHeight() ==
                        stage10upsampling::ScaledExtent(
                            density.height,
                            qualityDescriptor.resolutionPreset ==
                                    Stage10ResolutionPreset::Half
                                ? 0.5f : 1.0f) &&
                    renderer.ShadowCacheBytes() == stage12shadow::CacheBytes(
                        qualityDescriptor.shadowPreset) &&
                    renderer.WeatherTextureIdentity() == weatherTexture &&
                    renderer.WeatherSrvIdentity() == weatherSrv;
                // 같은 preset의 다른 카메라는 transaction no-op이다. 실제 preset
                // 상태가 바뀐 경우에만 history/jitter reset을 요구한다.
                const bool historyReset = !presetStateChanges ||
                    renderer.TemporalAccumulatedFrames() <= 1u;
                const bool stateMatches =
                    renderer.Stage15Concept() == concept &&
                    renderer.Stage15Quality() == quality &&
                    renderer.Stage15Diagnostic() ==
                        Stage15DiagnosticMode::None &&
                    renderer.WeatherMapHash() == expectedWeatherHash;
                const std::uint64_t frameHash = captured
                    ? HashDiagnosticFrame(density) : 0u;
                passed = passed && finite && resources && historyReset &&
                    stateMatches && frameHash != 0u;
                results.push_back({
                    stage15::ConceptName(concept),
                    stage15::QualityName(quality),
                    cameraPreset.diagnosticName,
                    renderer.WeatherMapHash(), frameHash,
                    finite, resources, historyReset });
            }
        }
    }

    // F5~F8은 그대로 둔다. Cirrus 층 내부의 8.5 km 자동 카메라는 별도이며
    // 방향성 경로가 실제 GPU에서 유한한 비영(非零) 밀도를 내는지만 확인한다.
    renderer.RequestStage15ConceptPreset(Stage15ConceptPreset::DesertCirrus);
    renderer.RequestStage15QualityPreset(Stage15QualityPreset::Medium);
    camera.SetLookAt({ 0.0f, 8500.0f, 0.0f },
                     { 0.0f, 8500.0f, -12000.0f });
    renderer.Render(camera, timeSeconds);
    const CloudDebugMode cirrusModes[] = {
        CloudDebugMode::CloudHitMask,
        CloudDebugMode::RawNoise,
        CloudDebugMode::WeatherCoverage,
        CloudDebugMode::LocalHeightFraction,
        CloudDebugMode::TypedShapeProfile,
        CloudDebugMode::BaseDensity,
        CloudDebugMode::FinalDensity,
    };
    std::array<float, std::size(cirrusModes)> cirrusMaxima = {};
    bool cirrusPassed = static_cast<CloudShapeMode>(
        renderer.ShapeSettings().shapeMode) ==
        CloudShapeMode::CirrusPhysicalLayer;
    for (std::size_t modeIndex = 0;
         modeIndex < std::size(cirrusModes); ++modeIndex)
    {
        CloudDiagnosticFrame cirrusInside;
        const bool captured = renderer.CaptureCloudDiagnosticFrame(
            camera, timeSeconds + 1.0f / 60.0f,
            cirrusModes[modeIndex], cirrusInside);
        cirrusPassed = cirrusPassed && captured;
        for (const DirectX::XMFLOAT4& pixel : cirrusInside.pixels)
        {
            cirrusPassed = cirrusPassed && std::isfinite(pixel.x);
            cirrusMaxima[modeIndex] = std::max(
                cirrusMaxima[modeIndex], pixel.x);
        }
    }
    const float cirrusMaximum = cirrusMaxima.back();
    cirrusPassed = cirrusPassed && cirrusMaximum > 1.0e-6f;

    renderer.ToggleStage15TemporalOverride();
    renderer.RequestStage15QualityPreset(Stage15QualityPreset::Medium);
    renderer.Render(camera, timeSeconds);
    const bool preDiagnosticRealtimeState =
        renderer.Stage15Diagnostic() == Stage15DiagnosticMode::None &&
        renderer.Stage15Concept() == Stage15ConceptPreset::DesertCirrus &&
        renderer.Stage15Quality() == Stage15QualityPreset::Medium &&
        renderer.Stage15TemporalOverrideActive() &&
        renderer.TemporalMode() == Stage11TemporalMode::Off &&
        renderer.ResolutionPreset() == Stage10ResolutionPreset::Half;
    // 작은 GPU smoke 창은 Win32 최소 track 폭/DPI에 따라 요청한 96 px보다
    // 실제 physical client 폭이 커질 수 있다. Capture Native 전환 대기 상태는
    // 그 실제 출력에서 쓰던 Medium half RT를 그대로 보존해야 한다.
    const std::uint32_t preCaptureCloudWidth = renderer.CloudRenderWidth();
    const std::uint32_t preCaptureCloudHeight = renderer.CloudRenderHeight();
    renderer.RequestStage15DiagnosticMode(
        Stage15DiagnosticMode::CaptureStill);
    renderer.Render(camera, timeSeconds);
    const bool captureContract =
        renderer.Stage15Diagnostic() == Stage15DiagnosticMode::CaptureStill &&
        renderer.CaptureState() == Stage15CaptureState::PendingNative &&
        renderer.CaptureCompletedSamples() == 0u &&
        renderer.SceneInputLocked() &&
        renderer.CloudRenderWidth() == preCaptureCloudWidth &&
        renderer.CloudRenderHeight() == preCaptureCloudHeight &&
        renderer.TemporalMode() == Stage11TemporalMode::Off &&
        renderer.CloudSettings().stepSize == 100.0f &&
        renderer.CloudSettings().maxViewSteps == 512u;
    if (!captureContract)
    {
        std::ostringstream detail;
        detail << "[STAGE15][CAPTURE-PENDING] diagnostic="
               << static_cast<unsigned int>(renderer.Stage15Diagnostic())
               << " state="
               << static_cast<unsigned int>(renderer.CaptureState())
               << " samples=" << renderer.CaptureCompletedSamples()
               << " locked=" << (renderer.SceneInputLocked() ? 1 : 0)
               << " cloud=" << renderer.CloudRenderWidth() << 'x'
               << renderer.CloudRenderHeight() << " temporal="
               << static_cast<unsigned int>(renderer.TemporalMode())
               << " step=" << renderer.CloudSettings().stepSize
               << " max=" << renderer.CloudSettings().maxViewSteps;
        WriteDiagnosticLine(detail.str());
    }

    // 진단 중에는 F4 요청/Q/T뿐 아니라 F1/F3가 사용하는 기존 품질 setter도
    // 완전한 no-op이어야 한다. fingerprint뿐 아니라 transaction commit 수도
    // 그대로여야 나중에 Restore 뒤 stale 품질 요청이 적용되지 않는다.
    const std::uint64_t captureBlockedFingerprint =
        renderer.Stage15StateFingerprint();
    const std::uint64_t captureBlockedCommits =
        renderer.Stage15TransitionCommitCount();
    renderer.RequestStage15QualityPreset(Stage15QualityPreset::High);
    renderer.CycleStage15QualityPreset();
    renderer.ToggleStage15TemporalOverride();
    renderer.ApplyStage9OptimizationPreset(
        Stage9OptimizationPreset::ApprovedReference);
    renderer.ConfigureStage9ConeForValidation(12u, 1.0f, 0.95f);
    renderer.ApplyStage10ResolutionPreset(Stage10ResolutionPreset::TwoThirds);
    renderer.SetStage10UpsampleFilter(Stage10UpsampleFilter::Nearest);
    renderer.SetStage11TemporalMode(Stage11TemporalMode::Stable4Phase);
    renderer.SetLightSampling(320u, 62.5f);
    renderer.SetCloudLodForValidation(true, 24000.0f, 40000.0f);
    renderer.SetViewSamplingForSmoke(2048u, 25.0f);
    renderer.SetStage12ShadowMode(Stage12ShadowMode::DirectReference);
    const bool shadowPresetSetterRejected =
        !renderer.SetStage12ShadowPreset(Stage12ShadowPreset::Fast256);
    renderer.Render(camera, timeSeconds);
    const bool qualityBlockedDuringDiagnosticContract =
        shadowPresetSetterRejected &&
        renderer.Stage15StateFingerprint() == captureBlockedFingerprint &&
        renderer.Stage15TransitionCommitCount() == captureBlockedCommits &&
        renderer.Stage15Diagnostic() == Stage15DiagnosticMode::CaptureStill &&
        renderer.Stage15Quality() == Stage15QualityPreset::Medium &&
        renderer.TemporalMode() == Stage11TemporalMode::Off &&
        renderer.Stage15TemporalOverrideActive();

    // Capture 누적 중에는 Concept도 장면 상태이므로 차단한다.
    const std::uint64_t captureConceptWeatherHash =
        renderer.WeatherMapHash();
    const std::uint64_t captureConceptCommits =
        renderer.Stage15TransitionCommitCount();
    renderer.RequestStage15ConceptPreset(Stage15ConceptPreset::SnowOvercast);
    renderer.Render(camera, timeSeconds);
    const bool conceptBlockedDuringCaptureContract =
        renderer.Stage15Concept() == Stage15ConceptPreset::DesertCirrus &&
        renderer.Stage15Diagnostic() == Stage15DiagnosticMode::CaptureStill &&
        renderer.Stage15Quality() == Stage15QualityPreset::Medium &&
        renderer.TemporalMode() == Stage11TemporalMode::Off &&
        renderer.Stage15TemporalOverrideActive() &&
        renderer.WeatherMapHash() == captureConceptWeatherHash &&
        renderer.Stage15TransitionCommitCount() == captureConceptCommits;

    renderer.RequestStage15DiagnosticMode(Stage15DiagnosticMode::Reference);
    renderer.Render(camera, timeSeconds);
    const std::uint64_t referenceConceptCommits =
        renderer.Stage15TransitionCommitCount();
    renderer.RequestStage15ConceptPreset(Stage15ConceptPreset::SnowOvercast);
    renderer.Render(camera, timeSeconds);
    const bool conceptAllowedDuringReferenceContract =
        renderer.Stage15Concept() == Stage15ConceptPreset::SnowOvercast &&
        renderer.Stage15Diagnostic() == Stage15DiagnosticMode::Reference &&
        renderer.Stage15TransitionCommitCount() ==
            referenceConceptCommits + 1u;
    const bool referenceContract =
        renderer.Stage15Diagnostic() == Stage15DiagnosticMode::Reference &&
        renderer.Stage15Concept() == Stage15ConceptPreset::SnowOvercast &&
        renderer.ShadowMode() == Stage12ShadowMode::DirectReference &&
        renderer.OptimizationPreset() ==
            Stage9OptimizationPreset::FineReference;
    renderer.RequestStage15DiagnosticMode(Stage15DiagnosticMode::None);
    renderer.Render(camera, timeSeconds);
    const bool restoreContract =
        renderer.Stage15Diagnostic() == Stage15DiagnosticMode::None &&
        renderer.Stage15Concept() == Stage15ConceptPreset::SnowOvercast &&
        renderer.Stage15Quality() == Stage15QualityPreset::Medium &&
        renderer.Stage15TemporalOverrideActive() &&
        renderer.TemporalMode() == Stage11TemporalMode::Off &&
        renderer.ResolutionPreset() == Stage10ResolutionPreset::Half &&
        renderer.CloudRenderWidth() == preCaptureCloudWidth &&
        renderer.CloudRenderHeight() == preCaptureCloudHeight &&
        renderer.CloudSettings().stepSize == 100.0f &&
        renderer.CloudSettings().maxViewSteps == 512u &&
        renderer.ShadowMode() == Stage12ShadowMode::DeepCache;
    if (!restoreContract)
    {
        std::ostringstream detail;
        detail << "[STAGE15][RESTORE] diagnostic="
               << static_cast<unsigned int>(renderer.Stage15Diagnostic())
               << " concept="
               << static_cast<unsigned int>(renderer.Stage15Concept())
               << " quality="
               << static_cast<unsigned int>(renderer.Stage15Quality())
               << " override="
               << (renderer.Stage15TemporalOverrideActive() ? 1 : 0)
               << " temporal="
               << static_cast<unsigned int>(renderer.TemporalMode())
               << " resolution="
               << static_cast<unsigned int>(renderer.ResolutionPreset())
               << " cloud=" << renderer.CloudRenderWidth() << 'x'
               << renderer.CloudRenderHeight();
        WriteDiagnosticLine(detail.str());
    }
    const std::uint64_t restoredFingerprint = renderer.Stage15StateFingerprint();
    const std::uint64_t restoredCommits =
        renderer.Stage15TransitionCommitCount();
    renderer.Render(camera, timeSeconds);
    const bool noStaleRequestAfterRestoreContract =
        renderer.Stage15StateFingerprint() == restoredFingerprint &&
        renderer.Stage15TransitionCommitCount() == restoredCommits &&
        renderer.Stage15Quality() == Stage15QualityPreset::Medium &&
        renderer.Stage15Concept() == Stage15ConceptPreset::SnowOvercast;
    const bool restoreAfterBlockedInputContract =
        preDiagnosticRealtimeState && qualityBlockedDuringDiagnosticContract &&
        conceptBlockedDuringCaptureContract &&
        conceptAllowedDuringReferenceContract && restoreContract &&
        noStaleRequestAfterRestoreContract;

    // 이후 schema/idempotency 계약은 이름 그대로 Desert/Medium/Realtime/
    // Temporal On canonical 상태에서 측정한다.
    renderer.ToggleStage15TemporalOverride();
    renderer.RequestStage15ConceptPreset(Stage15ConceptPreset::DesertCirrus);
    renderer.RequestStage15QualityPreset(Stage15QualityPreset::Medium);
    renderer.Render(camera, timeSeconds);

    const bool schemaFixtureState =
        renderer.Stage15Concept() == Stage15ConceptPreset::DesertCirrus &&
        renderer.Stage15Quality() == Stage15QualityPreset::Medium &&
        renderer.Stage15Diagnostic() == Stage15DiagnosticMode::None &&
        renderer.TemporalMode() == Stage11TemporalMode::Stable4Phase &&
        !renderer.Stage15TemporalOverrideActive() &&
        static_cast<CloudShapeMode>(renderer.ShapeSettings().shapeMode) ==
            CloudShapeMode::CirrusPhysicalLayer;
    const std::filesystem::path schemaExportRoot =
        std::filesystem::temp_directory_path() /
        (L"VolumetricCloudStage15Schema37-" +
         std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(GetTickCount64()));
    const bool schemaMetadataExported =
        renderer.ExportNoiseLabSnapshot(schemaExportRoot, false);
    bool schemaMetadataFound = false;
    bool schemaPngFound = false;
    std::error_code schemaExportError;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             schemaExportRoot, schemaExportError))
    {
        if (schemaExportError)
            break;
        if (entry.path().extension() == L".png")
            schemaPngFound = true;
        if (entry.path().filename() != L"noise-settings.json")
            continue;

        std::string metadata;
        if (!ReadTextFile(entry.path(), metadata))
            continue;
        const auto contains = [&metadata](const char* value)
        {
            return metadata.find(value) != std::string::npos;
        };
        schemaMetadataFound =
            contains("\"schemaVersion\": 37") &&
            contains("\"stage15\": {\"quality\": \"Medium\", \"concept\": \"Desert Cirrus\", \"diagnosticMode\": \"None\", \"temporalOverrideActive\": false}") &&
            contains("\"cloudShape\": {\"mode\": \"cirrusPhysicalLayer\"") &&
            contains("\"flowDirectionXZ\": [0.939693, 0.342020]") &&
            contains("\"baseScaleMeters\": [20000.000000, 4000.000000, 1000.000000]") &&
            contains("\"detailScaleMeters\": [6000.000000, 1500.000000, 500.000000]") &&
            contains("\"thicknessMeters\": [500.000000, 1500.000000]") &&
            contains("\"verticalProfile\": [0.480000, 0.450000]") &&
            contains("\"physicalAdvectionMode\": \"rigidSharedWindSpeed\"") &&
            contains("\"weatherMapWindSpeedUsage\": \"legacyOnlyIgnored\"") &&
            contains("\"detailWindSpeedUsage\": \"legacyOnlyIgnored\"");
    }
    const bool schema37CirrusContract = schemaFixtureState &&
        schemaMetadataExported && !schemaExportError && schemaMetadataFound;
    const bool metadataOnlyNoPngContract = schemaMetadataExported &&
        !schemaExportError && !schemaPngFound;

    // 같은 최종 상태를 100프레임 연속 요청해도 GPU upload와 history reset을
    // 새로 만들지 않는 transaction idempotency를 검사한다.
    const std::uint64_t uploadsBeforeNoop = renderer.WeatherUploadCount();
    const std::uint64_t commitsBeforeNoop =
        renderer.Stage15TransitionCommitCount();
    const std::uint64_t fingerprintBeforeNoop =
        renderer.Stage15StateFingerprint();
    for (int iteration = 0; iteration < 100; ++iteration)
    {
        renderer.RequestStage15ConceptPreset(
            Stage15ConceptPreset::DesertCirrus);
        renderer.RequestStage15QualityPreset(Stage15QualityPreset::Medium);
        renderer.Render(camera, timeSeconds);
    }
    const bool idempotentContract =
        renderer.WeatherUploadCount() == uploadsBeforeNoop &&
        renderer.Stage15TransitionCommitCount() == commitsBeforeNoop &&
        renderer.Stage15StateFingerprint() == fingerprintBeforeNoop;

    const std::uint64_t uploadsBeforeCombined = renderer.WeatherUploadCount();
    const std::uint64_t commitsBeforeCombined =
        renderer.Stage15TransitionCommitCount();
    renderer.RequestStage15ConceptPreset(
        Stage15ConceptPreset::UrbanFairWeather);
    renderer.RequestStage15QualityPreset(Stage15QualityPreset::Low);
    renderer.Render(camera, timeSeconds);
    const bool combinedCommitContract =
        renderer.Stage15Concept() ==
            Stage15ConceptPreset::UrbanFairWeather &&
        renderer.Stage15Quality() == Stage15QualityPreset::Low &&
        renderer.Stage15TransitionCommitCount() ==
            commitsBeforeCombined + 1u &&
        renderer.WeatherUploadCount() <= uploadsBeforeCombined + 1u;

    struct TransitionSnapshot
    {
        Stage15ConceptPreset concept;
        Stage15QualityPreset quality;
        Stage15DiagnosticMode diagnostic;
        bool temporalOverrideActive;
        std::uint64_t weatherHash;
        std::uintptr_t weatherTexture;
        std::uintptr_t weatherSrv;
        std::uint64_t weatherUploads;
        std::uint64_t commits;
        std::uint64_t stateFingerprint;
        std::uint64_t resourceIdentityFingerprint;
        std::size_t shadowBytes;
        int cloudWidth;
        int cloudHeight;
    };
    const auto transitionSnapshot = [&]()
    {
        return TransitionSnapshot{
            renderer.Stage15Concept(), renderer.Stage15Quality(),
            renderer.Stage15Diagnostic(),
            renderer.Stage15TemporalOverrideActive(),
            renderer.WeatherMapHash(), renderer.WeatherTextureIdentity(),
            renderer.WeatherSrvIdentity(), renderer.WeatherUploadCount(),
            renderer.Stage15TransitionCommitCount(),
            renderer.Stage15StateFingerprint(),
            renderer.Stage15GpuResourceIdentityFingerprint(),
            renderer.ShadowCacheBytes(), renderer.CloudRenderWidth(),
            renderer.CloudRenderHeight() };
    };
    const auto sameTransitionSnapshot = [](const TransitionSnapshot& a,
                                           const TransitionSnapshot& b)
    {
        return a.concept == b.concept && a.quality == b.quality &&
            a.diagnostic == b.diagnostic &&
            a.temporalOverrideActive == b.temporalOverrideActive &&
            a.weatherHash == b.weatherHash &&
            a.weatherTexture == b.weatherTexture &&
            a.weatherSrv == b.weatherSrv &&
            a.weatherUploads == b.weatherUploads && a.commits == b.commits &&
            a.stateFingerprint == b.stateFingerprint &&
            a.resourceIdentityFingerprint == b.resourceIdentityFingerprint &&
            a.shadowBytes == b.shadowBytes && a.cloudWidth == b.cloudWidth &&
            a.cloudHeight == b.cloudHeight;
    };
    const TransitionSnapshot stableTransition = transitionSnapshot();
    renderer.InjectStage15TransitionFailureForTest(
        Stage15TransitionFailurePoint::WeatherPreflight);
    renderer.RequestStage15ConceptPreset(
        Stage15ConceptPreset::MeadowBrokenClouds);
    renderer.RequestStage15QualityPreset(Stage15QualityPreset::High);
    renderer.Render(camera, timeSeconds);
    const bool weatherRollback = renderer.Stage15TransitionFailed() &&
        sameTransitionSnapshot(stableTransition, transitionSnapshot());

    renderer.InjectStage15TransitionFailureForTest(
        Stage15TransitionFailurePoint::CloudTargetPreflight);
    renderer.RequestStage15ConceptPreset(
        Stage15ConceptPreset::UrbanFairWeather);
    renderer.RequestStage15QualityPreset(Stage15QualityPreset::High);
    renderer.Render(camera, timeSeconds);
    const bool cloudRollback = renderer.Stage15TransitionFailed() &&
        sameTransitionSnapshot(stableTransition, transitionSnapshot());

    renderer.InjectStage15TransitionFailureForTest(
        Stage15TransitionFailurePoint::ShadowResourcePreflight);
    renderer.RequestStage15QualityPreset(Stage15QualityPreset::High);
    renderer.Render(camera, timeSeconds);
    const bool shadowRollback = renderer.Stage15TransitionFailed() &&
        sameTransitionSnapshot(stableTransition, transitionSnapshot());

    // retained High 요청을 현재 Low로 덮고, Concept가 먼저 적용된 뒤
    // Reference의 Balanced512 preflight에서 실패하게 만든다. CPU 값뿐
    // 아니라 Weather 픽셀과 교체된 D3D 객체 identity까지 원상복구해야 한다.
    renderer.RequestStage15QualityPreset(Stage15QualityPreset::Low);
    renderer.RequestStage15ConceptPreset(
        Stage15ConceptPreset::MeadowBrokenClouds);
    renderer.RequestStage15DiagnosticMode(Stage15DiagnosticMode::Reference);
    renderer.InjectStage15TransitionFailureForTest(
        Stage15TransitionFailurePoint::ShadowResourcePreflight);
    renderer.Render(camera, timeSeconds);
    const bool mixedTailRollback = renderer.Stage15TransitionFailed() &&
        sameTransitionSnapshot(stableTransition, transitionSnapshot());
    const bool failureRollbackContract = weatherRollback && cloudRollback &&
        shadowRollback && mixedTailRollback;
    passed = passed && overlayDefaultsContract &&
        temporalOverrideRoundTripContract &&
        temporalDirectEditClearsOverrideContract &&
        temporalOverrideCustomRoundTripContract &&
        namedQualityCanonicalRestoreContract && cirrusPassed &&
        captureContract && qualityBlockedDuringDiagnosticContract &&
        conceptBlockedDuringCaptureContract &&
        conceptAllowedDuringReferenceContract && referenceContract &&
        restoreContract && noStaleRequestAfterRestoreContract &&
        restoreAfterBlockedInputContract && schema37CirrusContract &&
        metadataOnlyNoPngContract && idempotentContract &&
        combinedCommitContract && failureRollbackContract &&
        !renderer.HasDebugLayerErrors();

    const std::filesystem::path directory =
        std::filesystem::path(VCLOUD_SHADER_SOURCE_DIR).parent_path() /
        "captures" / "stage15";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error)
        return 4;
    std::ofstream csv(directory / "preset-smoke.csv",
                      std::ios::binary | std::ios::trunc);
    csv << "concept,quality,camera,weather_hash,frame_hash,finite,resources,history_reset\n";
    for (const Result& result : results)
        csv << '"' << result.concept << "\",\"" << result.quality
            << "\",\"" << result.camera << "\"," << result.weatherHash
            << ',' << result.frameHash << ',' << (result.finite ? 1 : 0)
            << ',' << (result.resources ? 1 : 0) << ','
            << (result.historyReset ? 1 : 0) << '\n';
    std::ofstream json(directory / "preset-smoke.json",
                       std::ios::binary | std::ios::trunc);
    json << "{\n  \"adapter\": \"" << renderer.AdapterName()
         << "\",\n  \"driver\": \"" << renderer.DriverVersion()
         << "\",\n  \"cases\": " << results.size()
         << ",\n  \"cirrusInsideMaxima\": {\"hit\": "
         << cirrusMaxima[0] << ", \"rawNoise\": " << cirrusMaxima[1]
         << ", \"weatherCoverage\": " << cirrusMaxima[2]
         << ", \"localHeight\": " << cirrusMaxima[3]
         << ", \"profile\": " << cirrusMaxima[4]
         << ", \"baseDensity\": " << cirrusMaxima[5]
         << ", \"finalDensity\": " << cirrusMaxima[6] << "}"
         << ",\n  \"diagnosticContracts\": {\"overlayDefaults\": "
         << (overlayDefaultsContract ? "true" : "false")
         << ", \"temporalOverrideRoundTrip\": "
         << (temporalOverrideRoundTripContract ? "true" : "false")
         << ", \"temporalDirectEditClearsOverride\": "
         << (temporalDirectEditClearsOverrideContract ? "true" : "false")
         << ", \"temporalOverrideCustomRoundTrip\": "
         << (temporalOverrideCustomRoundTripContract ? "true" : "false")
         << ", \"namedQualityCanonicalRestore\": "
         << (namedQualityCanonicalRestoreContract ? "true" : "false")
         << ", \"capture\": "
         << (captureContract ? "true" : "false")
         << ", \"qualityBlockedDuringDiagnostic\": "
         << (qualityBlockedDuringDiagnosticContract ? "true" : "false")
         << ", \"conceptBlockedDuringCapture\": "
         << (conceptBlockedDuringCaptureContract ? "true" : "false")
         << ", \"conceptAllowedDuringReference\": "
         << (conceptAllowedDuringReferenceContract ? "true" : "false")
         << ", \"reference\": "
         << (referenceContract ? "true" : "false")
         << ", \"restore\": "
         << (restoreContract ? "true" : "false")
         << ", \"noStaleRequestAfterRestore\": "
         << (noStaleRequestAfterRestoreContract ? "true" : "false")
         << ", \"restoreAfterBlockedInput\": "
         << (restoreAfterBlockedInputContract ? "true" : "false")
         << ", \"cirrusSchema37\": "
         << (schema37CirrusContract ? "true" : "false")
         << ", \"metadataOnlyNoPng\": "
         << (metadataOnlyNoPngContract ? "true" : "false")
         << ", \"idempotent100\": "
         << (idempotentContract ? "true" : "false")
         << ", \"combinedCommitOnce\": "
         << (combinedCommitContract ? "true" : "false")
         << ", \"failureRollback\": "
         << (failureRollbackContract ? "true" : "false")
         << ", \"mixedTailRollback\": "
         << (mixedTailRollback ? "true" : "false") << "}"
         << ",\n  \"debugLayerPassed\": "
         << (!renderer.HasDebugLayerErrors() ? "true" : "false")
         << ",\n  \"passed\": " << (passed ? "true" : "false")
         << "\n}\n";
    if (!csv.good() || !json.good())
        return 5;

    WriteDiagnosticLine(std::string("STAGE15_PRESET_SMOKE CASES=") +
        std::to_string(results.size()) + " CIRRUS_MAX=" +
        std::to_string(cirrusMaximum) + (passed ? " PASS" : " FAIL"));
    return passed ? 0 : 9;
}

struct Stage15ImageMetric
{
    std::size_t imagePixelCount = 0u;
    double ssim = 0.0;
    double normalizedRmse = std::numeric_limits<double>::infinity();
    double transmittanceMae = std::numeric_limits<double>::infinity();
    double transmittanceP99 = std::numeric_limits<double>::infinity();
    // mask는 후보 영상이 아니라 Reference T에서만 만든다. 후보마다 mask가
    // 달라지면 흐릿한 후보가 어려운 픽셀을 스스로 제외할 수 있기 때문이다.
    std::size_t cloudMaskPixelCount = 0u;
    double cloudMaskNormalizedRmse = 0.0;
    double cloudMaskTransmittanceMae = 0.0;
    std::size_t cloudEdgePixelCount = 0u;
    double cloudEdgeNormalizedRmse = 0.0;
    double cloudEdgeTransmittanceMae = 0.0;
    bool finite = false;
};

constexpr double kStage15CloudOpacityThreshold = 0.01;
constexpr double kStage15CloudEdgeGradientThreshold = 0.002;

Stage15ImageMetric CompareStage15Images(
    const CloudDiagnosticFrame& referenceComposite,
    const CloudDiagnosticFrame& currentComposite,
    const CloudDiagnosticFrame& referenceTransmittance,
    const CloudDiagnosticFrame& currentTransmittance)
{
    Stage15ImageMetric result;
    const std::size_t pixelCount = referenceComposite.pixels.size();
    if (pixelCount == 0u ||
        referenceComposite.width <= 0 || referenceComposite.height <= 0 ||
        referenceComposite.width != currentComposite.width ||
        referenceComposite.height != currentComposite.height ||
        referenceComposite.width != referenceTransmittance.width ||
        referenceComposite.height != referenceTransmittance.height ||
        referenceComposite.width != currentTransmittance.width ||
        referenceComposite.height != currentTransmittance.height ||
        currentComposite.pixels.size() != pixelCount ||
        referenceTransmittance.pixels.size() != pixelCount ||
        currentTransmittance.pixels.size() != pixelCount)
        return result;
    result.imagePixelCount = pixelCount;

    const auto luminance = [](const DirectX::XMFLOAT4& value)
    {
        return 0.2126 * static_cast<double>(value.x) +
               0.7152 * static_cast<double>(value.y) +
               0.0722 * static_cast<double>(value.z);
    };
    std::vector<double> referenceMagnitudes;
    std::vector<double> transmittanceErrors;
    referenceMagnitudes.reserve(pixelCount);
    transmittanceErrors.reserve(pixelCount);
    double referenceMean = 0.0;
    double currentMean = 0.0;
    double rgbSquaredError = 0.0;
    double cloudRgbSquaredError = 0.0;
    double cloudTransmittanceAbsoluteError = 0.0;
    double edgeRgbSquaredError = 0.0;
    double edgeTransmittanceAbsoluteError = 0.0;
    result.finite = true;
    const int width = referenceComposite.width;
    const int height = referenceComposite.height;
    const auto referenceT = [&](int x, int y)
    {
        const std::size_t index = static_cast<std::size_t>(y) *
            static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
        return std::clamp(static_cast<double>(
            referenceTransmittance.pixels[index].x), 0.0, 1.0);
    };
    for (std::size_t index = 0; index < pixelCount; ++index)
    {
        const DirectX::XMFLOAT4& reference =
            referenceComposite.pixels[index];
        const DirectX::XMFLOAT4& current =
            currentComposite.pixels[index];
        const double referenceLuminance = luminance(reference);
        const double currentLuminance = luminance(current);
        const double transmittanceError = std::abs(
            static_cast<double>(referenceTransmittance.pixels[index].x) -
            currentTransmittance.pixels[index].x);
        result.finite = result.finite &&
            std::isfinite(referenceLuminance) &&
            std::isfinite(currentLuminance) &&
            std::isfinite(transmittanceError);
        referenceMean += referenceLuminance;
        currentMean += currentLuminance;
        const double red = static_cast<double>(reference.x) - current.x;
        const double green = static_cast<double>(reference.y) - current.y;
        const double blue = static_cast<double>(reference.z) - current.z;
        const double rgbPixelSquaredError =
            (red * red + green * green + blue * blue) / 3.0;
        rgbSquaredError += rgbPixelSquaredError;

        const int x = static_cast<int>(index %
            static_cast<std::size_t>(width));
        const int y = static_cast<int>(index /
            static_cast<std::size_t>(width));
        const double centerT = referenceT(x, y);
        const double centerOpacity = 1.0 - centerT;
        const bool cloudPixel =
            centerOpacity >= kStage15CloudOpacityThreshold;
        bool edgePixel = false;
        if (x > 0 && x + 1 < width && y > 0 && y + 1 < height)
        {
            const double leftT = referenceT(x - 1, y);
            const double rightT = referenceT(x + 1, y);
            const double topT = referenceT(x, y - 1);
            const double bottomT = referenceT(x, y + 1);
            // 중앙차분 T gradient를 사용한다. 최소 opacity support 조건은
            // 완전히 맑은 하늘의 half-float 잡음이 edge가 되는 것을 막는다.
            const double gradientX = 0.5 * (rightT - leftT);
            const double gradientY = 0.5 * (bottomT - topT);
            const double gradient = std::sqrt(
                gradientX * gradientX + gradientY * gradientY);
            const double maximumNeighborOpacity = std::max({
                centerOpacity, 1.0 - leftT, 1.0 - rightT,
                1.0 - topT, 1.0 - bottomT });
            edgePixel = maximumNeighborOpacity >=
                    kStage15CloudOpacityThreshold &&
                gradient >= kStage15CloudEdgeGradientThreshold;
        }
        if (cloudPixel)
        {
            ++result.cloudMaskPixelCount;
            cloudRgbSquaredError += rgbPixelSquaredError;
            cloudTransmittanceAbsoluteError += transmittanceError;
        }
        if (edgePixel)
        {
            ++result.cloudEdgePixelCount;
            edgeRgbSquaredError += rgbPixelSquaredError;
            edgeTransmittanceAbsoluteError += transmittanceError;
        }
        referenceMagnitudes.push_back(std::max(
            std::abs(referenceLuminance), 0.0));
        transmittanceErrors.push_back(transmittanceError);
        result.transmittanceMae += transmittanceError;
    }
    if (!result.finite)
        return result;

    referenceMean /= static_cast<double>(pixelCount);
    currentMean /= static_cast<double>(pixelCount);
    double referenceVariance = 0.0;
    double currentVariance = 0.0;
    double covariance = 0.0;
    for (std::size_t index = 0; index < pixelCount; ++index)
    {
        const double referenceDelta =
            luminance(referenceComposite.pixels[index]) - referenceMean;
        const double currentDelta =
            luminance(currentComposite.pixels[index]) - currentMean;
        referenceVariance += referenceDelta * referenceDelta;
        currentVariance += currentDelta * currentDelta;
        covariance += referenceDelta * currentDelta;
    }
    const double varianceDivisor = static_cast<double>(
        std::max<std::size_t>(pixelCount - 1u, 1u));
    referenceVariance /= varianceDivisor;
    currentVariance /= varianceDivisor;
    covariance /= varianceDivisor;
    // LDR 구간은 1.0을 기준으로, HDR highlight가 1을 넘을 때만 reference
    // P99를 확장 범위로 쓴다. 어두운 overcast 장면을 작은 최대값으로 나누면
    // 같은 절대 오차를 인위적으로 수 배 확대하게 된다.
    const double dynamicRange = std::max(
        FramePercentile(referenceMagnitudes, 0.99), 1.0);
    const double c1 = std::pow(0.01 * dynamicRange, 2.0);
    const double c2 = std::pow(0.03 * dynamicRange, 2.0);
    result.ssim = ((2.0 * referenceMean * currentMean + c1) *
                   (2.0 * covariance + c2)) /
        ((referenceMean * referenceMean + currentMean * currentMean + c1) *
         (referenceVariance + currentVariance + c2));
    result.normalizedRmse = std::sqrt(
        rgbSquaredError / static_cast<double>(pixelCount)) / dynamicRange;
    result.transmittanceMae = std::accumulate(
        transmittanceErrors.begin(), transmittanceErrors.end(), 0.0) /
        static_cast<double>(pixelCount);
    result.transmittanceP99 = FramePercentile(transmittanceErrors, 0.99);
    if (result.cloudMaskPixelCount > 0u)
    {
        const double count = static_cast<double>(result.cloudMaskPixelCount);
        result.cloudMaskNormalizedRmse =
            std::sqrt(cloudRgbSquaredError / count) / dynamicRange;
        result.cloudMaskTransmittanceMae =
            cloudTransmittanceAbsoluteError / count;
    }
    if (result.cloudEdgePixelCount > 0u)
    {
        const double count = static_cast<double>(result.cloudEdgePixelCount);
        result.cloudEdgeNormalizedRmse =
            std::sqrt(edgeRgbSquaredError / count) / dynamicRange;
        result.cloudEdgeTransmittanceMae =
            edgeTransmittanceAbsoluteError / count;
    }
    result.finite = result.finite &&
        std::isfinite(result.ssim) &&
        std::isfinite(result.normalizedRmse) &&
        std::isfinite(result.transmittanceMae) &&
        std::isfinite(result.transmittanceP99) &&
        std::isfinite(result.cloudMaskNormalizedRmse) &&
        std::isfinite(result.cloudMaskTransmittanceMae) &&
        std::isfinite(result.cloudEdgeNormalizedRmse) &&
        std::isfinite(result.cloudEdgeTransmittanceMae);
    return result;
}

bool PassStage15ImageGate(Stage15QualityPreset quality,
                          const Stage15ImageMetric& metric)
{
    if (!metric.finite)
        return false;
    if (quality == Stage15QualityPreset::Low)
        return metric.ssim >= 0.97 && metric.normalizedRmse <= 0.03 &&
            metric.transmittanceMae <= 0.03 &&
            metric.transmittanceP99 <= 0.08;
    return metric.ssim >= 0.99 && metric.normalizedRmse <= 0.01 &&
        metric.transmittanceMae <= 0.01 &&
        metric.transmittanceP99 <= 0.03;
}

int RunStage15QualityTest(Renderer& renderer, Camera& camera)
{
    struct Result
    {
        std::string concept;
        std::string camera;
        std::string candidate;
        Stage15QualityPreset gateQuality = Stage15QualityPreset::Medium;
        int accumulatedFrames = 1;
        int outputWidth = 0;
        int outputHeight = 0;
        int cloudRtWidth = 0;
        int cloudRtHeight = 0;
        bool cloudRtFull = false;
        std::uint32_t historyAge = 0u;
        std::uint32_t resetCountLast60Frames = 0u;
        Stage15ImageMetric metric;
        bool passed = false;
    };

    struct MetricAggregate
    {
        double normalizedSquaredError = 0.0;
        double transmittanceAbsoluteError = 0.0;
        std::size_t pixelCount = 0u;

        void AddImage(const Stage15ImageMetric& metric)
        {
            normalizedSquaredError += metric.normalizedRmse *
                metric.normalizedRmse *
                static_cast<double>(metric.imagePixelCount);
            transmittanceAbsoluteError += metric.transmittanceMae *
                static_cast<double>(metric.imagePixelCount);
            pixelCount += metric.imagePixelCount;
        }

        void AddEdge(const Stage15ImageMetric& metric)
        {
            normalizedSquaredError += metric.cloudEdgeNormalizedRmse *
                metric.cloudEdgeNormalizedRmse *
                static_cast<double>(metric.cloudEdgePixelCount);
            transmittanceAbsoluteError +=
                metric.cloudEdgeTransmittanceMae *
                static_cast<double>(metric.cloudEdgePixelCount);
            pixelCount += metric.cloudEdgePixelCount;
        }

        double NormalizedRmse() const
        {
            return pixelCount > 0u
                ? std::sqrt(normalizedSquaredError /
                    static_cast<double>(pixelCount)) : 0.0;
        }

        double TransmittanceMae() const
        {
            return pixelCount > 0u
                ? transmittanceAbsoluteError /
                    static_cast<double>(pixelCount) : 0.0;
        }
    };

    renderer.SetNoiseLabVisible(false);
    renderer.EnableNoiseLabPreviews(false);
    renderer.SetAutomatedRenderMode(true);
    renderer.SetVSyncEnabled(false);
    renderer.SetOpaqueSceneForTest(true);
    camera.SetClipPlanes(
        stage13camera::kNearPlaneMeters, stage13camera::kFarPlaneMeters);

    if (!renderer.ApplyStage15Defaults())
        return 2;
    // 1-frame 품질 비교는 sampling 오차만 보도록 T override를 Off로 고정한다.
    renderer.ToggleStage15TemporalOverride();
    renderer.Render(camera, 0.0f);
    if (renderer.Stage15TransitionFailed() ||
        renderer.Stage15Diagnostic() != Stage15DiagnosticMode::None ||
        renderer.Stage15Quality() != Stage15QualityPreset::Medium ||
        renderer.TemporalMode() != Stage11TemporalMode::Off ||
        !renderer.Stage15TemporalOverrideActive())
        return 3;

    constexpr Stage15ConceptPreset concepts[] = {
        Stage15ConceptPreset::UrbanFairWeather,
        Stage15ConceptPreset::MeadowBrokenClouds,
        Stage15ConceptPreset::DesertCirrus,
        Stage15ConceptPreset::SnowOvercast,
    };
    constexpr Stage15QualityPreset qualities[] = {
        Stage15QualityPreset::Low,
        Stage15QualityPreset::Medium,
        Stage15QualityPreset::High,
    };
    std::vector<Result> results;
    bool allPassed = true;
    bool highNotWorsePassed = true;
    bool highFullRtPassed = true;
    bool highEdgeImprovementPassed = false;
    bool temporal16ImprovementPassed = false;
    bool historyAgeProgressedPassed = false;
    bool noUnexpectedResetPassed = false;
    bool resolvedReadbackPassed = true;
    bool historyContractMeasured = false;
    std::uint32_t historyAgeAt4 = 0u;
    std::uint32_t historyAgeAt16 = 0u;
    std::uint32_t resetCountAt4 = 0u;
    std::uint32_t resetCountAt16 = 0u;
    MetricAggregate mediumEdgeAggregate;
    MetricAggregate highEdgeAggregate;
    MetricAggregate temporal4Aggregate;
    MetricAggregate temporal16Aggregate;
    MetricAggregate temporal4EdgeAggregate;
    MetricAggregate temporal16EdgeAggregate;
    bool referenceCompositeProbed = false;
    std::array<bool, 3> qualityCompositeProbed = {};
    const auto readbackPairIsValid = [](
        const CloudDiagnosticFrame& scattering,
        const CloudDiagnosticFrame& transmittance)
    {
        if (scattering.pixels.empty() ||
            scattering.pixels.size() != transmittance.pixels.size() ||
            HashDiagnosticFrame(scattering) ==
                HashDiagnosticFrame(transmittance))
            return false;
        for (std::size_t index = 0;
             index < scattering.pixels.size(); ++index)
        {
            const DirectX::XMFLOAT4& s = scattering.pixels[index];
            const DirectX::XMFLOAT4& t = transmittance.pixels[index];
            if (!std::isfinite(s.x) || !std::isfinite(s.y) ||
                !std::isfinite(s.z) || !std::isfinite(t.x) ||
                std::abs(t.x - t.y) > 1.0e-6f ||
                std::abs(t.x - t.z) > 1.0e-6f ||
                t.x < -1.0e-6f || t.x > 1.000001f)
                return false;
        }
        return true;
    };
    for (Stage15ConceptPreset concept : concepts)
    {
        for (const Stage13CameraPreset& cameraPreset :
             stage13camera::kOpenWorldPresets)
        {
            camera.SetLookAt(cameraPreset.position, cameraPreset.target);
            renderer.RequestStage15ConceptPreset(concept);
            renderer.RequestStage15DiagnosticMode(
                Stage15DiagnosticMode::Reference);
            renderer.Render(camera, 0.0f);
            if (renderer.Stage15TransitionFailed() ||
                renderer.Stage15Concept() != concept ||
                renderer.Stage15Diagnostic() !=
                    Stage15DiagnosticMode::Reference ||
                renderer.ShadowMode() != Stage12ShadowMode::DirectReference)
                return 3;
            CloudDiagnosticFrame referenceComposite;
            CloudDiagnosticFrame referenceTransmittance;
            if (!renderer.CaptureCloudDiagnosticFrame(
                    camera, 0.0f, CloudDebugMode::Stage15ResolvedCloud,
                    referenceComposite) ||
                !renderer.CaptureCloudDiagnosticFrame(
                    camera, 0.0f, CloudDebugMode::Transmittance,
                    referenceTransmittance))
                return 3;
            resolvedReadbackPassed = resolvedReadbackPassed &&
                readbackPairIsValid(
                    referenceComposite, referenceTransmittance);
            if (!referenceCompositeProbed)
            {
                CloudDiagnosticFrame displayedComposite;
                if (!renderer.CaptureCloudDiagnosticFrame(
                        camera, 0.0f, CloudDebugMode::Composite,
                        displayedComposite))
                    return 3;
                resolvedReadbackPassed = resolvedReadbackPassed &&
                    HashDiagnosticFrame(referenceComposite) !=
                        HashDiagnosticFrame(displayedComposite);
                referenceCompositeProbed = true;
            }
            renderer.RequestStage15DiagnosticMode(Stage15DiagnosticMode::None);
            renderer.Render(camera, 0.0f);
            if (renderer.Stage15TransitionFailed() ||
                renderer.Stage15Concept() != concept ||
                renderer.Stage15Diagnostic() != Stage15DiagnosticMode::None ||
                renderer.TemporalMode() != Stage11TemporalMode::Off ||
                !renderer.Stage15TemporalOverrideActive())
                return 3;

            std::array<Stage15ImageMetric, 3> realtimeMetrics = {};
            for (std::size_t qualityIndex = 0;
                 qualityIndex < std::size(qualities); ++qualityIndex)
            {
                const Stage15QualityPreset quality = qualities[qualityIndex];
                renderer.RequestStage15QualityPreset(quality);
                renderer.Render(camera, 0.0f);
                if (renderer.Stage15TransitionFailed() ||
                    renderer.Stage15Concept() != concept ||
                    renderer.Stage15Quality() != quality ||
                    renderer.Stage15Diagnostic() !=
                        Stage15DiagnosticMode::None ||
                    renderer.TemporalMode() != Stage11TemporalMode::Off)
                    return 4;
                const Stage15OutputExtentSnapshot outputExtent =
                    renderer.OutputExtentSnapshot();
                const bool cloudRtFull =
                    renderer.ResolutionPreset() ==
                        Stage10ResolutionPreset::Full &&
                    renderer.CloudRenderWidth() == outputExtent.sceneColorWidth &&
                    renderer.CloudRenderHeight() == outputExtent.sceneColorHeight;
                if (quality == Stage15QualityPreset::High)
                {
                    const bool thisHighFull = cloudRtFull &&
                        renderer.UpsampleFilter() ==
                            Stage10UpsampleFilter::Nearest &&
                        outputExtent.sceneColorWidth ==
                            referenceComposite.width &&
                        outputExtent.sceneColorHeight ==
                            referenceComposite.height;
                    highFullRtPassed = highFullRtPassed && thisHighFull;
                }
                CloudDiagnosticFrame composite;
                CloudDiagnosticFrame transmittance;
                if (!renderer.CaptureCloudDiagnosticFrame(
                        camera, 0.0f,
                        CloudDebugMode::Stage15ResolvedCloud, composite) ||
                    !renderer.CaptureCloudDiagnosticFrame(
                        camera, 0.0f, CloudDebugMode::Transmittance,
                        transmittance))
                    return 4;
                resolvedReadbackPassed = resolvedReadbackPassed &&
                    readbackPairIsValid(composite, transmittance);
                if (!qualityCompositeProbed[qualityIndex])
                {
                    CloudDiagnosticFrame displayedComposite;
                    if (!renderer.CaptureCloudDiagnosticFrame(
                            camera, 0.0f, CloudDebugMode::Composite,
                            displayedComposite))
                        return 4;
                    resolvedReadbackPassed = resolvedReadbackPassed &&
                        HashDiagnosticFrame(composite) !=
                            HashDiagnosticFrame(displayedComposite);
                    qualityCompositeProbed[qualityIndex] = true;
                }
                const Stage15ImageMetric metric = CompareStage15Images(
                    referenceComposite, composite,
                    referenceTransmittance, transmittance);
                realtimeMetrics[qualityIndex] = metric;
                const bool passed = PassStage15ImageGate(quality, metric);
                allPassed = allPassed && passed;
                results.push_back({
                    stage15::ConceptName(concept),
                    cameraPreset.diagnosticName,
                    stage15::QualityName(quality), quality, 1,
                    outputExtent.sceneColorWidth,
                    outputExtent.sceneColorHeight,
                    renderer.CloudRenderWidth(),
                    renderer.CloudRenderHeight(), cloudRtFull,
                    renderer.TemporalAccumulatedFrames(),
                    renderer.TemporalResetCountLast60Frames(),
                    metric, passed });
            }
            // GPU half-float/phase 반복의 1e-5~1e-4 차이는 gate 0.01의 1%보다
            // 작아 수치적으로 동등하게 취급한다. 실제 악화는 이 범위를 넘어야 한다.
            constexpr double kNotWorseTolerance = 1.1e-4;
            const bool highMonotonic =
                realtimeMetrics[2].normalizedRmse <=
                    realtimeMetrics[1].normalizedRmse +
                        kNotWorseTolerance &&
                realtimeMetrics[2].transmittanceMae <=
                    realtimeMetrics[1].transmittanceMae +
                        kNotWorseTolerance;
            allPassed = allPassed && highMonotonic;
            highNotWorsePassed = highNotWorsePassed && highMonotonic;
            mediumEdgeAggregate.AddEdge(realtimeMetrics[1]);
            highEdgeAggregate.AddEdge(realtimeMetrics[2]);

            // 현재 Off override를 On으로 뒤집고 Medium의 4/8/16-frame 누적을
            // 각각 history reset부터 독립적으로 재현한다.
            renderer.ToggleStage15TemporalOverride();
            renderer.RequestStage15QualityPreset(Stage15QualityPreset::Medium);
            renderer.Render(camera, 0.0f);
            if (renderer.Stage15TransitionFailed() ||
                renderer.Stage15Concept() != concept ||
                renderer.Stage15Quality() != Stage15QualityPreset::Medium ||
                renderer.Stage15Diagnostic() != Stage15DiagnosticMode::None ||
                renderer.TemporalMode() != Stage11TemporalMode::Stable4Phase ||
                renderer.Stage15TemporalOverrideActive())
                return 5;
            if (!historyContractMeasured)
            {
                // 명시적인 한 번의 reset 뒤 고정 camera/time으로 4→16 frame을
                // 연속 진행한다. 중간에 preset을 다시 적용하거나 진단 readback을
                // 하지 않으므로 여기서 reset count가 늘면 런타임 회귀다.
                renderer.ResetTemporalHistory(
                    Stage11HistoryResetReason::Manual);
                for (int frame = 0; frame < 4; ++frame)
                    renderer.Render(camera, 0.0f);
                historyAgeAt4 = renderer.TemporalAccumulatedFrames();
                resetCountAt4 =
                    renderer.TemporalResetCountLast60Frames();
                for (int frame = 4; frame < 16; ++frame)
                    renderer.Render(camera, 0.0f);
                historyAgeAt16 = renderer.TemporalAccumulatedFrames();
                resetCountAt16 =
                    renderer.TemporalResetCountLast60Frames();
                historyAgeProgressedPassed = historyAgeAt4 >= 4u &&
                    historyAgeAt16 >= historyAgeAt4 + 12u;
                // 60-frame rolling window의 오래된 reset은 빠질 수 있으므로
                // 동일 수가 아니라 '증가하지 않음'을 계약으로 삼는다.
                noUnexpectedResetPassed =
                    resetCountAt16 <= resetCountAt4 &&
                    renderer.LastTemporalResetReason() ==
                        Stage11HistoryResetReason::Manual;
                historyContractMeasured = true;
            }
            std::array<Stage15ImageMetric, 3> temporalMetrics = {};
            constexpr int frameCounts[] = { 4, 8, 16 };
            for (std::size_t frameIndex = 0;
                 frameIndex < std::size(frameCounts); ++frameIndex)
            {
                const int frameCount = frameCounts[frameIndex];
                renderer.ResetTemporalHistory();
                for (int frame = 1; frame < frameCount; ++frame)
                    renderer.Render(camera, 0.0f);
                CloudDiagnosticFrame composite;
                if (!renderer.CaptureCloudDiagnosticFrame(
                        camera, 0.0f,
                        CloudDebugMode::Stage15ResolvedCloud, composite))
                    return 5;

                renderer.ResetTemporalHistory();
                for (int frame = 1; frame < frameCount; ++frame)
                    renderer.Render(camera, 0.0f);
                CloudDiagnosticFrame transmittance;
                if (!renderer.CaptureCloudDiagnosticFrame(
                        camera, 0.0f, CloudDebugMode::Transmittance,
                        transmittance))
                    return 6;

                const Stage15ImageMetric metric = CompareStage15Images(
                    referenceComposite, composite,
                    referenceTransmittance, transmittance);
                temporalMetrics[frameIndex] = metric;
                const bool passed = PassStage15ImageGate(
                    Stage15QualityPreset::Medium, metric);
                allPassed = allPassed && passed;
                const Stage15OutputExtentSnapshot outputExtent =
                    renderer.OutputExtentSnapshot();
                results.push_back({
                    stage15::ConceptName(concept),
                    cameraPreset.diagnosticName,
                    std::string("Medium Temporal ") +
                        std::to_string(frameCount),
                    Stage15QualityPreset::Medium, frameCount,
                    outputExtent.sceneColorWidth,
                    outputExtent.sceneColorHeight,
                    renderer.CloudRenderWidth(),
                    renderer.CloudRenderHeight(),
                    renderer.ResolutionPreset() ==
                        Stage10ResolutionPreset::Full &&
                        renderer.CloudRenderWidth() ==
                            outputExtent.sceneColorWidth &&
                        renderer.CloudRenderHeight() ==
                            outputExtent.sceneColorHeight,
                    renderer.TemporalAccumulatedFrames(),
                    renderer.TemporalResetCountLast60Frames(),
                    metric, passed });
            }
            temporal4Aggregate.AddImage(temporalMetrics[0]);
            temporal16Aggregate.AddImage(temporalMetrics[2]);
            temporal4EdgeAggregate.AddEdge(temporalMetrics[0]);
            temporal16EdgeAggregate.AddEdge(temporalMetrics[2]);
            renderer.ToggleStage15TemporalOverride();
            renderer.Render(camera, 0.0f);
            if (renderer.Stage15TransitionFailed() ||
                renderer.Stage15Concept() != concept ||
                renderer.Stage15Quality() != Stage15QualityPreset::Medium ||
                renderer.Stage15Diagnostic() != Stage15DiagnosticMode::None ||
                renderer.TemporalMode() != Stage11TemporalMode::Off ||
                !renderer.Stage15TemporalOverrideActive())
                return 6;
        }
    }
    constexpr double kHighEdgeMaximumRatio = 0.95;
    const double mediumEdgeRmse = mediumEdgeAggregate.NormalizedRmse();
    const double highEdgeRmse = highEdgeAggregate.NormalizedRmse();
    const double mediumEdgeTransmittanceMae =
        mediumEdgeAggregate.TransmittanceMae();
    const double highEdgeTransmittanceMae =
        highEdgeAggregate.TransmittanceMae();
    highEdgeImprovementPassed =
        mediumEdgeAggregate.pixelCount >= 1024u &&
        highEdgeAggregate.pixelCount == mediumEdgeAggregate.pixelCount &&
        highEdgeRmse <= mediumEdgeRmse * kHighEdgeMaximumRatio &&
        highEdgeTransmittanceMae <=
            mediumEdgeTransmittanceMae * kHighEdgeMaximumRatio;

    const double temporal4Rmse = temporal4Aggregate.NormalizedRmse();
    const double temporal16Rmse = temporal16Aggregate.NormalizedRmse();
    const double temporal4TransmittanceMae =
        temporal4Aggregate.TransmittanceMae();
    const double temporal16TransmittanceMae =
        temporal16Aggregate.TransmittanceMae();
    const double temporal4EdgeRmse =
        temporal4EdgeAggregate.NormalizedRmse();
    const double temporal16EdgeRmse =
        temporal16EdgeAggregate.NormalizedRmse();
    const double temporal4EdgeTransmittanceMae =
        temporal4EdgeAggregate.TransmittanceMae();
    const double temporal16EdgeTransmittanceMae =
        temporal16EdgeAggregate.TransmittanceMae();
    // R16 history의 픽셀 ULP는 [0,1]에서 약 9.8e-4지만 1080p 평균
    // 오차의 반복 변동은 약 1e-6까지 줄어든다. 그 두 배(2e-6)를
    // '수치적으로 같음'으로 두고, 두 지표가 이보다 악화되지 않으면서
    // 적어도 하나는 이 바닥보다 더 개선되어야 16-frame 수렴으로 판정한다.
    constexpr double kTemporalAggregateTolerance = 2.0e-6;
    // 이번 회귀의 목표는 선명도이므로 수렴 pass/fail은 reference T로 고정한
    // cloudEdgeMask에서 판정한다. 전체 화면 수치도 함께 남겨 raymarch bias를
    // 숨기지 않지만, 넓은 구름 내부가 경계 수렴 판정을 지배하지 않게 한다.
    const bool temporalRmseNotWorse = temporal16EdgeRmse <=
        temporal4EdgeRmse + kTemporalAggregateTolerance;
    const bool temporalTransmittanceNotWorse =
        temporal16EdgeTransmittanceMae <=
            temporal4EdgeTransmittanceMae + kTemporalAggregateTolerance;
    const bool temporalMeaningfullyImproved =
        temporal16EdgeRmse + kTemporalAggregateTolerance <
            temporal4EdgeRmse ||
        temporal16EdgeTransmittanceMae + kTemporalAggregateTolerance <
            temporal4EdgeTransmittanceMae;
    temporal16ImprovementPassed =
        temporal4EdgeAggregate.pixelCount >= 1024u &&
        temporal16EdgeAggregate.pixelCount ==
            temporal4EdgeAggregate.pixelCount &&
        temporalRmseNotWorse && temporalTransmittanceNotWorse &&
        temporalMeaningfullyImproved;

    allPassed = allPassed && highFullRtPassed &&
        highEdgeImprovementPassed && temporal16ImprovementPassed &&
        historyContractMeasured && historyAgeProgressedPassed &&
        noUnexpectedResetPassed && resolvedReadbackPassed &&
        !renderer.HasDebugLayerErrors();

    const std::filesystem::path directory =
        std::filesystem::path(VCLOUD_SHADER_SOURCE_DIR).parent_path() /
        "captures" / "stage15";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error)
        return 7;
    std::ofstream csv(directory / "quality.csv",
                      std::ios::binary | std::ios::trunc);
    csv << "adapter,driver,concept,camera,candidate,frames,output_width,output_height,cloud_rt_width,cloud_rt_height,cloud_rt_full,history_age,reset_count_last_60_frames,ssim,normalized_rmse,transmittance_mae,transmittance_p99,cloud_mask_pixels,cloud_mask_normalized_rmse,cloud_mask_transmittance_mae,cloud_edge_pixels,cloud_edge_normalized_rmse,cloud_edge_transmittance_mae,passed\n";
    for (const Result& result : results)
        csv << '"' << renderer.AdapterName() << "\",\""
            << renderer.DriverVersion() << "\",\"" << result.concept
            << "\",\"" << result.camera << "\",\"" << result.candidate
            << "\"," << result.accumulatedFrames << ','
            << result.outputWidth << ',' << result.outputHeight << ','
            << result.cloudRtWidth << ',' << result.cloudRtHeight << ','
            << (result.cloudRtFull ? 1 : 0) << ',' << result.historyAge
            << ',' << result.resetCountLast60Frames << ','
            << result.metric.ssim
            << ',' << result.metric.normalizedRmse << ','
            << result.metric.transmittanceMae << ','
            << result.metric.transmittanceP99 << ','
            << result.metric.cloudMaskPixelCount << ','
            << result.metric.cloudMaskNormalizedRmse << ','
            << result.metric.cloudMaskTransmittanceMae << ','
            << result.metric.cloudEdgePixelCount << ','
            << result.metric.cloudEdgeNormalizedRmse << ','
            << result.metric.cloudEdgeTransmittanceMae << ','
            << (result.passed ? 1 : 0) << '\n';
    std::ofstream json(directory / "quality.json",
                       std::ios::binary | std::ios::trunc);
    json << "{\n  \"adapter\": \"" << renderer.AdapterName()
         << "\",\n  \"driver\": \"" << renderer.DriverVersion()
         << "\",\n  \"resolution\": [1920, 1080],\n"
         << "  \"reference\": \"Full/FineReference/DirectReference/TemporalOff\",\n"
         << "  \"notWorseMetrics\": [\"normalizedRmse\", \"transmittanceMae\"],\n"
         << "  \"notWorseNumericTolerance\": 0.00011,\n"
         << "  \"cloudMaskDefinition\": \"reference opacity >= 0.01\",\n"
         << "  \"cloudEdgeMaskDefinition\": \"reference central-difference transmittance gradient >= 0.002 with 4-neighbor max opacity >= 0.01\",\n"
         << "  \"highCloudRtFullPassed\": "
         << (highFullRtPassed ? "true" : "false") << ",\n"
         << "  \"highNotWorsePassed\": "
         << (highNotWorsePassed ? "true" : "false") << ",\n"
         << "  \"highEdgeMaximumRatio\": "
         << kHighEdgeMaximumRatio << ",\n"
         << "  \"mediumEdgePixelCount\": "
         << mediumEdgeAggregate.pixelCount << ",\n"
         << "  \"mediumEdgeNormalizedRmse\": " << mediumEdgeRmse
         << ",\n  \"highEdgeNormalizedRmse\": " << highEdgeRmse
         << ",\n  \"mediumEdgeTransmittanceMae\": "
         << mediumEdgeTransmittanceMae
         << ",\n  \"highEdgeTransmittanceMae\": "
         << highEdgeTransmittanceMae
         << ",\n  \"highEdgeImprovementPassed\": "
         << (highEdgeImprovementPassed ? "true" : "false") << ",\n"
         << "  \"temporalAggregateNumericTolerance\": "
         << kTemporalAggregateTolerance << ",\n"
         << "  \"temporalConvergenceGateMetrics\": [\"cloudEdgeNormalizedRmse\", \"cloudEdgeTransmittanceMae\"],\n"
         << "  \"temporal4NormalizedRmse\": " << temporal4Rmse
         << ",\n  \"temporal16NormalizedRmse\": " << temporal16Rmse
         << ",\n  \"temporal4TransmittanceMae\": "
         << temporal4TransmittanceMae
         << ",\n  \"temporal16TransmittanceMae\": "
         << temporal16TransmittanceMae
         << ",\n  \"temporal4EdgePixelCount\": "
         << temporal4EdgeAggregate.pixelCount
         << ",\n  \"temporal4EdgeNormalizedRmse\": "
         << temporal4EdgeRmse
         << ",\n  \"temporal16EdgeNormalizedRmse\": "
         << temporal16EdgeRmse
         << ",\n  \"temporal4EdgeTransmittanceMae\": "
         << temporal4EdgeTransmittanceMae
         << ",\n  \"temporal16EdgeTransmittanceMae\": "
         << temporal16EdgeTransmittanceMae
         << ",\n  \"temporal16ImprovementPassed\": "
         << (temporal16ImprovementPassed ? "true" : "false") << ",\n"
         << "  \"historyAgeAt4\": " << historyAgeAt4
         << ",\n  \"historyAgeAt16\": " << historyAgeAt16
         << ",\n  \"resetCountAt4\": " << resetCountAt4
         << ",\n  \"resetCountAt16\": " << resetCountAt16
         << ",\n  \"historyAgeProgressedPassed\": "
         << (historyAgeProgressedPassed ? "true" : "false") << ",\n"
         << "  \"noUnexpectedResetPassed\": "
         << (noUnexpectedResetPassed ? "true" : "false") << ",\n"
         << "  \"resolvedScatteringAndTransmittanceReadbackPassed\": "
         << (resolvedReadbackPassed ? "true" : "false") << ",\n"
         << "  \"resultCount\": " << results.size()
         << ",\n  \"debugLayerPassed\": "
         << (!renderer.HasDebugLayerErrors() ? "true" : "false")
         << ",\n  \"passed\": " << (allPassed ? "true" : "false")
         << "\n}\n";
    if (!csv.good() || !json.good())
        return 8;
    WriteDiagnosticLine(
        "[STAGE15][HIGH-EDGE] Medium RGB=" +
        std::to_string(mediumEdgeRmse) + " T=" +
        std::to_string(mediumEdgeTransmittanceMae) + " High RGB=" +
        std::to_string(highEdgeRmse) + " T=" +
        std::to_string(highEdgeTransmittanceMae) +
        (highEdgeImprovementPassed ? " PASS" : " FAIL"));
    WriteDiagnosticLine(
        "[STAGE15][TEMPORAL-EDGE-4-16] RGB=" +
        std::to_string(temporal4EdgeRmse) + " -> " +
        std::to_string(temporal16EdgeRmse) + " T=" +
        std::to_string(temporal4EdgeTransmittanceMae) + " -> " +
        std::to_string(temporal16EdgeTransmittanceMae) +
        (temporal16ImprovementPassed ? " PASS" : " FAIL"));
    WriteDiagnosticLine(std::string("STAGE15_QUALITY RESULTS=") +
        std::to_string(results.size()) + (allPassed ? " PASS" : " FAIL"));
    return allPassed ? 0 : 9;
}

struct Stage15TimingResult
{
    std::string concept;
    std::string quality;
    std::string camera;
    double frameAverage = 0.0;
    double frameP50 = 0.0;
    double frameP95 = 0.0;
    double frameP99 = 0.0;
    double cloudAverage = 0.0;
    double cloudP50 = 0.0;
    double cloudP95 = 0.0;
    double cloudP99 = 0.0;
    double cpuAverage = 0.0;
    double cpuP50 = 0.0;
    double cpuP95 = 0.0;
    double cpuP99 = 0.0;
    double atmosphereAverage = 0.0;
    double atmosphereP50 = 0.0;
    double atmosphereP95 = 0.0;
    double atmosphereP99 = 0.0;
    double shadowAverage = 0.0;
    double shadowP50 = 0.0;
    double shadowP95 = 0.0;
    double shadowP99 = 0.0;
    double opaqueAverage = 0.0;
    double opaqueP50 = 0.0;
    double opaqueP95 = 0.0;
    double opaqueP99 = 0.0;
    double raymarchAverage = 0.0;
    double raymarchP50 = 0.0;
    double raymarchP95 = 0.0;
    double raymarchP99 = 0.0;
    double resolveAverage = 0.0;
    double resolveP50 = 0.0;
    double resolveP95 = 0.0;
    double resolveP99 = 0.0;
    double toneAverage = 0.0;
    double toneP50 = 0.0;
    double toneP95 = 0.0;
    double toneP99 = 0.0;
    std::uint64_t stateFingerprint = 0;
    std::uint64_t raymarchShaderHash = 0;
    std::uint64_t deepShadowShaderHash = 0;
    bool componentInvariantPassed = true;
    bool absolutePassed = false;
};

bool CollectStage15Timing(Renderer& renderer, Camera& camera,
                          int warmupFrames, std::size_t sampleCount,
                          Stage15TimingResult& result)
{
    for (int frame = 0; frame < warmupFrames; ++frame)
        renderer.Render(camera, 0.0f);
    enum SampleIndex : std::size_t
    {
        Cpu = 0,
        Frame,
        Cloud,
        Atmosphere,
        Shadow,
        Opaque,
        Raymarch,
        Resolve,
        Tone,
        Count,
    };
    std::array<std::vector<double>, Count> samples;
    for (std::vector<double>& values : samples)
        values.reserve(sampleCount);
    std::uint64_t lastIndex = renderer.TimingSnapshot().gpuSampleIndex;
    const int maximumAttempts = static_cast<int>(sampleCount * 4u);
    for (int attempt = 0;
         attempt < maximumAttempts && samples[0].size() < sampleCount;
         ++attempt)
    {
        renderer.Render(camera, 0.0f);
        const FrameTimingSnapshot timing = renderer.TimingSnapshot();
        if (!timing.gpuValid || timing.gpuSampleIndex == lastIndex)
        {
            Sleep(1);
            continue;
        }
        lastIndex = timing.gpuSampleIndex;
        const double values[] = {
            timing.rawCpuFrameMs,
            timing.rawGpuFrameMs,
            timing.rawGpuCloudMs,
            timing.rawGpuAtmosphereLutMs,
            timing.rawGpuShadowCacheMs,
            timing.rawGpuOpaqueSceneMs,
            timing.rawGpuCloudRaymarchMs,
            timing.rawGpuUpsampleCompositeMs,
            timing.rawGpuToneMapMs,
        };
        if (!std::all_of(std::begin(values), std::end(values),
                [](double value)
                {
                    return std::isfinite(value) && value >= 0.0;
                }))
            return false;
        for (std::size_t index = 0; index < samples.size(); ++index)
            samples[index].push_back(values[index]);
        const double componentCloud = timing.rawGpuShadowCacheMs +
            timing.rawGpuCloudRaymarchMs +
            timing.rawGpuUpsampleCompositeMs;
        result.componentInvariantPassed =
            result.componentInvariantPassed &&
            std::abs(componentCloud - timing.rawGpuCloudMs) <= 1.0e-6;
    }
    if (samples[0].size() != sampleCount)
        return false;
    const auto average = [](const std::vector<double>& values)
    {
        return std::accumulate(values.begin(), values.end(), 0.0) /
            static_cast<double>(values.size());
    };
    const auto assignStatistics = [&](const std::vector<double>& values,
                                      double& resultAverage,
                                      double& resultP50,
                                      double& resultP95,
                                      double& resultP99)
    {
        resultAverage = average(values);
        resultP50 = FramePercentile(values, 0.50);
        resultP95 = FramePercentile(values, 0.95);
        resultP99 = FramePercentile(values, 0.99);
    };
    assignStatistics(samples[Cpu], result.cpuAverage, result.cpuP50,
                     result.cpuP95, result.cpuP99);
    assignStatistics(samples[Frame], result.frameAverage, result.frameP50,
                     result.frameP95, result.frameP99);
    assignStatistics(samples[Cloud], result.cloudAverage, result.cloudP50,
                     result.cloudP95, result.cloudP99);
    assignStatistics(samples[Atmosphere], result.atmosphereAverage,
                     result.atmosphereP50, result.atmosphereP95,
                     result.atmosphereP99);
    assignStatistics(samples[Shadow], result.shadowAverage, result.shadowP50,
                     result.shadowP95, result.shadowP99);
    assignStatistics(samples[Opaque], result.opaqueAverage, result.opaqueP50,
                     result.opaqueP95, result.opaqueP99);
    assignStatistics(samples[Raymarch], result.raymarchAverage,
                     result.raymarchP50, result.raymarchP95,
                     result.raymarchP99);
    assignStatistics(samples[Resolve], result.resolveAverage,
                     result.resolveP50, result.resolveP95,
                     result.resolveP99);
    assignStatistics(samples[Tone], result.toneAverage, result.toneP50,
                     result.toneP95, result.toneP99);
    result.stateFingerprint = renderer.Stage15StateFingerprint();
    result.raymarchShaderHash = renderer.NonCirrusRaymarchShaderHash();
    result.deepShadowShaderHash = renderer.NonCirrusDeepShadowShaderHash();
    result.absolutePassed = result.frameP95 <= 16.67 &&
        result.cloudP95 <= 10.0 && result.resolveP95 <= 2.0 &&
        result.componentInvariantPassed;
    return true;
}

double LoadStage14DenseHorizonBaseline(const std::filesystem::path& root)
{
    std::string stage14Json;
    if (!ReadTextFile(root / "captures" / "stage14" / "performance.json",
                      stage14Json))
        return 0.0;
    const std::string marker =
        "\"path\": \"Stage14Physical\", \"name\": \"DenseHorizon\"";
    const std::size_t markerPosition = stage14Json.find(marker);
    const std::size_t valuePosition = markerPosition == std::string::npos
        ? std::string::npos
        : stage14Json.find("\"cloudP95Ms\":", markerPosition);
    if (valuePosition == std::string::npos)
        return 0.0;
    try
    {
        return std::stod(stage14Json.substr(
            valuePosition + std::strlen("\"cloudP95Ms\":")));
    }
    catch (...)
    {
        return 0.0;
    }
}

bool ConfigureStage14DenseHorizonFixture(Renderer& renderer, Camera& camera)
{
    renderer.SetNoiseLabVisible(false);
    renderer.EnableNoiseLabPreviews(false);
    renderer.SetAutomatedRenderMode(true);
    renderer.SetVSyncEnabled(false);
    renderer.SetDebugMode(CloudDebugMode::Composite);
    renderer.SetOpaqueSceneForTest(false);
    camera.SetClipPlanes(
        stage13camera::kNearPlaneMeters, stage13camera::kFarPlaneMeters);
    if (!renderer.ApplyStage13OpenWorldPreset() ||
        !renderer.ApplyCloudAppearancePreset(
            CloudAppearancePreset::DenseMixedDefault))
        return false;
    renderer.ApplyStage9OptimizationPreset(Stage9OptimizationPreset::Balanced);
    renderer.ApplyStage10ResolutionPreset(Stage10ResolutionPreset::Full);
    renderer.SetStage10UpsampleFilter(Stage10UpsampleFilter::Nearest);
    renderer.SetStage11TemporalMode(Stage11TemporalMode::Off);
    if (!renderer.SetStage12ShadowPreset(Stage12ShadowPreset::Balanced512))
        return false;
    renderer.SetStage12ShadowMode(Stage12ShadowMode::DeepCache);
    renderer.MutableAtmosphereSettings().mode = AtmosphereMode::Physical;
    stage14atmosphere::ApplyPreset(
        renderer.MutableAtmosphereSettings(), AtmospherePreset::EarthClear);
    renderer.MutableAtmosphereSettings().sunAzimuthDegrees = -60.0f;
    renderer.MutableAtmosphereSettings().sunElevationDegrees = 18.0f;
    renderer.MutableAtmosphereSettings().timePlaybackEnabled = false;
    stage14ground::ApplyPreset(
        renderer.MutableGroundLightingSettings(),
        GroundMaterialPreset::Concrete);
    renderer.MutableToneMappingSettings().mode = ToneMappingMode::AcesFitted;
    renderer.MutableToneMappingSettings().exposureEv = 0.0f;
    renderer.MutableToneMappingSettings().whiteBalanceKelvin = 6500.0f;
    const Stage13CameraPreset& horizon = stage13camera::Get(
        Stage13CameraPresetId::GroundHorizon);
    camera.SetLookAt(horizon.position, horizon.target);
    return true;
}

int RunStage15Stage14RegressionProbe(Renderer& renderer, Camera& camera)
{
    constexpr int kWarmupFrames = 120;
    constexpr std::size_t kSamples = 600u;
    constexpr std::size_t kBlocks = 3u;
    constexpr double kMaximumRatio = 1.03;
    const std::filesystem::path repositoryRoot =
        std::filesystem::path(VCLOUD_SHADER_SOURCE_DIR).parent_path();
    const double baseline = LoadStage14DenseHorizonBaseline(repositoryRoot);
    if (baseline <= 0.0 ||
        !ConfigureStage14DenseHorizonFixture(renderer, camera))
        return 2;

    const Stage13CameraPreset& horizon = stage13camera::Get(
        Stage13CameraPresetId::GroundHorizon);
    std::array<Stage15TimingResult, kBlocks> blocks;
    std::vector<double> cloudP95Values;
    cloudP95Values.reserve(kBlocks);
    bool invariantPassed = true;
    std::uint64_t expectedFingerprint = 0;
    for (std::size_t block = 0; block < kBlocks; ++block)
    {
        Stage15TimingResult& result = blocks[block];
        result.concept = "Stage14DenseMixed";
        result.quality = "ApprovedState";
        result.camera = horizon.diagnosticName;
        if (!CollectStage15Timing(renderer, camera, kWarmupFrames,
                                  kSamples, result))
            return 3;
        if (block == 0)
            expectedFingerprint = result.stateFingerprint;
        invariantPassed = invariantPassed && result.componentInvariantPassed &&
            result.stateFingerprint == expectedFingerprint;
        cloudP95Values.push_back(result.cloudP95);
        WriteDiagnosticLine(std::string("[STAGE15][REGRESSION_PROBE][BLOCK=") +
            std::to_string(block) + "] SHADOW_P95=" +
            std::to_string(result.shadowP95) + " RAYMARCH_P95=" +
            std::to_string(result.raymarchP95) + " RESOLVE_P95=" +
            std::to_string(result.resolveP95) + " CLOUD_P95=" +
            std::to_string(result.cloudP95));
    }
    const double medianCloudP95 = FramePercentile(cloudP95Values, 0.50);
    const double ratio = medianCloudP95 / baseline;
    const bool passed = invariantPassed && ratio <= kMaximumRatio &&
        !renderer.HasDebugLayerErrors();

    const std::filesystem::path directory =
        repositoryRoot / "captures" / "stage15";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error)
        return 4;
    std::ofstream csv(directory / "regression_probe.csv",
                      std::ios::binary | std::ios::trunc);
    csv << "block,samples,warmup";
    for (const char* name : { "cpu", "frame", "atmosphere", "shadow",
                              "opaque", "raymarch", "resolve", "tone",
                              "cloud" })
    {
        csv << ',' << name << "_average_ms," << name << "_p50_ms,"
            << name << "_p95_ms," << name << "_p99_ms";
    }
    csv << ",state_fingerprint,raymarch_shader_hash,deep_shadow_shader_hash,component_invariant\n";
    for (std::size_t block = 0; block < kBlocks; ++block)
    {
        const Stage15TimingResult& result = blocks[block];
        const auto writeStatistics = [&](double average, double p50,
                                         double p95, double p99)
        {
            csv << ',' << average << ',' << p50 << ',' << p95 << ',' << p99;
        };
        csv << block << ',' << kSamples << ',' << kWarmupFrames;
        writeStatistics(result.cpuAverage, result.cpuP50,
                        result.cpuP95, result.cpuP99);
        writeStatistics(result.frameAverage, result.frameP50,
                        result.frameP95, result.frameP99);
        writeStatistics(result.atmosphereAverage, result.atmosphereP50,
                        result.atmosphereP95, result.atmosphereP99);
        writeStatistics(result.shadowAverage, result.shadowP50,
                        result.shadowP95, result.shadowP99);
        writeStatistics(result.opaqueAverage, result.opaqueP50,
                        result.opaqueP95, result.opaqueP99);
        writeStatistics(result.raymarchAverage, result.raymarchP50,
                        result.raymarchP95, result.raymarchP99);
        writeStatistics(result.resolveAverage, result.resolveP50,
                        result.resolveP95, result.resolveP99);
        writeStatistics(result.toneAverage, result.toneP50,
                        result.toneP95, result.toneP99);
        writeStatistics(result.cloudAverage, result.cloudP50,
                        result.cloudP95, result.cloudP99);
        csv << ',' << result.stateFingerprint << ','
            << result.raymarchShaderHash << ',' << result.deepShadowShaderHash
            << ',' << (result.componentInvariantPassed ? 1 : 0) << '\n';
    }
    std::ofstream json(directory / "regression_probe.json",
                       std::ios::binary | std::ios::trunc);
    json << "{\n  \"adapter\": \"" << renderer.AdapterName()
         << "\",\n  \"driver\": \"" << renderer.DriverVersion()
         << "\",\n  \"resolution\": [1920, 1080],\n"
         << "  \"samplesPerBlock\": " << kSamples
         << ",\n  \"warmupPerBlock\": " << kWarmupFrames
         << ",\n  \"blocks\": [\n";
    for (std::size_t block = 0; block < kBlocks; ++block)
    {
        const Stage15TimingResult& result = blocks[block];
        const auto statisticsJson = [](double average, double p50,
                                       double p95, double p99)
        {
            std::ostringstream value;
            value << "{\"averageMs\": " << average
                  << ", \"p50Ms\": " << p50
                  << ", \"p95Ms\": " << p95
                  << ", \"p99Ms\": " << p99 << '}';
            return value.str();
        };
        json << "    {\"index\": " << block
             << ", \"shadowP95Ms\": " << result.shadowP95
             << ", \"raymarchP95Ms\": " << result.raymarchP95
             << ", \"resolveP95Ms\": " << result.resolveP95
             << ", \"cloudP95Ms\": " << result.cloudP95
             << ", \"timings\": {\"cpu\": "
             << statisticsJson(result.cpuAverage, result.cpuP50,
                               result.cpuP95, result.cpuP99)
             << ", \"frame\": "
             << statisticsJson(result.frameAverage, result.frameP50,
                               result.frameP95, result.frameP99)
             << ", \"atmosphere\": "
             << statisticsJson(result.atmosphereAverage, result.atmosphereP50,
                               result.atmosphereP95, result.atmosphereP99)
             << ", \"shadow\": "
             << statisticsJson(result.shadowAverage, result.shadowP50,
                               result.shadowP95, result.shadowP99)
             << ", \"opaque\": "
             << statisticsJson(result.opaqueAverage, result.opaqueP50,
                               result.opaqueP95, result.opaqueP99)
             << ", \"raymarch\": "
             << statisticsJson(result.raymarchAverage, result.raymarchP50,
                               result.raymarchP95, result.raymarchP99)
             << ", \"resolve\": "
             << statisticsJson(result.resolveAverage, result.resolveP50,
                               result.resolveP95, result.resolveP99)
             << ", \"tone\": "
             << statisticsJson(result.toneAverage, result.toneP50,
                               result.toneP95, result.toneP99)
             << ", \"cloud\": "
             << statisticsJson(result.cloudAverage, result.cloudP50,
                               result.cloudP95, result.cloudP99) << '}'
             << ", \"stateFingerprint\": " << result.stateFingerprint
             << ", \"componentInvariantPassed\": "
             << (result.componentInvariantPassed ? "true" : "false")
             << "}" << (block + 1u == kBlocks ? "\n" : ",\n");
    }
    json << "  ],\n  \"baselineCloudP95Ms\": " << baseline
         << ",\n  \"maximumCloudP95Ms\": " << baseline * kMaximumRatio
         << ",\n  \"medianCloudP95Ms\": " << medianCloudP95
         << ",\n  \"ratio\": " << ratio
         << ",\n  \"stateAndComponentInvariantPassed\": "
         << (invariantPassed ? "true" : "false")
         << ",\n  \"debugLayerPassed\": "
         << (!renderer.HasDebugLayerErrors() ? "true" : "false")
         << ",\n  \"passed\": " << (passed ? "true" : "false")
         << "\n}\n";
    if (!csv.good() || !json.good())
        return 5;
    WriteDiagnosticLine(std::string("[STAGE15][REGRESSION_PROBE]") +
        " BASELINE=" + std::to_string(baseline) + " MEDIAN=" +
        std::to_string(medianCloudP95) + " RATIO=" +
        std::to_string(ratio) + (passed ? " PASS" : " FAIL"));
    return passed ? 0 : 6;
}

int RunShaderCacheSmokeTest(Renderer& renderer)
{
    const bool passed = renderer.ValidateWarmShaderCache() &&
        renderer.ShaderCompileCallCount() == 0u &&
        renderer.ShaderCacheHitCount() > 0u;
    WriteDiagnosticLine(std::string("SHADER_CACHE COMPILE_CALLS=") +
        std::to_string(renderer.ShaderCompileCallCount()) + " CACHE_HITS=" +
        std::to_string(renderer.ShaderCacheHitCount()) +
        (passed ? " PASS" : " FAIL"));
    return passed ? 0 : 1;
}

int RunStage15PerformanceTest(Renderer& renderer, Camera& camera)
{
    constexpr int kWarmupFrames = 120;
    constexpr std::size_t kSamples = 600u;
    constexpr Stage15ConceptPreset concepts[] = {
        Stage15ConceptPreset::UrbanFairWeather,
        Stage15ConceptPreset::MeadowBrokenClouds,
        Stage15ConceptPreset::DesertCirrus,
        Stage15ConceptPreset::SnowOvercast,
    };
    constexpr Stage15QualityPreset qualities[] = {
        Stage15QualityPreset::Low,
        Stage15QualityPreset::Medium,
        Stage15QualityPreset::High,
    };

    renderer.SetNoiseLabVisible(false);
    renderer.EnableNoiseLabPreviews(false);
    renderer.SetAutomatedRenderMode(true);
    renderer.SetVSyncEnabled(false);
    renderer.SetOpaqueSceneForTest(true);
    camera.SetClipPlanes(
        stage13camera::kNearPlaneMeters, stage13camera::kFarPlaneMeters);

    // 열이 누적되는 48-case 측정 뒤에 Stage 14 기준을 재면 UI/분기 비용과
    // 무관한 온도·클럭 변동이 회귀율에 섞인다. 승인 상태를 먼저 재현해 같은
    // 냉간 시작 조건의 숨김 기준을 확보한 뒤 Stage 15 matrix를 측정한다.
    const std::filesystem::path repositoryRoot =
        std::filesystem::path(VCLOUD_SHADER_SOURCE_DIR).parent_path();
    double stage14BaselineCloudP95 = 0.0;
    std::string stage14Json;
    if (ReadTextFile(repositoryRoot / "captures" / "stage14" /
            "performance.json", stage14Json))
    {
        const std::string marker =
            "\"path\": \"Stage14Physical\", \"name\": \"DenseHorizon\"";
        const std::size_t markerPosition = stage14Json.find(marker);
        const std::size_t valuePosition = markerPosition == std::string::npos
            ? std::string::npos
            : stage14Json.find("\"cloudP95Ms\":", markerPosition);
        if (valuePosition != std::string::npos)
        {
            try
            {
                stage14BaselineCloudP95 = std::stod(stage14Json.substr(
                    valuePosition + std::strlen("\"cloudP95Ms\":")));
            }
            catch (...)
            {
                stage14BaselineCloudP95 = 0.0;
            }
        }
    }
    if (!renderer.ApplyStage13OpenWorldPreset() ||
        !renderer.ApplyCloudAppearancePreset(
            CloudAppearancePreset::DenseMixedDefault))
        return 4;
    renderer.SetDebugMode(CloudDebugMode::Composite);
    renderer.SetOpaqueSceneForTest(false);
    renderer.ApplyStage9OptimizationPreset(Stage9OptimizationPreset::Balanced);
    renderer.ApplyStage10ResolutionPreset(Stage10ResolutionPreset::Full);
    renderer.SetStage10UpsampleFilter(Stage10UpsampleFilter::Nearest);
    renderer.SetStage11TemporalMode(Stage11TemporalMode::Off);
    if (!renderer.SetStage12ShadowPreset(Stage12ShadowPreset::Balanced512))
        return 5;
    renderer.SetStage12ShadowMode(Stage12ShadowMode::DeepCache);
    renderer.MutableAtmosphereSettings().mode = AtmosphereMode::Physical;
    stage14atmosphere::ApplyPreset(
        renderer.MutableAtmosphereSettings(), AtmospherePreset::EarthClear);
    renderer.MutableAtmosphereSettings().sunAzimuthDegrees = -60.0f;
    renderer.MutableAtmosphereSettings().sunElevationDegrees = 18.0f;
    renderer.MutableAtmosphereSettings().timePlaybackEnabled = false;
    stage14ground::ApplyPreset(
        renderer.MutableGroundLightingSettings(),
        GroundMaterialPreset::Concrete);
    renderer.MutableToneMappingSettings().mode = ToneMappingMode::AcesFitted;
    renderer.MutableToneMappingSettings().exposureEv = 0.0f;
    renderer.MutableToneMappingSettings().whiteBalanceKelvin = 6500.0f;
    const Stage13CameraPreset& horizon = stage13camera::Get(
        Stage13CameraPresetId::GroundHorizon);
    camera.SetLookAt(horizon.position, horizon.target);
    Stage15TimingResult regression;
    regression.concept = "Stage14DenseMixed";
    regression.quality = "ApprovedState";
    regression.camera = horizon.diagnosticName;
    // 승인 JSON의 DenseHorizon은 앞선 Stage12/Stage14 장면들 뒤에 수집됐다.
    // 실행 직후 낮은 boost clock의 표본을 회귀로 오인하지 않도록 동일한
    // 120+600 수집 1회를 clock 안정화용으로 버리고 공식 표본을 다시 받는다.
    Stage15TimingResult regressionPrecondition = regression;
    if (!CollectStage15Timing(renderer, camera,
            kWarmupFrames, kSamples, regressionPrecondition))
        return 6;
    if (!CollectStage15Timing(renderer, camera,
            kWarmupFrames, kSamples, regression))
        return 6;
    const double stage14RegressionRatio = stage14BaselineCloudP95 > 0.0
        ? regression.cloudP95 / stage14BaselineCloudP95
        : std::numeric_limits<double>::infinity();
    const bool stage14RegressionPassed = stage14RegressionRatio <= 1.03;
    WriteDiagnosticLine(std::string("[STAGE15][PERF][STAGE14_REGRESSION]") +
        " BASELINE_CLOUD_P95=" + std::to_string(stage14BaselineCloudP95) +
        " CURRENT_CLOUD_P95=" + std::to_string(regression.cloudP95) +
        " SHADOW_P95=" + std::to_string(regression.shadowP95) +
        " RAYMARCH_P95=" + std::to_string(regression.raymarchP95) +
        " RESOLVE_P95=" + std::to_string(regression.resolveP95) +
        " RATIO=" + std::to_string(stage14RegressionRatio) +
        (stage14RegressionPassed ? " PASS" : " FAIL"));

    renderer.SetOpaqueSceneForTest(true);
    if (!renderer.ApplyStage15Defaults())
        return 2;

    std::vector<Stage15TimingResult> results;
    results.reserve(std::size(concepts) * std::size(qualities) *
                    stage13camera::kOpenWorldPresets.size());
    bool passed = true;
    for (Stage15ConceptPreset concept : concepts)
    {
        for (Stage15QualityPreset quality : qualities)
        {
            for (const Stage13CameraPreset& cameraPreset :
                 stage13camera::kOpenWorldPresets)
            {
                renderer.RequestStage15ConceptPreset(concept);
                renderer.RequestStage15QualityPreset(quality);
                camera.SetLookAt(cameraPreset.position, cameraPreset.target);
                Stage15TimingResult result;
                result.concept = stage15::ConceptName(concept);
                result.quality = stage15::QualityName(quality);
                result.camera = cameraPreset.diagnosticName;
                if (!CollectStage15Timing(renderer, camera,
                        kWarmupFrames, kSamples, result))
                    return 3;
                passed = passed && result.absolutePassed;
                WriteDiagnosticLine(std::string("[STAGE15][PERF][") +
                    result.concept + "][" + result.quality + "][" +
                    result.camera + "] FRAME_P95=" +
                    std::to_string(result.frameP95) + " CLOUD_P95=" +
                    std::to_string(result.cloudP95) + " SHADOW_P95=" +
                    std::to_string(result.shadowP95) + " RAYMARCH_P95=" +
                    std::to_string(result.raymarchP95) + " RESOLVE_P95=" +
                    std::to_string(result.resolveP95) +
                    (result.absolutePassed ? " PASS" : " FAIL"));
                results.push_back(result);
            }
        }
    }

    // 같은 50% Joint4/Stable 구조인 Low/Medium만 상대 성능을 비교한다.
    // High는 Full RT/1:1/Full-resolution Temporal이라 픽셀 수와 Resolve 구조가
    // 다르므로 과거 High/Medium 180% 상대 gate를 적용하지 않는다. High는 위의
    // Frame/Cloud/Resolve 절대 p95 예산으로만 판정한다.
    bool relativeQualityPassed = true;
    for (Stage15ConceptPreset concept : concepts)
    {
        for (const Stage13CameraPreset& cameraPreset :
             stage13camera::kOpenWorldPresets)
        {
            const auto findResult = [&](Stage15QualityPreset quality)
                -> const Stage15TimingResult*
            {
                for (const Stage15TimingResult& result : results)
                    if (result.concept == stage15::ConceptName(concept) &&
                        result.quality == stage15::QualityName(quality) &&
                        result.camera == cameraPreset.diagnosticName)
                        return &result;
                return nullptr;
            };
            const Stage15TimingResult* low = findResult(
                Stage15QualityPreset::Low);
            const Stage15TimingResult* medium = findResult(
                Stage15QualityPreset::Medium);
            const Stage15TimingResult* high = findResult(
                Stage15QualityPreset::High);
            const double lowOwnedAverage = low
                ? low->shadowAverage + low->raymarchAverage : 0.0;
            const double mediumOwnedAverage = medium
                ? medium->shadowAverage + medium->raymarchAverage : 0.0;
            const bool relativeCasePassed = low && medium && high &&
                lowOwnedAverage <= mediumOwnedAverage * 0.97;
            relativeQualityPassed = relativeQualityPassed &&
                relativeCasePassed;
        }
    }
    passed = passed && relativeQualityPassed;

    passed = passed && stage14RegressionPassed &&
        !renderer.HasDebugLayerErrors();

    const std::filesystem::path directory =
        repositoryRoot / "captures" / "stage15";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error)
        return 7;
    std::ofstream csv(directory / "performance.csv",
                      std::ios::binary | std::ios::trunc);
    csv << "adapter,driver,concept,quality,camera,samples,warmup,cpu_average_ms,cpu_p50_ms,cpu_p95_ms,cpu_p99_ms,frame_average_ms,frame_p50_ms,frame_p95_ms,frame_p99_ms,cloud_average_ms,cloud_p50_ms,cloud_p95_ms,cloud_p99_ms,quality_owned_average_ms,atmosphere_average_ms,atmosphere_p50_ms,atmosphere_p95_ms,atmosphere_p99_ms,shadow_average_ms,shadow_p50_ms,shadow_p95_ms,shadow_p99_ms,opaque_average_ms,opaque_p50_ms,opaque_p95_ms,opaque_p99_ms,raymarch_average_ms,raymarch_p50_ms,raymarch_p95_ms,raymarch_p99_ms,resolve_average_ms,resolve_p50_ms,resolve_p95_ms,resolve_p99_ms,tone_average_ms,tone_p50_ms,tone_p95_ms,tone_p99_ms,state_fingerprint,raymarch_shader_hash,deep_shadow_shader_hash,component_invariant,absolute_passed\n";
    for (const Stage15TimingResult& result : results)
        csv << '"' << renderer.AdapterName() << "\",\""
            << renderer.DriverVersion() << "\",\"" << result.concept
            << "\",\"" << result.quality << "\",\"" << result.camera
            << "\"," << kSamples << ',' << kWarmupFrames << ','
            << result.cpuAverage << ',' << result.cpuP50 << ','
            << result.cpuP95 << ',' << result.cpuP99 << ','
            << result.frameAverage << ',' << result.frameP50 << ','
            << result.frameP95 << ',' << result.frameP99 << ','
            << result.cloudAverage << ',' << result.cloudP50 << ','
            << result.cloudP95 << ',' << result.cloudP99 << ','
            << (result.shadowAverage + result.raymarchAverage) << ','
            << result.atmosphereAverage << ',' << result.atmosphereP50 << ','
            << result.atmosphereP95 << ',' << result.atmosphereP99 << ','
            << result.shadowAverage << ',' << result.shadowP50 << ','
            << result.shadowP95 << ',' << result.shadowP99 << ','
            << result.opaqueAverage << ',' << result.opaqueP50 << ','
            << result.opaqueP95 << ',' << result.opaqueP99 << ','
            << result.raymarchAverage << ',' << result.raymarchP50 << ','
            << result.raymarchP95 << ',' << result.raymarchP99 << ','
            << result.resolveAverage << ',' << result.resolveP50 << ','
            << result.resolveP95 << ',' << result.resolveP99 << ','
            << result.toneAverage << ',' << result.toneP50 << ','
            << result.toneP95 << ',' << result.toneP99 << ','
            << result.stateFingerprint << ',' << result.raymarchShaderHash
            << ',' << result.deepShadowShaderHash << ','
            << (result.componentInvariantPassed ? 1 : 0) << ','
            << (result.absolutePassed ? 1 : 0) << '\n';
    std::ofstream json(directory / "performance.json",
                       std::ios::binary | std::ios::trunc);
    json << "{\n  \"adapter\": \"" << renderer.AdapterName()
         << "\",\n  \"driver\": \"" << renderer.DriverVersion()
         << "\",\n  \"resolution\": [1920, 1080],\n"
         << "  \"cases\": " << results.size()
         << ",\n  \"samples\": " << kSamples
         << ",\n  \"warmup\": " << kWarmupFrames
         << ",\n  \"stage14Regression\": {\"baselineCloudP95Ms\": "
         << stage14BaselineCloudP95 << ", \"currentCloudP95Ms\": "
         << regression.cloudP95 << ", \"ratio\": "
         << stage14RegressionRatio << ", \"passed\": "
         << (stage14RegressionPassed ? "true" : "false")
         << ", \"shadowP95Ms\": " << regression.shadowP95
         << ", \"raymarchP95Ms\": " << regression.raymarchP95
         << ", \"resolveP95Ms\": " << regression.resolveP95
         << ", \"stateFingerprint\": " << regression.stateFingerprint
         << ", \"raymarchShaderHash\": " << regression.raymarchShaderHash
         << ", \"deepShadowShaderHash\": " << regression.deepShadowShaderHash
         << ", \"componentInvariantPassed\": "
         << (regression.componentInvariantPassed ? "true" : "false")
         << "},\n  \"relativeQualityMetric\": "
            "\"lowShadowPlusRaymarchAverage<=0.97*medium\",\n"
         << "  \"highQualityMetric\": "
            "\"absoluteFrameCloudResolveP95\",\n"
         << "  \"relativeQualityPassed\": "
         << (relativeQualityPassed ? "true" : "false")
         << ",\n  \"debugLayerPassed\": "
         << (!renderer.HasDebugLayerErrors() ? "true" : "false")
         << ",\n  \"passed\": " << (passed ? "true" : "false")
         << "\n}\n";
    if (!csv.good() || !json.good())
        return 8;
    return passed ? 0 : 9;
}

int RunStage14PerformanceTest(Renderer& renderer, Camera& camera)
{
    constexpr int kWarmupFrames = 120;
    constexpr std::size_t kSamples = 600u;
    constexpr int kMaximumAttempts = 2400;
    constexpr double kFrameP95BudgetMs = 16.67;
    constexpr double kCloudP95BudgetMs = 10.0;
    constexpr double kAtmosphereUpdateP95BudgetMs = 2.0;
    constexpr double kStage12RegressionRatio = 1.05;

    struct Result
    {
        std::string path;
        std::string scene;
        double frameAverage = 0.0;
        double frameP95 = 0.0;
        double cloudAverage = 0.0;
        double cloudP95 = 0.0;
        double atmosphereAverage = 0.0;
        double atmosphereP95 = 0.0;
        double shadowAverage = 0.0;
        double shadowP95 = 0.0;
        double opaqueAverage = 0.0;
        double opaqueP95 = 0.0;
        double raymarchAverage = 0.0;
        double raymarchP95 = 0.0;
        double resolveAverage = 0.0;
        double resolveP95 = 0.0;
        double toneAverage = 0.0;
        double toneP95 = 0.0;
        double stage12CloudP95 = 0.0;
        double stage12Ratio = std::numeric_limits<double>::infinity();
        bool passed = false;
    };
    struct UpdateResult
    {
        std::string name;
        double atmosphereAverage = 0.0;
        double atmosphereP95 = 0.0;
        bool passed = false;
    };
    const struct Scene
    {
        const char* name;
        CloudAppearancePreset appearance;
        Stage13CameraPresetId cameraPreset;
        bool zenith;
        bool opaque;
    } scenes[] = {
        { "DenseZenith", CloudAppearancePreset::DenseMixedDefault,
          Stage13CameraPresetId::HeroDepth, true, false },
        { "DenseHorizon", CloudAppearancePreset::DenseMixedDefault,
          Stage13CameraPresetId::GroundHorizon, false, false },
        { "StratusHorizon", CloudAppearancePreset::Stratus,
          Stage13CameraPresetId::GroundHorizon, false, false },
        { "CumulusHorizon", CloudAppearancePreset::Cumulus,
          Stage13CameraPresetId::GroundHorizon, false, false },
        { "CumulusInside", CloudAppearancePreset::Cumulus,
          Stage13CameraPresetId::InsideLayer, false, false },
        { "AboveLayer", CloudAppearancePreset::DenseMixedDefault,
          Stage13CameraPresetId::AboveLayer, false, false },
        { "DepthOccluded", CloudAppearancePreset::DenseMixedDefault,
          Stage13CameraPresetId::HeroDepth, false, true },
    };

    const auto percentile95 = [](std::vector<double> values)
    {
        if (values.empty())
            return std::numeric_limits<double>::infinity();
        std::sort(values.begin(), values.end());
        return values[static_cast<std::size_t>(
            std::ceil(values.size() * 0.95)) - 1u];
    };
    const auto average = [](const std::vector<double>& values)
    {
        return values.empty()
            ? std::numeric_limits<double>::infinity()
            : std::accumulate(values.begin(), values.end(), 0.0) /
                  static_cast<double>(values.size());
    };
    const std::filesystem::path repositoryRoot =
        std::filesystem::path(VCLOUD_SHADER_SOURCE_DIR).parent_path();

    renderer.SetNoiseLabVisible(false);
    renderer.EnableNoiseLabPreviews(false);
    renderer.SetAutomatedRenderMode(true);
    renderer.SetVSyncEnabled(false);
    renderer.SetDebugMode(CloudDebugMode::Composite);
    renderer.SetOpaqueSceneForTest(false);
    if (!renderer.ApplyStage13OpenWorldPreset())
        return 4;
    renderer.ApplyStage9OptimizationPreset(Stage9OptimizationPreset::Balanced);
    renderer.ApplyStage10ResolutionPreset(Stage10ResolutionPreset::Full);
    renderer.SetStage10UpsampleFilter(Stage10UpsampleFilter::Nearest);
    renderer.SetStage11TemporalMode(Stage11TemporalMode::Off);
    if (!renderer.SetStage12ShadowPreset(Stage12ShadowPreset::Balanced512))
        return 5;
    renderer.SetStage12ShadowMode(Stage12ShadowMode::DeepCache);
    camera.SetClipPlanes(
        stage13camera::kNearPlaneMeters, stage13camera::kFarPlaneMeters);

    AtmosphereParameters& atmosphere = renderer.MutableAtmosphereSettings();
    atmosphere.mode = AtmosphereMode::Physical;
    stage14atmosphere::ApplyPreset(atmosphere, AtmospherePreset::EarthClear);
    atmosphere.sunAzimuthDegrees = -60.0f;
    atmosphere.sunElevationDegrees = 18.0f;
    atmosphere.timePlaybackEnabled = false;
    GroundLightingParameters& ground = renderer.MutableGroundLightingSettings();
    stage14ground::ApplyPreset(ground, GroundMaterialPreset::Concrete);
    ToneMappingParameters& tone = renderer.MutableToneMappingSettings();
    tone.mode = ToneMappingMode::AcesFitted;
    tone.exposureEv = 0.0f;
    tone.whiteBalanceKelvin = 6500.0f;

    const auto collectSamples = [&](const auto& beforeRender,
                                    std::array<std::vector<double>, 8>& samples)
    {
        for (auto& values : samples)
            values.reserve(kSamples);
        std::uint64_t lastIndex = renderer.TimingSnapshot().gpuSampleIndex;
        for (int attempt = 0;
             attempt < kMaximumAttempts && samples[0].size() < kSamples;
             ++attempt)
        {
            beforeRender(attempt);
            renderer.Render(camera, 0.0f);
            const FrameTimingSnapshot timing = renderer.TimingSnapshot();
            if (!timing.gpuValid || timing.gpuSampleIndex == lastIndex)
            {
                Sleep(1);
                continue;
            }
            lastIndex = timing.gpuSampleIndex;
            const double values[] = {
                timing.rawGpuFrameMs,
                timing.rawGpuCloudMs,
                timing.rawGpuAtmosphereLutMs,
                timing.rawGpuShadowCacheMs,
                timing.rawGpuOpaqueSceneMs,
                timing.rawGpuCloudRaymarchMs,
                timing.rawGpuUpsampleCompositeMs,
                timing.rawGpuToneMapMs,
            };
            if (!std::all_of(std::begin(values), std::end(values),
                    [](double value)
                    {
                        return std::isfinite(value) && value >= 0.0;
                    }))
                return false;
            for (std::size_t index = 0; index < samples.size(); ++index)
                samples[index].push_back(values[index]);
        }
        return samples[0].size() == kSamples;
    };

    const struct MeasurementPath
    {
        const char* name;
        AtmosphereMode atmosphereMode;
        ToneMappingMode toneMode;
    } measurementPaths[] = {
        { "Stage12Equivalent", AtmosphereMode::ManualReference,
          ToneMappingMode::LegacyShoulder },
        { "Stage14Physical", AtmosphereMode::Physical,
          ToneMappingMode::AcesFitted },
    };
    std::vector<Result> results;
    for (const MeasurementPath& path : measurementPaths)
    {
        atmosphere.mode = path.atmosphereMode;
        tone.mode = path.toneMode;
        for (const Scene& scene : scenes)
        {
            if (!renderer.ApplyCloudAppearancePreset(scene.appearance))
                return 6;
            renderer.SetOpaqueSceneForTest(scene.opaque);
            const Stage13CameraPreset& cameraPreset =
                stage13camera::Get(scene.cameraPreset);
            if (scene.zenith)
                camera.SetLookAt(cameraPreset.position,
                    { cameraPreset.position.x, 7000.0f,
                      cameraPreset.position.z });
            else
                camera.SetLookAt(cameraPreset.position, cameraPreset.target);

            for (int frame = 0; frame < kWarmupFrames; ++frame)
                renderer.Render(camera, 0.0f);
            std::array<std::vector<double>, 8> samples;
            if (!collectSamples([](int) {}, samples))
                return 7;

            Result result;
            result.path = path.name;
            result.scene = scene.name;
            result.frameAverage = average(samples[0]);
            result.frameP95 = percentile95(samples[0]);
            result.cloudAverage = average(samples[1]);
            result.cloudP95 = percentile95(samples[1]);
            result.atmosphereAverage = average(samples[2]);
            result.atmosphereP95 = percentile95(samples[2]);
            result.shadowAverage = average(samples[3]);
            result.shadowP95 = percentile95(samples[3]);
            result.opaqueAverage = average(samples[4]);
            result.opaqueP95 = percentile95(samples[4]);
            result.raymarchAverage = average(samples[5]);
            result.raymarchP95 = percentile95(samples[5]);
            result.resolveAverage = average(samples[6]);
            result.resolveP95 = percentile95(samples[6]);
            result.toneAverage = average(samples[7]);
            result.toneP95 = percentile95(samples[7]);
            result.passed = result.frameP95 <= kFrameP95BudgetMs &&
                result.cloudP95 <= kCloudP95BudgetMs;
            results.push_back(result);
        }
    }

    std::map<std::string, double> stage12Baseline;
    for (const Result& result : results)
        if (result.path == "Stage12Equivalent")
            stage12Baseline[result.scene] = result.cloudP95;
    if (stage12Baseline.size() != std::size(scenes))
        return 8;
    for (Result& result : results)
    {
        result.stage12CloudP95 = stage12Baseline.at(result.scene);
        result.stage12Ratio = result.cloudP95 / result.stage12CloudP95;
        std::ostringstream line;
        line << std::fixed << std::setprecision(6)
             << "[STAGE14][PERF][" << result.path << "][" << result.scene
             << "] FRAME_P95=" << result.frameP95
             << " CLOUD_P95=" << result.cloudP95
             << " ATMOSPHERE_P95=" << result.atmosphereP95;
        if (result.path == "Stage14Physical")
            line << " STAGE12_CLOUD_P95=" << result.stage12CloudP95
                 << " RATIO=" << result.stage12Ratio;
        line << ' ' << (result.passed ? "PASS" : "FAIL");
        WriteDiagnosticLine(line.str());
    }

    renderer.SetOpaqueSceneForTest(false);
    if (!renderer.ApplyCloudAppearancePreset(
            CloudAppearancePreset::DenseMixedDefault))
        return 8;
    const Stage13CameraPreset& horizon = stage13camera::Get(
        Stage13CameraPresetId::GroundHorizon);
    camera.SetLookAt(horizon.position, horizon.target);
    atmosphere.mode = AtmosphereMode::Physical;
    tone.mode = ToneMappingMode::AcesFitted;

    const auto measureAtmosphereUpdate = [&](const char* name,
                                             const auto& update)
    {
        for (int frame = 0; frame < kWarmupFrames; ++frame)
        {
            update(frame);
            renderer.Render(camera, 0.0f);
        }
        std::array<std::vector<double>, 8> samples;
        const bool collected = collectSamples(
            [&](int attempt) { update(kWarmupFrames + attempt); }, samples);
        UpdateResult result;
        result.name = name;
        if (collected)
        {
            result.atmosphereAverage = average(samples[2]);
            result.atmosphereP95 = percentile95(samples[2]);
            result.passed = result.atmosphereP95 <=
                kAtmosphereUpdateP95BudgetMs;
        }
        return std::pair<UpdateResult, bool>{ result, collected };
    };

    const auto sunUpdate = measureAtmosphereUpdate(
        "SunPlayback",
        [&](int frame)
        {
            atmosphere.sunAzimuthDegrees = -60.0f +
                0.05f * static_cast<float>(frame % 600);
            atmosphere.sunElevationDegrees = 18.0f +
                0.01f * static_cast<float>(frame % 120);
        });
    if (!sunUpdate.second)
        return 9;
    atmosphere.sunAzimuthDegrees = -60.0f;
    atmosphere.sunElevationDegrees = 18.0f;
    camera.SetLookAt(horizon.position, horizon.target);
    const auto cameraUpdate = measureAtmosphereUpdate(
        "CameraMotion",
        [&](int frame)
        {
            const float offset = 250.0f * std::sin(
                static_cast<float>(frame) * 0.01745329252f);
            camera.SetLookAt(horizon.position,
                { horizon.target.x + offset,
                  horizon.target.y, horizon.target.z });
        });
    if (!cameraUpdate.second)
        return 10;
    const UpdateResult updateResults[] = {
        sunUpdate.first, cameraUpdate.first
    };
    for (const UpdateResult& result : updateResults)
    {
        std::ostringstream line;
        line << std::fixed << std::setprecision(6)
             << "[STAGE14][PERF][" << result.name
             << "] ATMOSPHERE_AVG=" << result.atmosphereAverage
             << " ATMOSPHERE_P95=" << result.atmosphereP95 << ' '
             << (result.passed ? "PASS" : "FAIL");
        WriteDiagnosticLine(line.str());
    }

    std::error_code error;
    const std::filesystem::path directory =
        repositoryRoot / "captures" / "stage14";
    std::filesystem::create_directories(directory, error);
    if (error)
        return 11;
    std::ofstream csv(directory / "performance.csv",
                      std::ios::binary | std::ios::trunc);
    csv << "adapter,driver,path,scene,samples,warmup,frame_average_ms,frame_p95_ms,"
           "cloud_average_ms,cloud_p95_ms,atmosphere_average_ms,"
           "atmosphere_p95_ms,shadow_average_ms,shadow_p95_ms,"
           "opaque_average_ms,opaque_p95_ms,raymarch_average_ms,"
           "raymarch_p95_ms,resolve_average_ms,resolve_p95_ms,"
           "tone_average_ms,tone_p95_ms,stage12_cloud_p95_ms,"
           "stage12_ratio,passed\n";
    for (const Result& result : results)
        csv << '"' << renderer.AdapterName() << "\",\""
            << renderer.DriverVersion() << "\",\"" << result.path
            << "\",\"" << result.scene
            << "\"," << kSamples << ',' << kWarmupFrames << ','
            << result.frameAverage << ',' << result.frameP95 << ','
            << result.cloudAverage << ',' << result.cloudP95 << ','
            << result.atmosphereAverage << ',' << result.atmosphereP95 << ','
            << result.shadowAverage << ',' << result.shadowP95 << ','
            << result.opaqueAverage << ',' << result.opaqueP95 << ','
            << result.raymarchAverage << ',' << result.raymarchP95 << ','
            << result.resolveAverage << ',' << result.resolveP95 << ','
            << result.toneAverage << ',' << result.toneP95 << ','
            << result.stage12CloudP95 << ',' << result.stage12Ratio << ','
            << (result.passed ? 1 : 0) << '\n';
    if (!csv.good())
        return 12;

    const bool sceneGatePassed = std::all_of(
        results.begin(), results.end(),
        [](const Result& result)
        {
            return result.path != "Stage14Physical" || result.passed;
        });
    const double currentMeanSceneP95 = std::accumulate(
        results.begin(), results.end(), 0.0,
        [](double sum, const Result& result)
        {
            return sum + (result.path == "Stage14Physical"
                ? result.cloudP95 : 0.0);
        }) / static_cast<double>(std::size(scenes));
    const double stage12MeanSceneP95 = std::accumulate(
        results.begin(), results.end(), 0.0,
        [](double sum, const Result& result)
        {
            return sum + (result.path == "Stage12Equivalent"
                ? result.cloudP95 : 0.0);
        }) / static_cast<double>(std::size(scenes));
    const double stage12MeanRatio = currentMeanSceneP95 / stage12MeanSceneP95;
    const bool regressionGatePassed =
        stage12MeanRatio <= kStage12RegressionRatio;
    const bool updateGatePassed = std::all_of(
        std::begin(updateResults), std::end(updateResults),
        [](const UpdateResult& result) { return result.passed; });
    const bool debugLayerPassed = !renderer.HasDebugLayerErrors();
    const bool passed = sceneGatePassed && regressionGatePassed &&
        updateGatePassed && debugLayerPassed;

    std::ofstream json(directory / "performance.json",
                       std::ios::binary | std::ios::trunc);
    json << "{\n  \"adapter\": \"" << renderer.AdapterName()
         << "\",\n  \"driver\": \"" << renderer.DriverVersion()
         << "\",\n  \"resolution\": [1920, 1080],\n"
         << "  \"samples\": " << kSamples
         << ",\n  \"warmup\": " << kWarmupFrames
         << ",\n  \"configuration\": \"Full/Balanced/TemporalOff/"
            "Balanced512; same-run ManualReference and PhysicalEarthClear\",\n"
         << "  \"gates\": {\"frameP95Ms\": " << kFrameP95BudgetMs
         << ", \"cloudP95Ms\": " << kCloudP95BudgetMs
         << ", \"atmosphereUpdateP95Ms\": "
         << kAtmosphereUpdateP95BudgetMs
         << ", \"stage12MaximumRatio\": " << kStage12RegressionRatio
         << "},\n  \"stage12Regression\": {\"currentMeanSceneP95Ms\": "
         << currentMeanSceneP95 << ", \"baselineMeanSceneP95Ms\": "
         << stage12MeanSceneP95 << ", \"ratio\": " << stage12MeanRatio
         << ", \"passed\": "
         << (regressionGatePassed ? "true" : "false")
         << "},\n  \"atmosphereUpdates\": [\n";
    for (std::size_t index = 0; index < std::size(updateResults); ++index)
    {
        const UpdateResult& result = updateResults[index];
        json << "    {\"name\": \"" << result.name
             << "\", \"averageMs\": " << result.atmosphereAverage
             << ", \"p95Ms\": " << result.atmosphereP95
             << ", \"passed\": " << (result.passed ? "true" : "false")
             << "}" << (index + 1u == std::size(updateResults) ? "\n" : ",\n");
    }
    json << "  ],\n  \"scenes\": [\n";
    for (std::size_t index = 0; index < results.size(); ++index)
    {
        const Result& result = results[index];
        json << "    {\"path\": \"" << result.path
             << "\", \"name\": \"" << result.scene
             << "\", \"frameP95Ms\": " << result.frameP95
             << ", \"cloudP95Ms\": " << result.cloudP95
             << ", \"atmosphereP95Ms\": " << result.atmosphereP95
             << ", \"stage12CloudP95Ms\": " << result.stage12CloudP95
             << ", \"stage12Ratio\": " << result.stage12Ratio
             << ", \"passed\": " << (result.passed ? "true" : "false")
             << "}" << (index + 1u == results.size() ? "\n" : ",\n");
    }
    json << "  ],\n  \"debugLayerPassed\": "
         << (debugLayerPassed ? "true" : "false")
         << ",\n  \"passed\": " << (passed ? "true" : "false")
         << "\n}\n";
    if (!json.good())
        return 13;

    {
        std::ostringstream line;
        line << std::fixed << std::setprecision(6)
             << "[STAGE14][GATE] MEAN_CLOUD_P95=" << currentMeanSceneP95
             << " STAGE12_MEAN_CLOUD_P95=" << stage12MeanSceneP95
             << " RATIO=" << stage12MeanRatio << ' '
             << (regressionGatePassed ? "PASS" : "FAIL");
        WriteDiagnosticLine(line.str());
    }
    WriteDiagnosticLine(std::string("STAGE14_PERFORMANCE ") +
        (passed ? "PASS" : "FAIL"));
    return passed ? 0 : 1;
}

int RunStage12ShadowPerformanceTest(Renderer& renderer, Camera& camera)
{
    constexpr int kWarmupFrames = 120;
    constexpr std::size_t kSamples = 600u;
    constexpr int kMaximumAttempts = 1800;
    struct Result
    {
        std::string candidate;
        std::string resolution;
        std::string scene;
        double frameAverage = 0.0;
        double frameP95 = 0.0;
        double cloudAverage = 0.0;
        double cloudP95 = 0.0;
        double cacheAverage = 0.0;
        double cacheP95 = 0.0;
        double raymarchAverage = 0.0;
        double raymarchP95 = 0.0;
        double resolveAverage = 0.0;
        double resolveP95 = 0.0;
        std::uint64_t cacheBytes = 0u;
    };
    const struct Scene
    {
        const char* name;
        CloudAppearancePreset appearance;
        Stage13CameraPresetId cameraPreset;
        bool zenith;
        bool opaque;
    } scenes[] = {
        { "DenseZenith", CloudAppearancePreset::DenseMixedDefault,
          Stage13CameraPresetId::HeroDepth, true, false },
        { "DenseHorizon", CloudAppearancePreset::DenseMixedDefault,
          Stage13CameraPresetId::GroundHorizon, false, false },
        { "StratusHorizon", CloudAppearancePreset::Stratus,
          Stage13CameraPresetId::GroundHorizon, false, false },
        { "CumulusHorizon", CloudAppearancePreset::Cumulus,
          Stage13CameraPresetId::GroundHorizon, false, false },
        { "CumulusInside", CloudAppearancePreset::Cumulus,
          Stage13CameraPresetId::InsideLayer, false, false },
        { "AboveLayer", CloudAppearancePreset::DenseMixedDefault,
          Stage13CameraPresetId::AboveLayer, false, false },
        { "DepthOccluded", CloudAppearancePreset::DenseMixedDefault,
          Stage13CameraPresetId::HeroDepth, false, true },
    };
    const struct Candidate
    {
        const char* name;
        Stage12ShadowMode mode;
        Stage12ShadowPreset preset;
    } candidates[] = {
        { "DirectReference", Stage12ShadowMode::DirectReference,
          Stage12ShadowPreset::Fast256 },
        { "Fast256", Stage12ShadowMode::DeepCache,
          Stage12ShadowPreset::Fast256 },
        { "Balanced512", Stage12ShadowMode::DeepCache,
          Stage12ShadowPreset::Balanced512 },
    };
    const Stage10ResolutionPreset resolutions[] = {
        Stage10ResolutionPreset::Full,
        Stage10ResolutionPreset::Half,
    };

    renderer.SetNoiseLabVisible(false);
    renderer.EnableNoiseLabPreviews(false);
    renderer.SetVSyncEnabled(false);
    renderer.SetDebugMode(CloudDebugMode::Composite);
    renderer.SetStage11TemporalMode(Stage11TemporalMode::Off);
    renderer.ApplyStage9OptimizationPreset(Stage9OptimizationPreset::Balanced);
    camera.SetClipPlanes(
        stage13camera::kNearPlaneMeters, stage13camera::kFarPlaneMeters);
    if (!renderer.ApplyStage13OpenWorldPreset())
        return 2;

    const auto percentile95 = [](std::vector<double> values)
    {
        if (values.empty())
            return std::numeric_limits<double>::infinity();
        std::sort(values.begin(), values.end());
        return values[static_cast<std::size_t>(
            std::ceil(values.size() * 0.95)) - 1u];
    };
    const auto average = [](const std::vector<double>& values)
    {
        return values.empty()
            ? std::numeric_limits<double>::infinity()
            : std::accumulate(values.begin(), values.end(), 0.0) /
                  static_cast<double>(values.size());
    };

    std::vector<Result> results;
    for (Stage10ResolutionPreset resolution : resolutions)
    {
        renderer.ApplyStage10ResolutionPreset(resolution);
        renderer.SetStage10UpsampleFilter(
            resolution == Stage10ResolutionPreset::Full
                ? Stage10UpsampleFilter::Nearest
                : Stage10UpsampleFilter::Joint4);
        for (const Candidate& candidate : candidates)
        {
            if (!renderer.SetStage12ShadowPreset(candidate.preset))
                return 3;
            renderer.SetStage12ShadowMode(candidate.mode);
            for (const Scene& scene : scenes)
            {
                if (!renderer.ApplyCloudAppearancePreset(scene.appearance))
                    return 4;
                renderer.SetOpaqueSceneForTest(scene.opaque);
                const Stage13CameraPreset& cameraPreset =
                    stage13camera::Get(scene.cameraPreset);
                if (scene.zenith)
                    camera.SetLookAt(cameraPreset.position,
                        { cameraPreset.position.x, 7000.0f,
                          cameraPreset.position.z });
                else
                    camera.SetLookAt(cameraPreset.position, cameraPreset.target);

                for (int frame = 0; frame < kWarmupFrames; ++frame)
                    renderer.Render(camera, 0.0f);

                std::vector<double> frameSamples;
                std::vector<double> cloudSamples;
                std::vector<double> cacheSamples;
                std::vector<double> raymarchSamples;
                std::vector<double> resolveSamples;
                for (std::vector<double>* samples : {
                         &frameSamples, &cloudSamples, &cacheSamples,
                         &raymarchSamples, &resolveSamples })
                    samples->reserve(kSamples);
                std::uint64_t lastIndex =
                    renderer.TimingSnapshot().gpuSampleIndex;
                for (int attempt = 0;
                     attempt < kMaximumAttempts &&
                         cloudSamples.size() < kSamples;
                     ++attempt)
                {
                    renderer.Render(camera, 0.0f);
                    const FrameTimingSnapshot timing = renderer.TimingSnapshot();
                    if (!timing.gpuValid || timing.gpuSampleIndex == lastIndex)
                    {
                        Sleep(1);
                        continue;
                    }
                    lastIndex = timing.gpuSampleIndex;
                    const double values[] = {
                        timing.rawGpuFrameMs,
                        timing.rawGpuCloudMs,
                        timing.rawGpuShadowCacheMs,
                        timing.rawGpuCloudRaymarchMs,
                        timing.rawGpuUpsampleCompositeMs,
                    };
                    bool valid = true;
                    for (double value : values)
                        valid = valid && std::isfinite(value) && value >= 0.0;
                    if (!valid)
                        return 5;
                    frameSamples.push_back(values[0]);
                    cloudSamples.push_back(values[1]);
                    cacheSamples.push_back(values[2]);
                    raymarchSamples.push_back(values[3]);
                    resolveSamples.push_back(values[4]);
                }
                if (cloudSamples.size() != kSamples)
                    return 6;

                Result result;
                result.candidate = candidate.name;
                result.resolution =
                    stage10upsampling::ResolutionPresetName(resolution);
                result.scene = scene.name;
                result.frameAverage = average(frameSamples);
                result.frameP95 = percentile95(frameSamples);
                result.cloudAverage = average(cloudSamples);
                result.cloudP95 = percentile95(cloudSamples);
                result.cacheAverage = average(cacheSamples);
                result.cacheP95 = percentile95(cacheSamples);
                result.raymarchAverage = average(raymarchSamples);
                result.raymarchP95 = percentile95(raymarchSamples);
                result.resolveAverage = average(resolveSamples);
                result.resolveP95 = percentile95(resolveSamples);
                result.cacheBytes = candidate.mode ==
                    Stage12ShadowMode::DeepCache
                    ? renderer.ShadowCacheBytes() : 0u;
                results.push_back(result);

                std::ostringstream line;
                line << std::fixed << std::setprecision(6)
                     << "[STAGE12][PERF][" << result.resolution << "]["
                     << result.candidate << "][" << result.scene << "] "
                     << "FRAME_P95=" << result.frameP95
                     << " CLOUD_P95=" << result.cloudP95
                     << " CACHE_P95=" << result.cacheP95
                     << " RAYMARCH_P95=" << result.raymarchP95
                     << " RESOLVE_P95=" << result.resolveP95;
                WriteDiagnosticLine(line.str());
            }
        }
    }

    const auto meanSceneP95 = [&](const std::string& candidate,
                                  const std::string& resolution)
    {
        double sum = 0.0;
        std::size_t count = 0u;
        for (const Result& result : results)
            if (result.candidate == candidate &&
                result.resolution == resolution)
            {
                sum += result.cloudP95;
                ++count;
            }
        return count == std::size(scenes)
            ? sum / static_cast<double>(count)
            : std::numeric_limits<double>::infinity();
    };
    const auto allScenesUnderBudget = [&](const std::string& candidate)
    {
        for (const Result& result : results)
            if (result.candidate == candidate && result.cloudP95 > 10.0)
                return false;
        return true;
    };
    const std::string fullName =
        stage10upsampling::ResolutionPresetName(Stage10ResolutionPreset::Full);
    const std::string halfName =
        stage10upsampling::ResolutionPresetName(Stage10ResolutionPreset::Half);
    const double directFull = meanSceneP95("DirectReference", fullName);
    const double directHalf = meanSceneP95("DirectReference", halfName);
    std::map<std::string, bool> qualifies;
    for (const char* candidate : { "Fast256", "Balanced512" })
    {
        const double full = meanSceneP95(candidate, fullName);
        const double half = meanSceneP95(candidate, halfName);
        qualifies[candidate] = allScenesUnderBudget(candidate) &&
            full <= directFull * 0.85 && half <= directHalf * 1.03;
        std::ostringstream line;
        line << std::fixed << std::setprecision(6)
             << "[STAGE12][GATE][" << candidate << "] FULL_MEAN_P95="
             << full << " FULL_DIRECT=" << directFull
             << " HALF_MEAN_P95=" << half
             << " HALF_DIRECT=" << directHalf << ' '
             << (qualifies[candidate] ? "PASS" : "FAIL");
        WriteDiagnosticLine(line.str());
    }
    const std::string approvedCandidate = qualifies["Balanced512"]
        ? "Balanced512" : (qualifies["Fast256"] ? "Fast256" : "None");

    std::error_code error;
    const std::filesystem::path directory =
        std::filesystem::path(VCLOUD_SHADER_SOURCE_DIR).parent_path() /
        "captures" / "stage12";
    std::filesystem::create_directories(directory, error);
    if (error)
        return 7;
    std::ofstream csv(directory / "performance.csv",
                      std::ios::binary | std::ios::trunc);
    csv << "adapter,driver,candidate,resolution,scene,samples,warmup,"
           "frame_average_ms,frame_p95_ms,cloud_average_ms,cloud_p95_ms,"
           "cache_average_ms,cache_p95_ms,raymarch_average_ms,raymarch_p95_ms,"
           "resolve_average_ms,resolve_p95_ms,cache_bytes\n";
    for (const Result& result : results)
        csv << '"' << renderer.AdapterName() << "\",\""
            << renderer.DriverVersion() << "\",\"" << result.candidate
            << "\",\"" << result.resolution << "\",\"" << result.scene
            << "\"," << kSamples << ',' << kWarmupFrames << ','
            << result.frameAverage << ',' << result.frameP95 << ','
            << result.cloudAverage << ',' << result.cloudP95 << ','
            << result.cacheAverage << ',' << result.cacheP95 << ','
            << result.raymarchAverage << ',' << result.raymarchP95 << ','
            << result.resolveAverage << ',' << result.resolveP95 << ','
            << result.cacheBytes << '\n';
    if (!csv.good())
        return 8;

    std::ofstream json(directory / "performance.json",
                       std::ios::binary | std::ios::trunc);
    json << "{\n  \"adapter\": \"" << renderer.AdapterName()
         << "\",\n  \"driver\": \"" << renderer.DriverVersion()
         << "\",\n  \"resolution\": [1920, 1080],\n"
         << "  \"samples\": " << kSamples
         << ",\n  \"warmup\": " << kWarmupFrames
         << ",\n  \"approvedCandidate\": \"" << approvedCandidate
         << "\",\n  \"candidates\": {\n"
         << "    \"Fast256\": {\"performanceGate\": "
         << (qualifies["Fast256"] ? "true" : "false") << "},\n"
         << "    \"Balanced512\": {\"performanceGate\": "
         << (qualifies["Balanced512"] ? "true" : "false") << "}\n"
         << "  }\n}\n";
    if (!json.good())
        return 9;

    renderer.ApplyStage10ResolutionPreset(Stage10ResolutionPreset::Full);
    if (!renderer.SetStage12ShadowPreset(Stage12ShadowPreset::Balanced512))
        return 10;
    renderer.SetStage12ShadowMode(Stage12ShadowMode::DeepCache);
    return approvedCandidate != "None" && !renderer.HasDebugLayerErrors()
        ? 0 : 1;
}

int RunStage13WeatherShapeGpuTest(Renderer& renderer, Camera& camera)
{
    renderer.SetNoiseLabVisible(false);
    renderer.EnableNoiseLabPreviews(false);
    renderer.SetVSyncEnabled(false);
    renderer.SetOpaqueSceneForTest(false);
    if (!renderer.ApplyStage13OpenWorldPreset())
        return 2;
    // 단계 13의 CPU↔HLSL 형상 계약은 단계 9 생략 경로가 아닌 승인 Reference로 검사한다.
    renderer.ApplyStage9OptimizationPreset(
        Stage9OptimizationPreset::ApprovedReference);
    camera.SetClipPlanes(0.1f, 60000.0f);
    camera.SetOrbit(0.0f, 0.0f, 32000.0f,
                    { 0.0f, 4500.0f, 0.0f });

    const CloudDebugMode modes[] = {
        CloudDebugMode::CloudHitMask,
        CloudDebugMode::RawNoise,
        CloudDebugMode::WeatherCoverage,
        CloudDebugMode::CloudType,
        CloudDebugMode::WeatherThicknessPotential,
        CloudDebugMode::LocalThickness,
        CloudDebugMode::LocalHeightFraction,
        CloudDebugMode::TypedShapeProfile,
        CloudDebugMode::EffectiveShapeCoverage,
        CloudDebugMode::BaseSupportBeforeDensity,
        CloudDebugMode::BaseDensity,
        CloudDebugMode::DetailNoise,
        CloudDebugMode::FinalDensity,
    };
    std::map<CloudDebugMode, CloudDiagnosticFrame> frames;
    std::set<std::uint64_t> hashes;
    for (CloudDebugMode mode : modes)
    {
        CloudDiagnosticFrame frame;
        if (!renderer.CaptureCloudDiagnosticFrame(camera, 0.0f, mode, frame))
            return 3;
        for (const DirectX::XMFLOAT4& pixel : frame.pixels)
            if (!std::isfinite(pixel.x) || !std::isfinite(pixel.y) ||
                !std::isfinite(pixel.z) || !std::isfinite(pixel.w))
                return 4;
        if (mode != CloudDebugMode::CloudHitMask &&
            mode != CloudDebugMode::WeatherCoverage &&
            mode != CloudDebugMode::CloudType)
            hashes.insert(HashDiagnosticFrame(frame));
        frames.emplace(mode, std::move(frame));
    }
    if (hashes.size() != 10u)
        return 5;

    const auto& hit = frames.at(CloudDebugMode::CloudHitMask).pixels;
    const auto& rawNoise = frames.at(CloudDebugMode::RawNoise).pixels;
    const auto& coverage = frames.at(CloudDebugMode::WeatherCoverage).pixels;
    const auto& type = frames.at(CloudDebugMode::CloudType).pixels;
    const auto& potential = frames.at(
        CloudDebugMode::WeatherThicknessPotential).pixels;
    const auto& thicknessFrame = frames.at(CloudDebugMode::LocalThickness).pixels;
    const auto& localHeight = frames.at(CloudDebugMode::LocalHeightFraction).pixels;
    const auto& typedProfile = frames.at(CloudDebugMode::TypedShapeProfile).pixels;
    const auto& effectiveCoverage = frames.at(
        CloudDebugMode::EffectiveShapeCoverage).pixels;
    const auto& baseSupport = frames.at(
        CloudDebugMode::BaseSupportBeforeDensity).pixels;
    std::vector<double> thicknesses;
    double maximumFormulaError = 0.0;
    double maximumProfileError = 0.0;
    double maximumCoverageError = 0.0;
    double maximumSupportError = 0.0;
    double maximumProfileExpected = 0.0;
    double maximumProfileActual = 0.0;
    double maximumProfileHeight = 0.0;
    double maximumProfileType = 0.0;
    double maximumCoverageExpected = 0.0;
    double maximumCoverageActual = 0.0;
    double maximumCoverageWeather = 0.0;
    double maximumCoverageFootprint = 0.0;
    double maximumCoverageProfile = 0.0;
    double maximumCoverageHeight = 0.0;
    double maximumCoverageType = 0.0;
    std::size_t ceilingCount = 0;
    struct SupportBand
    {
        std::size_t samples = 0;
        std::size_t supported = 0;
        double Ratio() const
        {
            return static_cast<double>(supported) /
                static_cast<double>(std::max<std::size_t>(samples, 1u));
        }
    };
    SupportBand mixedBottom, mixedMiddle, mixedTop;
    SupportBand cumulusBottom, cumulusMiddle, cumulusTop;
    const CloudShapeParameters activeShape = renderer.ShapeSettings();
    const auto evaluateActiveVerticalProfile = [&](double height,
                                                   double cloudType)
    {
        const double h = stage13shape::SaturateFinite(height);
        const double typeValue = stage13shape::SaturateFinite(cloudType);
        const double stratus = stage13shape::EvaluateEnvelope(
            h, activeShape.stratusBottomFadeEnd,
            activeShape.stratusTopFadeStart);
        const double mixed = stage13shape::EvaluateEnvelope(
            h, activeShape.mixedBottomFadeEnd,
            activeShape.mixedTopFadeStart);
        const double cumulusEnvelope = stage13shape::EvaluateEnvelope(
            h, activeShape.cumulusBottomFadeEnd,
            activeShape.cumulusTopFadeStart);
        const double upperMass = activeShape.cumulusUpperMassBottom +
            (1.0 - activeShape.cumulusUpperMassBottom) *
            stage13shape::Smoothstep(activeShape.cumulusUpperMassStart,
                                     activeShape.cumulusUpperMassEnd, h);
        const double cumulus = stage13shape::SaturateFinite(
            cumulusEnvelope * upperMass);
        return typeValue <= 0.5
            ? stratus + (mixed - stratus) * (typeValue * 2.0)
            : mixed + (cumulus - mixed) * ((typeValue - 0.5) * 2.0);
    };
    const auto accumulateSupport = [](SupportBand& band, float support)
    {
        ++band.samples;
        if (support > 0.5f)
            ++band.supported;
    };
    for (std::size_t index = 0; index < hit.size(); ++index)
    {
        if (hit[index].x <= 0.5f || coverage[index].x <= 1.0f / 255.0f)
            continue;
        const double expectedMeters = stage13shape::EvaluateLocalThicknessMeters(
            potential[index].x, type[index].x);
        const double actualMeters = thicknessFrame[index].x * 6000.0;
        maximumFormulaError = std::max(
            maximumFormulaError, std::abs(expectedMeters - actualMeters));
        const double expectedFootprint = stage13shape::EvaluateTypedFootprintScale(
            localHeight[index].x, type[index].x);
        const double expectedProfile = evaluateActiveVerticalProfile(
            localHeight[index].x, type[index].x) * expectedFootprint;
        const double profileError = std::abs(
            expectedProfile - typedProfile[index].x);
        if (profileError > maximumProfileError)
        {
            maximumProfileError = profileError;
            maximumProfileExpected = expectedProfile;
            maximumProfileActual = typedProfile[index].x;
            maximumProfileHeight = localHeight[index].x;
            maximumProfileType = type[index].x;
        }
        const double expectedCoverage = stage13shape::EvaluateEffectiveShapeCoverage(
            renderer.CloudSettings().coverage, coverage[index].x,
            expectedFootprint);
        // LocalHeight debug는 column 밖을 0으로 표시하므로 outside sample에서는
        // 실제 footprint 입력을 복원할 수 없다. profile이 존재하는 내부만 대조한다.
        const double coverageError = std::abs(
            expectedCoverage - effectiveCoverage[index].x);
        if (typedProfile[index].x > 1.0e-5f &&
            coverageError > maximumCoverageError)
        {
            maximumCoverageError = coverageError;
            maximumCoverageExpected = expectedCoverage;
            maximumCoverageActual = effectiveCoverage[index].x;
            maximumCoverageWeather = coverage[index].x;
            maximumCoverageFootprint = expectedFootprint;
            maximumCoverageProfile = typedProfile[index].x;
            maximumCoverageHeight = localHeight[index].x;
            maximumCoverageType = type[index].x;
        }
        const double expectedSupport = stage13shape::RemapCoverage(
            rawNoise[index].x, expectedCoverage) > 0.0 ? 1.0 : 0.0;
        // LocalHeight debug는 column 밖을 0으로 표시하므로 inside 여부를 잃는다.
        // profile이 실제로 존재하는 column 내부에서만 threshold 식을 대조한다.
        if (typedProfile[index].x > 1.0e-5f)
            maximumSupportError = std::max(maximumSupportError,
                std::abs(expectedSupport - baseSupport[index].x));
        thicknesses.push_back(actualMeters);
        if (actualMeters >= 5900.0)
            ++ceilingCount;
    }
    if (thicknesses.empty())
        return 6;

    // Physical Shape는 Weather/Base/Detail에 같은 f(p-vt) 변위를 사용한다.
    // 카메라와 시간을 v*dt만큼 함께 이동하면 모든 밀도 진단이 원래 화면과 일치해야 한다.
    constexpr float motionTimeSeconds = 100.0f;
    const auto motionOffset = stage13shape::EvaluatePhysicalCloudAdvectionOffset(
        renderer.CloudSettings().windDirection.x,
        renderer.CloudSettings().windDirection.z,
        renderer.CloudSettings().windSpeed, motionTimeSeconds);
    const DirectX::XMFLOAT3 shiftedTarget = {
        static_cast<float>(motionOffset.x), 4500.0f,
        static_cast<float>(motionOffset.z)
    };
    camera.SetOrbit(0.0f, 0.0f, 32000.0f, shiftedTarget);
    const CloudDebugMode motionModes[] = {
        CloudDebugMode::RawNoise,
        CloudDebugMode::WeatherCoverage,
        CloudDebugMode::CloudType,
        CloudDebugMode::LocalThickness,
        CloudDebugMode::BaseSupportBeforeDensity,
        CloudDebugMode::BaseDensity,
        CloudDebugMode::DetailNoise,
        CloudDebugMode::FinalDensity,
    };
    double maximumMotionMae = 0.0;
    std::map<CloudDebugMode, CloudDiagnosticFrame> shiftedFrames;
    for (CloudDebugMode mode : motionModes)
    {
        CloudDiagnosticFrame shifted;
        if (!renderer.CaptureCloudDiagnosticFrame(
                camera, motionTimeSeconds, mode, shifted))
            return 11;
        maximumMotionMae = std::max(maximumMotionMae,
            DiagnosticFrameMeanAbsoluteError(frames.at(mode), shifted));
        shiftedFrames.emplace(mode, std::move(shifted));
    }

    // Physical에서는 Legacy 전용 속도를 극단적으로 바꿔도 같은 시간/카메라 결과가 같다.
    renderer.SetCloudWindSpeedsForValidation(
        renderer.CloudSettings().windSpeed, 999.0f, 0.0f);
    double maximumIgnoredSpeedMae = 0.0;
    for (CloudDebugMode mode : { CloudDebugMode::WeatherCoverage,
                                 CloudDebugMode::DetailNoise,
                                 CloudDebugMode::FinalDensity })
    {
        CloudDiagnosticFrame changedLegacySpeeds;
        if (!renderer.CaptureCloudDiagnosticFrame(
                camera, motionTimeSeconds, mode, changedLegacySpeeds))
            return 12;
        maximumIgnoredSpeedMae = std::max(maximumIgnoredSpeedMae,
            DiagnosticFrameMeanAbsoluteError(
                shiftedFrames.at(mode), changedLegacySpeeds));
    }

    // Bulk가 0이면 Physical은 Weather/Detail의 Legacy 속도와 관계없이 완전히 정지한다.
    renderer.SetCloudWindSpeedsForValidation(0.0f, 999.0f, 999.0f);
    camera.SetOrbit(0.0f, 0.0f, 32000.0f,
                    { 0.0f, 4500.0f, 0.0f });
    double maximumStoppedMae = 0.0;
    for (CloudDebugMode mode : { CloudDebugMode::WeatherCoverage,
                                 CloudDebugMode::RawNoise,
                                 CloudDebugMode::DetailNoise,
                                 CloudDebugMode::FinalDensity })
    {
        CloudDiagnosticFrame stoppedAtZero;
        CloudDiagnosticFrame stoppedLater;
        if (!renderer.CaptureCloudDiagnosticFrame(
                camera, 0.0f, mode, stoppedAtZero) ||
            !renderer.CaptureCloudDiagnosticFrame(
                camera, motionTimeSeconds, mode, stoppedLater))
            return 13;
        maximumStoppedMae = std::max(maximumStoppedMae,
            DiagnosticFrameMeanAbsoluteError(stoppedAtZero, stoppedLater));
    }
    const bool motionPassed = maximumMotionMae <= 0.002 &&
        maximumIgnoredSpeedMae <= 1e-7 && maximumStoppedMae <= 1e-7;
    {
        std::ostringstream motionLine;
        motionLine << std::fixed << std::setprecision(8)
                   << "[SHAPE][GPU-ADVECTION] TRANSLATED_MAE="
                   << maximumMotionMae
                   << " LEGACY_SPEED_MAE=" << maximumIgnoredSpeedMae
                   << " STOPPED_MAE=" << maximumStoppedMae << ' '
                   << (motionPassed ? "PASS" : "FAIL");
        WriteDiagnosticLine(motionLine.str());
    }
    if (!renderer.ApplyStage13OpenWorldPreset())
        return 14;
    renderer.ApplyStage9OptimizationPreset(
        Stage9OptimizationPreset::ApprovedReference);

    // Periodic Weather는 실제 두께 분산을 검증한다. 타입별 옆면 면적은 화면에
    // Stratus/Mixed/Cumulus 띠가 모두 보장되는 Channel Debug를 별도 측면에서 잰다.
    if (!renderer.ApplyStage5WeatherPreset(Stage5WeatherPreset::ChannelDebug))
        return 9;
    const CloudDebugMode supportModes[] = {
        CloudDebugMode::CloudHitMask,
        CloudDebugMode::WeatherCoverage,
        CloudDebugMode::CloudType,
        CloudDebugMode::LocalHeightFraction,
        CloudDebugMode::BaseSupportBeforeDensity,
    };
    const auto captureSupportView = [&](const DirectX::XMFLOAT3& target,
                                        float minimumType, float maximumType,
                                        SupportBand& bottom, SupportBand& middle,
                                        SupportBand& top)
    {
        camera.SetOrbit(1.57079632679f, 0.0f, 24000.0f, target);
        std::map<CloudDebugMode, CloudDiagnosticFrame> supportFrames;
        for (CloudDebugMode mode : supportModes)
        {
            CloudDiagnosticFrame frame;
            if (!renderer.CaptureCloudDiagnosticFrame(camera, 0.0f, mode, frame))
                return false;
            supportFrames.emplace(mode, std::move(frame));
        }
        const auto& supportHit = supportFrames.at(
            CloudDebugMode::CloudHitMask).pixels;
        const auto& supportCoverage = supportFrames.at(
            CloudDebugMode::WeatherCoverage).pixels;
        const auto& supportType = supportFrames.at(CloudDebugMode::CloudType).pixels;
        const auto& supportHeight = supportFrames.at(
            CloudDebugMode::LocalHeightFraction).pixels;
        const auto& supportMask = supportFrames.at(
            CloudDebugMode::BaseSupportBeforeDensity).pixels;
        for (std::size_t index = 0; index < supportHit.size(); ++index)
        {
            if (supportHit[index].x <= 0.5f ||
                supportCoverage[index].x <= 1.0f / 255.0f ||
                supportType[index].x < minimumType ||
                supportType[index].x > maximumType)
                continue;
            const float h = supportHeight[index].x;
            SupportBand* band = nullptr;
            if (h >= 0.05f && h <= 0.25f)
                band = &bottom;
            else if (h >= 0.40f && h <= 0.65f)
                band = &middle;
            else if (h >= 0.75f && h <= 0.95f)
                band = &top;
            if (band)
                accumulateSupport(*band, supportMask[index].x);
        }
        return true;
    };
    const float weatherSize = renderer.CloudSettings().weatherMapWorldSize;
    const DirectX::XMFLOAT3 mixedTarget = {
        weatherSize * 0.30f, 1900.0f, weatherSize * 0.34f
    };
    const DirectX::XMFLOAT3 cumulusTarget = {
        weatherSize * 0.73f, 4200.0f, weatherSize * 0.69f
    };
    if (!captureSupportView(mixedTarget, 0.40f, 0.60f,
                            mixedBottom, mixedMiddle, mixedTop) ||
        !captureSupportView(cumulusTarget, 0.90f, 1.00f,
                            cumulusBottom, cumulusMiddle, cumulusTop))
        return 10;
    const double span = FramePercentile(thicknesses, 0.95) -
        FramePercentile(thicknesses, 0.05);
    const double stddev = FrameStandardDeviation(thicknesses);
    const double ceilingRatio = static_cast<double>(ceilingCount) /
        static_cast<double>(thicknesses.size());
    const bool passed = span >= 1000.0 && stddev >= 250.0 &&
        ceilingRatio <= 0.10 && maximumFormulaError <= 1.0;
    const bool profilePassed = maximumProfileError <= 1e-4 &&
        maximumCoverageError <= 1e-4;
    const bool supportFormulaPassed = maximumSupportError <= 1e-4;
    const bool supportSamplesPassed = mixedBottom.samples >= 32u &&
        mixedMiddle.samples >= 32u && mixedTop.samples >= 32u &&
        cumulusBottom.samples >= 32u && cumulusMiddle.samples >= 32u &&
        cumulusTop.samples >= 32u;
    // 13-4E는 vertical profile을 threshold에서 분리한다. 따라서 같은
    // Channel Debug 열의 Base threshold support가 위/아래에서 다시 좁아지지
    // 않아야 하며 세로 형상 차이는 Typed Profile/Base Density가 담당한다.
    const bool supportShapePassed = supportSamplesPassed &&
        mixedBottom.Ratio() >= 0.95 && mixedMiddle.Ratio() >= 0.95 &&
        mixedTop.Ratio() >= 0.95 && cumulusBottom.Ratio() >= 0.95 &&
        cumulusMiddle.Ratio() >= 0.95 && cumulusTop.Ratio() >= 0.95;
    std::ostringstream line;
    line << std::fixed << std::setprecision(5)
         << "[SHAPE][GPU-SLICE] TOP_SPAN_M=" << span
         << " TOP_STDDEV_M=" << stddev
         << " CEILING_RATIO=" << ceilingRatio
         << " CPU_HLSL_MAX_ERROR_M=" << maximumFormulaError << ' '
         << "PROFILE_MAX_ERROR=" << maximumProfileError << ' '
         << "PROFILE_SAMPLE=" << maximumProfileExpected << '/'
         << maximumProfileActual << "@" << maximumProfileHeight << '/'
         << maximumProfileType << ' '
         << "COVERAGE_MAX_ERROR=" << maximumCoverageError << ' '
         << "COVERAGE_SAMPLE=" << maximumCoverageExpected << '/'
         << maximumCoverageActual << "@" << maximumCoverageWeather << '/'
         << maximumCoverageFootprint << "/p" << maximumCoverageProfile
         << "/h" << maximumCoverageHeight << "/t" << maximumCoverageType << ' '
         << "SUPPORT_MAX_ERROR=" << maximumSupportError << ' '
         << "MIXED_SUPPORT=" << mixedBottom.Ratio() << '/'
         << mixedMiddle.Ratio() << '/' << mixedTop.Ratio() << "(N="
         << mixedBottom.samples << '/' << mixedMiddle.samples << '/'
         << mixedTop.samples << ") "
         << "CUMULUS_SUPPORT=" << cumulusBottom.Ratio() << '/'
         << cumulusMiddle.Ratio() << '/' << cumulusTop.Ratio() << "(N="
         << cumulusBottom.samples << '/' << cumulusMiddle.samples << '/'
         << cumulusTop.samples << ") "
         << (passed && profilePassed && supportFormulaPassed && supportShapePassed
             ? "PASS" : "FAIL");
    WriteDiagnosticLine(line.str());
    if (renderer.HasDebugLayerErrors())
        return 7;
    return passed && profilePassed && supportFormulaPassed && supportShapePassed &&
        motionPassed
        ? 0 : 8;
}

}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR commandLine, int)
{
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    struct ComScope
    {
        HRESULT result;
        ~ComScope() { if (SUCCEEDED(result)) CoUninitialize(); }
    } comScope{ comResult };

    // Stage6Smoke는 중첩 View/Light Ray를 확인하므로 D3D 경로 검증에 충분한 작은 타깃을 쓴다.
    // 일반 실행과 이전 단계 smoke의 해상도·프레임 해시는 바꾸지 않는다.
    const bool requestedStage6Smoke = commandLine &&
        wcsstr(commandLine, L"--stage6-smoke-test") != nullptr;
    const bool requestedStage7Smoke = commandLine &&
        wcsstr(commandLine, L"--stage7-smoke-test") != nullptr;
    const bool requestedStage8Smoke = commandLine &&
        wcsstr(commandLine, L"--stage8-smoke-test") != nullptr;
    const bool requestedPerformanceOverlaySmoke = commandLine &&
        wcsstr(commandLine, L"--performance-overlay-smoke-test") != nullptr;
    const bool requestedStage13DomainSmoke = commandLine &&
        wcsstr(commandLine, L"--stage13-domain-smoke-test") != nullptr;
    const bool requestedStage13SimilarityGpu = commandLine &&
        wcsstr(commandLine, L"--stage13-similarity-gpu-test") != nullptr;
    const bool requestedStage13OpenWorldSmoke = commandLine &&
        wcsstr(commandLine, L"--stage13-open-world-smoke-test") != nullptr;
    const bool requestedStage13UnifiedSceneSmoke = commandLine &&
        wcsstr(commandLine, L"--stage13-unified-scene-smoke-test") != nullptr;
    const bool requestedStage13OpticsLightingSmoke = commandLine &&
        wcsstr(commandLine, L"--stage13-optics-lighting-smoke-test") != nullptr;
    const bool requestedStage13LightingPerformance = commandLine &&
        wcsstr(commandLine, L"--stage13-lighting-performance-test") != nullptr;
    const bool requestedStage13NoiseVolumeSmoke = commandLine &&
        wcsstr(commandLine, L"--stage13-noise-volume-smoke-test") != nullptr;
    const bool requestedStage13WeatherShapeGpu = commandLine &&
        wcsstr(commandLine, L"--stage13-weather-shape-gpu-test") != nullptr;
    const bool requestedStage9OptimizationSmoke = commandLine &&
        wcsstr(commandLine, L"--stage9-optimization-smoke-test") != nullptr;
    const bool requestedStage9Performance = commandLine &&
        wcsstr(commandLine, L"--stage9-performance-test") != nullptr;
    const bool requestedStage10UpsamplingSmoke = commandLine &&
        wcsstr(commandLine, L"--stage10-upsampling-smoke-test") != nullptr;
    const bool requestedStage11TemporalSmoke = commandLine &&
        wcsstr(commandLine, L"--stage11-temporal-smoke-test") != nullptr;
    const bool requestedStage12ShadowSmoke = commandLine &&
        wcsstr(commandLine, L"--stage12-shadow-smoke-test") != nullptr;
    const bool requestedStage12Performance = commandLine &&
        wcsstr(commandLine, L"--stage12-performance-test") != nullptr;
    const bool requestedStage14AtmosphereSmoke = commandLine &&
        wcsstr(commandLine, L"--stage14-atmosphere-smoke-test") != nullptr;
    const bool requestedStage14Performance = commandLine &&
        wcsstr(commandLine, L"--stage14-performance-test") != nullptr;
    const bool requestedStage15PresetSmoke = commandLine &&
        wcsstr(commandLine, L"--stage15-preset-smoke-test") != nullptr;
    const bool requestedStage15Quality = commandLine &&
        wcsstr(commandLine, L"--stage15-quality-test") != nullptr;
    const bool requestedStage15Performance = commandLine &&
        wcsstr(commandLine, L"--stage15-performance-test") != nullptr;
    const bool requestedStage15RegressionProbe = commandLine &&
        wcsstr(commandLine,
               L"--stage15-stage14-regression-probe") != nullptr;
    const bool requestedStage15OutputDpiSmoke = commandLine &&
        wcsstr(commandLine,
               L"--stage15-output-dpi-smoke-test") != nullptr;
    const bool requestedStage15CaptureSmoke = commandLine &&
        wcsstr(commandLine,
               L"--stage15-capture-smoke-test") != nullptr;
    const bool requestedNative1080p = commandLine &&
        wcsstr(commandLine, L"--stage15-native-1080p") != nullptr;
    const bool requestedShaderCacheSmoke = commandLine &&
        wcsstr(commandLine, L"--shader-cache-smoke-test") != nullptr;
    const bool requestedSmallGpuSmoke = requestedStage6Smoke || requestedStage7Smoke ||
        requestedStage8Smoke ||
        requestedPerformanceOverlaySmoke || requestedStage13DomainSmoke ||
        requestedStage13OpenWorldSmoke || requestedStage13NoiseVolumeSmoke ||
        requestedStage13WeatherShapeGpu || requestedStage13UnifiedSceneSmoke ||
        requestedStage13OpticsLightingSmoke || requestedStage9OptimizationSmoke ||
        requestedStage10UpsamplingSmoke || requestedStage11TemporalSmoke ||
        requestedStage12ShadowSmoke || requestedStage14AtmosphereSmoke ||
        requestedStage15PresetSmoke;
    const int kWidth  = (requestedStage13LightingPerformance ||
        requestedStage9Performance || requestedStage11TemporalSmoke ||
        requestedStage12Performance || requestedStage14Performance ||
        requestedStage15Quality || requestedStage15Performance ||
        requestedStage15RegressionProbe) ? 1920 :
        (requestedStage13SimilarityGpu ||
                         requestedStage13WeatherShapeGpu ||
                         requestedStage13UnifiedSceneSmoke) ? 320 :
        (requestedSmallGpuSmoke ? 96 : 1280);
    const int kHeight = (requestedStage9Performance ||
        requestedStage11TemporalSmoke || requestedStage12Performance ||
        requestedStage14Performance || requestedStage15Quality ||
        requestedStage15Performance || requestedStage15RegressionProbe) ? 1080 :
        (requestedStage13LightingPerformance ? 925 :
        (requestedStage13SimilarityGpu ||
                          requestedStage13WeatherShapeGpu ||
                          requestedStage13UnifiedSceneSmoke) ? 180 :
        (requestedSmallGpuSmoke ? 54 : 720));

    const bool smokeTest = commandLine &&
        wcsstr(commandLine, L"--foundation-smoke-test") != nullptr;
    const bool stage1SmokeTest = commandLine &&
        wcsstr(commandLine, L"--stage1-smoke-test") != nullptr;
    const bool stage2SmokeTest = commandLine &&
        wcsstr(commandLine, L"--stage2-smoke-test") != nullptr;
    const bool stage3SmokeTest = commandLine &&
        wcsstr(commandLine, L"--stage3-smoke-test") != nullptr;
    const bool stage4SmokeTest = commandLine &&
        wcsstr(commandLine, L"--stage4-smoke-test") != nullptr;
    const bool stage5SmokeTest = commandLine &&
        wcsstr(commandLine, L"--stage5-smoke-test") != nullptr;
    const bool stage6SmokeTest = requestedStage6Smoke;
    const bool stage7SmokeTest = requestedStage7Smoke;
    const bool stage8SmokeTest = requestedStage8Smoke;
    const bool performanceOverlaySmokeTest = requestedPerformanceOverlaySmoke;
    const bool stage13DomainSmokeTest = requestedStage13DomainSmoke;
    const bool stage13SimilarityGpuTest = requestedStage13SimilarityGpu;
    const bool stage13OpenWorldSmokeTest = requestedStage13OpenWorldSmoke;
    const bool stage13UnifiedSceneSmokeTest = requestedStage13UnifiedSceneSmoke;
    const bool stage13OpticsLightingSmokeTest =
        requestedStage13OpticsLightingSmoke;
    const bool stage13LightingPerformanceTest =
        requestedStage13LightingPerformance;
    const bool stage13NoiseVolumeSmokeTest = requestedStage13NoiseVolumeSmoke;
    const bool stage13WeatherShapeGpuTest = requestedStage13WeatherShapeGpu;
    const bool stage9OptimizationSmokeTest = requestedStage9OptimizationSmoke;
    const bool stage9PerformanceTest = requestedStage9Performance;
    const bool stage10UpsamplingSmokeTest = requestedStage10UpsamplingSmoke;
    const bool stage11TemporalSmokeTest = requestedStage11TemporalSmoke;
    const bool stage12ShadowSmokeTest = requestedStage12ShadowSmoke;
    const bool stage12PerformanceTest = requestedStage12Performance;
    const bool stage14AtmosphereSmokeTest = requestedStage14AtmosphereSmoke;
    const bool stage14PerformanceTest = requestedStage14Performance;
    const bool stage15PresetSmokeTest = requestedStage15PresetSmoke;
    const bool stage15QualityTest = requestedStage15Quality;
    const bool stage15PerformanceTest = requestedStage15Performance;
    const bool stage15RegressionProbe = requestedStage15RegressionProbe;
    const bool noiseLabSmokeTest = commandLine &&
        wcsstr(commandLine, L"--noise-lab-smoke-test") != nullptr;
    const bool shaderHotReloadSmokeTest = commandLine &&
        wcsstr(commandLine, L"--shader-hot-reload-smoke-test") != nullptr;
    const bool shaderCacheSmokeTest = requestedShaderCacheSmoke;
    const bool automatedTestRun = smokeTest || stage1SmokeTest || stage2SmokeTest ||
        stage3SmokeTest || stage4SmokeTest || stage5SmokeTest || stage6SmokeTest ||
        stage7SmokeTest || stage8SmokeTest || performanceOverlaySmokeTest ||
        stage13DomainSmokeTest || stage13SimilarityGpuTest ||
        stage13OpenWorldSmokeTest || stage13NoiseVolumeSmokeTest ||
        stage13WeatherShapeGpuTest || stage13UnifiedSceneSmokeTest ||
        stage13OpticsLightingSmokeTest || stage13LightingPerformanceTest ||
        stage9OptimizationSmokeTest || stage9PerformanceTest ||
        stage10UpsamplingSmokeTest || stage11TemporalSmokeTest ||
        stage12ShadowSmokeTest || stage12PerformanceTest ||
        stage14AtmosphereSmokeTest || stage14PerformanceTest ||
        stage15PresetSmokeTest || stage15QualityTest ||
        stage15PerformanceTest || stage15RegressionProbe ||
        requestedStage15OutputDpiSmoke || requestedStage15CaptureSmoke ||
        noiseLabSmokeTest || shaderHotReloadSmokeTest || shaderCacheSmokeTest;
    const bool enableNoiseVolumes = !automatedTestRun ||
        stage13OpenWorldSmokeTest || stage13NoiseVolumeSmokeTest ||
        stage13WeatherShapeGpuTest || stage13UnifiedSceneSmokeTest ||
        stage13OpticsLightingSmokeTest || stage13LightingPerformanceTest ||
        stage9OptimizationSmokeTest || stage9PerformanceTest ||
        stage10UpsamplingSmokeTest || stage11TemporalSmokeTest ||
        stage12ShadowSmokeTest || stage12PerformanceTest ||
        stage14PerformanceTest || stage15PresetSmokeTest ||
        stage15QualityTest || stage15PerformanceTest || stage15RegressionProbe ||
        requestedStage15CaptureSmoke;

    std::filesystem::path hotReloadShaderDirectory;
    if (shaderHotReloadSmokeTest)
    {
        hotReloadShaderDirectory = std::filesystem::temp_directory_path() /
            (L"VolumetricCloudHotReload-" + std::to_wstring(GetCurrentProcessId()));
        std::error_code error;
        std::filesystem::create_directories(hotReloadShaderDirectory, error);
        std::filesystem::copy(
            std::filesystem::path(VCLOUD_SHADER_SOURCE_DIR), hotReloadShaderDirectory,
            std::filesystem::copy_options::recursive |
            std::filesystem::copy_options::overwrite_existing, error);
        if (error || !SetEnvironmentVariableW(
                L"VCLOUD_SHADER_OVERRIDE_DIR", hotReloadShaderDirectory.c_str()))
            return 5;
    }

    // ---- 객체 생성 ----
    Window   window(hInstance, kWidth, kHeight,
                    L"VolumetricCloud - Stage 15 | 최종 프리셋 초기화 중",
                    !smokeTest && !stage1SmokeTest && !stage2SmokeTest &&
                    !stage3SmokeTest && !stage4SmokeTest && !stage5SmokeTest &&
                    !stage6SmokeTest && !stage7SmokeTest && !stage8SmokeTest &&
                    !performanceOverlaySmokeTest && !stage13DomainSmokeTest &&
                    !stage13SimilarityGpuTest && !stage13OpenWorldSmokeTest &&
                    !stage13NoiseVolumeSmokeTest &&
                    !stage13WeatherShapeGpuTest && !stage13UnifiedSceneSmokeTest &&
                    !stage13OpticsLightingSmokeTest &&
                    !stage13LightingPerformanceTest &&
                    !stage9OptimizationSmokeTest &&
                    !stage9PerformanceTest &&
                    !stage10UpsamplingSmokeTest && !stage11TemporalSmokeTest &&
                    !stage12ShadowSmokeTest && !stage12PerformanceTest &&
                    !stage14AtmosphereSmokeTest && !stage14PerformanceTest &&
                    !stage15PresetSmokeTest && !stage15QualityTest &&
                    !stage15PerformanceTest && !stage15RegressionProbe &&
                    !requestedStage15OutputDpiSmoke &&
                    !requestedStage15CaptureSmoke &&
                    !noiseLabSmokeTest && !shaderHotReloadSmokeTest &&
                    !shaderCacheSmokeTest);
    Camera   camera;
    Renderer renderer;

    if (requestedNative1080p && !requestedStage15OutputDpiSmoke)
    {
        const Native1080pResult nativeResult = window.EnterNative1080p();
        if (!Native1080pSucceeded(nativeResult))
        {
            WriteDiagnosticLine(std::string("[OUTPUT][NATIVE] FAIL: ") +
                                Native1080pResultText(nativeResult));
            return -3;
        }
    }

    int initialPhysicalWidth = window.GetWidth();
    int initialPhysicalHeight = window.GetHeight();
    window.QueryPhysicalClientExtent(
        initialPhysicalWidth, initialPhysicalHeight);
    camera.SetAspect(static_cast<float>(initialPhysicalWidth) /
                     std::max(initialPhysicalHeight, 1));

    if (!renderer.Init(window.GetHandle(), initialPhysicalWidth,
                       initialPhysicalHeight, enableNoiseVolumes))
        return -1; // 초기화 실패 (오류 메시지는 Renderer가 표시)

    // 단계 13 이하의 GPU 회귀는 당시 승인한 고정 하늘/환경광과 LDR shoulder를
    // 비교한다. Stage 14 전용 smoke를 제외한 자동 실행은 schema 34 호환 복원과
    // 같은 Manual Reference/Legacy Shoulder로 고정해 새 물리 기본값이 과거 gate의
    // 의미를 바꾸지 않게 한다.
    if (automatedTestRun && !stage14AtmosphereSmokeTest &&
        !stage14PerformanceTest && !stage15PresetSmokeTest &&
        !stage15QualityTest && !stage15PerformanceTest &&
        !stage15RegressionProbe)
    {
        renderer.MutableAtmosphereSettings().mode =
            AtmosphereMode::ManualReference;
        renderer.MutableToneMappingSettings().mode =
            ToneMappingMode::LegacyShoulder;
    }

    // 숨김 자동 검증은 D3D 분기 확인이 목적이다. 합성 모드를 사용하는 Foundation,
    // Noise Lab, Hot Reload smoke가 단계 6의 기본 128×16 중첩 비용을 그대로 쓰지
    // 않도록 낮은 표본 수를 적용한다. 일반 사용자 실행에는 영향을 주지 않는다.
    if (smokeTest || stage1SmokeTest || stage2SmokeTest || stage3SmokeTest ||
        stage4SmokeTest || stage5SmokeTest || stage6SmokeTest || stage7SmokeTest ||
        stage8SmokeTest || stage13DomainSmokeTest ||
        stage13SimilarityGpuTest ||
        performanceOverlaySmokeTest ||
        noiseLabSmokeTest || shaderHotReloadSmokeTest)
    {
        renderer.SetViewSamplingForSmoke(16u, 0.5f);
        renderer.SetLightSampling(4u, 1.0f);
    }

    // 입력/리사이즈 연결
    window.SetCamera(&camera);
    window.SetRenderer(&renderer);
    const auto updateOutputMetrics = [&]()
    {
        int physicalWidth = window.GetWidth();
        int physicalHeight = window.GetHeight();
        window.QueryPhysicalClientExtent(physicalWidth, physicalHeight);
        renderer.UpdateWindowMetrics(
            physicalWidth, physicalHeight, window.GetDpi(),
            window.GetDpiScale(), window.IsPerMonitorV2DpiAware(),
            window.IsNative1080p());
    };
    const auto outputCoreMatchesWindow = [&]()
    {
        const Stage15OutputExtentSnapshot extent =
            renderer.OutputExtentSnapshot();
        const int width = window.GetWidth();
        const int height = window.GetHeight();
        return renderer.SizeDependentResourcesValid() && width > 0 &&
            height > 0 && extent.physicalClientWidth == width &&
            extent.physicalClientHeight == height &&
            extent.swapChainWidth == width && extent.swapChainHeight == height &&
            extent.viewportWidth == width && extent.viewportHeight == height &&
            extent.sceneColorWidth == width &&
            extent.sceneColorHeight == height &&
            extent.sceneDepthWidth == width &&
            extent.sceneDepthHeight == height &&
            extent.historyWidth == width && extent.historyHeight == height &&
            extent.cloudWidth > 0 && extent.cloudHeight > 0;
    };
    updateOutputMetrics();

    const bool interactiveRun = !smokeTest && !stage1SmokeTest &&
        !stage2SmokeTest && !stage3SmokeTest && !stage4SmokeTest &&
        !stage5SmokeTest && !stage6SmokeTest && !stage7SmokeTest &&
        !stage8SmokeTest && !performanceOverlaySmokeTest &&
        !stage13DomainSmokeTest && !stage13SimilarityGpuTest &&
        !stage13OpenWorldSmokeTest && !stage13NoiseVolumeSmokeTest &&
        !stage13WeatherShapeGpuTest && !stage13UnifiedSceneSmokeTest &&
        !stage13OpticsLightingSmokeTest &&
        !stage13LightingPerformanceTest &&
        !stage9OptimizationSmokeTest &&
        !stage9PerformanceTest &&
        !stage10UpsamplingSmokeTest && !stage11TemporalSmokeTest &&
        !stage12ShadowSmokeTest && !stage12PerformanceTest &&
        !stage14AtmosphereSmokeTest && !stage14PerformanceTest &&
        !stage15PresetSmokeTest && !stage15QualityTest &&
        !stage15PerformanceTest && !stage15RegressionProbe &&
        !requestedStage15OutputDpiSmoke && !requestedStage15CaptureSmoke &&
        !noiseLabSmokeTest &&
        !shaderHotReloadSmokeTest && !shaderCacheSmokeTest;
    if (interactiveRun)
    {
        if (!renderer.ApplyStage15Defaults())
            return -2;
        window.ApplyInitialPortfolioCamera();
    }

    if (requestedStage15OutputDpiSmoke)
    {
        if (!renderer.ApplyStage15Defaults())
            return 20;
        const auto extentMatches = [](const Stage15OutputExtentSnapshot& value,
                                      int outputWidth, int outputHeight,
                                      int cloudWidth, int cloudHeight)
        {
            return value.physicalClientWidth == outputWidth &&
                value.physicalClientHeight == outputHeight &&
                value.swapChainWidth == outputWidth &&
                value.swapChainHeight == outputHeight &&
                value.viewportWidth == outputWidth &&
                value.viewportHeight == outputHeight &&
                value.sceneColorWidth == outputWidth &&
                value.sceneColorHeight == outputHeight &&
                value.sceneDepthWidth == outputWidth &&
                value.sceneDepthHeight == outputHeight &&
                value.historyWidth == outputWidth &&
                value.historyHeight == outputHeight &&
                value.cloudWidth == cloudWidth &&
                value.cloudHeight == cloudHeight;
        };

        updateOutputMetrics();
        Stage15OutputExtentSnapshot initial = renderer.OutputExtentSnapshot();
        if (!initial.perMonitorV2 ||
            !extentMatches(initial, 1280, 720, 640, 360))
        {
            WriteDiagnosticLine("[OUTPUT][WINDOWED] FAIL");
            return 21;
        }

        const Native1080pResult nativeResult = window.EnterNative1080p();
        updateOutputMetrics();
        Stage15OutputExtentSnapshot native = renderer.OutputExtentSnapshot();
        if (!Native1080pSucceeded(nativeResult) || !native.native1080p ||
            !extentMatches(native, 1920, 1080, 960, 540))
        {
            WriteDiagnosticLine(std::string("[OUTPUT][NATIVE] FAIL: ") +
                                Native1080pResultText(nativeResult));
            return 22;
        }

        renderer.RequestStage15QualityPreset(Stage15QualityPreset::High);
        renderer.Render(camera, 0.0f);
        const Stage15OutputExtentSnapshot high =
            renderer.OutputExtentSnapshot();
        if (!extentMatches(high, 1920, 1080, 1920, 1080) ||
            renderer.TemporalMode() != Stage11TemporalMode::FullResolution)
        {
            std::ostringstream detail;
            detail << "[OUTPUT][HIGH-FULL] FAIL physical="
                   << high.physicalClientWidth << 'x'
                   << high.physicalClientHeight << " swap="
                   << high.swapChainWidth << 'x' << high.swapChainHeight
                   << " viewport=" << high.viewportWidth << 'x'
                   << high.viewportHeight << " scene="
                   << high.sceneColorWidth << 'x' << high.sceneColorHeight
                   << " depth=" << high.sceneDepthWidth << 'x'
                   << high.sceneDepthHeight << " cloud=" << high.cloudWidth
                   << 'x' << high.cloudHeight << " history="
                   << high.historyWidth << 'x' << high.historyHeight
                   << " temporal="
                   << static_cast<std::uint32_t>(renderer.TemporalMode())
                   << " resources="
                   << (renderer.SizeDependentResourcesValid() ? 1 : 0);
            WriteDiagnosticLine(detail.str());
            return 23;
        }

        // Native 상태에서 Capture descriptor 적용이 실패해도 진단/누적/창 요청
        // 상태까지 frame-boundary transaction이 완전히 되돌리는지 검사한다.
        const std::uint64_t rollbackState = renderer.Stage15StateFingerprint();
        const std::uint64_t rollbackResources =
            renderer.Stage15GpuResourceIdentityFingerprint();
        const CloudDebugMode rollbackDebug = renderer.DebugMode();
        renderer.InjectStage15TransitionFailureForTest(
            Stage15TransitionFailurePoint::CloudTargetPreflight);
        renderer.RequestStage15DiagnosticMode(
            Stage15DiagnosticMode::CaptureStill);
        renderer.Render(camera, 0.0f);
        bool unexpectedNativeRequest = false;
        const bool captureTransactionRollback =
            renderer.Stage15TransitionFailed() &&
            renderer.Stage15Diagnostic() == Stage15DiagnosticMode::None &&
            renderer.CaptureState() == Stage15CaptureState::Inactive &&
            !renderer.SceneInputLocked() &&
            renderer.Stage15Quality() == Stage15QualityPreset::High &&
            renderer.TemporalMode() == Stage11TemporalMode::FullResolution &&
            renderer.DebugMode() == rollbackDebug &&
            renderer.Stage15StateFingerprint() == rollbackState &&
            renderer.Stage15GpuResourceIdentityFingerprint() ==
                rollbackResources &&
            !renderer.ConsumeNative1080pRequest(unexpectedNativeRequest);
        // 실패 시 유지된 요청을 명시적인 None으로 덮어 다음 frame에 재시도되지
        // 않는 계약도 함께 고정한다.
        renderer.RequestStage15DiagnosticMode(Stage15DiagnosticMode::None);
        renderer.Render(camera, 0.0f);
        if (!captureTransactionRollback ||
            renderer.Stage15Diagnostic() != Stage15DiagnosticMode::None ||
            renderer.CaptureState() != Stage15CaptureState::Inactive)
        {
            WriteDiagnosticLine("[OUTPUT][CAPTURE-ROLLBACK] FAIL");
            return 27;
        }

        if (!window.RestoreWindowed())
            return 24;
        updateOutputMetrics();
        Stage15OutputExtentSnapshot restored =
            renderer.OutputExtentSnapshot();
        if (restored.native1080p ||
            !extentMatches(restored, 1280, 720, 1280, 720))
        {
            WriteDiagnosticLine("[OUTPUT][RESTORE] FAIL");
            return 25;
        }

        // Capture가 요청한 Win32 Native 전환은 성공했지만 렌더 리소스 확인만
        // 실패한 상황을 주입한다. 실패 상태는 입력을 즉시 풀고, Capture가 연
        // Native 창을 반드시 자동 restore 요청해야 한다.
        renderer.RequestStage15DiagnosticMode(
            Stage15DiagnosticMode::CaptureStill);
        renderer.Render(camera, 1.0f);
        bool enableNative = false;
        if (!renderer.ConsumeNative1080pRequest(enableNative) || !enableNative)
            return 28;
        const Native1080pResult failureNativeResult = window.EnterNative1080p();
        updateOutputMetrics();
        if (!Native1080pSucceeded(failureNativeResult) ||
            !outputCoreMatchesWindow())
            return 29;
        renderer.NotifyNative1080pResult(
            true, false, window.IsNative1080p(),
            "Injected render resource resize failure");
        bool restoreNative = true;
        const bool failureRequestedRestore =
            renderer.CaptureState() == Stage15CaptureState::Failed &&
            !renderer.SceneInputLocked() &&
            renderer.ConsumeNative1080pRequest(restoreNative) &&
            !restoreNative;
        if (!failureRequestedRestore)
            return 37;
        window.InjectWindowedRestoreFailureForTest();
        if (window.RestoreWindowed() || !window.IsNative1080p())
            return 39;
        updateOutputMetrics();
        renderer.NotifyNative1080pResult(
            false, false, window.IsNative1080p(),
            "Injected windowed restore failure; retry pending");
        bool retryRestoreNative = true;
        if (!renderer.ConsumeNative1080pRequest(retryRestoreNative) ||
            retryRestoreNative || !window.RestoreWindowed())
            return 40;
        updateOutputMetrics();
        renderer.NotifyNative1080pResult(
            false, true, window.IsNative1080p(),
            "Injected failure restored to windowed output");
        renderer.RequestStage15DiagnosticMode(Stage15DiagnosticMode::None);
        renderer.Render(camera, 2.0f);
        restored = renderer.OutputExtentSnapshot();
        bool staleNativeRequest = false;
        if (restored.native1080p ||
            !extentMatches(restored, 1280, 720, 1280, 720) ||
            renderer.Stage15Diagnostic() != Stage15DiagnosticMode::None ||
            renderer.CaptureState() != Stage15CaptureState::Inactive ||
            renderer.ConsumeNative1080pRequest(staleNativeRequest))
            return 38;
        WriteDiagnosticLine(
            "[OUTPUT] PMv2 1280x720 -> Native 1920x1080 -> High Full -> "
            "transaction/failure restore PASS");
        return renderer.HasDebugLayerErrors() ? 26 : 0;
    }

    if (requestedStage15CaptureSmoke)
    {
        if (!renderer.ApplyStage15Defaults())
            return 30;
        renderer.RequestStage15DiagnosticMode(
            Stage15DiagnosticMode::CaptureStill);
        renderer.Render(camera, 1.0f);

        bool enableNative = false;
        if (!renderer.ConsumeNative1080pRequest(enableNative) ||
            !enableNative)
            return 31;
        const Native1080pResult nativeResult = window.EnterNative1080p();
        updateOutputMetrics();
        const bool nativeResourcesReady = outputCoreMatchesWindow();
        renderer.NotifyNative1080pResult(
            true, Native1080pSucceeded(nativeResult) && nativeResourcesReady,
            window.IsNative1080p(), nativeResourcesReady
                ? Native1080pResultText(nativeResult)
                : "Native window ready; render resource resize failed");
        if (!Native1080pSucceeded(nativeResult) || !nativeResourcesReady)
            return 32;

        for (std::uint32_t sample = 0;
             sample < stage15::kCaptureSampleCount; ++sample)
            renderer.Render(camera, 1.0f + static_cast<float>(sample));
        renderer.Render(camera, 9.0f); // Ready 고정 경로도 한 프레임 검증한다.

        const Stage15OutputExtentSnapshot captureExtent =
            renderer.OutputExtentSnapshot();
        if (renderer.CaptureState() != Stage15CaptureState::Ready ||
            renderer.CaptureCompletedSamples() !=
                stage15::kCaptureSampleCount ||
            renderer.TemporalMode() != Stage11TemporalMode::Off ||
            captureExtent.physicalClientWidth != 1920 ||
            captureExtent.physicalClientHeight != 1080 ||
            captureExtent.cloudWidth != 1920 ||
            captureExtent.cloudHeight != 1080 ||
            captureExtent.historyWidth != 1920 ||
            captureExtent.historyHeight != 1080)
            return 33;

        renderer.RequestStage15DiagnosticMode(Stage15DiagnosticMode::None);
        renderer.Render(camera, 10.0f);
        bool restoreNative = true;
        if (!renderer.ConsumeNative1080pRequest(restoreNative) ||
            restoreNative || !window.RestoreWindowed())
            return 34;
        updateOutputMetrics();
        renderer.NotifyNative1080pResult(
            false, true, window.IsNative1080p(),
            "Windowed output restored");
        const Stage15OutputExtentSnapshot restored =
            renderer.OutputExtentSnapshot();
        if (renderer.CaptureState() != Stage15CaptureState::Inactive ||
            renderer.Stage15Diagnostic() != Stage15DiagnosticMode::None ||
            restored.physicalClientWidth != 1280 ||
            restored.physicalClientHeight != 720 ||
            restored.cloudWidth != 640 || restored.cloudHeight != 360)
            return 35;
        WriteDiagnosticLine(
            "[CAPTURE] Native 1080p + frozen 4 spp HDR + Ready + restore PASS");
        return renderer.HasDebugLayerErrors() ? 36 : 0;
    }

    if (stage13SimilarityGpuTest)
        return RunStage13SimilarityGpuTest(renderer, camera);
    if (stage13OpenWorldSmokeTest)
        return RunStage13OpenWorldSmokeTest(renderer, camera);
    if (stage13UnifiedSceneSmokeTest)
        return RunStage13UnifiedSceneSmokeTest(renderer, camera);
    if (stage13OpticsLightingSmokeTest)
        return RunStage13OpticsLightingSmokeTest(renderer, camera);
    if (stage13LightingPerformanceTest)
        return RunStage13LightingPerformanceTest(renderer, camera);
    if (stage13NoiseVolumeSmokeTest)
        return RunStage13NoiseVolumeSmokeTest(renderer, camera);
    if (stage13WeatherShapeGpuTest)
        return RunStage13WeatherShapeGpuTest(renderer, camera);
    if (stage9OptimizationSmokeTest)
        return RunStage9OptimizationSmokeTest(renderer, camera);
    if (stage9PerformanceTest)
        return RunStage9PerformanceTest(renderer, camera);
    if (stage10UpsamplingSmokeTest)
        return RunStage10UpsamplingSmokeTest(renderer, camera);
    if (stage11TemporalSmokeTest)
        return RunStage11TemporalSmokeTest(renderer, camera);
    if (stage12ShadowSmokeTest)
        return RunStage12ShadowSmokeTest(renderer, camera);
    if (stage12PerformanceTest)
        return RunStage12ShadowPerformanceTest(renderer, camera);
    if (stage14AtmosphereSmokeTest)
        return RunStage14AtmosphereSmokeTest(renderer, camera);
    if (stage14PerformanceTest)
        return RunStage14PerformanceTest(renderer, camera);
    if (stage15PresetSmokeTest)
        return RunStage15PresetSmokeTest(renderer, camera);
    if (stage15QualityTest)
        return RunStage15QualityTest(renderer, camera);
    if (stage15PerformanceTest)
        return RunStage15PerformanceTest(renderer, camera);
    if (stage15RegressionProbe)
        return RunStage15Stage14RegressionProbe(renderer, camera);
    if (shaderCacheSmokeTest)
        return RunShaderCacheSmokeTest(renderer);

    struct VolumeFixture
    {
        DirectX::XMFLOAT3 minimum;
        DirectX::XMFLOAT3 maximum;
        float stepMeters;
    };
    const VolumeFixture volumeFixtures[] = {
        { { -2.0f, -1.0f, -2.0f }, { 2.0f, 2.0f, 2.0f }, 0.10f },
        { { -8.0f, -1.0f, -8.0f }, { 8.0f, 2.0f, 8.0f }, 0.10f },
        { { -2.0f, -1.0f, -0.5f }, { 2.0f, 2.0f, 0.5f }, 0.10f },
        { { -2.0f, -1.0f, -4.0f }, { 2.0f, 2.0f, 4.0f }, 0.10f },
        { { -2.0f, -1.0f, -2.0f }, { 2.0f, 2.0f, 2.0f }, 0.025f },
        { { -2.0f, -1.0f, -2.0f }, { 2.0f, 2.0f, 2.0f }, 0.50f },
    };
    const auto configureVolume = [&](int index)
    {
        const VolumeFixture& fixture = volumeFixtures[std::clamp(index, 0, 5)];
        renderer.ConfigureVolumeForTest(
            fixture.minimum, fixture.maximum, fixture.stepMeters);
    };
    struct NoiseFixture
    {
        float scale, coverage, density, wind, offset;
    };
    const NoiseFixture noiseFixtures[] = {
        { 0.35f, 0.55f, 1.0f, 0.25f, 0.0f },
        { 0.35f, 0.35f, 1.0f, 0.25f, 0.0f },
        { 0.35f, 0.75f, 1.0f, 0.25f, 0.0f },
        { 0.18f, 0.55f, 1.0f, 0.25f, 0.0f },
        { 0.70f, 0.55f, 1.0f, 0.25f, 0.0f },
        { 0.35f, 0.55f, 1.0f, 0.00f, 0.0f },
        { 0.35f, 0.55f, 1.0f, 0.80f, 0.0f },
        { 0.35f, 0.55f, 1.0f, 0.25f, 0.73f },
    };
    const auto configureNoise = [&](int index)
    {
        const NoiseFixture& fixture = noiseFixtures[std::clamp(index, 0, 7)];
        renderer.ConfigureNoiseForTest(
            fixture.scale, fixture.coverage, fixture.density,
            fixture.wind, fixture.offset);
    };
    struct DetailFixture { float scale, erosion, wind, offset; };
    const DetailFixture detailFixtures[] = {
        { 2.5f, 0.00f, 0.45f, 17.3f },
        { 2.5f, 0.25f, 0.45f, 17.3f },
        { 6.0f, 0.25f, 0.45f, 17.3f },
        { 2.5f, 0.55f, 0.45f, 17.3f },
    };
    const auto configureDetail = [&](int index)
    {
        const DetailFixture& fixture = detailFixtures[std::clamp(index, 0, 3)];
        renderer.ConfigureDetailForTest(
            fixture.scale, fixture.erosion, fixture.wind, fixture.offset);
    };

    if (smokeTest)
    {
        for (int frame = 0; frame < 3; ++frame)
            renderer.Render(camera, static_cast<float>(frame) / 60.0f);
        return renderer.HasDebugLayerErrors() ? 2 : 0;
    }

    if (stage1SmokeTest)
    {
        // 단계 1의 모든 분기 셰이더와 볼륨 프리셋이 실제 D3D11 draw에서
        // 오류 없이 실행되는지 검사한다.
        for (int mode = 0; mode <= 9; ++mode)
        {
            renderer.SetDebugMode(static_cast<CloudDebugMode>(mode));
            for (int preset = 0; preset < 6; ++preset)
            {
                configureVolume(preset);
                renderer.SetViewSamplingForSmoke(16u, 0.5f);
                renderer.Render(camera, static_cast<float>(mode + preset) / 60.0f);
            }
        }
        return renderer.HasDebugLayerErrors() ? 2 : 0;
    }

    if (stage2SmokeTest)
    {
        // 단계 2의 네 디버그 출력과 여덟 noise 프리셋을 실제 draw로 실행한다.
        for (int mode = static_cast<int>(CloudDebugMode::RawNoise);
             mode <= static_cast<int>(CloudDebugMode::NoiseUvw); ++mode)
        {
            renderer.SetDebugMode(static_cast<CloudDebugMode>(mode));
            for (int preset = 0; preset < 8; ++preset)
            {
                configureNoise(preset);
                renderer.Render(camera, static_cast<float>(mode + preset) / 60.0f);
            }
        }
        return renderer.HasDebugLayerErrors() ? 2 : 0;
    }

    if (stage3SmokeTest)
    {
        // 단계 3의 두 진단 모드와 기본/좁은/넓은/겹친 fade 조합을 GPU draw로 검사한다.
        const float profiles[][2] = {
            { 0.20f, 0.80f }, { 0.05f, 0.95f },
            { 0.35f, 0.65f }, { 0.80f, 0.20f },
        };
        for (int mode = static_cast<int>(CloudDebugMode::HeightFraction);
             mode <= static_cast<int>(CloudDebugMode::HeightProfile); ++mode)
        {
            renderer.SetDebugMode(static_cast<CloudDebugMode>(mode));
            for (const auto& profile : profiles)
            {
                renderer.SetHeightProfile(profile[0], profile[1]);
                renderer.Render(camera, static_cast<float>(mode) / 60.0f);
            }
        }
        return renderer.HasDebugLayerErrors() ? 2 : 0;
    }

    if (stage4SmokeTest)
    {
        // Base/Detail 중간값 네 종류와 네 프리셋을 작은/넓은 볼륨에서 모두 draw한다.
        const int volumes[] = { 0, 1 };
        for (int mode = static_cast<int>(CloudDebugMode::BaseDensity);
             mode <= static_cast<int>(CloudDebugMode::DetailSampleMask); ++mode)
        {
            renderer.SetDebugMode(static_cast<CloudDebugMode>(mode));
            for (int preset = 0; preset < 4; ++preset)
            {
                configureDetail(preset);
                for (int volume : volumes)
                {
                    configureVolume(volume);
                    renderer.Render(camera, static_cast<float>(mode + preset) / 60.0f);
                }
            }
        }
        return renderer.HasDebugLayerErrors() ? 2 : 0;
    }

    if (stage5SmokeTest)
    {
        // Weather 중간값 네 종류를 세 CPU 맵, Q/Y 볼륨, 이동 전/후에 draw한다.
        const int volumes[] = { 0, 1 };
        const std::uintptr_t weatherTextureIdentity =
            renderer.WeatherTextureIdentity();
        const std::uintptr_t weatherSrvIdentity = renderer.WeatherSrvIdentity();
        if (weatherTextureIdentity == 0 || weatherSrvIdentity == 0)
            return 3;
        for (int mode = static_cast<int>(CloudDebugMode::WeatherCoverage);
             mode <= static_cast<int>(CloudDebugMode::TypedShapeProfile); ++mode)
        {
            renderer.SetDebugMode(static_cast<CloudDebugMode>(mode));
            for (int preset = static_cast<int>(Stage5WeatherPreset::UniformLegacy);
                 preset <= static_cast<int>(Stage5WeatherPreset::ChannelDebug); ++preset)
            {
                if (!renderer.ApplyStage5WeatherPreset(
                        static_cast<Stage5WeatherPreset>(preset)))
                    return 3;
                if (renderer.WeatherTextureIdentity() != weatherTextureIdentity ||
                    renderer.WeatherSrvIdentity() != weatherSrvIdentity)
                    return 4;
                for (int volume : volumes)
                {
                    configureVolume(volume);
                    renderer.Render(camera, 0.0f);
                    renderer.Render(camera, 4.0f);
                }
            }
        }
        if (!renderer.ApplyStage5WeatherPreset(Stage5WeatherPreset::PeriodicPerlin))
            return 5;
        const std::uint64_t defaultHash = renderer.WeatherMapHash();
        WeatherMapGeneratorSettings changedGenerator;
        changedGenerator.coverage.seed += 1u;
        if (!renderer.ApplyWeatherGeneratorSettings(changedGenerator) ||
            renderer.WeatherMapHash() == defaultHash)
            return 6;
        if (renderer.WeatherTextureIdentity() != weatherTextureIdentity ||
            renderer.WeatherSrvIdentity() != weatherSrvIdentity)
            return 7;
        renderer.Render(camera, 0.0f);
        return renderer.HasDebugLayerErrors() ? 2 : 0;
    }

    if (stage6SmokeTest)
    {
        // 조명 진단 4종과 태양·Weather·Q/Y 값을 pairwise 실제 draw한다.
        const int volumes[] = { 0, 1 };
        const Stage6SunPreset suns[] = {
            Stage6SunPreset::Noon,
            Stage6SunPreset::LowEast,
            Stage6SunPreset::LowWest,
        };
        // 자동 smoke 조합은 바인딩/분기 검증이 목적이므로 4-step으로 실행한다.
        // 아래에서 8/16/32 설정을 각각 별도 draw해 실제 품질 경로도 확인한다.
        renderer.SetLightSampling(4u, 1.0f);
        renderer.EnableNoiseLabPreviews(false);
        for (int mode = static_cast<int>(CloudDebugMode::LightTransmittance);
             mode <= static_cast<int>(CloudDebugMode::DirectSingleScattering); ++mode)
        {
            renderer.SetDebugMode(static_cast<CloudDebugMode>(mode));
            // 네 pairwise draw가 모든 mode를 실행하며 sun/weather/volume 값도
            // 순환 배치해 각각 최소 한 번 실제 b1/b3 및 t2 경로를 지난다.
            const int combination = mode -
                static_cast<int>(CloudDebugMode::LightTransmittance);
            renderer.ApplyStage6SunPreset(suns[combination % 3]);
            if (!renderer.ApplyStage5WeatherPreset(
                    static_cast<Stage5WeatherPreset>(combination % 3)))
                return 3;
            configureVolume(volumes[combination % 2]);
            renderer.SetViewSamplingForSmoke(1u, 32.0f);
            renderer.Render(camera, 0.0f);
        }

        // 품질/비용 비교용 8/16/32 step 설정도 같은 b3 경로로 전달한다.
        const std::uint32_t lightSteps[] = { 8u, 16u, 32u };
        for (std::uint32_t steps : lightSteps)
        {
            renderer.SetLightSampling(steps, 0.25f);
            renderer.SetViewSamplingForSmoke(1u, 32.0f);
            renderer.SetDebugMode(CloudDebugMode::TotalLightSamples);
            renderer.Render(camera, 0.0f);
            if (renderer.LightSettings().maxLightSteps != steps)
                return 4;
        }

        const std::filesystem::path exportRoot =
            std::filesystem::temp_directory_path() / L"VolumetricCloudStage6Smoke";
        if (!renderer.ExportNoiseLabSnapshot(exportRoot))
            return 5;
        bool foundCurrentSchema = false;
        std::error_code exportError;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 exportRoot, exportError))
        {
            if (exportError)
                return 6;
            if (entry.path().filename() != L"noise-settings.json")
                continue;
            std::string metadata;
            if (ReadTextFile(entry.path(), metadata) &&
                metadata.find("\"schemaVersion\": 37") != std::string::npos &&
                metadata.find("\"stage13Preset\"") == std::string::npos &&
                metadata.find("\"A\": \"localThicknessPotential\"") != std::string::npos &&
                metadata.find("\"localThickness\": {\"seed\": 4051") != std::string::npos &&
                metadata.find("\"thicknessCoverageInfluence\": 0.200000") != std::string::npos &&
                metadata.find("\"worldSizeMeters\": 12000") != std::string::npos &&
                metadata.find("\"verticalWorldSizeMeters\": 12000") != std::string::npos &&
                metadata.find("\"minimumLocalThicknessFraction\"") != std::string::npos &&
                metadata.find("\"localHeightVariation\"") != std::string::npos &&
                metadata.find("\"cumulusTopBoost\"") != std::string::npos &&
                metadata.find("\"singleScatteringAlbedo\"") != std::string::npos &&
                metadata.find("\"lightRayDensitySource\": \"baseDensityWithoutDetailErosion\"") != std::string::npos)
                foundCurrentSchema = true;
        }
        if (!foundCurrentSchema)
            return 7;
        return renderer.HasDebugLayerErrors() ? 2 : 0;
    }

    if (stage7SmokeTest)
    {
        const int volumes[] = { 0, 1 };
        const Stage6SunPreset suns[] = {
            Stage6SunPreset::Noon,
            Stage6SunPreset::LowEast,
            Stage6SunPreset::LowWest,
        };
        const Stage7PhasePreset phases[] = {
            Stage7PhasePreset::Off,
            Stage7PhasePreset::Balanced,
            Stage7PhasePreset::SilverLining,
            Stage7PhasePreset::BackscatterCheck,
        };

        renderer.EnableNoiseLabPreviews(false);
        renderer.SetViewSamplingForSmoke(1u, 32.0f);
        renderer.SetLightSampling(4u, 1.0f);
        for (int mode = static_cast<int>(CloudDebugMode::PhaseCosTheta);
             mode <= static_cast<int>(CloudDebugMode::DualPhaseFactor); ++mode)
        {
            const int combination = mode -
                static_cast<int>(CloudDebugMode::PhaseCosTheta);
            renderer.SetDebugMode(static_cast<CloudDebugMode>(mode));
            renderer.ApplyStage6SunPreset(suns[combination % 3]);
            renderer.ApplyStage7PhasePreset(phases[combination % 4]);
            configureVolume(volumes[combination % 2]);
            renderer.Render(camera, 0.0f);
        }

        // Phase 프리셋을 바꿔도 태양 방향 프리셋 이름과 Light Ray 품질은 유지되어야 한다.
        renderer.ApplyStage6SunPreset(Stage6SunPreset::Noon);
        renderer.SetLightSampling(4u, 1.0f);
        renderer.ApplyStage6SunPreset(Stage6SunPreset::Noon);
        renderer.ApplyStage7PhasePreset(Stage7PhasePreset::Balanced);
        if (renderer.SunPreset() != Stage6SunPreset::Noon ||
            renderer.PhasePreset() != Stage7PhasePreset::Balanced ||
            renderer.LightSettings().maxLightSteps != 4u ||
            sizeof(LightParameters) != 80u)
            return 3;

        // 합성 경로에서 Off와 방향성 Phase가 실제로 다른 프레임을 만드는지 확인한다.
        renderer.EnableFrameHashCapture(true);
        configureVolume(1);
        renderer.SetViewSamplingForSmoke(8u, 2.0f);
        renderer.SetDebugMode(CloudDebugMode::Composite);
        renderer.ApplyStage7PhasePreset(Stage7PhasePreset::Off);
        renderer.Render(camera, 0.0f);
        const std::uint64_t offHash = renderer.LastCloudFrameHash();
        renderer.ApplyStage7PhasePreset(Stage7PhasePreset::SilverLining);
        renderer.Render(camera, 0.0f);
        const std::uint64_t onHash = renderer.LastCloudFrameHash();
        if (offHash == 0 || onHash == 0 || offHash == onHash)
            return 4;

        renderer.SetDebugMode(CloudDebugMode::DirectSingleScattering);
        renderer.Render(camera, 0.0f);

        const std::filesystem::path exportRoot =
            std::filesystem::temp_directory_path() / L"VolumetricCloudStage7Smoke";
        if (!renderer.ExportNoiseLabSnapshot(exportRoot))
            return 5;
        bool foundSchema7 = false;
        std::error_code exportError;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 exportRoot, exportError))
        {
            if (exportError)
                return 6;
            if (entry.path().filename() != L"noise-settings.json")
                continue;
            std::string metadata;
            if (ReadTextFile(entry.path(), metadata) &&
                metadata.find("\"schemaVersion\": 37") != std::string::npos &&
                metadata.find("\"stage13Preset\"") == std::string::npos &&
                metadata.find("\"singleScatteringAlbedo\"") != std::string::npos &&
                metadata.find("\"phaseFunction\": \"dualLobeHenyeyGreensteinIsotropicRelative\"") != std::string::npos &&
                metadata.find("\"phaseDirectionConvention\": \"cosTheta=dot(cameraToSample,sampleToSun)\"") != std::string::npos &&
                metadata.find("\"lightingLook\": \"custom\"") != std::string::npos &&
                metadata.find("\"edgeOpticalDepthScale\"") != std::string::npos &&
                metadata.find("\"shadowExponent\"") != std::string::npos)
                foundSchema7 = true;
        }
        if (!foundSchema7)
            return 7;
        return renderer.HasDebugLayerErrors() ? 2 : 0;
    }

    if (stage8SmokeTest)
    {
        renderer.EnableNoiseLabPreviews(false);
        renderer.SetViewSamplingForSmoke(8u, 2.0f);
        renderer.SetLightSampling(4u, 1.0f);
        renderer.SetDebugMode(CloudDebugMode::AccumulatedDirectLighting);
        renderer.ApplyStage8EnvironmentPreset(Stage8EnvironmentPreset::Balanced);
        renderer.ApplyStage6SunPreset(Stage6SunPreset::Noon);
        configureVolume(1);
        renderer.Render(camera, 0.0f);

        // Environment 프리셋은 Phase와 태양 프리셋을 바꾸지 않는다.
        renderer.ApplyStage6SunPreset(Stage6SunPreset::Noon);
        renderer.ApplyStage7PhasePreset(Stage7PhasePreset::Balanced);
        renderer.ApplyStage8EnvironmentPreset(Stage8EnvironmentPreset::StrongFill);
        if (renderer.SunPreset() != Stage6SunPreset::Noon ||
            renderer.PhasePreset() != Stage7PhasePreset::Balanced ||
            renderer.EnvironmentPreset() != Stage8EnvironmentPreset::StrongFill ||
            sizeof(EnvironmentParameters) != 80u)
            return 3;

        renderer.EnableFrameHashCapture(true);
        configureVolume(1);
        renderer.SetDebugMode(CloudDebugMode::Composite);
        renderer.ApplyStage8EnvironmentPreset(Stage8EnvironmentPreset::Off);
        renderer.Render(camera, 0.0f);
        const std::uint64_t offHash = renderer.LastCloudFrameHash();
        renderer.ApplyStage8EnvironmentPreset(Stage8EnvironmentPreset::Balanced);
        renderer.Render(camera, 0.0f);
        const std::uint64_t balancedHash = renderer.LastCloudFrameHash();
        if (offHash == 0 || balancedHash == 0 || offHash == balancedHash)
            return 4;

        // 다중 산란은 기존 Light Ray를 재사용하므로 비용 출력 해시가 같아야 한다.
        renderer.SetDebugMode(CloudDebugMode::TotalLightSamples);
        renderer.ApplyStage8EnvironmentPreset(Stage8EnvironmentPreset::Off);
        renderer.Render(camera, 0.0f);
        const std::uint64_t samplesOffHash = renderer.LastCloudFrameHash();
        renderer.ApplyStage8EnvironmentPreset(Stage8EnvironmentPreset::Balanced);
        renderer.Render(camera, 0.0f);
        const std::uint64_t samplesOnHash = renderer.LastCloudFrameHash();
        if (samplesOffHash == 0 || samplesOffHash != samplesOnHash)
            return 5;

        const std::filesystem::path exportRoot =
            std::filesystem::temp_directory_path() / L"VolumetricCloudStage8Smoke";
        if (!renderer.ExportNoiseLabSnapshot(exportRoot))
            return 6;
        bool foundSchema8 = false;
        std::error_code exportError;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 exportRoot, exportError))
        {
            if (exportError)
                return 7;
            if (entry.path().filename() != L"noise-settings.json")
                continue;
            std::string metadata;
            if (ReadTextFile(entry.path(), metadata) &&
                metadata.find("\"schemaVersion\": 37") != std::string::npos &&
                metadata.find("\"developerUiLayout\": \"F1Noise_F2Weather_F3LightingAtmosphere_F4Stage15Camera\"") != std::string::npos &&
                metadata.find("\"camera\": {") != std::string::npos &&
                metadata.find("\"verticalFovDegrees\": 60") != std::string::npos &&
                metadata.find("\"savedPosition\": null") != std::string::npos &&
                metadata.find("\"physicalAdvectionMode\": \"legacyIndependentSpeeds\"") != std::string::npos &&
                metadata.find("\"singleScatteringAlbedo\"") != std::string::npos &&
                metadata.find("\"scatteringCoefficient\"") == std::string::npos &&
                metadata.find("\"implementationStage\": \"15\"") != std::string::npos &&
                metadata.find("\"noiseVolumes\"") != std::string::npos &&
                metadata.find("\"stage13Preset\"") == std::string::npos &&
                metadata.find("\"similarityScale\"") == std::string::npos &&
                metadata.find("\"environmentSource\": \"analyticColorsNoExternalTexture\"") != std::string::npos &&
                metadata.find("\"multipleScatteringModel\": \"reusedLightDepthInteriorWeightedOctaves\"") != std::string::npos &&
                metadata.find("\"ambientShadowExponent\"") != std::string::npos &&
                metadata.find("\"multipleScatteringInteriorBlend\"") != std::string::npos)
                foundSchema8 = true;
        }
        if (!foundSchema8)
            return 8;
        return renderer.HasDebugLayerErrors() ? 2 : 0;
    }

    if (stage13DomainSmokeTest)
    {
        renderer.EnableNoiseLabPreviews(false);
        renderer.EnableFrameHashCapture(true);
        renderer.SetCloudDomainType(CloudDomainType::PlanarLayer);
        renderer.SetViewSamplingForSmoke(8u, 1000.0f);
        renderer.SetLightSampling(1u, 1000.0f);

        // 지상에서 위, 층 내부 수평, 층 위에서 아래와 20층 건물 폐색 구도를
        // 실제 b5/HLSL 및 Scene Depth 경로로 그린다.
        const struct CameraPreset
        {
            float yaw;
            float pitch;
            float distance;
            DirectX::XMFLOAT3 target;
        } cameras[] = {
            { 0.0f, -1.560796f, 2000.0f, { 0.0f, 2000.0f, 0.0f } },
            { 0.0f, 0.0f, 1000.0f, { 0.0f, 4500.0f, -1000.0f } },
            { 0.0f, 1.560796f, 4000.0f, { 0.0f, 4500.0f, 0.0f } },
            { 0.0f, -0.32f, 85.0f, { 0.0f, 28.5f, 0.0f } },
        };
        const CloudDebugMode modes[] = {
            CloudDebugMode::CloudHitMask,
            CloudDebugMode::Transmittance,
            CloudDebugMode::CloudSegmentLength,
            CloudDebugMode::ActualViewStepLength,
        };
        std::uint64_t firstHash = 0;
        bool foundDifferentHash = false;
        for (const CameraPreset& preset : cameras)
        {
            camera.SetOrbit(preset.yaw, preset.pitch,
                            preset.distance, preset.target);
            for (CloudDebugMode mode : modes)
            {
                renderer.SetDebugMode(mode);
                renderer.Render(camera, 0.0f);
                const std::uint64_t hash = renderer.LastCloudFrameHash();
                if (hash == 0)
                    return 3;
                if (firstHash == 0)
                    firstHash = hash;
                else if (hash != firstHash)
                    foundDifferentHash = true;
            }
        }
        if (!foundDifferentHash ||
            renderer.DomainType() != CloudDomainType::PlanarLayer ||
            sizeof(CloudDomainParameters) != 32u)
            return 4;

        // 13-2의 네 실제 런타임 프리셋을 같은 정규화 카메라에서 그린다.
        // CPU 기준값뿐 아니라 Renderer가 b1/b3/b5에 넣는 값과 HLSL 실행까지 확인한다.
        for (float scale : { 1.0f, 10.0f, 100.0f, 1000.0f })
        {
            if (!renderer.ApplyStage13SimilarityScale(scale))
                return 5;
            renderer.SetCloudDomainType(CloudDomainType::PlanarLayer);
            camera.SetOrbit(0.55f, 0.30f, 12.0f * scale,
                            { 40.0f * scale, 0.5f * scale, 0.0f });
            renderer.SetDebugMode(CloudDebugMode::FinalDensity);
            renderer.Render(camera, 0.0f);
            if (renderer.LastCloudFrameHash() == 0)
                return 6;

            const CloudParameters& cloud = renderer.CloudSettings();
            const CloudDomainParameters& domain = renderer.DomainSettings();
            const LightParameters& light = renderer.LightSettings();
            const auto nearValue = [](float a, float b)
            {
                return std::abs(a - b) <=
                    std::max(1e-6f, std::abs(b) * 1e-5f);
            };
            if (!nearValue(cloud.stepSize, 0.1f * scale) ||
                !nearValue(cloud.baseNoiseScale, 0.35f / scale) ||
                !nearValue(cloud.detailNoiseScale, 2.5f / scale) ||
                !nearValue(cloud.extinctionCoefficient, 1.0f / scale) ||
                !nearValue(cloud.weatherMapWorldSize, 16.0f * scale) ||
                !nearValue(domain.cloudBottomAltitude, -1.0f * scale) ||
                !nearValue(domain.cloudLayerThickness, 3.0f * scale) ||
                !nearValue(light.lightStepSize, 0.25f * scale) ||
                !nearValue(light.singleScatteringAlbedo, 1.0f))
                return 7;
        }
        return renderer.HasDebugLayerErrors() ? 2 : 0;
    }

    if (performanceOverlaySmokeTest)
    {
        // UI 창을 숨긴 상태에서도 오버레이와 timestamp query가 계속 동작해야 한다.
        // 작은 숨김 타깃에서 GPU 완료를 비동기로 기다리되, 어떤 프레임에서도
        // GetData가 GPU flush나 CPU 대기를 유발하지 않도록 profiler가 DONOTFLUSH만 쓴다.
        renderer.SetNoiseLabVisible(false);
        renderer.EnableNoiseLabPreviews(false);
        renderer.SetVSyncEnabled(false);
        renderer.SetViewSamplingForSmoke(1u, 32.0f);
        renderer.SetLightSampling(1u, 32.0f);

        bool receivedGpuTiming = false;
        for (int frame = 0; frame < 180; ++frame)
        {
            renderer.Render(camera, static_cast<float>(frame) / 60.0f);
            const FrameTimingSnapshot timing = renderer.TimingSnapshot();
            if (timing.gpuValid)
            {
                receivedGpuTiming = true;
                if (!std::isfinite(timing.gpuFrameMs) ||
                    !std::isfinite(timing.gpuCloudMs) ||
                    timing.gpuFrameMs < 0.0 || timing.gpuCloudMs < 0.0 ||
                    timing.gpuCloudMs > timing.gpuFrameMs)
                    return 3;
                break;
            }
            Sleep(1);
        }
        if (!receivedGpuTiming)
            return 4;

        const FrameTimingSnapshot timing = renderer.TimingSnapshot();
        if (!timing.cpuValid || timing.frameIndex == 0 ||
            !std::isfinite(timing.cpuFrameMs) ||
            !std::isfinite(timing.fps) || timing.cpuFrameMs <= 0.0 ||
            timing.fps <= 0.0)
            return 5;

        renderer.SetVSyncEnabled(true);
        renderer.Render(camera, 0.0f);
        if (!renderer.VSyncEnabled())
            return 6;
        renderer.SetVSyncEnabled(false);
        renderer.Render(camera, 0.0f);
        if (renderer.VSyncEnabled())
            return 7;

        return renderer.HasDebugLayerErrors() ? 2 : 0;
    }

    if (noiseLabSmokeTest)
    {
        for (int mode = static_cast<int>(NoiseOutputMode::RawNoise);
             mode <= static_cast<int>(NoiseOutputMode::WeatherUv); ++mode)
        {
            renderer.SetNoiseLabOutputMode(static_cast<NoiseOutputMode>(mode));
            renderer.Render(camera, 0.0f);
            if (!renderer.ValidateNoiseLabPreviews())
                return 30 + mode;
        }
        wchar_t temporaryPath[MAX_PATH] = {};
        if (GetTempPathW(MAX_PATH, temporaryPath) == 0 ||
            !renderer.ExportNoiseLabSnapshot(
                std::filesystem::path(temporaryPath) / L"VolumetricCloudNoiseLabSmoke"))
            return 4;
        return renderer.HasDebugLayerErrors() ? 2 : 0;
    }

    if (shaderHotReloadSmokeTest)
    {
        renderer.EnableFrameHashCapture(true);
        renderer.Render(camera, 0.0f);
        const std::uint64_t baselineGeneration = renderer.ShaderGeneration();
        const std::uint64_t baselineLabHash = renderer.NoiseLabPreviewHash(0);
        const std::uint64_t baselineCloudHash = renderer.LastCloudFrameHash();

        const std::filesystem::path noisePath =
            hotReloadShaderDirectory / L"Noise.hlsli";
        std::string original;
        if (!ReadTextFile(noisePath, original))
            return 6;
        std::string biased = original;
        const std::string from = "#define VCLOUD_NOISE_TEST_BIAS 0.0";
        const size_t biasPosition = biased.find(from);
        if (biasPosition == std::string::npos)
            return 7;
        biased.replace(biasPosition, from.size(),
                       "#define VCLOUD_NOISE_TEST_BIAS 0.25");
        if (!WriteTextFile(noisePath, biased))
            return 8;
        renderer.Render(camera, 0.0f);
        const std::uint64_t biasedGeneration = renderer.ShaderGeneration();
        const std::uint64_t biasedLabHash = renderer.NoiseLabPreviewHash(0);
        const std::uint64_t biasedCloudHash = renderer.LastCloudFrameHash();
        if (biasedGeneration != baselineGeneration + 1 ||
            biasedLabHash == baselineLabHash || biasedCloudHash == baselineCloudHash)
            return 9;

        if (!WriteTextFile(noisePath, biased + "\n#error expected hot reload failure\n"))
            return 10;
        renderer.Render(camera, 0.0f);
        if (renderer.ShaderGeneration() != biasedGeneration ||
            renderer.NoiseLabPreviewHash(0) != biasedLabHash ||
            renderer.LastCloudFrameHash() != biasedCloudHash)
            return 11;

        if (!WriteTextFile(noisePath, original))
            return 12;
        renderer.Render(camera, 0.0f);
        if (renderer.ShaderGeneration() != biasedGeneration + 1 ||
            renderer.NoiseLabPreviewHash(0) != baselineLabHash ||
            renderer.LastCloudFrameHash() != baselineCloudHash)
            return 13;
        return renderer.HasDebugLayerErrors() ? 2 : 0;
    }

    // ---- 고해상도 타이머 준비 ----
    LARGE_INTEGER freq, start, previous;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    previous = start;

    // ---- 메인 루프 ----
    while (window.ProcessMessages())
    {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        float elapsed = static_cast<float>(now.QuadPart - start.QuadPart) / freq.QuadPart;
        const float deltaSeconds = static_cast<float>(
            now.QuadPart - previous.QuadPart) / freq.QuadPart;
        previous = now;

        window.UpdateCameraMovement(deltaSeconds);
        updateOutputMetrics();

        // 창이 리사이즈되면 종횡비를 갱신
        if (window.GetHeight() > 0)
            camera.SetAspect(static_cast<float>(window.GetWidth()) / window.GetHeight());

        renderer.Render(camera, elapsed);

        // Capture가 요청한 Native 전환은 draw/present가 끝난 프레임 경계에서만
        // 실행한다. WM_SIZE가 동기적으로 Renderer::Resize를 끝낸 뒤 실제 물리
        // client를 다시 읽고 성공 여부를 Capture 상태기에 전달한다.
        bool enableNative1080p = false;
        if (renderer.ConsumeNative1080pRequest(enableNative1080p))
        {
            bool succeeded = false;
            std::string nativeStatus;
            if (enableNative1080p)
            {
                const Native1080pResult result = window.EnterNative1080p();
                succeeded = Native1080pSucceeded(result);
                nativeStatus = Native1080pResultText(result);
            }
            else
            {
                succeeded = window.RestoreWindowed();
                nativeStatus = succeeded
                    ? "Windowed output restored"
                    : "Windowed output restore failed";
            }
            updateOutputMetrics();
            const bool resourcesReady = outputCoreMatchesWindow();
            succeeded = succeeded && resourcesReady;
            if (!resourcesReady)
                nativeStatus += "; render resource resize failed";
            renderer.NotifyNative1080pResult(
                enableNative1080p, succeeded, window.IsNative1080p(),
                nativeStatus);
            if (window.GetHeight() > 0)
                camera.SetAspect(static_cast<float>(window.GetWidth()) /
                                 window.GetHeight());
        }
        // Noise Lab에서 프리셋/파라미터를 바꾼 경우에도 다음 프레임까지 기다리지
        // 않고 창 제목이 Renderer의 현재 상태를 정확히 표시하게 한다.
        window.RefreshDebugTitle();
    }

    return 0;
}
