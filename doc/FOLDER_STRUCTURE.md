# 폴더 / 파일 구조

06-B 경계 진단은 기존 `tests/DirectionalLightingBaseline.cpp`와 `shaders/RimLightingProbe.hlsl`을 재사용한다. `tests/AnalyzeRimBoundary.py`는 NumPy로 선형 RGBA16F의 ROI 통계·CSV 프로파일을 분석한다. 로컬 계획 패키지는 `notes/stage15-cloud-rim-midtone-package/`, 출력은 `captures/stage15-directional-lighting/06-boundary/`이며 둘 다 Git 제외 대상이다.

```text
VolumetricCloud/
├─ CMakeLists.txt
├─ README.md
├─ AGENTS.md
├─ cmake/
│  └─ RunShaderReloadSmoke.cmake
├─ src/
├─ shaders/
├─ tests/
├─ doc/
│  └─ changes/
├─ third_party/imgui/
└─ captures/                    # 실행 중 생성; 대부분 gitignore
```

## 실행 코드 `src/`

| 파일 | 책임 |
|---|---|
| `main.cpp` | 앱 진입점과 GPU smoke command line |
| `Window.*` | Win32 창, DPI, 키보드·마우스 입력, resize 전달 |
| `Camera.*` | FPS 카메라와 view/projection 역행렬 |
| `Renderer.*` | D3D11 자원, 단일 High 프레임, 프리셋 적용, 핫 리로드 |
| `NoiseLab.*` | F1~F4 ImGui, noise/weather preview, schema 40 export |
| `DeveloperUiSettings.*` | `developer-ui.json`의 Zoom schema 1 원자 저장 |
| `FrameProfiler.*` | Weather/Atmosphere/Shadow/Opaque/Cloud/Tone/Frame timestamp |
| `PresentationMath.h` | VSync와 tearing 지원 상태에 따른 Present 인수 계약 |
| `WeatherMap.*` | 256² periodic Weather RGBA CPU 기준·GPU build 계약과 hash |

## 현재 설정과 계약 `src/`

| 파일 | 책임 |
|---|---|
| `HighCloudQuality.h` | View/skip/distance/cone High 고정 상수 |
| `CloudParameters.h` | 80B CloudCB(b1)와 구름 진단 enum |
| `LightParameters.h` | 80B LightCB(b3) |
| `EnvironmentParameters.h` | 48B EnvironmentCB(b4) |
| `CloudDomainParameters.h` | 32B Planar-only DomainCB(b5) |
| `Stage13NoiseVolumeMath.h` | 96B NoiseVolumeCB(b6)와 Texture3D CPU 기준 |
| `CloudShapeParameters.h` | 48B vertical profile ShapeCB(b7) |
| `WeatherColumnParameters.h` | 32B 두께·lift·타입 선택 WeatherColumnCB(b10) |
| `CloudMotionParameters.h` | 세션 전역 XZ 방향과 m/s 속도 |
| `CloudTypeSelection.h` | Fixed/Regional 유효 타입 resolver |
| `Fnv1a64.h` | 표준 FNV-1a 64-bit 공통 구현 |
| `CloudShapeDomainContract.h` | local top + lift + headroom domain fit |
| `Stage12ShadowParameters.h` | 160B Balanced512 ShadowCB(b8) |
| `AtmosphereParameters.h` | 물리 대기 CPU 설정 |
| `GroundLightingParameters.h` | Concrete/Grass/Snow 지면 설정 |
| `ToneMappingParameters.h` | Tone curve, exposure, white balance |
| `Stage14Parameters.h` | 224B Stage14CB(b9)와 LUT 크기 |
| `Stage15Parameters.h` | 세 scene concept의 CPU descriptor |

## Formation과 저장 `src/`

| 파일 | 책임 |
|---|---|
| `CloudFormationSettings.*` | formation snapshot, strict range/domain-fit 검증, runtime 변환 |
| `CloudFormationPresetStore.*` | 세 type·세 concept 내장 resolver와 Custom schema 3, schema 1/2 읽기 이관 |

