# 성능 측정 기준

## 현재 상태

단계 9의 Early Exit·빈 공간 coarse 탐색과 계측 코드는 보존되어 있지만, 소규모 AABB의
`DenseExterior`에서 GPU Cloud p95 15% 개선 기준을 만족하지 못해 보류했다. 2026-08-09 직렬 측정의
Balanced 개선율은 Dense -1.66%, Sparse +10.19%, DepthOccluded +2.18%, Inside +15.85%였다.
Early Exit Only와 Empty Space Only의 Dense p95도 각각 8.892ms, 9.625ms로 Off 8.657ms보다 느렸다.

현재 선행하는 단계 13은 대규모 평면층의 기준선을 수립하는 단계다. 이 단계의 성능 수치는 기록만 하며
16.67ms 합격 조건이나 단계 9 개선 게이트를 적용하지 않는다.

## 계측 범위

`FrameProfiler`는 8-slot D3D11 timestamp query ring으로 GPU Frame과 GPU Cloud 구간을 비동기로
읽는다. CPU Frame은 `Renderer::Render` 시작부터 `Present` 반환까지이므로 VSync 대기를 포함할 수 있다.
구름 비용 비교에는 FPS보다 `GPU Cloud ms`를 우선 사용한다.

| 항목 | 의미 |
|---|---|
| `CPU Frame` | 렌더 함수 시작부터 Present 반환까지 |
| `GPU Frame` | 진단 장면 시작부터 ImGui draw 종료까지 |
| `GPU Cloud` | 볼류메트릭 클라우드 패스만의 GPU 시간 |
| `View` | `maxViewSteps @ stepSize(m)` |
| `Light` | `maxLightSteps @ lightStepSize(m)` |

## 고정 비교 절차

1. Release, 1920×1080, VSync Off, 시간 0, UI 제외 조건을 사용한다.
2. 카메라·Weather·Noise·조명·Phase·Environment 값을 고정한다.
3. 각 시나리오를 2초이자 최소 120프레임 워밍업한다.
4. EMA가 아닌 원시 timestamp 300개를 3회 기록한다.
5. 한 번에는 최적화 정책 하나만 바꾸고 p50·p95와 화질을 함께 비교한다.

```powershell
.\tools\Run-Stage9Benchmark.ps1
```

`raw.csv`는 원시 GPU Cloud/Frame과 CPU Frame을, `summary.json`은
min/mean/p50/p95/max/표준편차를 기록한다. GUI 실행은 겹치지 않도록 직렬로 수행한다.

## 단계 13 승인 뒤 단계 9 재측정

단계 13 사용자가 지상·수평선·구름 내부·상공·장거리 이동 화면을 승인하면, 같은 평면층의
Optimization Off 결과를 새 성능·화질 기준으로 고정한다.

| 시나리오 | 목적 |
|---|---|
| `GroundZenithDense` | 지상에서 위를 보는 조밀 구름 |
| `GroundHorizonDense` | 가장 긴 View 구간과 원거리 fade를 포함한 주 게이트 |
| `SparseHorizon` | 빈 공간 탐색 효과와 회귀 |
| `DepthOccluded` | 불투명 Scene Depth 조기 제한 |
| `InsideLayer` | `tStart=0`인 내부 카메라 |

완료 조건은 `GroundHorizonDense` GPU Cloud p95 15% 이상 개선, 나머지 장면 3% 초과 회귀 없음,
Optimization Off 대비 SSIM 0.99 이상·정규화 RMSE 0.01 이하, 그리고 사용자 화질 승인이다.
단계 8 승인 태그와 실행본은 과거 화질 기준으로 계속 보존한다.
