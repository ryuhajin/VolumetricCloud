# 아키텍처

이 문서는 High 단일화 이후의 현재 런타임만 설명한다. Stage 10 저해상도 복원, Stage 11 Temporal, Cirrus, Detail LOD, NTE Rim, Capture/Reference는 과거 실험이며 현재 객체·셰이더·상수버퍼 계약에 존재하지 않는다.

## 프레임 흐름

```text
CPU formation/scene 상태
   │
   ├─ Atmosphere LUT compute ──────────────┐
   ├─ Balanced512 Deep Shadow compute ──┐  │
   └─ Opaque Scene → HDR Scene + D32F ─┐│  │
                                       ▼▼  ▼
                              Full-resolution Cloud PS
                         (raymarch + scene/atmosphere 합성)
                                       │ RGBA16F HDR Cloud
                                       ▼
                                  Tone Map PS
                                       │
                                       ▼
                                  R8G8B8A8 Back Buffer
```

Cloud PS는 구름 scattering과 transmittance를 적분한 뒤 같은 픽셀에서 HDR scene, sky, aerial perspective를 합성한다. 따라서 별도 Resolve, Temporal, Composite 패스가 없다.

## 모듈 책임

| 모듈 | 책임 |
|---|---|
| `main.cpp` | 일반 실행과 formation/대기/핫 리로드/성능 GPU smoke 진입점 |
| `Window`, `Camera` | Win32 입력, resize, FPS 카메라와 F5~F8 고정 시점 |
| `Renderer` | D3D11 자원, 프레임 순서, formation 원자 적용, 셰이더 세대 교체 |
| `NoiseLab` | 재개방 가능한 F1~F4 UI, 독립 profiler, Texture3D/Weather preview, schema 39 snapshot |
| `CloudFormationSettings` | formation 소유 필드의 strict 검증, domain-fit 준비와 런타임 변환 |
| `CloudFormationPresetStore` | 여섯 내장 formation과 Custom schema 1 원자 저장 |
| `ShaderManifest` | 프로그램별 source/entry/target/defines/object/dependency/invalidation |
| `FrameProfiler` | 여섯 최종 GPU 구간 timestamp와 CPU frame EMA |
| `WeatherMap` | 256² RGBA8 periodic Weather 생성과 hash |
| `HighCloudQuality` | CPU/HLSL가 공유하는 변경 불가능한 High 수치 |

## 상태 소유권

### Scene concept

`ApplySceneConcept`는 다음을 한 장면으로 적용한다.

- formation
- 태양과 phase
- 환경광
- 물리 대기와 시간
- 지면 재질과 반사광
- 표면 Deep Cache 그림자 강도

최종 콘셉트는 `Urban Fair Weather`, `Meadow Broken Clouds`, `Snow Overcast` 세 개다. 기본값은 Urban이다.

### Cloud type

`ApplyCloudType`은 formation만 바꾼다. 태양·대기·지면·Tone·카메라는 건드리지 않는다.

F2의 Weather G 생성기는 `CloudTypeMode::WeatherMap`에서만 실제 G를 소유한다.
Stratus/Mixed/Cumulus 고정 소스는 생성된 G를 각각 0/0.5/1로 덮어쓰므로 UI가 해당
채널 편집을 비활성화한다. 이 구분 덕분에 움직였지만 렌더링에는 반영되지 않는
슬라이더 상태가 없다. 공통 wind는 Weather/Base/Detail의 sample position에서 같은
`direction × speed × time`을 빼며 별도 Weather 속도는 존재하지 않는다.

공통 time은 `NoiseLab::UpdateEffectiveTime`이 매 frame 누적하며 F1의 `Animate clouds`가
누적 여부를 제어한다. F1 `Time` slider는 같은 값을 직접 scrub하고 Cloud PS, Deep Cache,
NoiseLab preview가 모두 이 effective time을 사용한다. 이 런타임 값은 formation/Custom에
저장되지 않는다. VSync도 F1의 presentation 상태이며 일반 실행 기본은 On이다.
Renderer는 On에서 `Present(1, 0)`, Off에서 `Present(0, 0)`을 호출한다.

