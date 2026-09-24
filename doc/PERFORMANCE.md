# 성능 측정 기준

현재 성능 목표는 Full-resolution High 한 경로의 실제 비용을 측정하는 것이다. Low/Medium, Resolve, Temporal, Composite 별도 항목은 존재하지 않는다.

## GPU profiler 범위

`FrameProfiler`는 8-slot D3D11 timestamp query ring으로 다음 순서를 비동기 측정한다.

| 항목 | 범위 |
|---|---|
| `Weather Map` | generator key가 바뀐 프레임의 256² Compute/검증 구간. overlay는 마지막 실제 generation 시간·번호도 유지 |
| `Atmosphere LUT` | 이번 프레임에 필요한 LUT compute. hash가 같으면 거의 0ms |
| `Shadow` | Near/Far Balanced512 Deep Optical-Depth Cache compute |
| `Opaque` | HDR 지면·건물과 D32 depth raster |
| `Cloud` | Full-resolution raymarch와 scene/atmosphere HDR 합성 |
| `Tone` | HDR exposure, white balance, tone curve, sRGB와 dither |
| `Frame` | 첫 timestamp부터 Tone/ImGui 종료까지 전체 GPU 구간 |

CPU Frame은 `Renderer::Render`부터 `Present` 반환까지이며 VSync 대기를 포함할 수 있다.
Performance overlay는 `Time → FPS → CPU frame → GPU frame` 순서다. Time은 구름
시뮬레이션 초이며 프레임 비용이 아니다. CPU/GPU 프레임과 GPU 패스 시간은
`새 평균 = 이전 평균 × 0.9 + 최신 표본 × 0.1`인 지수 이동 평균(EMA)이다.
Weather 행에 마우스를 올리면 평활화 전 최신 GPU 표본(raw)을 볼 수 있다.

`Last Weather rebuild (CPU elapsed)`는 GPU 패스 표와 분리한다. `Elapsed`는
`GenerateWeatherMapTexture`의 compute 제출 직전부터 staging 생성, GPU readback
대기, CPU 행 복사와 Unmap까지의 CPU 벽시계 경과 시간이다. 해시 계산과 공개
texture 복사는 제외된다. 앞서 제출된 GPU 작업 대기도 포함될 수 있으므로 수십 ms가
표시되어도 Weather CS 자체의 실행 시간으로 해석하지 않는다. 마지막 값을 다음
성공까지 유지하며, 매 프레임 비용이 아니다. `Rebuild count`는 이 Renderer 세션의
성공한 생성 횟수다. 결과 hash가 같아 공개 복사를 생략해도 생성했으면 증가하고,
generator key가 같아서 생성 자체를 생략하면 증가하지 않는다. 기존 `#1132`는 1,132번째
성공 생성이라는 뜻이다. GPU 패스 평활화/Weather 값/재생성 제목·횟수에 설명 tooltip을 둔다.

VSync Off는 지원 환경에서 tearing 허용 즉시 Present를 사용하지만 GPU 렌더 자체가
병목이면 FPS는 오르지 않는다. 성능 gate는 표시 주기와 분리된 raw GPU timestamp를 사용한다.

## 최종 성능 gate

배포 성능 gate는 `VCLOUD_STRICT_VALIDATION=OFF`인 Release에만 적용한다.
ON Strict Validation은 필요 시 사용하는 bit-exact 보조 TC이며 그 성능을 배포 성능으로 판단하지 않는다.
OFF에서도 Weather의 기존 `/Gis` parity 계약은 유지한다. 옵션 분리 전 all-strict의
Meadow InsideLayer 10.544ms는 보존할 과거 측정이며, OFF의 성능은 다시 측정한다.
빌드/테스트 명령과 tolerance 이미지 회귀 규약은 [CONTRIBUTING](CONTRIBUTING.md)을 따른다.

| 조건 | 기준 |
|---|---:|
| 해상도 | 1920×1080 |
| 빌드 | Release |
| 장면 | Urban / Meadow / Snow |
| 카메라 | F5 / F6 / F7 / F8 |
| 시간 | automated fixed-step |
| VSync/UI/preview | Off |
| Cloud p95 | `≤ 10.00ms` |
| Frame p95 | `≤ 16.67ms` |
| 유효 표본 | 100개 이상 |
| D3D11 | error/corruption/resource hazard 0 |

