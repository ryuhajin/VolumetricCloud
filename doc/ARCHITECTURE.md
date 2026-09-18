# 아키텍처

## 2026-09-18 독립 프리셋 소유권

일반 시작은 Cumulus+F4 3이다. F1/F2 형상과 F3/F4 조명·환경은 독립 슬롯이며 양쪽 Save Preset으로 선택 슬롯 JSON에 저장한다. LightingPresetSettings/LightingPresetStore가 조명 물리 값과 Tone을 소유하고 CloudFormationPresetStore는 세 타입+Custom을 저장한다. 기존 0~2 scene descriptor는 역사적 진단 전용이다. F4 적용은 formation/Weather/편집 상태를 보존한다. CB ABI와 Deep Cache512·80/79는 유지하고 기존 dirty 의존성으로 LUT/cache를 갱신한다. snapshot44, formation4, lighting1이다. 파일·실패·Custom 일회 초기화 계약은 [슬롯 변경 기록](changes/stage15-cloud-quality-followups.md)을 따른다.



## Stage 15C 결정성 계약

기본 `VCLOUD_STRICT_VALIDATION=OFF`는 Weather만 기존 IEEE strictness를 유지하고 나머지는
성능 우선으로 컴파일한다. ON인 별도 보조 빌드만 모든 활성 셰이더에
`D3DCOMPILE_IEEE_STRICTNESS`를 사용한다. 기본 최적화 level과
High 상수·cbuffer 슬롯/크기는 유지하며, 플래그가 DXBC 캐시 키에 포함되어 기존 blob과 분리된다.
대기 LUT가 CameraCB를 직접 쓰면 Cloud 업로드 생략 캐시를 무효화한다.
`RendererDeterminism.cpp`는 테스트 모드에서만 HDR/Weather/Tone 및 중간 GPU 리소스를 읽는다.
상태 전환은 양쪽에서, bit-exact cold/warm은 Strict에서 검사한다. Release가 주요 렌더/성능
검증 대상이다. 이미지 tolerance 회귀 규약과 미구현 범위는 [기여 규칙](CONTRIBUTING.md)을 참조한다.

이 문서는 High 단일화 이후의 현재 런타임만 설명한다. Stage 10 저해상도 복원, Stage 11 Temporal, Cirrus, Detail LOD, NTE Rim, Capture/Reference는 과거 실험이며 현재 객체·셰이더·상수버퍼 계약에 존재하지 않는다.

처음 코드를 추적한다면 [렌더링 파이프라인 가이드](RENDERING_PIPELINE_GUIDE.md)를 먼저 읽고,
필드 단위 ABI는 [상수버퍼 참조](CBUFFER_REFERENCE.md), 실제 수치 조절은
[구름·빛 튜닝 가이드](CLOUD_LIGHTING_TUNING_GUIDE.md)를 사용한다.

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
| `NoiseLab` | 재개방 가능한 F1~F4 UI, 독립 profiler, Texture3D/Weather preview, schema 42 snapshot |
| `CloudFormationSettings` | formation 소유 필드의 strict 검증, domain-fit 준비와 런타임 변환 |
| `CloudFormationPresetStore` | 여섯 내장 formation과 Custom schema 4 원자 저장, schema 1~3 공통 형상 이관 |
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

Weather Compute는 R/B/A만 생성한다. RGBA의 G는0.5(UNORM128) 예약값이다.
`CloudTypeSelection`은 b10 fixedType=0/.5/1만 전달한다. Regional Blend는 제거했다.
두께는 공통 min/max와 Weather A로 정하며, Vertical profile도 공통5필드로 계산한다.
타입은 footprint/lift 특성 및 프리셋 시작값을 선택한다. Mixed/Meadow는 고정Mixed다.

공통 effective time은 `NoiseLab::UpdateEffectiveTime`이 매 frame 실제 delta time으로
누적한다. F1 `Cloud Motion`의 방향과 속도는 세션 전역 `CloudMotionParameters`다. Cloud PS,
Deep Cache, NoiseLab preview는 모두
`wind direction × movement speed(m/s) × effective time(s)`을 사용하므로 외곽과 내부
무늬가 함께 움직인다. motion은 formation/Custom/Weather 생성 key에 포함되지 않아 타입과
콘셉트를 바꿔도 유지되며 앱 시작 때 기존 방향과 12m/s로 초기화한다.

