# ============================================================================
#  VolumetricCloud - DirectX11 + HLSL 레이마칭 학습 프로젝트
#  1단계: noise/light 없는 반투명 안개 구(sphere) 렌더링
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
target_compile_definitions(VolumetricCloud PRIVATE UNICODE _UNICODE)

# 소스 파일이 UTF-8(한국어 주석 포함)임을 MSVC에 알림 (C4819 방지)
if (MSVC)
    target_compile_options(VolumetricCloud PRIVATE /utf-8)
endif()

# ----------------------------------------------------------------------------
#  셰이더 폴더를 실행 파일 옆으로 복사 (런타임에 HLSL을 파일에서 읽어 컴파일)
#  빌드 후 build/<Config>/shaders/ 에 hlsl이 위치하게 됨
# ----------------------------------------------------------------------------
add_custom_command(TARGET VolumetricCloud POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/shaders
        $<TARGET_FILE_DIR:VolumetricCloud>/shaders
    COMMENT "Copying HLSL shaders next to the executable"
)
