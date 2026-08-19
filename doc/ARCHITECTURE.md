# 아키텍처

단계 13-4B는 2026-08-14, 단계 13-4C는 2026-08-16 사용자 승인을 받았다. 13-4C의
Local Inspector 이력은 보존하지만 런타임 구조는 13-4D 단일 포트폴리오 씬으로 대체했다.
13-4D 사용자 검증에서 발견한 낮은 Weather 점유율, 이중 profile threshold와 약한 광학
대비를 13-4E Dense Broken-Sky로 교체했다. 13-5는 기존 Light Ray 예산을 유지한 채
태양 노출 표면에 Phase를 제한하고 환경광을 자기 그림자에 결합했으며 2026-08-17 승인됐다.
단계 9는 이 평면층 승인 기준을 별도 Reference PS로 보존한 채 View 공백 탐색·조기 종료와
deterministic Light cone 후보를 Optimized PS에 추가했다. 2026-08-19 Balanced를 일반 시작
기본값으로 승인했으며 Reference PS는 이후 단계의 회귀 비교용으로 유지한다.

## 모듈과 책임

| 모듈 | 책임 |
|---|---|
| `Window` / `Camera` | Win32 입력, 숫자 0~9·F1~F8, 오빗/휠, WASD·Shift rig 이동, FOV·현재/저장 시점과 view/projection 제공 |
| `Renderer` | D3D11 장치, 10km 지면·20층 건물, compute noise, 구름·Noise Lab 패스와 테스트 전용 legacy fixture |
| `NoiseLab` | F1 외형/Noise/Optimization, F2 Weather, F3 Lighting, F4 Camera 독립 ImGui 창, 세 축 512² 단면, schema 31 snapshot 내보내기 |
| `CloudAppearance` | Dense Mixed/Stratus/Cumulus 외형 계약, F1 요청값 0~3 공통 디코딩, Physical density CPU 기준, schema 29 Custom 원자 저장·엄격 로드 |
| `CloudParameters` | 128바이트 AABB·Base·Detail·Weather·step 설정과 디버그 모드 |
| `CloudLodParameters` | 16바이트 Detail 거리 LOD 시작·끝과 실제 volume 중립 평균 |
| `OptimizationParameters` | 64바이트 b9 View/Light 후보와 Balanced~Fine Reference 활성 preset; Fast/4× 값은 실패 이력·schema 호환용 보존 |
| `CloudShapeParameters` | 64바이트 Legacy/Weather Physical 모드, 타입별 두께와 세로 프로파일 |
| `CloudDomainParameters` | AABB/평면층 선택, 구름 고도·두께와 View/Light 추적 제한 |
| `LightParameters` | 80바이트 태양·Light Ray·외곽 범위 Dual-lobe Phase 설정 |
| `EnvironmentParameters` | 80바이트 하늘·지면·태양 차폐 AO·다중 산란 설정 |
| `FrameProfiler` | CPU Frame과 8-slot 비동기 D3D11 timestamp query, EMA 성능 통계 |
| `WeatherMap` | 256² RGBA8 Uniform 회귀/Periodic Perlin/Channel Debug 픽셀 생성과 해시, 공통 `CloudTypeMode` |
| `DiagnosticScene.hlsl` | 평면·박스의 불투명 색상과 장치 깊이 출력 |
| `Ray.hlsli` | 평행축 0 나누기를 피하는 slab Ray-AABB 교차 |
| `CloudParameters.hlsli` / `Noise.hlsli` / `Weather.hlsli` | 공유 128바이트 설정과 Base/Detail/Weather 밀도 함수 |
| `LightParameters.hlsli` / `CloudLighting.hlsli` | 공유 80바이트 조명 설정과 Base-only Light Ray·외곽 응답 |
| `PhaseFunction.hlsli` | 방향 부호를 고정한 전방·후방 HG, raw 진단과 LDR 적용 배율 분리 |
| `CloudEnvironment.hlsli` | 높이 환경광, 밀도 AO와 광학 깊이 재사용 octave |
| `VolumetricClouds.hlsl` / `NoiseLab.hlsl` | Beer-Lambert 구름 합성 / XY·XZ·YZ 단면 출력 |
| `Stage1VolumeMath.h` | GPU와 독립적으로 같은 경계 조건과 투과율을 검사하는 CPU 기준 구현 |
| `Stage2NoiseMath.h` | value noise, coverage와 바람 좌표의 CPU 기준 구현 |
| `Stage3HeightMath.h` | 정규화 높이, 상·하단 smoothstep과 밀도 결합의 CPU 기준 구현 |
| `Stage4DetailMath.h` | Detail 좌표·바람, subtractive erosion과 샘플 생략 CPU 기준 구현 |
| `Stage5WeatherMath.h` | Weather UV, coverage remap과 cloud type 프로파일 CPU 기준 구현 |
| `Stage6LightMath.h` | 광학 깊이, Base 선택과 단일 산란 CPU 기준 구현 |
| `Stage7PhaseMath.h` | HG, 방향 내적, Dual-lobe와 안전 범위 CPU 기준 구현 |
| `Stage8AmbientMath.h` | 높이 가중치, AO와 multiple octave CPU 기준 구현 |
| `Stage13ScaleMath.h` | Renderer 프리셋과 CPU 테스트가 공유하는 meter 상사 변환·광학 깊이·파장당 표본 기준 |
| `Stage13OpenWorldMath.h` | 13-3 실제 고도·거리·sampling budget·Weather texel·거리 fade 기준 |
| `Stage13NoiseVolumeMath.h` | 13-4 Texture3D 규격, periodic Perlin-Worley/Worley CPU 기준과 Nyquist 검사 |
| `Stage13CameraPresets.h` | 단일 씬 F5~F8 position/target, 60° FOV와 0.1m/60km clip의 기준 |
| `Stage13SceneMath.h` | 10km 지면·20×60×20m 건물·50km 지원 반경, 입력·이동·숫자 Debug 매핑 기준 |
| `Stage13OpticsLightingMath.h` | km Beer-Lambert, Light sampling 후보와 Detail LOD CPU 기준 |
| `Stage9OptimizationMath.h` | 가변 View 구간, coarse 되감기와 weighted cone CPU 기준 |
| `NoiseVolumeCache.h` | 테스트 전용 cache header·parameter/payload hash와 손상 거부 |
| `Stage13CloudDomainMath.h` | 평면층의 아래·내부·위·수평·깊이 제한 교차 CPU 기준 |