각 12개 case는 60 frame을 예열한 뒤 유효한 timestamp 표본 120개를 모은다. LUT/cache가 바뀌는 프리셋 전환 직후 값과 GPU clock 안정화 구간은 예열에 포함되며 steady-state p95에는 넣지 않는다. 전체 합산 p95뿐 아니라 **각 case의 p95도 같은 한계값을 통과해야** 한다. 30표본처럼 두 순간값이 p95를 결정하는 짧은 측정은 사용하지 않는다.

실행:

```powershell
ctest --test-dir build -C Release -R "VolumetricCloud.HighPerformance$" --output-on-failure -V
```

앱 직접 실행:

```powershell
.\build\Release\VolumetricCloud.exe --high-performance-test
```

출력 예:

```text
HIGH_PERFORMANCE=PASS samples=... cloud_p95_ms=... frame_p95_ms=...
```

## High 비용을 고정한 이유

품질 선택 UI가 없어도 다음 최적화는 항상 동작한다.

- Weather/local-column support precheck
- 연속 빈 표본 3개 뒤 2× coarse 탐색과 hit rewind
- 24~50km 거리 step, 최대 1.25×
- View transmittance `0.01` early exit
- Balanced512 Deep Cache와 cache miss용 8-tap deterministic cone

이 값은 `HighCloudQuality` CPU/HLSL 상수로 고정되어 성능 측정 중 바뀌지 않는다. Detail Texture3D는 모든 거리에서 유지하므로 성능 수치에는 Detail LOD 이득이 섞이지 않는다.

2026-09-24 근경 미세 Detail(E19): 타입별 `nearMicroStrength`가 0보다 크면 화면 footprint로 정한 근경(1080p/FOV60 중앙 약 tile×29.3m 이내) 표본에서만
전용 64³ 텍스처를 두 번 읽고 warp>0이면 gradient noise 벡터 1회를 더 계산한다. strength 0(기본)과 원경은 추가 조회가 없다.
시험 측정(Cumulus, tile 570~700/strength .5/DUAL/WARP .15·.2, 정/역 두 회차 Cloud 중앙값): Near75 약+.8ms, F5 약+1.4~1.7ms, F6 약+2.2~2.5ms, 최대 p95 약7ms로 예산 안이다.

## 비교 기준 보존

삭제 전 기준은 `captures/simplification-baseline-2026-08-31`에 로컬 보존한다. 이 fixture는 1920×1080, High, Temporal Off, Rim Off, Detail LOD Off, optimized direct 조건에서 만들었다.

기존 Stage 15B 정량 비교에서 High direct 경로는 reference 대비 edge normalized RGB RMSE `0.000250957`, edge transmittance MAE `0.0000908621`을 기록했고 debug layer gate를 통과했다. 이 수치는 삭제 전 동등성 근거이며 현재 런타임에 Reference PS를 다시 두기 위한 기능이 아니다.

## 핫 리로드 성능 gate

핫 리로드 시간은 shader 저장 후 transaction 내부 경과 시간으로 측정한다. startup과 Texture3D 생성이 필요 없는 테스트는 `enableNoiseVolumes=false`로 실행한다.

| smoke | 하드 게이트 | warm 목표 | 2026-09-01 결과 |
|---|---|---:|---:|
| Tone Map | 영향 프로그램 1, 성공 교체, 오류 rollback, 원본 cache hit 1 | 15초 이하 | `0.024s`, 통과 |
| `Noise.hlsli` | Cloud PS + Deep Shadow CS + NoiseLab PS 정확히 3 | 30초 이하 | warm `0.224s`, 통과 |
| Weather Map CS | 성공 시 안정된 texture/SRV에 교체, 오류 시 shader/hash/generation/identity 보존 | 15초 이하 | changed `0.166s`, invalid rollback·restore cache hit 통과 |

```powershell
ctest --test-dir build -C Debug -R "HotReloadSmoke$" -V
ctest --test-dir build -C Debug -R "HotReloadDependencySmoke$" -V
```

CTest script는 source shader를 build 하위 임시 폴더에 복사하고 표식 파일을 만든다. 실행 파일은 이 표식이 없는 디렉터리에서 forced mutation을 거부한다.

