#include "NoiseLab.h"

#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"
#include "Stage3HeightMath.h"

using Microsoft::WRL::ComPtr;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

namespace
{
bool IsMouseMessage(UINT message)
{
    return (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) ||
           message == WM_NCMOUSEMOVE || message == WM_NCMOUSELEAVE;
}

bool IsKeyboardMessage(UINT message)
{
    return (message >= WM_KEYFIRST && message <= WM_KEYLAST) ||
           message == WM_CHAR;
}

std::string NarrowUtf8(const std::filesystem::path& path)
{
    const std::wstring wide = path.wstring();
    const int size = WideCharToMultiByte(
        CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1)
        return {};
    std::string result(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1,
                        result.data(), size, nullptr, nullptr);
    return result;
}

std::filesystem::path TimestampDirectory(const std::filesystem::path& root)
{
    SYSTEMTIME time = {};
    GetLocalTime(&time);
    wchar_t name[32] = {};
    swprintf_s(name, L"%04u%02u%02u-%02u%02u%02u",
               time.wYear, time.wMonth, time.wDay,
               time.wHour, time.wMinute, time.wSecond);
    return root / name;
}
}

NoiseLab::~NoiseLab()
{
    Shutdown();
}

bool NoiseLab::Init(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context)
{
    m_hwnd = hwnd;
    m_device = device;
    m_context = context;
    if (!CreatePreviewTargets() || !CreateConstantBuffer())
        return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    if (!ImGui_ImplWin32_Init(hwnd) || !ImGui_ImplDX11_Init(device, context))
    {
        Shutdown();
        return false;
    }
    m_initialized = true;
    return true;
}

void NoiseLab::Shutdown()
{
    if (m_initialized)
    {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
    m_initialized = false;
    m_noiseLabCb.Reset();
    m_targets = {};
    m_context = nullptr;
    m_device = nullptr;
    m_hwnd = nullptr;
}

bool NoiseLab::CreatePreviewTargets()
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = kPreviewSize;
    desc.Height = kPreviewSize;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    for (SliceTarget& target : m_targets)
    {
        if (FAILED(m_device->CreateTexture2D(&desc, nullptr, &target.texture)) ||
            FAILED(m_device->CreateRenderTargetView(
                target.texture.Get(), nullptr, &target.rtv)) ||
            FAILED(m_device->CreateShaderResourceView(
                target.texture.Get(), nullptr, &target.srv)) ||
            FAILED(m_device->CreateTexture2D(
                &stagingDesc, nullptr, &target.staging)))
            return false;
    }
    return true;
}

bool NoiseLab::CreateConstantBuffer()
{
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(NoiseLabParameters);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    return SUCCEEDED(m_device->CreateBuffer(&desc, nullptr, &m_noiseLabCb));
}

bool NoiseLab::HandleWindowMessage(
    HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_KEYDOWN && wParam == VK_F1)
    {
        if ((lParam & (1ll << 30)) == 0)
            ToggleVisible();
        return true;
    }
    if (!m_initialized || !m_visible)
        return false;

    ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam);
    const ImGuiIO& io = ImGui::GetIO();
    return (IsMouseMessage(message) && io.WantCaptureMouse) ||
           (IsKeyboardMessage(message) && io.WantCaptureKeyboard);
}

void NoiseLab::ToggleVisible()
{
    m_visible = !m_visible;
}

void NoiseLab::UpdateEffectiveTime(float applicationTime)
{
    if (!m_hasApplicationTime)
    {
        m_lastApplicationTime = applicationTime;
        m_hasApplicationTime = true;
    }
    const float delta = std::clamp(
        applicationTime - m_lastApplicationTime, 0.0f, 0.25f);
    m_lastApplicationTime = applicationTime;
    if (!m_timePaused)
        m_effectiveTime = std::max(0.0f, m_effectiveTime + delta * m_timeScale);

    if (m_slicePlaying)
    {
        float* values = &m_parameters.normalizedSlicePosition.x;
        values[m_playAxis] += delta * m_slicePlaySpeed * m_slicePlayDirection;
        if (values[m_playAxis] >= 1.0f)
        {
            values[m_playAxis] = 1.0f;
            m_slicePlayDirection = -1.0f;
        }
        else if (values[m_playAxis] <= 0.0f)
        {
            values[m_playAxis] = 0.0f;
            m_slicePlayDirection = 1.0f;
        }
    }
    m_parameters.effectiveTime = m_effectiveTime;
}

