#include "FrameProfiler.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
}

int main()
{
    FrameTimingAccumulator accumulator;
    accumulator.AdvanceFrame();
    accumulator.RecordCpuMilliseconds(10.0);
    accumulator.RecordGpuMilliseconds(9.0, 0.5, 1.0, 0.5, 6.0, 1.0);
    const FrameTimingSnapshot first = accumulator.Snapshot();
    Require(first.frameIndex == 1u && first.cpuValid && first.gpuValid,
            "first timing sample");
    Require(first.gpuAtmosphereLutMs == 0.5 &&
            first.gpuShadowCacheMs == 1.0 &&
            first.gpuOpaqueSceneMs == 0.5 &&
            first.gpuCloudMs == 6.0 && first.gpuToneMapMs == 1.0,
            "six High pipeline timing categories");

    accumulator.RecordGpuMilliseconds(10.0, 1.0, 1.0, 1.0, 6.0, 1.0);
    const FrameTimingSnapshot second = accumulator.Snapshot();
    Require(std::abs(second.gpuFrameMs - 9.1) < 1.0e-9 &&
            std::abs(second.gpuAtmosphereLutMs - 0.55) < 1.0e-9 &&
            second.rawGpuCloudMs == 6.0,
            "EMA and raw timing sample");

    const std::uint64_t sampleIndex = second.gpuSampleIndex;
    accumulator.RecordGpuMilliseconds(1.0, 1.0, 1.0, 1.0, 1.0, 1.0);
    Require(accumulator.Snapshot().gpuSampleIndex == sampleIndex,
            "invalid interval sum rejected");
    std::cout << "FrameProfilerMath passed\n";
    return 0;
}
