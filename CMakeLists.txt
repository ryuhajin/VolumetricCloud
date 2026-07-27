# ============================================================================
#  VolumetricCloud - DirectX11 + HLSL 레이마칭 학습 프로젝트
#  3단계: 절차식 노이즈 + 디버그 UI + 3D 텍스처 캐시
# ============================================================================
cmake_minimum_required(VERSION 3.20)

project(VolumetricCloud LANGUAGES CXX)

if (NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/imgui/imgui.cpp")
    message(FATAL_ERROR "Dear ImGui submodule missing. Run: git submodule update --init --recursive")
endif()

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
    src/DebugUI.cpp
    src/NoiseCacheManager.cpp

    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_demo.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_dx11.cpp
    third_party/imgui/backends/imgui_impl_win32.cpp
)

target_include_directories(VolumetricCloud PRIVATE
    src
    third_party/imgui
    third_party/imgui/backends
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
)

# 유니코드 윈도우 API 사용
# 개발 중에는 소스 트리의 shaders/를 직접 읽어 실행 중 HLSL 핫-리로드가 바로 반영되게 한다.
target_compile_definitions(VolumetricCloud PRIVATE
    UNICODE
    _UNICODE
    VCLOUD_SHADER_SOURCE_DIR="${CMAKE_SOURCE_DIR}/shaders"
    VCLOUD_CACHE_SOURCE_DIR="${CMAKE_SOURCE_DIR}/assets/noise-cache"
)

# 소스 파일이 UTF-8(한국어 주석 포함)임을 MSVC에 알림 (C4819 방지)
if (MSVC)
    target_compile_options(VolumetricCloud PRIVATE /utf-8)
endif()

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
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:VolumetricCloud>/assets/noise-cache
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets/noise-cache
        $<TARGET_FILE_DIR:VolumetricCloud>/assets/noise-cache
    COMMENT "Copying HLSL shaders next to the executable"
)

include(CTest)
if (BUILD_TESTING)
    add_test(NAME VolumetricCloud.CacheSmoke
        COMMAND $<TARGET_FILE:VolumetricCloud> --cache-smoke-test)
    set_tests_properties(VolumetricCloud.CacheSmoke PROPERTIES TIMEOUT 30)

    add_test(NAME VolumetricCloud.CodeTests
        COMMAND $<TARGET_FILE:VolumetricCloud> --run-code-tests)
    set_tests_properties(VolumetricCloud.CodeTests PROPERTIES TIMEOUT 180)
endif()
