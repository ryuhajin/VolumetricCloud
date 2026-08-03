# AGENTS.md — AI 에이전트 안내

이 파일은 Codex 등 AI 에이전트가 이 저장소에서 작업할 때 가장 먼저 읽는 진입점입니다.
**작업을 시작하기 전에 이 문서와 아래 링크된 문서를 먼저 확인하세요.**

## 이 프로젝트가 무엇인가

DirectX11 + HLSL로 **레이마칭을 학습**하고, 최종적으로 **볼류메트릭 클라우드**를 렌더링하는
학습 프로젝트입니다. 현재는 **재구축 단계 3**을 진행하며, 월드 Y 높이 비율,
독립적인 상·하단 fade와 Noise Lab 높이 단면을 검증합니다.

## 빠른 사실 (Quick Facts)

- **언어/환경:** C++17, HLSL(shader model 5.0), DirectX 11, Win32, Windows
- **빌드:** CMake (`cmake -B build -G "Visual Studio 17 2022" -A x64` → `cmake --build build --config Debug`)
- **실행:** `build/Debug/VolumetricCloud.exe` (숫자 0~9, Z/X/C/V/B/M, F5~F8, Q/W/E/R/T, N/A/S/D/F/G/H/K)
- **셰이더:** 런타임 컴파일(`D3DCompileFromFile`) + 실행 중 핫-리로드.
  개발 중에는 소스 `shaders/`를 우선 읽고, 없으면 exe 옆 `shaders/`로 폴백
- **렌더 방식:** 정점 버퍼 없이 풀스크린 삼각형 1개를 그리고, 픽셀 셰이더에서 레이마칭
- **문서/주석 언어:** 한국어
- **소스 인코딩:** UTF-8. MSVC는 `/utf-8` 플래그로 컴파일 (CMake에 설정됨)

## 코드 지도

| 영역 | 위치 | 한 줄 설명 |
|------|------|-----------|
| 진입점 | `src/main.cpp` | 창·카메라·렌더러 생성 + 메인 루프 |
| 윈도우/입력 | `src/Window.*` | Win32 창, 마우스 → Camera, 리사이즈 → Renderer |
| 카메라 | `src/Camera.*` | 오빗 카메라 → view/proj/invViewProj |
| 렌더러 | `src/Renderer.*` | D3D11 초기화, 진단 장면, 깊이 SRV, 합성 패스 |
| 노이즈 도구 | `src/NoiseLab.*` | ImGui 3축 단면, 파라미터 조절, PNG/JSON 내보내기 |
| 구름 설정 | `src/CloudParameters.h` | 96바이트 CPU/HLSL 공유 파라미터와 디버그 모드 |
| VS | `shaders/Fullscreen.hlsl` | 풀스크린 삼각형 |
| Scene | `shaders/DiagnosticScene.hlsl` | 깊이 검증용 불투명 평면·박스 |
| Ray | `shaders/Ray.hlsli` | 평행축을 안전하게 처리하는 slab AABB 교차 |
| PS | `shaders/VolumetricClouds.hlsl` | noise 밀도 적분과 합성 |
| Noise | `shaders/Noise.hlsli` | 구름·Noise Lab 공용 3D noise, coverage와 높이 프로파일 |
| 수치 기준 | `src/Stage1VolumeMath.h` | 단계 1 CPU 회귀 검사용 교차·적분 |
| Noise 기준 | `src/Stage2NoiseMath.h` | 단계 2 CPU 회귀 검사용 noise·밀도·바람 좌표 |
| 높이 기준 | `src/Stage3HeightMath.h` | 단계 3 CPU 회귀 검사용 높이·fade·최종 밀도 |

## 반드시 지킬 규칙

1. **문서 동기화:** 코드를 바꾸면 관련 `doc/*.md`를 같은 변경에서 갱신합니다.
   상수버퍼는 C++ 구조체 ↔ HLSL cbuffer ↔ `doc/ARCHITECTURE.md` 표
   **세 곳을 동시에** 맞춥니다.
2. **브랜치/커밋 규칙**을 따릅니다 → [doc/CONTRIBUTING.md](doc/CONTRIBUTING.md)
3. **빌드가 깨지지 않게** 유지합니다. 변경 후 위 빌드 명령으로 확인하세요.
4. 현재 단계 3 범위 밖(detail noise, weather, light, early exit, temporal)은 다음 단계로 분리합니다.

## 로컬 단계별 구현 문서 규칙

각 구현 단계를 시작할 때 `notes/<단계>단계-<주요업데이트>.md`를 만들고 구현 전·중·후 계속 갱신합니다.
예: `notes/0단계-기반구성.md`, `notes/1단계-상수밀도-AABB.md`.

- 문서 위에는 볼류메트릭 클라우드 0~15단계 전체 흐름을 간단히 적고 현재 단계를 표시합니다.
- 현재 단계는 처음 구현해 보는 중학생도 이해할 수 있도록 용어 정의, 비유, 필요한 이유와 화면 문제가 무엇인지 깊게 설명합니다.
- 이전 단계에서 재사용하는 기능, 이번에 추가한 기능, 의도적으로 미룬 기능을 구분합니다.
- CPU→GPU→셰이더→화면 데이터 흐름, 좌표계·단위, GPU 리소스, 수식, 파일 책임, 파라미터와 디버깅 방법을 기록합니다.
- 마지막에는 에이전트 자동 검증과 사용자 렌더 검증을 분리한 체크리스트를 둡니다. 화면 항목과 최종 승인은 사용자가 직접 체크합니다.
- 사용자 승인 후 승인일, 피드백, 수정과 재검증 결과를 기록한 뒤 다음 단계로 넘어갑니다.
- 자세한 형식은 로컬 전용 `notes/README.md`와 `notes/TEMPLATE.md`를 따릅니다.
- `notes/`는 `.gitignore` 대상이며 공식 `doc/*.md`와 `doc/changes/`를 대체하지 않습니다. 코드 변경 시 공식 문서도 계속 같은 변경에서 갱신합니다.

## 더 읽을 문서

- [doc/ARCHITECTURE.md](doc/ARCHITECTURE.md) — 모듈 구조 · 파이프라인 · 상수버퍼
- [doc/RAYMARCHING.md](doc/RAYMARCHING.md) — 레이마칭 수식 (레이 생성 · 교차 · Beer-Lambert)
- [doc/FOLDER_STRUCTURE.md](doc/FOLDER_STRUCTURE.md) — 폴더/파일 역할
- [doc/ROADMAP.md](doc/ROADMAP.md) — 단계별 계획
- [doc/CONTRIBUTING.md](doc/CONTRIBUTING.md) — 문서/브랜치/커밋 규칙
