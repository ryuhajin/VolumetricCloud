# AGENTS.md — AI 에이전트 안내

이 파일은 Codex 등 AI 에이전트가 이 저장소에서 작업할 때 가장 먼저 읽는 진입점입니다.
**작업을 시작하기 전에 이 문서와 아래 링크된 문서를 먼저 확인하세요.**

## 이 프로젝트가 무엇인가

DirectX11 + HLSL로 **레이마칭을 학습**하고, 최종적으로 **볼류메트릭 클라우드**를 렌더링하는
학습 프로젝트입니다. 단계 0~8과 **단계 13 대규모 평면 구름층**은 사용자 승인을 받았으며,
현재는 단계 9 기본 최적화를 진행합니다. 이전 단계 9 최적화와 초기 단계 13
평면 구름층 실험은 별도 브랜치에 보관했습니다. 새 포트폴리오 계획은 사용자 승인을 받았고
단계 13-0 공간 단위 계약과 단계 13-1 AABB/평면층 교차는 사용자 승인을 받았고,
단계 13-2 상사 확대와 단계 13-3 실제 오픈 월드 스케일은 2026-08-11 사용자 승인을
받았고, **단계 13-4B Weather 기반 가변 두께와 3D texture 형태**는 2026-08-14,
**단계 13-4C 개발 UI와 Local Cloud Inspector**는 2026-08-16 사용자 승인을 받았습니다.
13-4C 이력은 보존하되 현재 런타임은 **단계 13-4D 단일 포트폴리오 디버깅 씬**이
Local Inspector를 대체합니다. **단계 13-4D 단일 씬, 13-4E Dense Broken-Sky와 타입 프리셋,
13-5 km 조명**까지 2026-08-17 최종 승인했습니다. 구형 shell 계획은 취소했고 PlanarLayer를
최종 대규모 도메인으로 사용합니다. 실행 순서는 `9 → 10 → 11 → 12 → 14 → 15`입니다.

## 빠른 사실 (Quick Facts)

- **언어/환경:** C++17, HLSL(shader model 5.0), DirectX 11, Win32, Windows
- **빌드:** CMake (`cmake -B build -G "Visual Studio 17 2022" -A x64` → `cmake --build build --config Debug`)
- **실행:** `build/Debug/VolumetricCloud.exe` (숫자 0~9 Debug View, F1~F4 UI, F5~F8 카메라)
- **셰이더:** 런타임 컴파일(`D3DCompileFromFile`) + 실행 중 핫-리로드.
  개발 중에는 소스 `shaders/`를 우선 읽고, 없으면 exe 옆 `shaders/`로 폴백
- **렌더 방식:** 정점 버퍼 없이 풀스크린 삼각형 1개를 그리고, 픽셀 셰이더에서 레이마칭
- **문서/주석 언어:** 한국어
- **소스 인코딩:** UTF-8. MSVC는 `/utf-8` 플래그로 컴파일 (CMake에 설정됨)

## 코드 지도

