// 00 baseline 전용 실행기. 렌더 수식을 바꾸지 않고 실제 HDR/화면을 보존한다.
#include "Renderer.h"
#include "Camera.h"
#include "Fnv1a64.h"
#include "Stage13CameraPresets.h"
#include "Stage12ShadowMath.h"
#include "CloudRimMath.h"
#include <DirectXPackedVector.h>
#include <wincodec.h>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>

struct DirectionalLightingFrame
{
    std::vector<std::uint8_t> hdr;
    std::vector<std::uint8_t> rgba;
    bool valid = false;
};

// F1/F4 새 슬롯 전용 회귀는 아래 구현에서 별도 임시 저장 루트를 사용한다.

namespace
{
using Microsoft::WRL::ComPtr;
constexpr UINT kWidth = 1920, kHeight = 1080;
constexpr float kTime = 71.0f;

void Log(const std::string& text)
{
    const std::string line = text + "\n";
    DWORD written = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), line.data(),
        static_cast<DWORD>(line.size()), &written, nullptr);
    OutputDebugStringA(line.c_str());
}

// 원본은 CREATE_NEW로만 저장한다. 재실행/실패 복구도 기존 파일을 덮어쓰지 않는다.
bool WriteNew(const std::filesystem::path& path, const void* bytes, std::size_t size)
{
    if (size > MAXDWORD) return false;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(file, bytes, static_cast<DWORD>(size), &written, nullptr) &&
        written == size;
    CloseHandle(file);
    return ok;
}

bool WriteText(const std::filesystem::path& path, const std::string& text)
{
    return WriteNew(path, text.data(), text.size());
}

bool ReadBytes(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes,
               std::size_t expected)
{
    std::error_code error;
    if (std::filesystem::file_size(path, error) != expected || error) return false;
    bytes.resize(expected);
    std::ifstream file(path, std::ios::binary);
    return bool(file.read(reinterpret_cast<char*>(bytes.data()), bytes.size()));
}

bool SavePng(const std::filesystem::path& path, const std::vector<std::uint8_t>& pixels)
{
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory))) ||
        FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) ||
        FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(&frame, nullptr)) ||
        FAILED(frame->Initialize(nullptr)) || FAILED(frame->SetSize(kWidth, kHeight)))
        return false;
    // WIC PNG encoder는 BGRA를 받는다. 화면 RGB 값은 바꾸지 않고 저장 채널 순서만 맞춘다.
    std::vector<std::uint8_t> bgra = pixels;
    for (std::size_t i = 0; i < bgra.size(); i += 4) std::swap(bgra[i], bgra[i + 2]);
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(frame->SetPixelFormat(&format)) || format != GUID_WICPixelFormat32bppBGRA ||
        FAILED(frame->WritePixels(kHeight, kWidth * 4, static_cast<UINT>(pixels.size()),
            bgra.data())) || FAILED(frame->Commit()) ||
        FAILED(encoder->Commit())) return false;
    STATSTG stat = {};
    LARGE_INTEGER beginning = {};
    if (FAILED(stream->Stat(&stat, STATFLAG_NONAME)) || stat.cbSize.HighPart ||
        FAILED(stream->Seek(beginning, STREAM_SEEK_SET, nullptr))) return false;
    std::vector<std::uint8_t> encoded(stat.cbSize.LowPart);
    ULONG read = 0;
    return SUCCEEDED(stream->Read(encoded.data(), static_cast<ULONG>(encoded.size()), &read)) &&
        read == encoded.size() && WriteNew(path, encoded.data(), encoded.size());
}

bool LoadPng(const std::filesystem::path& path, std::vector<std::uint8_t>& pixels)
{
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
            WICDecodeMetadataCacheOnLoad, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) ||
        FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return false;
    UINT width = 0, height = 0;
    if (FAILED(frame->GetSize(&width, &height)) || width != kWidth || height != kHeight)
        return false;
    pixels.resize(std::size_t(kWidth) * kHeight * 4);
    return SUCCEEDED(converter->CopyPixels(nullptr, kWidth * 4,
        static_cast<UINT>(pixels.size()), pixels.data()));
}

std::string Hash(const void* data, std::size_t size)
{
    std::ostringstream output;
    output << std::hex << fnv1a64::Hash(data, size);
    return output.str();
}

bool FiniteHdr(const std::vector<std::uint8_t>& bytes)
{
    if (bytes.size() != std::size_t(kWidth) * kHeight * 8) return false;
    for (std::size_t offset = 0; offset < bytes.size(); offset += 2)
    {
        std::uint16_t bits = 0;
        std::memcpy(&bits, bytes.data() + offset, 2);
        if ((bits & 0x7c00u) == 0x7c00u) return false;
    }
    return true;
}

struct Difference
{
    double hdrMae = 0, hdrMax = 0, ldrMae = 0, ldrMax = 0;
    bool passed = false;
};

Difference Compare(const DirectionalLightingFrame& a, const DirectionalLightingFrame& b)
{
    Difference result;
    if (!a.valid || !b.valid || !FiniteHdr(a.hdr) || !FiniteHdr(b.hdr) ||
        a.rgba.size() != std::size_t(kWidth) * kHeight * 4 || a.rgba.size() != b.rgba.size())
        return result;
    const std::size_t components = std::size_t(kWidth) * kHeight * 3;
    for (std::size_t pixel = 0; pixel < std::size_t(kWidth) * kHeight; ++pixel)
    {
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            const std::size_t index = pixel * 4 + channel;
            std::uint16_t ah = 0, bh = 0;
            std::memcpy(&ah, a.hdr.data() + index * 2, 2);
            std::memcpy(&bh, b.hdr.data() + index * 2, 2);
            const double av = std::max(0.0f, DirectX::PackedVector::XMConvertHalfToFloat(ah));
            const double bv = std::max(0.0f, DirectX::PackedVector::XMConvertHalfToFloat(bh));
            const double hdr = std::abs(av / (1.0 + av) - bv / (1.0 + bv));
            const double ldr = std::abs(int(a.rgba[index]) - int(b.rgba[index])) / 255.0;
            result.hdrMae += hdr; result.hdrMax = std::max(result.hdrMax, hdr);
            result.ldrMae += ldr; result.ldrMax = std::max(result.ldrMax, ldr);
        }
    }
    result.hdrMae /= components; result.ldrMae /= components;
    result.passed = result.hdrMae <= 1e-5 && result.hdrMax <= 1e-3 &&
        result.ldrMae <= 1.0 / 255.0 && result.ldrMax <= 2.0 / 255.0;
    return result;
}

std::string Float3(const DirectX::XMFLOAT3& value)
{
    std::ostringstream out;
    out << std::setprecision(9) << '[' << value.x << ',' << value.y << ',' << value.z << ']';
    return out.str();
}
}

void Renderer::CaptureDirectionalLightingFrame()
{
    DirectionalLightingFrame* target = m_directionalCaptureTarget;
    m_directionalCaptureTarget = nullptr;
    if (!target || !m_automatedRenderMode) return;
    const auto read = [&](ID3D11Texture2D* texture, DXGI_FORMAT expected,
                          UINT pixelBytes, std::vector<std::uint8_t>& bytes)
    {
        if (!texture) return false;
        D3D11_TEXTURE2D_DESC desc = {};
        texture->GetDesc(&desc);
        if (desc.Width != kWidth || desc.Height != kHeight || desc.Format != expected ||
            desc.SampleDesc.Count != 1 || desc.MipLevels != 1 || desc.ArraySize != 1)
            return false;
        desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; desc.MiscFlags = 0;
        ComPtr<ID3D11Texture2D> staging;
        if (FAILED(m_device->CreateTexture2D(&desc, nullptr, &staging))) return false;
        m_context->CopyResource(staging.Get(), texture);
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(m_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
        bytes.resize(std::size_t(kWidth) * kHeight * pixelBytes);
        for (UINT y = 0; y < kHeight; ++y)
            std::memcpy(bytes.data() + std::size_t(y) * kWidth * pixelBytes,
                static_cast<const BYTE*>(mapped.pData) + std::size_t(y) * mapped.RowPitch,
                kWidth * pixelBytes);
        m_context->Unmap(staging.Get(), 0);
        return true;
    };
    ComPtr<ID3D11Texture2D> backBuffer;
    target->valid = SUCCEEDED(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))) &&
        read(m_hdrCloud.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, 8, target->hdr) &&
        read(backBuffer.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, 4, target->rgba) &&
        FiniteHdr(target->hdr);
}

bool Renderer::RunDirectionalLightingBaseline(Camera& camera,
    const std::filesystem::path& root, bool verifyOnly, bool finalApproved)
{
    if (m_width != kWidth || m_height != kHeight || !root.is_absolute()) return false;
#if defined(_DEBUG) || VCLOUD_STRICT_VALIDATION
    if (!verifyOnly) { Log("[Directional] Capture requires OFF Release"); return false; }
#endif
    // 00 촬영 당시의 Base 1.00을 명시적으로 복원한다.
    if (!SetBaseOctaveExtraForValidation(finalApproved ? .5f : 0.f)) return false;
    if (finalApproved && (!SetShadowHeightRefinementForValidation(false) || verifyOnly)) return false;
    const auto clean = root / L"clean";
    std::error_code error;
    if (verifyOnly)
    {
        if (!std::filesystem::is_regular_file(clean / L"complete.json")) return false;
    }
    else if (!std::filesystem::create_directory(clean, error) || error)
    {
        Log("[Directional] Refusing to overwrite existing clean directory");
        return false;
    }
    SetAutomatedRenderMode(true); SetVSyncEnabled(false); EnableNoiseLabPreviews(false);
    SetDebugMode(CloudDebugMode::Composite);
    const auto& f5 = stage13camera::Get(Stage13CameraPresetId::HeroDepth);
    camera.SetClipPlanes(stage13camera::kNearPlaneMeters, stage13camera::kFarPlaneMeters);
    camera.SetFovYDegrees(60.0f); camera.SetAspect(float(kWidth) / kHeight);
    camera.SetLookAt(f5.position, f5.target);
    const auto forward = camera.GetForward();
    // 카메라 yaw는 +Z 기준이지만 태양 방위는 +X 기준이므로 atan2(z,x)를 사용한다.
    const float viewAzimuth = std::atan2(forward.z, forward.x) * 180.0f / 3.14159265358979323846f;
    const auto wrap = [](float degrees) { return std::remainder(degrees, 360.0f); };
    struct SunCase { const char* name; float azimuth, altitude; };
    const SunCase suns[] = {
        {"azimuth-minus108p5-alt18", -108.5f, 18},
        {"azimuth-minus108p5-alt45", -108.5f, 45},
        {"azimuth-minus108p5-alt70", -108.5f, 70},
        {"front-lit-alt18", wrap(viewAzimuth + 180), 18},
        {"side-lit-alt18", wrap(viewAzimuth + 90), 18},
        {"back-lit-alt18", viewAzimuth, 18}
    };
    const char* formations[] = {"urban", "stratus", "cumulus", "mixed"};
    std::size_t count = 0;
    for (std::size_t formation = 0; formation < std::size(formations); ++formation)
    {
        for (const SunCase& sun : suns)
        {
            if (!ApplySceneConcept(Stage15ConceptPreset::UrbanFairWeather) ||
                (formation > 0 && !ApplyCloudType(TypeFormationTarget(
                    static_cast<CloudFormationType>(formation - 1))))) return false;
            // 00 원본의 강도0 설정을 내장 승인값과 독립적으로 재적용한다.
            if (!finalApproved) m_cloudShapeParameters.densityShaping = 0.0f;
            SetCloudMovementSpeedForValidation(0.0f);
            m_atmosphereParameters.sunControlMode = SunControlMode::Angles;
            m_atmosphereParameters.timePlaybackEnabled = false;
            m_atmosphereParameters.sunAzimuthDegrees = sun.azimuth;
            m_atmosphereParameters.sunElevationDegrees = sun.altitude;
            m_toneMappingParameters = ToneMappingParameters{};
            // 워밍업은 고정 시간. 프레임 수 증가가 바람/태양을 이동시키지 않는다.
            for (int frame = 0; frame < 12; ++frame) Render(camera, kTime);
            DirectionalLightingFrame current;
            m_directionalCaptureTarget = &current;
            Render(camera, kTime);
            if (!current.valid || m_directionalCaptureTarget || HasDebugLayerErrors()) return false;
            const auto caseRoot = clean / formations[formation] / sun.name;
            DirectionalLightingFrame reference;
            if (verifyOnly)
            {
                // 최초 촬영의 반복 검사 프레임까지 동일하게 실행한다. Motion의 매 프레임
                // 정규화 등 CPU 상태도 같은 프레임 수에서 비교하며 PNG는 새로 저장하지 않는다.
                Render(camera, kTime);
                reference.valid = ReadBytes(caseRoot / L"frame.rgba16f", reference.hdr,
                    std::size_t(kWidth) * kHeight * 8) &&
                    LoadPng(caseRoot / L"frame.png", reference.rgba);
            }
            else
            {
                m_directionalCaptureTarget = &reference;
                Render(camera, kTime);
            }
            const Difference difference = Compare(current, reference);
            std::ostringstream result;
            result << std::setprecision(9) << "[Directional] case=" << formations[formation]
                << '/' << sun.name << " hdr_mae=" << difference.hdrMae
                << " hdr_max=" << difference.hdrMax << " ldr_mae=" << difference.ldrMae
                << " ldr_max=" << difference.ldrMax << " pass=" << difference.passed;
            Log(result.str());
            if (!difference.passed) return false;
            // 주요 GPU 설정을 binary로 추가 기록한다. 나머지 CPU 설정은 snapshot/소스에 보존한다.
            struct State { const char* name; const void* data; std::size_t size; };
            const State states[] = {
                {"cloud", &m_cloudParameters, sizeof(m_cloudParameters)},
                {"shape", &m_cloudShapeParameters, sizeof(m_cloudShapeParameters)},
                {"domain", &m_cloudDomainParameters, sizeof(m_cloudDomainParameters)},
                {"light", &m_lightParameters, sizeof(m_lightParameters)},
                {"environment", &m_environmentParameters, sizeof(m_environmentParameters)},
                {"shadow", &m_shadowParameters, sizeof(m_shadowParameters)},
                {"noise", &m_noiseVolumeParameters, sizeof(m_noiseVolumeParameters)},
                {"weather-column", &m_weatherColumnParameters, sizeof(m_weatherColumnParameters)},
                {"tone", &m_toneMappingParameters, sizeof(m_toneMappingParameters)}
            };
            if (verifyOnly)
            {
                for (const State& state : states)
                {
                    std::vector<std::uint8_t> saved;
                    if (!ReadBytes(caseRoot / (std::string(state.name) + ".bin"), saved, state.size) ||
                        std::memcmp(saved.data(), state.data, state.size) != 0)
                    {
                        Log(std::string("[Directional] state mismatch: ") + state.name);
                        const auto* actual = static_cast<const std::uint8_t*>(state.data);
                        for (std::size_t i = 0; i < saved.size(); ++i)
                            if (saved[i] != actual[i]) Log("[Directional] byte=" + std::to_string(i) +
                                " expected=" + std::to_string(saved[i]) + " actual=" + std::to_string(actual[i]));
                        return false;
                    }
                }
            }
            else
            {
                std::filesystem::create_directories(caseRoot, error);
                if (error || !SavePng(caseRoot / L"frame.png", current.rgba) ||
                    !WriteNew(caseRoot / L"frame.rgba16f", current.hdr.data(), current.hdr.size()))
                    return false;
                for (const State& state : states)
                    if (!WriteNew(caseRoot / (std::string(state.name) + ".bin"), state.data, state.size))
                        return false;
                std::string status;
                if (!SaveCloudFormationPresetAtomic(caseRoot / L"formation.json", CustomFormationTarget(),
                        CurrentCloudFormation(), status) || !ExportNoiseLabSnapshot(caseRoot / L"snapshot"))
                    return false;
                const auto sunDirection = m_lightParameters.directionToSun;
                const float dot = forward.x * sunDirection.x + forward.y * sunDirection.y + forward.z * sunDirection.z;
                std::ostringstream settings;
                settings << std::setprecision(9) << "{\n  \"captureSchema\": 1,\n"
                    << "  \"formation\": \"" << formations[formation] << "\",\n"
                    << "  \"lightingConcept\": \"UrbanFairWeather\",\n"
                    << "  \"width\": 1920, \"height\": 1080, \"timeSeconds\": 71,\n"
                    << "  \"ui\": false, \"windSpeed\": 0, \"sunPlayback\": false,\n"
                    << "  \"cameraPosition\": " << Float3(camera.GetPosition()) << ",\n"
                    << "  \"cameraTarget\": " << Float3(camera.GetTarget()) << ",\n"
                    << "  \"fovYDegrees\": 60, \"nearMeters\": " << camera.GetNearPlane()
                    << ", \"farMeters\": " << camera.GetFarPlane() << ",\n"
                    << "  \"sunAzimuthDegrees\": " << sun.azimuth
                    << ", \"sunElevationDegrees\": " << sun.altitude << ",\n"
                    << "  \"directionToSun\": " << Float3(sunDirection)
                    << ", \"viewDotSun\": " << dot << ",\n"
                    << "  \"toneMode\": 0, \"exposureEv\": 0, \"whiteBalanceKelvin\": 6500,\n"
                    << "  \"hdrFormat\": \"RGBA16_FLOAT little-endian row-major top-down no padding\",\n"
                    << "  \"hdrRowBytes\": 15360, \"ldrFormat\": \"PNG RGBA8 sRGB output\",\n"
                    << "  \"strictValidation\": " << VCLOUD_STRICT_VALIDATION << ",\n"
                    << "  \"cloudShaderHash\": \"" << std::hex << m_cloudShaderHash
                    << "\", \"deepShadowShaderHash\": \"" << m_deepShadowShaderHash << std::dec << "\",\n"
                    << "  \"hdrHash\": \"" << Hash(current.hdr.data(), current.hdr.size())
                    << "\", \"rgbaHash\": \"" << Hash(current.rgba.data(), current.rgba.size()) << "\",\n"
                    << "  \"repeatHdrMae\": " << difference.hdrMae
                    << ", \"repeatHdrMax\": " << difference.hdrMax
                    << ", \"repeatLdrMae\": " << difference.ldrMae
                    << ", \"repeatLdrMax\": " << difference.ldrMax << "\n}\n";
                if (!WriteText(caseRoot / L"settings.json", settings.str())) return false;
            }
            ++count;
        }
    }
    if (count != 24 || HasDebugLayerErrors()) return false;
    if (!verifyOnly && !WriteText(clean / L"complete.json",
            "{\"captureSchema\":1,\"cases\":24,\"complete\":true,\"visualApproval\":false}\n"))
        return false;
    Log("[Directional] completed_cases=" + std::to_string(count) +
        " adapter=" + AdapterName() + " driver=" + DriverVersion());
    return true;
}

namespace
{
float HdrChannel(const DirectionalLightingFrame& frame, std::size_t pixel, int channel = 0)
{
    std::uint16_t half = 0;
    std::memcpy(&half, frame.hdr.data() + (pixel * 4 + channel) * 2, 2);
    return DirectX::PackedVector::XMConvertHalfToFloat(half);
}

// 기존의 L/(1+L) 진단을 HDR readback에서 역변환한다. Tone/대기 배경은 포함하지 않는다.
float DiagnosticLuminance(const DirectionalLightingFrame& frame, std::size_t pixel)
{
    const float weights[] = {0.2126f, 0.7152f, 0.0722f};
    float luminance = 0;
    for (int channel = 0; channel < 3; ++channel)
    {
        const float mapped = std::clamp(HdrChannel(frame, pixel, channel), 0.0f, 0.99999f);
        luminance += weights[channel] * mapped / (1.0f - mapped);
    }
    return luminance;
}

double Mean(const std::vector<float>& samples)
{
    double total = 0;
    for (float sample : samples) total += sample;
    return samples.empty() ? 0 : total / samples.size();
}

double Contrast(std::vector<float> samples)
{
    if (samples.empty()) return 0;
    std::sort(samples.begin(), samples.end());
    const double low = samples[(samples.size() - 1) / 10];
    const double high = samples[(samples.size() - 1) * 9 / 10];
    return (high - low) / std::max(high + low, 1e-9);
}
}

