# 성능 측정 기준

이 문서는 이후 단계에서 최적화 전후 결과를 같은 조건으로 비교하기 위한 공통 기준이다.
화면 우측 상단 성능 오버레이는 `FrameProfiler`가 모은 CPU/GPU 시간을 보여 준다.

Stage 15 구현 중 발견한 Stage 14 호환 경로 회귀의 원인, 실패한 실험과 수정별
전후 수치는 [Stage 15 성능 회귀 조사와 해결 기록](changes/STAGE15_PERFORMANCE_REGRESSION.md)에
시간순으로 보존한다.
자동 수치와 사용자가 화면에서 확인할 정상·실패 징후를 함께 대조할 때는
[Stage 15 프리셋 사용자 검증 가이드](STAGE15_PRESET_VALIDATION_GUIDE.md)의 10분 승인과 상세
진단 카드를 사용한다.

## 오버레이 항목

| 항목 | 시간 범위와 의미 |
|---|---|
| `Frame #` | `Renderer::Render`가 실행된 누적 프레임 번호 |
| `FPS` | 평활화된 CPU Frame 시간의 `1000 / ms` 값 |
| `CPU Frame` | `Renderer::Render` 시작부터 `Present` 반환까지. VSync 대기 포함 |
| `GPU Frame` | 진단 장면 시작부터 ImGui draw 종료까지. `Present` 제외 |
| `GPU Shadow Cache` | 매 프레임 Near/Far `R32_FLOAT` 광학 깊이 배열을 생성하는 두 compute dispatch |
| `Cloud Raymarch` | 선택 해상도에서 구름 scattering/T/depth를 MRT에 적분 |
| `Spatial/Temporal Resolve` | Full-resolution 공간 복원, 선택적 history 재투영·clip과 장면 합성 |
| `GPU Cloud Total` (`GPU Cloud`) | Shadow Cache+Raymarch+Resolve 세 구간의 합 |
| `View` | `maxViewSteps @ stepSize(m)` |
| `Light` | `maxLightSteps @ lightStepSize(m)` |
| `VSync` | 현재 `Present(1, 0)` 또는 `Present(0, 0)` 경로 |

CPU Frame과 GPU Frame은 측정 범위가 다르므로 서로 같은 값일 필요가 없다. 특히 VSync On에서는
CPU Frame이 모니터 주사율 대기 시간을 포함한다. View/Light Ray 비용을 비교할 때는 FPS보다
`GPU Cloud Total ms`를 우선 사용한다.

성능 오버레이는 F4 `Performance Overlay`로 표시만 끌 수 있으며 계측 자체는 계속된다. 좌측
`Compact Stage 15 Overlay`와 독립된 상태이고 일반 실행은 둘 다 On이다. 포트폴리오 화면은 두
overlay와 F1~F4를 모두 숨겨 찍는다. 자동 성능 명령은 표시 체크 상태와 무관하게 Automated Render
Mode에서 ImGui frame, panel, overlay, preview와 수동 네 PNG export를 렌더 프레임에서 생략한다.
자동 quality/performance는 Temporal 통계 관찰용 네 번째 MRT를 바인딩하지 않고 mip 생성과
readback도 생략한다. 이 통계는 화면·history 출력이 아니라 디버그 관찰 자료다. 단, preset smoke는
schema 37 계약만 확인하려고 프레임 밖에서 metadata-only JSON export를 한 번 호출하며 PNG는
만들지 않는다.

Stage 15A의 F4 출력 진단은 성능 수치와 별도로 Physical Client, SwapChain,
Viewport, Scene Color, Full Scene Depth, Cloud RT, Temporal History width/height와 DPI/scale,
PMv2/Native 상태를 표시한다. `1920×1080`이라는 성능 결과는 Physical/Swap/Scene/History가
모두 1920×1080이고, Medium은 960×540, High는 1920×1080 Cloud RT임을 함께 증명해야 한다.

## 비동기 GPU 계측 방식

`FrameProfiler`는 8개 슬롯의 D3D11 timestamp query ring을 사용한다. 각 슬롯은 timestamp
disjoint와 GPU Frame 시작·종료, Atmosphere LUT 종료, Cloud 시작, Shadow Cache 종료, Opaque 종료,
Raymarch 종료, Resolve(Cloud) 종료와 Tone Map 종료 timestamp를 가진다. 현재 프레임을 기다리지 않고
`D3D11_ASYNC_GETDATA_DONOTFLUSH`로 완료된 과거 슬롯만 읽는다. 8개 슬롯이
모두 사용 중이면 해당 프레임의 GPU 측정을 생략하고 렌더링을 계속한다.

query가 아직 준비되지 않았으면 마지막 유효값 또는 `warming up`을 표시한다. disjoint,
리사이즈에 따른 세대 변경, 잘못된 timestamp 순서와 비정상 값은 버린다. CPU/GPU 표시값에는
`alpha=0.1` EMA를 적용하지만 원본 표본은 profiler 내부에서 검증한 뒤 누적한다.

## 고정 비교 절차

1. Release 빌드를 사용한다.
2. Physical Client/SwapChain/Scene/Cloud/History 크기, 카메라, Weather, Detail,
   태양·Phase·Environment 설정을 동일하게 맞춘다.
