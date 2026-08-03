# ============================================================================
#  VolumetricCloud - DirectX11 + HLSL 레이마칭 학습 프로젝트
#  재구축 단계 4: Base Shape와 단일 Value Noise Detail Erosion 분리
# ============================================================================
cmake_minimum_required(VERSION 3.20)

project(VolumetricCloud LANGUAGES CXX)

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
    src/Renderer.cpp
    src/NoiseLab.cpp
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