// 01: 파일을 저장하지 않는 진단. 이미지 합격이 아니라 수치/전달 계약을 검사한다.
bool Renderer::RunDirectionalLightingDiagnostics(Camera& camera)
{
    if (m_width != kWidth || m_height != kHeight) return false;
    SetAutomatedRenderMode(true); SetVSyncEnabled(false); EnableNoiseLabPreviews(false);
    const auto& f5 = stage13camera::Get(Stage13CameraPresetId::HeroDepth);
    camera.SetClipPlanes(stage13camera::kNearPlaneMeters, stage13camera::kFarPlaneMeters);
    camera.SetFovYDegrees(60); camera.SetAspect(float(kWidth) / kHeight);
    camera.SetLookAt(f5.position, f5.target);

    ComPtr<ID3DBlob> bytecode, errors;
    const auto probePath = (ShaderDirectoryForValidation() / L"../tests/DirectionalLightingProbe.hlsl").lexically_normal();
    if (FAILED(D3DCompileFromFile(probePath.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
            "main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &bytecode, &errors)))
    {
        Log("[Directional01] probe compile failed: " + probePath.string());
        if (errors) Log(std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()));
        return false;
    }
    ComPtr<ID3D11ComputeShader> probeShader;
    if (FAILED(m_device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(),
            nullptr, &probeShader))) return false;
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(DirectX::XMFLOAT4) * 5;
    desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = sizeof(DirectX::XMFLOAT4);
    ComPtr<ID3D11Buffer> output, readback;
    ComPtr<ID3D11UnorderedAccessView> uav;
    if (FAILED(m_device->CreateBuffer(&desc, nullptr, &output)) ||
        FAILED(m_device->CreateUnorderedAccessView(output.Get(), nullptr, &uav))) return false;
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; desc.MiscFlags = 0; desc.StructureByteStride = 0;
    if (FAILED(m_device->CreateBuffer(&desc, nullptr, &readback))) return false;

    const auto frame = [&](CloudDebugMode mode) {
        SetDebugMode(mode);
        DirectionalLightingFrame result;
        // 각 case의 정상 Render가 만든 Weather/Noise/Shadow/LUT/Depth를 고정한다.
        // 조명만의 불변식에 매 프레임 캐시 재생성 오차가 섞이지 않도록 Cloud/Tone만 실행한다.
        // 광원 외 입력을 다시 sanitize/파생하지 않는다. 정상 프레임의 물리 CB를 고정하고
        // b1의 debugMode 및 b3/b4의 조명만 갱신한다.
        const auto upload = [&](ID3D11Buffer* buffer, const void* data, std::size_t size) {
            D3D11_MAPPED_SUBRESOURCE mapped = {};
            if (FAILED(m_context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
            std::memcpy(mapped.pData, data, size); m_context->Unmap(buffer, 0); return true;
        };
        if (!upload(m_cloudCb.Get(), &m_cloudParameters, sizeof(m_cloudParameters)) ||
            !upload(m_lightCb.Get(), &m_lightParameters, sizeof(m_lightParameters)) ||
            !upload(m_environmentCb.Get(), &m_environmentParameters, sizeof(m_environmentParameters))) return result;
        m_constantBufferUploadValid[1] = m_constantBufferUploadValid[2] = m_constantBufferUploadValid[3] = false;
        RenderCloudPass();
        RenderToneMapPass();
        m_directionalCaptureTarget = &result;
        CaptureDirectionalLightingFrame();
        if (HasDebugLayerErrors()) result.valid = false;
        return result;
    };
    const auto directionProbe = [&](float altitude) {
        // CPU 소유 버퍼를 직접 선택하지 않고 실제 PS 슬롯에 바인딩된 버퍼를 조회한다.
        for (UINT slot : {3u, 8u, 9u})
        {
            ComPtr<ID3D11Buffer> bound;
            m_context->PSGetConstantBuffers(slot, 1, &bound);
            if (!bound) return false;
            if (slot == 8u)
            {
                ComPtr<ID3D11Buffer> cacheBound;
                m_context->CSGetConstantBuffers(slot, 1, &cacheBound);
                if (cacheBound.Get() != bound.Get())
                { Log("[Directional01] FAIL: cache CS and cloud PS use different ShadowCB"); return false; }
            }
            ID3D11Buffer* pointer = bound.Get();
            m_context->CSSetConstantBuffers(slot, 1, &pointer);
        }
        m_context->CSSetShader(probeShader.Get(), nullptr, 0);
        ID3D11UnorderedAccessView* pointer = uav.Get();
        m_context->CSSetUnorderedAccessViews(0, 1, &pointer, nullptr);
        m_context->Dispatch(1, 1, 1);
        pointer = nullptr;
        m_context->CSSetUnorderedAccessViews(0, 1, &pointer, nullptr);
        m_context->CSSetShader(nullptr, nullptr, 0);
        m_context->CopyResource(readback.Get(), output.Get());
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(m_context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
        DirectX::XMFLOAT4 gpu[5];
        std::memcpy(gpu, mapped.pData, sizeof(gpu));
        m_context->Unmap(readback.Get(), 0);
        const double radians = 3.14159265358979323846 / 180.0;
        DirectX::XMFLOAT3 expectedSun{
            float(std::cos(-108.5 * radians) * std::cos(altitude * radians)),
            float(std::sin(altitude * radians)),
            float(std::sin(-108.5 * radians) * std::cos(altitude * radians))};
        const auto basis = stage12shadow::BuildLightBasis(expectedSun);
        const DirectX::XMFLOAT3 expected[] = {expectedSun, expectedSun, basis.right, basis.up, expectedSun};
        double maximum = 0;
        for (int i = 0; i < 5; ++i)
        {
            if (!std::isfinite(gpu[i].x) || !std::isfinite(gpu[i].y) || !std::isfinite(gpu[i].z)) return false;
            maximum = std::max({maximum, double(std::abs(gpu[i].x - expected[i].x)),
                double(std::abs(gpu[i].y - expected[i].y)), double(std::abs(gpu[i].z - expected[i].z))});
        }
        const auto cpuSun = m_lightParameters.directionToSun;
        maximum = std::max({maximum, double(std::abs(cpuSun.x - expectedSun.x)),
            double(std::abs(cpuSun.y - expectedSun.y)), double(std::abs(cpuSun.z - expectedSun.z))});
        std::ostringstream report;
        report << std::setprecision(9) << "[Directional01] sun_basis_max_error=" << maximum
            << " cache_ready=" << gpu[1].w;
        Log(report.str());
        return maximum <= 1e-5 && gpu[1].w == 1 && !HasDebugLayerErrors();
    };

    const char* types[] = {"urban", "stratus", "cumulus", "mixed"};
    const char* variants[] = {"default", "indirect-off", "phase-neutral", "both-neutral"};
    std::size_t passedCases = 0;
    for (int type = 0; type < 4; ++type)
    for (float altitude : {18.0f, 45.0f, 70.0f})
    {
        if (!ApplySceneConcept(Stage15ConceptPreset::UrbanFairWeather) ||
            (type && !ApplyCloudType(TypeFormationTarget(static_cast<CloudFormationType>(type - 1))))) return false;
        SetCloudMovementSpeedForValidation(0);
        m_atmosphereParameters.sunControlMode = SunControlMode::Angles;
        m_atmosphereParameters.timePlaybackEnabled = false;
        m_atmosphereParameters.sunAzimuthDegrees = -108.5f;
        m_atmosphereParameters.sunElevationDegrees = altitude;
        Render(camera, kTime);
        const auto baseEnvironment = m_environmentParameters;
        const auto baseLight = m_lightParameters;
        if (!directionProbe(altitude)) return false;
        // 정상 전체 프레임에서 진단 전용 패스로 전환한 뒤 동일 입력으로 예열한다.
        for (CloudDebugMode mode : {CloudDebugMode::FinalDensity, CloudDebugMode::Transmittance,
                CloudDebugMode::VisibleSunTransmittance})
            if (!frame(mode).valid) return false;
        DirectionalLightingFrame originalDensity, originalViewT, originalSunT;
        std::vector<std::size_t> visiblePixels;
        for (int variant = 0; variant < 4; ++variant)
        {
            m_environmentParameters = baseEnvironment; m_lightParameters = baseLight;
            DirectionalLightingFrame controlDensity, controlViewT, controlSunT;
            if (variant > 0)
            {
                controlDensity = frame(CloudDebugMode::FinalDensity);
                controlViewT = frame(CloudDebugMode::Transmittance);
                controlSunT = frame(CloudDebugMode::VisibleSunTransmittance);
                const auto control = Compare(controlDensity, originalDensity);
                std::ostringstream note;
                note << "[Directional01] unchanged_lighting_control hdr_max=" << control.hdrMax
                    << " hdr_mae=" << control.hdrMae;
                Log(note.str());
            }
            if (variant & 1)
            {
                m_environmentParameters.physicalSkyFillScale = 0;
                m_environmentParameters.physicalGroundFillScale = 0;
                m_environmentParameters.multipleScatteringEnabled = 0;
            }
            if (variant & 2) m_lightParameters.phaseEnabled = 0;
            auto density = frame(CloudDebugMode::FinalDensity);
            auto viewT = frame(CloudDebugMode::Transmittance);
            auto sunT = frame(CloudDebugMode::VisibleSunTransmittance);
            if (!density.valid || !viewT.valid || !sunT.valid) return false;
            if (variant == 0)
            {
                originalDensity = std::move(density); originalViewT = std::move(viewT);
                originalSunT = std::move(sunT);
                double weightedT = 0, opacity = 0, highT = 0, lowT = 0;
                for (std::size_t pixel = 0; pixel < std::size_t(kWidth) * kHeight; ++pixel)
                {
                    const float t = HdrChannel(originalSunT, pixel);
                    const float w = 1 - HdrChannel(originalViewT, pixel);
                    if (t < 0 || t > 1 || w < 0 || w > 1) return false;
                    weightedT += w * t; opacity += w;
                    if (t > 0.8f) highT += w;
                    if (t < 0.2f) lowT += w;
                    if (w > 0.1f) visiblePixels.push_back(pixel);
                }
                if (opacity <= 0 || visiblePixels.empty()) return false;
                for (CloudDebugMode diagnostic : {CloudDebugMode::LightTransmittance,
                        CloudDebugMode::ShapedSunVisibility, CloudDebugMode::AmbientVisibility,
                        CloudDebugMode::DualPhaseFactor})
                {
                    const auto check = frame(diagnostic);
                    if (!check.valid) return false;
                    for (std::size_t pixel = 0; pixel < std::size_t(kWidth) * kHeight; ++pixel)
                        if (HdrChannel(check, pixel) < 0 || HdrChannel(check, pixel) > 1) return false;
                }
                std::ostringstream report;
                report << std::setprecision(7) << "[Directional01] type=" << types[type] << " altitude=" << altitude
                    << " opacity_weighted_sun_t=" << weightedT / opacity
                    << " opacity_fraction_t_above_0p8=" << highT / opacity
                    << " opacity_fraction_t_below_0p2=" << lowT / opacity
                    << " visible_pixels=" << visiblePixels.size();
                Log(report.str());
            }
            else if (!Compare(density, controlDensity).passed || !Compare(viewT, controlViewT).passed ||
                     !Compare(sunT, controlSunT).passed)
            {
                for (const auto& check : {std::make_pair("density", Compare(density, controlDensity)),
                        std::make_pair("viewT", Compare(viewT, controlViewT)),
                        std::make_pair("sunT", Compare(sunT, controlSunT))})
                {
                    std::ostringstream detail;
                    detail << std::setprecision(9) << "[Directional01] " << check.first << " hdr_mae=" << check.second.hdrMae
                        << " hdr_max=" << check.second.hdrMax << " ldr_max=" << check.second.ldrMax;
                    Log(detail.str());
                }
                Log("[Directional01] FAIL: lighting changed density/view T/sun T"); return false;
            }
            std::vector<float> direct, total(visiblePixels.size(), 0);
            const CloudDebugMode modes[] = {CloudDebugMode::AccumulatedDirectLighting,
                CloudDebugMode::AccumulatedSkyAmbient, CloudDebugMode::AccumulatedGroundBounce,
                CloudDebugMode::AccumulatedMultipleScattering, CloudDebugMode::SilverLiningContribution};
            const char* names[] = {"direct", "sky", "ground", "multiple", "silver"};
            std::ostringstream report;
            report << std::setprecision(7) << "[Directional01] type=" << types[type] << " altitude=" << altitude
                << " variant=" << variants[variant];
            for (int component = 0; component < 5; ++component)
            {
                auto outputFrame = frame(modes[component]);
                if (!outputFrame.valid) return false;
                std::vector<float> samples; samples.reserve(visiblePixels.size());
                for (std::size_t i = 0; i < visiblePixels.size(); ++i)
                {
                    const float luminance = DiagnosticLuminance(outputFrame, visiblePixels[i]);
                    if (!std::isfinite(luminance)) return false;
                    samples.push_back(luminance);
                    if (component < 4) total[i] += luminance;
                    if (((variant & 1) && component >= 1 && component <= 3) ||
                        ((variant & 2) && component == 4))
                        if (luminance > 1e-6f) { Log("[Directional01] FAIL: neutral component nonzero"); return false; }
                }
                report << ' ' << names[component] << "_mean=" << Mean(samples);
                if (component == 0) direct = std::move(samples);
            }
            report << " direct_contrast=" << Contrast(direct) << " total_contrast=" << Contrast(total)
                << " invariant=PASS";
            Log(report.str());
        }
        // albedo와 빈 형상은 가중치 진단에서 특히 혼동하기 쉬운 두 경계다.
        const auto albedoControl = frame(CloudDebugMode::VisibleSunTransmittance);
        m_lightParameters.singleScatteringAlbedo = 0;
        if (!Compare(frame(CloudDebugMode::VisibleSunTransmittance), albedoControl).passed)
        { Log("[Directional01] FAIL: albedo affected raw sun T"); return false; }
        m_cloudParameters.densityMultiplier = 0;
        const auto empty = frame(CloudDebugMode::VisibleSunTransmittance);
        if (!empty.valid) return false;
        for (std::size_t pixel = 0; pixel < std::size_t(kWidth) * kHeight; ++pixel)
            if (HdrChannel(empty, pixel) != 0) return false;
        ++passedCases;
    }
    Log("[Directional01] passed_cases=" + std::to_string(passedCases) +
        " variants_per_case=4 screenshots_written=0");
    return passedCases == 12 && !HasDebugLayerErrors();
}

// 02: 후보는 메모리에서만 비교한다. 사용자 기본값/스크린샷을 저장하지 않는다.
bool Renderer::RunDensityShapingDiagnostics(Camera& camera)
{
    if (m_width != kWidth || m_height != kHeight) return false;
    SetAutomatedRenderMode(true); SetVSyncEnabled(false); EnableNoiseLabPreviews(false);
    const auto& f5 = stage13camera::Get(Stage13CameraPresetId::HeroDepth);
    camera.SetClipPlanes(stage13camera::kNearPlaneMeters, stage13camera::kFarPlaneMeters);
    camera.SetFovYDegrees(60); camera.SetAspect(float(kWidth) / kHeight);
    camera.SetLookAt(f5.position, f5.target);
    ComPtr<ID3DBlob> bytecode, errors;
    const auto path = (ShaderDirectoryForValidation() / L"../tests/DensityShapingProbe.hlsl").lexically_normal();
    if (FAILED(D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
            "main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &bytecode, &errors)))
    {
        if (errors) Log(std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()));
        return false;
    }
    ComPtr<ID3D11ShaderReflection> reflection;
    if (FAILED(D3DReflect(bytecode->GetBufferPointer(), bytecode->GetBufferSize(),
            __uuidof(ID3D11ShaderReflection), &reflection))) return false;
    D3D11_SHADER_VARIABLE_DESC field = {};
    D3D11_SHADER_BUFFER_DESC cb = {};
    auto* shapeReflection = reflection->GetConstantBufferByName("CloudShapeCB");
    if (FAILED(shapeReflection->GetDesc(&cb)) || cb.Size != 48 ||
        FAILED(shapeReflection->GetVariableByName("densityShaping")->GetDesc(&field)) ||
        field.StartOffset != 40 || field.Size != 4) return false;
    ComPtr<ID3D11ComputeShader> probe;
    if (FAILED(m_device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(),
            nullptr, &probe))) return false;
    constexpr UINT samples = 1025;
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(DirectX::XMFLOAT4) * samples;
    desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = sizeof(DirectX::XMFLOAT4);
    ComPtr<ID3D11Buffer> output, readback;
    ComPtr<ID3D11UnorderedAccessView> uav;
    if (FAILED(m_device->CreateBuffer(&desc, nullptr, &output)) ||
        FAILED(m_device->CreateUnorderedAccessView(output.Get(), nullptr, &uav))) return false;
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; desc.MiscFlags = 0; desc.StructureByteStride = 0;
    if (FAILED(m_device->CreateBuffer(&desc, nullptr, &readback))) return false;
    double maxCurveError = 0;
    const auto curveProbe = [&](float strength) {
        ComPtr<ID3D11Buffer> bound;
        m_context->PSGetConstantBuffers(7, 1, &bound);
        if (!bound) return false;
        ID3D11Buffer* pointer = bound.Get();
        m_context->CSSetConstantBuffers(7, 1, &pointer);
        m_context->CSSetShader(probe.Get(), nullptr, 0);
        ID3D11UnorderedAccessView* view = uav.Get();
        m_context->CSSetUnorderedAccessViews(0, 1, &view, nullptr);
        m_context->Dispatch(samples, 1, 1);
        view = nullptr; m_context->CSSetUnorderedAccessViews(0, 1, &view, nullptr);
        m_context->CopyResource(readback.Get(), output.Get());
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(m_context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
        const auto* gpu = static_cast<const DirectX::XMFLOAT4*>(mapped.pData);
        bool valid = true;
        for (UINT i = 0; i < samples; ++i)
        {
            const float q = float(i) / 256;
            const float base = ShapeCloudDensity(q, strength);
            const float detail = ShapeCloudDensity(std::clamp(q - 0.12f, 0.0f, 1.0f), strength);
            maxCurveError = std::max({maxCurveError, double(std::abs(gpu[i].x - base)),
                double(std::abs(gpu[i].y - detail)),
                double(std::abs(gpu[i].z-EvaluateCommonVerticalProfile(float(i)/1024.f,m_cloudShapeParameters)))});
            valid &= std::isfinite(gpu[i].x) && std::isfinite(gpu[i].y) && std::isfinite(gpu[i].z) &&
                gpu[i].w == strength && gpu[i].y <= gpu[i].x + 1e-6f;
            if (strength == 0) valid &= gpu[i].x == q;
            if (i == 0) valid &= gpu[i].x == 0 && gpu[i].y == 0;
            if (i) valid &= gpu[i].x >= gpu[i - 1].x;
        }
        m_context->Unmap(readback.Get(), 0);
        return valid && maxCurveError <= 1e-5;
    };
    const auto frame = [&](CloudDebugMode mode) {
        SetDebugMode(mode);
        DirectionalLightingFrame result;
        m_directionalCaptureTarget = &result;
        Render(camera, kTime);
        if (HasDebugLayerErrors()) result.valid = false;
        return result;
    };
    const char* types[] = {"urban", "stratus", "cumulus", "mixed"};
    for (int type = 0; type < 4; ++type)
    for (float altitude : {18.0f, 45.0f, 70.0f})
    {
        if (!ApplySceneConcept(Stage15ConceptPreset::UrbanFairWeather) ||
            (type && !ApplyCloudType(TypeFormationTarget(static_cast<CloudFormationType>(type - 1))))) return false;
        if (m_cloudShapeParameters.densityShaping != 0.70f) return false;
        SetCloudMovementSpeedForValidation(0);
        m_atmosphereParameters.sunControlMode = SunControlMode::Angles;
        m_atmosphereParameters.timePlaybackEnabled = false;
        m_atmosphereParameters.sunAzimuthDegrees = -108.5f;
        m_atmosphereParameters.sunElevationDegrees = altitude;
        DirectionalLightingFrame original;
        std::uint64_t baseHash = 0, detailHash = 0;
        double initialT = 0;
        for (float strength : {0.0f, 0.35f, 0.70f})
        {
            m_cloudShapeParameters.densityShaping = strength;
            auto composite = frame(CloudDebugMode::Composite);
            auto sun = frame(CloudDebugMode::VisibleSunTransmittance);
            auto view = frame(CloudDebugMode::Transmittance);
            if (!composite.valid || !sun.valid || !view.valid || !curveProbe(strength)) return false;
            if (strength == 0)
            {
                original = std::move(composite);
                baseHash = m_baseNoiseVolumeHash; detailHash = m_detailNoiseVolumeHash;
            }
            else if (m_baseNoiseVolumeHash != baseHash || m_detailNoiseVolumeHash != detailHash) return false;
            double opacity = 0, sunSum = 0;
            std::size_t occupied = 0;
            for (std::size_t pixel = 0; pixel < std::size_t(kWidth) * kHeight; ++pixel)
            {
                const float t = HdrChannel(sun, pixel), w = 1 - HdrChannel(view, pixel);
                if (t < 0 || t > 1 || w < 0 || w > 1) return false;
                opacity += w; sunSum += w * t;
                if (w > 0.1f) ++occupied;
            }
            if (opacity <= 0) return false;
            const double meanT = sunSum / opacity;
            if (strength == 0) initialT = meanT;
            // 후보의 중간 밀도 증가가 실제 태양 차폐까지 전달되는지 확인한다.
            if (strength == 0.70f && meanT >= initialT - 0.005)
            { Log("[Directional02] FAIL: no measurable shadow response"); return false; }
            std::ostringstream line;
            line << std::setprecision(8) << "[Directional02] type=" << types[type]
                << " altitude=" << altitude << " strength=" << strength
                << " opacity_weighted_sun_t=" << meanT << " opacity_sum=" << opacity
                << " occupied_pixels=" << occupied;
            Log(line.str());
        }
        m_cloudShapeParameters.densityShaping = 0;
        const auto restored = frame(CloudDebugMode::Composite);
        if (!restored.valid || !Compare(restored, original).passed)
        { Log("[Directional02] FAIL: zero strength restore"); return false; }
    }
    Log("[Directional02] cases=12 candidates=3 zero_restore=PASS curve_max_error=" +
        std::to_string(maxCurveError) + " screenshots_written=0");
    return !HasDebugLayerErrors();
}

// 03: 승인 밀도 .70 고정. Base 중간 옥타브만 바꾸고 Detail Off/On을 함께 검사한다.
bool Renderer::RunBaseOctaveDiagnostics(Camera& camera)
{
    if (m_width != kWidth || m_height != kHeight) return false;
    SetAutomatedRenderMode(true); SetVSyncEnabled(false); EnableNoiseLabPreviews(false);
    const auto& f5 = stage13camera::Get(Stage13CameraPresetId::HeroDepth);
    camera.SetClipPlanes(stage13camera::kNearPlaneMeters, stage13camera::kFarPlaneMeters);
    camera.SetFovYDegrees(60); camera.SetAspect(float(kWidth) / kHeight);
    camera.SetLookAt(f5.position, f5.target);
    const auto frame = [&](CloudDebugMode mode) {
        SetDebugMode(mode); DirectionalLightingFrame result;
        m_directionalCaptureTarget = &result; Render(camera, kTime);
        if (HasDebugLayerErrors()) result.valid = false;
        return result;
    };
    if (!SetBaseOctaveExtraForValidation(0)) return false;
    const auto baseHash = m_baseNoiseVolumeHash, detailHash = m_detailNoiseVolumeHash;
    std::vector<std::uint8_t> original;
    if (!ReadNoiseVolumeBytes(true, original)) return false;
    double maxParityError = 0;
    std::size_t cases = 0;
    const char* names[] = {"urban", "stratus", "cumulus", "mixed"};
    for (float extra : {0.0f, 0.25f, 0.5f})
    {
        if (!SetBaseOctaveExtraForValidation(extra) || m_detailNoiseVolumeHash != detailHash ||
            (extra != 0 && m_baseNoiseVolumeHash == baseHash)) return false;
        std::vector<std::uint8_t> bytes;
        if (!ReadNoiseVolumeBytes(true, bytes) || bytes.size() != original.size()) return false;
        for (std::size_t i = 0; i < bytes.size(); ++i)
            if (i % 4 != 0 && bytes[i] != original[i])
            { Log("[Directional03] FAIL: Base GBA changed"); return false; }
        const UINT size = m_noiseVolumeParameters.baseResolution;
        for (UINT z = 5; z < size; z += 19)
        for (UINT y = 7; y < size; y += 23)
        for (UINT x = 3; x < size; x += 17)
        {
            const double cpu = stage13noise::BasePerlinWorley(
                {(x + 0.5) / size, (y + 0.5) / size, (z + 0.5) / size}, m_noiseVolumeParameters);
            const auto index = ((std::size_t(z) * size + y) * size + x) * 4;
            const double error = std::abs(bytes[index] / 255.0 - cpu);
            maxParityError = std::max(maxParityError, error);
            if (error > 1.0 / 255 + 1e-5)
            { Log("[Directional03] FAIL: CPU/GPU Base parity"); return false; }
        }
        for (int type = 0; type < 4; ++type)
        for (float altitude : {18.0f, 45.0f, 70.0f})
        for (int detail = 0; detail < 2; ++detail)
        {
            if (!ApplySceneConcept(Stage15ConceptPreset::UrbanFairWeather) ||
                (type && !ApplyCloudType(TypeFormationTarget(static_cast<CloudFormationType>(type - 1))))) return false;
            if (m_cloudShapeParameters.densityShaping != 0.70f ||
                m_noiseVolumeParameters.baseMidOctaveExtra != extra) return false;
            SetCloudMovementSpeedForValidation(0);
            m_atmosphereParameters.sunControlMode = SunControlMode::Angles;
            m_atmosphereParameters.timePlaybackEnabled = false;
            m_atmosphereParameters.sunAzimuthDegrees = -108.5f;
            m_atmosphereParameters.sunElevationDegrees = altitude;
            if (!detail) m_cloudParameters.detailErosionStrength = 0;
            const auto weatherHash = m_weatherMapHash;
            auto composite = frame(CloudDebugMode::Composite);
            auto direct = frame(CloudDebugMode::AccumulatedDirectLighting);
            auto sun = frame(CloudDebugMode::VisibleSunTransmittance);
            auto view = frame(CloudDebugMode::Transmittance);
            if (!composite.valid || !direct.valid || !sun.valid || !view.valid ||
                weatherHash != m_weatherMapHash) return false;
            std::vector<float> luminance;
            double sum = 0, weight = 0;
            for (std::size_t pixel = 0; pixel < std::size_t(kWidth) * kHeight; ++pixel)
            {
                const float w = 1 - HdrChannel(view, pixel), t = HdrChannel(sun, pixel);
                if (w < 0 || w > 1 || t < 0 || t > 1) return false;
                sum += w * t; weight += w;
                if (w > 0.1f) luminance.push_back(DiagnosticLuminance(direct, pixel));
            }
            if (weight <= 0 || luminance.empty()) return false;
            std::ostringstream line;
            line << std::setprecision(8) << "[Directional03] scale=" << 1 + extra
                << " type=" << names[type] << " altitude=" << altitude << " detail=" << detail
                << " sun_t=" << sum / weight << " direct_contrast=" << Contrast(luminance)
                << " visible_pixels=" << luminance.size();
            Log(line.str()); ++cases;
        }
    }
    if (!SetBaseOctaveExtraForValidation(0) || m_baseNoiseVolumeHash != baseHash ||
        m_detailNoiseVolumeHash != detailHash) return false;
    std::ostringstream result;
    result << std::setprecision(9) << "[Directional03] cases=" << cases
        << " cpu_gpu_max_error=" << maxParityError
        << " GBA_Detail_Weather=unchanged restore=PASS screenshots_written=0";
    Log(result.str());
    return cases == 72 && !HasDebugLayerErrors();
}

// 04: 실제 cache readback으로 침식 반영/원거리 불변/0 복원을 검증한다. 촬영 파일은 만들지 않는다.
// 05: 물리 입력을 고정하고 여섯 조명 계수만 독립 비교한다. 품질 채택은 사용자 판정이다.
bool Renderer::RunLightingTuningDiagnostics(Camera& camera)
{
    if (m_width != kWidth || m_height != kHeight) return false;
    SetAutomatedRenderMode(true); SetVSyncEnabled(false); EnableNoiseLabPreviews(false);
    camera.SetClipPlanes(stage13camera::kNearPlaneMeters, stage13camera::kFarPlaneMeters);
    camera.SetFovYDegrees(60); camera.SetAspect(float(kWidth) / kHeight);
    const auto& f5 = stage13camera::Get(Stage13CameraPresetId::HeroDepth);
    camera.SetLookAt(f5.position, f5.target);
    if (!SetBaseOctaveExtraForValidation(0.5f)) return false;
    wchar_t comparePath[1024]{};
    const DWORD compareLength = GetEnvironmentVariableW(L"VCLOUD_LIGHTING_COMPARE_DIR", comparePath, 1024);
    if (compareLength >= 1024) return false;
    const std::filesystem::path compareRoot(comparePath);
    std::size_t screenshots = 0;
    const auto saveComparison = [&](const DirectionalLightingFrame& value, const wchar_t* name) {
        if (!compareLength) return true;
        std::filesystem::create_directories(compareRoot);
        if (!SavePng(compareRoot / (std::wstring(name) + L".png"), value.rgba) ||
            !WriteNew(compareRoot / (std::wstring(name) + L".rgba16f"), value.hdr.data(), value.hdr.size())) return false;
        ++screenshots; return true;
    };
    const auto originalShader = m_cloudPs;
    ComPtr<ID3D11PixelShader> capShaders[2];
    const auto frame = [&](CloudDebugMode mode) {
        SetDebugMode(mode); DirectionalLightingFrame result;
        const auto upload = [&](ID3D11Buffer* buffer, const void* data, std::size_t size) {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(m_context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
            std::memcpy(mapped.pData, data, size); m_context->Unmap(buffer, 0); return true;
        };
        if (!upload(m_cloudCb.Get(), &m_cloudParameters, sizeof(m_cloudParameters)) ||
            !upload(m_lightCb.Get(), &m_lightParameters, sizeof(m_lightParameters)) ||
            !upload(m_environmentCb.Get(), &m_environmentParameters, sizeof(m_environmentParameters))) return result;
        m_constantBufferUploadValid[1] = m_constantBufferUploadValid[2] = m_constantBufferUploadValid[3] = false;
        RenderCloudPass(); RenderToneMapPass();
        m_directionalCaptureTarget = &result; CaptureDirectionalLightingFrame();
        if (HasDebugLayerErrors()) result.valid = false;
        return result;
    };
    const char* names[] = {"base", "shadow120", "sky075", "sky050", "ground075", "ground050",
        "multiple075", "multiple050", "phase+010", "phase+020", "edge125"};
    std::size_t cases = 0, capCases = 0;
    for (int type = 0; type < 4; ++type)
    for (float altitude : {18.0f, 45.0f, 70.0f})
    for (float azimuth : {-108.5f, -18.5f, 71.5f})
    {
        if (!ApplySceneConcept(Stage15ConceptPreset::UrbanFairWeather) ||
            (type && !ApplyCloudType(TypeFormationTarget(static_cast<CloudFormationType>(type - 1))))) return false;
        SetCloudMovementSpeedForValidation(0);
        m_atmosphereParameters.sunControlMode = SunControlMode::Angles;
        m_atmosphereParameters.timePlaybackEnabled = false;
        m_atmosphereParameters.sunAzimuthDegrees = azimuth;
        m_atmosphereParameters.sunElevationDegrees = altitude;
        SetDebugMode(CloudDebugMode::Composite); Render(camera, kTime);
        const auto baseLight = m_lightParameters;
        const auto baseEnvironment = m_environmentParameters;
        const auto view = frame(CloudDebugMode::Transmittance);
        if (!view.valid || m_cloudShapeParameters.densityShaping != .7f) return false;
        bool capHit = false;
        const auto summarize = [&](const DirectionalLightingFrame& composite,
                                   const DirectionalLightingFrame& direct,
                                   const DirectionalLightingFrame& multiple, const char* name) {
            if (!composite.valid || !direct.valid || !multiple.valid) return false;
            std::vector<float> values; double total = 0, directTotal = 0, multipleTotal = 0;
            for (std::size_t pixel = 0; pixel < std::size_t(kWidth) * kHeight; pixel += 4) {
                if (1 - HdrChannel(view, pixel) <= .1f) continue;
                const float luminance = .2126f * HdrChannel(composite,pixel,0) +
                    .7152f * HdrChannel(composite,pixel,1) + .0722f * HdrChannel(composite,pixel,2);
                values.push_back(luminance); total += luminance;
                directTotal += DiagnosticLuminance(direct,pixel);
                multipleTotal += DiagnosticLuminance(multiple,pixel);
            }
            if (values.empty()) return false;
            std::ostringstream line; line << "[Directional05] type=" << type << " altitude=" << altitude
                << " azimuth=" << azimuth << " candidate=" << name << " composite_mean=" << total/values.size()
                << " contrast=" << Contrast(values) << " direct_mean=" << directTotal/values.size()
                << " multiple_mean=" << multipleTotal/values.size(); Log(line.str()); return true;
        };
        for (int candidate = 0; candidate < 11; ++candidate)
        {
            m_lightParameters = baseLight; m_environmentParameters = baseEnvironment;
            switch(candidate) {
            case 1: m_lightParameters.shadowExponent *= 1.2f; break;
            case 2: m_environmentParameters.physicalSkyFillScale *= .75f; break;
            case 3: m_environmentParameters.physicalSkyFillScale *= .5f; break;
            case 4: m_environmentParameters.physicalGroundFillScale *= .75f; break;
            case 5: m_environmentParameters.physicalGroundFillScale *= .5f; break;
            case 6: m_environmentParameters.multipleScatteringAttenuation *= .75f; break;
            case 7: m_environmentParameters.multipleScatteringAttenuation *= .5f; break;
            case 8: m_lightParameters.phaseIntensity += .1f; break;
            case 9: m_lightParameters.phaseIntensity += .2f; break;
            case 10: m_lightParameters.edgeOpticalDepthScale *= 1.25f; break;
            }
            const auto composite = frame(CloudDebugMode::Composite);
            const auto direct = frame(CloudDebugMode::AccumulatedDirectLighting);
            const auto multiple = frame(CloudDebugMode::AccumulatedMultipleScattering);
            if (!summarize(composite, direct, multiple, names[candidate])) return false;
            if (type==0 && altitude==18 && azimuth==-108.5f && (candidate==0 || candidate==9) &&
                !saveComparison(composite,candidate==0?L"urban-phase020-cap25":L"urban-phase040-cap25")) return false;
            if (candidate == 0 || candidate == 8 || candidate == 9) {
                const auto phase = frame(CloudDebugMode::DualPhaseFactor);
                if (!phase.valid) return false;
                std::size_t clipped = 0;
                for (std::size_t pixel=0; pixel<std::size_t(kWidth)*kHeight;pixel+=4)
                    if (1-HdrChannel(view,pixel)>.1f && HdrChannel(phase,pixel,0)>.999f &&
                        std::abs(HdrChannel(phase,pixel,2)-.54f)<.001f) ++clipped;
                capHit |= clipped > 0;
                std::ostringstream line; line << "[Directional05] phase_intensity=" << m_lightParameters.phaseIntensity
                    << " visible_cap_pixels_stride4=" << clipped; Log(line.str());
            }
            ++cases;
        }
        if (capHit) {
            m_lightParameters = baseLight; m_lightParameters.phaseIntensity += .2f;
            m_environmentParameters = baseEnvironment;
            for (int cap=0;cap<2;++cap) {
                if (!capShaders[cap]) {
                    const D3D_SHADER_MACRO macros[] = {{"VCLOUD_TEST_PHASE_CAP", cap==0?"4.0":"8.0"}, {nullptr,nullptr}};
                    ComPtr<ID3DBlob> bytes, errors;
                    const auto path=ShaderDirectoryForValidation()/L"VolumetricClouds.hlsl";
                    if (!CompileShaderFromFile(path.wstring(), "main", "ps_5_0", bytes, false, macros)) return false;
                    if(FAILED(m_device->CreatePixelShader(bytes->GetBufferPointer(),bytes->GetBufferSize(),nullptr,&capShaders[cap]))) return false;
                }
                m_cloudPs=capShaders[cap];
                const auto composite=frame(CloudDebugMode::Composite);
                const auto direct=frame(CloudDebugMode::AccumulatedDirectLighting);
                const auto multiple=frame(CloudDebugMode::AccumulatedMultipleScattering);
                m_cloudPs=originalShader;
                if(!summarize(composite,direct,multiple,cap==0?"phase040_cap4":"phase040_cap8")) return false;
                if(type==0 && altitude==18 && azimuth==-108.5f &&
                    !saveComparison(composite,cap==0?L"urban-phase040-cap4":L"urban-phase040-cap8")) return false;
                ++capCases;
            }
        }
        m_lightParameters = baseLight; m_environmentParameters = baseEnvironment;
    }
    std::ostringstream line; line << "[Directional05] cases=" << cases << " conditional_cap_cases=" << capCases
        << " screenshots_written=" << screenshots << " defaults=unchanged"; Log(line.str());
    return cases == 396 && !HasDebugLayerErrors();
}

// 05 테스트 전용: 화면 적분과 독립된 월드 단면에서 차폐 근사 오차를 측정한다.
bool Renderer::RunSolarOcclusionDiagnostics(Camera& camera)
{
    SetAutomatedRenderMode(true); SetVSyncEnabled(false); EnableNoiseLabPreviews(false);
    const auto& f5 = stage13camera::Get(Stage13CameraPresetId::HeroDepth);
    camera.SetLookAt(f5.position, f5.target);
    camera.SetClipPlanes(stage13camera::kNearPlaneMeters, stage13camera::kFarPlaneMeters);
    camera.SetFovYDegrees(60); camera.SetAspect(float(kWidth) / kHeight);
    if (!SetBaseOctaveExtraForValidation(.5f)) return false;
    ComPtr<ID3DBlob> code;
    auto path = (ShaderDirectoryForValidation() / L"SolarOcclusionProbe.hlsl").lexically_normal();
    // 테스트 폴더 밖의 중첩 include는 표준 파일 include로 해석한다.
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if VCLOUD_STRICT_VALIDATION
    flags |= D3DCOMPILE_IEEE_STRICTNESS;
#endif
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_OPTIMIZATION_LEVEL1;
#endif
    ComPtr<ID3DBlob> errors;
    if (FAILED(D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "main", "cs_5_0", flags, 0, &code, &errors))) {
        if (errors) Log(std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()));
        return false;
    }
    ComPtr<ID3D11ComputeShader> shader;
    if (FAILED(m_device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader))) return false;
    constexpr UINT samples = 64 * 32;
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = samples * 2 * sizeof(DirectX::XMFLOAT4);
    desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; desc.StructureByteStride = 16;
    ComPtr<ID3D11Buffer> output, readback; ComPtr<ID3D11UnorderedAccessView> uav;
    if (FAILED(m_device->CreateBuffer(&desc, nullptr, &output)) ||
        FAILED(m_device->CreateUnorderedAccessView(output.Get(), nullptr, &uav))) return false;
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.MiscFlags = 0;
    desc.StructureByteStride = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(m_device->CreateBuffer(&desc, nullptr, &readback))) return false;
    // 함수 종료/실패 시에도 정상 CS를 복원한다.
    struct RestoreShader { ComPtr<ID3D11ComputeShader>& slot; ComPtr<ID3D11ComputeShader> original;
        ~RestoreShader() { slot = original; } } restore{m_deepShadowCs, m_deepShadowCs};
    wchar_t captureDirectory[32768]{};
    const DWORD captureLength = GetEnvironmentVariableW(L"VCLOUD_SOLAR_COMPARE_DIR", captureDirectory, 32768);
    if (captureLength >= 32768) return false;
    if (captureLength) std::filesystem::create_directories(captureDirectory);
    for (int variant = 0; variant < 3; ++variant)
    {
        m_deepShadowCs = restore.original;
        if (variant) {
            const D3D_SHADER_MACRO defines[] = {
                {variant == 1 ? "VCLOUD_TEST_LIGHT_SUBSTEPS" : "VCLOUD_TEST_CONSTANT_DENSITY", "4"}, {nullptr,nullptr}};
            ComPtr<ID3DBlob> cacheCode;
            if (!CompileShaderFromFile(m_deepShadowShaderPath, "main", "cs_5_0", cacheCode, false, defines) ||
                FAILED(m_device->CreateComputeShader(cacheCode->GetBufferPointer(), cacheCode->GetBufferSize(), nullptr, &m_deepShadowCs))) return false;
        }
    for (int type = 0; type < 4; ++type)
    for (float altitude : {5.f, 10.f, 18.f, 45.f, 70.f})
    for (float azimuth : {-108.5f, -18.5f, 71.5f})
    {
        if (!ApplySceneConcept(Stage15ConceptPreset::UrbanFairWeather)) return false;
        if (type && !ApplyCloudType(TypeFormationTarget(static_cast<CloudFormationType>(type - 1)))) return false;
        SetCloudMovementSpeedForValidation(0);
        m_atmosphereParameters.sunControlMode = SunControlMode::Angles;
        m_atmosphereParameters.timePlaybackEnabled = false;
        m_atmosphereParameters.sunAzimuthDegrees = azimuth;
        m_atmosphereParameters.sunElevationDegrees = altitude;
        m_lightParameters.phaseIntensity = 0;
        m_lightParameters.shadowExponent = 4;
        SetDebugMode(CloudDebugMode::AccumulatedDirectLighting);
        Render(camera, kTime);
        if (captureLength && variant < 2 && type == 0 && azimuth == -108.5f && (altitude == 5 || altitude == 18)) {
            for (auto mode : {CloudDebugMode::AccumulatedDirectLighting,
                              CloudDebugMode::Transmittance, CloudDebugMode::VisibleSunTransmittance}) {
                SetDebugMode(mode);
                DirectionalLightingFrame capture; m_directionalCaptureTarget = &capture;
                Render(camera, kTime);
                const auto stem = std::filesystem::path(captureDirectory) /
                    (L"urban-alt" + std::to_wstring(int(altitude)) + L"-variant" + std::to_wstring(variant)
                    + L"-mode" + std::to_wstring(int(mode)));
                if (!capture.valid || !SavePng(stem.wstring() + L".png", capture.rgba) ||
                    !WriteNew(stem.wstring() + L".rgba16f", capture.hdr.data(), capture.hdr.size())) return false;
            }
        }
        ID3D11ShaderResourceView* resources[] = {m_weatherMapSrv.Get(), m_baseNoiseVolumeSrv.Get(), m_detailNoiseVolumeSrv.Get()};
        m_context->CSSetShaderResources(2, 3, resources);
        ID3D11ShaderResourceView* shadows[] = {m_shadowNearSrv.Get(), m_shadowFarSrv.Get()};
        m_context->CSSetShaderResources(6, 2, shadows);
        ID3D11SamplerState* sampler = m_linearClampSampler.Get();
        m_context->CSSetSamplers(2, 1, &sampler);
        m_context->CSSetShader(shader.Get(), nullptr, 0);
        ID3D11UnorderedAccessView* pointer = uav.Get();
        m_context->CSSetUnorderedAccessViews(1, 1, &pointer, nullptr);
        m_context->Dispatch(8, 4, 1);
        pointer = nullptr; m_context->CSSetUnorderedAccessViews(1, 1, &pointer, nullptr);
        ID3D11ShaderResourceView* nullSrvs[8] = {};
        m_context->CSSetShaderResources(0, 8, nullSrvs); m_context->CSSetShader(nullptr, nullptr, 0);
        m_context->CopyResource(readback.Get(), output.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(m_context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
        std::vector<DirectX::XMFLOAT4> values(samples * 2);
        std::memcpy(values.data(), mapped.pData, desc.ByteWidth); m_context->Unmap(readback.Get(), 0);
        double cacheSum = 0, cacheMax = 0, refSum = 0, refMax = 0, coarseSum = 0;
        unsigned count = 0;
        for (unsigned i = 0; i < samples; ++i) {
            const auto v = values[2*i]; const auto extra = values[2*i+1];
            for (float x : {v.x, v.y, v.z, v.w, extra.x, extra.y, extra.z, extra.w})
                if (!std::isfinite(x) || x < 0) return false;
            if (v.x > 1 || v.y > 1 || v.z > 1 || v.w > 1) return false;
            // Near 코어와 물질이 있는 표본만: 빈 하늘이 평균 오차를 희석하지 않게 한다.
            if (extra.y < .999f || (variant != 2 && extra.z <= .001f)) continue;
            ++count;
                        const double h = (double(i / 64) + .5) / 32;
            const double distance = (1-h) * (m_shadowParameters.cloudTopMeters - m_shadowParameters.cloudBottomMeters)
                / m_shadowParameters.lightForward.y;
            const double analytic = std::exp(-std::min(9.21034037, .25 * m_cloudParameters.extinctionCoefficient * distance));
            const double c = std::abs(v.x - (variant == 2 ? analytic : v.z)), r = std::abs(v.y-v.z);
            cacheSum += c; cacheMax = std::max(cacheMax,c);
            refSum += r; refMax = std::max(refMax,r); coarseSum += std::abs(v.w-v.z);
        }
        if (!count) { Log("[Solar05] empty probe support"); return false; }
        std::ostringstream line;
        line << "[Solar05] variant=" << variant << " type=" << type << " alt=" << altitude << " az=" << azimuth << " n=" << count
             << " light_step_m=" << values[1].w << " cache_T_mae=" << cacheSum/count << " cache_T_max=" << cacheMax
             << " reference_T_mae=" << refSum/count << " reference_T_max=" << refMax << " coarse_T_mae=" << coarseSum/count;
        Log(line.str());
        if (HasDebugLayerErrors() || (variant == 2 && cacheMax > 1e-4)) return false;
    }
    }
    // 재현된 저고도 화면: XY texel과 높이 slice를 각각 두 배로 비교한다.
    // 정상 Render 뒤 물리 CB를 고정하고 임시 리소스만 바꾼다. 일반 규격은 수정하지 않는다.
    m_deepShadowCs = restore.original;
    for (int axis = 0; axis < 2; ++axis) {
        if (!ApplySceneConcept(Stage15ConceptPreset::UrbanFairWeather)) return false;
        SetCloudMovementSpeedForValidation(0);
        m_atmosphereParameters.sunControlMode = SunControlMode::Angles;
        m_atmosphereParameters.timePlaybackEnabled = false;
        m_atmosphereParameters.sunAzimuthDegrees = -108.5f;
        m_atmosphereParameters.sunElevationDegrees = 5;
        m_lightParameters.phaseIntensity = 0; m_lightParameters.shadowExponent = 4;
        SetDebugMode(CloudDebugMode::AccumulatedDirectLighting); Render(camera, kTime);
        const auto savedParams = m_shadowParameters;
        const auto nearTex = m_shadowNearTexture, farTex = m_shadowFarTexture;
        const auto nearSrv = m_shadowNearSrv, farSrv = m_shadowFarSrv;
        const auto nearUav = m_shadowNearUav, farUav = m_shadowFarUav;
        const auto restoreResources = [&]() {
            m_shadowParameters = savedParams;
            m_shadowNearTexture = nearTex; m_shadowFarTexture = farTex;
            m_shadowNearSrv = nearSrv; m_shadowFarSrv = farSrv;
            m_shadowNearUav = nearUav; m_shadowFarUav = farUav;
        };
        const auto create = [&](UINT slices, ComPtr<ID3D11Texture2D>& tex,
            ComPtr<ID3D11ShaderResourceView>& srv, ComPtr<ID3D11UnorderedAccessView>& view) {
            D3D11_TEXTURE2D_DESC d{}; d.Width = d.Height = axis == 0 ? 1024 : 512;
            d.MipLevels = 1; d.ArraySize = slices; d.Format = DXGI_FORMAT_R32_FLOAT;
            d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT;
            d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            return SUCCEEDED(m_device->CreateTexture2D(&d, nullptr, tex.ReleaseAndGetAddressOf())) &&
                SUCCEEDED(m_device->CreateShaderResourceView(tex.Get(), nullptr, srv.ReleaseAndGetAddressOf())) &&
                SUCCEEDED(m_device->CreateUnorderedAccessView(tex.Get(), nullptr, view.ReleaseAndGetAddressOf()));
        };
        m_shadowParameters.nearResolution = m_shadowParameters.farResolution = axis == 0 ? 1024 : 512;
        m_shadowParameters.nearSliceCount = axis == 1 ? 159 : 80;
        m_shadowParameters.farSliceCount = axis == 1 ? 79 : stage12shadow::kFarSlices;
        if (!create(m_shadowParameters.nearSliceCount, m_shadowNearTexture, m_shadowNearSrv, m_shadowNearUav) ||
            !create(m_shadowParameters.farSliceCount, m_shadowFarTexture, m_shadowFarSrv, m_shadowFarUav)) {
            restoreResources(); return false;
        }
        ID3D11ShaderResourceView* nullSrvs[16] = {};
        m_context->PSSetShaderResources(0, 16, nullSrvs);
        DispatchDeepShadowCaches(); RenderCloudPass(); RenderToneMapPass();
        DirectionalLightingFrame capture; m_directionalCaptureTarget = &capture; CaptureDirectionalLightingFrame();
        restoreResources();
        if (!capture.valid || HasDebugLayerErrors()) return false;
        if (captureLength) {
            const auto stem = std::filesystem::path(captureDirectory) / (axis == 0 ? L"urban-alt5-xy1024" : L"urban-alt5-height159-79");
            if (!SavePng(stem.wstring()+L".png", capture.rgba) ||
                !WriteNew(stem.wstring()+L".rgba16f", capture.hdr.data(), capture.hdr.size())) return false;
        }
    }
    DirectionalLightingFrame reference50;
    for (int control = 0; control < 4; ++control) {
        const auto normalPs = m_cloudPs;
        const D3D_SHADER_MACRO defines[] = {{control == 0 ? "VCLOUD_TEST_SUN_UNOCCLUDED" : (control == 1 ? "VCLOUD_TEST_VIEW_STEP_METERS" : "VCLOUD_TEST_SOLAR_REFERENCE_STEP"), control == 0 ? "1" : (control == 2 ? "50.0" : "25.0")},{nullptr,nullptr}};
        ComPtr<ID3DBlob> psCode; ComPtr<ID3D11PixelShader> testPs;
        if (!CompileShaderFromFile(m_cloudShaderPath, "main", "ps_5_0", psCode, false, defines) ||
            FAILED(m_device->CreatePixelShader(psCode->GetBufferPointer(), psCode->GetBufferSize(), nullptr, &testPs))) return false;
        m_cloudPs = testPs;
        DirectionalLightingFrame capture; m_directionalCaptureTarget = &capture; Render(camera, kTime);
        m_cloudPs = normalPs;
        if (control == 2) reference50 = capture;
        if (control == 3) {
            double inside = 0, outside = 0; std::size_t ni = 0, no = 0;
            for (UINT y = 0; y < kHeight; y += 4) for (UINT x = 0; x < kWidth; x += 4)
            for (int c = 0; c < 3; ++c) {
                const double e = std::abs(HdrChannel(reference50, y*kWidth+x,c)-HdrChannel(capture,y*kWidth+x,c));
                if (x >= 550 && x < 750 && y >= 640 && y < 710) { inside += e; ++ni; }
                else { outside += e; ++no; }
            }
            std::ostringstream line; line << "[Solar05] ROI convergence hdr_mae=" << inside/ni
                << " outside_hdr_mae=" << outside/no; Log(line.str());
            if (inside/ni > 1e-5 || outside/no > 1e-5) return false;
        }
        if (!capture.valid || HasDebugLayerErrors()) return false;
        if (captureLength) {
            const auto stem = std::filesystem::path(captureDirectory) / (control == 0 ? L"urban-alt5-sun-unoccluded" : (control == 1 ? L"urban-alt5-view25m" : (control == 2 ? L"urban-alt5-roi-light50m" : L"urban-alt5-roi-light25m")));
            if (!SavePng(stem.wstring()+L".png", capture.rgba) ||
                !WriteNew(stem.wstring()+L".rgba16f", capture.hdr.data(), capture.hdr.size())) return false;
        }
    }
    Log("[Solar05] 180 cases finite; accuracy metrics are observations, not visual approval.");
    return true;
}

bool Renderer::RunSolarBandingDiagnostics(Camera& camera)
{
    // 기본 회귀는 5도. 다른 양의 저고도 참조는 새 폴더에서 선택적으로 비교한다.
    float referenceAltitude = 5.f;
    wchar_t altitudeText[64]{};
    DWORD altitudeLength = GetEnvironmentVariableW(L"VCLOUD_SOLAR_BANDING_ALTITUDE", altitudeText, 64);
    if (altitudeLength >= 64) return false;
    if (altitudeLength) {
        wchar_t* end = nullptr;
        referenceAltitude = static_cast<float>(std::wcstod(altitudeText, &end));
        if (end == altitudeText || *end || !std::isfinite(referenceAltitude) ||
            referenceAltitude < 3.0f || referenceAltitude > 70.f) return false;
    }
    Log("[SolarBanding] full_reference_altitude=" + std::to_string(referenceAltitude));
    SetAutomatedRenderMode(true); SetVSyncEnabled(false); EnableNoiseLabPreviews(false);
    const auto& f5 = stage13camera::Get(Stage13CameraPresetId::HeroDepth);
    camera.SetLookAt(f5.position, f5.target);
    camera.SetClipPlanes(stage13camera::kNearPlaneMeters, stage13camera::kFarPlaneMeters);
    camera.SetFovYDegrees(60); camera.SetAspect(float(kWidth)/kHeight);
    if (!SetBaseOctaveExtraForValidation(.5f) || !ApplySceneConcept(Stage15ConceptPreset::UrbanFairWeather)) return false;
    SetCloudMovementSpeedForValidation(0);
    m_atmosphereParameters.sunControlMode = SunControlMode::Angles;
    m_atmosphereParameters.timePlaybackEnabled = false;
    m_atmosphereParameters.sunAzimuthDegrees = -108.5f;
    m_atmosphereParameters.sunElevationDegrees = referenceAltitude;
    m_lightParameters.phaseIntensity = 0; m_lightParameters.shadowExponent = 4;
    SetDebugMode(CloudDebugMode::AccumulatedDirectLighting); Render(camera,kTime);
    D3D11_RASTERIZER_DESC raster{}; raster.FillMode=D3D11_FILL_SOLID; raster.CullMode=D3D11_CULL_NONE;
    raster.DepthClipEnable=TRUE; raster.ScissorEnable=TRUE;
    ComPtr<ID3D11RasterizerState> scissor;
    if (FAILED(m_device->CreateRasterizerState(&raster,&scissor))) return false;
    wchar_t directory[32768]{};
    DWORD length=GetEnvironmentVariableW(L"VCLOUD_SOLAR_BANDING_DIR",directory,32768);
    if (length>=32768) return false;
    if (length) std::filesystem::create_directories(directory);
    const auto normalPs=m_cloudPs;
    struct Restore { ComPtr<ID3D11PixelShader>& slot; ComPtr<ID3D11PixelShader> value;
        ~Restore(){slot=value;} } restore{m_cloudPs,normalPs};
    DirectionalLightingFrame reference50,reference25,baseline;
    const auto normalCs=m_deepShadowCs;
    const auto currentShadow=m_shadowParameters;
    struct RestoreCs { ComPtr<ID3D11ComputeShader>& slot; ComPtr<ID3D11ComputeShader> value;
        ~RestoreCs(){slot=value;} } restoreCs{m_deepShadowCs,normalCs};
    const char* names[]={"baseline","reference50","reference25","near-only","far-only","unoccluded","projected-cache","corrected-cache","corrected-height2x","corrected-xy2x"};
    for(int variant=0;variant<10;++variant) {
        m_shadowParameters=currentShadow;
        std::vector<D3D_SHADER_MACRO> macros;
        if(variant<6) {
            macros.push_back({"VCLOUD_TEST_LEGACY_SHADOW","1"});
            const auto basis=stage12shadow::BuildLightBasis(m_lightParameters.directionToSun);
            auto raw=camera.GetPosition(); raw.y=.5f*(currentShadow.cloudBottomMeters+currentShadow.cloudTopMeters);
            m_shadowParameters.nearCenter=stage12shadow::SnappedCenter(raw,basis,currentShadow.nearWidthMeters,512);
            m_shadowParameters.farCenter=stage12shadow::SnappedCenter(raw,basis,currentShadow.farWidthMeters,512);
        }
        if(variant==1 || variant==2) {
            macros.push_back({"VCLOUD_TEST_SOLAR_REFERENCE_STEP",variant==1?"50.0":"25.0"});
            macros.push_back({"VCLOUD_TEST_SOLAR_REFERENCE_FULL","1"});
        }
        if(variant==3) macros.push_back({"VCLOUD_TEST_CASCADE_NEAR","1"});
        if(variant==4) macros.push_back({"VCLOUD_TEST_CASCADE_FAR","1"});
        if(variant==5) macros.push_back({"VCLOUD_TEST_SUN_UNOCCLUDED","1"});
        if(variant==6) macros.push_back({"VCLOUD_TEST_LIGHT_SUBSTEPS","1"});
        macros.push_back({nullptr,nullptr});
        ComPtr<ID3DBlob> psCode,csCode;
        if(!CompileShaderFromFile(m_cloudShaderPath,"main","ps_5_0",psCode,false,macros.data()) ||
            !CompileShaderFromFile(m_deepShadowShaderPath,"main","cs_5_0",csCode,false,macros.data()) ||
            FAILED(m_device->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,&m_cloudPs)) ||
            FAILED(m_device->CreateComputeShader(csCode->GetBufferPointer(),csCode->GetBufferSize(),nullptr,&m_deepShadowCs))) return false;
        const auto nearTex = m_shadowNearTexture, farTex = m_shadowFarTexture;
        const auto nearSrv = m_shadowNearSrv, farSrv = m_shadowFarSrv;
        const auto nearUav = m_shadowNearUav, farUav = m_shadowFarUav;
        const auto restoreResources = [&]() {
            m_shadowNearTexture = nearTex; m_shadowFarTexture = farTex;
            m_shadowNearSrv = nearSrv; m_shadowFarSrv = farSrv;
            m_shadowNearUav = nearUav; m_shadowFarUav = farUav;
        };
        if (variant >= 8) {
            const UINT resolution = variant == 9 ? 1024 : 512;
            m_shadowParameters.nearResolution = m_shadowParameters.farResolution = resolution;
            m_shadowParameters.nearSliceCount = variant == 8 ? 159 : 80;
            m_shadowParameters.farSliceCount = variant == 8 ? 79 : stage12shadow::kFarSlices;
            const auto create = [&](UINT slices, ComPtr<ID3D11Texture2D>& texture,
                ComPtr<ID3D11ShaderResourceView>& srv, ComPtr<ID3D11UnorderedAccessView>& uav) {
                D3D11_TEXTURE2D_DESC d{}; d.Width = d.Height = resolution;
                d.MipLevels = 1; d.ArraySize = slices; d.Format = DXGI_FORMAT_R32_FLOAT;
                d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT;
                d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
                return SUCCEEDED(m_device->CreateTexture2D(&d,nullptr,texture.ReleaseAndGetAddressOf())) &&
                    SUCCEEDED(m_device->CreateShaderResourceView(texture.Get(),nullptr,srv.ReleaseAndGetAddressOf())) &&
                    SUCCEEDED(m_device->CreateUnorderedAccessView(texture.Get(),nullptr,uav.ReleaseAndGetAddressOf()));
            };
            if (!create(m_shadowParameters.nearSliceCount,m_shadowNearTexture,m_shadowNearSrv,m_shadowNearUav) ||
                !create(m_shadowParameters.farSliceCount,m_shadowFarTexture,m_shadowFarSrv,m_shadowFarUav)) {
                restoreResources(); return false;
            }
        }
        DispatchDeepShadowCaches();
        // CB/캐시/시점은 한 번만 준비하고 각 참조에서 같은 입력을 쓴다.
        RenderCloudPass(true); m_context->RSSetState(scissor.Get());
        for(LONG y=0;y<LONG(kHeight);y+=64) for(LONG x=0;x<LONG(kWidth);x+=128) {
            const D3D11_RECT rect{x,y,std::min(x+128,LONG(kWidth)),std::min(y+64,LONG(kHeight))};
            m_context->RSSetScissorRects(1,&rect); m_context->Draw(3,0);
        }
        m_context->RSSetState(nullptr); UnbindCloudShaderResources(14); RenderToneMapPass();
        DirectionalLightingFrame capture; m_directionalCaptureTarget=&capture; CaptureDirectionalLightingFrame();
        restoreResources();
        if(!capture.valid || HasDebugLayerErrors()) return false;
        if(variant==0) baseline=capture;
        if(variant==1) reference50=capture;
        if(variant==2) reference25=capture;
        const auto difference=Compare(baseline,capture);
        std::ostringstream line; line << "[SolarBanding] " << names[variant]
            << " vs_baseline_hdr_mae=" << difference.hdrMae << " max=" << difference.hdrMax; Log(line.str());
        if(variant==2) {
            auto d=Compare(reference50,capture); std::ostringstream convergence;
            convergence << "[SolarBanding] full_reference_convergence mae=" << d.hdrMae << " max=" << d.hdrMax;
            Log(convergence.str()); if(d.hdrMae>1e-5 || d.hdrMax>1e-3) return false;
        }
        if(variant>=6) {
            const auto oldError=Compare(baseline,reference25), newError=Compare(capture,reference25);
            std::ostringstream e; e << "[SolarBanding] " << names[variant] << " vs_reference mae=" << newError.hdrMae << " max=" << newError.hdrMax
                << " baseline_mae=" << oldError.hdrMae; Log(e.str());
            // 고정 재현 사례에서 개선이 실제 참조에 가까워졌는지 회귀 검사한다.
            // 검은 하늘을 포함한 통계이므로 절대 화질 기준으로 사용하지 않는다.
            if (variant == 7 && referenceAltitude == 5.f && (newError.hdrMae > oldError.hdrMae * .5 ||
                                newError.hdrMax > oldError.hdrMax * .5)) return false;
        }
        if(length) {
            auto path=std::filesystem::path(directory)/names[variant];
            if(!SavePng(path.wstring()+L".png",capture.rgba) || !WriteNew(path.wstring()+L".rgba16f",capture.hdr.data(),capture.hdr.size())) return false;
        }
    }
    m_cloudPs = normalPs;
    m_deepShadowCs = normalCs;
    // 일반 Render 전체를 거친다. 아래 3도는 기존 cone 경로이며 참조 수렴 검사와 구분한다.
    UINT cases = 0;
    for (bool refined : {false, true})
    {
    if (!SetShadowHeightRefinementForValidation(refined)) return false;
    for (int type = 0; type < 4; ++type)
    for (float altitude : {0.f, 1.f, 2.9f, 3.1f, 5.f, 10.f, 18.f, 45.f, 70.f})
    for (float azimuth : {-108.5f, -18.5f, 71.5f})
    {
        if (!ApplySceneConcept(Stage15ConceptPreset::UrbanFairWeather)) return false;
        if (type && !ApplyCloudType(TypeFormationTarget(static_cast<CloudFormationType>(type-1)))) return false;
        SetCloudMovementSpeedForValidation(0);
        m_atmosphereParameters.sunControlMode = SunControlMode::Angles;
        m_atmosphereParameters.timePlaybackEnabled = false;
        m_atmosphereParameters.sunAzimuthDegrees = azimuth;
        m_atmosphereParameters.sunElevationDegrees = altitude;
        m_lightParameters.phaseIntensity = 0;
        m_lightParameters.shadowExponent = 4;
        for (auto mode : {CloudDebugMode::AccumulatedDirectLighting,
                          CloudDebugMode::VisibleSunTransmittance, CloudDebugMode::Composite})
        {
            SetDebugMode(mode);
            DirectionalLightingFrame frame;
            m_directionalCaptureTarget = &frame;
            Render(camera, kTime);
            if (!frame.valid || HasDebugLayerErrors() ||
                bool(m_shadowParameters.cacheReady) != (altitude >= 3.f)) return false;
            if (length && type == 0 && azimuth == -108.5f && altitude <= 10 &&
                mode == CloudDebugMode::AccumulatedDirectLighting)
            {
                const auto path = std::filesystem::path(directory) /
                    ((refined ? "refined-current-alt" : "current-alt") + std::to_string(altitude) + "-direct");
                if (!SavePng(path.wstring()+L".png", frame.rgba) ||
                    !WriteNew(path.wstring()+L".rgba16f", frame.hdr.data(), frame.hdr.size())) return false;
            }
            ++cases;
        }
    }
    }
    if (!SetShadowHeightRefinementForValidation(false)) return false;
    Log("[SolarBanding] runtime_finite_cases=" + std::to_string(cases) +
        " height_variants=2 formations=4 altitudes=9 azimuths=3 modes=3; cone_below_3=unchanged; visual_approval=pending");
    return true;
}

bool Renderer::RunSolarTransitionDiagnostics(Camera& camera)
{
    SetAutomatedRenderMode(true); SetVSyncEnabled(false); EnableNoiseLabPreviews(false);
    if (!SetShadowHeightRefinementForValidation(false) || !SetBaseOctaveExtraForValidation(.5f)) return false;
    const auto& f5 = stage13camera::Get(Stage13CameraPresetId::HeroDepth);
    camera.SetLookAt(f5.position, f5.target);
    camera.SetClipPlanes(stage13camera::kNearPlaneMeters, stage13camera::kFarPlaneMeters);
    camera.SetFovYDegrees(60); camera.SetAspect(float(kWidth)/kHeight);
    const auto currentCloud = m_cloudPs, currentScene = m_scenePs;
    struct Restore {
        ComPtr<ID3D11PixelShader>& cloud; ComPtr<ID3D11PixelShader>& scene;
        ComPtr<ID3D11PixelShader> savedCloud, savedScene;
        ~Restore() { cloud=savedCloud; scene=savedScene; }
    } restore{m_cloudPs,m_scenePs,currentCloud,currentScene};
    const D3D_SHADER_MACRO macros[]={{"VCLOUD_TEST_LEGACY_SUN_TRANSITION","1"},{nullptr,nullptr}};
    ComPtr<ID3DBlob> code; ComPtr<ID3D11PixelShader> legacyCloud,legacyScene;
    if (!CompileShaderFromFile(m_cloudShaderPath,"main","ps_5_0",code,false,macros) ||
        FAILED(m_device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&legacyCloud))) return false;
    if (!CompileShaderFromFile((ShaderDirectoryForValidation()/L"DiagnosticScene.hlsl").wstring(),"PSMain","ps_5_0",code,false,macros) ||
        FAILED(m_device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&legacyScene))) return false;
    wchar_t directory[32768]{};
    const DWORD length=GetEnvironmentVariableW(L"VCLOUD_SOLAR_TRANSITION_DIR",directory,32768);
    if (length>=32768) return false;
    if (length) std::filesystem::create_directories(directory);
    const auto setup = [&](int type, float azimuth) {
        if (!ApplySceneConcept(Stage15ConceptPreset::UrbanFairWeather)) return false;
        if (type && !ApplyCloudType(TypeFormationTarget(static_cast<CloudFormationType>(type-1)))) return false;
        SetCloudMovementSpeedForValidation(0);
        m_atmosphereParameters.sunControlMode=SunControlMode::Angles;
        m_atmosphereParameters.timePlaybackEnabled=false;
        m_atmosphereParameters.sunAzimuthDegrees=azimuth;
        m_lightParameters.phaseIntensity=0; m_lightParameters.shadowExponent=4;
        return true;
    };
    const auto frame = [&](float altitude, CloudDebugMode mode, bool legacy) {
        m_cloudPs=legacy?legacyCloud:currentCloud; m_scenePs=legacy?legacyScene:currentScene;
        m_atmosphereParameters.sunElevationDegrees=altitude; SetDebugMode(mode);
        DirectionalLightingFrame result; m_directionalCaptureTarget=&result; Render(camera,kTime);
        return result;
    };
    const auto save = [&](const std::string& name, const DirectionalLightingFrame& image) {
        if (!length) return true;
        const auto path=std::filesystem::path(directory)/name;
        return SavePng(path.wstring()+L".png",image.rgba) &&
            WriteNew(path.wstring()+L".rgba16f",image.hdr.data(),image.hdr.size());
    };
    UINT boundaries=0;
    for (int type=0;type<4;++type)
    for (float azimuth : {-108.5f,-18.5f,71.5f})
    {
        if (!setup(type,azimuth)) return false;
        for (float boundary : {3.f,5.f})
        for (auto mode : {CloudDebugMode::VisibleSunTransmittance,
                          CloudDebugMode::AccumulatedDirectLighting,CloudDebugMode::Composite})
        {
            Difference differences[2];
            for (int legacy=0;legacy<2;++legacy)
            {
                const auto a=frame(boundary-.001f,mode,legacy!=0);
                const auto b=frame(boundary+.001f,mode,legacy!=0);
                if (!a.valid || !b.valid || HasDebugLayerErrors()) return false;
                differences[legacy]=Compare(a,b);
                if (length && type==0 && azimuth==-108.5f && boundary==3 &&
                    mode==CloudDebugMode::Composite)
                {
                    const std::string prefix=legacy?"old-":"new-";
                    if (!save(prefix+"alt2.999-composite",a) || !save(prefix+"alt3.001-composite",b)) return false;
                }
            }
            std::ostringstream line;
            line << "[SolarTransition] type=" << type << " azimuth=" << azimuth << " boundary=" << boundary
                << " mode=" << int(mode) << " new_mae=" << differences[0].hdrMae
                << " old_mae=" << differences[1].hdrMae << " new_max=" << differences[0].hdrMax
                << " old_max=" << differences[1].hdrMax;
            Log(line.str());
            // 3도에서 즉시교체 오차를 최소절반 이하로 줄인다. 물리적 각도/LUT 변화 여유는 별도 둔다.
            if (boundary==3 && differences[0].hdrMae > differences[1].hdrMae*.5+1e-5) return false;
            if (boundary==5 && differences[0].hdrMae > differences[1].hdrMae+1e-5) return false;
            ++boundaries;
        }
        // 경로를 바꿔도 시선 밀도 적분은 불변이고 전환 밖의 결과는 기존과 같아야 한다.
        for (float altitude : {1.f,2.5f,3.5f,4.f,5.1f,10.f})
        {
            const auto oldView=frame(altitude,CloudDebugMode::Transmittance,true);
            const auto newView=frame(altitude,CloudDebugMode::Transmittance,false);
            const auto viewDifference=Compare(oldView,newView);
            if (!viewDifference.passed) {
                std::ostringstream line; line << "[SolarTransition] FAIL view invariant altitude=" << altitude
                    << " valid=" << oldView.valid << "/" << newView.valid << " hdr_mae=" << viewDifference.hdrMae
                    << " hdr_max=" << viewDifference.hdrMax << " ldr_max=" << viewDifference.ldrMax;
                Log(line.str()); return false;
            }
            const auto oldImage=frame(altitude,CloudDebugMode::Composite,true);
            const auto newImage=frame(altitude,CloudDebugMode::Composite,false);
            if (!oldImage.valid || !newImage.valid || HasDebugLayerErrors()) return false;
            // 서로 다른 PS 컴파일 변형이다. OFF Release는 bit-exact가 아니므로
            // HDR 평균과 표시 RGB 허용오차로 판정하고 HDR 최대도 실패 로그에 남긴다.
            const auto outsideDifference=Compare(oldImage,newImage);
            const bool outsidePassed=outsideDifference.hdrMae<=1e-5 &&
                outsideDifference.ldrMae<=1.0/255.0 && outsideDifference.ldrMax<=2.0/255.0;
            if ((altitude<3 || altitude>5) && !outsidePassed) {
                const auto d=Compare(oldImage,newImage);
                std::ostringstream line; line << "[SolarTransition] FAIL outside invariant altitude=" << altitude
                    << " hdr_mae=" << d.hdrMae << " hdr_max=" << d.hdrMax << " ldr_max=" << d.ldrMax;
                Log(line.str()); return false;
            }
        }
    }
    Log("[SolarTransition] boundary_cases="+std::to_string(boundaries)+" outside_transition_and_density=PASS");

    // 하부 명암: 형상/차폐/간접광을 바꾸지 않고 성분만 관찰한다.
    for (int type=0;type<4;++type)
    for (float altitude : {5.f,18.f,45.f})
    {
        if (!setup(type,-108.5f)) return false;
        m_lightParameters.phaseIntensity=.20f; m_lightParameters.shadowExponent=1.35f;
        const auto view=frame(altitude,CloudDebugMode::Transmittance,false);
        if (!view.valid) return false;
        for (auto mode : {CloudDebugMode::Transmittance,CloudDebugMode::VisibleSunTransmittance,
            CloudDebugMode::AccumulatedDirectLighting,CloudDebugMode::AccumulatedSkyAmbient,
            CloudDebugMode::AccumulatedGroundBounce,CloudDebugMode::AccumulatedMultipleScattering,
            CloudDebugMode::Composite})
        {
            const auto image=frame(altitude,mode,false);
            if (!image.valid || HasDebugLayerErrors()) return false;
            double lowerMean=0; UINT count=0;
            for (UINT y=600;y<800;y+=4) for (UINT x=0;x<kWidth;x+=4)
            {
                const UINT pixel=y*kWidth+x;
                if (1-HdrChannel(view,pixel)>.1f) { lowerMean+=HdrChannel(image,pixel); ++count; }
            }
            std::ostringstream line;
            line << "[LowerCloud] type=" << type << " altitude=" << altitude << " mode=" << int(mode)
                << " lower_screen_samples=" << count << " raw_debug_R_mean=" << (count?lowerMean/count:0);
            Log(line.str());
            if (length && (type==1 || type==2) && altitude==18 &&
                !save("lower-type"+std::to_string(type)+"-alt18-mode"+std::to_string(int(mode)),image)) return false;
        }
    }
    // 전환 구간은 두 경로를 계산하므로 별도 직렬 GPU raw 측정. 캡처 타겟/UI/VSync는 꺼져 있다.
#if !defined(_DEBUG)
    for (auto cameraId : {Stage13CameraPresetId::HeroDepth,Stage13CameraPresetId::CloudOverview})
    for (float altitude : {2.5f,3.5f,4.5f,5.5f})
    {
        if (!setup(0,-108.5f)) return false;
        const auto& preset=stage13camera::Get(cameraId); camera.SetLookAt(preset.position,preset.target);
        m_lightParameters.phaseIntensity=.20f; m_lightParameters.shadowExponent=1.35f;
        m_atmosphereParameters.sunElevationDegrees=altitude;
        m_cloudPs=currentCloud; m_scenePs=currentScene; SetDebugMode(CloudDebugMode::Composite);
        for (UINT i=0;i<60;++i) Render(camera,kTime);
        std::vector<double> cloud,frames;
        for (UINT i=0;i<180 && cloud.size()<120;++i)
        {
            Render(camera,kTime);
            const auto& timing=TimingSnapshot();
            if (timing.gpuValid && timing.rawGpuFrameMs>0) {
                cloud.push_back(timing.rawGpuCloudMs); frames.push_back(timing.rawGpuFrameMs);
            }
        }
        if (cloud.size()!=120) return false;
        std::sort(cloud.begin(),cloud.end()); std::sort(frames.begin(),frames.end());
        const std::size_t p95=std::size_t(std::ceil(.95*cloud.size()))-1;
        std::ostringstream line;
        line << "[SolarTransitionPerformance] camera=" << preset.diagnosticName << " altitude=" << altitude
            << " cloud_p95_ms=" << cloud[p95] << " frame_p95_ms=" << frames[p95];
        Log(line.str());
        if (cloud[p95]>10 || frames[p95]>16.67 || HasDebugLayerErrors()) return false;
    }
#endif
    return true;
}

// 06 후보의 근거리/태양 전환/카메라 이동 검사. 성능 측정과 화면 승인은 별도다.
bool Renderer::RunFarHeightStability(Camera& camera, const std::filesystem::path& root)
{
    SetAutomatedRenderMode(true); SetVSyncEnabled(false); EnableNoiseLabPreviews(false);
    if(!SetBaseOctaveExtraForValidation(.5f))return false;
    camera.SetClipPlanes(stage13camera::kNearPlaneMeters,stage13camera::kFarPlaneMeters);
    camera.SetFovYDegrees(60);camera.SetAspect(float(kWidth)/kHeight);
    std::ostringstream csv;
    csv<<"type,near_slices,path,index,altitude,x,z,ldr_delta_255,anchor_near_weight\n";
    for(int type=0;type<4;++type)for(bool farOnly:{true,false})for(int path=0;path<3;++path) {
        const bool focused=GetEnvironmentVariableW(L"VCLOUD_RIM_STABILITY_FOCUS",nullptr,0)!=0;
        if(focused&&(type!=3||path!=1))continue;
        if(!SetShadowHeightRefinementForValidation(true,farOnly)||!ApplySceneConcept(Stage15ConceptPreset::UrbanFairWeather))return false;
        const auto approvedLight=m_lightParameters;
        if(type&&!ApplyCloudType(TypeFormationTarget(static_cast<CloudFormationType>(type-1))))return false;
        if(m_lightParameters.shadowExponent!=approvedLight.shadowExponent||
           m_lightParameters.rimIntensity!=approvedLight.rimIntensity||
           m_lightParameters.rimDepthScale!=approvedLight.rimDepthScale||m_cloudShapeParameters.densityShaping!=.7f)return false;
        m_cloudParameters.extinctionCoefficient*=4;
        m_constantBufferUploadValid[1]=false;
        SetCloudMovementSpeedForValidation(0);
        m_atmosphereParameters.sunControlMode=SunControlMode::Angles;
        m_atmosphereParameters.timePlaybackEnabled=false;
        m_atmosphereParameters.sunAzimuthDegrees=-108.5f;
        m_atmosphereParameters.sunElevationDegrees=path==2?5.f:2.5f;
        m_toneMappingParameters=ToneMappingParameters{};
        SetDebugMode(CloudDebugMode::Composite);
        const auto& preset=stage13camera::Get(path==0?Stage13CameraPresetId::HeroDepth:Stage13CameraPresetId::CloudOverview);
        camera.SetLookAt(preset.position,preset.target);
        for(int warm=0;warm<60;++warm)Render(camera,kTime);
        const auto right=m_shadowParameters.lightRight;
        const DirectX::XMFLOAT3 anchor{preset.position.x,
            (m_shadowParameters.cloudBottomMeters+m_shadowParameters.cloudTopMeters)*.5f,preset.position.z};
        std::vector<std::uint8_t> previous;
        float minWeight=1,maxWeight=0;
        for(int index=0;index<=60;++index) {
            if(path<2)m_atmosphereParameters.sunElevationDegrees=2.5f+.1f*float(index<=30?index:60-index);
            else {
                const float offset=200.f*index;
                camera.SetLookAt({preset.position.x+right.x*offset,preset.position.y,preset.position.z+right.z*offset},
                    {preset.target.x+right.x*offset,preset.target.y,preset.target.z+right.z*offset});
            }
            Render(camera,kTime);
            if(focused&&GetEnvironmentVariableW(L"VCLOUD_RIM_STABILITY_SETTLED",nullptr,0)!=0)
                for(int hold=0;hold<3;++hold)Render(camera,kTime);
            DirectionalLightingFrame frame;m_directionalCaptureTarget=&frame;CaptureDirectionalLightingFrame();
            if(!frame.valid||!FiniteHdr(frame.hdr)||HasDebugLayerErrors()||
                m_shadowParameters.nearSliceCount!=(farOnly?80u:159u)||m_shadowParameters.farSliceCount!=79u)return false;
            double delta=0;std::size_t samples=0;
            if(!previous.empty())for(std::size_t p=0;p<frame.rgba.size();p+=64)for(int c=0;c<3;++c){
                delta+=std::abs(int(frame.rgba[p+c])-int(previous[p+c]));++samples;}
            const auto center=m_shadowParameters.nearCenter;
            const auto up=m_shadowParameters.lightUp;
            const float dx=anchor.x-center.x,dy=anchor.y-center.y,dz=anchor.z-center.z;
            const float edge=2*std::max(std::abs(dx*right.x+dy*right.y+dz*right.z)/m_shadowParameters.nearWidthMeters,
                std::abs(dx*up.x+dy*up.y+dz*up.z)/stage12shadow::ProjectedUpWidth(m_shadowParameters.nearWidthMeters,
                    m_cloudDomainParameters.cloudLayerThickness,m_shadowParameters.lightForward.y));
            const float t=std::clamp((edge-m_shadowParameters.nearCoreEnd)/(m_shadowParameters.nearBlendEnd-m_shadowParameters.nearCoreEnd),0.f,1.f);
            const float weight=1-t*t*(3-2*t);minWeight=std::min(minWeight,weight);maxWeight=std::max(maxWeight,weight);
            const auto pos=camera.GetPosition();
            csv<<type<<','<<(farOnly?80:159)<<','<<path<<','<<index<<','<<m_atmosphereParameters.sunElevationDegrees<<','
                <<pos.x<<','<<pos.z<<','<<(samples?delta/samples:0)<<','<<weight<<'\n';
            if(index==30 || (path==2&&(index==48||index==60)) || (focused&&index>=27&&index<=33)) {
                const auto name="type"+std::to_string(type)+"-near"+std::to_string(farOnly?80:159)+"-path"+std::to_string(path)+"-i"+std::to_string(index);
                if(!SavePng(root/(name+".png"),frame.rgba))return false;
            }
            previous=std::move(frame.rgba);
        }
        if(path==2&&(minWeight>.01f||maxWeight<.99f))return false;
        Log("[Far79Stability] type="+std::to_string(type)+" near="+std::to_string(farOnly?80:159)+" path="+std::to_string(path)+" finite/CB passed");
    }
    if(!WriteText(root/L"stability.csv",csv.str()))return false;
    if(!SetShadowHeightRefinementForValidation(false)||!ApplySceneConcept(Stage15ConceptPreset::UrbanFairWeather))return false;
    return true;
}

// 06: 폐기 기능 정리 전 승인 화면, 생산 HLSL 수치, 림의 독립성을 검증한다.
// 06-B 경계 원인 분리. 같은 장면/광학 모델의 수치 참조와 별도 수송 진단을 구분한다.
bool Renderer::RunRimBoundaryDiagnostics(Camera& camera)
{
    wchar_t directory[32768]{};
    const DWORD n=GetEnvironmentVariableW(L"VCLOUD_RIM_BOUNDARY_DIR",directory,32768);
    if(!n || n>=32768 || std::filesystem::exists(directory)) return false;
    std::filesystem::create_directories(directory);
    const auto root=std::filesystem::path(directory);
    if(GetEnvironmentVariableW(L"VCLOUD_RIM_FAR_STABILITY",nullptr,0)!=0)return RunFarHeightStability(camera,root);
    const bool densityComparison=GetEnvironmentVariableW(L"VCLOUD_RIM_DENSITY_COMPARE",nullptr,0)!=0;
    const bool cacheOptical=GetEnvironmentVariableW(L"VCLOUD_RIM_CACHE_OPTICAL",nullptr,0)!=0;
    const bool detailReview=GetEnvironmentVariableW(L"VCLOUD_DETAIL_REVIEW",nullptr,0)!=0;
    const bool powderReview=GetEnvironmentVariableW(L"VCLOUD_POWDER_REVIEW",nullptr,0)!=0;
    const bool baseDirectReview=GetEnvironmentVariableW(L"VCLOUD_BASE_DIRECT_REVIEW",nullptr,0)!=0;
    const bool skyBalanceReview=GetEnvironmentVariableW(L"VCLOUD_SKY_BALANCE_REVIEW",nullptr,0)!=0;
    const bool skyColorReview=detailReview||powderReview||baseDirectReview||skyBalanceReview||GetEnvironmentVariableW(L"VCLOUD_SKY_COLOR_REVIEW",nullptr,0)!=0;
    const bool rimPathReview=GetEnvironmentVariableW(L"VCLOUD_RIM_PATH_REVIEW",nullptr,0)!=0;
    const bool rimDepthReview=skyColorReview||rimPathReview||GetEnvironmentVariableW(L"VCLOUD_RIM_DEPTH_REVIEW",nullptr,0)!=0;
    const bool rimReview=rimDepthReview||GetEnvironmentVariableW(L"VCLOUD_RIM_APPROVED_CACHE_REVIEW",nullptr,0)!=0;
    const bool stripeDiagnosis=GetEnvironmentVariableW(L"VCLOUD_RIM_STRIPE_DIAG",nullptr,0)!=0;
    const bool cacheComparison=rimReview||stripeDiagnosis||cacheOptical||GetEnvironmentVariableW(L"VCLOUD_RIM_CACHE_COMPARE",nullptr,0)!=0;
    if(densityComparison&&cacheComparison)return false;
    SetAutomatedRenderMode(true); SetVSyncEnabled(false); EnableNoiseLabPreviews(false);
    if(!SetBaseOctaveExtraForValidation(.5f)||!SetShadowHeightRefinementForValidation(false)||
        !SetRimPhaseCapForValidation(2.5f)) return false;
    const auto& f5=stage13camera::Get(Stage13CameraPresetId::HeroDepth);
    camera.SetLookAt(f5.position,f5.target);
    camera.SetClipPlanes(stage13camera::kNearPlaneMeters,stage13camera::kFarPlaneMeters);
    camera.SetFovYDegrees(60);camera.SetAspect(float(kWidth)/kHeight);
    D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;
    raster.DepthClipEnable=TRUE;raster.ScissorEnable=TRUE;
    ComPtr<ID3D11RasterizerState> scissor;
    if(FAILED(m_device->CreateRasterizerState(&raster,&scissor)))return false;
    const auto normal=m_cloudPs;
    const auto normalCache=m_deepShadowCs;
    struct RestoreCache {ComPtr<ID3D11ComputeShader>& s;ComPtr<ID3D11ComputeShader> v;~RestoreCache(){s=v;}} restoreCache{m_deepShadowCs,normalCache};
    struct Restore {ComPtr<ID3D11PixelShader>& s;ComPtr<ID3D11PixelShader> v;~Restore(){s=v;}} restore{m_cloudPs,normal};
    // 전역 pixel 좌표/카메라는 고정. ROI만 타일로 그려 진단 비용과 단일 draw 시간을 제한한다.
    const bool denseRoi=GetEnvironmentVariableW(L"VCLOUD_RIM_BOUNDARY_DENSE_ROI",nullptr,0)!=0;
    const D3D11_RECT roi=stripeDiagnosis?D3D11_RECT{1536,624,1696,720}:
        (denseRoi?D3D11_RECT{624,560,880,752}:D3D11_RECT{1200,540,1456,732});
    if(!WriteText(root/L"contract.json",
        std::string("{\"width\":1920,\"height\":1080,\"roi\":")+
        (stripeDiagnosis?"[1536,624,1696,720]":(denseRoi?"[624,560,880,752]":"[1200,540,1456,732]"))+",\"time\":71,"
        "\"azimuth\":-108.5,\"altitudes\":[18,5],\"formation\":\"Urban\","
        "\"densityShaping\":0.70,\"base\":1.50,\"shadowExponent\":1.35,"
        "\"rim\":[1,1,2.5],\"format\":\"RGBA16F little endian\","
        "\"pack1\":[\"Direct luminance\",\"Rim luminance\",\"Single T*P0 luminance\",\"View T\"],"
        "\"pack2\":[\"Sky luminance\",\"Ground luminance\",\"Multiple luminance\",\"View T\"],"
        "\"pack3\":[\"opacity weighted Sun T\",\"integrated View tau\",\"P0\",\"View T\"],"
        "\"outsideRoi\":\"undefined; never analyze\",\"singlePhase\":\"existing project P0 cap2.5 (not normalized sr^-1)\","
        "\"sunDensity\":\"unchanged Base\",\"viewDensity\":\"unchanged Detail plus distance fade\","
        "\"referenceTauLimit\":\"existing stage12MaximumOpticalDepth\","
        "\"viewReference\":\"fixed step, 4096 iterations, no coarse skip, no early exit\"}\n"))return false;
    struct Variant {const char* name;const char* view;const char* sun;float extinctionScale=1;};
    std::vector<Variant> variants={{"N0",nullptr,nullptr},{"N1-s50",nullptr,"50.0"},
        {"N1-s25",nullptr,"25.0"},{"N2-v50","50.0",nullptr},
        {"N2-v25","25.0",nullptr},{"N3-v50-s50","50.0","50.0"},
        {"N3-v25-s25","25.0","25.0"},{"N3-v12p5-s12p5","12.5","12.5"},
        {"D-v12p5-s12p5-detail","12.5","12.5"}};
    if(densityComparison)variants={{"B-scale1","12.5","12.5",1},{"D-scale1","12.5","12.5",1},
        {"B-scale2","12.5","12.5",2},{"D-scale2","12.5","12.5",2},
        {"B-scale4","12.5","12.5",4},{"D-scale4","12.5","12.5",4},
        {"B-scale8","12.5","12.5",8},{"D-scale8","12.5","12.5",8}};
    if(cacheComparison)variants={{"B-cache",nullptr,nullptr},{"D-cache",nullptr,nullptr},
        {"B-reference",nullptr,"12.5"},{"D-reference",nullptr,"12.5"}};
    if(cacheOptical)variants={{"B-scale1",nullptr,nullptr,1},{"D-scale1",nullptr,nullptr,1},
        {"B-scale2",nullptr,nullptr,2},{"D-scale2",nullptr,nullptr,2},
        {"B-scale4",nullptr,nullptr,4},{"D-scale4",nullptr,nullptr,4}};
    if(stripeDiagnosis)variants={{"B-cache",nullptr,nullptr,4},
        {"B-sun12",nullptr,"12.5",4},{"B-view25","25.0",nullptr,4},
        {"B-view12","12.5",nullptr,4},{"B-both25","25.0","12.5",4},
        {"B-both12","12.5","12.5",4}};
    const bool stripeCache=stripeDiagnosis&&GetEnvironmentVariableW(L"VCLOUD_RIM_STRIPE_CACHE",nullptr,0)!=0;
    if(stripeCache)variants={{"B-cache",nullptr,nullptr,4},{"B-near",nullptr,nullptr,4},
        {"B-far",nullptr,nullptr,4},{"B-sub8",nullptr,nullptr,4},{"B-height2x",nullptr,nullptr,4}};
    const bool farHeightComparison=stripeDiagnosis&&GetEnvironmentVariableW(L"VCLOUD_RIM_FAR_HEIGHT",nullptr,0)!=0;
    const bool heightInterpolation=farHeightComparison||(stripeDiagnosis&&GetEnvironmentVariableW(L"VCLOUD_RIM_HEIGHT_INTERPOLATION",nullptr,0)!=0);
    if(heightInterpolation)variants={{"B-cache",nullptr,nullptr,4},{"B-hermite",nullptr,nullptr,4},
        {"B-height2x",nullptr,nullptr,4}};
    if(farHeightComparison)variants={{"B-cache",nullptr,nullptr,4},{"B-far79",nullptr,nullptr,4},
        {"B-height2x",nullptr,nullptr,4}};
    if(rimReview)variants={{"B-rim1",nullptr,nullptr,4},{"B-cap4",nullptr,nullptr,4},
        {"B-cap8",nullptr,nullptr,4},{"B-rim2",nullptr,nullptr,4},{"B-rim4",nullptr,nullptr,4}};
    if(rimDepthReview)variants={{"B-depth05",nullptr,nullptr,4},{"B-depth1",nullptr,nullptr,4},
        {"B-depth2",nullptr,nullptr,4}};
    if(skyColorReview)variants={{"B-sky-before",nullptr,nullptr,1},{"B-sky-after",nullptr,nullptr,1}};
    if(skyColorReview&&!WriteText(root/L"sky-contract.json", "{\"sigmaScale\":1,\"intensity\":2,\"depth\":1,\"modes\":[\"legacy sky sun coupling\",\"sky local AO only\"],\"rimUnchanged\":true}\n"))return false;
    if(skyBalanceReview) {
        variants={{"B-sky-before-base",nullptr,nullptr,1},{"B-sky-before-sky",nullptr,nullptr,1},
            {"B-sky-before-multi",nullptr,nullptr,1},{"B-sky-before-both",nullptr,nullptr,1},
            {"B-sky-after-base",nullptr,nullptr,1},{"B-sky-after-sky",nullptr,nullptr,1},
            {"B-sky-after-multi",nullptr,nullptr,1},{"B-sky-after-both",nullptr,nullptr,1}};
        if(!WriteText(root/L"balance-contract.json", R"({"sigmaScale":1,"rimIntensity":2,"rimDepth":1,"cache":"80/79","skyCoupling":["before: legacy coupling","after: removed"],"candidates":{"base":[0.85,0.15],"sky":[2,0.15],"multi":[0.85,0.075],"both":[2,0.075]},"azimuth":-90,"altitudes":[18,5],"camera":"Urban F5 time71","runtimeDefaultsChanged":false})"))return false;
    }
    if(baseDirectReview) {
        variants={{"B-direct1",nullptr,nullptr,1},{"B-direct075",nullptr,nullptr,1},{"B-direct05",nullptr,nullptr,1}};
        if(!WriteText(root/L"direct-contract.json", R"({"baseDirectScale":[1,0.75,0.5],"sigmaScale":1,"skyFill":0.85,"multipleAttenuation":0.15,"skySunCoupling":false,"rimIntensity":2,"rimDepth":1,"cache":"80/79","azimuth":-90,"altitudes":[18,5],"camera":"Urban F5 time71","runtimeDefaultsChanged":false})"))return false;
    }
    if(powderReview) {
        variants={{"B-powder0",nullptr,nullptr,1},{"B-powder025",nullptr,nullptr,1},{"B-powder05",nullptr,nullptr,1}};
        if(!WriteText(root/L"powder-contract.json", R"json({"strength":[0,0.25,0.5],"curve":4,"densityInput":"finalDensity without step length","angle":"1-smoothstep(-0.5,0.5,cosTheta)","target":"base direct only","sigmaScale":1,"skyFill":0.85,"multipleAttenuation":0.15,"rimIntensity":2,"rimDepth":1,"cache":"80/79","azimuths":[-90,0,90],"altitudes":[18,5],"camera":"Urban F5 time71","runtimeDefaultsChanged":false})json"))return false;
    }
    if(detailReview) {
        variants={{"B-detail0",nullptr,nullptr,1},{"B-detailDefault",nullptr,nullptr,1},
            {"B-detail05",nullptr,nullptr,1},{"B-detail1",nullptr,nullptr,1},{"D-detail1",nullptr,nullptr,1}};
        if(!WriteText(root/L"detail-contract.json", R"({"erosion":[0,"Urban default",0.5,1],"extra":"D-detail1: Detail shadow diagnostic only","powder":false,"sigmaScale":1,"rim":[2,1],"cache":"80/79","azimuths":[0,90],"altitudes":[18,5],"time":71,"camera":"F5","runtimeDefaultsChanged":false})"))return false;
    }
    if(rimPathReview)variants={{"B-current",nullptr,nullptr,4},{"B-transport",nullptr,nullptr,4},{"B-joint",nullptr,nullptr,4}};
    if(rimPathReview&&!WriteText(root/L"path-contract.json", "{\"sigmaScale\":4,\"intensity\":2,\"depth\":1,\"cap\":2.5,\"cache\":\"80/79\",\"modes\":[\"current SunT weight\",\"no extra rim weight\",\"midpoint ViewT times SunT weight\"],\"runtimeChanged\":false}\n"))return false;
    if(rimDepthReview&&!WriteText(root/L"depth-contract.json",
        "{\"azimuths\":[-90,0,90],\"labels\":[\"backlit\",\"side\",\"frontlit\"],"
        "\"altitudes\":[18,5],\"sigmaScale\":4,\"rimIntensity\":2,\"cap\":2.5,\"depth\":[0.5,1,2],"
        "\"camera\":\"F5 fixed, forward horizontal azimuth -90\",\"cache\":\"80/79\",\"runtimeDepthChanged\":false}\n"))return false;
    if(stripeDiagnosis&&!WriteText(root/L"stripe.json",
        "{\"sigmaScale\":4,\"sigma\":0.00144,\"shadowDensity\":\"Base\","
        "\"viewDensity\":\"Detail\",\"reference\":\"Sun12.5m,View25/12.5m no skip/exit\","
        "\"runtimeDefaultsChanged\":false}\n"))return false;
    if(cacheOptical&&!WriteText(root/L"cache-optical.json",
        "{\"sun\":\"80/79 cache and cone; B=Base D=Detail\",\"view\":\"normal High\","
        "\"scales\":[1,2,4],\"sigmaBase\":0.00036,\"runtimeDefaultsChanged\":false}\n"))return false;
    if(densityComparison && !WriteText(root/L"density-comparison.json",
        "{\"sun\":\"direct 12.5m, B=Base D=Detail\",\"view\":\"Detail 12.5m no skip/exit\","
        "\"extinctionScales\":[1,2,4,8],\"densityFieldChanged\":false,\"phaseChanged\":false,"
        "\"comparisonOrder\":\"B1 vs D1, then within each B/D 1/2/4/8\","
        "\"pack4\":\"Single RGB\",\"pack5\":\"Direct RGB\",\"pack6\":\"Rim RGB\","
        "\"scope\":\"test only; no runtime defaults adopted\"}\n"))return false;
    const std::vector<float> azimuths=detailReview?std::vector<float>{0.f,90.f}:(baseDirectReview||skyBalanceReview)?std::vector<float>{-90.f}:rimDepthReview?std::vector<float>{-90.f,0.f,90.f}:std::vector<float>{-108.5f};
    for(float azimuth:azimuths)for(float altitude:{18.f,5.f}) {
        m_cloudPs=normal;
        m_deepShadowCs=normalCache;
        if(!ApplySceneConcept(Stage15ConceptPreset::UrbanFairWeather))return false;
        SetCloudMovementSpeedForValidation(0);
        m_atmosphereParameters.sunControlMode=SunControlMode::Angles;
        m_atmosphereParameters.timePlaybackEnabled=false;
        m_atmosphereParameters.sunAzimuthDegrees=azimuth;m_atmosphereParameters.sunElevationDegrees=altitude;
        m_toneMappingParameters=ToneMappingParameters{};
        SetDebugMode(CloudDebugMode::Composite);
        // 초기 resize/리소스 준비 및 프레임 끝에서 확정되는 대기 입력을 안정화한다.
        for(int warm=0;warm<60;++warm)Render(camera,kTime);
        std::ostringstream actual;
        actual<<"altitude="<<altitude<<" sun="<<m_lightParameters.directionToSun.x<<','
            <<m_lightParameters.directionToSun.y<<','<<m_lightParameters.directionToSun.z
            <<" density="<<m_cloudShapeParameters.densityShaping<<" shadow="<<m_lightParameters.shadowExponent
            <<" phase="<<m_lightParameters.phaseIntensity<<" rim="<<m_lightParameters.rimIntensity<<','<<m_lightParameters.rimDepthScale;
        if(std::abs(m_lightParameters.directionToSun.y-std::sin(altitude*3.14159265358979323846f/180))>1e-5f||
            m_cloudShapeParameters.densityShaping!=.7f||m_lightParameters.shadowExponent!=1.35f)return false;
        if(!WriteText(root/((rimDepthReview?std::string("az")+std::to_string(int(azimuth))+"-":"")+
            std::string("alt")+std::to_string(int(altitude))+"-actual.txt"),actual.str()))return false;
        DirectionalLightingFrame baseline;m_directionalCaptureTarget=&baseline;CaptureDirectionalLightingFrame();
        const auto prefix=(rimDepthReview?std::string("az")+std::to_string(int(azimuth))+"-":"")+std::string("alt")+std::to_string(int(altitude));
        if(!baseline.valid||!FiniteHdr(baseline.hdr)||
            !SavePng(root/(prefix+"-composite.png"),baseline.rgba)||
            !WriteNew(root/(prefix+"-composite.rgba16f"),baseline.hdr.data(),baseline.hdr.size()))return false;
        const float approvedDetail=m_cloudParameters.detailErosionStrength;
        if(detailReview&&!WriteText(root/(prefix+"-detail-default.txt"),std::to_string(approvedDetail)))return false;
        const float approvedExtinction=m_cloudParameters.extinctionCoefficient;
        if(!WriteText(root/(prefix+"-extinction.txt"),std::to_string(approvedExtinction)))return false;
        for(const auto& variant:variants) for(int pack=1;pack<=(skyColorReview?7:6);++pack) {
            if(cacheComparison&&pack>3&&!(stripeDiagnosis&&pack==5)&&!(skyColorReview&&(pack==5||pack==7)))continue;
            if(!cacheComparison&&!densityComparison && pack>3 && std::strcmp(variant.name,"N0"))continue;
            // 고비용 세부 분해는 N0와 마지막 참조에 한정한다.
            if(!cacheComparison&&!densityComparison && pack>1 && std::strcmp(variant.name,"N0") && std::strcmp(variant.name,"N3-v12p5-s12p5"))continue;
            if(cacheComparison && pack==1) {
                if(detailReview) {
                    m_cloudParameters.detailErosionStrength=std::strcmp(variant.name,"B-detail0")==0?0.f:
                        (std::strcmp(variant.name,"B-detailDefault")==0?approvedDetail:
                        (std::strcmp(variant.name,"B-detail05")==0?.5f:1.f));
                    m_constantBufferUploadValid[1]=false;
                }
                const bool farOnly=std::strcmp(variant.name,"B-far79")==0;
                if(stripeDiagnosis&&!SetShadowHeightRefinementForValidation(farOnly||std::strcmp(variant.name,"B-height2x")==0,farOnly))return false;
                // 캐시 재생성 전에 View/Sun 공통 소멸계수를 적용한다.
                m_cloudParameters.extinctionCoefficient=approvedExtinction*variant.extinctionScale;
                m_constantBufferUploadValid[1]=false;
                if(rimReview) {
                    m_lightParameters.rimIntensity=std::strcmp(variant.name,"B-rim4")==0?4.f:
                        (std::strcmp(variant.name,"B-rim2")==0?2.f:1.f);
                    m_constantBufferUploadValid[3]=false;
                    if(rimDepthReview) {
                        m_lightParameters.rimIntensity=2.f;
                        m_lightParameters.rimDepthScale=std::strcmp(variant.name,"B-depth05")==0?.5f:
                            (std::strcmp(variant.name,"B-depth2")==0?2.f:1.f);
                    }
                }
                if(skyBalanceReview) {
                    const bool both=std::strstr(variant.name,"-both")!=nullptr;
                    m_environmentParameters.physicalSkyFillScale=(both||std::strstr(variant.name,"-sky")!=nullptr&&
                        std::string(variant.name).substr(std::string(variant.name).size()-4)=="-sky")?2.f:.85f;
                    m_environmentParameters.multipleScatteringAttenuation=(both||std::strstr(variant.name,"-multi")!=nullptr)?.075f:.15f;
                    m_constantBufferUploadValid[4]=false;
                }
                std::vector<D3D_SHADER_MACRO> runtimeMacros;
                if(rimReview)runtimeMacros.push_back({"VCLOUD_TEST_RIM_CAP",
                    std::strcmp(variant.name,"B-cap8")==0?"8.0":(std::strcmp(variant.name,"B-cap4")==0?"4.0":"2.5")});
                if(std::strcmp(variant.name,"B-hermite")==0)runtimeMacros.push_back({"VCLOUD_TEST_MONOTONE_HEIGHT","1"});
                if(std::strcmp(variant.name,"B-sub8")==0)runtimeMacros.push_back({"VCLOUD_TEST_LIGHT_SUBSTEPS","8"});
                if(variant.name[0]=='D')runtimeMacros.push_back({"VCLOUD_TEST_CACHE_DETAIL","1"});
                if(rimPathReview&&std::strcmp(variant.name,"B-current"))runtimeMacros.push_back({"VCLOUD_TEST_RIM_PATH",std::strcmp(variant.name,"B-joint")==0?"2":"1"});
                if(skyColorReview&&std::strstr(variant.name,"B-sky-before")==variant.name)runtimeMacros.push_back({"VCLOUD_TEST_LEGACY_SKY_OCCLUSION","1"});
                if(baseDirectReview&&std::strcmp(variant.name,"B-direct1")!=0)runtimeMacros.push_back({"VCLOUD_TEST_BASE_DIRECT_SCALE",
                    std::strcmp(variant.name,"B-direct075")==0?"0.75":"0.5"});
                if(powderReview)runtimeMacros.push_back({"VCLOUD_TEST_POWDER_STRENGTH",
                    std::strcmp(variant.name,"B-powder025")==0?"0.25":(std::strcmp(variant.name,"B-powder05")==0?"0.5":"0.0")});
                runtimeMacros.push_back({nullptr,nullptr});
                ComPtr<ID3DBlob> cs,ps;
                if(!CompileShaderFromFile(m_deepShadowShaderPath,"main","cs_5_0",cs,false,runtimeMacros.data())||
                    !CompileShaderFromFile(m_cloudShaderPath,"main","ps_5_0",ps,false,runtimeMacros.data())||
                    FAILED(m_device->CreateComputeShader(cs->GetBufferPointer(),cs->GetBufferSize(),nullptr,&m_deepShadowCs))||
                    FAILED(m_device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&m_cloudPs)))return false;
                for(int warm=0;warm<60;++warm)Render(camera,kTime);
                if(farHeightComparison) {
                    D3D11_TEXTURE2D_DESC nearDesc{},farDesc{};
                    m_shadowNearTexture->GetDesc(&nearDesc);m_shadowFarTexture->GetDesc(&farDesc);
                    if(nearDesc.ArraySize!=m_shadowParameters.nearSliceCount||farDesc.ArraySize!=m_shadowParameters.farSliceCount)return false;
                    if(!WriteText(root/(prefix+"-"+variant.name+"-slices.txt"),
                        std::to_string(nearDesc.ArraySize)+"/"+std::to_string(farDesc.ArraySize)))return false;
                }
                if(m_cloudParameters.extinctionCoefficient!=approvedExtinction*variant.extinctionScale)return false;
                if(!variant.sun&&(!stripeDiagnosis||heightInterpolation)) {
                    DirectionalLightingFrame look;m_directionalCaptureTarget=&look;CaptureDirectionalLightingFrame();
                    if(!look.valid||!FiniteHdr(look.hdr)||!SavePng(root/(prefix+"-"+variant.name+"-composite.png"),look.rgba))return false;
#if !defined(_DEBUG)
                    std::vector<double> shadows,clouds,frames;
                    for(unsigned i=0;i<180&&frames.size()<120;++i){Render(camera,kTime);const auto& t=TimingSnapshot();
                        if(t.gpuValid&&t.rawGpuFrameMs>0){shadows.push_back(t.rawGpuShadowCacheMs);clouds.push_back(t.rawGpuCloudMs);frames.push_back(t.rawGpuFrameMs);}}
                    if(frames.size()!=120)return false;
                    std::sort(shadows.begin(),shadows.end());std::sort(clouds.begin(),clouds.end());std::sort(frames.begin(),frames.end());
                    std::ostringstream perf;perf<<"shadow_p95_ms,cloud_p95_ms,frame_p95_ms\n"<<shadows[113]<<','<<clouds[113]<<','<<frames[113]<<'\n';
                    if(!WriteText(root/(prefix+"-"+variant.name+"-performance.csv"),perf.str()))return false;
#endif
                }
            }
            if(densityComparison) {
                m_cloudParameters.extinctionCoefficient=approvedExtinction*variant.extinctionScale;
                D3D11_MAPPED_SUBRESOURCE cbMap{};
                if(FAILED(m_context->Map(m_cloudCb.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&cbMap)))return false;
                std::memcpy(cbMap.pData,&m_cloudParameters,sizeof(m_cloudParameters));m_context->Unmap(m_cloudCb.Get(),0);
                m_constantBufferUploadValid[1]=false;
            }
            const std::string packText=std::to_string(pack);
            std::vector<D3D_SHADER_MACRO> macros{{"VCLOUD_TEST_RIM_BOUNDARY",packText.c_str()}};
            if(rimReview)macros.push_back({"VCLOUD_TEST_RIM_CAP",
                std::strcmp(variant.name,"B-cap8")==0?"8.0":(std::strcmp(variant.name,"B-cap4")==0?"4.0":"2.5")});
            if(std::strcmp(variant.name,"B-hermite")==0)macros.push_back({"VCLOUD_TEST_MONOTONE_HEIGHT","1"});
            if(std::strcmp(variant.name,"B-near")==0)macros.push_back({"VCLOUD_TEST_CASCADE_NEAR","1"});
            if(std::strcmp(variant.name,"B-far")==0)macros.push_back({"VCLOUD_TEST_CASCADE_FAR","1"});
            if(variant.view) {
                macros.push_back({"VCLOUD_TEST_VIEW_STEP_METERS",variant.view});
                macros.push_back({"VCLOUD_TEST_BOUNDARY_NO_SKIP","1"});
                macros.push_back({"VCLOUD_TEST_BOUNDARY_NO_EXIT","1"});
            }
            if(variant.sun) {
                macros.push_back({"VCLOUD_TEST_SOLAR_REFERENCE_STEP",variant.sun});
                macros.push_back({"VCLOUD_TEST_SOLAR_REFERENCE_FULL","1"});
            }
            if(variant.name[0]=='D')macros.push_back({"VCLOUD_TEST_BOUNDARY_SHADOW_DETAIL","1"});
            if(cacheComparison&&variant.name[0]=='D')macros.push_back({"VCLOUD_TEST_CACHE_DETAIL","1"});
            if(rimPathReview&&std::strcmp(variant.name,"B-current"))macros.push_back({"VCLOUD_TEST_RIM_PATH",std::strcmp(variant.name,"B-joint")==0?"2":"1"});
            if(skyColorReview&&std::strstr(variant.name,"B-sky-before")==variant.name)macros.push_back({"VCLOUD_TEST_LEGACY_SKY_OCCLUSION","1"});
            if(baseDirectReview&&std::strcmp(variant.name,"B-direct1")!=0)macros.push_back({"VCLOUD_TEST_BASE_DIRECT_SCALE",
                std::strcmp(variant.name,"B-direct075")==0?"0.75":"0.5"});
            if(powderReview)macros.push_back({"VCLOUD_TEST_POWDER_STRENGTH",
                std::strcmp(variant.name,"B-powder025")==0?"0.25":(std::strcmp(variant.name,"B-powder05")==0?"0.5":"0.0")});
            macros.push_back({nullptr,nullptr});
            ComPtr<ID3DBlob> code;
            if(!CompileShaderFromFile(m_cloudShaderPath,"main","ps_5_0",code,false,macros.data())||
                FAILED(m_device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&m_cloudPs))) {
                Log("[Boundary] compile failed "+m_shaderError);return false;
            }
            Log("[Boundary] begin "+prefix+" "+variant.name+" pack"+packText);
            RenderCloudPass(true);m_context->RSSetState(scissor.Get());
            const D3D11_RECT drawRoi=std::strcmp(variant.name,"N0")?roi:D3D11_RECT{0,0,LONG(kWidth),LONG(kHeight)};
            for(LONG y=drawRoi.top;y<drawRoi.bottom;y+=16)for(LONG x=drawRoi.left;x<drawRoi.right;x+=32) {
                const D3D11_RECT tile{x,y,std::min(x+32,drawRoi.right),std::min(y+16,drawRoi.bottom)};
                m_context->RSSetScissorRects(1,&tile);m_context->Draw(3,0);
            }
            m_context->RSSetState(nullptr);UnbindCloudShaderResources(14);RenderToneMapPass();
            DirectionalLightingFrame capture;m_directionalCaptureTarget=&capture;CaptureDirectionalLightingFrame();
            if(!capture.valid||!FiniteHdr(capture.hdr)||HasDebugLayerErrors())return false;
            if(!WriteNew(root/(prefix+"-"+variant.name+"-pack"+packText+".rgba16f"),capture.hdr.data(),capture.hdr.size()))return false;
            if(pack>3 && !SavePng(root/(prefix+"-"+variant.name+"-pack"+packText+".png"),capture.rgba))return false;
            Log("[Boundary] saved "+prefix+" "+variant.name+" pack"+packText);
        }
        m_cloudParameters.extinctionCoefficient=approvedExtinction;
        m_constantBufferUploadValid[1]=false;
        if(stripeDiagnosis&&!SetShadowHeightRefinementForValidation(false))return false;
    }
    if(cacheComparison&&!stripeDiagnosis&&!rimReview) {
        const D3D_SHADER_MACRO detailMacros[]={{"VCLOUD_TEST_CACHE_DETAIL","1"},{nullptr,nullptr}};
        ComPtr<ID3DBlob> code;
        if(!CompileShaderFromFile(m_cloudShaderPath,"main","ps_5_0",code,false,detailMacros)||
            FAILED(m_device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&m_cloudPs)))return false;
        for(float alt:{2.5f,2.99f,3.01f,3.5f,4.99f,5.01f,5.5f}) {
            m_atmosphereParameters.sunElevationDegrees=alt;
            for(int i=0;i<3;++i)Render(camera,kTime);
            DirectionalLightingFrame low;m_directionalCaptureTarget=&low;CaptureDirectionalLightingFrame();
            if(!low.valid||!FiniteHdr(low.hdr)||HasDebugLayerErrors())return false;
        }
        if(!WriteText(root/L"low-sun-finite.txt","Detail cache/cone 2.5/2.99/3.01/3.5/4.99/5.01/5.5: finite and D3D checks passed. Not visual stability approval.\n"))return false;
    }
    // 실제 ComputeDirectInteractionColor의 시선 감쇠/구간 가중치를 균일 구 해석해와 비교한다.
    D3D11_BUFFER_DESC desc{};desc.ByteWidth=1024*sizeof(DirectX::XMFLOAT4);desc.Usage=D3D11_USAGE_DEFAULT;
    desc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;desc.StructureByteStride=16;
    ComPtr<ID3D11Buffer> output,readback;ComPtr<ID3D11UnorderedAccessView> uav;
    if(FAILED(m_device->CreateBuffer(&desc,nullptr,&output))||FAILED(m_device->CreateUnorderedAccessView(output.Get(),nullptr,&uav)))return false;
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;desc.StructureByteStride=0;
    if(FAILED(m_device->CreateBuffer(&desc,nullptr,&readback)))return false;
    const D3D_SHADER_MACRO sphereMacros[]={{"VCLOUD_TEST_BOUNDARY_SPHERE","1"},{nullptr,nullptr}};
    ComPtr<ID3DBlob> sphereCode;ComPtr<ID3D11ComputeShader> sphereShader;
    if(!CompileShaderFromFile((ShaderDirectoryForValidation()/L"RimLightingProbe.hlsl").wstring(),"main","cs_5_0",sphereCode,false,sphereMacros)||
        FAILED(m_device->CreateComputeShader(sphereCode->GetBufferPointer(),sphereCode->GetBufferSize(),nullptr,&sphereShader)))return false;
    ID3D11Buffer* cloud=m_cloudCb.Get();ID3D11Buffer* light=m_lightCb.Get();ID3D11UnorderedAccessView* target=uav.Get();
    m_context->CSSetConstantBuffers(1,1,&cloud);m_context->CSSetConstantBuffers(3,1,&light);
    m_context->CSSetShader(sphereShader.Get(),nullptr,0);m_context->CSSetUnorderedAccessViews(0,1,&target,nullptr);m_context->Dispatch(16,1,1);
    target=nullptr;m_context->CSSetUnorderedAccessViews(0,1,&target,nullptr);m_context->CopyResource(readback.Get(),output.Get());
    D3D11_MAPPED_SUBRESOURCE map{};if(FAILED(m_context->Map(readback.Get(),0,D3D11_MAP_READ,0,&map)))return false;
    const auto* values=static_cast<const DirectX::XMFLOAT4*>(map.pData);
    std::ostringstream sphere;sphere<<"steps,b,tau,gpu,analytic,absolute_error\n";
    bool spherePass=true;double maxFinal=0;
    for(unsigned i=0;i<1024;++i) {
        const unsigned count=i/256==0?8:i/256==1?32:i/256==2?128:4096;
        const double b=double(i%256)/255, tau=8*std::sqrt(std::max(0.,1-b*b));
        const double expected=m_lightParameters.singleScatteringAlbedo*tau*std::exp(-tau);
        const double error=std::abs(values[i].x-expected);
        sphere<<count<<','<<b<<','<<tau<<','<<values[i].x<<','<<expected<<','<<error<<'\n';
        spherePass&=std::isfinite(values[i].x)&&values[i].y>=0&&values[i].y<=1;
        if(count==4096) {maxFinal=std::max(maxFinal,error);spherePass&=error<.0005;}
    }
    m_context->Unmap(readback.Get(),0);m_context->CSSetShader(nullptr,nullptr,0);
    if(!WriteText(root/L"sphere.csv",sphere.str())||!spherePass||HasDebugLayerErrors())return false;
    Log("[Boundary] sphere final max absolute error="+std::to_string(maxFinal));
    Log("[Boundary] finite/d3d checks passed; convergence and ROI interpretation require analysis, no look approval.");
    return true;
}