VSync도 F1의 presentation 상태이며 일반 실행 기본은 On이다. 초기화 시
`DXGI_FEATURE_PRESENT_ALLOW_TEARING`을 조회하고 지원되면 swap chain에
`DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING`을 설정한다. On은 `Present(1, 0)`, Off는 지원
환경에서 `Present(0, DXGI_PRESENT_ALLOW_TEARING)`, 미지원 환경에서 `Present(0, 0)`이다.
창 resize에도 같은 swap-chain flag를 유지한다. `PresentationMath.h`가 Present 선택을
CPU 테스트 가능한 계약으로 고정한다.

| 타입 | 두께 | Base lift | Footprint | Planar domain |
|---|---:|---:|---:|---:|
| Stratus | 1500~2300m | 0m | 0.20 | 1500~4000m |
| Cumulus | 2000~3200m | 300m | 0.50 | 1800~5500m |
| Mixed | Stratus 1500~2500m, Cumulus 3000~4600m | 200m | 0.40 | 1500~6500m |

모든 apply/load는 최대 local thickness + base lift + 200m가 Planar domain 안에 들어가는지 먼저 검사한다. 실패하면 Weather texture와 CPU formation을 바꾸지 않는다.

### Custom formation

`captures/noise-lab/custom-cloud.json` 하나만 파일 기반이다. schema 4는 다음 범위만 저장한다.

- coverage, density, extinction, detail erosion
- Weather channel과 제작 설정
- 물리 shape와 Planar domain
- Base/Detail noise 월드 크기
- `CloudTypeSelection` (세션 전역 motion은 저장하지 않음)

품질, 최적화, 조명, 대기, 지면, Tone, 카메라, debug 상태는 저장하지 않는다. schema 1은
기존 타입 모드를 새 selection으로 이관하고 legacy wind를 무시해 현재 세션 motion을 보존한다.
임시 파일 쓰기가 완료된 뒤 `MoveFileEx(...REPLACE_EXISTING|WRITE_THROUGH)`로 원자 교체한다.

## GPU 자원

| 자원 | 포맷/크기 | 생성자 | 소비자 |
|---|---|---|---|
| Scene Color | 창 크기 `RGBA16_FLOAT` | Opaque PS | Cloud PS |
| Scene Depth | 창 크기 `D32_FLOAT` + SRV | Opaque raster | Cloud PS |
| HDR Cloud | 창 크기 `RGBA16_FLOAT` | Cloud PS | Tone Map PS |
| Weather Map | 256² `RGBA8_UNORM` | 8×8 Weather Map CS → 임시 UAV → `CopyResource` | Cloud/Deep Shadow/NoiseLab |
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
| b0 | `WeatherMapComputeParameters` / `WeatherMapBuildCB` | 160B | Weather CS 전용 4채널 생성 설정, threshold/link와 256² 출력 크기 |
| b0 | `SceneCB` / `cbScene` | 64B | opaque scene view-projection. 다른 stage에서 사용 |
| b1 | `CloudParameters` / `CloudCB` | 80B | bounds Y mirror, density/extinction, debug, coverage, wind, offsets |
| b2 | `NoiseLabParameters` / `NoiseLabCB` | 32B | preview 축, 출력 필드, 시간 |
| b3 | `LightParameters` / `LightCB` | 80B | 태양, albedo, dual-lobe phase와 직접광 shape |
| b4 | `EnvironmentParameters` / `EnvironmentCB` | 48B | sky/ground fill, AO, 다중 산란 |
| b5 | `CloudDomainParameters` / `CloudDomainCB` | 32B | Planar bottom/thickness와 View/Light 유한 거리 |
| b6 | `NoiseVolumeParameters` / `NoiseVolumeCB` | 96B | Texture3D 규격, 월드 크기와 조합 weight |
| b7 | `CloudShapeParameters` / `CloudShapeCB` | 48B | 공통 profile(offset 0~16), 예약20~32, footprint36, densityShaping40, 예약44 |
| b8 | `Stage12ShadowParameters` / `ShadowCB` | 160B | Balanced512 basis, cascade, surface shadow, debug |
| b9 | `stage14::GpuParameters` / `Stage14CB` | 224B | 물리 대기, 태양, Tone, LUT 크기와 debug |
| b10 | `WeatherColumnParameters` / `WeatherColumnCB` | 32B | 공통 min/max(offset0/4), 예약8/12, lift16, fixedType20, 예약24/28 |

