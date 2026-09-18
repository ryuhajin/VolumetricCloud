# ============================================================================
#  VolumetricCloud - High 단일 DirectX 11 볼류메트릭 클라우드
# ============================================================================
cmake_minimum_required(VERSION 3.20)
project(VolumetricCloud LANGUAGES CXX RC)

# 기본 배포/회귀는 성능 우선. 엄격한 해시 검증은 별도 build 디렉터리에서 켠다.
option(VCLOUD_STRICT_VALIDATION "Enable strict HLSL arithmetic and auxiliary bit-exact tests" OFF)
message(STATUS "VCLOUD_STRICT_VALIDATION=${VCLOUD_STRICT_VALIDATION}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT VolumetricCloud)

add_executable(VolumetricCloud WIN32
    src/main.cpp
    src/Window.cpp
    src/VolumetricCloud.rc
    src/Renderer.cpp
    src/RendererDeterminism.cpp
    src/LightingPresetStore.cpp
    tests/DirectionalLightingBaseline.cpp
    src/CloudFormationSettings.cpp
    src/CloudFormationPresetStore.cpp
    src/DeveloperUiSettings.cpp
    src/NoiseLab.cpp
    src/FrameProfiler.cpp
    src/WeatherMap.cpp
    src/Camera.cpp
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_win32.cpp
    third_party/imgui/backends/imgui_impl_dx11.cpp
)

# IDE 탐색용 원본 목록. HLSL은 빌드하지 않고 기존 런타임 컴파일을 유지한다.
set(VCLOUD_SHADER_SOURCES
    shaders/SolarOcclusionProbe.hlsl
    shaders/CloudAdvection.hlsli
    shaders/CloudDeepShadow.hlsl
    shaders/CloudDomainParameters.hlsli
    shaders/CloudEnvironment.hlsli
    shaders/CloudLighting.hlsli
    shaders/CloudParameters.hlsli
    shaders/CloudShapeParameters.hlsli
    shaders/DiagnosticScene.hlsl
    shaders/EnvironmentParameters.hlsli
    shaders/Fullscreen.hlsl
    shaders/HighCloudQuality.hlsli
    shaders/LightParameters.hlsli
    shaders/Noise.hlsli
    shaders/NoiseLab.hlsl
    shaders/NoiseVolume.hlsl
    shaders/NoiseVolumeParameters.hlsli
    shaders/PhaseFunction.hlsli
    shaders/Stage12Shadow.hlsli
    shaders/Stage12ShadowParameters.hlsli
    shaders/Stage14Atmosphere.hlsli
    shaders/Stage14AtmosphereLut.hlsl
    shaders/Stage14ToneMap.hlsl
    shaders/VolumetricClouds.hlsl
    shaders/Weather.hlsli
    shaders/WeatherColumnParameters.hlsli
    shaders/WeatherMapCompute.hlsl
)
set_source_files_properties(${VCLOUD_SHADER_SOURCES} PROPERTIES HEADER_FILE_ONLY TRUE)
target_sources(VolumetricCloud PRIVATE ${VCLOUD_SHADER_SOURCES})
source_group("Shaders" FILES ${VCLOUD_SHADER_SOURCES})

target_include_directories(VolumetricCloud PRIVATE src third_party/imgui)
target_link_libraries(VolumetricCloud PRIVATE
    d3d11 dxgi d3dcompiler windowscodecs setupapi advapi32 bcrypt)
target_compile_definitions(VolumetricCloud PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    VCLOUD_STRICT_VALIDATION=$<BOOL:${VCLOUD_STRICT_VALIDATION}>
    VCLOUD_SHADER_SOURCE_DIR="${CMAKE_SOURCE_DIR}/shaders"
)

if(MSVC)
    target_compile_options(VolumetricCloud PRIVATE /utf-8)
    target_link_options(VolumetricCloud PRIVATE /MANIFEST:NO)
endif()

enable_testing()

function(add_vcloud_cpu_test target test_name)
    add_executable(${target} ${ARGN})
    target_include_directories(${target} PRIVATE src)
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8)
    endif()
    add_test(NAME ${test_name} COMMAND ${target})