bool Renderer::RunRimLightingDiagnostics(Camera& camera)
{
    if (GetEnvironmentVariableW(L"VCLOUD_RIM_BOUNDARY_DIR", nullptr, 0))
        return RunRimBoundaryDiagnostics(camera);
    SetAutomatedRenderMode(true); SetVSyncEnabled(false); EnableNoiseLabPreviews(false);
    if (!SetBaseOctaveExtraForValidation(.5f) || !SetShadowHeightRefinementForValidation(false)) return false;
    const auto& f5=stage13camera::Get(Stage13CameraPresetId::HeroDepth);
    camera.SetLookAt(f5.position,f5.target);
    camera.SetClipPlanes(stage13camera::kNearPlaneMeters,stage13camera::kFarPlaneMeters);
    camera.SetFovYDegrees(60); camera.SetAspect(float(kWidth)/kHeight);
    wchar_t captures[32768]{}, approved[32768]{};
    const DWORD captureLength=GetEnvironmentVariableW(L"VCLOUD_RIM_CAPTURE_DIR",captures,32768);
    const DWORD approvedLength=GetEnvironmentVariableW(L"VCLOUD_RIM_APPROVED_ROOT",approved,32768);
    if (captureLength>=32768 || approvedLength>=32768) return false;
    if (captureLength) std::filesystem::create_directories(captures);
    auto setup=[&](int type,float az,float alt) {
        if (!ApplySceneConcept(Stage15ConceptPreset::UrbanFairWeather)) return false;
        const auto lightBefore=m_lightParameters;
        if (type && !ApplyCloudType(TypeFormationTarget(static_cast<CloudFormationType>(type-1)))) return false;
        if (std::memcmp(&lightBefore,&m_lightParameters,sizeof(lightBefore)) || m_cloudShapeParameters.densityShaping!=.7f) return false;
        SetCloudMovementSpeedForValidation(0);
        m_atmosphereParameters.sunControlMode=SunControlMode::Angles;
        m_atmosphereParameters.timePlaybackEnabled=false;
        m_atmosphereParameters.sunAzimuthDegrees=az; m_atmosphereParameters.sunElevationDegrees=alt;
        m_toneMappingParameters=ToneMappingParameters{};
        return true;
    };
    auto frame=[&](CloudDebugMode mode) {
        SetDebugMode(mode); DirectionalLightingFrame f;
        m_directionalCaptureTarget=&f; Render(camera,kTime); return f;
    };
    auto valid=[&](const DirectionalLightingFrame& f) {return f.valid && FiniteHdr(f.hdr) && !HasDebugLayerErrors();};
    auto same=[&](const DirectionalLightingFrame& a,const DirectionalLightingFrame& b) {
        const auto d=Compare(a,b);
        if(d.hdrMae>1e-5||d.ldrMae>1.0/255||d.ldrMax>2.0/255) {
            std::ostringstream log;log<<"[RimCompare] mode="<<int(m_cloudParameters.debugMode)<<" hdr_mae="<<d.hdrMae<<" hdr_max="<<d.hdrMax<<" ldr_max="<<d.ldrMax;Log(log.str());
        }
        return valid(a)&&valid(b)&&d.hdrMae<=1e-5 && d.ldrMae<=1.0/255 && d.ldrMax<=2.0/255;
    };
    // 독립성 검사에서는 Weather/캐시 재생성의 수치 변화를 섞지 않고 같은 GPU 입력을 재사용한다.
    auto frozenFrame=[&](CloudDebugMode mode) {
        SetDebugMode(mode); DirectionalLightingFrame f;
        auto upload=[&](ID3D11Buffer* buffer,const void* data,std::size_t size) {
            D3D11_MAPPED_SUBRESOURCE map{};if(FAILED(m_context->Map(buffer,0,D3D11_MAP_WRITE_DISCARD,0,&map)))return false;
            std::memcpy(map.pData,data,size);m_context->Unmap(buffer,0);return true;
        };
        if(!upload(m_cloudCb.Get(),&m_cloudParameters,sizeof(m_cloudParameters))||!upload(m_lightCb.Get(),&m_lightParameters,sizeof(m_lightParameters))||
            !upload(m_environmentCb.Get(),&m_environmentParameters,sizeof(m_environmentParameters)))return f;
        m_constantBufferUploadValid[1]=m_constantBufferUploadValid[2]=m_constantBufferUploadValid[3]=false;
        RenderCloudPass();RenderToneMapPass();m_directionalCaptureTarget=&f;CaptureDirectionalLightingFrame();return f;
    };
    const auto forward=camera.GetForward();
    const float viewAz=std::atan2(forward.z,forward.x)*180/3.14159265358979323846f;
    struct Sun {const char* name; float az,alt;};
    const Sun suns[]={{"azimuth-minus108p5-alt18",-108.5f,18},{"azimuth-minus108p5-alt45",-108.5f,45},
        {"azimuth-minus108p5-alt70",-108.5f,70},{"front-lit-alt18",std::remainder(viewAz+180,360.f),18},
        {"side-lit-alt18",std::remainder(viewAz+90,360.f),18},{"back-lit-alt18",viewAz,18}};
    const char* types[]={"urban","stratus","cumulus","mixed"};
    // 생산 HLSL 함수와 CPU 기준을 비교한다. 실제 PS b3를 CS에 전달한다.
    D3D11_BUFFER_DESC desc{};desc.ByteWidth=1024*sizeof(DirectX::XMFLOAT4);desc.Usage=D3D11_USAGE_DEFAULT;
    desc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;desc.StructureByteStride=16;
    ComPtr<ID3D11Buffer> output,readback;ComPtr<ID3D11UnorderedAccessView> uav;
    if(FAILED(m_device->CreateBuffer(&desc,nullptr,&output))||FAILED(m_device->CreateUnorderedAccessView(output.Get(),nullptr,&uav)))return false;
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;desc.StructureByteStride=0;
    if(FAILED(m_device->CreateBuffer(&desc,nullptr,&readback)))return false;
    for(float cap:{2.5f,4.f,8.f}) {
        if(!SetRimPhaseCapForValidation(cap)||!setup(0,viewAz,18))return false;
        ComPtr<ID3DBlob> code;ComPtr<ID3D11ComputeShader> shader;
        if(!CompileShaderFromFile((ShaderDirectoryForValidation()/L"RimLightingProbe.hlsl").wstring(),"main","cs_5_0",code,false)||
           FAILED(m_device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader))) {Log("[RimProbe] "+m_shaderError);return false;}
        for(const auto pair:{std::pair<float,float>{0.f,1.f},{1.f,1.f},{2.f,.5f},{4.f,2.f}}) {
            m_lightParameters.rimIntensity=pair.first;m_lightParameters.rimDepthScale=pair.second;
            if(!valid(frame(CloudDebugMode::Composite)))return false;
            ComPtr<ID3D11Buffer> bound;m_context->PSGetConstantBuffers(3,1,&bound);
            ID3D11Buffer* cb=bound.Get();m_context->CSSetConstantBuffers(3,1,&cb);
            ID3D11UnorderedAccessView* target=uav.Get();m_context->CSSetUnorderedAccessViews(0,1,&target,nullptr);
            m_context->CSSetShader(shader.Get(),nullptr,0);m_context->Dispatch(16,1,1);
            target=nullptr;m_context->CSSetUnorderedAccessViews(0,1,&target,nullptr);m_context->CSSetShader(nullptr,nullptr,0);
            m_context->CopyResource(readback.Get(),output.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
            if(FAILED(m_context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped)))return false;
            bool passed=true;double maximum=0;unsigned clipped=0;
            const auto* values=static_cast<const DirectX::XMFLOAT4*>(mapped.pData);
            for(unsigned i=0;i<1024;++i) {
                float mu=-1.f+2.f*(i%128)/127.f,t=float(i/128)/7;
                const auto& l=m_lightParameters;
                const auto phase=stage7::EvaluateDualLobePhase({std::sqrt(std::max(1-mu*mu,0.f)),mu,0},{0,1,0},l.phaseEnabled>=.5f,l.forwardScatteringG,l.backwardScatteringG,l.phaseBlend,l.phaseIntensity);
                float raw=l.phaseEnabled>=.5f?1+(std::clamp(phase.dualLobe,0.f,16.f)-1)*l.phaseIntensity:1;
                const auto response=cloudrim::Evaluate(t,phase.phaseFactor,raw,l,cap);
                const float expected[]={response.basePhase,response.rimPhase,response.shapedTransmittance,raw};
                const float* gpu=&values[i].x;
                for(int c=0;c<4;++c){double e=std::abs(double(gpu[c])-expected[c]);maximum=std::max(maximum,e);passed &=std::isfinite(gpu[c])&&e<5e-4;}
                clipped+=raw>cap;
                const float s0=1+(std::pow(t,l.edgeOpticalDepthScale)-1)*l.edgeInfluence;
                const float old=1+(phase.phaseFactor-1)*s0;
                if(cap==2.5f&&pair.first==1&&pair.second==1)passed&=std::abs(response.basePhase+response.rimPhase-old)<1e-5;
            }
            m_context->Unmap(readback.Get(),0);
            std::ostringstream log;log<<"[RimGPU] cap="<<cap<<" intensity="<<pair.first<<" depth="<<pair.second<<" max_error="<<maximum<<" angular_probe_clipped="<<clipped<<"/1024";Log(log.str());
            if(!passed||HasDebugLayerErrors())return false;
        }
    }
    unsigned oldComparisons=0;
    double sunWeightedMeanMax=0,sunWeightedMax=0,sunNormalizedMax=0;
    for (int type=0;type<4;++type) for (int sun=0;sun<6;++sun) {
        if (!SetRimPhaseCapForValidation(2.5f)||!setup(type,suns[sun].az,suns[sun].alt)) return false;
        SetDebugMode(CloudDebugMode::Composite);
        for(int i=0;i<12;++i) Render(camera,kTime);
        const auto current=frame(CloudDebugMode::Composite);
        if (!valid(current)) return false;
        if (approvedLength) {
            const auto root=std::filesystem::path(approved)/"clean"/types[type]/suns[sun].name;
            DirectionalLightingFrame old;
            old.valid=ReadBytes(root/L"frame.rgba16f",old.hdr,std::size_t(kWidth)*kHeight*8)&&LoadPng(root/L"frame.png",old.rgba);
            const auto d=Compare(old,current);
            std::ostringstream log; log<<"[RimApproved] "<<types[type]<<'/'<<suns[sun].name<<" hdr_mae="<<d.hdrMae<<" hdr_max="<<d.hdrMax<<" ldr_max="<<d.ldrMax; Log(log.str());
            if(!same(old,current)) return false;
            ++oldComparisons;
        }
        const auto baseMultiple=frozenFrame(CloudDebugMode::AccumulatedMultipleScattering);
        const auto baseSky=frozenFrame(CloudDebugMode::AccumulatedSkyAmbient);
        const auto baseGround=frozenFrame(CloudDebugMode::AccumulatedGroundBounce);
        const auto baseT=frozenFrame(CloudDebugMode::Transmittance);
        const auto baseDensity=frozenFrame(CloudDebugMode::FinalDensity);
        const auto baseSunT=frozenFrame(CloudDebugMode::VisibleSunTransmittance);
        const auto repeatSunT=frozenFrame(CloudDebugMode::VisibleSunTransmittance);
        if (!same(baseSunT,repeatSunT)) {
            unsigned differences=0;
            for(std::size_t i=0;i<std::size_t(kWidth)*kHeight;++i) if(std::abs(HdrChannel(baseSunT,i)-HdrChannel(repeatSunT,i))>.01f) {
                if(differences++<8) {std::ostringstream out;out<<"[RimRepeat] pixel="<<i<<" old="<<HdrChannel(baseSunT,i)<<" new="<<HdrChannel(repeatSunT,i)<<" opacity="<<1-HdrChannel(baseT,i);Log(out.str());}
            }
            Log("[RimRepeat] same-input differences="+std::to_string(differences));return false;
        }
        for(float cap:{2.5f,4.f,8.f}) {
            if(!SetRimPhaseCapForValidation(cap)) return false;
            m_lightParameters.rimIntensity=1; m_lightParameters.rimDepthScale=1;
            const auto composite=frozenFrame(CloudDebugMode::Composite);
            const auto rim=frozenFrame(CloudDebugMode::SilverLiningContribution);
            if (!valid(composite)||!valid(rim)) return false;
            double sum=0,peak=0,compositeHdr=0,compositeLdr=0,opacitySum=0,clippedWeight=0; unsigned pixels=0,white=0;
            const auto inverseProjection=camera.GetInvProjection(), inverseRotation=camera.GetInvViewRotation();
            for(std::size_t i=0;i<std::size_t(kWidth)*kHeight;i+=4) {
                // F4는 L/(1+L)이다. half 양자화 뒤 역변환한 HDR 근사임을 명시한다.
                const double v=DiagnosticLuminance(rim,i);
                if(v>1e-6) {sum+=v;peak=std::max(peak,v);++pixels;
                    compositeHdr+=.2126*HdrChannel(composite,i,0)+.7152*HdrChannel(composite,i,1)+.0722*HdrChannel(composite,i,2);
                    compositeLdr+=(.2126*composite.rgba[i*4]+.7152*composite.rgba[i*4+1]+.0722*composite.rgba[i*4+2])/255.;
                    white+=composite.rgba[i*4]>=250&&composite.rgba[i*4+1]>=250&&composite.rgba[i*4+2]>=250;
                }
                const double opacity=1-HdrChannel(baseT,i);
                const float u=(float(i%kWidth)+.5f)/kWidth,w=(float(i/kWidth)+.5f)/kHeight;
                auto ray=DirectX::XMVector4Transform(DirectX::XMVectorSet(2*u-1,1-2*w,1,1),inverseProjection);
                ray=DirectX::XMVector3Normalize(DirectX::XMVector3TransformNormal(DirectX::XMVector3Normalize(ray),inverseRotation));
                DirectX::XMFLOAT3 direction;DirectX::XMStoreFloat3(&direction,ray);
                const auto& l=m_lightParameters;
                const auto p=stage7::EvaluateDualLobePhase({direction.x,direction.y,direction.z},{l.directionToSun.x,l.directionToSun.y,l.directionToSun.z},l.phaseEnabled>=.5f,l.forwardScatteringG,l.backwardScatteringG,l.phaseBlend,l.phaseIntensity);
                const float raw=l.phaseEnabled>=.5f?1+(std::clamp(p.dualLobe,0.f,16.f)-1)*l.phaseIntensity:1;
                opacitySum+=opacity; if(raw>cap)clippedWeight+=opacity;
            }
            std::ostringstream log; log<<"[RimHDR] type="<<types[type]<<" sun="<<suns[sun].name<<" cap="<<cap
                <<" sampled_rim_pixels="<<pixels<<" reconstructed_rim_luminance_mean="<<(pixels?sum/pixels:0)<<" max="<<peak
                <<" composite_hdr_mean="<<(pixels?compositeHdr/pixels:0)<<" composite_srgb_mean="<<(pixels?compositeLdr/pixels:0)
                <<" white_fraction="<<(pixels?double(white)/pixels:0)<<" opacity_weighted_phase_clipped="<<(opacitySum?clippedWeight/opacitySum:0);Log(log.str());
            if(captureLength && (sun==5 || (type==0 && sun==4))) {
                const auto stem=std::filesystem::path(captures)/(std::string(types[type])+"-"+suns[sun].name+"-cap"+std::to_string(cap));
                if(!SavePng(stem.wstring()+L".png",composite.rgba)||!WriteNew(stem.wstring()+L".rgba16f",composite.hdr.data(),composite.hdr.size()) ||
                   !SavePng(stem.wstring()+L"-silver.png",rim.rgba)) return false;
            }
            const auto directOne=frozenFrame(CloudDebugMode::AccumulatedDirectLighting);
            m_lightParameters.rimIntensity=0;
            const auto directZero=frozenFrame(CloudDebugMode::AccumulatedDirectLighting);
            if(!valid(directOne)||!valid(directZero))return false;
            for(std::size_t i=0;i<std::size_t(kWidth)*kHeight;i+=4) {
                const double direct=DiagnosticLuminance(directOne,i);
                const double expected=DiagnosticLuminance(directZero,i)+DiagnosticLuminance(rim,i);
                // F4 L/(1+L)의 half 양자화/역변환 오차를 고려한 상대 잔차.
                if(std::abs(direct-expected)/(1+std::max(direct,expected))>.003) {
                    Log("[Rim] Direct = base + rim FAIL");return false;
                }
            }
            for(const auto pair:{std::pair<float,float>{0.f,1.f},{2.f,.5f},{4.f,2.f}}) {
                m_lightParameters.rimIntensity=pair.first; m_lightParameters.rimDepthScale=pair.second;
                if(!same(baseMultiple,frozenFrame(CloudDebugMode::AccumulatedMultipleScattering))||!same(baseSky,frozenFrame(CloudDebugMode::AccumulatedSkyAmbient))||
                   !same(baseGround,frozenFrame(CloudDebugMode::AccumulatedGroundBounce))||!same(baseT,frozenFrame(CloudDebugMode::Transmittance))||
                   !same(baseDensity,frozenFrame(CloudDebugMode::FinalDensity))) {Log("[Rim] independent channels FAIL");return false;}
                const auto changedSun=frozenFrame(CloudDebugMode::VisibleSunTransmittance);
                if(!valid(changedSun))return false;
                // 거의 투명한 경계의 분모가 1e-6을 넘나들면 T 평균은 불안정하다.
                // 같은 opacity로 태양 T를 가중해 화면 기여의 평균/최대 오차를 검사한다.
                double weightedError=0,maxWeighted=0,maxVisible=0,weightSum=0;
                for(std::size_t i=0;i<std::size_t(kWidth)*kHeight;++i) {
                    const double w=1-HdrChannel(baseT,i),e=std::abs(HdrChannel(baseSunT,i)-HdrChannel(changedSun,i));
                    weightedError+=w*e;weightSum+=w;maxWeighted=std::max(maxWeighted,w*e);
                    const double a=HdrChannel(baseSunT,i),b=HdrChannel(changedSun,i);
                    if(w>.01)maxVisible=std::max(maxVisible,std::abs(a/(1+a)-b/(1+b)));
                }
                sunWeightedMeanMax=std::max(sunWeightedMeanMax,weightedError/std::max(weightSum,1.));
                sunWeightedMax=std::max(sunWeightedMax,maxWeighted);sunNormalizedMax=std::max(sunNormalizedMax,maxVisible);
                if(weightedError/std::max(weightSum,1.)>1e-5||maxWeighted>1e-3) {
                    std::ostringstream out;out<<"[RimSunInvariant] mean="<<weightedError/std::max(weightSum,1.)<<" weighted_max="<<maxWeighted<<" visible_max="<<maxVisible;Log(out.str());return false;
                }
                if(!valid(frozenFrame(CloudDebugMode::Composite))) return false;
                if(pair.first==0) {
                    const auto zero=frozenFrame(CloudDebugMode::SilverLiningContribution);
                    if(!valid(zero)) return false;
                    for(std::size_t i=0;i<std::size_t(kWidth)*kHeight;i+=13) if(HdrChannel(zero,i)!=0) return false;
                }
            }
        }
    }
    Log("[Rim] approved_reference_cases="+std::to_string(oldComparisons)+(approvedLength?" PASS":" (external archive not requested)"));
    {std::ostringstream out;out<<"[RimSunInvariant] max_weighted_mean="<<sunWeightedMeanMax<<" max_weighted_error="<<sunWeightedMax<<" observed_normalized_max="<<sunNormalizedMax;Log(out.str());}
    // 저고도 경계는 기본값뿐 아니라 가장 강한 후보에서도 유한값을 검사한다.
    for(int type=0;type<4;++type) for(float alt:{2.5f,2.999f,3.001f,3.5f,4.5f,4.999f,5.001f,5.5f}) {
        if(!setup(type,viewAz,alt))return false;
        m_lightParameters.rimIntensity=4;m_lightParameters.rimDepthScale=2;
        if(!valid(frame(CloudDebugMode::Composite))){Log("[Rim] low altitude finite FAIL");return false;}
    }
    if(!setup(0,viewAz,18)) return false;
    m_lightParameters.sunIntensity=0;
    const auto noSun=frame(CloudDebugMode::SilverLiningContribution);
    if(!valid(noSun)) {Log("[Rim] no-sun finite FAIL");return false;}
    for(std::size_t i=0;i<std::size_t(kWidth)*kHeight;i+=13)if(HdrChannel(noSun,i)!=0){Log("[Rim] no-sun nonzero FAIL");return false;}
    Log("[Rim] low altitude/no-sun PASS");
    // 대표 성능: 최대 림 후보, GPU만 직렬 측정. 캡처/UI/VSync 없음.
