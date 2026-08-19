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
            metadata.find("\"schemaVersion\": 31") != std::string::npos &&
            metadata.find("\"implementationStage\": \"9\"") != std::string::npos &&
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
        "[UNIFIED-SCENE][GPU] CAMERAS=4 DIGITS=10 APPEARANCES=3 F5F6=FINITE_DISTINCT PIPELINE_INVARIANT=1 SCHEMA=30 PASS");
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
    const bool requestedSmallGpuSmoke = requestedStage6Smoke || requestedStage7Smoke ||
        requestedStage8Smoke ||
        requestedPerformanceOverlaySmoke || requestedStage13DomainSmoke ||
        requestedStage13OpenWorldSmoke || requestedStage13NoiseVolumeSmoke ||
        requestedStage13WeatherShapeGpu || requestedStage13UnifiedSceneSmoke ||
        requestedStage13OpticsLightingSmoke || requestedStage9OptimizationSmoke;
    const int kWidth  = (requestedStage13LightingPerformance ||
        requestedStage9Performance) ? 1920 :
        (requestedStage13SimilarityGpu ||
                         requestedStage13WeatherShapeGpu ||
                         requestedStage13UnifiedSceneSmoke) ? 320 :
        (requestedSmallGpuSmoke ? 96 : 1280);
    const int kHeight = requestedStage9Performance ? 1080 :
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
    const bool noiseLabSmokeTest = commandLine &&
        wcsstr(commandLine, L"--noise-lab-smoke-test") != nullptr;
    const bool shaderHotReloadSmokeTest = commandLine &&
        wcsstr(commandLine, L"--shader-hot-reload-smoke-test") != nullptr;
    const bool automatedTestRun = smokeTest || stage1SmokeTest || stage2SmokeTest ||
        stage3SmokeTest || stage4SmokeTest || stage5SmokeTest || stage6SmokeTest ||
        stage7SmokeTest || stage8SmokeTest || performanceOverlaySmokeTest ||
        stage13DomainSmokeTest || stage13SimilarityGpuTest ||
        stage13OpenWorldSmokeTest || stage13NoiseVolumeSmokeTest ||
        stage13WeatherShapeGpuTest || stage13UnifiedSceneSmokeTest ||
        stage13OpticsLightingSmokeTest || stage13LightingPerformanceTest ||
        stage9OptimizationSmokeTest || stage9PerformanceTest ||
        noiseLabSmokeTest || shaderHotReloadSmokeTest;
    const bool enableNoiseVolumes = !automatedTestRun ||
        stage13OpenWorldSmokeTest || stage13NoiseVolumeSmokeTest ||
        stage13WeatherShapeGpuTest || stage13UnifiedSceneSmokeTest ||
        stage13OpticsLightingSmokeTest || stage13LightingPerformanceTest ||
        stage9OptimizationSmokeTest || stage9PerformanceTest;

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
                    L"VolumetricCloud - Stage 13-5 | Portfolio Lighting 초기화 중",
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
                    !noiseLabSmokeTest && !shaderHotReloadSmokeTest);
    Camera   camera;
    Renderer renderer;

    camera.SetAspect(static_cast<float>(kWidth) / kHeight);

    if (!renderer.Init(window.GetHandle(), kWidth, kHeight, enableNoiseVolumes))
        return -1; // 초기화 실패 (오류 메시지는 Renderer가 표시)

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
        !noiseLabSmokeTest &&
        !shaderHotReloadSmokeTest;
    if (interactiveRun)
    {
        if (!renderer.ApplyStage13OpenWorldPreset() ||
            !renderer.ApplyCloudAppearancePreset(CloudAppearancePreset::Stratus))
            return -2;
        window.ApplyInitialPortfolioCamera();
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
                metadata.find("\"schemaVersion\": 31") != std::string::npos &&
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
                metadata.find("\"schemaVersion\": 31") != std::string::npos &&
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
                metadata.find("\"schemaVersion\": 31") != std::string::npos &&
                metadata.find("\"developerUiLayout\": \"F1Noise_F2Weather_F3Lighting_F4Camera\"") != std::string::npos &&
                metadata.find("\"camera\": {") != std::string::npos &&
                metadata.find("\"verticalFovDegrees\": 60") != std::string::npos &&
                metadata.find("\"savedPosition\": null") != std::string::npos &&
                metadata.find("\"physicalAdvectionMode\": \"legacyIndependentSpeeds\"") != std::string::npos &&
                metadata.find("\"singleScatteringAlbedo\"") != std::string::npos &&
                metadata.find("\"scatteringCoefficient\"") == std::string::npos &&
                metadata.find("\"implementationStage\": \"9\"") != std::string::npos &&
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

        // 창이 리사이즈되면 종횡비를 갱신
        if (window.GetHeight() > 0)
            camera.SetAspect(static_cast<float>(window.GetWidth()) / window.GetHeight());

        renderer.Render(camera, elapsed);
        // Noise Lab에서 프리셋/파라미터를 바꾼 경우에도 다음 프레임까지 기다리지
        // 않고 창 제목이 Renderer의 현재 상태를 정확히 표시하게 한다.
        window.RefreshDebugTitle();
    }

    return 0;
}