3. F1 Noise 창의 Animation에서 시간을 정지한다.
4. 같은 F1 창의 Performance에서 VSync를 Off로 설정한다.
5. 설정 변경 후 최소 2초 동안 워밍업한다.
6. `GPU Shadow Cache`, `Cloud Raymarch`, `Spatial/Temporal Resolve`, `GPU Cloud Total`을 기록하고 같은 조건에서 비교한다.
7. View/Light Step을 바꿀 때 한 번에 한 파라미터만 변경한다.

예를 들어 Light Step 8/16/32의 비용을 비교할 때 카메라와 나머지 설정을 고정한다. FPS는
운영체제·Present·다른 앱의 영향을 함께 받으므로 보조 지표로만 사용한다.
단계 8 Off/Balanced 또는 Multiple Octaves 0~4를 비교할 때도 한 번에 해당 설정만 바꾸고
`Shift+P`의 Light Sample 출력이 동일한지 함께 확인한다.

## 현재 범위와 보관된 측정

단계 13 대규모 평면층은 2026-08-17 사용자 승인을 받았다. View 512와 Light 80은 단계 9의
승인 기준이며 같은 실행 파일의 Reference/Optimized 경로로 화질과 비용을 비교한다.

이전 단계 9의 AABB 벤치마크와 단계 13 평면 구름층 측정은 `captures/performance/`와
각 보관 브랜치에 역사 자료로 남아 있다. 해당 결과는 현재 성능 게이트가 아니며 새 계획에서
장면 규모·카메라·품질 설정을 확정하기 전에는 최적화 합격 판정에 재사용하지 않는다.

## 포트폴리오 1080p 합격 기준

단계 13 승인 뒤 현재 개발 PC를 기준 장치로 삼는다. 실행 시 DXGI 어댑터 이름과 드라이버,
해상도, 품질 프리셋, seed와 카메라를 결과 JSON에 기록한다.

| 항목 | High 합격 기준 |
|---|---:|
| 해상도 | 1920×1080 |
| High Cloud RT | 1920×1080 Full |
| VSync | Off |
| 워밍업 | 120 frames |
| 기록 | 600 frames |
| GPU Frame p95 | 16.67ms 이하 |
| GPU Cloud p95 | 10.00ms 이하 |
| Temporal Resolve p95 | 2.00ms 이하 |
| 최적화 전후 SSIM | 0.99 이상 |
| 정규화 RMSE | 0.01 이하 |

고정 장면은 `GroundZenith`, `GroundHorizon`, `InsideLayer`, `AboveLayer`,
`FlightTraversal`, `DepthOccluded`, `CumulusHorizonStress`다. 다른 앱의 동시 GPU 부하나
timestamp disjoint가 감지되면 측정을 무효로 표시하며 합격 자료로 사용하지 않는다.

### Stage 11 source validation 회귀 측정 (2026-08-22)

Full D32 plane source gate와 3×3 Cloud Depth 범위 수정은 Release `Stratus`, Stable 4-Phase,
1920×1080 Scene/960×540 Cloud Data, Joint4, camera/time 고정, wind 0에서 측정했다. F5 HeroDepth와
F8 AboveLayer yaw +3° 각각 120프레임 warmup 뒤 서로 다른 GPU timestamp 600개를 사용했다.

| 장면·구간 | p95 | 기준 | 결과 |
|---|---:|---:|---|
| F5 Spatial/Temporal Resolve | 1.255424ms | 2.00ms 이하 | 통과 |
| F5 GPU Cloud Total | 3.897344ms | 10.00ms 이하 | 통과 |
| F8 Spatial/Temporal Resolve | 1.314816ms | 2.00ms 이하 | 통과 |
| F8 GPU Cloud Total | 2.793472ms | 10.00ms 이하 | 통과 |

F8 기본·yaw ±3°·pitch ±2°와 세 필터의 평면 내부 Composite range 평균/P99는
`0.000012/0.000250`이었다. Current Source non-green, invalid horizontal run, Weight phase P99,
Diff blue, Cloud Depth yellow run과 Temporal Off hole run은 모두 0이었다. 이 값은 F5/F8 회귀
gate이며 일곱 장면 최종 포트폴리오 측정이나 Full reference SSIM/RMSE/T 승인을 대신하지 않는다.
## 단계 13-2 상사 진단 해상도

`Stage13SimilarityGpu`의 320×180 float offscreen 렌더는 배율별 수치 비교를 위한 고정
테스트 조건이며 성능 벤치마크가 아니다. VSync와 Noise Lab을 끄고 PNG를 생성하지 않는다.
Lighting과 Composite 시간도 합격 기준으로 사용하지 않으며, 성능 평가는 기존 고정 창·GPU
Cloud ms 절차를 그대로 따른다.

## 단계 13-3 Open World smoke

`Stage13OpenWorldSmoke`는 96×54 숨김 float 타깃에서 View `100m/512`, Light
`250m/80` 실제 budget을 실행한다. Ground Zenith/Horizon/Inside/Above의 Entry,
Segment, Actual Step, Transmittance와 Composite가 유한하고 구분되는지만 검사하며 PNG를
만들거나 성능 합격을 판정하지 않는다.

## 단계 13-4 Texture3D 비용