void NoiseLab::BeginFrame(float applicationTime,
                          CloudParameters& cloudParameters,
                          std::uint64_t shaderGeneration,
                          const std::string& shaderStatus,
                          const std::string& shaderError)
{
    if (!m_initialized)
        return;
    UpdateEffectiveTime(applicationTime);
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    if (m_visible)
        DrawControlWindow(cloudParameters, shaderGeneration, shaderStatus, shaderError);
}

void NoiseLab::DrawControlWindow(CloudParameters& cloudParameters,
                                 std::uint64_t shaderGeneration,
                                 const std::string& shaderStatus,
                                 const std::string& shaderError)
{
    ImGui::SetNextWindowSize(ImVec2(620.0f, 680.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Noise Lab (F1)", &m_visible))
    {
        ImGui::End();
        return;
    }

    ImGui::Text("Shader generation: %llu",
                static_cast<unsigned long long>(shaderGeneration));
    ImGui::SameLine();
    ImGui::TextColored(shaderError.empty() ? ImVec4(0.4f, 1.0f, 0.5f, 1.0f)
                                            : ImVec4(1.0f, 0.35f, 0.3f, 1.0f),
                       "%s", shaderStatus.c_str());
    if (!shaderError.empty())
    {
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted(shaderError.c_str());
        ImGui::PopTextWrapPos();
    }

    const char* outputs[] = {
        "Raw Noise", "Threshold Density", "Final Density",
        "Height Fraction", "Height Profile", "Base Density",
        "Detail Noise", "Erosion", "Detail Sample Mask"
    };
    int output = static_cast<int>(m_parameters.outputMode);
    if (ImGui::Combo("Output", &output, outputs, 9))
        m_parameters.outputMode = static_cast<std::uint32_t>(output);

    float* slice = &m_parameters.normalizedSlicePosition.x;
    ImGui::SliderFloat3("Slice XYZ", slice, 0.0f, 1.0f, "%.3f");
    const char* axes[] = { "X", "Y", "Z" };
    ImGui::Combo("Play Axis", &m_playAxis, axes, 3);
    ImGui::Checkbox("Slice Play", &m_slicePlaying);
    ImGui::SameLine();
    ImGui::SliderFloat("Slice Speed", &m_slicePlaySpeed, 0.01f, 1.0f, "%.2f cycle/s");

    ImGui::SeparatorText("Synchronized 3D slices");
    DrawSlice("XY (fixed Z)", NoiseSliceAxis::XY, m_targets[0]);
    ImGui::SameLine();
    DrawSlice("XZ (fixed Y)", NoiseSliceAxis::XZ, m_targets[1]);
    ImGui::SameLine();
    DrawSlice("YZ (fixed X)", NoiseSliceAxis::YZ, m_targets[2]);

    const CloudParameters before = cloudParameters;
    if (ImGui::CollapsingHeader(
            "Shared cloud parameters", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SliderFloat("Noise Scale", &cloudParameters.baseNoiseScale,
                           0.01f, 2.0f, "%.3f cycle/m", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Coverage", &cloudParameters.coverage, 0.0f, 1.0f);
        ImGui::SliderFloat("Density", &cloudParameters.densityMultiplier, 0.0f, 4.0f);
        ImGui::DragFloat("Noise Offset", &cloudParameters.noiseOffset,
                         0.01f, -20.0f, 20.0f);
        ImGui::DragFloat3("Wind Direction", &cloudParameters.windDirection.x,
                          0.01f, -1.0f, 1.0f);
        ImGui::SliderFloat("Wind Speed", &cloudParameters.windSpeed,
                           0.0f, 3.0f, "%.2f m/s");
    }

    if (ImGui::CollapsingHeader("Height profile", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SliderFloat("Bottom Fade End", &cloudParameters.bottomFadeEnd,
                           0.01f, 0.99f, "%.2f normalized height");
        ImGui::SliderFloat("Top Fade Start", &cloudParameters.topFadeStart,
                           0.01f, 0.99f, "%.2f normalized height");
        if (cloudParameters.bottomFadeEnd > cloudParameters.topFadeStart)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f),
                               "Fade ranges overlap: the full-density plateau disappears.");
        }

        std::array<float, 64> profileCurve = {};
        for (std::size_t index = 0; index < profileCurve.size(); ++index)
        {
            const float height = static_cast<float>(index) /
                static_cast<float>(profileCurve.size() - 1);
            profileCurve[index] = stage3::EvaluateHeightProfileFromFraction(
                height, cloudParameters.bottomFadeEnd, cloudParameters.topFadeStart);
        }
        ImGui::PlotLines("Profile curve", profileCurve.data(),
                         static_cast<int>(profileCurve.size()), 0,
                         "bottom 0 -> top 1", 0.0f, 1.0f, ImVec2(0.0f, 80.0f));
        const float selectedWorldY = cloudParameters.cloudBoundsMin.y +
            (cloudParameters.cloudBoundsMax.y - cloudParameters.cloudBoundsMin.y) *
            m_parameters.normalizedSlicePosition.y;
        const float selectedHeight = stage3::EvaluateHeightFraction(
            selectedWorldY, cloudParameters.cloudBoundsMin.y, cloudParameters.cloudBoundsMax.y);
        const float selectedProfile = stage3::EvaluateHeightProfileFromFraction(
            selectedHeight, cloudParameters.bottomFadeEnd, cloudParameters.topFadeStart);
        ImGui::Text("Selected Y: fraction %.3f, profile %.3f",
                    selectedHeight, selectedProfile);
        if (ImGui::Button("Reset Bottom Fade"))
            cloudParameters.bottomFadeEnd = 0.20f;
        ImGui::SameLine();
        if (ImGui::Button("Reset Top Fade"))
            cloudParameters.topFadeStart = 0.80f;
    }
    cloudParameters.bottomFadeEnd = std::clamp(cloudParameters.bottomFadeEnd, 0.01f, 0.99f);
    cloudParameters.topFadeStart = std::clamp(cloudParameters.topFadeStart, 0.01f, 0.99f);

    if (ImGui::CollapsingHeader("Detail erosion", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SliderFloat("Detail Scale", &cloudParameters.detailNoiseScale,
                           0.1f, 16.0f, "%.3f cycle/m", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Erosion Strength", &cloudParameters.detailErosionStrength,
                           0.0f, 1.0f, "%.3f");
        ImGui::SliderFloat("Detail Wind Speed", &cloudParameters.detailWindSpeed,
                           0.0f, 3.0f, "%.2f m/s");
        ImGui::DragFloat("Detail Offset", &cloudParameters.detailNoiseOffset,
                         0.01f, -50.0f, 50.0f, "%.2f cycle");
        if (ImGui::Button("Reset Detail"))
        {
            cloudParameters.detailNoiseScale = 2.5f;
            cloudParameters.detailErosionStrength = 0.25f;
            cloudParameters.detailWindSpeed = 0.45f;
            cloudParameters.detailNoiseOffset = 17.3f;
        }
        ImGui::TextDisabled("F9 Off / F10 Default / F11 Fine / F12 Strong");
    }
    cloudParameters.detailNoiseScale = std::max(cloudParameters.detailNoiseScale, 0.1f);
    cloudParameters.detailErosionStrength = std::clamp(
        cloudParameters.detailErosionStrength, 0.0f, 1.0f);
    cloudParameters.detailWindSpeed = std::max(cloudParameters.detailWindSpeed, 0.0f);

    if (ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Checkbox("Pause Time", &m_timePaused);
        ImGui::SliderFloat("Time Scale", &m_timeScale, 0.0f, 4.0f);
        if (m_timePaused)
        {
            if (ImGui::DragFloat("Manual Time", &m_effectiveTime,
                                 0.02f, 0.0f, 10000.0f))
                m_parameters.effectiveTime = m_effectiveTime;
        }
        if (ImGui::Button("Reset Time"))
        {
            m_effectiveTime = 0.0f;
            m_parameters.effectiveTime = 0.0f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset Noise"))
        {
            cloudParameters.baseNoiseScale = 0.35f;
            cloudParameters.coverage = 0.55f;
            cloudParameters.densityMultiplier = 1.0f;
            cloudParameters.windDirection = { 0.9701425f, 0.0f, 0.2425356f };
            cloudParameters.windSpeed = 0.25f;
            cloudParameters.noiseOffset = 0.0f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Export 3 PNG + JSON"))
            m_exportRequested = true;
        if (!m_exportStatus.empty())
            ImGui::TextWrapped("%s", m_exportStatus.c_str());
    }

    m_parametersChanged = m_parametersChanged ||
        std::memcmp(&before, &cloudParameters, sizeof(CloudParameters)) != 0;

    ImGui::End();
}

void NoiseLab::DrawSlice(const char* label, NoiseSliceAxis axis, SliceTarget& target)
{
    ImGui::BeginGroup();
    ImGui::TextUnformatted(label);
    const ImVec2 size(180.0f, 180.0f);
    ImGui::Image(ImTextureRef(static_cast<ImTextureID>(
                     reinterpret_cast<std::uintptr_t>(target.srv.Get()))), size);
    const ImVec2 imageMin = ImGui::GetItemRectMin();
    const ImVec2 imageMax = ImGui::GetItemRectMax();

    float horizontal = 0.5f;
    float vertical = 0.5f;
    if (axis == NoiseSliceAxis::XY)
    {
        horizontal = m_parameters.normalizedSlicePosition.x;
        vertical = m_parameters.normalizedSlicePosition.y;
    }
    else if (axis == NoiseSliceAxis::XZ)
    {
        horizontal = m_parameters.normalizedSlicePosition.x;
        vertical = m_parameters.normalizedSlicePosition.z;
    }
    else
    {
        horizontal = m_parameters.normalizedSlicePosition.z;
        vertical = m_parameters.normalizedSlicePosition.y;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 color = IM_COL32(255, 70, 70, 220);
    const float x = imageMin.x + horizontal * (imageMax.x - imageMin.x);
    const float y = imageMin.y + (1.0f - vertical) * (imageMax.y - imageMin.y);
    drawList->AddLine(ImVec2(x, imageMin.y), ImVec2(x, imageMax.y), color);
    drawList->AddLine(ImVec2(imageMin.x, y), ImVec2(imageMax.x, y), color);

    if (ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        const ImVec2 mouse = ImGui::GetMousePos();
        const float u = std::clamp((mouse.x - imageMin.x) / size.x, 0.0f, 1.0f);
        const float v = 1.0f - std::clamp((mouse.y - imageMin.y) / size.y, 0.0f, 1.0f);
        if (axis == NoiseSliceAxis::XY)
        {
            m_parameters.normalizedSlicePosition.x = u;
            m_parameters.normalizedSlicePosition.y = v;
        }
        else if (axis == NoiseSliceAxis::XZ)
        {
            m_parameters.normalizedSlicePosition.x = u;
            m_parameters.normalizedSlicePosition.z = v;
        }
        else
        {
            m_parameters.normalizedSlicePosition.z = u;
            m_parameters.normalizedSlicePosition.y = v;
        }
    }
    ImGui::EndGroup();
}

void NoiseLab::RenderPreviews(ID3D11VertexShader* fullscreenVs,
                              ID3D11PixelShader* noiseLabPs,
                              ID3D11Buffer* cloudCb)
{
    if (!m_initialized || !m_visible || !fullscreenVs || !noiseLabPs)
        return;

    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(kPreviewSize);
    viewport.Height = static_cast<float>(kPreviewSize);
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);
    m_context->OMSetDepthStencilState(nullptr, 0);
    m_context->RSSetState(nullptr);
    m_context->IASetInputLayout(nullptr);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(fullscreenVs, nullptr, 0);
    m_context->PSSetShader(noiseLabPs, nullptr, 0);
    m_context->PSSetConstantBuffers(1, 1, &cloudCb);

    for (std::uint32_t axis = 0; axis < m_targets.size(); ++axis)
    {
        m_parameters.sliceAxis = axis;
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(m_context->Map(
                m_noiseLabCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            continue;
        std::memcpy(mapped.pData, &m_parameters, sizeof(m_parameters));
        m_context->Unmap(m_noiseLabCb.Get(), 0);
        ID3D11Buffer* labCb = m_noiseLabCb.Get();
        m_context->PSSetConstantBuffers(2, 1, &labCb);

        const float clear[4] = {};
        ID3D11RenderTargetView* rtv = m_targets[axis].rtv.Get();
        m_context->OMSetRenderTargets(1, &rtv, nullptr);
        m_context->ClearRenderTargetView(rtv, clear);
        m_context->Draw(3, 0);
    }
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
}

void NoiseLab::EndFrame(ID3D11RenderTargetView* backBufferRtv)
{
    if (!m_initialized)
        return;
    m_context->OMSetRenderTargets(1, &backBufferRtv, nullptr);
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

bool NoiseLab::ConsumeParametersChanged()
{
    const bool result = m_parametersChanged;
    m_parametersChanged = false;
    return result;
}

bool NoiseLab::ConsumeExportRequest()
{
    const bool result = m_exportRequested;
    m_exportRequested = false;
    return result;
}

bool NoiseLab::ValidatePreviewData()
{
    const auto outputMode = static_cast<NoiseOutputMode>(m_parameters.outputMode);
    const bool heightOnly = outputMode == NoiseOutputMode::HeightFraction ||
                            outputMode == NoiseOutputMode::HeightProfile;
    const bool sampleMask = outputMode == NoiseOutputMode::DetailSampleMask;
    for (std::size_t targetIndex = 0; targetIndex < m_targets.size(); ++targetIndex)
    {
        SliceTarget& target = m_targets[targetIndex];
        m_context->CopyResource(target.staging.Get(), target.texture.Get());
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(m_context->Map(target.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            return false;
        unsigned char minimum = 255;
        unsigned char maximum = 0;
        for (UINT y = 0; y < kPreviewSize; ++y)
        {
            const auto* row = static_cast<const unsigned char*>(mapped.pData) +
                              static_cast<size_t>(y) * mapped.RowPitch;
            for (UINT x = 0; x < kPreviewSize; ++x)
            {
                const unsigned char value = row[x * 4];
                minimum = std::min(minimum, value);
                maximum = std::max(maximum, value);
            }
        }
        m_context->Unmap(target.staging.Get(), 0);
        // XZ는 Y를 고정하므로 height-only 출력이 단색인 것이 정상이다.
        // XY와 YZ는 세로축에 월드 Y가 들어가므로 위아래 변화가 반드시 있어야 한다.
        const bool fixedYHeightSlice = heightOnly && targetIndex == 1;
        if (fixedYHeightSlice)
        {
            if (maximum != minimum)
                return false;
        }
        else if (maximum <= minimum)
        {
            return false;
        }
        if (sampleMask && (minimum != 0 || maximum != 255))
            return false;
    }
    return true;
}

std::uint64_t NoiseLab::PreviewHash(std::size_t targetIndex)
{
    if (targetIndex >= m_targets.size())
        return 0;
    SliceTarget& target = m_targets[targetIndex];
    m_context->CopyResource(target.staging.Get(), target.texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(m_context->Map(target.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return 0;
    std::uint64_t hash = 1469598103934665603ull;
    for (UINT y = 0; y < kPreviewSize; ++y)
    {
        const auto* row = static_cast<const unsigned char*>(mapped.pData) +
                          static_cast<size_t>(y) * mapped.RowPitch;
        for (UINT x = 0; x < kPreviewSize * 4; ++x)
        {
            hash ^= row[x];
            hash *= 1099511628211ull;
        }
    }
    m_context->Unmap(target.staging.Get(), 0);
    return hash;
}

bool NoiseLab::SaveTargetPng(const std::filesystem::path& path, SliceTarget& target)
{
    m_context->CopyResource(target.staging.Get(), target.texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(m_context->Map(target.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return false;

    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    HRESULT result = CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (SUCCEEDED(result)) result = factory->CreateStream(&stream);
    if (SUCCEEDED(result)) result = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(result)) result = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(result)) result = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (SUCCEEDED(result)) result = encoder->CreateNewFrame(&frame, &properties);
    if (SUCCEEDED(result)) result = frame->Initialize(properties.Get());
    if (SUCCEEDED(result)) result = frame->SetSize(kPreviewSize, kPreviewSize);
    // D3D texture는 회색조라 R/B 순서가 같으며 PNG encoder가 기본 지원하는
    // 32bpp BGRA를 사용하면 별도 format converter 없이 안전하게 기록할 수 있다.
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(result)) result = frame->SetPixelFormat(&format);
    if (SUCCEEDED(result) && format != GUID_WICPixelFormat32bppBGRA)
        result = E_FAIL;
    if (SUCCEEDED(result))
        result = frame->WritePixels(kPreviewSize, mapped.RowPitch,
                                    mapped.RowPitch * kPreviewSize,
                                    static_cast<BYTE*>(mapped.pData));
    if (SUCCEEDED(result)) result = frame->Commit();
    if (SUCCEEDED(result)) result = encoder->Commit();
    m_context->Unmap(target.staging.Get(), 0);
    return SUCCEEDED(result);
}

std::uint64_t NoiseLab::HashFile(const std::filesystem::path& path) const
{
    std::ifstream input(path, std::ios::binary);
    std::uint64_t hash = 1469598103934665603ull;
    char byte = 0;
    while (input.get(byte))
    {
        hash ^= static_cast<unsigned char>(byte);
        hash *= 1099511628211ull;
    }
    return hash;
}

bool NoiseLab::WriteMetadata(const std::filesystem::path& path,
                             const CloudParameters& cloud,
                             Stage4DetailPreset detailPreset,
                             const std::filesystem::path& noiseSourcePath) const
{
    std::ofstream output(path, std::ios::binary);
    if (!output)
        return false;
    static const char* outputNames[] = {
        "rawNoise", "thresholdDensity", "finalDensity",
        "heightFraction", "heightProfile", "baseDensity",
        "detailNoise", "erosion", "detailSampleMask"
    };
    static const char* detailPresetNames[] = {
        "detailOff", "defaultDetail", "fineDetail", "strongErosion", "custom"
    };
    const int presetIndex = std::clamp(static_cast<int>(detailPreset), 0, 4);
    output << std::fixed << std::setprecision(6)
           << "{\n"
           << "  \"schemaVersion\": 3,\n"
           << "  \"output\": \"" << outputNames[m_parameters.outputMode] << "\",\n"
           << "  \"detailPreset\": \"" << detailPresetNames[presetIndex] << "\",\n"
           << "  \"resolution\": [512, 512],\n"
           << "  \"slicePosition\": [" << m_parameters.normalizedSlicePosition.x << ", "
           << m_parameters.normalizedSlicePosition.y << ", "
           << m_parameters.normalizedSlicePosition.z << "],\n"
           << "  \"effectiveTime\": " << m_effectiveTime << ",\n"
           << "  \"baseNoiseScale\": " << cloud.baseNoiseScale << ",\n"
           << "  \"coverage\": " << cloud.coverage << ",\n"
           << "  \"densityMultiplier\": " << cloud.densityMultiplier << ",\n"
           << "  \"bottomFadeEnd\": " << cloud.bottomFadeEnd << ",\n"
           << "  \"topFadeStart\": " << cloud.topFadeStart << ",\n"
           << "  \"detailNoiseScale\": " << cloud.detailNoiseScale << ",\n"
           << "  \"detailErosionStrength\": " << cloud.detailErosionStrength << ",\n"
           << "  \"detailWindSpeed\": " << cloud.detailWindSpeed << ",\n"
           << "  \"detailNoiseOffset\": " << cloud.detailNoiseOffset << ",\n"
           << "  \"noiseOffset\": " << cloud.noiseOffset << ",\n"
           << "  \"windDirection\": [" << cloud.windDirection.x << ", "
           << cloud.windDirection.y << ", " << cloud.windDirection.z << "],\n"
           << "  \"windSpeed\": " << cloud.windSpeed << ",\n"
           << "  \"noiseSource\": \"Noise.hlsli\",\n"
           << "  \"noiseSourceHashFnv1a64\": \"" << std::hex
           << HashFile(noiseSourcePath) << "\"\n"
           << "}\n";
    return output.good();
}

bool NoiseLab::ExportSnapshot(const std::filesystem::path& root,
                              const CloudParameters& cloudParameters,
                              Stage4DetailPreset detailPreset,
                              const std::filesystem::path& noiseSourcePath)
{
    std::error_code error;
    const std::filesystem::path directory = TimestampDirectory(root);
    std::filesystem::create_directories(directory, error);
    if (error)
    {
        m_exportStatus = "Export failed: " + error.message();
        return false;
    }

    const bool success = SaveTargetPng(directory / L"xy.png", m_targets[0]) &&
                         SaveTargetPng(directory / L"xz.png", m_targets[1]) &&
                         SaveTargetPng(directory / L"yz.png", m_targets[2]) &&
                         WriteMetadata(directory / L"noise-settings.json",
                                       cloudParameters, detailPreset, noiseSourcePath);
    m_exportStatus = success
        ? "Exported: " + NarrowUtf8(directory)
        : "Export failed while writing PNG or JSON";
    return success;
}