## 수치 해석

- `Cloud p95`가 높고 Shadow는 안정적이면 View density/lighting 비용을 먼저 본다.
- `Shadow p95`만 높으면 cache invalidation이 매 프레임 잘못 발생하는지 확인한다.
- `Weather Map`이 motion/type selection 변경에도 높으면 generator key 소유권을 확인한다.
- `Atmosphere LUT`가 정지 상태에서도 지속적으로 높으면 LUT hash나 카메라 의존 aerial invalidation을 확인한다.
- `Frame - (Weather+Atmosphere+Shadow+Opaque+Cloud+Tone)`은 query 사이의 명령·UI와 driver scheduling 여유다.
- 자동 gate 통과는 최종 외형 승인이 아니다. shimmer, 구름 절단, 산란과 색은 사용자가 Release 화면에서 확인한다.

## 최신 결과

2026-09-05 정책 분리 후 OFF Release 전체 CTest의 성능 실행은 Meadow InsideLayer
Cloud p95 `10.289ms`, Frame p95 `10.833ms`로 case별 Cloud gate를 실패했다.
전체 합산은 `6.019ms`/`6.708ms`지만 합산값으로 실패를 상쇄하지 않는다.
로그: `build/build-policy-release.log`. 별도 GPU 테스트를 동시에 실행하지 않았으나
다른 구성의 C++ 빌드와 일부 시간이 겹쳤으므로 고립된 성능 원인 비교로 해석하지 않는다.
옵션 OFF만으로 성능 통과를 보장하지 않으며 기존 결과는 아래에 보존한다.

| 날짜 | GPU/driver | Cloud p95 | Frame p95 | 결과 |
|---|---|---:|---:|---|
| 2026-09-04 | NVIDIA GeForce RTX 4080 SUPER / 32.0.15.9186 | `6.036ms` | `6.866ms` | 최종 Weather CS·b10 적용 후 1,440표본 자동 gate 통과, 2026-09-05 사용자 화면 승인 |
| 2026-09-01 | NVIDIA GeForce RTX 4080 SUPER / 32.0.15.9186 | `5.153ms` | `5.984ms` | 3회 반복 최악값, 자동 gate 통과, 사용자 화면 승인 전 |

2026-09-04 최종 실행의 가장 무거운 case는 `Meadow Broken Clouds / InsideLayer`였고 Cloud p95 `8.324ms`, Frame p95 `9.299ms`로 case별 gate를 통과했다. Weather 생성 smoke는 실제 generator 변경을 정확히 한 generation으로 기록했고 CPU 기준과 GPU RGBA8 결과의 최대 차이는 채널별 1 LSB였다. 2 LSB 이상 차이는 자동 실패다.

한 번의 실행마다 유효 표본은 12 case × 120개 = 1,440개다. 2026-09-01 수치는 3회 반복 중 최악값이며 가장 무거운 case는 `Meadow Broken Clouds / InsideLayer`로 Cloud p95 `8.888ms`, Frame p95 `9.788ms`였다. GPU timestamp는 실행 환경과 온도에 따라 달라질 수 있으므로 이후 하드웨어에서는 같은 명령으로 다시 측정한다.
## Stage 15 방향광 개선 시작 기준 (2026-09-14)

00 baseline은 조명/밀도 수식을 수정하기 전의 별도 시작 기준이다.
OFF Release, RTX 4080 SUPER / driver 32.0.15.9186, 1920×1080,
VSync/UI/캡처 Off, case별 예열 60·표본 120, 기존 HighPerformance 12개 case를 직렬 측정했다.
첫 측정과 빌드가 일부 겹쳐 빌드 종료 후 단독으로 다시 측정한 아래 결과를 채택했다.

| Concept | Camera | Cloud p95 ms | Frame p95 ms |
|---|---|---:|---:|
| Urban | F5 HeroDepth | 0.986 | 1.364 |
| Urban | F6 GroundHorizon | 2.221 | 2.588 |
| Urban | F7 InsideLayer | 1.122 | 1.497 |
| Urban | F8 AboveLayer | 1.303 | 1.662 |
| Meadow | F5 | 4.229 | 4.825 |
| Meadow | F6 | 4.428 | 5.044 |
| Meadow | F7 | 8.132 | 8.658 |
| Meadow | F8 | 3.265 | 3.808 |
| Snow | F5 | 2.528 | 3.438 |
| Snow | F6 | 3.732 | 4.439 |
| Snow | F7 | 2.924 | 3.807 |
| Snow | F8 | 2.170 | 2.894 |