Base `128³ RGBA8`는 `8,388,608B`, Detail `32³ RGBA8`는 `131,072B`로 총
`8,519,680B`다. SRV/UAV와 테스트 readback staging은 생성 시에만 추가되며 정상 프레임에는
두 SRV를 `t3/t4`에서 샘플링한다. 현재 구현은 mip을 만들지 않는다.

`Stage13NoiseVolumeSmoke`는 compute 생성과 CPU readback을 포함한 시간을
`[NOISE3D][GPU] GENERATION_MS=... REPORT`로만 남긴다. 이 수치는 GPU·드라이버·Debug/Release
컴파일에 따라 달라지므로 gate가 아니다. 구형 회귀 smoke는 Texture3D를 쓰지 않아 CS 컴파일과
볼륨 생성을 건너뛰며, 일반 실행과 Open World/NoiseVolume smoke만 생성한다. 런타임의 정식
cache와 거리 LOD·mip 성능 판정은 각각 후속 단계에서 다룬다.

`Stage13WeatherShapeGpu`의 320×180 float 세로 단면은 Local Thickness/Typed Shape
Profile/Effective Coverage/Base Support의 CPU/HLSL 일치를 검사하는 정확성 테스트다.
Periodic Weather는 상단 span·표준편차·천장 도달률을, Channel Debug의 Mixed/Cumulus
두 측면은 중간 support가 바닥보다 10%, 상단보다 15% 이상 넓은지를 gate로 사용한다.
같은 테스트가 카메라를 `windSpeed×time`만큼 이동한 프레임을 원본과 비교해
Weather/Base/Detail의 공통 강체 이동도 검사한다. Legacy 전용 속도 변경과 Bulk 정지
검사는 각각 결과 불변을 요구한다.
프레임 시간은 성능 합격에 반영하지 않고 PNG도 생성하지 않는다.

F5~F8 플레이어 시야 변경은 품질 관찰용이다. `Stage13CameraControlMath`는 CPU 수학만
검사하고 성능 수치를 기록하지 않는다.

## 단계 13-4D 단일 씬 smoke

`Stage13UnifiedSceneSmoke`는 320×180 숨김 타깃에서 F5~F8과 숫자 0~9의 float 출력을
검사한다. 모든 출력은 finite이고 주요 기준 출력은 non-black이어야 하며 카메라별 Composite와
파이프라인 출력 hash가 구분되어야 한다. Compare 1~5 중 지면·건물·domain·이동 기준은
불변이고 Full Open World가 Dense Mixed+13-5 최종 입력을 복원해야 한다. 전체 snapshot schema 31 파싱과 제거 필드
부재도 함께 검사한다. 실행 시간은 정확성 검사용이며 성능 gate가 아니다.

## 단계 13-4E 외형·Custom smoke

`CloudAppearanceTests`는 기본 seed에서 Dense/Stratus/Cumulus Weather R non-zero/core를
각각 `79.62/49.11`, `87.31/56.60`, `73.82/42.07%`로 고정한다. Weather R=0,
vertical profile=0, layer 밖 밀도 0과 profile의 단일 곱, 정확한 preset 조합, seed/wind
불변, render-only Compare time 0을 검사한다. schema 29 Custom round-trip, 누락·구버전·
손상·범위 밖 거부와 원자 저장 실패도 CPU에서 검사한다.

`Stage13UnifiedSceneSmoke`는 F5/F6에서 Dense/Stratus/Cumulus의 Composite, Weather,
Base/Final Density, View τ, Light T가 finite·non-black이고 각 preset hash가 서로 다른지
확인한다. camera/domain/light/environment/LOD/Weather seed와 wind는 전환 전후 동일해야 한다.
이 검사는 화면 미학이나 성능 합격을 대신하지 않는다.

## Stage 14 대기·HDR 계측과 게이트

프로파일러는 `Atmosphere LUT`, `Shadow Cache`, `Opaque Scene`, `Cloud Raymarch`, `Resolve`,
`Tone Map`, `GPU Frame`을 별도 timestamp 구간으로 기록한다. 정적 상태에서 hash가 같으면
Atmosphere 구간에는 LUT dispatch가 없으며, 전체 LUT 최초/정적 rebuild 시간은 별도 기록하고
프레임 성능 gate에는 포함하지 않는다. 시간 재생·카메라 이동 중 실제 갱신 구간의 p95만
Atmosphere `2ms` 이하를 요구한다.

정식 측정은 Release 1920×1080, VSync/UI/preview Off, 120 warmup 뒤 원시 timestamp 600개를
Stage 12와 같은 일곱 장면에서 수집한다. GPU Frame p95 `16.67ms`, GPU Cloud p95 `10ms`,
Stage 12 동등 장면 대비 `+5%` 이내가 gate다. Full Direct, Full split, 50% spatial, Temporal
display 비교는 `MAE≤0.01`, `P99≤0.03`을 함께 만족해야 한다.

정식 명령은 Release 실행 파일의 `--stage14-performance-test`다. 과거 실행의 부하 차이를
회귀로 오인하지 않도록 같은 프로세스에서 `ManualReference + LegacyShoulder` Stage 12 호환
경로를 먼저 측정하고, 같은 장면·Balanced·Full·Temporal Off·Balanced512 조건에서
`Physical EarthClear + ACES`를 측정한다. `GPU Cloud Total`은 두 경로 모두
Shadow Cache+Raymarch+Resolve 세 구간의 합이다. 결과는 로컬
`captures/stage14/performance.csv`와 `performance.json`에 기록한다.

