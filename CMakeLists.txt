# ============================================================================
#  VolumetricCloud - High 단일 DirectX 11 볼류메트릭 클라우드
# ============================================================================
cmake_minimum_required(VERSION 3.20)
project(VolumetricCloud LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT VolumetricCloud)

add_executable(VolumetricCloud WIN32
    src/main.cpp
    src/Window.cpp
    src/VolumetricCloud.rc
    src/Renderer.cpp
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

target_include_directories(VolumetricCloud PRIVATE src third_party/imgui)
target_link_libraries(VolumetricCloud PRIVATE
    d3d11 dxgi d3dcompiler windowscodecs setupapi advapi32 bcrypt)
target_compile_definitions(VolumetricCloud PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
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
set_tests_properties(VolumetricCloud.HighPerformance PROPERTIES
    TIMEOUT 600 LABELS "gpu;performance")

add_custom_command(TARGET VolumetricCloud POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        $<TARGET_FILE_DIR:VolumetricCloud>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/shaders
        $<TARGET_FILE_DIR:VolumetricCloud>/shaders
    COMMENT "Copying High-only HLSL shaders next to the executable")