## 프레임 순서

1. `Renderer`가 Y=0, X/Z ±5km 실제 사각 평면과 원점의 `20×60×20m` 건물을
   `R16G16B16A16_FLOAT` 색상 타깃과 `D32_FLOAT` 깊이에 항상 렌더링한다. linear RGB는
   지면 `(0.10,0.14,0.12)`, 건물 `(0.02,0.025,0.035)`이다.
2. 깊이 타깃을 DSV에서 해제하고 `R32_FLOAT` SRV로 전환한다.
3. 풀스크린 삼각형이 장면 색상과 깊이를 읽어 월드 레이, 월드 위치와 장면 거리를 복원한다.
4. 일반 실행은 Open World와 `Texture3D`를, 자동 회귀는 기존 AABB/13-2와
   `ProceduralLegacy`와 Legacy shape mode를 b1/b3/b5/b6/b7에 넣는다.
5. DomainCB 선택에 따라 AABB 또는 Y 평면층의 진입·이탈 거리를 구하고 Scene Depth와
   S배 된 View/Light 제한을 적용한다.
6. Physical Shape는 `windSpeed × effectiveTime`의 공통 수평 월드 변위를 Weather·Base·
   Detail에 적용하고 Base `t3`와 Detail `t4`를 linear-wrap 샘플링한다. Similarity/Legacy는
   기존 독립 속도와 단일 value noise 경로를 보존한다.
7. 월드 XZ로 Weather Map R/G/B/A를 읽고 A/G로 타입별 물리 두께를 정한다. 13-4E는
   Weather를 `support`와 70~100%의 완만한 수평 coverage로 분리하고, footprint는 threshold에
   20%만 반영한다. 세로 profile은 threshold가 아니라 최종 Base 밀도에 정확히 한 번 곱한다.
8. Weather Base가 있을 때만 Detail Noise로 깎는다.

## 단계 13-4D 단일 씬 런타임 계약

일반 실행의 장면·도메인은 하나뿐이다. 구름은 Planar `1500~7500m`, View/Fade/Light는
`50/40/20km`, Weather는 64km이며 analytic sky가 카메라 기준 50km를 담당한다. 별도
sky dome이나 shell은 만들지 않는다. F5~F8은 각각 Hero/건물 Depth, 지면 수평선, 구름
내부, 구름 위 하향 시점이며 geometry/domain을 바꾸지 않는다.

F1의 Noise/Weather/Shape/Sampling과 F3의 Lighting/Phase/Environment는 메인 화면
출력을 각각 한 콤보에서 선택한다. F2에는 Periodic Perlin·Channel Debug와 현재 G mode의
읽기 전용 표시만 노출한다. F4에는 F5~F8, 카메라 수치·FOV·이동 속도,
저장·복원·내보내기만 있다. Local Inspector, Stage 13 presets, Legacy Validation,
Cloud Scene/Domain, F9~F12와 문자 진단 키는 런타임에 존재하지 않는다.

숫자 키는 명시적 테이블을 공유한다: `0/1/2/3/4/5/6/7/8/9`는 각각 Composite,
Raw Noise, Weather Coverage, Base Density, Detail Noise, Final Density, View Optical
Depth, Accumulated Direct, View Transmittance, Light Transmittance다. 기존 HLSL ID 1~7은
삭제했으며 CPU에서 Composite로 sanitize한다. CloudCB는 여전히 128바이트다.

13-4D 내보내기는 schema 28, 13-4E 전체 snapshot은 schema 29, 13-5는 schema 30이었다.
단계 9 전체 snapshot은 schema 31/`implementationStage=9`이며 `optimization` preset과 b9 값을 기록한다.
13-4E Custom 외형 전용 원자 저장 파일은 schema 29를 유지한다. snapshot은 `sceneContract`, 현재/저장 카메라,
`cloudTypeMode`, `openWorldPipelinePreset`을 기록하며 `cloudScene`, `domainStates`,
`stage13Preset`, `similarityScale`, `diagnosticSceneEnabled`와 Stage1/2/4 preset 상태는
기록하지 않는다.

## 단계 13-4E Dense Broken-Sky와 외형 상태

일반 시작은 Stratus를 적용하고 Pipeline Compare 5 `Full Open World`는 저장 Custom과
무관하게 `Dense Mixed Default`를 복원한다. 이 Dense 기준은 coverage/density/extinction/erosion
`0.68/1.15/0.00035m^-1/0.18`이며 결정적 Weather의 R non-zero/core 점유율은
`79.62%/49.11%`다. Stratus는 `87.31%/56.60%`, Cumulus는 `73.82%/42.07%`다.
세 외형은 seed·period, Texture3D, wind/offset, camera/domain, sun/phase/environment와
View `512×100m`·Light `80×250m`를 변경하지 않는다. Stratus/Cumulus Coverage는
2026-08-17 사용자 피드백에 따라 `0.40/0.45`로 낮췄다.