Custom 파일은 `captures/noise-lab/custom-cloud.json` 하나다. 구형 `cloud-presets` 디렉터리는 코드가 읽거나 삭제하지 않는다.

## 핫 리로드 `src/`, `cmake/`

| 파일 | 책임 |
|---|---|
| `ShaderManifest.h` | 프로그램 목록, literal include parser, dependency closure와 report |
| `Renderer.cpp` | content cache, 임시 D3D 객체 생성, reflection, atomic commit/rollback |
| `RunShaderReloadSmoke.cmake` | build 하위 임시 shader 복사본과 표식 생성 |

## HLSL `shaders/`

| 파일 | 책임 |
|---|---|
| `Fullscreen.hlsl` | vertex buffer 없는 fullscreen triangle VS |
| `DiagnosticScene.hlsl` | HDR 지면·건물과 물리 대기 PS |
| `VolumetricClouds.hlsl` | 유일한 Full-resolution High Cloud PS |
| `Noise.hlsli` | Weather + Base/Detail Texture3D density |
| `Weather.hlsli` | Weather 표본과 PhysicalColumnGeometry/profile |
| `CloudAdvection.hlsli` | 공통 wind 좌표 |
| `HighCloudQuality.hlsli` | High 고정 GPU 수치 |
| `CloudLighting.hlsli` | Deep Cache 조회와 deterministic cone fallback |
| `CloudEnvironment.hlsli` | 물리 sky/ground fill, AO, 다중 산란 |
| `PhaseFunction.hlsli` | dual-lobe Henyey–Greenstein |
| `CloudDeepShadow.hlsl` | Balanced512 Near/Far optical-depth compute |
| `Stage12Shadow.hlsli` | cache lookup, cascade blend와 surface shadow |
| `Stage14Atmosphere.hlsli` | 물리 대기 LUT 조회와 aerial 합성 |
| `Stage14AtmosphereLut.hlsl` | 여섯 LUT compute entry |
| `Stage14ToneMap.hlsl` | HDR → display 출력 |
| `NoiseVolume.hlsl` | Base/Detail Texture3D 생성 CS |
| `WeatherMapCompute.hlsl` | 8×8 thread group Weather RGBA8 생성 CS |
| `NoiseLab.hlsl` | F1/F2 slice preview PS |
| `*Parameters.hlsli` | 해당 C++ 구조체와 일치하는 cbuffer |

별도 Cloud Upsample, Temporal Resolve, Composite, Rim, Capture shader는 없다.

## CPU 회귀 `tests/`

결정성 GPU 계측은 `src/RendererDeterminism.cpp`, 직렬 cold/warm 실행과 비교기 자체 검사는
`tests/RunDeterminism.ps1`, 원본 픽셀 차이 분석은 `tests/CompareDeterminismDump.ps1`가 담당한다.
조사·수정 근거는 `doc/changes/stage15c-determinism.md`에 기록한다.

`Stage1`~`Stage8`, `Stage13`, `Stage14` math 테스트는 학습 단계의 수치 근거다. 현재 런타임 단일화는 다음 테스트가 직접 보호한다.

- `HighCloudQualityTests.cpp`
- `ShaderManifestTests.cpp`
- `CloudFormationPresetStoreTests.cpp`
- `Stage15PresetMathTests.cpp`
- `FrameProfilerMathTests.cpp`

Stage10/11, Cirrus, LOD, Reference 전용 테스트는 삭제했다.

## 문서 `doc/`

