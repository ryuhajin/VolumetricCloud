# ============================================================================
#  VolumetricCloud - DirectX11 + HLSL 레이마칭 학습 프로젝트
#  재구축 단계 0: 불투명 진단 장면, 깊이 복원과 풀스크린 합성 기반
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
    src/Camera.cpp
)

target_include_directories(VolumetricCloud PRIVATE src)

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
)

# 유니코드 윈도우 API 사용
# 개발 중에는 소스 트리의 shaders/를 직접 읽어 실행 중 HLSL 핫-리로드가 바로 반영되게 한다.
target_compile_definitions(VolumetricCloud PRIVATE
    UNICODE
    _UNICODE
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