F1 `Cloud Type Settings`는 Dense Mixed Default, Stratus, Cumulus, Custom과
`Save Current as Custom`을 제공한다.
외형 소유 슬라이더를 움직이면 `Custom Unsaved`가 되며 자동 저장하지 않는다. 저장 파일은
`captures/noise-lab/custom/noise-settings.json`에 임시 파일을 완전히 쓴 뒤 원자 교체한다.
시작 때 schema 29를 메모리 슬롯에만 읽고 자동 적용하지 않는다. 누락·구버전·손상·비유한·
범위 밖 값은 렌더 상태를 건드리지 않고 Dense Mixed fallback 메시지를 표시한다. 전체 snapshot의
`cloudAppearance`는 active/dirty/current/saved Custom을 기록한다. 13-4E 자체에서는
상수버퍼 ABI를 바꾸지 않았고, 이후 13-5 보완에서 LightCB/EnvironmentCB만 80바이트로 확장했다.

Pipeline Compare 버튼으로 들어간 1~5 단계의 main cloud pass만 effective time `0`을 사용한다.
카메라·도메인·일반 Animation 상태는 그대로이며 외형 버튼이나 파라미터 편집으로 Compare를
벗어나면 정상 effective time으로 즉시 돌아간다.
9. 최종 밀도가 있는 View 표본에서 태양 방향 도메인 이탈까지 Base Density를 적분한다.
10. 카메라→표본과 표본→태양 방향으로 Phase를 계산하고 `Tsun` 기반 표면 마스크로 외곽 적용 범위를 제한한다.
11. 높이·밀도 AO에 `Tsun` 차폐를 결합하고 다중 산란을 태양 차폐 내부 쪽으로 이동시킨다.
12. Direct/Sky/Ground/Multiple을 같은 View 구간에 누적하고 배경과 합성한다.
13. RGB peak 0.8 위만 LDR highlight shoulder로 압축해 UNORM 흰색 clip을 막는다.
14. 개발 UI가 열린 F1~F4 독립 창과 성능 오버레이를 그리고, F1이 열렸을 때만 Noise Lab 단면을 갱신한다.
14. GPU Frame timestamp를 닫은 뒤 VSync 설정에 따라 Present한다.

## 프레임 성능 계측

`FrameProfiler`의 CPU 범위는 `Renderer::Render` 시작부터 `Present` 반환까지라서 VSync 대기를
포함한다. GPU Frame 범위는 진단 장면 직전부터 ImGui draw 직후까지이며 Present는 포함하지
않는다. GPU Cloud는 그 안의 `RenderCloudPass`만 측정한다.

GPU 시간은 8개 query 슬롯을 순환하며 완료된 과거 프레임만
`D3D11_ASYNC_GETDATA_DONOTFLUSH`로 읽는다. 준비되지 않은 query 때문에 CPU나 GPU를 기다리지
않으며, ring이 모두 사용 중이면 해당 프레임 계측만 생략한다. 리사이즈 시 통계를 reset하고
이전 세대 결과를 폐기한다. 자세한 비교 절차는 [PERFORMANCE.md](PERFORMANCE.md)를 따른다.

## 상수버퍼

### `CameraCB` / `cbCamera` (`b0`, 224바이트)

| 필드 | 타입 | 의미 |
|---|---|---|
| `invViewProj` | `float4x4` | 화면 좌표와 장치 깊이를 월드 좌표로 복원 |
| `invProjection` | `float4x4` | NDC를 카메라가 원점인 View Space로 복원 |
| `invViewRotation` | `float4x4` | translation 없이 View 방향을 World 방향으로 회전 |
| `cameraPos`, `time` | `float3`, `float` | 월드 레이 원점과 바람 이동용 경과 시간(s) |
| `renderSize` | `float2` | 픽셀 크기와 화면 종횡비 계산 |
| `nearPlane`, `farPlane` | `float`, `float` | meter 단위 카메라 절두체 범위 |

### `CloudParameters` / `CloudCB` (`b1`, 128바이트)

CPU 구조체와 HLSL cbuffer의 16바이트 묶음을 항상 동시에 변경한다.

| 묶음 | 필드 | 기본값과 현재 역할 |
|---|---|---|
| 0 | `cloudBoundsMin(float3)`, `densityMultiplier` | `(-8,-1,-8)m`, `1.0`; 실행 기본 넓은 경계 최소와 threshold 뒤 밀도 배율 |
| 1 | `cloudBoundsMax(float3)`, `stepSize` | `(8,2,8)m`, `0.10m`; 실행 기본 넓은 경계 최대와 목표 간격 |
| 2 | `maxViewSteps`, `extinctionCoefficient`, `transmittanceThreshold`, `debugMode` | `128`, `1.0`, `0.01`, `0`; Optimized View의 early exit threshold |
| 3 | `baseNoiseScale`, `coverage`, `windSpeed`, `noiseOffset` | `0.35 cycle/m`, `0.55`, `0.25m/s`, `0`; Physical 전체 구름의 Bulk 이동, Legacy Base 이동 |
| 4 | `windDirection(float3)`, `bottomFadeEnd` | 정규화 `(0.9701,0,0.2425)`, `0.20`; 월드 바람 방향과 바닥 fade 종료 높이 |
| 5 | `topFadeStart`, `minimumLocalThicknessFraction`, `localHeightVariation`, `cumulusTopBoost` | `0.80`, `0.40`, Open World `1.0`, `0.35`; 최소 1.2km 두께와 XZ별 로컬 상단 제어 |
| 6 | `detailNoiseScale`, `detailErosionStrength`, `detailWindSpeed`, `detailNoiseOffset` | `2.5 cycle/m`, `0.25`, `0.45m/s`, `17.3`; 표면 침식, 속도는 Legacy 전용 |
| 7 | `weatherMapWorldSize`, `weatherMapWindSpeed`, `weatherMapOffset(float2)` | `16m`, `0.10m/s`, `(0,0)`; Weather 반복 크기·UV offset, 속도는 Legacy 전용 |