모든 case가 Cloud≤10ms, Frame≤16.67ms를 만족했고 시작 기준 실패 case는 없다.
원시 로그는 로컬 `captures/stage15-directional-lighting/00-baseline/validation/serial-performance.log`에 보존한다.
baseline 이미지 촬영의 readback 시간은 이 측정에 포함하지 않았다.
## 방향광 01 진단 추가 후 회귀 (2026-09-14)

동일 OFF Release/1080p/RTX 4080 SUPER/driver 32.0.15.9186에서 전체 CTest를 직렬 실행했다.
HighPerformance 12개 case(60 예열/120 표본)는 모두 Cloud≤10ms, Frame≤16.67ms를 만족했다.
최대 case p95는 Cloud 8.395ms, Frame 9.014ms(Meadow InsideLayer)다.
00 최대 8.132/8.658ms와 실행 간 변동이 있어 이를 개선 수치로 해석하지 않는다.
새 진단의 readback/통계 비용은 별도 테스트이며 위 성능 구간에 포함하지 않는다.
원시 결과: 로컬 `captures/stage15-directional-lighting/01-diagnostics/release-all-tests.log`.

### 방향광 02 후보 성능

기존 12 case, 60 frame 예열/120 GPU raw 표본, 1920×1080 OFF Release/VSync·UI·캡처 Off를 유지한다.
`--high-performance-test --density-shaping-035` 또는 `--density-shaping-070`은 테스트 중에만
각 Concept의 Formation 밀도 강도를 덮어쓴다. 일반 실행에는 적용되지 않으며 Custom을 저장하지 않는다.
인자가 없으면 강도 0을 측정한다. 각 case Cloud p95≤10ms, Frame p95≤16.67ms를 요구한다.

### 03 Base 굴곡 검사

`ctest --test-dir build -C Release -R "BaseOctaves|Stage13NoiseVolumeMath|Stage15PresetMath" --output-on-failure -j 1`.
Debug에서도 BaseOctaves/NoiseLabSmoke를 실행한다. 03 진단은 이미지를 파일로 저장하지 않는다.
12 case 성능은 `--high-performance-test --base-octaves-125` 및 `--base-octaves-150`.
밀도 override 인자가 없으면 승인된 Concept 기본값을 유지한다(Urban .70, Meadow/Snow 0).

### 04 근거리 Detail 검증

`ctest --test-dir build -C Release -R NearDetailShadows --output-on-failure`는 Urban·세 Type/F5·F7에서
Near/Far GPU 배열의 하단·중간·상단 직전 slice를 읽어 강도0/.5/1/0의 finite·tau 비증가,
Far 불변·0 복원·Detail Off·Type 유지·Concept 초기화·저고도 cone 불변을 검사한다. 모든 texel/slice의 증명은 아니다.
CPU Stage12ShadowMath는 4/6/8km 기준값과 전환 단조성·연속성 및 sanitize/offset을 검사한다.
`--high-performance-test --near-detail-050`과 `--high-performance-test --near-detail-100`으로
기존 12 case/60 warm/120 samples를 직렬 실행한다. OFF Release 1920×1080 UI/VSync/캡처 Off 유지.
03 승인 Base 1.50배, Urban .70/Meadow·Snow 기존 밀도 기본값을 사용한다. 비교 강도는 로그에 기록한다.
04 cache 불변/복원은 T=exp(-tau) MAE≤1e-5/max≤1e-3 허용오차 기준이다. 입력 고정·강도0 반복에서도 미세 차이가 있어 bit-exact를 주장하지 않는다. 세부 실패 이력은 stage15-directional-cloud-lighting 변경 기록을 따른다.

## 05-B 후보 성능 (2026-09-15)

OFF Release,1920×1080,VSync/UI/캡처Off,기존12case각60프레임예열/120 raw표본 직렬.
RTX4080 SUPER,driver32.0.15.9186. 형상/조명은각Concept의승인된기본값이다.