#if !defined(_DEBUG)
    if(!SetRimPhaseCapForValidation(8))return false;
    for(auto cameraId:{Stage13CameraPresetId::HeroDepth,Stage13CameraPresetId::CloudOverview})
    for(float altitude:{3.5f,18.f,70.f}) {
        if(!setup(0,viewAz,altitude))return false;
        const auto& preset=stage13camera::Get(cameraId);camera.SetLookAt(preset.position,preset.target);
        m_lightParameters.rimIntensity=4;m_lightParameters.rimDepthScale=2;SetDebugMode(CloudDebugMode::Composite);
        for(unsigned i=0;i<60;++i)Render(camera,kTime);
        std::vector<double> clouds,frames;
        for(unsigned i=0;i<180&&clouds.size()<120;++i){Render(camera,kTime);const auto& t=TimingSnapshot();if(t.gpuValid&&t.rawGpuFrameMs>0){clouds.push_back(t.rawGpuCloudMs);frames.push_back(t.rawGpuFrameMs);}}
        if(clouds.size()!=120)return false;
        std::sort(clouds.begin(),clouds.end());std::sort(frames.begin(),frames.end());const auto p95=std::size_t(std::ceil(.95*clouds.size()))-1;
        std::ostringstream log;log<<"[RimPerformance] "<<preset.diagnosticName<<" altitude="<<altitude<<" cloud_p95_ms="<<clouds[p95]<<" frame_p95_ms="<<frames[p95];Log(log.str());
        if(clouds[p95]>10||frames[p95]>16.67||HasDebugLayerErrors())return false;
    }
