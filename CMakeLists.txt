# ============================================================================
#  VolumetricCloud - DirectX11 + HLSL 레이마칭 학습 프로젝트
#  재구축 단계 8: 분석적 환경광과 저비용 다중 산란
# ============================================================================
cmake_minimum_required(VERSION 3.20)

project(VolumetricCloud LANGUAGES CXX RC)

# C++17 사용 (DirectXMath, 표준 라이브러리)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Visual Studio에서 이 프로젝트를 시작 프로젝트로 설정
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT VolumetricCloud)

# ----------------------------------------------------------------------------
#  실행 파일 (WIN32 = 콘솔 창 없는 GUI 앱, 진입점은 WinMain)
# ----------------------------------------------------------------------------
add_executable(VolumetricCloud WIN32
    src/main.cpp
    src/Window.cpp
    src/VolumetricCloud.rc
    src/Renderer.cpp
    src/CloudAppearance.cpp
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

target_include_directories(VolumetricCloud PRIVATE
    src
    third_party/imgui
)

# ----------------------------------------------------------------------------
#  DirectX11 관련 라이브러리 링크
#   d3d11      : Direct3D 11 코어 (device, context)
#   dxgi       : 스왑체인, 어댑터
#   d3dcompiler: 런타임 HLSL 컴파일 (D3DCompileFromFile)
# ----------------------------------------------------------------------------
target_link_libraries(VolumetricCloud PRIVATE
    d3d11
    dxgi
    d3dcompiler
    windowscodecs
    setupapi
    advapi32
    bcrypt
)

# 유니코드 윈도우 API 사용
# 개발 중에는 소스 트리의 shaders/를 직접 읽어 실행 중 HLSL 핫-리로드가 바로 반영되게 한다.
target_compile_definitions(VolumetricCloud PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    VCLOUD_SHADER_SOURCE_DIR="${CMAKE_SOURCE_DIR}/shaders"
)

# 소스 파일이 UTF-8(한국어 주석 포함)임을 MSVC에 알림 (C4819 방지)
if (MSVC)
    target_compile_options(VolumetricCloud PRIVATE /utf-8)
    # PMv2 manifest는 VolumetricCloud.rc의 RT_MANIFEST가 소유한다. 링크 후 자동
    # manifest가 같은 resource ID를 덮어쓰지 않게 한다.
    target_link_options(VolumetricCloud PRIVATE /MANIFEST:NO)
endif()

# 카메라 역투영 수학은 GPU 렌더와 별도로 빠르게 회귀 검사한다.
enable_testing()
add_executable(VolumetricCloud.FoundationTests
    tests/FoundationTests.cpp
    src/Camera.cpp
)
target_include_directories(VolumetricCloud.FoundationTests PRIVATE src)
if (MSVC)
    target_compile_options(VolumetricCloud.FoundationTests PRIVATE /utf-8)
endif()
add_test(NAME VolumetricCloud.FoundationMath
         COMMAND VolumetricCloud.FoundationTests)
add_test(NAME VolumetricCloud.FoundationSmoke
         COMMAND VolumetricCloud --foundation-smoke-test)

# 단계 1의 slab 교차와 상수 밀도 적분을 GPU와 독립된 수치 기준으로 검사한다.
add_executable(VolumetricCloud.Stage1VolumeMathTests
    tests/Stage1VolumeMathTests.cpp
)
target_include_directories(VolumetricCloud.Stage1VolumeMathTests PRIVATE src)
if (MSVC)
    target_compile_options(VolumetricCloud.Stage1VolumeMathTests PRIVATE /utf-8)
endif()
add_test(NAME VolumetricCloud.Stage1VolumeMath
         COMMAND VolumetricCloud.Stage1VolumeMathTests)
add_test(NAME VolumetricCloud.Stage1Smoke
         COMMAND VolumetricCloud --stage1-smoke-test)

# 단계 2의 절차적 value noise, coverage remap과 바람 좌표를 CPU에서 검사한다.
add_executable(VolumetricCloud.Stage2NoiseMathTests
    tests/Stage2NoiseMathTests.cpp
)
target_include_directories(VolumetricCloud.Stage2NoiseMathTests PRIVATE src)
if (MSVC)
    target_compile_options(VolumetricCloud.Stage2NoiseMathTests PRIVATE /utf-8)
endif()
add_test(NAME VolumetricCloud.Stage2NoiseMath
         COMMAND VolumetricCloud.Stage2NoiseMathTests)
add_test(NAME VolumetricCloud.Stage2Smoke
         COMMAND VolumetricCloud --stage2-smoke-test)
add_test(NAME VolumetricCloud.NoiseLabSmoke
         COMMAND VolumetricCloud --noise-lab-smoke-test)
add_test(NAME VolumetricCloud.ShaderHotReloadSmoke
         COMMAND VolumetricCloud --shader-hot-reload-smoke-test)
add_test(NAME VolumetricCloud.ShaderCacheSmoke
         COMMAND VolumetricCloud --shader-cache-smoke-test)

# 단계 3의 정규화 높이, 상·하단 smoothstep과 최종 밀도 결합을 CPU에서 검사한다.
add_executable(VolumetricCloud.Stage3HeightMathTests
    tests/Stage3HeightMathTests.cpp
)
target_include_directories(VolumetricCloud.Stage3HeightMathTests PRIVATE src)
if (MSVC)
    target_compile_options(VolumetricCloud.Stage3HeightMathTests PRIVATE /utf-8)
endif()
add_test(NAME VolumetricCloud.Stage3HeightMath
         COMMAND VolumetricCloud.Stage3HeightMathTests)
add_test(NAME VolumetricCloud.Stage3Smoke
         COMMAND VolumetricCloud --stage3-smoke-test)

# 단계 4의 독립 Detail 좌표, subtractive erosion과 빈 Base sample skip을 검사한다.
add_executable(VolumetricCloud.Stage4DetailMathTests
    tests/Stage4DetailMathTests.cpp
)
target_include_directories(VolumetricCloud.Stage4DetailMathTests PRIVATE src)
if (MSVC)
    target_compile_options(VolumetricCloud.Stage4DetailMathTests PRIVATE /utf-8)
endif()
add_test(NAME VolumetricCloud.Stage4DetailMath
         COMMAND VolumetricCloud.Stage4DetailMathTests)
add_test(NAME VolumetricCloud.Stage4Smoke
         COMMAND VolumetricCloud --stage4-smoke-test)

# 단계 5의 CPU 맵 채널, 월드 UV, coverage와 cloud type 프로파일을 검사한다.
add_executable(VolumetricCloud.Stage5WeatherMathTests
    tests/Stage5WeatherMathTests.cpp
    src/WeatherMap.cpp
)
target_include_directories(VolumetricCloud.Stage5WeatherMathTests PRIVATE src)
if (MSVC)
    target_compile_options(VolumetricCloud.Stage5WeatherMathTests PRIVATE /utf-8)
endif()
add_test(NAME VolumetricCloud.Stage5WeatherMath
         COMMAND VolumetricCloud.Stage5WeatherMathTests)
add_test(NAME VolumetricCloud.Stage5Smoke
         COMMAND VolumetricCloud --stage5-smoke-test)

# 단계 6의 태양 투과율, Base-only Light Ray와 단일 산란 수식을 검사한다.
add_executable(VolumetricCloud.Stage6LightMathTests
    tests/Stage6LightMathTests.cpp
)
target_include_directories(VolumetricCloud.Stage6LightMathTests PRIVATE src)
if (MSVC)
    target_compile_options(VolumetricCloud.Stage6LightMathTests PRIVATE /utf-8)
endif()
add_test(NAME VolumetricCloud.Stage6LightMath
         COMMAND VolumetricCloud.Stage6LightMathTests)
add_test(NAME VolumetricCloud.Stage6Smoke
         COMMAND VolumetricCloud --stage6-smoke-test)

# 7단계 전 성능 오버레이의 EMA 수학과 비동기 D3D query 경로를 검사한다.
add_executable(VolumetricCloud.FrameProfilerMathTests
    tests/FrameProfilerMathTests.cpp
    src/FrameProfiler.cpp
)
target_include_directories(VolumetricCloud.FrameProfilerMathTests PRIVATE src)
if (MSVC)
    target_compile_options(VolumetricCloud.FrameProfilerMathTests PRIVATE /utf-8)
endif()
target_link_libraries(VolumetricCloud.FrameProfilerMathTests PRIVATE d3d11)
add_test(NAME VolumetricCloud.FrameProfilerMath
         COMMAND VolumetricCloud.FrameProfilerMathTests)
add_test(NAME VolumetricCloud.PerformanceOverlaySmoke
         COMMAND VolumetricCloud --performance-overlay-smoke-test)

# 단계 7의 방향 부호, Dual-lobe HG와 등방성 회귀를 CPU와 D3D 경로에서 검사한다.
add_executable(VolumetricCloud.Stage7PhaseMathTests
    tests/Stage7PhaseMathTests.cpp
)
target_include_directories(VolumetricCloud.Stage7PhaseMathTests PRIVATE src)
if (MSVC)
    target_compile_options(VolumetricCloud.Stage7PhaseMathTests PRIVATE /utf-8)
endif()
add_test(NAME VolumetricCloud.Stage7PhaseMath
         COMMAND VolumetricCloud.Stage7PhaseMathTests)
add_test(NAME VolumetricCloud.Stage7Smoke
         COMMAND VolumetricCloud --stage7-smoke-test)

# 단계 8의 분석적 환경광, AO와 광학 깊이 재사용 다중 산란을 검사한다.
add_executable(VolumetricCloud.Stage8AmbientMathTests
    tests/Stage8AmbientMathTests.cpp
)
target_include_directories(VolumetricCloud.Stage8AmbientMathTests PRIVATE src)
if (MSVC)
    target_compile_options(VolumetricCloud.Stage8AmbientMathTests PRIVATE /utf-8)
endif()
add_test(NAME VolumetricCloud.Stage8AmbientMath
         COMMAND VolumetricCloud.Stage8AmbientMathTests)
add_test(NAME VolumetricCloud.Stage8Smoke
         COMMAND VolumetricCloud --stage8-smoke-test)

# 단계 13-0은 렌더 경로를 바꾸기 전에 meter 기반 상사 변환의 단위 관계를 검사한다.
add_executable(VolumetricCloud.Stage13ScaleMathTests
    tests/Stage13ScaleMathTests.cpp
)
target_include_directories(VolumetricCloud.Stage13ScaleMathTests PRIVATE src)
if (MSVC)
    target_compile_options(VolumetricCloud.Stage13ScaleMathTests PRIVATE /utf-8)
endif()
add_test(NAME VolumetricCloud.Stage13ScaleMath
         COMMAND VolumetricCloud.Stage13ScaleMathTests)

# 단계 13-1의 Y 평면층 교차, 수평 레이, 깊이 제한과 거리 fade를 검사한다.
add_executable(VolumetricCloud.Stage13CloudDomainMathTests
    tests/Stage13CloudDomainMathTests.cpp
)
target_include_directories(VolumetricCloud.Stage13CloudDomainMathTests PRIVATE src)
if (MSVC)
    target_compile_options(VolumetricCloud.Stage13CloudDomainMathTests PRIVATE /utf-8)
endif()
add_test(NAME VolumetricCloud.Stage13CloudDomainMath
         COMMAND VolumetricCloud.Stage13CloudDomainMathTests)
add_test(NAME VolumetricCloud.Stage13DomainSmoke
         COMMAND VolumetricCloud --stage13-domain-smoke-test)

add_executable(VolumetricCloud.Stage13SimilarityCameraMathTests
    tests/Stage13SimilarityCameraMathTests.cpp
    src/Camera.cpp
)
target_include_directories(VolumetricCloud.Stage13SimilarityCameraMathTests PRIVATE src)
if (MSVC)
    target_compile_options(VolumetricCloud.Stage13SimilarityCameraMathTests PRIVATE /utf-8)
endif()
add_test(NAME VolumetricCloud.Stage13SimilarityCameraMath
         COMMAND VolumetricCloud.Stage13SimilarityCameraMathTests)
add_test(NAME VolumetricCloud.Stage13SimilarityGpu
         COMMAND VolumetricCloud --stage13-similarity-gpu-test)

# 단계 13-3 실제 km 시작값, sampling budget과 네 고정 카메라 GPU 경로를 검사한다.
add_executable(VolumetricCloud.Stage13OpenWorldMathTests
    tests/Stage13OpenWorldMathTests.cpp
)
target_include_directories(VolumetricCloud.Stage13OpenWorldMathTests PRIVATE src)
if (MSVC)
    target_compile_options(VolumetricCloud.Stage13OpenWorldMathTests PRIVATE /utf-8)
endif()
add_test(NAME VolumetricCloud.Stage13OpenWorldMath
         COMMAND VolumetricCloud.Stage13OpenWorldMathTests)
add_test(NAME VolumetricCloud.Stage13OpenWorldSmoke
         COMMAND VolumetricCloud --stage13-open-world-smoke-test)

# 단계 13-4 periodic Base/Detail Texture3D 규격, seam, cache와 GPU 경로를 검사한다.
add_executable(VolumetricCloud.Stage13NoiseVolumeMathTests
    tests/Stage13NoiseVolumeMathTests.cpp
    src/WeatherMap.cpp
)
target_include_directories(VolumetricCloud.Stage13NoiseVolumeMathTests PRIVATE src)
if (MSVC)
    target_compile_options(VolumetricCloud.Stage13NoiseVolumeMathTests PRIVATE /utf-8)
endif()
add_test(NAME VolumetricCloud.Stage13NoiseVolumeMath
         COMMAND VolumetricCloud.Stage13NoiseVolumeMathTests)
add_test(NAME VolumetricCloud.Stage13NoiseVolumeSmoke
         COMMAND VolumetricCloud --stage13-noise-volume-smoke-test)

# 단계 13-4B Weather 기반 물리 두께, 타입 세로 프로파일과 GPU 단면을 검사한다.
add_executable(VolumetricCloud.Stage13WeatherShapeMathTests
    tests/Stage13WeatherShapeMathTests.cpp
    src/WeatherMap.cpp
)
target_include_directories(VolumetricCloud.Stage13WeatherShapeMathTests PRIVATE src)
if (MSVC)
    target_compile_options(VolumetricCloud.Stage13WeatherShapeMathTests PRIVATE /utf-8)
endif()
add_test(NAME VolumetricCloud.Stage13WeatherShapeMath
         COMMAND VolumetricCloud.Stage13WeatherShapeMathTests)
add_test(NAME VolumetricCloud.Stage13WeatherShapeGpu
         COMMAND VolumetricCloud --stage13-weather-shape-gpu-test)

# 단계 13-4E Dense Broken-Sky 외형, 점유율, density 결합과 Custom JSON을 검사한다.
add_executable(VolumetricCloud.CloudAppearanceTests
    tests/CloudAppearanceTests.cpp
    src/CloudAppearance.cpp
    src/WeatherMap.cpp
)
target_include_directories(VolumetricCloud.CloudAppearanceTests PRIVATE src)
if(MSVC)
    target_compile_options(VolumetricCloud.CloudAppearanceTests PRIVATE /utf-8)
endif()
add_test(NAME VolumetricCloud.CloudAppearance
         COMMAND VolumetricCloud.CloudAppearanceTests)

# 단계 13-4B 사용자 카메라 위치, 건물 분리와 일반/고속 휠 줌 수학을 검사한다.
add_executable(VolumetricCloud.Stage13CameraControlMathTests
    tests/Stage13CameraControlMathTests.cpp
    src/Camera.cpp
)
target_include_directories(VolumetricCloud.Stage13CameraControlMathTests PRIVATE src)
if (MSVC)
    target_compile_options(VolumetricCloud.Stage13CameraControlMathTests PRIVATE /utf-8)
endif()
add_test(NAME VolumetricCloud.Stage13CameraControlMath
         COMMAND VolumetricCloud.Stage13CameraControlMathTests)

add_executable(VolumetricCloud.Stage13SceneMathTests
    tests/Stage13SceneMathTests.cpp
)
target_include_directories(VolumetricCloud.Stage13SceneMathTests PRIVATE src)
if(MSVC)
    target_compile_options(VolumetricCloud.Stage13SceneMathTests PRIVATE /utf-8)
endif()
add_test(NAME VolumetricCloud.Stage13UnifiedSceneMath
         COMMAND VolumetricCloud.Stage13SceneMathTests)
add_test(NAME VolumetricCloud.Stage13UnifiedSceneSmoke
         COMMAND VolumetricCloud --stage13-unified-scene-smoke-test)
set_tests_properties(VolumetricCloud.Stage13UnifiedSceneSmoke PROPERTIES TIMEOUT 120)

# 단계 13-5 km 광학, Light sampling 후보와 Detail 거리 LOD 수학을 검사한다.
add_executable(VolumetricCloud.Stage13OpticsLightingMathTests
    tests/Stage13OpticsLightingMathTests.cpp
)
target_include_directories(VolumetricCloud.Stage13OpticsLightingMathTests PRIVATE src)
if(MSVC)
    target_compile_options(VolumetricCloud.Stage13OpticsLightingMathTests PRIVATE /utf-8)
endif()
add_test(NAME VolumetricCloud.Stage13OpticsLightingMath
         COMMAND VolumetricCloud.Stage13OpticsLightingMathTests)
add_test(NAME VolumetricCloud.Stage13OpticsLightingSmoke
         COMMAND VolumetricCloud --stage13-optics-lighting-smoke-test)
set_tests_properties(VolumetricCloud.Stage13OpticsLightingSmoke
                     PROPERTIES TIMEOUT 180)

# 단계 9 가변 View 간격, preset ABI와 deterministic Light cone 구간을 검사한다.
add_executable(VolumetricCloud.Stage9OptimizationMathTests
    tests/Stage9OptimizationMathTests.cpp
)
target_include_directories(VolumetricCloud.Stage9OptimizationMathTests PRIVATE src)
if(MSVC)
    target_compile_options(VolumetricCloud.Stage9OptimizationMathTests PRIVATE /utf-8)
endif()
add_test(NAME VolumetricCloud.Stage9OptimizationMath
         COMMAND VolumetricCloud.Stage9OptimizationMathTests)
add_test(NAME VolumetricCloud.Stage9OptimizationSmoke
         COMMAND VolumetricCloud --stage9-optimization-smoke-test)
set_tests_properties(VolumetricCloud.Stage9OptimizationSmoke
                     PROPERTIES TIMEOUT 300)

# 단계 10 해상도 크기, UV 중심, 대표 깊이와 joint 가중치 기준을 검사한다.
add_executable(VolumetricCloud.Stage10UpsamplingMathTests
    tests/Stage10UpsamplingMathTests.cpp
)
target_include_directories(VolumetricCloud.Stage10UpsamplingMathTests PRIVATE src)
if(MSVC)
    target_compile_options(VolumetricCloud.Stage10UpsamplingMathTests PRIVATE /utf-8)
endif()
add_test(NAME VolumetricCloud.Stage10UpsamplingMath
         COMMAND VolumetricCloud.Stage10UpsamplingMathTests)
add_test(NAME VolumetricCloud.Stage10UpsamplingSmoke
         COMMAND VolumetricCloud --stage10-upsampling-smoke-test)
set_tests_properties(VolumetricCloud.Stage10UpsamplingSmoke
                     PROPERTIES TIMEOUT 300)

# 단계 11 4-phase jitter, 재투영, 거부, clipping과 EMA 기준을 검사한다.
add_executable(VolumetricCloud.Stage11TemporalMathTests
    tests/Stage11TemporalMathTests.cpp
)
target_include_directories(VolumetricCloud.Stage11TemporalMathTests PRIVATE src)
if(MSVC)
    target_compile_options(VolumetricCloud.Stage11TemporalMathTests PRIVATE /utf-8)
endif()
add_test(NAME VolumetricCloud.Stage11TemporalMath
         COMMAND VolumetricCloud.Stage11TemporalMathTests)
add_test(NAME VolumetricCloud.Stage11TemporalSmoke
         COMMAND VolumetricCloud --stage11-temporal-smoke-test)
set_tests_properties(VolumetricCloud.Stage11TemporalSmoke
                     PROPERTIES TIMEOUT 300)

# 단계 12 light-space, cascade, slice, Beer-Lambert와 cache ABI를 검사한다.
add_executable(VolumetricCloud.Stage12ShadowMathTests
    tests/Stage12ShadowMathTests.cpp
)
target_include_directories(VolumetricCloud.Stage12ShadowMathTests PRIVATE src)
if(MSVC)
    target_compile_options(VolumetricCloud.Stage12ShadowMathTests PRIVATE /utf-8)
endif()
add_test(NAME VolumetricCloud.Stage12ShadowMath
         COMMAND VolumetricCloud.Stage12ShadowMathTests)
add_test(NAME VolumetricCloud.Stage12ShadowSmoke
         COMMAND VolumetricCloud --stage12-shadow-smoke-test)
set_tests_properties(VolumetricCloud.Stage12ShadowSmoke
                     PROPERTIES TIMEOUT 300)

# 단계 14 구면 대기, 밀도·위상·Beer-Lambert, LUT 좌표와 HDR 출력을 검사한다.
add_executable(VolumetricCloud.Stage14AtmosphereMathTests
    tests/Stage14AtmosphereMathTests.cpp
)
target_include_directories(VolumetricCloud.Stage14AtmosphereMathTests PRIVATE src)
if (MSVC)
    target_compile_options(VolumetricCloud.Stage14AtmosphereMathTests PRIVATE /utf-8)
endif()
add_test(NAME VolumetricCloud.Stage14AtmosphereMath
         COMMAND VolumetricCloud.Stage14AtmosphereMathTests)
add_test(NAME VolumetricCloud.Stage14AtmosphereSmoke
         COMMAND VolumetricCloud --stage14-atmosphere-smoke-test)
set_tests_properties(VolumetricCloud.Stage14AtmosphereSmoke
                     PROPERTIES TIMEOUT 300)

# 단계 15 품질/콘셉트 resolver, 소유권 분리, Cirrus 좌표와 b7 ABI를 검사한다.
add_executable(VolumetricCloud.Stage15PresetMathTests
    tests/Stage15PresetMathTests.cpp
    src/CloudAppearance.cpp
    src/WeatherMap.cpp
)
target_include_directories(VolumetricCloud.Stage15PresetMathTests PRIVATE src)
if (MSVC)
    target_compile_options(VolumetricCloud.Stage15PresetMathTests PRIVATE /utf-8)
endif()
add_test(NAME VolumetricCloud.Stage15PresetMath
         COMMAND VolumetricCloud.Stage15PresetMathTests)
add_test(NAME VolumetricCloud.Stage15PresetSmoke
         COMMAND VolumetricCloud --stage15-preset-smoke-test)
add_test(NAME VolumetricCloud.Stage15OutputDpiSmoke
         COMMAND VolumetricCloud --stage15-output-dpi-smoke-test)
add_test(NAME VolumetricCloud.Stage15CaptureSmoke
         COMMAND VolumetricCloud --stage15-capture-smoke-test)
set_tests_properties(
    VolumetricCloud.Stage15PresetSmoke
    VolumetricCloud.Stage15OutputDpiSmoke
    VolumetricCloud.Stage15CaptureSmoke
    PROPERTIES TIMEOUT 300)

# ----------------------------------------------------------------------------
#  셰이더 폴더를 실행 파일 옆으로 복사 (런타임에 HLSL을 파일에서 읽어 컴파일)
#  빌드 후 build/<Config>/shaders/ 에 hlsl이 위치하게 됨
# ----------------------------------------------------------------------------
add_custom_command(TARGET VolumetricCloud POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        $<TARGET_FILE_DIR:VolumetricCloud>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/shaders
        $<TARGET_FILE_DIR:VolumetricCloud>/shaders
    COMMENT "Copying HLSL shaders next to the executable"
)
