// 자동 검증 전용 readback. 일반 렌더/성능 실행에서는 호출하지 않는다.
#include "Renderer.h"
#include "Camera.h"
#include "Fnv1a64.h"
#include <d3dcompiler.h>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>

void Renderer::RecordDeterminismShader(const std::wstring& path, const char* entry,
    const char* target, UINT flags, const std::string& key, ID3DBlob* blob)
{
    if (!m_determinismValidation || !blob) return;
    if (m_determinismShaderReport.empty()) {
        wchar_t compilerPath[32768] = {};
        if (GetModuleFileNameW(GetModuleHandleW(L"d3dcompiler_47.dll"), compilerPath, 32768)) {
            std::ifstream input(std::filesystem::path(compilerPath), std::ios::binary);
            std::vector<char> bytes((std::istreambuf_iterator<char>(input)), {});
            std::uint64_t compilerHash = fnv1a64::kOffsetBasis;
            fnv1a64::Append(compilerHash, bytes.data(), bytes.size());
            std::ostringstream compiler;
            compiler << "[Compiler] name=d3dcompiler_47.dll bytes=" << bytes.size()
                << " hash=" << std::hex << compilerHash << '\n';
            m_determinismShaderReport = compiler.str();
        }
    }
    std::uint64_t hash = fnv1a64::kOffsetBasis;
    fnv1a64::Append(hash, blob->GetBufferPointer(), blob->GetBufferSize());
    // Debug 재컴파일은 SPDB만 달라질 수 있다. 원본 hash는 보존하고 비교용으로는
    // debug 정보만 제외한다(명령어/리소스 reflection/입출력 signature는 그대로).
    ComPtr<ID3DBlob> executable;
    const HRESULT stripped = D3DStripShader(blob->GetBufferPointer(), blob->GetBufferSize(),
        D3DCOMPILER_STRIP_DEBUG_INFO, &executable);
    std::uint64_t executableHash = fnv1a64::kOffsetBasis;
    if (SUCCEEDED(stripped)) fnv1a64::Append(executableHash,
        executable->GetBufferPointer(), executable->GetBufferSize());
    std::ostringstream out;
    std::string macros = "none";
    for (const auto& program : m_shaderManifest) {
        if (program.source.filename() == std::filesystem::path(path).filename() && program.entry == entry) {
            if (!program.defines.empty()) macros.clear();
            for (const auto& define : program.defines) macros += define.name + "=" + define.value + ";";
        }
    }
    out << "[Shader] name=" << std::filesystem::path(path).filename().string()
        << " entry=" << entry << " target=" << target << " flags=" << flags << " macros=" << macros
        << " sourceCompilerKey=" << key << " dxbc=" << std::hex << hash
        << " executable=" << executableHash << " valid=" << SUCCEEDED(stripped) << '\n';
    m_determinismShaderReport += out.str();
}

