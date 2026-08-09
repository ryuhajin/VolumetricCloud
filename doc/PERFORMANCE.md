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
3. Noise Lab의 Animation을 정지한다.
4. Noise Lab의 Performance에서 VSync를 Off로 설정한다.
5. 설정 변경 후 최소 2초 동안 워밍업한다.
6. `GPU Cloud ms`를 기록하고, 같은 조건에서 여러 번 관찰해 안정된 값을 비교한다.
7. View/Light Step을 바꿀 때 한 번에 한 파라미터만 변경한다.

예를 들어 Light Step 8/16/32의 비용을 비교할 때 카메라와 나머지 설정을 고정한다. FPS는
운영체제·Present·다른 앱의 영향을 함께 받으므로 보조 지표로만 사용한다.
단계 8 Off/Balanced 또는 Multiple Octaves 0~4를 비교할 때도 한 번에 해당 설정만 바꾸고
`Shift+P`의 Light Sample 출력이 동일한지 함께 확인한다.

## 현재 범위

단계 9부터 Release CLI 벤치마크가 1920×1080, VSync Off, 시간 0, UI 제외 조건을 강제한다. 각 시나리오는 2초이자 120프레임 이상 워밍업한 뒤 원시 timestamp 300개를 3회 기록한다.

```powershell
.\tools\Run-Stage9Benchmark.ps1
```

Windows GUI 실행 파일은 PowerShell의 직접 호출이 종료를 기다리지 않을 수 있다. 위 스크립트는 `Start-Process -Wait`를 사용해 Off와 Balanced 측정이 동시에 실행되어 GPU 시간을 오염시키지 않게 한다.

`raw.csv`에는 EMA 전의 GPU Cloud/Frame과 CPU Frame을 기록하고 `summary.json`에는 min/mean/p50/p95/max/표준편차를 기록한다. DenseExterior는 GPU Cloud p95 15% 이상 개선, 다른 장면은 3% 초과 회귀 없음이 기준이다. 승인된 원본은 `stage8-approved` 태그와 `captures/performance/baselines/stage8-approved` 로컬 번들로 보존한다.

2026-08-09 1차 직렬 측정에서 Balanced의 GPU Cloud p95 개선율은 Dense -1.66%, Sparse +10.19%, DepthOccluded +2.18%, Inside +15.85%였다. Dense 15% 기준을 통과하지 못했으므로 단계 9는 사용자 승인 대기가 아니라 추가 최적화가 필요한 상태다. Early Exit Only와 Empty Space Only의 Dense p95도 각각 8.892ms, 9.625ms로 Off 8.657ms보다 느려, 단순 기능 조합 변경만으로는 해결되지 않았다.