구조체는 16바이트 묶음 여덟 개다. `transmittanceThreshold`는 단계 9 Optimized View early exit가 읽는다. `minimumLocalThicknessFraction/localHeightVariation/cumulusTopBoost`는 Similarity와 구형 회귀 전용이며 Open World는 b7을 사용한다.

Physical Shape의 이동은 `normalizeOrZero(windDirection.xz) × windSpeed × effectiveTime`을
Weather/Base/Detail 샘플 위치에서 똑같이 빼는 강체 이동이다. Y는 바꾸지 않는다.
`weatherMapWindSpeed`와 `detailWindSpeed`는 CloudCB 배치와 Legacy 회귀를 위해 남지만
Physical에서는 읽지 않는다. 따라서 Coverage·Type·Thickness 경계와 내부 3D 밀도가
같은 월드 거리로 이동한다.

13-2의 `1x/10x/100x/1000x` 버튼은 구조체 크기를 바꾸지 않고 위 필드값을 Stage 8
기준에서 매번 다시 계산한다. 순서 의존성이 없으며 coverage·density·offset·높이 비율·step
개수는 유지한다. Weather Map 픽셀은 Uniform Legacy로 고정하고 월드 배치 크기만 S배한다.

| S | 층 두께 / View step / Light step | Base / Detail cycle/m | extinction 1/m | Weather 크기 |
|---:|---|---|---:|---:|
| 1× | `3m / 0.1m / 0.25m` | `0.35 / 2.5` | `1.0` | `16m` |
| 10× | `30m / 1m / 2.5m` | `0.035 / 0.25` | `0.1` | `160m` |
| 100× | `300m / 10m / 25m` | `0.0035 / 0.025` | `0.01` | `1.6km` |
| 1000× | `3km / 100m / 250m` | `0.00035 / 0.0025` | `0.001` | `16km` |

### `CloudDomainParameters` / `CloudDomainCB` (`b5`, 32바이트)

| 묶음 | 필드 | 기본값과 현재 역할 |
|---|---|---|
| 0 | `domainType`, `cloudBottomAltitude`, `cloudLayerThickness`, `maxViewTraceDistance` | AABB Reference, `1500m`, `3000m`, `50000m` |
| 1 | `viewTraceFadeStartDistance`, `maxLightTraceDistance`, padding | `40000m`, `20000m`; 평면층 거리 fade와 Light 상한 |

`domainType=0`은 기존 CloudCB AABB를 그대로 사용해 단계 8 회귀 화면을 보존하고,
`domainType=1`은 무한한 XZ와 유한한 Y 범위의 평면층을 사용한다. CPU와 HLSL 모두
수평 레이가 층 밖이면 miss, 층 안이면 유한 추적 거리까지 hit로 처리한다.
런타임 도메인은 회귀용 AABB와 최종 `PlanarLayer`만 허용한다. 구형 shell 예약값은
2026-08-17 사용자 결정으로 제거했다.

13-2 프리셋은 평면층 바닥 `-1×S`, 두께 `3×S`, View/Fade/Light 제한
`50×S/40×S/20×S`를 CloudCB의 높이 범위와 함께 맞춘다. 1000×의 `-1000~2000m`는
상사 검증용이다.

13-4E/13-5 Open World는 Planar `1500~7500m`, View/Fade/Light `50/40/20km`, View
`100m/512`, Light `250m/80`, Weather `64km`, Base/Detail `0.00035/0.0025 cycle/m`,
density `1.15`, extinction `0.00035/m`, albedo `1`을 원자적으로 적용한다. 최대 6km의
단순 extinction scale은 τ=2.1이며 실제 τ에는 density/noise/profile이 들어간다. 일반 실행만 이
프리셋과 Periodic Perlin으로 시작하며 숨김 자동 테스트는 Stage 8 AABB 기본값을 보존한다.

### `CloudShapeParameters` / `CloudShapeCB` (`b7`, 64바이트)

| 묶음 | 필드 | Open World 값과 의미 |
|---|---|---|
| 0 | `shapeMode`, Stratus 최소/최대, Cumulus 최소 | Physical, `1500/2500/3000m` |
| 1 | Cumulus 최대, Stratus bottom/top, Mixed bottom | `6000m`, `0.06/0.65/0.10` |
| 2 | Mixed top, Cumulus bottom/top, lower mass | `0.86/0.08/0.93/0.65` |
| 3 | Cumulus upper-mass start/end, padding | `0.08/0.70`; 타입별 세로 질량 |

`shapeMode=WeatherPhysicalThickness`는 A를 타입별 최소·최대 두께 사이에서 보간한다.
Physical mode는 footprint만 Base Noise 통과 coverage에 20% 반영하고 vertical profile은
최종 Base 밀도에 한 번 곱한다.
Stratus/Mixed/Cumulus footprint cutoff는 각각 `0.16/0.08/0.22@0.45`,
`0.22/0.04/0.38@0.50`, `0.32/0.03/0.62@0.58`의 바닥/최대 폭/상단을
전체 높이에 걸쳐 연결한다. 따라서 Weather R=1 코어도 profile이 1보다 작으면 좁아진다.
`LegacyNormalizedLayer`는 기존 CloudCB의 로컬 상단 수식을 그대로 사용해 이전 회귀를 보존한다.