| 영역 | 위치 | 한 줄 설명 |
|------|------|-----------|
| 진입점 | `src/main.cpp` | 창·카메라·렌더러 생성 + 메인 루프 |
| 윈도우/입력 | `src/Window.*` | Win32 창, 마우스/WASD → Camera, 리사이즈 → Renderer |
| 카메라 | `src/Camera.*` | 오빗·rig 평행 이동 카메라 → view/proj/invViewProj |
| 렌더러 | `src/Renderer.*` | D3D11 초기화, 진단 장면, 깊이 SRV, 합성 패스 |
| 노이즈 도구 | `src/NoiseLab.*` | ImGui 3축 단면, 파라미터 조절, PNG/JSON 내보내기 |
| 구름 설정 | `src/CloudParameters.h` | 128바이트 CPU/HLSL 공유 파라미터와 디버그 모드 |
| 거리 LOD 설정 | `src/CloudLodParameters.h` | 16바이트 b8 Detail 거리 LOD와 측정 중립 평균 |
| 최적화 설정 | `src/OptimizationParameters.h` | 64바이트 b9 View/Light 후보와 단계 9 preset |
| 도메인 설정 | `src/CloudDomainParameters.h` | AABB/평면층 선택과 meter 단위 추적 범위 |
| Weather Map | `src/WeatherMap.*` | 256² CPU RGBA(coverage/type/density/local thickness) 프리셋 생성과 해시 |
| 외형 프리셋 | `src/CloudAppearance.*` | Dense Mixed·층운·적운과 schema 29 Custom 원자 저장/복원 |
| 형상 설정 | `src/CloudShapeParameters.h` | 64바이트 b7 물리 두께·타입별 Vertical Profile 설정 |
| 조명 설정 | `src/LightParameters.h` | 80바이트 LightCB와 태양·외곽 범위 Phase 프리셋·sanitize |
| 환경광 설정 | `src/EnvironmentParameters.h` | 80바이트 EnvironmentCB와 태양 차폐 기반 환경광 프리셋·sanitize |
| 성능 계측 | `src/FrameProfiler.*` | 8-slot 비동기 D3D11 timestamp와 CPU/GPU EMA |
| VS | `shaders/Fullscreen.hlsl` | 풀스크린 삼각형 |
| Scene | `shaders/DiagnosticScene.hlsl` | 깊이 검증용 불투명 평면·박스 |
| Ray | `shaders/Ray.hlsli` | 평행축을 안전하게 처리하는 slab AABB 교차 |
| PS | `shaders/VolumetricClouds.hlsl` | noise 밀도 적분과 합성 |
| Noise | `shaders/Noise.hlsli` | 교체 가능한 Base/Detail noise, 높이와 erosion 밀도 함수 |
| Lighting | `shaders/CloudLighting.hlsli` | Base-only 태양 Light Ray와 직접 단일 산란 |
| Phase | `shaders/PhaseFunction.hlsli` | 방향 부호가 고정된 Dual-lobe HG와 안전한 Phase Factor |
| Environment | `shaders/CloudEnvironment.hlsli` | 하늘·지면·AO와 광학 깊이 재사용 다중 산란 |
| 수치 기준 | `src/Stage1VolumeMath.h` | 단계 1 CPU 회귀 검사용 교차·적분 |
| Noise 기준 | `src/Stage2NoiseMath.h` | 단계 2 CPU 회귀 검사용 noise·밀도·바람 좌표 |
| 높이 기준 | `src/Stage3HeightMath.h` | 단계 3 CPU 회귀 검사용 높이·fade·최종 밀도 |
| Detail 기준 | `src/Stage4DetailMath.h` | 단계 4 CPU 회귀 검사용 Detail 좌표·침식·샘플 생략 |
| Weather 기준 | `src/Stage5WeatherMath.h` | 단계 5 CPU 회귀 검사용 UV·coverage·구름 종류 프로파일 |
| 조명 기준 | `src/Stage6LightMath.h` | 단계 6 CPU 회귀 검사용 광학 깊이·단일 산란 |
| Phase 기준 | `src/Stage7PhaseMath.h` | 단계 7 CPU 회귀 검사용 HG·방향·Dual-lobe 수학 |
| 환경광 기준 | `src/Stage8AmbientMath.h` | 단계 8 CPU 회귀 검사용 높이·AO·octave 수학 |
| 단위 기준 | `src/Stage13ScaleMath.h` | 단계 13-0 CPU 회귀와 13-2 런타임 프리셋의 meter 상사 변환 수학 |
| 오픈 월드 기준 | `src/Stage13OpenWorldMath.h` | 단계 13-3 실제 km 시작값과 sampling budget 수학 |
| 3D noise 기준 | `src/Stage13NoiseVolumeMath.h` | 단계 13-4 Texture3D 규격·주기 noise CPU 기준 수학 |
| 3D noise cache | `src/NoiseVolumeCache.h` | 테스트 전용 cache header·hash·원자적 저장 검증 |
| Weather 형상 기준 | `src/Stage13WeatherShapeMath.h` | 단계 13-4E 점유율·두께 분포·타입 프로파일·주파수 CPU 기준 수학 |
| 카메라 프리셋 | `src/Stage13CameraPresets.h` | 단일 씬 F5~F8 위치·타깃 기준 |
| 단일 씬 기준 | `src/Stage13SceneMath.h` | 10km 지면·20층 건물·50km·입력·이동·숫자 매핑 기준 |
| km 광학 기준 | `src/Stage13OpticsLightingMath.h` | Light 후보·Beer-Lambert·Detail LOD CPU 기준 |
| 최적화 기준 | `src/Stage9OptimizationMath.h` | 가변 step·coarse 되감기·cone 구간 CPU 기준 |

