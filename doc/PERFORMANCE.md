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
| `GPU Cloud` | `RenderCloudPass`의 GPU 실행 시간만 측정 |
| `View` | `maxViewSteps @ stepSize(m)` |
| `Light` | `maxLightSteps @ lightStepSize(m)` |
| `VSync` | 현재 `Present(1, 0)` 또는 `Present(0, 0)` 경로 |

CPU Frame과 GPU Frame은 측정 범위가 다르므로 서로 같은 값일 필요가 없다. 특히 VSync On에서는
CPU Frame이 모니터 주사율 대기 시간을 포함한다. View/Light Ray 비용을 비교할 때는 FPS보다
`GPU Cloud ms`를 우선 사용한다.

## 비동기 GPU 계측 방식

`FrameProfiler`는 8개 슬롯의 D3D11 timestamp query ring을 사용한다. 각 슬롯은 timestamp
disjoint와 GPU Frame 시작·종료, Cloud Pass 시작·종료 timestamp를 가진다. 현재 프레임을
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
6. `GPU Cloud ms`를 기록하고, 같은 조건에서 여러 번 관찰해 안정된 값을 비교한다.
7. View/Light Step을 바꿀 때 한 번에 한 파라미터만 변경한다.

예를 들어 Light Step 8/16/32의 비용을 비교할 때 카메라와 나머지 설정을 고정한다. FPS는
운영체제·Present·다른 앱의 영향을 함께 받으므로 보조 지표로만 사용한다.
단계 8 Off/Balanced 또는 Multiple Octaves 0~4를 비교할 때도 한 번에 해당 설정만 바꾸고
`Shift+P`의 Light Sample 출력이 동일한지 함께 확인한다.

## 현재 범위와 보관된 측정

현재 사용자 화질 승인 대상은 단계 13-4E Dense Broken-Sky와 그 기본값에서 다시 확인하는 단계 13-5다.
View 512와 Light 80은 품질 시작값이며 GPU 시간과 Texture3D 생성 시간은 텍스트로 기록하되 13-5의
자동 실패 기준으로 사용하지 않는다.
최적화 합격 판정은 사용자 화면 승인 뒤 단계 9에서 시작한다.
두 모드의 공식 p95 비교는 형태·광학이 같은 입력을 공유하는 13-6~13-7에서 수행한다.

이전 단계 9의 AABB 벤치마크와 단계 13 평면 구름층 측정은 `captures/performance/`와
각 보관 브랜치에 역사 자료로 남아 있다. 해당 결과는 현재 성능 게이트가 아니며 새 계획에서
장면 규모·카메라·품질 설정을 확정하기 전에는 최적화 합격 판정에 재사용하지 않는다.

## 포트폴리오 1080p 합격 기준

단계 13-7 승인 뒤 현재 개발 PC를 기준 장치로 삼는다. 실행 시 DXGI 어댑터 이름과 드라이버,
해상도, 품질 프리셋, seed와 카메라를 결과 JSON에 기록한다.

| 항목 | High 합격 기준 |
|---|---:|
| 해상도 | 1920×1080 |
| VSync | Off |
| 워밍업 | 120 frames |
| 기록 | 600 frames |
| GPU Frame p95 | 16.67ms 이하 |
| GPU Cloud p95 | 10.00ms 이하 |
| 최적화 전후 SSIM | 0.99 이상 |
| 정규화 RMSE | 0.01 이하 |

고정 장면은 `GroundZenith`, `GroundHorizon`, `InsideLayer`, `AboveLayer`,
`FlightTraversal`, `DepthOccluded`, `PlanarVsShell`이다. 다른 앱의 동시 GPU 부하나
timestamp disjoint가 감지되면 측정을 무효로 표시하며 합격 자료로 사용하지 않는다.
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

F5~F8 플레이어 시야 변경은 품질 관찰용이며 성능 기준 장면을 확정하는 단계 13-7을
대체하지 않는다. `Stage13CameraControlMath`는 CPU 수학만 검사하고 성능 수치를 기록하지 않는다.

## 단계 13-4D 단일 씬 smoke

`Stage13UnifiedSceneSmoke`는 320×180 숨김 타깃에서 F5~F8과 숫자 0~9의 float 출력을
검사한다. 모든 출력은 finite이고 주요 기준 출력은 non-black이어야 하며 카메라별 Composite와
파이프라인 출력 hash가 구분되어야 한다. Compare 1~5 중 지면·건물·domain·이동 기준은
불변이고 Full Open World가 Dense Mixed+13-5 최종 입력을 복원해야 한다. schema 29 파싱과 제거 필드
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
이는 단계 13-5 Light 비용 변경의 한정 게이트이며, 위의 단계 13-7 1080p p95 기준을
대체하지 않는다.

같은 smoke는 Detail LOD Off와 32~48km On의 factor 출력이 실제로 달라지고, compute 생성
readback에서 계산한 weighted neutral mean이 유한한 `[0,1]`인지 검사한다. 기본 mean은
`0.44994098`이다. 이 테스트는 정확성 비교이며 GPU p95 성능 gate가 아니다. Light 표본 증가와
48km 밖 Detail fetch 생략의 최종 성능 효과는 단계 9 기준 측정에서 별도로 판정한다.

사용자 방향 검증을 회귀로 고정하기 위해 Low East/West의 View τ와 누적 Direct도 비교한다.
View τ MAE는 `0`, Direct MAE는 `0.03709492`로 태양 방향이 밀도 광학에는 영향을 주지 않고
직접광만 바꾸는 계약을 통과했다. Silver Lining+Balanced Composite의 float 최대값은 East
`0.96951944`, West `0.88669246`로 LDR 1을 넘지 않는다. 13-4E Dense Mixed F6에서 같은 허용 오차로
재측정해 동일한 값과 PASS를 확인했다.
