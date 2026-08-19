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
    accumulator.RecordGpuMilliseconds(8.0, 5.0);
    accumulator.RecordGpuMilliseconds(6.0, 3.0);
    Require(std::abs(accumulator.Snapshot().gpuFrameMs - 7.8) < 1e-9 &&
            std::abs(accumulator.Snapshot().gpuCloudMs - 4.8) < 1e-9,
            "GPU times must use alpha 0.1 EMA");
    Require(std::abs(accumulator.Snapshot().rawGpuFrameMs - 6.0) < 1e-9 &&
                std::abs(accumulator.Snapshot().rawGpuCloudMs - 3.0) < 1e-9 &&
                accumulator.Snapshot().gpuSampleIndex == 2u,
            "GPU snapshot must expose unique raw samples for percentile gates");

    const FrameTimingSnapshot valid = accumulator.Snapshot();
    accumulator.RecordCpuMilliseconds(0.0);
    accumulator.RecordCpuMilliseconds(-1.0);
    accumulator.RecordCpuMilliseconds(std::numeric_limits<double>::quiet_NaN());
    accumulator.RecordGpuMilliseconds(1.0, 2.0);
    accumulator.RecordGpuMilliseconds(
        std::numeric_limits<double>::infinity(), 0.0);
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