2026-08-28 RTX 4080 SUPER/driver `32.0.15.9186` 측정에서 물리 경로의 일곱 장면
GPU Cloud p95 평균은 `7.37616ms`, 같은 실행의 Stage 12 호환 경로는 `7.07040ms`로
비율 `1.04325`(`+4.325%`)를 기록해 회귀 gate를 통과했다. 물리 경로의 장면별 GPU Cloud
p95는 `5.42720~9.34810ms`, GPU Frame p95는 `5.51219~9.47302ms`로 모든 절대 gate도
통과했다. 정적 장면 Atmosphere p95는 `0ms`였고, 강제 SunPlayback/CameraMotion 갱신 p95는
각각 `0.019456/0.008192ms`로 `2ms` gate를 통과했다. D3D11 debug layer 오류는 없었다.

구름 View step마다 반복하던 태양 Transmittance·Sky Irradiance·Ground 입사광 LUT 조회는
View ray마다 구름층 대표 고도에서 한 번 준비하는 `CloudLightingContext`로 옮겼다. 표본별
밀도·높이 가중치·AO·다중 산란 적분은 그대로 유지하며, 이 최적화 뒤 위 성능값과 Stage 14
LUT 수치 smoke를 다시 통과했다.

현재 `Stage14AtmosphereSmoke`는 여섯 LUT의 모든 texel이 finite/non-negative인지 읽고,
Transmittance 전체 RGB를 CPU 40-step 기준과 비교해 `MAE=0.000189`, `P99=0.000485`로
`0.01/0.03` gate를 통과했다. 또한 최초 여섯 LUT 생성, 정적 dispatch 생략, Exposure 변경 시
LUT 유지, Ground 변경 시 Multi 이후, 태양 변경 시 Sky View/Aerial, 카메라 회전 시 Aerial만
generation이 증가하는지를 D3D11 debug layer와 함께 검사해 통과했다. 이 smoke는 Sky
radiance의 독립 CPU 기준 비교를 대신하지 않는다.

## Stage 12 Cloud Shadow 후보와 게이트

Deep Cache는 화면 해상도와 별도로 `24km Near × 80 slice`와 `128km Far × 40 slice`를
매 프레임 갱신한다. Fast256은 30MiB, Balanced512는 120MiB의 `R32_FLOAT` 배열을 사용한다.
1920×1080 Full과 50% Axis는 같은 월드 cache를 공유하므로 창 크기나 Cloud Data 크기를
cache 범위 계산에 사용하지 않는다.

정식 후보 측정은 Direct/Fast256/Balanced512를 Full과 50%에서 각각 비교한다. VSync/UI/preview를
끄고 120프레임 warmup 뒤 서로 다른 원시 timestamp 600개를 일곱 고정 장면에서 수집한다.
모든 후보는 GPU Cloud Total p95 10ms 이하, Full은 Direct 대비 15% 이상 개선, 50%는 측정
오차를 포함해 3% 이상 회귀하지 않아야 한다. Light/Surface `T MAE≤0.01`, `P99≤0.03`,
Composite `SSIM≥0.99`, normalized `RMSE≤0.01`, seam `T P99≤0.03`도 함께 적용한다.
두 해상도에서 모두 통과한 가장 높은 후보만 기본값으로 승격한다. 현재 구현 시작값은 Stage 11
화면을 보존하는 DirectReference이며, 96×54 Fast256 D3D smoke의 Light T는
`MAE=0.001635`, `P99=0.029349`였다. Full/50% 및 resize 전후 cache identity와 실제
Surface T도 통과했다. 이 값은 정식 1080p 성능·사용자 화면 승인을 대신하지 않는다.
2026-08-24 재검증에서는 raw Near/Far bottom-slice preview가 모두 0이 아닌 공간 변화를
가지는지도 smoke gate에 추가했고 두 cache가 모두 통과했다. Preview exposure는 진단 표시만
바꾸며 cache 생성·조회 비용에는 포함되지 않는다.

## 단계 13-5 km 광학·조명 smoke

`Stage13OpticsLightingSmoke`는 단일 씬 F6 Ground Horizon 고정 장면에서 Light
`62.5m/320`을 fine reference로 만들고 기본 `250m/80`과 이전 품질 `125m/160`의 Light
Transmittance, 누적 직접광과 Composite를 float readback 비교한다. Dense/Stratus/Cumulus
모두 기본 후보의 MAE와 P99 `0.01/0.03` 이하를 gate로 사용한다. 새 기본의 Light T/Direct/
Composite MAE는 Dense `0.00007532/0.00001377/0.00002579`, Stratus
`0.00001735/0.00000548/0.00000912`, Cumulus
`0.00008399/0.00000871/0.00001599`로 통과했다.

Light 전용 scalar density 경로는 Weather/높이/profile 공백에서 Base fetch를 생략하고,
`T≤0.0001`에서 실제 Light 반복을 종료한다. `Total Light Samples`는 예정 수가 아니라 실제
실행 수다. View early exit와 coarse march는 여전히 단계 9 범위다.

