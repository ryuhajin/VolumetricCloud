# AGENTS.md — AI 에이전트 안내

이 파일은 Codex 등 AI 에이전트가 이 저장소에서 작업할 때 가장 먼저 읽는 진입점입니다.
**작업을 시작하기 전에 이 문서와 아래 링크된 문서를 먼저 확인하세요.**

## 이 프로젝트가 무엇인가

DirectX11 + HLSL로 **레이마칭을 학습**하고, 최종적으로 **볼류메트릭 클라우드**를 렌더링하는
학습 프로젝트입니다. 단계 0~8, 단계 13 대규모 평면 구름층, 단계 9 기본 최적화,
단계 12 Balanced512 Deep Cache, 단계 14 HDR 대기·지면 통합까지의 사용자 승인 이력은
태그와 변경 기록에 보존합니다.

현재 `feature/stage15-final-quality`에서는 사용자가 이해하고 유지할 수 있는 구조를 목표로
**Full-resolution High 한 경로**만 남기는 최종 간소화를 진행합니다. Stage 10 저해상도 복원,
Stage 11 Temporal, Cirrus/Desert, Detail LOD, NTE Rim, Capture/Reference와 런타임 최적화
프리셋은 최종 런타임에서 폐기했습니다. Stage 9의 검증된 support precheck, empty-space
skipping, 거리 step, early exit와 cone fallback은 변경 불가능한 High 상수로 유지합니다.
기본 상태는 `Urban Fair Weather + High`이며 2026-09-05 사용자 최종 화면 승인을 완료했습니다.
구름 도메인은 PlanarLayer 하나입니다.

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
| 카메라 | `src/Camera.*` | position+yaw/pitch FPS 자유 시점과 프리셋 호환 → view/proj/invViewProj |
| 렌더러 | `src/Renderer.*` | D3D11 초기화, 대기/Shadow compute, HDR 장면·구름·Aerial 합성과 Tone Map |
| 노이즈 도구 | `src/NoiseLab.*` | F1~F4 단일 ImGui 패널, 최종 프리셋/진단 UI와 schema 40 내보내기 |
| High 계약 | `src/HighCloudQuality.h`, `shaders/HighCloudQuality.hlsli` | 100m/512, early exit, empty skip, 거리 step과 8-tap cone 고정값 |
| 구름 설정 | `src/CloudParameters.h` | 80바이트 CloudCB(b1)와 최종 진단 모드 |
| Shadow 설정 | `src/Stage12ShadowParameters.h` | 160바이트 ShadowCB(b8), Balanced512 Deep Cache |
| 대기 통합 설정 | `src/AtmosphereParameters.h`, `src/GroundLightingParameters.h`, `src/ToneMappingParameters.h`, `src/Stage14Parameters.h` | CPU 분리 설정과 224바이트 Stage14CB(b9)/LUT 규격 |
| 도메인 설정 | `src/CloudDomainParameters.h` | 32바이트 PlanarLayer 도메인과 meter 단위 추적 범위 |
| Weather Map | `src/WeatherMap.*`, `shaders/WeatherMapCompute.hlsl` | 256² GPU RGBA(coverage/regional type/density/thickness potential) 생성과 CPU 검증 기준 |
| 형성/Custom 저장 | `src/CloudFormationSettings.*`, `src/CloudFormationPresetStore.*` | 세 타입·세 scene formation과 schema 2 Custom 하나의 strict 원자 저장, schema 1 읽기 이관 |
| 형상 설정 | `src/CloudShapeParameters.h`, `src/WeatherColumnParameters.h` | 48바이트 ShapeCB(b7) profile/upper mass/footprint와 32바이트 WeatherColumnCB(b10) 두께·lift·타입 선택 |
| 최종 scene 프리셋 | `src/Stage15Parameters.h` | Urban/Meadow/Snow scene의 formation·조명·대기 resolver |
| 조명 설정 | `src/LightParameters.h` | 64바이트 LightCB(b3), 태양·albedo·dual-lobe phase |
| 환경광 설정 | `src/EnvironmentParameters.h` | 80바이트 EnvironmentCB와 태양 차폐 기반 환경광 프리셋·sanitize |
| 핫 리로드 | `src/ShaderManifest.h` | 프로그램 manifest, literal include closure, reload report와 후속 무효화 |
| 성능 계측 | `src/FrameProfiler.*` | Weather/Atmosphere/Shadow/Opaque/Cloud/Tone/Frame GPU timestamp와 EMA |
| VS | `shaders/Fullscreen.hlsl` | 풀스크린 삼각형 |
| Scene | `shaders/DiagnosticScene.hlsl` | 깊이 검증용 불투명 평면·박스 |
| Cloud PS | `shaders/VolumetricClouds.hlsl` | Full-resolution High 적분과 scene/atmosphere 직접 합성 |
| Noise | `shaders/Noise.hlsli` | Base/Detail Texture3D, 높이와 erosion 밀도 함수 |
| Lighting | `shaders/CloudLighting.hlsli` | Balanced512 조회와 고정 8-tap cone fallback |
| Phase | `shaders/PhaseFunction.hlsli` | 방향 부호가 고정된 Dual-lobe HG와 안전한 Phase Factor |
| Environment | `shaders/CloudEnvironment.hlsli` | 하늘·지면·AO와 광학 깊이 재사용 다중 산란 |
| Atmosphere | `shaders/Stage14Atmosphere.hlsli`, `shaders/Stage14AtmosphereLut.hlsl` | b9/t8~t13/s3 조회·합성과 여섯 compute LUT |
| Tone Map | `shaders/Stage14ToneMap.hlsl` | Exposure·Bradford WB·ACES·sRGB·dither와 LUT debug |
| 수치 기준 | `src/Stage1VolumeMath.h` | 단계 1 CPU 회귀 검사용 교차·적분 |
| Noise 기준 | `src/Stage2NoiseMath.h` | 단계 2 CPU 회귀 검사용 noise·밀도·바람 좌표 |
| 높이 기준 | `src/Stage3HeightMath.h` | 단계 3 CPU 회귀 검사용 높이·fade·최종 밀도 |
| Detail 기준 | `src/Stage4DetailMath.h` | 단계 4 CPU 회귀 검사용 Detail 좌표·침식·샘플 생략 |
| Weather 기준 | `src/Stage5WeatherMath.h` | 단계 5 CPU 회귀 검사용 UV·coverage·구름 종류 프로파일 |
| 조명 기준 | `src/Stage6LightMath.h` | 단계 6 CPU 회귀 검사용 광학 깊이·단일 산란 |
| Phase 기준 | `src/Stage7PhaseMath.h` | 단계 7 CPU 회귀 검사용 HG·방향·Dual-lobe 수학 |
| 환경광 기준 | `src/Stage8AmbientMath.h` | 단계 8 CPU 회귀 검사용 높이·AO·octave 수학 |
| 대기 기준 | `src/Stage14AtmosphereMath.h` | 단계 14 구면·밀도·phase·LUT UV·시간·HDR CPU 기준 |
| 단위 기준 | `src/Stage13ScaleMath.h` | 단계 13-0 CPU 회귀와 13-2 런타임 프리셋의 meter 상사 변환 수학 |
| 오픈 월드 기준 | `src/Stage13OpenWorldMath.h` | 단계 13-3 실제 km 시작값과 sampling budget 수학 |
| 3D noise 기준 | `src/Stage13NoiseVolumeMath.h` | 단계 13-4 Texture3D 규격·주기 noise CPU 기준 수학 |
| 3D noise cache | `src/NoiseVolumeCache.h` | 테스트 전용 cache header·hash·원자적 저장 검증 |
| Weather 형상 기준 | `src/Stage13WeatherShapeMath.h` | 단계 13-4E 점유율·두께 분포·타입 프로파일·주파수 CPU 기준 수학 |
| 카메라 프리셋 | `src/Stage13CameraPresets.h` | 단일 씬 F5~F8 위치·타깃 기준 |
| 단일 씬 기준 | `src/Stage13SceneMath.h` | 10km 지면·20층 건물·50km·입력·이동·숫자 매핑 기준 |
| High 최적화 기준 | `src/Stage9OptimizationMath.h` | 고정 High 거리 step·coarse 되감기·cone 구간 CPU 기준 |