Cloud PS 핫 리로드 시 reflection으로 b1, b3~b10의 이름·크기·register를 검사한다. Deep Shadow, Scene, Tone, LUT, Weather shader도 각자 실제로 사용하는 부분 계약을 검사한다.

05-B 후보는 b8 ABI를 유지한 채 cache up 폭을 `W*sunY+층두께*sqrt(1-sunY²)`로
파생한다. `Renderer::UpdateStage12ShadowParameters`는 해당 폭으로 center를 snap하고,
CloudDeepShadow 생성과 Stage12Shadow 조회는 같은 HLSL helper를 공유한다.
512²/80·40 저장 사이의 광선 적분 간격만250m 이하로 나눈다. View High/기본 조명값은 유지한다.
전체 화면 참조 타일 Draw는 테스트 실행기의 `RenderCloudPass(true)`만 사용하며 일반 렌더는 기존1회 Draw다.

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
cascade, surface transmittance와 Atmosphere LUT 진단을 노출한다. Stage 15 방향광 01은
Direct/Sky/Ground/Multiple/Silver Lining, Shaped/Ambient Visibility와
Visible Sun T(opacity weighted, ID 60), 중점 Sun T(숫자 9, ID 24), Phase를 추가 노출한다.
ID 60은 `sum(Tview*(1-Tstep)*Tsun)/sum(Tview*(1-Tstep))`이며 albedo/phase와 독립이다.
분모≤1e-6인 빈 기여는 0이다. CloudCB 80B와 기존 슬롯/기본값은 그대로다. 각 combo 항목은
enum ID를 ImGui ID stack에 넣으며 `Optical Depth`와 `Cloud Depth`는 서로 다른 이름을 쓴다.

셰이더 오류 시 자홍색 화면으로 진행하지 않고 마지막 성공 세대가 계속 렌더링된다. D3D11 debug smoke는 error/corruption뿐 아니라 SRV/RTV/UAV hazard warning도 실패로 처리한다.

테스트 전용 `--directional-lighting-diagnostics`는 정상 프레임이 만든 GPU 물리 입력을
고정한 뒤 Cloud/Tone 패스만 다시 실행해 간접광/phase 독립성을 검사한다.
`tests/DirectionalLightingProbe.hlsl`은 실제 PS b3/b8/b9를 GPU에서 읽어 태양과 cache basis를
검사한다. Shadow CS/Cloud PS의 b8 리소스 identity도 검사하며 일반 런타임에는 probe를 등록하지 않는다.

### 방향광 개선 02: 공통 밀도 곡선

b7 `densityShaping`은 float offset 40, 크기 4B, 범위 0~1이며 F1/Formation이 소유한다.
b7 총 48B, offset 44의 padding1만 예약한다. CPU offsetof와 활성 셰이더 reflection을 검사한다.
`F(q,s)=lerp(q,smoothstep(0,0.4,q),s)`; s=0은 raw q를 그대로 반환한다.
View는 기존 Detail 침식 후, Light는 기존 Base 조립 후 적용한다. 침식 문턱은 raw Base로 유지한다.
매 프레임 b7 업로드와 기존 Shadow 적분 경로를 사용하며 새 texture/step은 없다.
Custom schema 3은 densityShaping을 저장, schema 1/2는 0으로 읽는다.
Snapshot schema 42는 Formation.shape.densityShaping과 lighting의 rimIntensity/rimDepthScale/rimPhaseCap을 기록한다. 폐기된 Near Detail 필드는 없다.

## 방향광 03 — Base 중간 옥타브 실험

02 사용자 승인: Urban 및 Stratus/Cumulus/Mixed 내장 Density shaping=.70. Meadow/Snow=0 유지.
03은 밀도 .70을 고정하고 Base R의 2/3번째 옥타브 진폭만 k=1/1.25/1.50배로 바꾼다.
[.5,.25*k,.125*k,.0625]를 합으로 정규화한다. 첫/넷째 진폭, seed, 주파수, 타일,
Base GBA Worley, Detail, Weather/profile과 High step/skip/cone 상수는 유지한다.

