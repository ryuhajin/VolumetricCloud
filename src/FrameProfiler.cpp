#include "FrameProfiler.h"

#include <algorithm>

namespace
{
double Ema(double current, double sample)
{
    return current + FrameTimingAccumulator::kEmaAlpha * (sample - current);
}
}

void FrameTimingAccumulator::Reset()
{
    m_snapshot = {};
}

void FrameTimingAccumulator::AdvanceFrame()
{
    ++m_snapshot.frameIndex;
}

void FrameTimingAccumulator::RecordCpuMilliseconds(double milliseconds)
{
    if (!std::isfinite(milliseconds) || milliseconds <= 0.0)
        return;
    m_snapshot.rawCpuFrameMs = milliseconds;
    m_snapshot.cpuFrameMs = m_snapshot.cpuValid
        ? Ema(m_snapshot.cpuFrameMs, milliseconds) : milliseconds;
    m_snapshot.cpuValid = true;
    m_snapshot.fps = 1000.0 / m_snapshot.cpuFrameMs;
}

void FrameTimingAccumulator::RecordGpuMilliseconds(
    double frameMilliseconds,
    double weatherMilliseconds,
    double atmosphereMilliseconds,
    double shadowMilliseconds,
    double opaqueMilliseconds,
    double cloudMilliseconds,
    double toneMilliseconds)
{
    const double values[] = {
        frameMilliseconds, weatherMilliseconds, atmosphereMilliseconds, shadowMilliseconds,
        opaqueMilliseconds, cloudMilliseconds, toneMilliseconds
    };
    if (!std::all_of(std::begin(values), std::end(values),
                     [](double value)
                     {
                         return std::isfinite(value) && value >= 0.0;
                     }) ||
        frameMilliseconds <= 0.0 ||
        weatherMilliseconds + atmosphereMilliseconds + shadowMilliseconds + opaqueMilliseconds +
            cloudMilliseconds + toneMilliseconds > frameMilliseconds + 1.0e-6)
    {
        return;
    }

    m_snapshot.rawGpuFrameMs = frameMilliseconds;
    m_snapshot.rawGpuWeatherMapMs = weatherMilliseconds;
    m_snapshot.rawGpuAtmosphereLutMs = atmosphereMilliseconds;
    m_snapshot.rawGpuShadowCacheMs = shadowMilliseconds;
    m_snapshot.rawGpuOpaqueSceneMs = opaqueMilliseconds;
    m_snapshot.rawGpuCloudMs = cloudMilliseconds;
    m_snapshot.rawGpuToneMapMs = toneMilliseconds;
    ++m_snapshot.gpuSampleIndex;

    if (m_snapshot.gpuValid)
    {
        m_snapshot.gpuFrameMs = Ema(
            m_snapshot.gpuFrameMs, frameMilliseconds);
        m_snapshot.gpuWeatherMapMs = Ema(
            m_snapshot.gpuWeatherMapMs, weatherMilliseconds);
        m_snapshot.gpuAtmosphereLutMs = Ema(
            m_snapshot.gpuAtmosphereLutMs, atmosphereMilliseconds);
        m_snapshot.gpuShadowCacheMs = Ema(
            m_snapshot.gpuShadowCacheMs, shadowMilliseconds);
        m_snapshot.gpuOpaqueSceneMs = Ema(
            m_snapshot.gpuOpaqueSceneMs, opaqueMilliseconds);
        m_snapshot.gpuCloudMs = Ema(
            m_snapshot.gpuCloudMs, cloudMilliseconds);
        m_snapshot.gpuToneMapMs = Ema(
            m_snapshot.gpuToneMapMs, toneMilliseconds);
    }
    else
    {
        m_snapshot.gpuFrameMs = frameMilliseconds;
        m_snapshot.gpuWeatherMapMs = weatherMilliseconds;
        m_snapshot.gpuAtmosphereLutMs = atmosphereMilliseconds;
        m_snapshot.gpuShadowCacheMs = shadowMilliseconds;
        m_snapshot.gpuOpaqueSceneMs = opaqueMilliseconds;
        m_snapshot.gpuCloudMs = cloudMilliseconds;
        m_snapshot.gpuToneMapMs = toneMilliseconds;
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
        SUCCEEDED(device->CreateQuery(&desc, &slot.weatherEnd)) &&
        SUCCEEDED(device->CreateQuery(&desc, &slot.atmosphereEnd)) &&
        SUCCEEDED(device->CreateQuery(&desc, &slot.shadowEnd)) &&
        SUCCEEDED(device->CreateQuery(&desc, &slot.opaqueEnd)) &&
        SUCCEEDED(device->CreateQuery(&desc, &slot.cloudEnd)) &&
        SUCCEEDED(device->CreateQuery(&desc, &slot.toneEnd)) &&
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
    const double milliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - m_cpuStart).count();
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
        UINT64 frameStart = 0, weatherEnd = 0, atmosphereEnd = 0, shadowEnd = 0;
        UINT64 opaqueEnd = 0, cloudEnd = 0, toneEnd = 0, frameEnd = 0;
        const bool ready =
            context->GetData(slot.frameStart.Get(), &frameStart,
                sizeof(frameStart), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
            context->GetData(slot.weatherEnd.Get(), &weatherEnd,
                sizeof(weatherEnd), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
            context->GetData(slot.atmosphereEnd.Get(), &atmosphereEnd,
                sizeof(atmosphereEnd), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
            context->GetData(slot.shadowEnd.Get(), &shadowEnd,
                sizeof(shadowEnd), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
            context->GetData(slot.opaqueEnd.Get(), &opaqueEnd,
                sizeof(opaqueEnd), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
            context->GetData(slot.cloudEnd.Get(), &cloudEnd,
                sizeof(cloudEnd), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
            context->GetData(slot.toneEnd.Get(), &toneEnd,
                sizeof(toneEnd), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
            context->GetData(slot.frameEnd.Get(), &frameEnd,
                sizeof(frameEnd), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
        if (!ready)
            continue;
        slot.inFlight = false;
        if (slot.generation != m_generation || disjoint.Disjoint ||
            disjoint.Frequency == 0 ||
            !(frameStart <= weatherEnd && weatherEnd <= atmosphereEnd && atmosphereEnd <= shadowEnd &&
              shadowEnd <= opaqueEnd && opaqueEnd <= cloudEnd &&
              cloudEnd <= toneEnd && toneEnd <= frameEnd))
            continue;
        const double scale = 1000.0 / static_cast<double>(disjoint.Frequency);
        m_accumulator.RecordGpuMilliseconds(
            (frameEnd - frameStart) * scale,
            (weatherEnd - frameStart) * scale,
            (atmosphereEnd - weatherEnd) * scale,
            (shadowEnd - atmosphereEnd) * scale,
            (opaqueEnd - shadowEnd) * scale,
            (cloudEnd - opaqueEnd) * scale,
            (toneEnd - cloudEnd) * scale);
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
        m_nextSlot = (index + 1u) % m_slots.size();
        return;
    }
}

#define VCLOUD_MARK_QUERY(methodName, fieldName)                              \
void FrameProfiler::methodName(ID3D11DeviceContext* context)                 \
{                                                                            \
    if (m_activeSlot >= 0 && context)                                         \
        context->End(m_slots[static_cast<std::size_t>(m_activeSlot)].         \
                     fieldName.Get());                                        \
}

VCLOUD_MARK_QUERY(MarkWeatherMapEnd, weatherEnd)
VCLOUD_MARK_QUERY(MarkAtmosphereLutEnd, atmosphereEnd)
VCLOUD_MARK_QUERY(MarkShadowCacheEnd, shadowEnd)
VCLOUD_MARK_QUERY(MarkOpaqueSceneEnd, opaqueEnd)
VCLOUD_MARK_QUERY(MarkCloudEnd, cloudEnd)
VCLOUD_MARK_QUERY(MarkToneMapEnd, toneEnd)

#undef VCLOUD_MARK_QUERY

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
