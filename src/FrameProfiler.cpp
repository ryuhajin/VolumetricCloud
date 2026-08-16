#include "FrameProfiler.h"

#include <algorithm>

void FrameTimingAccumulator::Reset()
{
    m_snapshot = {};
    m_lastRawCpuFrameMs = 0.0;
    m_lastRawGpuFrameMs = 0.0;
    m_lastRawGpuCloudMs = 0.0;
}

void FrameTimingAccumulator::AdvanceFrame()
{
    ++m_snapshot.frameIndex;
}

void FrameTimingAccumulator::RecordCpuMilliseconds(double milliseconds)
{
    if (!std::isfinite(milliseconds) || milliseconds <= 0.0)
        return;
    m_lastRawCpuFrameMs = milliseconds;
    m_snapshot.cpuFrameMs = m_snapshot.cpuValid
        ? m_snapshot.cpuFrameMs +
              kEmaAlpha * (milliseconds - m_snapshot.cpuFrameMs)
        : milliseconds;
    m_snapshot.cpuValid = true;
    m_snapshot.fps = 1000.0 / m_snapshot.cpuFrameMs;
}

void FrameTimingAccumulator::RecordGpuMilliseconds(double frameMilliseconds,
                                                    double cloudMilliseconds)
{
    if (!std::isfinite(frameMilliseconds) ||
        !std::isfinite(cloudMilliseconds) ||
        frameMilliseconds <= 0.0 || cloudMilliseconds < 0.0 ||
        cloudMilliseconds > frameMilliseconds)
        return;
    m_lastRawGpuFrameMs = frameMilliseconds;
    m_lastRawGpuCloudMs = cloudMilliseconds;
    if (m_snapshot.gpuValid)
    {
        m_snapshot.gpuFrameMs += kEmaAlpha *
            (frameMilliseconds - m_snapshot.gpuFrameMs);
        m_snapshot.gpuCloudMs += kEmaAlpha *
            (cloudMilliseconds - m_snapshot.gpuCloudMs);
    }
    else
    {
        m_snapshot.gpuFrameMs = frameMilliseconds;
        m_snapshot.gpuCloudMs = cloudMilliseconds;
    }
    m_snapshot.gpuValid = true;
}

bool FrameProfiler::CreateSlot(ID3D11Device* device, QuerySlot& slot)
{
    D3D11_QUERY_DESC desc = {};
    desc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
    if (FAILED(device->CreateQuery(&desc, &slot.disjoint)))
        return false;

    desc.Query = D3D11_QUERY_TIMESTAMP;
    return SUCCEEDED(device->CreateQuery(&desc, &slot.frameStart)) &&
           SUCCEEDED(device->CreateQuery(&desc, &slot.cloudStart)) &&
           SUCCEEDED(device->CreateQuery(&desc, &slot.cloudEnd)) &&
           SUCCEEDED(device->CreateQuery(&desc, &slot.frameEnd));
}

bool FrameProfiler::Init(ID3D11Device* device)
{
    if (!device)
        return false;
    for (QuerySlot& slot : m_slots)
    {
        if (!CreateSlot(device, slot))
        {
            m_slots = {};
            m_initialized = false;
            return false;
        }
    }
    m_initialized = true;
    ResetMeasurements();
    return true;
}

void FrameProfiler::ResetMeasurements()
{
    m_accumulator.Reset();
    ++m_generation;
    m_activeSlot = -1;
    m_cpuFrameActive = false;
}

void FrameProfiler::BeginCpuFrame()
{
    m_accumulator.AdvanceFrame();
    m_cpuStart = std::chrono::steady_clock::now();
    m_cpuFrameActive = true;
}

void FrameProfiler::EndCpuFrame()
{
    if (!m_cpuFrameActive)
        return;
    const auto end = std::chrono::steady_clock::now();
    const double milliseconds =
        std::chrono::duration<double, std::milli>(end - m_cpuStart).count();
    m_accumulator.RecordCpuMilliseconds(milliseconds);
    m_cpuFrameActive = false;
}

void FrameProfiler::ResolveCompleted(ID3D11DeviceContext* context)
{
    if (!m_initialized || !context)
        return;
    for (QuerySlot& slot : m_slots)
    {
        if (!slot.inFlight)
            continue;
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint = {};
        if (context->GetData(slot.disjoint.Get(), &disjoint, sizeof(disjoint),
                             D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
            continue;

        UINT64 frameStart = 0;
        UINT64 cloudStart = 0;
        UINT64 cloudEnd = 0;
        UINT64 frameEnd = 0;
        const bool timestampsReady =
            context->GetData(slot.frameStart.Get(), &frameStart, sizeof(frameStart),
                             D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
            context->GetData(slot.cloudStart.Get(), &cloudStart, sizeof(cloudStart),
                             D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
            context->GetData(slot.cloudEnd.Get(), &cloudEnd, sizeof(cloudEnd),
                             D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
            context->GetData(slot.frameEnd.Get(), &frameEnd, sizeof(frameEnd),
                             D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
        if (!timestampsReady)
            continue;

        slot.inFlight = false;
        if (slot.generation != m_generation || disjoint.Disjoint ||
            disjoint.Frequency == 0 || frameEnd < frameStart ||
            cloudEnd < cloudStart || cloudStart < frameStart ||
            cloudEnd > frameEnd)
            continue;

        const double millisecondsPerTick =
            1000.0 / static_cast<double>(disjoint.Frequency);
        m_accumulator.RecordGpuMilliseconds(
            static_cast<double>(frameEnd - frameStart) * millisecondsPerTick,
            static_cast<double>(cloudEnd - cloudStart) * millisecondsPerTick);
    }
}

void FrameProfiler::BeginGpuFrame(ID3D11DeviceContext* context)
{
    m_activeSlot = -1;
    if (!m_initialized || !context)
        return;
    ResolveCompleted(context);

    for (std::size_t offset = 0; offset < m_slots.size(); ++offset)
    {
        const std::size_t index = (m_nextSlot + offset) % m_slots.size();
        if (m_slots[index].inFlight)
            continue;
        QuerySlot& slot = m_slots[index];
        slot.generation = m_generation;
        context->Begin(slot.disjoint.Get());
        context->End(slot.frameStart.Get());
        m_activeSlot = static_cast<int>(index);
        m_nextSlot = (index + 1) % m_slots.size();
        return;
    }
    // GPU가 8개 slot보다 늦으면 이 프레임은 계측하지 않고 렌더를 계속한다.
}

void FrameProfiler::BeginCloudPass(ID3D11DeviceContext* context)
{
    if (m_activeSlot >= 0 && context)
        context->End(m_slots[static_cast<std::size_t>(m_activeSlot)].cloudStart.Get());
}

void FrameProfiler::EndCloudPass(ID3D11DeviceContext* context)
{
    if (m_activeSlot >= 0 && context)
        context->End(m_slots[static_cast<std::size_t>(m_activeSlot)].cloudEnd.Get());
}

void FrameProfiler::EndGpuFrame(ID3D11DeviceContext* context)
{
    if (m_activeSlot < 0 || !context)
        return;
    QuerySlot& slot = m_slots[static_cast<std::size_t>(m_activeSlot)];
    context->End(slot.frameEnd.Get());
    context->End(slot.disjoint.Get());
    slot.inFlight = true;
    m_activeSlot = -1;
}
