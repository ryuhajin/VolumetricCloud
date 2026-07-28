# 프로젝트 현황 & 프레임 흐름

> 복귀 시 현재 구현과 한 프레임의 작업을 빠르게 확인하는 문서.
> Stage 6 / `feature/temporal-cloud-performance` 기준.

## 현재 구현

| 영역 | 상태 |
|---|---|
| 레이마칭 | 평면 구름층 교차, 3~16 km 기준 두께, 50m fine step 상한, 최대 2000회, weather 4×/2×/1× 진행과 경계 이분 탐색 |
| 광역 분포 | 512² RGBA weather map, coverage/type/base-height/thickness, base/detail과 동일한 월드 바람 이동 |
| 형태 | 독립 월드 공간 periodic Perlin-Worley + Worley base 3밴드 + 128³ detail 4옥타브 |
| 높이 | 하단이 좁고 중단이 부푸는 Cumulus profile, weather type 성장과 anvil |
| 조명 | 8-step 0.6km 근거리 + 4-step 원거리 self-shadow, 정규화 dual-lobe phase, powder, silver lining |
| 다중 산란 | light visibility의 3-octave 저비용 근사 |
| 화면 복원 | beauty 0.5배 축 raymarch, 4-frame jitter, full-resolution temporal reprojection·depth rejection·neighborhood clamp |
| 캐시 | v7 불변 세대 + active 포인터, `.cso` 9개 + 128³ base/detail + 512² weather |
| 진단 | 20개 렌더 모드, 4-MRT Inspector, temporal/reference 전환, raymarch/reconstruction/total 및 startup CSV |
| 검증 | 기존 수치 검사 + density early-out 동등성, temporal 행렬·깊이 rejection, history reset, 리소스 규격·resize |

형태와 조명 수식, temporal 성능 목표는 구현·수치 검증됐다. 최종 심미 품질은 사용자 장비에서 승인해야 하며 자동 테스트는 스크린샷의 미적 합격 여부를 판단하지 않는다.

현재 측정 장비의 Release 1280×720 기본 3.8km/128은 reference median 28.9833ms, p95 34.3624ms이고 temporal median 12.2353ms, p95 13.4062ms다. temporal은 median 16.0ms와 p95 16.67ms 목표를 통과했다. 120초 VSync 진단에서 reference는 전 구간 약 30 FPS, temporal은 시작부터 전 구간 약 60 FPS였고 두 경로 모두 runtime compile/noise dispatch 0회였다.

## GPU 리소스

| 슬롯 | 리소스 | 내용 |
|---|---|---|
| b0 | `CameraCB` | 96바이트: 역 view-projection, 카메라/시간, jitter/render size |
| b1 | `CloudCB` | 224바이트 노이즈·weather·구름층·조명·샘플링 파라미터 |
| b2 | `TemporalCB` 또는 Preview/Generation CB | 이전 view-projection·카메라·바람·history 상태, 또는 단면/볼륨 생성 설정 |
| t0 | base Texture3D | 128³ RGBA8, Perlin-Worley + Worley 3밴드 |
| t1 | detail Texture3D | 128³ RGBA8, Worley 4옥타브 |
| t2 | weather Texture2D | 512² RGBA8, coverage/type/base-height/thickness |
| temporal MRT | half color/depth | `R11G11B10_FLOAT` + `R16_FLOAT` |
| temporal history | full color/depth ×2 | ping-pong `R11G11B10_FLOAT` + `R16_FLOAT` |
| s0 | wrap sampler | 선형 Texture3D 샘플링 |

메인 ray/light loop는 캐시 Texture3D만 읽는다. 절차식 RGBA 노이즈는 Inspector와 cache difference의 단일 지점 진단에서만 평가해 fxc 컴파일 시간과 프레임 비용을 제한한다.

## 한 프레임 순서

```text
ImGui Begin
  → UI 변경 및 캐시 명령 처리
  → CameraCB / CloudCB 업로드
  → [생성 파라미터 변경 시] base/detail/weather compute dispatch
  → [Inspector 열림 + dirty] 4-MRT preview
  → beauty: 0.5배 축 fullscreen triangle
      → cloud slab intersection
      → 50m 상한 jittered/adaptive view march + 밀도 진입 경계 정제
      → density가 있을 때 근거리/원거리 light march
      → Beer–Lambert + dual-lobe + multiple-scatter approximation
  → full-resolution temporal resolve
      → 이전 view-projection + 바람 이동 재투영
      → 화면/depth 검사 + 3×3 neighborhood clamp
  → back-buffer composite
  → debug/reference: full-resolution 직접 raymarch
  → F1 편집기 + F2 telemetry draw
  → Present
  → HLSL hot reload 확인
```

정상 v7 캐시 시작에서는 HLSL runtime compile과 noise dispatch가 모두 0회다.

## F1 진단과 F2 HUD

- 형태: Final Density, Base Shape, Detail, Height, Base R/G/B/A, Weather R/G/B/A
- 조명: Transmittance, Light Visibility, Phase, Ambient, Direct
- 일관성: Procedural/Cache Difference, Seam Difference
- 파라미터: Shape & Noise만 최초에 열리는 6개 접이식 그룹, 상태는 `imgui.ini`에 보존
- 프리셋: Default, Cumulus, Stratus, Cumulus Showcase, Cumulus Wide
- 통계: Inspector/메인 source, base/detail/weather 캐시 규격과 캐시 조작
- 성능 전환: beauty temporal/reference 선택, debug mode는 자동 reference
- 창: 최초 480×560, 최소 360×280, Inspector는 가용 폭 540 px 기준 1/2열 전환
- 상시 HUD: 프리셋·모드, FPS, raymarch/reconstruction/total GPU, 태양·구름층·캐시 오류

## 남은 우선순위

1. 기본 장면·회전·줌·F1 비교의 ghosting/shimmer/경계 사용자 승인
2. 사용자 장비 startup CSV와 표시 전환 재현 여부 확인
3. 장면 depth 합성
4. 구형 대기층과 지구 곡률

변경 이유와 대안은 [feature-temporal-cloud-performance 변경 기록](changes/feature-temporal-cloud-performance.md)을 참고한다.
