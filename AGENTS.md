# AGENTS.md — AI 에이전트 안내

작업 전에 이 문서와 관련 `doc/*.md`를 확인하세요.

## 프로젝트

DirectX11 + HLSL로 레이마칭과 볼류메트릭 클라우드를 학습하는 Windows 프로젝트입니다. 현재 평면 구름층, RGBA weather/3D 캐시, 디버그 UI, dual-lobe와 근사 다중 산란 라이팅까지 구현되어 있습니다.

## 빠른 사실

- C++17, HLSL shader model 5.0, DirectX 11, Win32
- 빌드: `cmake -B build -G "Visual Studio 17 2022" -A x64` 후 `cmake --build build --config Debug`
- 실행: `build/Debug/VolumetricCloud.exe`, F1 디버그 UI
- 정상 시작은 저장된 `.cso`, 128³/64³ volume과 512² weather cache를 사용
- 개발 중 HLSL 변경은 첫 Present 뒤 런타임 컴파일·재생성
- 문서/주석은 한국어, 소스 UTF-8, MSVC `/utf-8`

## 코드 지도

| 영역 | 위치 |
|---|---|
| 진입·테스트 플래그 | `src/main.cpp` |
| Win32 입력 | `src/Window.*` |
| 카메라 | `src/Camera.*` |
| D3D11 파이프라인 | `src/Renderer.*` |
| CPU 구름 상수 | `src/CloudParameters.h` |
| ImGui 패널 | `src/DebugUI.*` |
| 영구 캐시 | `src/NoiseCacheManager.*` |
| 공통 periodic 밀도 | `shaders/CloudNoise.hlsli` |
| 메인 레이마칭 | `shaders/VolumetricClouds.hlsl` |
| 볼륨 생성·GPU 검사 | `shaders/NoiseVolumeCS.hlsl` |

## 반드시 지킬 규칙

1. 코드를 바꾸면 관련 `doc/*.md`를 같은 변경에서 갱신합니다.
2. `CloudParameters.h`의 구조체, `CloudNoise.hlsli`의 `CloudCB`, `doc/ARCHITECTURE.md` 표를 동시에 맞춥니다.
3. periodic noise나 생성 파라미터 호환성이 깨지면 캐시 버전을 올리고 `assets/noise-cache/bundle`을 재생성합니다.
4. 변경 후 Debug/Release 빌드와 `ctest --test-dir build -C Release --output-on-failure`를 확인합니다.
5. 실제 렌더의 심미적 평가는 사용자 영역이며 에이전트 자동 테스트에는 스크린샷 비교를 넣지 않습니다.
6. 브랜치·커밋은 [doc/CONTRIBUTING.md](doc/CONTRIBUTING.md)를 따릅니다.
7. 코드를 바꾸는 브랜치는 `doc/changes/<브랜치명>.md` 작업 기록을 만들고, 변경 이유·대안·구현·검증 결과를 같은 커밋에서 갱신합니다. 파일명에서는 `/`를 `-`로 바꿉니다.
8. 공개 작업 기록에는 개인 면접 답변을 넣지 않습니다. 면접용 정리는 Git에서 제외된 `notes/interview/`에만 작성합니다.

## 문서

- [doc/ARCHITECTURE.md](doc/ARCHITECTURE.md)
- [doc/RAYMARCHING.md](doc/RAYMARCHING.md)
- [doc/FOLDER_STRUCTURE.md](doc/FOLDER_STRUCTURE.md)
- [doc/ROADMAP.md](doc/ROADMAP.md)
- [doc/CONTRIBUTING.md](doc/CONTRIBUTING.md)
- [doc/changes/TEMPLATE.md](doc/changes/TEMPLATE.md)