2026-08-17 Release 실측은 첨부 화면과 같은 `1920×925` client, VSync Off, 180-frame
워밍업, 고정 time에서 수행했다. GPU Cloud EMA는 Stratus F5/F6 `7.8925/8.2290ms`,
Cumulus F5/F6 `13.9028/14.7525ms`로 이번 변경의 `16.67ms` 게이트를 모두 통과했다.
이는 단계 13-5 Light 비용 변경의 한정 게이트이며, 단계 9의 1080p p95 기준을 대체하지 않는다.

외곽광 보완 뒤에는 같은 `1920×925`, Cumulus F6, Noon/Balanced, VSync Off 조건에서
180-frame 워밍업 뒤 원시 timestamp 600개를 새로 수집했다. GPU Cloud 평균은
`13.867684ms`, p95는 `15.785984ms`다. 같은 수집기를 변경 전 HEAD 셰이더에 적용한
baseline 평균/p95는 `13.688282/15.639552ms`이며, 새 p95 증가는 약 `0.94%`다.
평균/p95 모두 이전 대비 +5% 이내이고 p95 16.67ms도 통과했다. 과거 UI의
`14.7525ms`는 EMA이므로 새 원시 p95와 직접 비교하지 않는다.

같은 smoke는 Detail LOD Off와 32~48km On의 factor 출력이 실제로 달라지고, compute 생성
readback에서 계산한 weighted neutral mean이 유한한 `[0,1]`인지 검사한다. 기본 mean은
`0.44994098`이다. 이 테스트는 정확성 비교이며 GPU p95 성능 gate가 아니다. Light 표본 증가와
48km 밖 Detail fetch 생략의 최종 성능 효과는 단계 9 기준 측정에서 별도로 판정한다.

사용자 방향 검증을 회귀로 고정하기 위해 Low East/West의 View τ, 누적 Direct와 분리된
Silver Lining Contribution을 비교한다. 태양 노출 기반 Portfolio Hero에서 View τ MAE는 `0`,
Direct/Silver MAE는 `0.03554698/0.03117338`로 태양 방향이 밀도 광학에는 영향을 주지 않고
조명만 바꾸는 계약을 통과했다. Composite 최대값은 East/West `0.96254736/0.82435226`,
`RGB peak≥0.98` 비율은 `0`이다. Shaped Sun Visibility 0.5를 기준으로 분리한 Silver 평균은
노출 외곽/내부 `0.12536342/0.06037134`, Ambient Visibility 평균은 `0.46502053`으로
외곽 선택성과 유한한 내부 fill gate를 통과했다.

## 단계 9 Reference/후보 측정

`Stage9OptimizationSmoke`는 96×54 float 타깃에서 Dense/Stratus/Cumulus의
Approved Reference View와 `62.5m/320` Fine Light를 만든 뒤 Balanced/Conservative와
Final Density, View τ, Light T를 비교한다. SSIM/RMSE와 Light MAE/P99를 보고하고 모든
픽셀이 finite인지 gate로 검사한다. Dense와 Stratus Reference Final Density가 실제로 다른지도
회귀한다. 측정이 끝나면 실행 상태를 2026-08-19 승인 기본값인 Balanced로 되돌린다.

정식 성능 명령은 Release 실행 파일의 `--stage9-performance-test`다. 1920×1080,
VSync/UI/preview Off, time 0, 120-frame 워밍업 뒤 장면별 원시 GPU timestamp 600개를 모은다.
장면은 Dense Zenith/Horizon, Stratus Horizon, Cumulus Horizon/Inside, Above Layer,
Depth Occluded 일곱 개다. Balanced/Conservative/Approved Reference를 같은 실행
파일에서 순차 측정하며 결과는
`captures/stage9/performance.csv`와 `.json`에
DXGI adapter, driver version, 평균/p50/p95와 gate를 기록한다.

Fine Reference는 `50m×1024 View`와 `62.5m×320 Light`를 함께 쓰는 오프라인 화질 기준이라
1080p Composite 600프레임 실시간 후보에서는 제외한다. Stage9OptimizationSmoke의
Light T 화질 reference로만 사용한다.

후보 gate는 GPU Cloud p95 `10ms` 이하, Cumulus Horizon이 Approved Reference보다 15% 이상
빠름, 나머지 장면 p95 회귀 3% 이하를 동시에 요구한다. 이 자동 gate와 사용자 렌더 검증을
모두 통과하기 전에는 가장 싼 후보를 시작 기본값으로 자동 승격하지 않는다. Balanced는 자동
gate와 2026-08-19 사용자 렌더 검증을 모두 통과해 단계 9 시작 기본값으로 승인됐다.

2026-08-17 RTX 4080 SUPER/드라이버 `32.0.15.9186` 측정에서 자동 합격 Balanced
(`6탭/2°/원거리 77%`)의 2026-08-19 재측정 일곱 장면 p95는
`7.85/8.07/8.85/8.64/9.41/7.65/7.44ms`다. Cumulus Horizon은 Approved Reference
`18.68ms`에서 `8.64ms`로 약 `53.8%` 개선됐고,
나머지 장면도 Reference보다 빨라 3% 회귀 gate를 통과했다. 화질 측정의 Light T MAE/P99는
Dense `0.00142/0.02814`, Stratus `0.00042/0.01250`, Cumulus `0.00167/0.02899`이며
Final Density/View τ도 SSIM 0.99, RMSE 0.01 기준을 통과했다.