endfunction()

add_vcloud_cpu_test(VolumetricCloud.FoundationTests
    VolumetricCloud.FoundationMath
    tests/FoundationTests.cpp src/Camera.cpp)
add_vcloud_cpu_test(VolumetricCloud.Stage1VolumeMathTests
    VolumetricCloud.Stage1VolumeMath tests/Stage1VolumeMathTests.cpp)
add_vcloud_cpu_test(VolumetricCloud.Stage2NoiseMathTests
    VolumetricCloud.Stage2NoiseMath tests/Stage2NoiseMathTests.cpp)
add_vcloud_cpu_test(VolumetricCloud.Stage3HeightMathTests
    VolumetricCloud.Stage3HeightMath tests/Stage3HeightMathTests.cpp)
add_vcloud_cpu_test(VolumetricCloud.Stage4DetailMathTests
    VolumetricCloud.Stage4DetailMath tests/Stage4DetailMathTests.cpp)
add_vcloud_cpu_test(VolumetricCloud.Stage5WeatherMathTests
    VolumetricCloud.Stage5WeatherMath
    tests/Stage5WeatherMathTests.cpp src/WeatherMap.cpp)
add_vcloud_cpu_test(VolumetricCloud.Stage6LightMathTests
    VolumetricCloud.Stage6LightMath tests/Stage6LightMathTests.cpp)
add_vcloud_cpu_test(VolumetricCloud.Stage7PhaseMathTests
    VolumetricCloud.Stage7PhaseMath tests/Stage7PhaseMathTests.cpp)
add_vcloud_cpu_test(VolumetricCloud.Stage8AmbientMathTests
    VolumetricCloud.Stage8AmbientMath tests/Stage8AmbientMathTests.cpp)

add_vcloud_cpu_test(VolumetricCloud.FrameProfilerMathTests
    VolumetricCloud.FrameProfilerMath
    tests/FrameProfilerMathTests.cpp src/FrameProfiler.cpp)
target_link_libraries(VolumetricCloud.FrameProfilerMathTests PRIVATE d3d11)

add_vcloud_cpu_test(VolumetricCloud.Stage13ScaleMathTests
    VolumetricCloud.Stage13ScaleMath tests/Stage13ScaleMathTests.cpp)
add_vcloud_cpu_test(VolumetricCloud.Stage13CloudDomainMathTests
    VolumetricCloud.Stage13CloudDomainMath
    tests/Stage13CloudDomainMathTests.cpp)
add_vcloud_cpu_test(VolumetricCloud.Stage13SimilarityCameraMathTests
    VolumetricCloud.Stage13SimilarityCameraMath
    tests/Stage13SimilarityCameraMathTests.cpp src/Camera.cpp)
add_vcloud_cpu_test(VolumetricCloud.Stage13OpenWorldMathTests
    VolumetricCloud.Stage13OpenWorldMath
    tests/Stage13OpenWorldMathTests.cpp)
add_vcloud_cpu_test(VolumetricCloud.Stage13NoiseVolumeMathTests
    VolumetricCloud.Stage13NoiseVolumeMath
    tests/Stage13NoiseVolumeMathTests.cpp src/WeatherMap.cpp)
add_vcloud_cpu_test(VolumetricCloud.Stage13WeatherShapeMathTests
    VolumetricCloud.Stage13WeatherShapeMath
    tests/Stage13WeatherShapeMathTests.cpp src/WeatherMap.cpp)
add_vcloud_cpu_test(VolumetricCloud.Stage13CameraControlMathTests
    VolumetricCloud.Stage13CameraControlMath
    tests/Stage13CameraControlMathTests.cpp src/Camera.cpp)