| CPU / HLSL 필드 | 슬롯·크기·offset | 범위·소유권 | 반영 |
|---|---|---|---|
| `NoiseVolumeParameters::baseMidOctaveExtra` / `baseMidOctaveExtra` | b6 총96B, offset28, float4B | extra=0/.25/.5; F2 세션 실험 | 생성 시 k=1+extra |

기존 padding0를 사용하며 offset12의 uint padding은 유지한다. CPU sanitize는 비정상 입력을0으로,
나머지는 후보에 양자화한다. CPU offsetof/HLSL reflection으로 packing을 검사한다.
`F2 → Base mid octaves (03 test)` 선택 시 기존 Base/Detail 재생성 경로로 새 텍스처를 만들고
성공할 때 교체한다. 실패하면 이전 extra와 텍스처를 유지한다. 재생성 직후 첫 프레임은 성능 표본에서 제외한다.
Type/Concept 선택은 세션 후보를 유지한다. Custom에는 저장하지 않고 snapshot42의 noiseVolumes에
baseMidOctaveExtra를 기록한다. 현재 사용자 승인 기본1.50배이며06에서 일반 후보 선택을 읽기 전용 표시로 정리했다. 과거1/1.25 비교는 테스트 실행기에서만 한다.

`--base-octave-test`/CTest BaseOctaves는 72조건(세 후보×Urban/세 Type×세 고도×Detail Off/On)의
HDR finite/진단 범위를 검사한다. 생성 RGBA8 R은 CPU 기준과 1/255+1e-5 이내 비교한다.
GBA와 Detail의 비트 동일성, Weather 해시 유지, 1.00배로 복원 시 원래 Base 해시를 검사한다.
`--high-performance-test --base-octaves-125` 또는 `--base-octaves-150`은 기존 12 case를 직렬 측정한다.
밀도 인자가 없으면 승인된 Concept 기본값(Urban .70, Meadow/Snow 0)을 사용한다.
00 baseline 검증은 실행기에서 강도0을 명시적으로 복원하며 원본을 다시 촬영하지 않는다.

### 04 — 폐기된 Near Detail 그림자

06-A에서 사용자 요청으로 UI·설정·거리 가중치·Detail 조회·전용 테스트를 제거했다. Near/Far/cone는 모두 Base 그림자 밀도를 사용한다. 실험 원인·결과는 [방향광 변경 기록](changes/stage15-directional-cloud-lighting.md)에 보존한다. ShadowCB 160B의 offset152/156은 uint padding이며 0으로 초기화한다. 미사용 surfaceShadowEnabled도 offset8의 uint padding으로 바꿨다. 실제 지면 그림자 strength/floor는 유지한다.

## 05 직접광·간접광·림 비교 — 2026-09-15

F3에 기존 필드 Shadow exponent(b3 offset60), Edge optical depth scale(b3 offset56),
Multiple attenuation(b4 offset16)을 노출했다. 기존 CPU/HLSL sanitize 범위는 각각 .5~4, .25~8,0~1이다.
06 현재 ABI는 LightCB80B/EnvironmentCB48B이며 ShadowCB160B다. 기존 Phase intensity/Sky fill/Ground fill을 함께 비교한다.
Reset approved Urban lighting은 위 여섯 명암 값과 Rim intensity/depth를 Urban 기준으로 복원한다. Type과 태양각·형상·노출·화이트밸런스는 유지한다.
단, 기존 F3 편집 처리와 동일하게 UI preset 표시는 Custom이 된다. Concept 재선택은 전체 장면 조명을 복원한다.
snapshot42 lighting 객체는 shadowExponent/edgeOpticalDepthScale/phaseIntensity/phaseCap/multipleAttenuation을 추가 기록한다.
Custom Formation schema3은 조명을 저장하지 않는다.

일반 Phase 상한은2.5다. `VCLOUD_TEST_PHASE_CAP`은05 검사에서만4/8을 컴파일하는 상수 override이며
UI 옵션이나 승인된 런타임 기본값이 아니다. 기존 shader manifest/include closure는 해당 소스 변경을 감지한다.
01 진단/밀도 불변 회귀를 유지하고05는 고정 물리 입력에서 성분/최종HDR 평균·대비를 보고한다.
`ctest --test-dir build -C Release -R LightingTuning --output-on-failure`로 실행한다.
4 Formation×3고도×3방위각×11독립후보=396조건. 방위각 -108.5/-18.5/71.5도, 시간71초, F5, Base 그림자/Base1.50/밀도.70.
가시 구름은 View 불투명도>.1인 픽셀을4픽셀 간격으로 집계한다. Phase diagnostic의2.5 색을 세어
실제 가시 표본 포화를 발견한 조건에만 Phase.40/상한4,8을 추가 비교한다. HDR finite·D3D오류 검사는 필수다.
수치 대비 상승이 미학적 개선이나 림 품질 승인을 뜻하지는 않는다.

