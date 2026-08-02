# 프로젝트 현황 & 프레임 흐름

> 복귀 시 현재 구현과 한 프레임의 작업을 빠르게 확인하는 문서.
> Stage 7 / `feature/cloud-visual-quality-optimization` 기준.

## 현재 구현

| 영역 | 상태 |
|---|---|
| 레이마칭 | 평면 구름층 교차, 3~16 km 기준 두께, 50m fine step 상한, 최대 2000회, weather 4×/2×/1× 진행과 경계 이분 탐색 |
| 광역 분포 | 저주파 cluster×중주파 variation weather와 support union·혼합 속성 cellular placement, 공통 월드 바람 이동 |
| 형태 | placement 중심·반경 support + 독립 월드 공간 Perlin-Worley + 높이별 Worley base 3밴드 + 128³ detail 4옥타브 |
| 높이 | 평평한 하단·넓은 중단·높이 0.45 이후 좁아지는 상단, 중심별 두께 변이 |
| 조명 | 5-sample 근거리 cone detail + 1-sample 원거리 macro self-shadow, sky AO, optical-depth powder와 visibility 제한 silver lining |
| 다중 산란 | single scattering + extinction/eccentricity 감쇠 octave 1개 |
| 화면 복원 | beauty 0.5배 축 scattering/metadata raymarch, 2×2 depth/transmittance-aware 복원, temporal reprojection·깊이/transmittance rejection |
| 캐시 | v10 불변 세대 + active 포인터, `.cso` 9개 + 128³ base/detail + 512² weather/placement |
| 진단 | 23개 렌더 모드, 4-MRT Inspector, temporal/reference 전환, raymarch/reconstruction/total 및 startup CSV |
| 검증 | density early-out, cone/AO/에너지 범위, RG16 metadata, temporal 깊이·transmittance rejection, edge weight, history reset, resize |

형태와 조명 수식, temporal 성능 목표는 구현·수치 검증됐다. 최종 심미 품질은 사용자 장비에서 승인해야 하며 자동 테스트는 스크린샷의 미적 합격 여부를 판단하지 않는다.

이전 v7 측정 장비의 Release 1280×720 기본 3.8km/128은 reference median 28.9833ms, p95 34.3624ms이고 temporal median 12.2353ms, p95 13.4062ms였다. v8 temporal은 GPU total median 9.7736ms, p95 11.6204ms였고 placement-first v9은 median 2.1371ms, p95 4.8128ms였다. 속성 혼합과 자동 edge AA를 적용한 v10의 120표본은 3.8km/128 median 1.6579ms, p95 2.4730ms이며 16km/128도 median 2.5395ms, p95 3.1150ms다. 두 기본 합격선 16.0/16.67ms를 통과했으며 실제 절단면 완화는 사용자 화면 승인이 남아 있다.

## GPU 리소스

| 슬롯 | 리소스 | 내용 |
|---|---|---|
| b0 | `CameraCB` | 112바이트: 역 view-projection, 카메라/시간, jitter/render size, temporal output |
| b1 | `CloudCB` | 272바이트 노이즈·weather·placement·구름층·조명·샘플링 파라미터 |
| b2 | `TemporalCB` 또는 Preview/Generation CB | 이전 view-projection·카메라·바람·history 상태, 또는 단면/볼륨 생성 설정 |
| t0 | base Texture3D | 128³ RGBA8, Perlin-Worley + Worley 3밴드 |
| t1 | detail Texture3D | 128³ RGBA8, Worley 4옥타브 |
| t2 | weather Texture2D | 512² RGBA8, coverage/type/base-height/thickness |
| t3 | placement Texture2D | 512² RGBA8, support union/blended radius-height-profile |
| temporal MRT | half scattering/metadata | `R11G11B10_FLOAT` + `R16G16_FLOAT` |
| temporal history | full scattering/metadata ×2 | ping-pong `R11G11B10_FLOAT` + `R16G16_FLOAT` |
| s0 | wrap sampler | 선형 Texture3D 샘플링 |

메인 ray/light loop는 캐시 Texture3D만 읽는다. 절차식 RGBA 노이즈는 Inspector와 cache difference의 단일 지점 진단에서만 평가해 fxc 컴파일 시간과 프레임 비용을 제한한다.

## 한 프레임 순서

```text
ImGui Begin
  → UI 변경 및 캐시 명령 처리
  → CameraCB / CloudCB 업로드
  → [생성 파라미터 변경 시] base/detail + weather/placement compute dispatch
  → [Inspector 열림 + dirty] 4-MRT preview
  → beauty: 0.5배 축 fullscreen triangle
      → cloud slab intersection
      → 50m 상한 jittered/adaptive view march + 밀도 진입 경계 정제
      → density가 있을 때 5개 cone detail + 1개 far macro light sample
      → Beer–Lambert + dual-lobe + attenuated scattering octave + sky AO
  → full-resolution temporal resolve
      → 이전 view-projection + 바람 이동 재투영
      → 2×2 edge reconstruction + 화면/depth/transmittance 검사 + 3×3 clamp
  → 분석적 하늘과 scattering/transmittance back-buffer composite
  → debug/reference: full-resolution 직접 raymarch
  → F1 편집기 + F2 telemetry draw
  → Present
  → HLSL hot reload 확인
```

정상 v10 캐시 시작에서는 HLSL runtime compile과 noise dispatch가 모두 0회다.

## F1 진단과 F2 HUD

- 형태: Final Density, Base Shape, Detail, Height, Base R/G/B/A, Weather R/G/B/A, Placement Support/Radius/Height
- 조명: Transmittance, Light Visibility, Phase, Ambient, Direct, Ambient Occlusion
- 복원: Resolved Opacity, Temporal History Confidence
- 일관성: Procedural/Cache Difference, Seam Difference
- 파라미터: Shape & Noise만 최초에 열리는 6개 접이식 그룹, 상태는 `imgui.ini`에 보존
- 프리셋: Default, Cumulus, Stratus, Cumulus Showcase, Cumulus Wide
- 통계: Inspector/메인 source, base/detail/weather/placement 캐시 규격과 캐시 조작
- 성능 전환: beauty temporal/reference 선택, debug mode는 자동 reference
- 창: 최초 480×560, 최소 360×280, Inspector는 가용 폭 540 px 기준 1/2열 전환
- 상시 HUD: 프리셋·모드, FPS, raymarch/reconstruction/total GPU, 태양·구름층·캐시 오류

## 남은 우선순위

1. 기본 장면·회전·줌·F1 비교의 ghosting/shimmer/경계 사용자 승인
2. 사용자 장비 startup CSV와 표시 전환 재현 여부 확인
3. 장면 depth 합성
4. 구형 대기층과 지구 곡률

변경 이유와 대안은 [feature-cloud-visual-quality-optimization 변경 기록](changes/feature-cloud-visual-quality-optimization.md)을 참고한다.
