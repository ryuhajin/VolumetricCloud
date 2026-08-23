# 성능 측정 기준

이 문서는 이후 단계에서 최적화 전후 결과를 같은 조건으로 비교하기 위한 공통 기준이다.
화면 우측 상단 성능 오버레이는 `FrameProfiler`가 모은 CPU/GPU 시간을 보여 준다.

## 오버레이 항목

| 항목 | 시간 범위와 의미 |
|---|---|
| `Frame #` | `Renderer::Render`가 실행된 누적 프레임 번호 |
| `FPS` | 평활화된 CPU Frame 시간의 `1000 / ms` 값 |
| `CPU Frame` | `Renderer::Render` 시작부터 `Present` 반환까지. VSync 대기 포함 |
| `GPU Frame` | 진단 장면 시작부터 ImGui draw 종료까지. `Present` 제외 |
| `Cloud Raymarch` | 선택 해상도에서 구름 scattering/T/depth를 MRT에 적분 |
| `Spatial/Temporal Resolve` | Full-resolution 공간 복원, 선택적 history 재투영·clip과 장면 합성 |
| `GPU Cloud Total` (`GPU Cloud`) | 위 두 구간의 합. 단계 9 JSON과 비교하는 호환 지표 |
| `View` | `maxViewSteps @ stepSize(m)` |
| `Light` | `maxLightSteps @ lightStepSize(m)` |
| `VSync` | 현재 `Present(1, 0)` 또는 `Present(0, 0)` 경로 |

CPU Frame과 GPU Frame은 측정 범위가 다르므로 서로 같은 값일 필요가 없다. 특히 VSync On에서는
CPU Frame이 모니터 주사율 대기 시간을 포함한다. View/Light Ray 비용을 비교할 때는 FPS보다
`GPU Cloud Total ms`를 우선 사용한다.

## 비동기 GPU 계측 방식

`FrameProfiler`는 8개 슬롯의 D3D11 timestamp query ring을 사용한다. 각 슬롯은 timestamp
disjoint와 GPU Frame 시작·종료, Cloud 시작·Raymarch 종료·Cloud 종료 timestamp를 가진다. 현재 프레임을
기다리지 않고 `D3D11_ASYNC_GETDATA_DONOTFLUSH`로 완료된 과거 슬롯만 읽는다. 8개 슬롯이
모두 사용 중이면 해당 프레임의 GPU 측정을 생략하고 렌더링을 계속한다.

query가 아직 준비되지 않았으면 마지막 유효값 또는 `warming up`을 표시한다. disjoint,
리사이즈에 따른 세대 변경, 잘못된 timestamp 순서와 비정상 값은 버린다. CPU/GPU 표시값에는
`alpha=0.1` EMA를 적용하지만 원본 표본은 profiler 내부에서 검증한 뒤 누적한다.

## 고정 비교 절차

1. Release 빌드를 사용한다.
2. 창 해상도, 카메라, Weather, Detail, 태양·Phase·Environment 설정을 동일하게 맞춘다.
3. F1 Noise 창의 Animation에서 시간을 정지한다.
4. 같은 F1 창의 Performance에서 VSync를 Off로 설정한다.
5. 설정 변경 후 최소 2초 동안 워밍업한다.
6. `Cloud Raymarch`, `Spatial/Temporal Resolve`, `GPU Cloud Total`을 기록하고 같은 조건에서 비교한다.
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