## 반드시 지킬 규칙

1. **문서 동기화:** 코드를 바꾸면 관련 `doc/*.md`를 같은 변경에서 갱신합니다.
   상수버퍼는 C++ 구조체 ↔ HLSL cbuffer ↔ `doc/ARCHITECTURE.md` 표
   **세 곳을 동시에** 맞춥니다.
2. **브랜치/커밋 규칙**을 따릅니다 → [doc/CONTRIBUTING.md](doc/CONTRIBUTING.md)
   - 단계 구현은 최신 `main`에서 만든 `feature/stage<번호>-<설명>` 브랜치에서만 진행합니다.
   - `main` 직접 commit/push는 금지하며 GitHub PR의 merge commit으로만 병합합니다.
   - 사용자 승인 뒤 main 병합 commit을 annotated `stage<번호>-approved` 태그로 고정하고,
     원격 태그 검증 뒤 완료 브랜치를 삭제합니다. 기존 `stage11` 이름은 유지합니다.
3. **빌드가 깨지지 않게** 유지합니다. 변경 후 위 빌드 명령으로 확인하세요.
4. [포트폴리오 계획](doc/VOLUMETRIC_CLOUD_PORTFOLIO_PLAN.md)의 승인 게이트를 지킵니다.
   현재 Stage 15 High 단일화에서는 자동 수치·빌드·성능만 에이전트가 판정하고, 화면 품질과
   최종 승인은 사용자가 직접 판정합니다.

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

- **첫 투입 권장 순서:** [렌더링 파이프라인](doc/RENDERING_PIPELINE_GUIDE.md) → [상수버퍼 참조](doc/CBUFFER_REFERENCE.md) → [구름·빛 튜닝](doc/CLOUD_LIGHTING_TUNING_GUIDE.md)
- [doc/RENDERING_PIPELINE_GUIDE.md](doc/RENDERING_PIPELINE_GUIDE.md) — 실제 Stage 15 프레임·밀도·조명·대기 흐름
- [doc/CBUFFER_REFERENCE.md](doc/CBUFFER_REFERENCE.md) — b0~b10 CPU/HLSL ABI와 필드 상태
- [doc/CLOUD_LIGHTING_TUNING_GUIDE.md](doc/CLOUD_LIGHTING_TUNING_GUIDE.md) — F1~F4 파라미터 연결과 실전 튜닝
- [doc/ARCHITECTURE.md](doc/ARCHITECTURE.md) — 모듈 구조 · 파이프라인 · 상수버퍼
- [doc/RAYMARCHING.md](doc/RAYMARCHING.md) — 레이마칭 수식 (레이 생성 · 교차 · Beer-Lambert)
- [doc/FOLDER_STRUCTURE.md](doc/FOLDER_STRUCTURE.md) — 폴더/파일 역할
- [doc/PERFORMANCE.md](doc/PERFORMANCE.md) — 최적화 전후 성능 측정 조건과 지표 범위
- [doc/ROADMAP.md](doc/ROADMAP.md) — 단계별 계획
- [doc/CONTRIBUTING.md](doc/CONTRIBUTING.md) — 문서/브랜치/커밋 규칙
