# CLAUDE.md — AI 에이전트 안내

이 파일은 Claude Code 등 AI 에이전트가 이 저장소에서 작업할 때 가장 먼저 읽는 진입점입니다.
**작업을 시작하기 전에 이 문서와 아래 링크된 문서를 먼저 확인하세요.**

## 이 프로젝트가 무엇인가

DirectX11 + HLSL로 **레이마칭을 학습**하고, 최종적으로 **볼류메트릭 클라우드**를 렌더링하는
학습 프로젝트입니다. 현재는 **1단계: noise·light 없는 반투명 안개 구**를 레이마칭으로 그립니다.

## 빠른 사실 (Quick Facts)

- **언어/환경:** C++17, HLSL(shader model 5.0), DirectX 11, Win32, Windows
- **빌드:** CMake (`cmake -B build -G "Visual Studio 17 2022" -A x64` → `cmake --build build --config Debug`)
- **실행:** `build/Debug/VolumetricCloud.exe` (창 + 마우스 드래그 회전 / 휠 줌)
- **셰이더:** 런타임 컴파일(`D3DCompileFromFile`). HLSL만 고치고 재실행하면 반영됨
  (단 빌드 시 `shaders/`가 exe 옆으로 복사되어야 함 — CMake POST_BUILD가 처리)
- **렌더 방식:** 정점 버퍼 없이 풀스크린 삼각형 1개를 그리고, 픽셀 셰이더에서 레이마칭
- **문서/주석 언어:** 한국어
- **소스 인코딩:** UTF-8. MSVC는 `/utf-8` 플래그로 컴파일 (CMake에 설정됨)

## 코드 지도

| 영역 | 위치 | 한 줄 설명 |
|------|------|-----------|
| 진입점 | `src/main.cpp` | 창·카메라·렌더러 생성 + 메인 루프 |
| 윈도우/입력 | `src/Window.*` | Win32 창, 마우스 → Camera, 리사이즈 → Renderer |
| 카메라 | `src/Camera.*` | 오빗 카메라 → view/proj/invViewProj |
| 렌더러 | `src/Renderer.*` | D3D11 초기화, 셰이더 컴파일, 상수버퍼, 드로우 |
| VS | `shaders/Fullscreen.hlsl` | 풀스크린 삼각형 |
| PS | `shaders/RaymarchSphere.hlsl` | ray-sphere 교차 + Beer-Lambert 적분 |

## 반드시 지킬 규칙

1. **문서 동기화:** 코드를 바꾸면 관련 `doc/*.md`를 같은 변경에서 갱신합니다.
   상수버퍼는 `Renderer.h`의 `CameraCB` ↔ `RaymarchSphere.hlsl`의 `cbCamera` ↔
   `doc/ARCHITECTURE.md` 표 **세 곳을 동시에** 맞춥니다.
2. **브랜치/커밋 규칙**을 따릅니다 → [doc/CONTRIBUTING.md](doc/CONTRIBUTING.md)
3. **빌드가 깨지지 않게** 유지합니다. 변경 후 위 빌드 명령으로 확인하세요.
4. 현재 1단계 범위 밖(noise/light/ImGui 등)은 [doc/ROADMAP.md](doc/ROADMAP.md)의 다음 단계로 분리합니다.

## 더 읽을 문서

- [doc/ARCHITECTURE.md](doc/ARCHITECTURE.md) — 모듈 구조 · 파이프라인 · 상수버퍼
- [doc/RAYMARCHING.md](doc/RAYMARCHING.md) — 레이마칭 수식 (레이 생성 · 교차 · Beer-Lambert)
- [doc/FOLDER_STRUCTURE.md](doc/FOLDER_STRUCTURE.md) — 폴더/파일 역할
- [doc/ROADMAP.md](doc/ROADMAP.md) — 단계별 계획
- [doc/CONTRIBUTING.md](doc/CONTRIBUTING.md) — 문서/브랜치/커밋 규칙