### Stage 13-4B 사용자 카메라

`Stage13CameraPresets.h`가 일반 F5~F8의 월드 position/target과 진단 장면 표시 여부를
Window와 GPU smoke에 함께 제공한다. 일반 Open World 프리셋은 X=40m에서 주황 건물을
피하고 진단 장면을 숨긴다. 건물 Scene Depth와 Similarity/Texture wrap 진단은 F4의
Planar Diagnostics 버튼으로 명시적으로 선택한다. Camera의 LookAt은 position-target을 orbit
yaw/pitch/distance로 변환한다. 휠은 지수식이라 큰 delta에도 음수 거리가 생기지 않으며
일반/Shift 한 notch의 거리 비율은 각각 1.25/2다.

### Stage 13-4C Local Cloud Inspector

Inspector는 같은 `CloudDomainType::AabbReference`와 `kCloudDomainAabb`를 사용하지만 UI에서
기술 이름을 숨긴다. 기본 AABB는 `(-100,5,-120)m~(100,55,80)m`, Weather 타일은 200m,
Base/Detail 반복 크기는 `90m/30m`다. View `0.5m×512`, Light `1m×256`, extinction
`0.03/m`가 Open World의 대표 `τ=1.5`를 작은 장면에서 재현한다. 진단 필드와 20×60×20m
주황 건물은 Scene Depth를 제공하며 첫 F5 구도에서 건물이 구름 앞을 가린다.

`Renderer`는 Weather·Cloud Type·Shape·Noise·조명·sampling을 하나의 공통 렌더 상태로
유지하고, Inspector/Open World에는 AABB/평면층 기하·trace 범위와 마지막 카메라만 따로
저장한다. 장면 전환은 Weather texture를 재생성하거나 렌더 프리셋을 적용하지 않는다.
`Camera::TranslateRigLocal`은 현재 view forward/right로 position과 target을 같은 변위만큼
옮겨 orbit 거리와 방향을 보존한다. 두 도메인 모두 F4 공통 속도(기본 `20m/s`, `Shift 4x`)와
제한된 delta time으로 `WASD`를 처리하고 ImGui 키보드 캡처 중에는 입력을 무시한다.

### `NoiseVolumeParameters` / `NoiseVolumeCB` (`b6`, 96바이트)

| 묶음 | 필드 | Open World 값과 의미 |
|---|---|---|
| 0 | `noiseSource`, `baseResolution`, `detailResolution`, `seed` | `Texture3D`, `128`, `32`, `1337`; 경로 선택과 결정적 생성 규격 |
| 1 | `baseWorldSizeMeters`, `detailWorldSizeMeters`, `baseVerticalWorldSizeMeters`, padding | `12000m`, `2000m`, `12000m`, `0`; Base XYZ 등방/Detail XYZ world-fixed 반복 크기 |
| 2 | `baseVolumeFrequencies(uint4)` | `4/9/17/23`; Base Perlin과 Worley 대역 |
| 3 | `detailVolumeFrequencies(uint4)` | `2/3/4/5`; Detail Worley 대역 |
| 4 | `baseVolumeWeights(float4)` | `0.625/0.25/0.125/0`; G/B/A cellular 결합 |
| 5 | `detailVolumeWeights(float4)` | `0.50/0.30/0.15/0.05`; RGBA erosion 결합 |

`NoiseVolume.hlsl`의 `CSBase`와 `CSDetail`은 각각 `4×4×4` thread group으로
`DXGI_FORMAT_R8G8B8A8_UNORM` Texture3D를 생성한다. Base R은 periodic
Perlin-Worley이며 네 Perlin octave는 `seed + octave×173`을 사용한다. G/B/A는 주파수가
다른 Worley distance이고 Detail RGBA는 기존 네 Worley
대역이다. 총 메모리는 `128³×4 = 8,388,608B`와 `32³×4 = 131,072B`다.

구름과 Noise Lab은 Base `t3`, Detail `t4`, linear-wrap `s1`을 공유한다. Base XYZ는
12km 등방 타일이며 주파수 `4/9/17/23`의 파장은 `3000/1333.33/705.88/521.74m`,
100m View step의 파장당 표본은 `30/13.33/7.06/5.22`다. Detail
XYZ는 2km 타일이며 주파수 `2/3/4/5`의 표본 수가 `10/6.67/5/4`다. 모든 생성 주파수는
각 해상도의 Nyquist 상한보다 낮다. 핫리로드는 VS/PS/CS와 새 두 볼륨 생성·hash까지 모두
성공한 경우에만 한 세대로 교체한다. cache header version 3은 이전 생성 알고리즘 cache를 거부한다.

### `CloudLodParameters` / `CloudLodCB` (`b8`, 16바이트)

| 묶음 | 필드 | Open World 값과 의미 |
|---|---|---|
| 0 | `detailLodEnabled`, `detailLodStartMeters`, `detailLodEndMeters`, `detailNeutralValue` | `1`, `32000m`, `48000m`, 생성된 Detail의 weighted mean; 원본→평균 전환과 끝 거리 sample 생략 |

`detailNeutralValue`는 상수 0.5가 아니다. compute 생성 뒤 hash를 위해 읽은 실제 `32³ RGBA8`
데이터와 현재 `detailWeights`를 결합해 계산하며 기본 seed에서는 `0.44994098`이다. 32km
전에는 원본 Detail, 32~48km에서는 `smoothstep`으로 평균에 수렴하고 48km 뒤에는 `t4`를
읽지 않는다. 평균 침식은 계속 적용하므로 LOD 경계에서 구름 두께가 갑자기 변하지 않는다.
Similarity 프리셋은 LOD를 꺼 상사 회귀 화면을 보존한다.

