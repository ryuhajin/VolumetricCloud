# ============================================================================
#  VolumetricCloud - High 단일 DirectX 11 볼류메트릭 클라우드
# ============================================================================
cmake_minimum_required(VERSION 3.20)
project(VolumetricCloud LANGUAGES CXX RC)

# 기본 배포/회귀는 성능 우선. 엄격한 해시 검증은 별도 build 디렉터리에서 켠다.
option(VCLOUD_STRICT_VALIDATION "Enable strict HLSL arithmetic and auxiliary bit-exact tests" OFF)
option(VCLOUD_BUILD_TESTS "Build the separate local validation runner and CTest suite" OFF)
option(VCLOUD_ENABLE_DIAGNOSTIC_TESTS
    "Register large-output unresolved cloud diagnostic tests with CTest" OFF)
message(STATUS "VCLOUD_STRICT_VALIDATION=${VCLOUD_STRICT_VALIDATION}")
message(STATUS "VCLOUD_ENABLE_DIAGNOSTIC_TESTS=${VCLOUD_ENABLE_DIAGNOSTIC_TESTS}")
message(STATUS "VCLOUD_BUILD_TESTS=${VCLOUD_BUILD_TESTS}")
if(VCLOUD_ENABLE_DIAGNOSTIC_TESTS AND NOT VCLOUD_BUILD_TESTS)
    message(FATAL_ERROR "Diagnostic tests require VCLOUD_BUILD_TESTS=ON and the local tests package")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT VolumetricCloud)

set(VCLOUD_RUNTIME_SOURCES
    src/Window.cpp
    src/VolumetricCloud.rc
    src/Renderer.cpp
    src/LightingPresetStore.cpp
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
add_executable(VolumetricCloud WIN32 src/main.cpp ${VCLOUD_RUNTIME_SOURCES})
target_sources(VolumetricCloud PRIVATE ${VCLOUD_SHADER_SOURCES})
source_group("Shaders" FILES ${VCLOUD_SHADER_SOURCES})

function(configure_vcloud_app target shader_root test_hooks)
target_include_directories(${target} PRIVATE src third_party/imgui)
target_link_libraries(${target} PRIVATE
    d3d11 dxgi d3dcompiler windowscodecs setupapi advapi32 bcrypt)
target_compile_definitions(${target} PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    VCLOUD_STRICT_VALIDATION=$<BOOL:${VCLOUD_STRICT_VALIDATION}>
    VCLOUD_TEST_HOOKS=${test_hooks}
    VCLOUD_SHADER_SOURCE_DIR="${shader_root}"
    VCLOUD_PRESET_SOURCE_DIR="${CMAKE_SOURCE_DIR}/presets"
)

if(MSVC)
    target_compile_options(${target} PRIVATE /utf-8)
    target_link_options(${target} PRIVATE /MANIFEST:NO)
endif()



# 원본 JSON만 바뀌어도 매 build에 동기화한다. 런타임의 개발 저장 대상은 원본이다.
set(VCLOUD_PRESET_FILES types/stratus.json types/cumulus.json types/mixed.json
    custom-cloud.json lighting/1.json lighting/2.json lighting/3.json lighting/4.json)
add_custom_target(${target}PresetSync
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:${target}>/presets/types"
        "$<TARGET_FILE_DIR:${target}>/presets/lighting")
foreach(preset IN LISTS VCLOUD_PRESET_FILES)
    add_custom_command(TARGET ${target}PresetSync POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/presets/${preset}"
            "$<TARGET_FILE_DIR:${target}>/presets/${preset}"
        VERBATIM)
endforeach()
add_dependencies(${target} ${target}PresetSync)
add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_FILE_DIR:${target}>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${shader_root}" "$<TARGET_FILE_DIR:${target}>/shaders"
    COMMENT "Copying ${target} shaders next to the executable")
endfunction()

configure_vcloud_app(VolumetricCloud "${CMAKE_SOURCE_DIR}/shaders" 0)
if(VCLOUD_BUILD_TESTS)
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/tests/RegisterTests.cmake")
        message(FATAL_ERROR "Local tests package is absent. Use VCLOUD_BUILD_TESTS=OFF for the runtime-only checkout.")
    endif()
    enable_testing()
    include(tests/RegisterTests.cmake)
endif()