void Renderer::CaptureDeterminismFrame()
{
    m_determinismCaptureValid = true;
    std::ostringstream report;
    // CopyResource → blocking Map은 해당 복사 완료를 기다린다. 별도 Flush/sleep 불필요.
    const auto capture = [&](const char* name, ID3D11Resource* source) {
        bool valid = source != nullptr;
        UINT width = 0, height = 1, depth = 1, arrays = 1, bytesPerPixel = 1;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        ComPtr<ID3D11Resource> staging;
        D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
        if (source) source->GetType(&dimension);
        if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
            ComPtr<ID3D11Texture2D> texture; source->QueryInterface(IID_PPV_ARGS(&texture));
            D3D11_TEXTURE2D_DESC desc = {}; texture->GetDesc(&desc);
            width = desc.Width; height = desc.Height; arrays = desc.ArraySize; format = desc.Format;
            valid = desc.MipLevels == 1 && desc.SampleDesc.Count == 1;
            desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; desc.MiscFlags = 0;
            ComPtr<ID3D11Texture2D> copy;
            valid = SUCCEEDED(m_device->CreateTexture2D(&desc, nullptr, &copy)) && valid;
            staging = copy;
        } else if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE3D) {
            ComPtr<ID3D11Texture3D> texture; source->QueryInterface(IID_PPV_ARGS(&texture));
            D3D11_TEXTURE3D_DESC desc = {}; texture->GetDesc(&desc);
            width = desc.Width; height = desc.Height; depth = desc.Depth; format = desc.Format;
            valid = desc.MipLevels == 1;
            desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; desc.MiscFlags = 0;
            ComPtr<ID3D11Texture3D> copy;
            valid = SUCCEEDED(m_device->CreateTexture3D(&desc, nullptr, &copy)) && valid;
            staging = copy;
        } else if (dimension == D3D11_RESOURCE_DIMENSION_BUFFER) {
            ComPtr<ID3D11Buffer> buffer; source->QueryInterface(IID_PPV_ARGS(&buffer));
            D3D11_BUFFER_DESC desc = {}; buffer->GetDesc(&desc); width = desc.ByteWidth;
            desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; desc.MiscFlags = 0;
            ComPtr<ID3D11Buffer> copy;
            valid = SUCCEEDED(m_device->CreateBuffer(&desc, nullptr, &copy)); staging = copy;
        } else valid = false;
        if (format == DXGI_FORMAT_R16G16B16A16_FLOAT) bytesPerPixel = 8;
        else if (format == DXGI_FORMAT_R8G8B8A8_UNORM || format == DXGI_FORMAT_R32_FLOAT ||
                 format == DXGI_FORMAT_R32_TYPELESS || format == DXGI_FORMAT_D32_FLOAT) bytesPerPixel = 4;
        else if (dimension != D3D11_RESOURCE_DIMENSION_BUFFER) valid = false;
        std::vector<std::uint8_t> bytes;
        if (valid) {
            m_context->CopyResource(staging.Get(), source);
            for (UINT array = 0; array < arrays && valid; ++array) {
                D3D11_MAPPED_SUBRESOURCE mapped = {};
                if (FAILED(m_context->Map(staging.Get(), array, D3D11_MAP_READ, 0, &mapped))) {
                    valid = false; break;
                }
                for (UINT z = 0; z < depth; ++z) for (UINT y = 0; y < height; ++y) {
                    const auto* row = static_cast<const std::uint8_t*>(mapped.pData) +
                        size_t(z) * mapped.DepthPitch + size_t(y) * mapped.RowPitch;
                    bytes.insert(bytes.end(), row, row + size_t(width) * bytesPerPixel);
                }
                m_context->Unmap(staging.Get(), array);
            }
        }
        if (format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
            for (size_t i = 0; i + 1 < bytes.size(); i += 2) {
                std::uint16_t bits; std::memcpy(&bits, bytes.data() + i, 2);
                if ((bits & 0x7c00u) == 0x7c00u) valid = false;
            }
        }
        std::uint64_t hash = fnv1a64::kOffsetBasis;
        fnv1a64::Append(hash, bytes.data(), bytes.size());
        report << "[Resource] frame=" << m_renderFrameSerial << " name=" << name
            << " width=" << width << " height=" << height << " depth=" << depth
            << " arrays=" << arrays << " format=" << unsigned(format)
            << " bpp=" << bytesPerPixel << " valid=" << valid << " hash="
            << std::hex << hash << std::dec << '\n';
        // 좌표/비트 차이 분석이 필요할 때만 지정 프레임의 패딩 없는 원본을 보존한다.
        wchar_t dumpFrame[32] = {}, dumpRoot[32768] = {};
        if (GetEnvironmentVariableW(L"VCLOUD_TEST_DUMP_FRAME", dumpFrame, 32) &&
            std::wcstoull(dumpFrame, nullptr, 10) == m_renderFrameSerial &&
            GetEnvironmentVariableW(L"VCLOUD_TEST_DUMP_ROOT", dumpRoot, 32768)) {
            std::error_code error; std::filesystem::create_directories(dumpRoot, error);
            std::ofstream output(std::filesystem::path(dumpRoot) / (std::string(name) + ".bin"), std::ios::binary);
            output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            valid = valid && !error && bool(output);
        }
        m_determinismCaptureValid = valid && m_determinismCaptureValid;
    };
    capture("Weather", m_weatherMapTexture.Get());
    if (m_determinismDetailed) {
        capture("Base", m_baseNoiseVolume.Get()); capture("Detail", m_detailNoiseVolume.Get());
        capture("Transmittance", m_atmosphereLuts.transmittance.texture.Get());
        capture("Multi", m_atmosphereLuts.multiScattering.texture.Get());
        capture("SkyView", m_atmosphereLuts.skyView.texture.Get());
        capture("SkyIrradiance", m_atmosphereLuts.skyIrradiance.texture.Get());
        capture("AerialRadiance", m_atmosphereLuts.aerialRadiance.texture.Get());
        capture("AerialTransmittance", m_atmosphereLuts.aerialTransmittance.texture.Get());
        capture("ShadowNear", m_shadowNearTexture.Get()); capture("ShadowFar", m_shadowFarTexture.Get());
        capture("Scene", m_sceneColor.Get()); capture("Depth", m_sceneDepth.Get());
        ID3D11Buffer* buffers[] = {m_cameraCb.Get(), m_cloudCb.Get(), m_lightCb.Get(),
            m_environmentCb.Get(), m_cloudDomainCb.Get(), m_noiseVolumeCb.Get(),
            m_cloudShapeCb.Get(), m_shadowCb.Get(), m_stage14Cb.Get(), m_weatherColumnCb.Get()};
        for (size_t i = 0; i < std::size(buffers); ++i)
            capture(("CB" + std::to_string(i)).c_str(), buffers[i]);
    }
    capture("HDR", m_hdrCloud.Get());
    ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) m_determinismCaptureValid = false;
    capture("Tone", backBuffer.Get());
    report << "[State] frame=" << m_renderFrameSerial << " inputs=" << std::hex;
    // 비교 가능한 CB 업로드 내용 키. COM 주소/CPU 구조체 패딩은 포함하지 않는다.
    for (auto hash : m_constantBufferUploadHashes) report << hash << ',';
    report << " lutKeys=";
    for (auto hash : m_atmosphereLutHashes) report << hash << ',';
    report << " generations=" << std::dec;
    for (auto generation : m_atmosphereLutGenerations) report << generation << ',';
    report << '\n';
    m_determinismCaptureValid = SUCCEEDED(m_device->GetDeviceRemovedReason()) && m_determinismCaptureValid;
    m_determinismFrameReport = report.str();
}

