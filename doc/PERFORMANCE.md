# 성능 측정 기준

현재 성능 목표는 Full-resolution High 한 경로의 실제 비용을 측정하는 것이다. Low/Medium, Resolve, Temporal, Composite 별도 항목은 존재하지 않는다.

## GPU profiler 범위

`FrameProfiler`는 8-slot D3D11 timestamp query ring으로 다음 순서를 비동기 측정한다.

| 항목 | 범위 |
|---|---|
| `Atmosphere` | 이번 프레임에 필요한 LUT compute. hash가 같으면 거의 0ms |
| `Shadow` | Near/Far Balanced512 Deep Optical-Depth Cache compute |
| `Opaque` | HDR 지면·건물과 D32 depth raster |
| `Cloud` | Full-resolution raymarch와 scene/atmosphere HDR 합성 |
| `Tone` | HDR exposure, white balance, tone curve, sRGB와 dither |
| `Frame` | 첫 timestamp부터 Tone/ImGui 종료까지 전체 GPU 구간 |

CPU Frame은 `Renderer::Render`부터 `Present` 반환까지이며 VSync 대기를 포함할 수 있다.
VSync Off는 지원 환경에서 tearing 허용 즉시 Present를 사용하지만 GPU 렌더 자체가
병목이면 FPS는 오르지 않는다. 성능 gate는 표시 주기와 분리된 raw GPU timestamp를 사용한다.

## 최종 성능 gate

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

## 비교 기준 보존

삭제 전 기준은 `captures/simplification-baseline-2026-08-31`에 로컬 보존한다. 이 fixture는 1920×1080, High, Temporal Off, Rim Off, Detail LOD Off, optimized direct 조건에서 만들었다.

기존 Stage 15B 정량 비교에서 High direct 경로는 reference 대비 edge normalized RGB RMSE `0.000250957`, edge transmittance MAE `0.0000908621`을 기록했고 debug layer gate를 통과했다. 이 수치는 삭제 전 동등성 근거이며 현재 런타임에 Reference PS를 다시 두기 위한 기능이 아니다.

## 핫 리로드 성능 gate

핫 리로드 시간은 shader 저장 후 transaction 내부 경과 시간으로 측정한다. startup과 Texture3D 생성이 필요 없는 테스트는 `enableNoiseVolumes=false`로 실행한다.

| smoke | 하드 게이트 | warm 목표 | 2026-09-01 결과 |
|---|---|---:|---:|
| Tone Map | 영향 프로그램 1, 성공 교체, 오류 rollback, 원본 cache hit 1 | 15초 이하 | `0.024s`, 통과 |
| `Noise.hlsli` | Cloud PS + Deep Shadow CS + NoiseLab PS 정확히 3 | 30초 이하 | warm `0.224s`, 통과 |

```powershell
ctest --test-dir build -C Debug -R "HotReloadSmoke$" -V
ctest --test-dir build -C Debug -R "HotReloadDependencySmoke$" -V
```

CTest script는 source shader를 build 하위 임시 폴더에 복사하고 표식 파일을 만든다. 실행 파일은 이 표식이 없는 디렉터리에서 forced mutation을 거부한다.

## 수치 해석

- `Cloud p95`가 높고 Shadow는 안정적이면 View density/lighting 비용을 먼저 본다.
- `Shadow p95`만 높으면 cache invalidation이 매 프레임 잘못 발생하는지 확인한다.
- `Atmosphere`가 정지 상태에서도 지속적으로 높으면 LUT hash나 카메라 의존 aerial invalidation을 확인한다.
- `Frame - (Atmosphere+Shadow+Opaque+Cloud+Tone)`은 query 사이의 명령·UI와 driver scheduling 여유다.
- 자동 gate 통과는 최종 외형 승인이 아니다. shimmer, 구름 절단, 산란과 색은 사용자가 Release 화면에서 확인한다.

## 최신 결과

| 날짜 | GPU/driver | Cloud p95 | Frame p95 | 결과 |
|---|---|---:|---:|---|
| 2026-09-01 | NVIDIA GeForce RTX 4080 SUPER / 32.0.15.9186 | `5.153ms` | `5.984ms` | 3회 반복 최악값, 자동 gate 통과, 사용자 화면 승인 전 |

한 번의 실행마다 유효 표본은 12 case × 120개 = 1,440개이며 3회 연속 통과했다. 세 반복에서 가장 무거운 case는 `Meadow Broken Clouds / InsideLayer`로 Cloud p95 `8.888ms`, Frame p95 `9.788ms`였으며 case별 gate도 모두 통과했다. 표의 전체값 역시 세 실행에서 각각 관찰한 p95 중 가장 큰 값이다. GPU timestamp는 실행 환경과 온도에 따라 달라질 수 있으므로 이후 하드웨어에서는 같은 명령으로 다시 측정한다.