#endif
    if(!SetRimPhaseCapForValidation(2.5f)||!ApplySceneConcept(Stage15ConceptPreset::UrbanFairWeather))return false;
    Log("RIM_LIGHTING=PASS");return true;
}


bool Renderer::RunPresetSlotDiagnostics(Camera& camera, bool capture)
{
    SetAutomatedRenderMode(true); SetVSyncEnabled(false); EnableNoiseLabPreviews(false);
    const auto originalRoot = m_cloudFormationPresetRoot;
    const auto root = std::filesystem::temp_directory_path() /
        ("vcloud-slot-gpu-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    SetCloudFormationPresetRootForValidation(root);
    const auto check = [](bool ok, const char* name) { Log(std::string("[PresetSlots] ")+name+(ok?" PASS":" FAIL")); return ok; };
    bool passed = true;
    std::array<CloudFormationSettings,4> savedFormations;
    std::array<LightingPresetSettings,4> savedLights;
    for (unsigned i=0;i<4;++i) {
        const auto target=i==3?CustomFormationTarget():TypeFormationTarget(static_cast<CloudFormationType>(i));
        passed = check(ApplyCloudFormationPresetTarget(target,true), "select formation") && passed;
        auto formation=CurrentCloudFormation(); formation.coverage=.52f+i*.02f;
        auto before=CurrentLightingPreset();
        passed=check(ApplyCloudFormationSettingsForValidation(formation) && SaveSelectedFormation() &&
            LightingPresetEqual(before,CurrentLightingPreset()),"save formation keeps lighting") && passed;
        savedFormations[i]=CurrentCloudFormation();
        const auto slot=static_cast<Stage15ConceptPreset>(i+3);
        passed=check(ApplySceneConcept(slot),"select lighting") && passed;
        m_toneMappingParameters.exposureEv+=.123f;
        m_lightParameters.sunColor.y=.876f;
        const auto targetBefore=m_cloudFormationTarget;
        m_cloudFormationSource=CloudFormationPresetSource::Unsaved;
        passed=check(SaveSelectedLighting() && CloudFormationSettingsEqual(formation,CurrentCloudFormation()) &&
            CloudFormationPresetTargetEqual(targetBefore,m_cloudFormationTarget) &&
            m_cloudFormationSource==CloudFormationPresetSource::Unsaved,"save lighting keeps formation dirty state") && passed;
        savedLights[i]=CurrentLightingPreset();
    }
    for (unsigned i=0;i<4;++i) {
        const auto target=i==3?CustomFormationTarget():TypeFormationTarget(static_cast<CloudFormationType>(i));
        const auto slot=static_cast<Stage15ConceptPreset>(i+3);
        passed=check(ApplyCloudFormationPresetTarget(target,true) && ApplySceneConcept(slot) &&
            CloudFormationSettingsEqual(savedFormations[i],CurrentCloudFormation()) &&
            LightingPresetEqual(savedLights[i],CurrentLightingPreset()),"reload both slots") && passed;
        const auto before=CurrentCloudFormation(); const auto generation=m_weatherMapGeneration;
        const auto debug=m_atmosphereParameters.debugView=Stage14DebugView::OpticalDepth;
        passed=check(ApplySceneConcept(slot) && m_weatherMapGeneration==generation &&
            m_atmosphereParameters.debugView==debug && CloudFormationSettingsEqual(before,CurrentCloudFormation()),
            "lighting preserves Weather and diagnostics") && passed;
    }
    // 완료 표식이 없는 기존 Custom도 일반 시작에서 덮어쓰지 않아야 한다.
    const auto customPath = CloudFormationPresetPath(root, CustomFormationTarget());
    std::ifstream customInput(customPath, std::ios::binary);
    std::string customText((std::istreambuf_iterator<char>(customInput)), {});
    customInput.close();
    const std::string markerText = "\"snowDefaultInitialized\": 1";
    const auto marker = customText.find(markerText);
    if (marker != std::string::npos) customText.replace(marker, markerText.size(), "\"snowDefaultInitialized\": 0");
    { std::ofstream out(customPath, std::ios::binary); out << customText; }
    LoadUserPresetDefaults();
    std::ifstream customAfter(customPath, std::ios::binary);
    const std::string afterText((std::istreambuf_iterator<char>(customAfter)), {});
    customAfter.close();
    passed = check(customText == afterText, "startup preserves Custom bytes") && passed;
    // 손상 파일은 현재 선택·설정·편집 표식을 모두 보존한다.
    const auto before=CurrentLightingPreset(); const auto selected=m_stage15ConceptPreset;
    const auto badSlot=Stage15ConceptPreset::AutumnMorning;
    { std::ofstream out(LightingPresetPath(root,badSlot)); out << "{bad}"; }
    passed=check(!ApplySceneConcept(badSlot) && selected==m_stage15ConceptPreset &&
        LightingPresetEqual(before,CurrentLightingPreset()),"invalid JSON rollback") && passed;
    m_atmosphereParameters.debugView=Stage14DebugView::None;
    m_useSavedPresets=false;
    SetCloudMovementSpeedForValidation(0);
    const auto& f5=stage13camera::Get(Stage13CameraPresetId::HeroDepth);
    camera.SetLookAt(f5.position,f5.target);
    camera.SetClipPlanes(stage13camera::kNearPlaneMeters,stage13camera::kFarPlaneMeters);
    camera.SetFovYDegrees(60);
    const auto output=DefaultNoiseLabOutputRoot().parent_path()/"preset-slots"/std::to_string(GetTickCount64());
    if(capture) std::filesystem::create_directories(output);
    const char* names[]={"stratus","cumulus","altocumulus","custom"};
    for(unsigned type=0;type<4;++type) {
        const auto target=type==3?CustomFormationTarget():TypeFormationTarget(static_cast<CloudFormationType>(type));
        passed=ApplyCloudFormationPresetTarget(target,false)&&passed;
        for(unsigned light=0;light<4;++light) {
            passed=ApplySceneConcept(static_cast<Stage15ConceptPreset>(light+3))&&passed;
            for(int frame=0;frame<(capture?8:2);++frame) Render(camera,kTime);
            if(capture) {
                DirectionalLightingFrame image; m_directionalCaptureTarget=&image; Render(camera,kTime);
                const auto path=output/(std::string(names[type])+"-"+std::to_string(light+1)+".png");
                passed=check(image.valid && SavePng(path,image.rgba),path.filename().string().c_str())&&passed;
                const auto settings=output/(std::string(names[type])+"-"+std::to_string(light+1));
                std::string status;
                passed=SaveLightingPreset(settings,static_cast<Stage15ConceptPreset>(light+3),CurrentLightingPreset(),status)&&passed;
                passed=SaveCloudFormationPresetAtomic(settings/"formation.json",target,CurrentCloudFormation(),status)&&passed;
            }
        }
    }
    passed=check(!HasDebugLayerErrors(),"D3D11")&&passed;
    if(capture) Log("[PresetSlots] captures="+output.string());
    m_cloudFormationPresetRoot=originalRoot;
    std::error_code error; std::filesystem::remove_all(root,error);
    passed=!error&&passed;
    Log(std::string("PRESET_SLOTS=")+(passed?"PASS":"FAIL"));
    return passed;
}