| 타입 | 두께 | Base lift | Footprint | Planar domain |
|---|---:|---:|---:|---:|
| Stratus | 1500~2300m | 0m | 0.20 | 1500~4000m |
| Cumulus | 2000~3200m | 300m | 0.50 | 1800~5500m |
| Mixed | Stratus 1500~2500m, Cumulus 3000~4600m | 200m | 0.40 | 1500~6500m |

모든 apply/load는 최대 local thickness + base lift + 200m가 Planar domain 안에 들어가는지 먼저 검사한다. 실패하면 Weather texture와 CPU formation을 바꾸지 않는다.

### Custom formation

`captures/noise-lab/custom-cloud.json` 하나만 파일 기반이다. schema 1은 다음 범위만 저장한다.

- coverage, density, extinction, detail erosion
- Weather channel과 제작 설정
- 물리 shape와 Planar domain
- Base/Detail noise 월드 크기
- wind 방향과 속도

품질, 최적화, 조명, 대기, 지면, Tone, 카메라, debug 상태는 저장하지 않는다. 임시 파일 쓰기가 완료된 뒤 `MoveFileEx(...REPLACE_EXISTING|WRITE_THROUGH)`로 원자 교체한다. schema 29/30과 구형 `cloud-presets`는 탐색하거나 변환하지 않는다.

## GPU 자원

| 자원 | 포맷/크기 | 생성자 | 소비자 |
|---|---|---|---|
| Scene Color | 창 크기 `RGBA16_FLOAT` | Opaque PS | Cloud PS |
| Scene Depth | 창 크기 `D32_FLOAT` + SRV | Opaque raster | Cloud PS |
| HDR Cloud | 창 크기 `RGBA16_FLOAT` | Cloud PS | Tone Map PS |
| Weather Map | 256² `RGBA8_UNORM` | CPU | Cloud/Deep Shadow/NoiseLab |
| Base Noise | 128³ `RGBA8_UNORM` | Noise Volume CS | Cloud/Deep Shadow/NoiseLab |
| Detail Noise | 32³ `RGBA8_UNORM` | Noise Volume CS | Cloud/NoiseLab |
| Near Deep Cache | 512²×80 `R32_FLOAT` | Deep Shadow CS | Cloud/Scene |
| Far Deep Cache | 512²×40 `R32_FLOAT` | Deep Shadow CS | Cloud/Scene |
| Atmosphere LUT 2D | 256×64, 32×32, 192×108, 64×16 `RGBA16_FLOAT` | Atmosphere CS | Scene/Cloud/Tone |
| Aerial LUT 3D | 32³ radiance + transmittance `RGBA16_FLOAT` | Atmosphere CS | Scene/Cloud/Tone |

존재하지 않는 자원: 저해상도 cloud-data MRT, resolve pair, Temporal history ping-pong, capture accumulator, Rim target.

## 상수버퍼 계약

C++ 구조체, HLSL cbuffer, 이 표는 함께 변경한다.

| register | C++ / HLSL | 크기 | 주요 내용 |
|---:|---|---:|---|
| b0 | `CameraCB` / `cbCamera` | 224B | 역행렬, 카메라, 시간, Full render size, clip |
| b0 | `SceneCB` / `cbScene` | 64B | opaque scene view-projection. 다른 stage에서 사용 |
| b1 | `CloudParameters` / `CloudCB` | 80B | bounds Y mirror, density/extinction, debug, coverage, wind, offsets |
| b2 | `NoiseLabParameters` / `NoiseLabCB` | 32B | preview 축, 출력 필드, 시간 |
| b3 | `LightParameters` / `LightCB` | 64B | 태양, albedo, dual-lobe phase와 직접광 shape |
| b4 | `EnvironmentParameters` / `EnvironmentCB` | 80B | sky/ground fill, AO, 다중 산란 |
| b5 | `CloudDomainParameters` / `CloudDomainCB` | 32B | Planar bottom/thickness와 View/Light 유한 거리 |
| b6 | `NoiseVolumeParameters` / `NoiseVolumeCB` | 96B | Texture3D 규격, 월드 크기와 조합 weight |
| b7 | `CloudShapeParameters` / `CloudShapeCB` | 64B | Stratus/Cumulus 두께와 profile, lift, footprint |
| b8 | `Stage12ShadowParameters` / `ShadowCB` | 160B | Balanced512 basis, cascade, surface shadow, debug |
| b9 | `stage14::GpuParameters` / `Stage14CB` | 224B | 물리 대기, 태양, Tone, LUT 크기와 debug |