2026-08-19 사용자 검증에서 Fast와 Empty Search 4×는 400m 표본 alias로 구름에 규칙적인
등고선이 생겨 화질 탈락했다. 이전 자동 결과도 Fast View SSIM이 Dense/Stratus/Cumulus
`0.73/0.70/0.61`로 기준에 미달했다. 이후 측정 산출물과 합격 판정에는 Fast를 포함하지 않고
Balanced를 가장 왼쪽 실시간 후보로 사용한다.

2026-08-19 최종 승인 뒤 일반 실행과 Full Open World 복원은 Balanced로 시작한다. Approved
Reference와 Fine Reference는 F1의 Advanced Comparison에 남아 단계 10 이후에도 화질·비용
회귀 기준으로 사용한다.

승인 커밋 직전 같은 조건으로 다시 수집한 동결 측정에서 Balanced 일곱 장면 p95는
`9.89/9.04/9.65/9.78/9.41/9.02/7.89ms`, 최대 `9.89ms`로 10ms gate를 유지했다.
Cumulus Horizon은 같은 실행의 Reference `21.01ms` 대비 `9.78ms`로 약 `53.5%` 빨랐다.
앞의 `8.64ms`와 최대 `9.41ms`는 사용자 승인 시점 기록이며, 두 측정 모두 같은 어댑터·드라이버와
품질/성능 gate를 통과했다.

## 단계 10 저해상도·공간 업샘플링 측정

단계 10 기준은 `Stage9 Balanced + 1920×1080 Full`이다. 불투명 장면은 항상 Full이고 활성
구름 Raymarch 후보는 축 50/100%다. Full도 저해상도 후보와 같은 RGBA16F/RG32F MRT와
resolve를 지나며 1:1 최근접으로 복원해 파이프라인 분리 자체의 차이를 잰다.

`Stage10UpsamplingSmoke`는 96×54에서 Full 직접 합성과 Full split 경로를 비교한다. 최초 네
해상도 × 네 필터 및 네 업샘플 디버그 출력의 finite 결과와 D3D11 오류를 검사했고, 2026-08-19 첫 구현
결과는 Full RGB MAE `0.000093`, 서로 다른 후보 hash 13개, Half 실제 타깃 `48×27`로 통과했다.
활성 후보 축소 뒤 smoke는 2개 해상도 × 3개 필터에서 예상한 서로 다른 hash 4개와 같은
Full MAE·Half 타깃을 다시 통과했다. 이는 기능 회귀이지 1080p 화질·성능 승인이 아니다.

2026-08-19 사용자 F6 정지 화면 비교에서 Full/50/67/75%의 오버레이 Total은 각각
`9.964/6.623/7.055/8.165ms`였다. VSync On 단일 관찰값이므로 정식 성능값은 아니지만, 50%가
Full과 비슷하면서 67/75%보다 격자감이 적었다. 세 활성 필터의 가시적 차이도 크지 않아
`50% Axis + Nearest`를 잠정 최종 후보로 정했다. 67/75%와 Joint9는 활성 UI·자동 후보에서
제외하고 enum/schema 호환만 유지한다.

정식 후보 평가는 Full 대비 Composite `SSIM≥0.99`, 정규화 `RMSE≤0.01`, T `MAE≤0.01`,
`P99≤0.03`을 먼저 통과한 조합만 일곱 장면 600-sample 측정에 올린다. Cumulus Horizon
GPU Cloud Total p95가 단계 9 Balanced보다 20% 이상 개선되고 모든 장면 p95가 8ms 이하여야
한다. 시작 Resolution은 자동 1080p 측정이 끝날 때까지 Full이며, 50%+Nearest는 아직 성능
p95와 전체 장면 게이트 전이므로 시작 Resolution로 승격하지 않았다.

## 단계 15 최종 프리셋 측정

아래 수치는 자동 합격 계약이며 화면 모양의 최종 승인을 대신하지 않는다. 같은 상태를 화면에서
재현하고 이상 증상을 분류하는 절차는 [Stage 15 프리셋 사용자 검증 가이드](STAGE15_PRESET_VALIDATION_GUIDE.md)를
따른다.

Release의 `--stage15-performance-test`는 1920×1080, VSync/UI off, 고정 시간에서 네
Concept×Low/Medium/High×F5~F8의 48조합을 측정한다. 각 조합은 120 warmup 뒤 비동기 query의
`gpuSampleIndex`가 서로 다른 600개 표본만 채택한다. Frame/Cloud/Temporal Resolve p95 한도는
각각 `16.67/10/2ms`다. Low/Medium은 50% Joint4/Stable 4-Phase고 High는
Full/Nearest 1:1/Full Resolution Temporal이다. 따라서 기존처럼 세 품질이 같은 RT/Resolve라는
가정으로 High/Medium `Shadow+Raymarch` 180% 상대 gate를 적용하지 않는다. Low는 같은
50% 구조의 Medium보다 최소 3% 낮은 소유 작업 평균을 유지하고, High는 절대
`GPU Cloud p95 ≤ 10ms`, `GPU Frame p95 ≤ 16.67ms`를 통과해야 한다. High의 첫 descriptor는
100m/512며 이 절대 gate를 통과한 뒤에만 80m/768을 다음 후보로 측정한다.

