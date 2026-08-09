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

## 현재 범위와 보관된 측정

현재 승인 기준은 단계 8이며 오버레이는 실시간 관찰용이다. CSV 기록, 자동 벤치마크,
중앙값·p95 통계의 구체적인 장면과 합격 기준은 후속 단계 재계획에서 다시 정한다.

이전 단계 9의 AABB 벤치마크와 단계 13 평면 구름층 측정은 `captures/performance/`와
각 보관 브랜치에 역사 자료로 남아 있다. 해당 결과는 현재 성능 게이트가 아니며 새 계획에서
장면 규모·카메라·품질 설정을 확정하기 전에는 최적화 합격 판정에 재사용하지 않는다.