### `OptimizationParameters` / `OptimizationCB` (`b9`, 64바이트)

| 묶음 | 필드 | 기본값과 역할 |
|---|---|---|
| 0 | `supportPrecheckEnabled`, `emptySpaceSkippingEnabled`, `viewEarlyExitEnabled`, `distanceStepEnabled` | 네 최적화 축의 독립 On/Off |
| 1 | `emptySamplesBeforeCoarse`, `baseDensityEpsilon`, `coarseStepMultiplier`, `maxSearchStepMeters` | `3`, `0.0001`, `2`, `400m`; 공백 판정과 탐색 상한 |
| 2 | `distanceStepStartMeters`, `distanceStepEndMeters`, `farStepMultiplier`, padding | `16/48km`, `1`; smoothstep 거리 step |
| 3 | `lightSamplingMode`, `coneSampleCount`, `coneAngleDegrees`, `lightFarSampleFraction` | 초기 fallback은 Straight, `6`, `3°`, `0.85`; 자동 합격 Balanced는 Cone `6`, `2°`, `0.77` |

`Approved Reference`와 `Fine Reference`는 모든 View 최적화가 Off이고 Straight Light라서
`mainReference`를 선택한다. Balanced/Conservative와 개별 Custom 조합은
`mainOptimized`를 선택한다. Debug에서도 Reference는 `/Od`, 동적 Optimized는 실제 비용과
D3D11 instruction 한도를 위해 `/O1`로 컴파일한다. 두 PS와 나머지 셰이더가 모두 성공해야
핫 리로드 세대가 교체된다.

F1 Optimization의 활성 Master는 Balanced → Conservative → Approved Reference → Fine
Reference, Empty Search는 2× → Off 순서다. Fast와 4×는 2026-08-19 사용자 검증에서
400m deterministic 표본의 등고선 alias가 확인되어 활성 UI와 자동 후보에서 제외했다.
enum `Fast=0`과 4× preset 값은 schema 31 숫자와 실패 이력 재현을 위해서만 유지한다.
새 디버그 ID 60~63은
Executed View Samples, Skipped Distance, Early Exit Savings, Support Precheck Skip이며
숫자 0~9 단축키 표는 바꾸지 않는다. Cone Far Fraction은 탭 수가 같아 계산량이 같은
`75/77/85/95%` 비교 버튼이며, 자동 스윕에서 4°→3°→2°와 77% 순으로 올린 첫 합격값을
Balanced에 반영했다.

### `LightParameters` / `LightCB` (`b3`, 80바이트)

| 묶음 | 필드 | 기본값과 역할 |
|---|---|---|
| 0 | `directionToSun(float3)`, `sunIntensity` | normalize `(0.45,0.80,0.35)`, `1.0`; 표본→태양 월드 방향과 세기 |
| 1 | `sunColor(float3)`, `singleScatteringAlbedo` | `(1,0.95,0.85)`, `1.0`; linear RGB와 `ω=σs/σt` 무차원 산란 비율 `[0,1]` |
| 2 | `maxLightSteps`, `lightStepSize`, `lightRayBias`, `phaseEnabled` | Stage 8 `16/0.25m/0.01m`, Open World `80/250m/1m`; Phase Off 기본값 |
| 3 | `forwardScatteringG`, `backwardScatteringG`, `phaseBlend`, `phaseIntensity` | `0.65`, `-0.25`, `0.80`, `0.25`; 전방·후방 HG와 적용 강도 |
| 4 | `edgeInfluence`, `edgeOpticalDepthScale`, `shadowExponent`, padding | `0/1/1/0`; Phase 외곽 제한 비율·폭과 직접광 그림자 대비 중립값 |

LightCB는 CloudCB와 분리해 `b3`에 바인딩한다. 방향은 빛의 진행 방향이 아니라
현재 표본에서 태양으로 나가는 방향이다. CPU는 방향을 정규화하고 음수·비정상
값을 안전 범위로 제한하며 알베도는 `[0,1]`로 제한한다. `extinctionCoefficient=σt`
는 `1/m`, `singleScatteringAlbedo=ω=σs/σt`는 무차원이고 `σs=ωσt`다.
13-2의 1000× `lightStepSize=250m`, `lightRayBias=10m`와 13-5 reference `320 steps`를
보존하도록 길이 필드의 sanitize 상한은 각각 1000m와 100m이며 step 수 상한은 512다.
HLSL bias도 CPU와 같은 0~100m 범위를 사용한다. Open World Light Ray는
Weather support·로컬 높이·세로 profile이 0인 표본을 Base Texture3D 조회 전에 거르고,
나머지는 View Base와 같은 수식을 사용한다. 누적 광학 깊이가 `9.21034`에 도달해
투과율이 `0.0001` 이하가 되면 해당 Light Ray만 종료하며 `Total Light Samples`는 실제
실행 횟수를 표시한다. Phase는
직접 산란량에만 적용하며 Light 투과율과 광학 깊이를 바꾸지 않는다. Phase Off에서는
최종 배율이 정확히 1이다. raw HG/dual 진단은 16까지 보존하지만 LDR 합성에 적용하는 최종
배율은 2.5로 제한한다. Silver Lining은 `g/blend/intensity=0.75/0.90/0.20`,
`edgeInfluence/scale/shadowExponent=0.85/2.0/1.35`로 태양 투과율이 높은 얇은 표면에만
강한 전방 산란을 남긴다. 중립값 `0/1/1`은 이전 직접광을 보존한다.

### `EnvironmentParameters` / `EnvironmentCB` (`b4`, 80바이트)