add_vcloud_cpu_test(VolumetricCloud.Stage13SceneMathTests
    VolumetricCloud.Stage13UnifiedSceneMath
    tests/Stage13SceneMathTests.cpp)

add_vcloud_cpu_test(VolumetricCloud.HighCloudQualityTests
    VolumetricCloud.HighCloudQuality
    tests/HighCloudQualityTests.cpp)
add_vcloud_cpu_test(VolumetricCloud.Fnv1a64Tests
    VolumetricCloud.Fnv1a64 tests/Fnv1a64Tests.cpp)
target_compile_definitions(VolumetricCloud.HighCloudQualityTests PRIVATE
    VCLOUD_HIGH_QUALITY_HLSL="${CMAKE_SOURCE_DIR}/shaders/HighCloudQuality.hlsli")
add_vcloud_cpu_test(VolumetricCloud.ShaderManifestTests
    VolumetricCloud.ShaderManifest
    tests/ShaderManifestTests.cpp)
target_compile_definitions(VolumetricCloud.ShaderManifestTests PRIVATE
    VCLOUD_SHADER_SOURCE_DIR="${CMAKE_SOURCE_DIR}/shaders")
add_vcloud_cpu_test(VolumetricCloud.Stage12ShadowMathTests
    VolumetricCloud.Stage12ShadowMath tests/Stage12ShadowMathTests.cpp)
add_vcloud_cpu_test(VolumetricCloud.Stage14AtmosphereMathTests
    VolumetricCloud.Stage14AtmosphereMath
    tests/Stage14AtmosphereMathTests.cpp)
add_vcloud_cpu_test(VolumetricCloud.CloudFormationPresetStoreTests
    VolumetricCloud.CloudFormationPresetStore
    tests/CloudFormationPresetStoreTests.cpp
    src/CloudFormationSettings.cpp
    src/CloudFormationPresetStore.cpp
    src/WeatherMap.cpp)
add_vcloud_cpu_test(VolumetricCloud.Stage15PresetMathTests
    VolumetricCloud.Stage15PresetMath
    tests/Stage15PresetMathTests.cpp
    src/CloudFormationSettings.cpp
    src/CloudFormationPresetStore.cpp
    src/WeatherMap.cpp)
add_vcloud_cpu_test(VolumetricCloud.DeveloperUiSettingsTests
    VolumetricCloud.DeveloperUiSettings
    tests/DeveloperUiSettingsTests.cpp src/DeveloperUiSettings.cpp)

add_test(NAME VolumetricCloud.HighCloudSmoke
         COMMAND VolumetricCloud --high-cloud-smoke-test)
add_test(NAME VolumetricCloud.FormationSmoke
         COMMAND VolumetricCloud --formation-smoke-test)
add_test(NAME VolumetricCloud.WeatherGpuSmoke
         COMMAND VolumetricCloud --formation-smoke-test)
add_test(NAME VolumetricCloud.AtmosphereSmoke
         COMMAND VolumetricCloud --atmosphere-smoke-test)
add_test(NAME VolumetricCloud.NoiseLabSmoke
         COMMAND VolumetricCloud --noise-lab-smoke-test)
add_test(NAME VolumetricCloud.ShaderCacheSmoke
         COMMAND VolumetricCloud --shader-cache-smoke-test)
add_test(NAME VolumetricCloud.HotReloadSmoke
    COMMAND ${CMAKE_COMMAND}
        -DVCLOUD_EXE=$<TARGET_FILE:VolumetricCloud>
        -DVCLOUD_SHADER_SOURCE=${CMAKE_SOURCE_DIR}/shaders
        -DVCLOUD_WORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/shader-reload-smoke/tone
        -DVCLOUD_MODE=tone
        -P ${CMAKE_SOURCE_DIR}/cmake/RunShaderReloadSmoke.cmake)
