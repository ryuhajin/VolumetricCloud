#include "FrameProfiler.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FrameProfilerMath failure: " << message << '\n';
        std::exit(1);
    }
}
}

int main()
{
    FrameTimingAccumulator accumulator;
    accumulator.AdvanceFrame();
    accumulator.RecordCpuMilliseconds(20.0);
    Require(accumulator.Snapshot().frameIndex == 1,
            "frame index must advance");
    Require(std::abs(accumulator.Snapshot().fps - 50.0) < 1e-9,
            "FPS must be the reciprocal of CPU frame milliseconds");

    accumulator.RecordCpuMilliseconds(10.0);
    Require(std::abs(accumulator.Snapshot().cpuFrameMs - 19.0) < 1e-9,
            "CPU time must use alpha 0.1 EMA");
    Require(std::abs(accumulator.Snapshot().rawCpuFrameMs - 10.0) < 1e-9,
            "CPU snapshot must expose the latest raw sample for percentile gates");
    accumulator.RecordGpuMilliseconds(8.0, 5.0, 3.0, 1.0);
    accumulator.RecordGpuMilliseconds(6.0, 3.0, 1.5, 0.5);
    Require(std::abs(accumulator.Snapshot().gpuFrameMs - 7.8) < 1e-9 &&
            std::abs(accumulator.Snapshot().gpuCloudMs - 4.8) < 1e-9,
            "GPU times must use alpha 0.1 EMA");
    Require(std::abs(accumulator.Snapshot().gpuShadowCacheMs - 0.95) < 1e-9 &&
            std::abs(accumulator.Snapshot().gpuCloudRaymarchMs - 2.85) < 1e-9 &&
            std::abs(accumulator.Snapshot().gpuUpsampleCompositeMs - 1.0) < 1e-9 &&
            std::abs(accumulator.Snapshot().gpuCloudCompositeMs) < 1e-9,
            "GPU cloud split must preserve cache, raymarch and resolve timings");
    Require(std::abs(accumulator.Snapshot().rawGpuFrameMs - 6.0) < 1e-9 &&
                std::abs(accumulator.Snapshot().rawGpuCloudMs - 3.0) < 1e-9 &&
                std::abs(accumulator.Snapshot().rawGpuShadowCacheMs - 0.5) < 1e-9 &&
                std::abs(accumulator.Snapshot().rawGpuCloudRaymarchMs - 1.5) < 1e-9 &&
                std::abs(accumulator.Snapshot().rawGpuUpsampleCompositeMs - 1.0) < 1e-9 &&
                std::abs(accumulator.Snapshot().rawGpuCloudCompositeMs) < 1e-9 &&
                accumulator.Snapshot().gpuSampleIndex == 2u,
            "GPU snapshot must expose unique raw samples for percentile gates");

    accumulator.Reset();
    accumulator.RecordStage14GpuMilliseconds(
        12.0, 1.0, 0.5, 0.4, 6.0, 1.5, 0.7, 0.6);
    accumulator.RecordStage14GpuMilliseconds(
        10.0, 0.8, 0.4, 0.3, 5.0, 1.0, 0.4, 0.5);
    Require(std::abs(accumulator.Snapshot().gpuAtmosphereLutMs - 0.98) < 1e-9 &&
            std::abs(accumulator.Snapshot().gpuCloudMs - 8.51) < 1e-9 &&
            std::abs(accumulator.Snapshot().gpuShadowCacheMs - 0.49) < 1e-9 &&
            std::abs(accumulator.Snapshot().gpuOpaqueSceneMs - 0.39) < 1e-9 &&
            std::abs(accumulator.Snapshot().gpuCloudRaymarchMs - 5.9) < 1e-9 &&
            std::abs(accumulator.Snapshot().gpuUpsampleCompositeMs - 1.45) < 1e-9 &&
            std::abs(accumulator.Snapshot().gpuCloudCompositeMs - 0.67) < 1e-9 &&
            std::abs(accumulator.Snapshot().gpuToneMapMs - 0.59) < 1e-9,
            "Stage 14 GPU split must preserve every pass EMA");
    Require(std::abs(accumulator.Snapshot().rawGpuCloudMs - 6.8) < 1e-9 &&
            std::abs(accumulator.Snapshot().rawGpuCloudCompositeMs - 0.4) < 1e-9,
            "Stage 14 GPU Cloud Total must include shadow, raymarch, resolve and composite");

    FrameTimingAccumulator directCompositeAccumulator;
    directCompositeAccumulator.RecordStage14GpuMilliseconds(
        5.0, 0.2, 0.1, 0.1, 2.0, 1.0, 0.0, 0.2);
    Require(std::abs(directCompositeAccumulator.Snapshot().
                         rawGpuCloudCompositeMs) < 1e-9 &&
            std::abs(directCompositeAccumulator.Snapshot().
                         gpuCloudCompositeMs) < 1e-9 &&
            std::abs(directCompositeAccumulator.Snapshot().
                         rawGpuCloudMs - 3.1) < 1e-9,
            "a direct path must record resolve while keeping composite near zero");

    const FrameTimingSnapshot valid = accumulator.Snapshot();
    accumulator.RecordCpuMilliseconds(0.0);
    accumulator.RecordCpuMilliseconds(-1.0);
    accumulator.RecordCpuMilliseconds(std::numeric_limits<double>::quiet_NaN());
    accumulator.RecordGpuMilliseconds(1.0, 2.0);
    accumulator.RecordGpuMilliseconds(
        std::numeric_limits<double>::infinity(), 0.0);
    accumulator.RecordStage14GpuMilliseconds(
        10.0, 0.8, 0.4, 0.3, 5.0, 1.0,
        std::numeric_limits<double>::quiet_NaN(), 0.5);
    accumulator.RecordStage14GpuMilliseconds(
        1.0, 0.2, 0.2, 0.2, 0.2, 0.2, 0.2, 0.2);
    Require(accumulator.Snapshot().cpuFrameMs == valid.cpuFrameMs &&
            accumulator.Snapshot().gpuFrameMs == valid.gpuFrameMs,
            "invalid samples must not alter the last valid values");

    accumulator.Reset();
    Require(accumulator.Snapshot().frameIndex == 0 &&
            !accumulator.Snapshot().cpuValid &&
            !accumulator.Snapshot().gpuValid,
            "reset must clear counters and validity");

    std::cout << "FrameProfilerMath passed\n";
    return 0;
}