Cloud PS 핫 리로드 시 reflection으로 b1, b3~b9의 이름·크기·register를 검사한다. Deep Shadow, Scene, Tone, LUT shader도 각자 실제로 사용하는 부분 계약을 검사한다.

## High 알고리즘

High 수치는 런타임 구조체가 아니라 `HighCloudQuality` 상수다.

1. Planar layer와 scene depth를 교차해 유효한 `[tStart, tEnd]`를 만든다.
2. Weather와 local column으로 확실히 빈 위치를 Texture3D fetch 전에 거부한다.
3. 기본 100m step으로 Base+Detail density를 읽는다.
4. Base가 epsilon `0.0001` 이하인 표본이 3개 연속이면 최대 200m의 2× coarse 탐색으로 전환한다.
5. coarse에서 밀도를 만나면 직전 구간으로 되돌아가 정상 적분한다.
6. 24~50km에서 step을 최대 1.25×로 늘린다.
7. 최대 512회 또는 transmittance `0.01` 이하에서 종료한다.
8. Deep Cache가 유효하지 않은 표본만 8-tap, 2° deterministic cone fallback을 사용한다.

Detail Texture3D는 모든 추적 거리에서 같은 방식으로 사용한다. 거리별 Detail fade나 LOD 상수버퍼는 없다.

## 핫 리로드

`ShaderManifest`의 각 프로그램은 다음을 가진다.

```text
source + entry + target + defines + dependencies + object + invalidation
```

초기화 때 literal `#include`를 재귀적으로 읽어 closure를 만든다. 실행 중에는 shader 폴더를 250ms마다 확인하고 변경 파일이 closure에 포함된 프로그램만 고른다.

재로드 transaction:

1. 영향받은 blob만 compile/cache load한다.
2. reflection 계약을 검사한다.
3. 모든 D3D shader 객체와 필요한 input layout을 임시 생성한다.
4. Noise Volume shader가 바뀌고 절차 생성이 활성 상태면 임시 Texture3D까지 만든다.
5. 하나라도 실패하면 임시 세대를 폐기한다. 기존 객체, dependency closure, shader hash, generation은 유지한다.
6. 전부 성공하면 변경된 객체만 교체하고 generation을 한 번 증가시킨다.
7. LUT, Deep Cache, Noise Volume은 프로그램별 invalidation만 수행한다.

F4 reload report는 성공 여부, 변경 파일, 영향 프로그램 수, compile 수, cache hit 수와 시간을 표시한다. 테스트용 forced scan은 250ms 제한을 건너뛰지만 production transaction과 같은 함수를 쓴다.

## 프로파일과 진단

GPU timestamp는 `Atmosphere / Shadow / Opaque / Cloud / Tone / Frame`만 기록한다.
좌측 상단의 독립 `Performance` 창은 FPS, cloud time, CPU/GPU frame과 다섯 GPU 구간을
항상 표시한다. F4에는 density, transmittance, optical/cloud depth, Near/Far cache,
cascade, surface transmittance와 Atmosphere LUT 진단만 노출한다. 각 combo 항목은
enum ID를 ImGui ID stack에 넣으며 `Optical Depth`와 `Cloud Depth`는 서로 다른 이름을 쓴다.

셰이더 오류 시 자홍색 화면으로 진행하지 않고 마지막 성공 세대가 계속 렌더링된다. D3D11 debug smoke는 error/corruption뿐 아니라 SRV/RTV/UAV hazard warning도 실패로 처리한다.