Capture Still은 실시간 p95 품질이 아니다. exact Native 1920×1080, Full/T Off,
50m/1024, Cone8, distance/Detail LOD Off, Balanced512의 projection-jitter HDR sample 4개를
`R32G32B32A32_FLOAT`에 평균낸다. 자동 검증은 4/4 완료, finite 누적, 서로 다른
sample hash, 입력/시간 고정, 소유권에 따른 Native 복원을 검사한다. Reference는
FineReference/DirectReference를 쓰는 수치 비교용으로 Capture와 분리한다.

Stage 14 승인 회귀는 `captures/stage14/performance.json`의 같은 adapter/driver
`Stage14Physical/DenseHorizon` Cloud p95를 기준으로 Full/Balanced/Temporal Off/Balanced512를
다시 측정한다. 비율은 1.03 이하여야 한다. 출력은 `captures/stage15/performance.csv`와
`performance.json`이며 PNG를 만들지 않는다.

`--stage15-quality-test`는 Full/FineReference/DirectReference/Temporal Off를 기준으로 구름
산란 RGB의 luminance SSIM과 정규화 RMSE, resolved T의 MAE/P99를 계산한다. SSIM은 전체 readback의
평균·분산·공분산을 쓰는 단일 global SSIM이며 11×11 같은 local window SSIM은 아니다. HDR 정규화 범위는
`max(1.0, reference luminance P99)`다. Low gate는 `0.97/0.03/0.03/0.08`, Medium/High는
`0.99/0.01/0.01/0.03`이다. High의 Medium 대비 “not worse”는 전체 화면 정규화 RMSE와 T MAE
두 평균 오차에만 half-float/phase 수치 동등 범위 `1.1e-4`를 허용한다. SSIM과 T P99는 위 절대
gate로 검사하지만 이 High 상대 판정에는 넣지 않으며 JSON의 `notWorseMetrics`에 이 범위를
명시한다. Medium 4/8/16-frame의 Temporal 수렴은 아래 cloud-edge 집계 gate로 별도 판정한다.
각 Concept/Diagnostic/Quality
요청 뒤 실제 enum·Temporal·Shadow
상태를 확인하며, 요청한 Cloud target을 준비하지 못하면 Full direct 경로로 대체하지 않고 fixture를
즉시 실패시킨다. `resolvedScatteringAndTransmittanceReadbackPassed`는 scattering/T hash가 서로
다르고 T가 finite grayscale 0~1이며 둘 다 화면 Composite와 다른지도 별도 확인한다.
고정 거리 ring이나 국소 ghost처럼 작은 영역의 결함은 global SSIM만으로 놓칠 수 있으므로 사용자
가이드의 Detail LOD/Temporal Debug View 판정을 별도로 유지한다.

15A는 전체 화면 평균 외에 `cloudMask`와 `cloudEdgeMask`의 SSIM/RMSE/T MAE를 함께
기록한다. `cloudMask`는 reference opacity가 `0.01` 이상인 픽셀이다. `cloudEdgeMask`는
reference T 중앙차분 gradient가 `0.002` 이상이고 4-neighbor 중 하나의 opacity가 `0.01`
이상인 픽셀이다. 후보 결과로 mask를 다시 만들지 않으므로 Low/Medium/High가 같은 기준 픽셀을
비교한다. High의 Cloud RT가 Full이 아니면 즉시
실패하고, 같은 Concept/Camera에서 High cloud-edge RMSE는 Medium보다 의미 있게 낮아야
한다. 정지 Temporal fixture는 4/8/16 frame 결과와 history age/reset count를 함께 저장하며,
reset이 없는데 age가 증가하지 않으면 실패다. 4→16 cloud-edge RGB RMSE와 T MAE에는 `2e-6`
수치 동등 범위를 적용해 둘 다 악화되지 않고 적어도 하나가 그 범위보다 더 개선되어야 한다.

Joint4 화질 gate는 Accepted Tap Count debug ID 80을 읽어 순수 sky 영역의 평균 hard-valid
tap이 2 이상인지 검사한다. Geometry/Sky 경계의 hard rejection은 그대로 유지하며,
sky 전체가 투명 fallback로 떨어지는 수정은 합격하지 않는다. 최신 Debug smoke는 경계에서
2픽셀 이상 떨어진 sky 8,482픽셀의 평균 `4.000000` taps와 Base/Detail Texture3D sampler의
linear/wrap 계약을 통과했다. Full RT는 spatial resolve를 우회하므로 ID 80을 짙은 회색 N/A로 표시한다.

Release Stage10/11/15 targeted suite는 `8/8` 통과했다. Release 전체 CTest 첫 실행은 Stage 12
smoke의 최초 캡처가 이전 출력 크기를 이어받아 `53/54`였으며, 캡처 전에 `96×54`를 명시하도록
fixture를 고친 뒤 Stage 12 focused Debug/Release `2/2`가 `MAE=0.001635`, `P99=0.029349`로
다시 통과했다. 최종 Debug/Release 전체 CTest도 각각 `54/54`로 통과했다.

