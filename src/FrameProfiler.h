// ============================================================================
//  FrameProfiler.h - 비동기 D3D11 GPU timestamp와 CPU frame 시간 계측
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
    double fps = 0.0;
    double cpuFrameMs = 0.0;
    double gpuFrameMs = 0.0;
    double gpuCloudMs = 0.0;
    double gpuCloudRaymarchMs = 0.0;
    double gpuUpsampleCompositeMs = 0.0;
    double rawGpuFrameMs = 0.0;
    double rawGpuCloudMs = 0.0;
    double rawGpuCloudRaymarchMs = 0.0;
    double rawGpuUpsampleCompositeMs = 0.0;
    std::uint64_t gpuSampleIndex = 0;
    bool cpuValid = false;
    bool gpuValid = false;
};

// GPU와 무관한 EMA 계산을 별도 클래스로 두어 CPU 단위 테스트에서 검증한다.
class FrameTimingAccumulator
{
public:
    static constexpr double kEmaAlpha = 0.1;

    void Reset();
    void AdvanceFrame();
    void RecordCpuMilliseconds(double milliseconds);
    void RecordGpuMilliseconds(double frameMilliseconds,
                               double cloudMilliseconds,
                               double cloudRaymarchMilliseconds = -1.0);
    const FrameTimingSnapshot& Snapshot() const { return m_snapshot; }

private:
    FrameTimingSnapshot m_snapshot;
    // 오버레이는 EMA를 표시하고 자동 성능 gate는 snapshot의 원본 표본을 읽는다.
    double m_lastRawCpuFrameMs = 0.0;
    double m_lastRawGpuFrameMs = 0.0;
    double m_lastRawGpuCloudMs = 0.0;
};

class FrameProfiler
{
public:
    static constexpr std::size_t kQueryRingSize = 8;

    bool Init(ID3D11Device* device);
    void ResetMeasurements();

    // CPU frame은 Render 시작부터 Present 반환까지 측정한다.
    void BeginCpuFrame();
    void EndCpuFrame();

    // GPU frame은 진단 장면 직전부터 ImGui draw 직후까지이며 Present는 제외한다.
    // timestamp query는 End로 기록하고 GetData에는 DONOTFLUSH만 사용한다.
    void BeginGpuFrame(ID3D11DeviceContext* context);
    void BeginCloudPass(ID3D11DeviceContext* context);
    void MarkCloudRaymarchEnd(ID3D11DeviceContext* context);
    void EndCloudPass(ID3D11DeviceContext* context);
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
        ComPtr<ID3D11Query> cloudStart;
        ComPtr<ID3D11Query> cloudRaymarchEnd;
        ComPtr<ID3D11Query> cloudEnd;
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