### 05-C 태양 고도 전환 후보 (80/40 유지)

기존 cacheReady/valid는sin(3도)에서켜진다. 자원생성조건/최소고도는유지하고사용비율을별도로계산한다.
`w=smoothstep(sin(3도),sin(5도),sunY)`.
CloudLighting은cache가유효하고w<1일때만기존cone을함께읽어
`tau=lerp(coneTau,cacheTau,w)`, `T=exp(-tau)`로반환한다.
이전의3도즉시분기를연속으로연결하며View밀도/High cone8tap 상수는변경하지않는다.
3도미만cone/5도이상cache 경로는기존과같다. Near/Far공간혼합은기존T혼합이다.
지면은3도미만의중립T=1에서cache T로같은w로fade한다. 구름하부명암과지면그림자는별개다.

Stage12ShadowParameters.hlsli의helper는추가CB필드없이기존b8 sunY/minimumSunY를쓴다.
b8=160B,offset/schema/512²/80·40 유지. snapshot42 deepCache에sunTransitionDegrees[3,5]와
cloudTransitionBlend=optical-depth를추가기록한다. F3의Deep Cache 아래전환구간을읽기전용으로표시한다.
수치/화면확인중인후보이며전환구간의추가GPU비용과잔여탁함을검증해야한다.

## 06 현재 림/상수버퍼 계약 (2026-09-15)

LightCB(b3)는 80B다. 기존 offset0~60은 보존하고 rimIntensity(float, offset64), rimDepthScale(float, offset68), float2 padding(72)을 추가했다. 범위는 각각 [0,4], [.5,2], 비정상 입력 기본1이며 padding은0이다. EnvironmentCB(b4)는 48B로 축소했다.

| EnvironmentCB 필드 | byte offset |
|---|---:|
| ambientOcclusionStrength / ambientHeightInfluence | 0 / 4 |
| multipleScatteringEnabled / multipleScatteringOctaves | 8 / 12 |
| multipleScatteringAttenuation / multipleScatteringExtinctionFactor | 16 / 20 |
| multipleScatteringPhaseFactor / physicalSkyFillScale | 24 / 28 |
| ambientShadowCoupling / ambientShadowExponent | 32 / 36 |
| multipleScatteringInteriorBlend / physicalGroundFillScale | 40 / 44 |

ShadowCB(b8)는160B. offset8/152/156은 uint padding0이며 다른 offset은 유지한다. CPU static_assert와 실제 HLSL reflection을 함께 검사한다. Snapshot42 lighting은 rimIntensity/rimDepthScale/rimPhaseCap을 저장한다. Formation Custom3 및 schema1/2 읽기는 유지하며 조명은 Formation 파일에 넣지 않는다.

Concept은 해당 조명 기본값(현재 Rim1/1)을 적용하고 Type은 기존 조명과 림을 보존한다. 일반 phase는2.5, 원본 HG는16 상한. 림 전용 상한만2.5/4/8 비교 가능하며 현재 기본은2.5다. 아래/앞선03~05 기록은 당시 실험 설명이고 현재 일반 실행의 조작 계약은 본 절과 06 사용자 체크리스트를 우선한다.

### 06 사용자 승인 캐시 규격 — 2026-09-16

현재 일반 Deep Cache는Near512²×80/Far512²×79,R32_FLOAT 배열159MiB다. 이전05의80/40 기록은과거규격이다. C++초기값·sanitize·자원생성·HLSL slice입력·디버그최대index78이일치한다. ShadowCB160B/offset/바인딩은불변이며선형tau보간과3~5도전환을유지한다.159/79는검증전용이다.
06 사용자 승인: Rim intensity 기본2, Rim depth1, cap2.5. Concept 공통 초기값2/Type 전환 보존. LightCB80B offset64/68 유지. 림은 공유HG의 양의 증가분이며 Sun T 기반 외곽 가중치를 사용한다. 화면 윤곽 검출은 아니다.