| 파일 | 내용 |
|---|---|
| `RENDERING_PIPELINE_GUIDE.md` | 초기화·프레임·밀도·레이마칭·조명·대기 코드 흐름 |
| `CBUFFER_REFERENCE.md` | b0~b9 CPU/HLSL 필드와 재패킹·파생·비활성 상태 |
| `CLOUD_LIGHTING_TUNING_GUIDE.md` | F1~F4 파라미터, 기본값, 저장 범위와 증상별 튜닝 |
| `ARCHITECTURE.md` | 현재 파이프라인, 자원, CB, 핫 리로드 |
| `RAYMARCHING.md` | 현재 Planar High 수식 |
| `PERFORMANCE.md` | 1080p gate와 profiler |
| `STAGE15_PRESET_VALIDATION_GUIDE.md` | 사용자 최종 화면 검증 |
| `ROADMAP.md` | 승인 이력, 폐기 기능과 현재 gate |
| `CONTRIBUTING.md` | 브랜치·커밋·문서 규칙 |
| `changes/` | 구현 당시의 짧은 변경 이력 |

`notes/`는 로컬 작업 일지이며 gitignore 대상이다. 공식 구조는 항상 `doc/`가 기준이다.

첫 투입자는 `RENDERING_PIPELINE_GUIDE.md` → `CBUFFER_REFERENCE.md` →
`CLOUD_LIGHTING_TUNING_GUIDE.md` 순서로 읽은 뒤, 수식이 필요할 때
`RAYMARCHING.md`를 참조한다.

Stage 15 방향광 기준 보존은 `tests/DirectionalLightingBaseline.cpp`와
`tests/RunDirectionalLightingBaseline.ps1`이 담당한다. 명시적 CLI에서만 촬영하며
일반 렌더에서는 UI/Present 직전의 null capture pointer 검사만 수행한다.
자료는 Git 제외 경로 `captures/stage15-directional-lighting/00-baseline/`에 둔다.
`originals`는 첨부 원본, `clean`은 PNG/HDR/설정, `source-state`는 시작 소스,
`validation`은 로그/해시, `baseline-tool`은 재현 실행 도구다.
00~05 공식 상태는 `doc/changes/stage15-directional-cloud-lighting.md`, 06 림 착수 이후는 `doc/changes/stage15-cloud-rim-lighting.md`에 기록한다.

`tests/AnalyzeCloudOpticalDepth.py`는 같은 ROI에서 Shadow Base/Detail 표현과 소멸계수 배수를 분리해 분석한다. 기존 `RunRimBoundaryDiagnostics`에서 생성한 선형 HDR를 사용한다.

01 진단은 같은 C++ 실행기의 `RunDirectionalLightingDiagnostics`와 테스트 전용
`tests/DirectionalLightingProbe.hlsl`을 사용한다. probe는 활성 HLSL 헤더를 include하며
Cloud/Shadow/Atmosphere GPU 방향을 읽는다. 이미지 없이 `01-diagnostics/`에 수치 로그를 남긴다.

### 02 검증 파일

`tests/DensityShapingProbe.hlsl`은 실제 PS b7로 공통 HLSL 곡선을 검사하는 테스트 전용 CS다.
`tests/DirectionalLightingBaseline.cpp`의 02 실행기는 후보별 HDR/태양 T를 메모리에서 검사한다.
`tests/Stage15PresetMathTests.cpp`는 밀도 단조성·Base/Detail 상한·0 복원,
`tests/CloudFormationPresetStoreTests.cpp`는 Custom schema 3 저장과 1/2 이관을 검증한다.

03 Base 중간 옥타브 실험은 `Stage13NoiseVolumeMath.h`/`NoiseVolume.hlsl` 생성식,
b6 offset28 세션 후보, F2 선택과 기존 재생성 요청을 사용한다.
`tests/DirectionalLightingBaseline.cpp::RunBaseOctaveDiagnostics`는 CPU/GPU 생성 R, GBA/Detail 보존,
72조건 HDR와 진단을 검사한다. 별도 텍스처 규격/형상 알고리즘은 추가하지 않는다.
`tests/DirectionalLightingBaseline.cpp`의 04 NearDetailShadows는 실제 Near/Far 캐시 readback 및 설정/복원 회귀를 포함한다.
`tests/DirectionalLightingBaseline.cpp`의 LightingTuning은05 독립 조명 후보와 조건부 Phase 상한 shader variant를 검사한다.
`doc/research/stage15-cloud-lighting-research.md` — 05 사용자 결과, 공개 실무 조명 자료와 height/directional mask 적용 범위 조사.
`shaders/SolarOcclusionProbe.hlsl`: 05 실제 캐시와 25m/12.5m 직접 적분을 비교하는 테스트 전용 월드 단면 검사.
`tests/DirectionalLightingBaseline.cpp::RunSolarBandingDiagnostics` — 전체 화면 태양 참조, Near/Far 분리, 투영/적분/높이·XY 축 비교와 finite 회귀. 원본은 로컬05-banding 폴더에 선택적으로 기록한다.