| 후보 | 캐시 메모리 | 12case 중 Cloud p95 최대 | Frame p95 최대 | 판정 |
|---|---:|---:|---:|---|
| 투영+250m,512²×80/40 |120MiB|9.075ms|10.222ms|12/12예산 통과|
| 위 수정+512²×159/79 |238MiB|9.310ms|10.653ms|12/12예산 통과|

기준은각case Cloud≤10ms/Frame≤16.67ms다. 위값은1440표본을합친전체p95와구분한다.
전체소요시간/50m·25m참조실행시간은게임프레임성능이아니다.
로그: captures/stage15-directional-lighting/05-banding/release-full-46.log.
고도3.1/5/10의오류강조진단은이12case와동일한장면이아니다. 추가저고도/이동화면의체감성능은사용자검증에남긴다.
높이159/79는미채택비교인자 --shadow-height-2x 에서만사용한다. 06의최종승인조합은값확정후재측정한다.

### 05-C 전환 구간 성능 (2026-09-15)

Release OFF, 1920×1080, UI/VSync/캡처 Off, 60프레임 예열/120표본 직렬 측정. Near80/Far40 유지. F5/F7 × 태양 고도2.5/3.5/4.5/5.5의 8case에서 최대 Cloud p95 4.75648ms, Frame p95 6.28531ms로 예산을 통과했다. 3~5도에서 cone/cache를 함께 계산하므로 추가 비용이 있다. 이 검사는 기존12case를 대체하지 않는다. 원본 로그는 로컬 captures/stage15-directional-lighting/05-transition/review-test.log에 보관한다.

### 06 대표 성능 (2026-09-15)

Release OFF 1920×1080, UI/VSync/캡처 Off, 직렬60프레임 예열/120 raw 표본. Rim cap8/intensity4/depth2의 최대 후보, Urban,80/40, F5/F7 × 고도3.5/18/70의6case를 검사했다.

| 카메라 | 태양 고도 | Cloud p95 ms | Frame p95 ms |
|---|---:|---:|---:|
| HeroDepth | 3.5 | 2.73101 | 4.31206 |
| HeroDepth | 18 | 2.31424 | 2.81907 |
| HeroDepth | 70 | 1.44896 | 2.26099 |
| InsideLayer | 3.5 | 3.51334 | 5.83066 |
| InsideLayer | 18 | 3.02797 | 3.55328 |
| InsideLayer | 70 | 2.2825 | 2.95731 |

각case Cloud p95≤10ms/Frame p95≤16.67ms 통과. 기존12case 전체 성능은07 승인 단계에서 다시 수행하며 이6case가 이를 대체하지 않는다. 기록: captures/stage15-directional-lighting/06-rim/release-rim-pass.log. 사진 캡처/수치 probe/셰이더 컴파일 시간은 위 GPU 측정에 포함하지 않는다.

### 06 사용자 승인 캐시 규격 — 2026-09-16

현재 일반 Deep Cache는Near512²×80/Far512²×79,R32_FLOAT 배열159MiB다. 이전05의80/40 기록은과거규격이다. C++초기값·sanitize·자원생성·HLSL slice입력·디버그최대index78이일치한다. ShadowCB160B/offset/바인딩은불변이며선형tau보간과3~5도전환을유지한다.159/79는검증전용이다.
2026-09-18 독립 슬롯: 고정 High·512/80+79 cache 유지. 전체 회귀의 HighPerformance/ShadowHeightPerformance 및 최종 튜닝 후 HighPerformance 통과. 새16조합 촬영은8프레임 예열의 외관 비교이며16조합 전체 steady-state 성능 게이트 측정으로 해석하지 않는다.

### 시선 표본 지터 — 2026-09-25

View march의 표본 위치만 구간 중점에서 픽셀별 IGN 지터로 바꿨다(step 수·적분 길이 불변). Release 1920×1080, RTX 4080 SUPER, 드라이버 32.0.15.9186에서 HighPerformance 12case·1440표본 통과: 전체 Cloud p95 4.564ms, Frame p95 5.089ms, 최대 case Meadow CloudOverview Cloud 6.948ms/Frame 7.498ms. 상세는 [시선 표본 지터](changes/stage15-view-sample-jitter.md).