| 묶음 | 필드 | 기본값과 역할 |
|---|---|---|
| 0 | `skyColor(float3)`, `skyStrength` | `(0.35,0.50,0.75)`, `0.12`; linear 하늘색과 세기 |
| 1 | `groundColor(float3)`, `groundStrength` | `(0.18,0.12,0.08)`, `0.05`; linear 지면색과 세기 |
| 2 | `ambientOcclusionStrength`, `ambientHeightInfluence`, `multipleScatteringEnabled`, `multipleScatteringOctaves` | `1.50`, `0.65`, `1`, `2`; AO·높이·octave 제어 |
| 3 | `multipleScatteringAttenuation`, `multipleScatteringExtinctionFactor`, `multipleScatteringPhaseFactor`, padding | `0.20`, `0.50`, `0.25`, `0`; 반복 에너지·광학 깊이·방향성 감소 |
| 4 | `ambientShadowCoupling`, `ambientShadowExponent`, `multipleScatteringInteriorBlend`, padding | `0/1/0/0`; 기존 환경광을 보존하는 태양 차폐·내부 가중 중립값 |

EnvironmentCB는 `b4`에 바인딩하며 외부 SRV나 sampler를 추가하지 않는다. 기본 Balanced
프리셋은 환경광을 켜고 Off는 Sky/Ground/Multiple을 0으로 만들어 단계 7 결과를 보존한다.
Portfolio Hero는 `0.55/0.50/0.75`의 차폐·곡선·내부 가중값으로 푸른 fill은 남기되
태양측 외곽과 자기 그림자를 덮지 않는다. 새 계산은 기존 Light Ray 결과만 재사용한다.
Cube Map이나 실제 대기 입력은 단계 14에서 `skyColor` 평가만 교체할 수 있다.

### Weather Map 리소스

`t2`는 CPU 생성 `DXGI_FORMAT_R8G8B8A8_UNORM` 256² Weather Map이고 `s1`은
linear-wrap sampler다. R/G/B/A는 coverage/cloud type/density source/local thickness potential이다.
Scene Color `t0`, Scene Depth `t1`, Base Volume `t3`, Detail Volume `t4`,
point-clamp `s0`와 register를 분리한다.

Texture2D와 SRV는 초기화 때 `D3D11_USAGE_DEFAULT`, mip 1개로 한 번만 만든다.
F2 Weather 창의 프리셋 전환이나 생성기 변경은 먼저 `t2` 바인딩을 해제한 뒤
`UpdateSubresource`로 같은 texture에 256² RGBA만 업로드한다. 생성 설정은 CPU
전용 `WeatherMapGeneratorSettings`이며 128바이트 CloudCB에는 들어가지 않는다.
업로드 전 크기와 RGBA 길이를 검증하고 실패하면 기존 맵과 해시를 유지한다.

Open World Coverage 기본값은 seed `1013`, Macro/Detail `4/11`, Detail Weight `0.42`,
Bias `-0.02`, Contrast `1.15`, Threshold/Softness `0.56/0.14`다. 기본 맵은 약 43.8%를
덮고 토러스 연결 성분 약 10개, 최대 성분은 전체 맵 약 31.2%다. A는 독립 두께장과
`smoothstep(0.05,0.95,R)`을 `Thickness-Coverage Link`로 보간하며 기본 `0.20`은 독립
두께 80%, Coverage 중심 20%다. 빈 R 픽셀의 A는 항상 0이다.

### `NoiseLabParameters` / `NoiseLabCB` (`b2`, 32바이트)

| 묶음 | 필드 | 의미 |
|---|---|---|
| 0 | `normalizedSlicePosition(float3)`, `outputMode` | 도메인 내부 교차점과 30개 density/Weather/3D volume/shape 출력 선택 |
| 1 | `sliceAxis`, `effectiveTime`, `padding(float2)` | XY/XZ/YZ 축과 구름 패스와 공유하는 시간 |

Noise Lab은 512² 단면 타깃 세 벌과 실제 Weather SRV를 사용한다. Periodic Perlin의
R/G/B seed·주기·가중치·bias·contrast와 coverage threshold/softness, density의
coverage influence, A의 `Thickness-Coverage Link`를 편집한다. Live Update는 CPU 생성·업로드를 최대 10Hz로
제한하고 조작이 끝난 값은 즉시 반영한다. 내보내기는 세 단면과 256²
`weather-map.png`, 모든 생성 설정·맵 해시·Light/Environment/Domain과
`cloudScene`, `cloudTypeMode`, `sharedRenderState`, `domainStates`와 두 카메라,
`stage13Preset`, `openWorldPipelinePreset`, `noiseSource`, 두 volume 규격·seed·hash를 담은
schema 27 JSON을 기록한다. Local 20m/s/Open World 1000m/s의 장면별 이동 속도, A 생성 설정,
CloudShapeCB와 Base XYZ world size, Physical 이동 mode·Bulk 속도·누적 이동 거리와 현재/저장 카메라 상태도 포함한다. Similarity 프리셋만
`similarityScale` 숫자를 기록하고 Open World/Custom은 `null`이다.
Generator는 `Weather map`과 분리된 최상위 헤더로 기본 펼쳐지고, 그 안의 R/G/B
채널은 각각 기본으로 접힌다. A Local Thickness의 seed/period/weight는 고정 설명으로 보이고,
Coverage 연결 강도만 슬라이더로 조절한다. `Next Seeds`는 seed만 결정적으로 갱신한다. 프리셋과 생성 설정은 F2 Weather 창에서
조작하며, 다른 프리셋에서도 초안은 보존되고 Periodic Perlin을 다시 선택하면 반영된다. 상위 헤더를
접으면 채널과 적용 버튼이 모두 숨겨지지만 sanitize와 보류 중인 Live Update
처리는 표시 상태와 독립적으로 계속된다.