## 반드시 지킬 규칙

1. **문서 동기화:** 코드를 바꾸면 관련 `doc/*.md`를 같은 변경에서 갱신합니다.
   상수버퍼는 C++ 구조체 ↔ HLSL cbuffer ↔ `doc/ARCHITECTURE.md` 표
   **세 곳을 동시에** 맞춥니다.
2. **브랜치/커밋 규칙**을 따릅니다 → [doc/CONTRIBUTING.md](doc/CONTRIBUTING.md)
3. **빌드가 깨지지 않게** 유지합니다. 변경 후 위 빌드 명령으로 확인하세요.
4. [포트폴리오 계획](doc/VOLUMETRIC_CLOUD_PORTFOLIO_PLAN.md)의 단계 경계와 사용자 승인
   게이트를 지킵니다. 현재 단계 9에서는 View/Light 기본 최적화만 다루며 저해상도·temporal·
   Cloud Shadow Map/Light Cache는 각각 단계 10~12 전까지 구현하지 않습니다.

## 로컬 단계별 구현 문서 규칙

각 구현 단계를 시작할 때 `notes/<단계>단계-<주요업데이트>.md`를 만들고 구현 전·중·후 계속 갱신합니다.
예: `notes/0단계-기반구성.md`, `notes/1단계-상수밀도-AABB.md`.

- 문서 위에는 볼류메트릭 클라우드 0~15단계 전체 흐름을 간단히 적고 현재 단계를 표시합니다.
- 현재 단계는 처음 구현해 보는 중학생도 이해할 수 있도록 용어 정의, 비유, 필요한 이유와 화면 문제가 무엇인지 깊게 설명합니다.
- 이전 단계에서 재사용하는 기능, 이번에 추가한 기능, 의도적으로 미룬 기능을 구분합니다.
- CPU→GPU→셰이더→화면 데이터 흐름, 좌표계·단위, GPU 리소스, 수식, 파일 책임, 파라미터와 디버깅 방법을 기록합니다.
- 마지막에는 에이전트 자동 검증과 사용자 렌더 검증을 분리한 체크리스트를 둡니다. 화면 항목과 최종 승인은 사용자가 직접 체크합니다.
- 사용자 렌더 체크리스트는 처음 실행하는 사람을 기준으로 씁니다. 버튼·단축키·파라미터마다
  `어디에서 조작하는지`, `무슨 값을 화면에 표시하거나 바꾸는지`, `이 조작으로 어떤 구현을
  검증하는지`, `정상 결과`, `실패 징후`를 적고, 거리·색·모양이 진단값 때문에 어떻게
  보이는지도 설명합니다. 이름만 나열하거나 "정상인지 확인"처럼 판정 기준이 없는 항목은
  체크리스트로 인정하지 않습니다.
- 사용자 승인 후 승인일, 피드백, 수정과 재검증 결과를 기록한 뒤 다음 단계로 넘어갑니다.
- 자세한 형식은 로컬 전용 `notes/README.md`와 `notes/TEMPLATE.md`를 따릅니다.
- `notes/`는 `.gitignore` 대상이며 공식 `doc/*.md`와 `doc/changes/`를 대체하지 않습니다. 코드 변경 시 공식 문서도 계속 같은 변경에서 갱신합니다.

## 더 읽을 문서

- [doc/ARCHITECTURE.md](doc/ARCHITECTURE.md) — 모듈 구조 · 파이프라인 · 상수버퍼
- [doc/RAYMARCHING.md](doc/RAYMARCHING.md) — 레이마칭 수식 (레이 생성 · 교차 · Beer-Lambert)
- [doc/FOLDER_STRUCTURE.md](doc/FOLDER_STRUCTURE.md) — 폴더/파일 역할
- [doc/PERFORMANCE.md](doc/PERFORMANCE.md) — 최적화 전후 성능 측정 조건과 지표 범위
- [doc/ROADMAP.md](doc/ROADMAP.md) — 단계별 계획
- [doc/CONTRIBUTING.md](doc/CONTRIBUTING.md) — 문서/브랜치/커밋 규칙