add_test(NAME VolumetricCloud.HotReloadDependencySmoke
    COMMAND ${CMAKE_COMMAND}
        -DVCLOUD_EXE=$<TARGET_FILE:VolumetricCloud>
        -DVCLOUD_SHADER_SOURCE=${CMAKE_SOURCE_DIR}/shaders
        -DVCLOUD_WORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/shader-reload-smoke/noise
        -DVCLOUD_MODE=noise
        -P ${CMAKE_SOURCE_DIR}/cmake/RunShaderReloadSmoke.cmake)
add_test(NAME VolumetricCloud.WeatherHotReloadSmoke
    COMMAND ${CMAKE_COMMAND}
        -DVCLOUD_EXE=$<TARGET_FILE:VolumetricCloud>
        -DVCLOUD_SHADER_SOURCE=${CMAKE_SOURCE_DIR}/shaders
        -DVCLOUD_WORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/shader-reload-smoke/weather
        -DVCLOUD_MODE=weather
        -P ${CMAKE_SOURCE_DIR}/cmake/RunShaderReloadSmoke.cmake)
set_tests_properties(
    VolumetricCloud.HighCloudSmoke
    VolumetricCloud.FormationSmoke
    VolumetricCloud.WeatherGpuSmoke
    VolumetricCloud.AtmosphereSmoke
    VolumetricCloud.NoiseLabSmoke
    VolumetricCloud.ShaderCacheSmoke
    PROPERTIES TIMEOUT 180 LABELS "gpu")
set_tests_properties(VolumetricCloud.HotReloadSmoke PROPERTIES
    TIMEOUT 45 LABELS "gpu;reload")
set_tests_properties(VolumetricCloud.HotReloadDependencySmoke PROPERTIES
    TIMEOUT 90 LABELS "gpu;reload;heavy")
set_tests_properties(VolumetricCloud.WeatherHotReloadSmoke PROPERTIES
    TIMEOUT 45 LABELS "gpu;reload")

add_test(NAME VolumetricCloud.HighPerformance
         COMMAND VolumetricCloud --high-performance-test)
add_test(NAME VolumetricCloud.DirectionalLightingDiagnostics
         COMMAND VolumetricCloud --directional-lighting-diagnostics)
add_test(NAME VolumetricCloud.DensityShaping
         COMMAND VolumetricCloud --density-shaping-test)
add_test(NAME VolumetricCloud.BaseOctaves
         COMMAND VolumetricCloud --base-octave-test)
set_tests_properties(VolumetricCloud.HighPerformance PROPERTIES
    TIMEOUT 600 LABELS "gpu;performance")

find_program(VCLOUD_POWERSHELL NAMES pwsh powershell REQUIRED)
add_test(NAME VolumetricCloud.DeterminismComparator
    COMMAND ${VCLOUD_POWERSHELL} -NoProfile -ExecutionPolicy Bypass
        -File ${CMAKE_SOURCE_DIR}/tests/RunDeterminism.ps1 -SelfTest)
if(VCLOUD_STRICT_VALIDATION)
    add_test(NAME VolumetricCloud.DeterminismSmoke
        COMMAND ${VCLOUD_POWERSHELL} -NoProfile -ExecutionPolicy Bypass
            -File ${CMAKE_SOURCE_DIR}/tests/RunDeterminism.ps1
            -Exe $<TARGET_FILE:VolumetricCloud>
            -ShaderRoot ${CMAKE_SOURCE_DIR}/shaders
            -WorkRoot ${CMAKE_CURRENT_BINARY_DIR}/determinism/$<CONFIG>
            -Sets 3 -Detailed)
    set_tests_properties(VolumetricCloud.DeterminismSmoke PROPERTIES
        TIMEOUT 900 LABELS "gpu;determinism;strict-validation" RESOURCE_LOCK vcloud_gpu)