F1은 XY/XZ/YZ 단면·Output·Stage 13/3D Noise/Domain/공통 밀도/Height/Detail/
Performance/Animation을, F2는 Weather Map/Generator를, F3는 장면별 Light 후보·Detail LOD·
조명 성분 진단/Directional Light/Phase/Environment를 담당한다. Local Inspector에는
`0.5/1/2m`, Open World에는 `62.5/125/250m` 후보만 표시한다. Directional Light의 XZ
도식은 태양 위치, 들어오는 광선, 그림자 방향과 현재 카메라 진행 방향을 함께 표시한다.
F4는 현재 position·target·거리·FOV·clip을 텍스트로 표시하고
F5~F8과 같은 네 시점, FOV 조절, 현재 위치 저장·복원과 JSON 내보내기를 제공한다.
각 창 안의 대분류는 독립적으로 접고 펼칠 수 있다.

키보드 메시지는 전역 진단 명령을 ImGui보다 먼저 분류한다. 단, `WantTextInput` 또는 실제
활성 UI item이 있을 때는 장면 입력을 차단한다. 일반 창 포커스만으로는 `0~9`, 진단 키,
F키와 WASD를 막지 않으며 ImGui keyboard navigation은 WASD를 선점하지 않는다.

## 셰이더 핫리로드

`shaders/` 아래 모든 `.hlsl`과 `.hlsli`의 수정 시간을 재귀 감시한다. 변경 시 모든
VS/PS/Noise CS를 임시 객체로 컴파일하고 Base/Detail Texture3D까지 임시 생성한다. 전부
성공할 때만 shader와 volume을 generation 하나로 교체한다. 실패하면 직전 generation과
두 volume hash를 유지하므로 Noise Lab과 구름이 서로 다른 noise 알고리즘을 사용하는
프레임은 없다.

## 디버그 입력

| 키 | 출력 |
|---|---|
| `0` | Composite |
| `1` | Raw Noise (HLSL ID 10) |
| `2` | Weather Coverage (ID 20) |
| `3` | Base Density (ID 16) |
| `4` | Detail Noise (ID 17) |
| `5` | Final Density (ID 12) |
| `6` | View Optical Depth (ID 52) |
| `7` | Accumulated Direct (ID 32) |
| `8` | View Transmittance (ID 8) |
| `9` | Light Transmittance (ID 24) |
| `F1` | Noise texture·밀도·VSync·Time 창 표시/숨김 |
| `F2` | Weather Map·Periodic Generator 창 표시/숨김 |
| `F3` | 방향광·Phase·환경광 창 표시/숨김 |
| `F4` | 카메라 값·F5~F8 버튼·저장 위치 창 표시/숨김 |
| `F5`~`F8` | Hero/Building Depth, Ground Horizon, Inside Cloud, Above/Down 고정 카메라 |
| `W/S/A/D`, `Shift` | 카메라 rig 이동과 가속 |
| 마우스 드래그/휠 | 오빗/줌 |

상단 숫자와 숫자 패드는 `Stage13SceneMath`의 명시적 테이블을 공유하며 직접 enum cast하지
않는다. ImGui 텍스트 입력 중에는 전역 키를 차단한다. 문자 진단 키와 modifier 진단 조합,
`F9`~`F12`는 처리하지 않으며 HLSL ID 1~7 입력은 Composite로 sanitize한다. 나머지 중간 출력은
F1 `Noise/Weather/Shape/Sampling Debug View`와 F3 `Lighting/Phase/Environment Debug View`에서
선택한다.

## 의도적으로 제외한 기능

- 외부 Weather PNG 로딩·페인팅·precipitation, fBm/Worley와 shadow
- Cube Map/IBL·실제 대기 입력, Light Ray Detail Erosion
- 단계 9보다 더 복잡한 오차 기반 adaptive stepping
- 저해상도, temporal reconstruction, 영구 캐시와 프리셋

실행 기본 `Y` 볼륨은 X/Z `±8m`로 15×15m 진단 바닥을 덮는다. `Q`는 기존
X/Z `±2m` 수치 검증 범위를 보존한다. 두 프리셋의 Y `-1~2m`와 높이 프로파일은
같으며, 넓은 볼륨의 긴 레이는 128 step 상한 때문에 실제 간격이 `0.10m`보다
커질 수 있다.
## 단계 13-2 상사 수치 진단 경로

일반 실행은 기존 `R8G8B8A8_UNORM` 백버퍼를 그대로 사용한다. 자동 진단 명령
`--stage13-similarity-gpu-test`만 같은 Cloud PS 출력을 320×180
`R32G32B32A32_FLOAT` 오프스크린 타깃에 그린 뒤 staging texture로 readback한다.
Renderer는 파일을 저장하지 않고 `CloudDiagnosticFrame`의 float RGBA 배열만 반환한다.

`CloudHitMask(35)`는 b1 구조체 크기를 바꾸지 않는 추가 디버그 모드다. Ray Direction,
Noise UV, Raw Noise, Final Density는 자동 게이트이며 Light Transmittance, Accumulated
Direct Lighting, Composite는 원인 분리용 텍스트 보고다. 현재 경로, 배율 clip 경로,
원점 중심 경로를 각각 비교해 clip precision과 큰 translation precision을 구분한다.

레이 방향은 `invViewProj`로 큰 월드 점을 만든 뒤 `cameraPos`를 빼지 않는다. 먼저
`invProjection`으로 View Space 방향을 만들고 `w=0`인 벡터에 `invViewRotation`만
적용한다. `invViewProj`는 Scene Depth의 실제 월드 위치 복원에만 남겨 둔다.