2026-09-16 하늘광 차폐 분리: EnvironmentCB48B/모든 offset은 유지한다. ambientShadowCoupling/ambientShadowExponent는 이제 구름의 지면 반사광 가시성에만 사용하며 하늘광에는 적용하지 않는다. Sky는 기존 LUT RGB×fill×height×local AO다. Direct/Rim/Multiple·LUT 생성은 그대로다. F4 Ground Ambient Visibility가 남은 지면 가시성 의미를 표시한다. 실제 변경과 수식은 stage15-cloud-rim-lighting 변경 기록 참조.


### 2026-09-17 씬/카메라 현행 계약

건물20×30×20m(10층),정점 생성은 stage13scene 치수 상수 사용. F5=(12,1.7,60)→(0,42,-40). F7은 현재 태양 방향 쪽에서(0,3000,-1200)을 보는20km 조감이며 키 입력 시 갱신한다. 저고도에서는 방향Y최소.25(고도약14.5도),천정Y최대.9999로 안전한 상공 시점을 유지한다. F6/F8과 수직FOV60도/near.1m/far60km는 유지. 과거 좌표/건물치수 기록은 역사 자료다.

2026-09-18 F2 Weather 미리보기: NoiseLab 소유512² RGBA8 RTV/SRV 5장(RGB alpha1+RGBA흑백),F2열림시에만5draw. 기존32B NoiseLabCB outputMode100~104를내부전용으로사용하며ABI변경없음. Weather scale은기존CloudCB weatherMapWorldSize. Formation허용하한3000m,UI3000~64000m.


### 2026-09-18 공통 형상 계약

b7 48B/b10 32B 크기는 유지하지만 필드 의미가 바뀌었으므로 새 exe와 shader를 함께 사용한다.
Custom4는 공통 두께2개/profile5개와 활성 R/B/A 생성기만 저장한다. 구 schema1~3의 고정 타입은 해당 두께/profile로 이관하고 Regional은 고정Mixed 및 두께 범위의 중간값으로 이관한다. 기존 파일을 읽는 것만으로 덮어쓰지 않는다. Snapshot43은 공통 thicknessMeters/verticalProfile을 기록한다.
F2 원본 미리보기는 RBA 합성+R/B/A 흑백4장(512² RGBA8 약4MiB)이며 G 조작/조회는 없다.

## 2026-09-18 프리셋 경로 갱신
현재 활성 JSON은 저장소 presets/의 형상4개·조명4개다. 개발 실행은 원본을 읽고 Save Preset으로 수정한다. 소스 루트가 없는 배포에서는 exe 옆 presets/를 사용한다. CMake 빌드마다8개를 copy_if_different로 배치하며 JSON만 수정해도 복사한다. 일반 시작의 Snow 자동 교체는 제거했다. 이전 captures/noise-lab 저장/초기화 설명은 역사적 동작이다. snapshot 출력과 UI 설정은 captures에 유지한다. schema/CB/승인 기본값은 변경하지 않는다. 상세: [문제와 해결 기록](changes/stage15-cloud-quality-followups.md).

## 2026-09-18 구름 거리 대기 진단 (01)
CloudDebugMode90=Cloud without aerial perspective,91=Air T at cloud depth,92=Air L at cloud depth. 기존82/83 등 폐기 번호는 보존한다. 일반0은 기존 경로이며90도 동일 구름 조명과 Tone을 적용하되 구름 앞 airL/airT만 제외한다. 하늘/지면의 기존 대기는 유지한다.
Stage14CB224B renderFlags offset160의 x(uint)는 기존예약0에서0/91/92로 사용한다. 일반 및90에서는0이며, y/z/w와 다른 offset은 유지한다. CPU GpuParameters와 HLSL을 함께 갱신했다.91/92는 Cloud HDR RGB에 실제 대표 거리의 airT/airL, alpha에 유효 구름 불투명도 여부(1-Tcloud>1e-6)를 쓴다. Tone은 이 두 모드에서 기존 ValidateAndExposeDebug를 사용하고 EV/WB/ACES를 우회한다. 무기여 픽셀은 화면 회색. 구름 대표 깊이는 불투명도 기여 가중 평균이고 첫 표면 깊이가 아니다. 화면 지표와 별개로 rgba16f는 선형 원자료다.
