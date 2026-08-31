// ============================================================================
//  FrameProfiler.h - High 단일 파이프라인 GPU/CPU 계측
// ============================================================================
#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>

struct FrameTimingSnapshot
{
    std::uint64_t frameIndex = 0;
    std::uint64_t gpuSampleIndex = 0;
    double fps = 0.0;
    double cpuFrameMs = 0.0;
    double gpuFrameMs = 0.0;
    double gpuAtmosphereLutMs = 0.0;
    double gpuShadowCacheMs = 0.0;
    double gpuOpaqueSceneMs = 0.0;
    double gpuCloudMs = 0.0;
    double gpuToneMapMs = 0.0;
    double rawCpuFrameMs = 0.0;
    double rawGpuFrameMs = 0.0;
    double rawGpuAtmosphereLutMs = 0.0;
    double rawGpuShadowCacheMs = 0.0;
    double rawGpuOpaqueSceneMs = 0.0;
    double rawGpuCloudMs = 0.0;
    double rawGpuToneMapMs = 0.0;
    bool cpuValid = false;
    bool gpuValid = false;
};

class FrameTimingAccumulator
{
public:
    static constexpr double kEmaAlpha = 0.1;

    void Reset();
    void AdvanceFrame();
    void RecordCpuMilliseconds(double milliseconds);
    void RecordGpuMilliseconds(
        double frameMilliseconds,
        double atmosphereMilliseconds,
        double shadowMilliseconds,
        double opaqueMilliseconds,
        double cloudMilliseconds,
        double toneMilliseconds);
    const FrameTimingSnapshot& Snapshot() const { return m_snapshot; }

private:
    FrameTimingSnapshot m_snapshot;
};

class FrameProfiler
{
public:
    static constexpr std::size_t kQueryRingSize = 8;

    bool Init(ID3D11Device* device);
    void ResetMeasurements();
    void BeginCpuFrame();
    void EndCpuFrame();
    void BeginGpuFrame(ID3D11DeviceContext* context);
    void MarkAtmosphereLutEnd(ID3D11DeviceContext* context);
    void MarkShadowCacheEnd(ID3D11DeviceContext* context);
    void MarkOpaqueSceneEnd(ID3D11DeviceContext* context);
    void MarkCloudEnd(ID3D11DeviceContext* context);
    void MarkToneMapEnd(ID3D11DeviceContext* context);
    void EndGpuFrame(ID3D11DeviceContext* context);

    const FrameTimingSnapshot& Snapshot() const
    {
        return m_accumulator.Snapshot();
    }
    bool IsGpuSupported() const { return m_initialized; }

private:
    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    struct QuerySlot
    {
        ComPtr<ID3D11Query> disjoint;
        ComPtr<ID3D11Query> frameStart;
        ComPtr<ID3D11Query> atmosphereEnd;
        ComPtr<ID3D11Query> shadowEnd;
        ComPtr<ID3D11Query> opaqueEnd;
        ComPtr<ID3D11Query> cloudEnd;
        ComPtr<ID3D11Query> toneEnd;
        ComPtr<ID3D11Query> frameEnd;
        bool inFlight = false;
        std::uint64_t generation = 0;
    };

    bool CreateSlot(ID3D11Device* device, QuerySlot& slot);
    void ResolveCompleted(ID3D11DeviceContext* context);

    std::array<QuerySlot, kQueryRingSize> m_slots;
    FrameTimingAccumulator m_accumulator;
    std::chrono::steady_clock::time_point m_cpuStart;
    std::size_t m_nextSlot = 0;
    int m_activeSlot = -1;
    std::uint64_t m_generation = 1;
    bool m_initialized = false;
    bool m_cpuFrameActive = false;
};