bool Renderer::ValidateDeterminismTransitions(Camera& camera)
{
    const auto cameraTimeIs = [&](float expected) {
        D3D11_BUFFER_DESC desc = {}; m_cameraCb->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Buffer> staging;
        if (FAILED(m_device->CreateBuffer(&desc, nullptr, &staging))) return false;
        m_context->CopyResource(staging.Get(), m_cameraCb.Get());
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(m_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
        CameraCB actual = {}; std::memcpy(&actual, mapped.pData, sizeof(actual));
        m_context->Unmap(staging.Get(), 0);
        return actual.time == expected;
    };
    Render(camera, 23.0f);
    if (!m_determinismCaptureValid || !cameraTimeIs(23.0f)) return false;
    // 같은 시각/카메라라도 LUT의 직접 Map은 CameraCB(time=0)를 쓴다.
    m_atmosphereLutsValid = false;
    Render(camera, 23.0f);
    if (!m_determinismCaptureValid || !cameraTimeIs(23.0f)) return false;
    const int width = m_width, height = m_height;
    Resize(width + 16, height + 8);
    camera.SetAspect(float(width + 16) / float(height + 8));
    Render(camera, 23.0f);
    if (!m_determinismCaptureValid || !cameraTimeIs(23.0f)) return false;
    Resize(width, height); camera.SetAspect(float(width) / float(height));
    Render(camera, 23.0f);
    return m_determinismCaptureValid && cameraTimeIs(23.0f) && !HasDebugLayerErrors();
}