endif()
# 실제 CameraCB 상태 회귀는 일반 Release에서도 실행한다. 이미지 해시 일치를 요구하지 않는다.
add_test(NAME VolumetricCloud.RendererStateSmoke
    COMMAND VolumetricCloud --determinism-smoke-test --determinism-state-test)
set_tests_properties(VolumetricCloud.RendererStateSmoke PROPERTIES
    TIMEOUT 180 LABELS "gpu;state")
set_tests_properties(
    VolumetricCloud.HighCloudSmoke VolumetricCloud.FormationSmoke
    VolumetricCloud.WeatherGpuSmoke VolumetricCloud.AtmosphereSmoke
    VolumetricCloud.NoiseLabSmoke VolumetricCloud.ShaderCacheSmoke
    VolumetricCloud.HotReloadSmoke VolumetricCloud.HotReloadDependencySmoke
    VolumetricCloud.WeatherHotReloadSmoke VolumetricCloud.HighPerformance
    VolumetricCloud.RendererStateSmoke PROPERTIES RESOURCE_LOCK vcloud_gpu)

add_custom_command(TARGET VolumetricCloud POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        $<TARGET_FILE_DIR:VolumetricCloud>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/shaders
        $<TARGET_FILE_DIR:VolumetricCloud>/shaders
    COMMENT "Copying High-only HLSL shaders next to the executable")

add_test(NAME VolumetricCloud.LightingTuning COMMAND VolumetricCloud --lighting-tuning-test)
set_tests_properties(VolumetricCloud.LightingTuning PROPERTIES TIMEOUT 300 RESOURCE_LOCK vcloud_gpu LABELS "gpu;lighting")

add_test(NAME VolumetricCloud.SolarOcclusion COMMAND VolumetricCloud --solar-occlusion-test)
set_tests_properties(VolumetricCloud.SolarOcclusion PROPERTIES TIMEOUT 300 RESOURCE_LOCK vcloud_gpu LABELS "gpu;lighting")

add_test(NAME VolumetricCloud.SolarBanding COMMAND VolumetricCloud --solar-banding-test)
add_test(NAME VolumetricCloud.SolarTransition COMMAND VolumetricCloud --solar-transition-test)
add_test(NAME VolumetricCloud.RimLighting COMMAND VolumetricCloud --rim-lighting-test)
set_tests_properties(VolumetricCloud.RimLighting PROPERTIES TIMEOUT 900 RESOURCE_LOCK vcloud_gpu LABELS "gpu;lighting")
set_tests_properties(VolumetricCloud.SolarTransition PROPERTIES TIMEOUT 600 RESOURCE_LOCK vcloud_gpu LABELS "gpu;lighting")
set_tests_properties(VolumetricCloud.SolarBanding PROPERTIES TIMEOUT 600 RESOURCE_LOCK vcloud_gpu LABELS "gpu;lighting")

# 05-B 미채택 후보: 기존 12 case와 같은 측정 조건으로 높이 저장 비용을 분리한다.
add_test(NAME VolumetricCloud.ShadowHeightPerformance
         COMMAND VolumetricCloud --high-performance-test --shadow-height-2x)
set_tests_properties(VolumetricCloud.ShadowHeightPerformance PROPERTIES
    TIMEOUT 600 RESOURCE_LOCK vcloud_gpu LABELS "gpu;performance;lighting")

add_vcloud_cpu_test(VolumetricCloud.LightingPresetStoreTests
    VolumetricCloud.LightingPresetStore tests/LightingPresetStoreTests.cpp
    src/LightingPresetStore.cpp src/CloudFormationSettings.cpp src/CloudFormationPresetStore.cpp src/WeatherMap.cpp)

add_test(NAME VolumetricCloud.PresetSlotsSmoke COMMAND VolumetricCloud --preset-slots-test)
set_tests_properties(VolumetricCloud.PresetSlotsSmoke PROPERTIES RESOURCE_LOCK vcloud_gpu TIMEOUT 180)
