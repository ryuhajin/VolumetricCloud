# 폴더 / 파일 구조

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
| `NoiseLab.*` | F1~F4 ImGui, noise/weather preview, schema 39 export |
| `DeveloperUiSettings.*` | `developer-ui.json`의 Zoom schema 1 원자 저장 |
| `FrameProfiler.*` | Atmosphere/Shadow/Opaque/Cloud/Tone/Frame timestamp |
| `PresentationMath.h` | VSync와 tearing 지원 상태에 따른 Present 인수 계약 |
| `WeatherMap.*` | 256² periodic Weather RGBA 생성과 hash |

## 현재 설정과 계약 `src/`

| 파일 | 책임 |
|---|---|
| `HighCloudQuality.h` | View/skip/distance/cone High 고정 상수 |
| `CloudParameters.h` | 80B CloudCB(b1)와 구름 진단 enum |
| `LightParameters.h` | 64B LightCB(b3) |
| `EnvironmentParameters.h` | 80B EnvironmentCB(b4) |
| `CloudDomainParameters.h` | 32B Planar-only DomainCB(b5) |
| `Stage13NoiseVolumeMath.h` | 96B NoiseVolumeCB(b6)와 Texture3D CPU 기준 |
| `CloudShapeParameters.h` | 64B non-Cirrus ShapeCB(b7) |
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
| `CloudFormationPresetStore.*` | 세 type·세 concept 내장 resolver와 Custom schema 1 |

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
| `NoiseLab.hlsl` | F1/F2 slice preview PS |
| `*Parameters.hlsli` | 해당 C++ 구조체와 일치하는 cbuffer |

별도 Cloud Upsample, Temporal Resolve, Composite, Rim, Capture shader는 없다.

## CPU 회귀 `tests/`

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
| `ARCHITECTURE.md` | 현재 파이프라인, 자원, CB, 핫 리로드 |
| `RAYMARCHING.md` | 현재 Planar High 수식 |
| `PERFORMANCE.md` | 1080p gate와 profiler |
| `STAGE15_PRESET_VALIDATION_GUIDE.md` | 사용자 최종 화면 검증 |
| `ROADMAP.md` | 승인 이력, 폐기 기능과 현재 gate |
| `CONTRIBUTING.md` | 브랜치·커밋·문서 규칙 |
| `changes/` | 구현 당시의 짧은 변경 이력 |

`notes/`는 로컬 작업 일지이며 gitignore 대상이다. 공식 구조는 항상 `doc/`가 기준이다.