06 수치 기준은 src/CloudRimMath.h와 shaders/RimLightingProbe.hlsl, GPU/화면 검증 실행기는 tests/DirectionalLightingBaseline.cpp다. 옛 분석적 환경광 수학은 src에서 제거하고 tests/LegacyEnvironmentParameters.h 및 tests/LegacyStage8AmbientMath.h로 옮겼다. 현재 EnvironmentCB48B와 혼용하지 않는다.

- tests/AnalyzeSkyColor.py: LUT 하늘광 전후 HDR 색과 Direct/Rim/Ground/Multiple/투과율 불변성 분석.

- tests/AnalyzeSkyBalance.py: 두 하늘광 차폐 방식의 Sky fill/Multiple attenuation 독립성·HDR 비율·빌드 간 비교.

- tests/AnalyzeBaseDirect.py: 기본 직접광 합산 비중 실험의 HDR 수식/나머지 성분 불변성 검사.

- tests/AnalyzePowder.py: 기본 직접광 Powder 후보의 HDR 단조성·강도 비례와 Rim/환경광/투과율 불변성, Debug/Release 오차 검사.

- tests/AnalyzeDetailReview.py: Detail 침식 강도와 그림자 밀도 표현 비교의 ROI HDR 수치·View 불변성 및 갤러리 생성.

- src/FormationParameterRanges.h: F1/F2와 Formation·Weather·Column·Motion 검증이 공유하는 허용 범위 상수.

### 독립 슬롯 프리셋 추가
- `src/LightingPresetStore.h/.cpp`: F4 조명·환경 descriptor, 내장 1~4, strict JSON/원자 저장.
- `src/PresetJsonSyntax.h`: formation 파일의 전체 JSON 문법 검증.
- `tests/LightingPresetStoreTests.cpp`: 8슬롯 파일 격리·round-trip·실패·Custom 초기 교체.
- `doc/changes/stage15-cloud-quality-followups.md`: 이번 튜닝과 저장 계약·검증 기록.

## 2026-09-18 프리셋 경로 갱신
현재 활성 JSON은 저장소 presets/의 형상4개·조명4개다. 개발 실행은 원본을 읽고 Save Preset으로 수정한다. 소스 루트가 없는 배포에서는 exe 옆 presets/를 사용한다. CMake 빌드마다8개를 copy_if_different로 배치하며 JSON만 수정해도 복사한다. 일반 시작의 Snow 자동 교체는 제거했다. 이전 captures/noise-lab 저장/초기화 설명은 역사적 동작이다. snapshot 출력과 UI 설정은 captures에 유지한다. schema/CB/승인 기본값은 변경하지 않는다. 상세: [문제와 해결 기록](changes/stage15-cloud-quality-followups.md).

- src/PresetPaths.h: 개발 원본 또는 실행 파일 옆 프리셋 루트 선택.
- tests/PresetDeploymentTests.cpp: 원본/배포8슬롯 로드와 작업 디렉터리 독립 경로 회귀.
- presets/: Git에 보존하는 형상4개·조명4개 JSON 원본.

- tests/CloudDetailComparisonPage.h: Detail32³/64³ 진단의 로컬 비교 HTML. 네트워크 없이 정지/차이/원본 확대/동일 이동 경로를 표시한다.