숨김 ID 79와 Temporal Off spatial 출력 의미를 바로잡은 첫 재측정은 전체 평균의 작은 Temporal
bias로 실패했으며 회고 자료로 보존한다. reference T만으로 고정한 cloud-edge를 선명도 수렴 gate로
추가한 최신 Release 1080p 재측정은 96개 결과를 모두 통과했다. 16개 High Cloud RT는 모두
1920×1080이었다. 동일 edge 1,876,893픽셀에서 Medium→High RGB RMSE는
`0.00304644→0.00033319`, T MAE는 `0.00327674→0.00006603`으로 감소했다. Medium Temporal
4→16 frame edge RGB RMSE는 `0.00347051→0.00337033`, edge T MAE는
`0.00347650→0.00345479`로 개선됐고, history age `4→16`, reset count `9→9`였다. 전체 화면
T MAE `0.000603569→0.000615385`의 작은 bias도 숨기지 않고 JSON에 기록한다.

`--stage15-stage14-regression-probe`는 DenseHorizon만 120 warmup+600 unique sample로 세 번
측정하고 Cloud p95 중앙값을 승인값 `7.592960ms`의 103%인 `7.8207488ms`와 비교한다. GPU
Frame/Atmosphere/Shadow/Opaque/Raymarch/Resolve/Tone/Cloud Total의 avg/p50/p95/p99와 raw CPU
frame, adapter/driver, 해상도, 상태 fingerprint, shader variant/hash를 JSON/CSV에 보존한다.
Cloud Total과 Shadow+Raymarch+Resolve의 합이 timestamp 오차 범위에서 다르면 fixture 자체를
실패 처리한다. 이 회귀 probe의 렌더 프레임은 ImGui frame과 모든 panel/overlay/preview 및 수동
네 PNG export를 생성하지 않는다. Temporal 통계용 네 번째 MRT와 mip/readback도 수집하지 않는다.
위 preset smoke의 metadata-only JSON 예외는 회귀 probe에는 없다.

데스크톱 에이전트에서 측정할 때는 앱 내부 UI뿐 아니라 host `ChatGPT.exe`도 같은 GPU의 3D
엔진을 사용할 수 있다. Windows GPU Engine counter에서 다른 3D 프로세스를 확인하고, 측정 동안
ChatGPT 창을 최소화한 뒤 종료 즉시 복원한다. 이 절차를 생략하면 동일 fingerprint와 bytecode에서도
Cloud p95가 `6.36~8.30ms`로 흔들려 3% gate를 오판할 수 있다.

2026-08-28 1차 구현 측정은 **High가 아직 50% Joint4/Stable 4-Phase였던 이전 descriptor**의
48개 실시간 조합 Frame/Cloud/Resolve 절대 예산과 상대 평균
순서를 모두 통과했다. Resolve의 최악 p95는 2ms 아래였다. 당시 Stage 14 승인 DenseHorizon
대비 `8.109056ms`, `1.06797×` 회귀를 확인했다. 이후 non-Cirrus raymarch와 Deep Shadow를
compile-time variant로 분리했으며 고정 FXC `/O1` bytecode가 `stage14` tag와 각각 완전히
동일함을 정적으로 확인했다. 당시 오염 없는 probe 중앙값은 `5.659648ms`, 승인 기준 대비
`0.745381×`다. 48-case도 절대 실패 0, 최악 Frame/Cloud/Resolve p95
`5.787648/5.718016/1.182720ms`, Stage 14 회귀 `6.979584ms`/`0.919218×`로 통과했다.
상대 품질의 최악 Low/Medium과 High/Medium ratio는 `0.699942/1.537978`이다.

위 값은 숨김 화질 출력과 High Full 승격 전의 보존된 성능 기준이다. 이후 Temporal Off 화질 readback을 위해
일반 `CloudUpsample`에 ID 8(T)/79(scattering) 분기를 추가해 shader bytecode가 바뀌었으므로, 최신
실행 파일의 최종 성능 증거는 실제 extent와 High Full descriptor를 기록한 3-block
regression probe와 48-case를 다시 실행한 결과로 교체한다.

2026-08-31 High Full 1차 재실행은 외부 게임 PID가 Windows GPU 3D 엔진을 약 `37%` 사용하고
별도 VolumetricCloud 회귀 인스턴스까지 겹친 상태였다. 이때 생성된
`performance.json/csv`의 실패값은 외부 부하 탐지 기록일 뿐 성능 판정 자료가 아니다. 해당 프로세스와
다른 GPU fixture를 제거한 뒤 clean 재측정했다. 48개 절대 실패는 0이며 품질별 최대
Frame/Cloud/Resolve p95는 Low `2.64397/2.59891/1.12026ms`, Medium
`3.86970/3.82566/0.956416ms`, High `10.0014/9.95738/0.892928ms`다. Low/Medium owned-average
최악 ratio는 `0.811623`으로 3% 상대 gate를 통과했다. 성능 명령 내부 Stage 14 회귀는
`6.37645ms`/`0.839784×`, 독립 3-block은 `6.57203/6.15219/6.21363ms`, 중앙값
`6.21363ms`/`0.818341×`로 통과했다. 모든 state/component invariant와 D3D debug gate도 통과했다.
