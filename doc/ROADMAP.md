# 단계별 재구축 로드맵

한 단계의 자동 검증과 사용자 렌더 승인이 끝나야 다음 단계로 진행한다. 단계 번호는 기존
학습 기록을 보존하며 실행 순서만 다음과 같이 확정한다.

```text
0~8 완료 → 13 재구축 → 9 → 10 → 11 → 12 → 14 → 15
```

## 승인 완료

- [x] 단계 0~1: 기반 구성과 상수 밀도 AABB
- [x] 단계 2~5: Base/Detail/Weather 구름 형태
- [x] 단계 6~8: 태양광, Phase, 환경광과 다중 산란

## 승인 완료: 단계 13 대규모 평면 구름 도메인

- [x] 13-0: Stage 8 기준선과 meter/cycle/m/1/m 단위 계약 — 2026-08-10 사용자 승인
- [x] 13-1: AABB 비교 기준과 Y 평면층 교차 분리 — 2026-08-10 사용자 승인
- [x] 13-2: 기존 noise의 1×/10×/100×/1000× 상사 확대 — 2026-08-11 사용자 승인
- [x] 13-3: 1.5~4.5km 오픈 월드 층, 64km Weather와 50km 유한 추적 — 2026-08-11 사용자 승인
- [x] 13-4B: Weather 기반 1~6km 물리 두께, 타입별 Vertical Profile, Base/Detail 3D texture — 2026-08-14 사용자 승인
- [x] 13-4C: F1~F4 역할 분리와 Local Cloud Inspector — 입력 우선순위, 장면 이동·복원, 5단계 파이프라인 비교와 Weather/Base/Thickness 품질 재조정 — 2026-08-16 사용자 승인
- [x] 13-4D: Local Inspector를 대체하는 단일 포트폴리오 디버깅 씬 — 2026-08-17 사용자 승인
- [x] 13-4E: Dense Broken-Sky와 Stratus/Cumulus/Custom — 2026-08-17 사용자 승인
- [x] 13-5: km 광학, 조명과 Detail 거리 LOD — 2026-08-17 사용자 승인
- [x] 단계 13 최종: PlanarLayer 대규모 도메인 — 2026-08-17 사용자 승인, 구형 shell 계획 제외

중단된 단계 13 v1은 `feature/large-planar-cloud-layer`와 `stage13-paused-20260810`에
보관한다. 새 구현은 코드를 병합하지 않고 수식·테스트·실패 기록만 참고한다.

## 현재 목표: 단계 12 Cloud Shadow Map과 Light Cache

- [x] 단계 9: View Weather/Base precheck, empty-space skip, View early exit, 거리별 step과
  deterministic Light cone — Balanced 기본값, 2026-08-19 사용자 승인
- [x] 단계 10: 저해상도 구름 타깃과 depth/transmittance-aware 업샘플링 — Stage 11 승인 계보에 포함해 2026-08-23 고정
  - [x] RGBA16F scattering/T + RG32F cloud depth/scene limit MRT와 Full resolve 분리
  - [x] 최초 50/67/75/Full, Nearest/Bilinear/Joint4/Joint9 비교와 schema 32
  - [x] 사용자 검증으로 활성 후보를 50/Full, Nearest/Bilinear/Joint4로 축소
  - [x] opacity-weighted depth, 경계 거부 fallback, 분리 GPU timestamp와 단계 10 smoke
  - [x] 1080p 자동 성능·경계 회귀와 사용자 렌더 기준 고정
- [x] 단계 11: Jitter, Temporal Reprojection과 history rejection — 2026-08-23 사용자 승인, `stage11`
  - [x] Stage 10 커밋 계보와 Off current-only 회귀 기준을 `stage11`에 함께 고정
  - [x] 50% 축 저해상도 texel `±0.25`의 2×2 4-phase Cloud UV/Ray/Scene Depth jitter
  - [x] Full `RGBA16F scattering/T + RG16F cloud depth/scene limit` history ping-pong과 b11 계약
  - [x] 대표 Cloud Depth, previous View-Projection과 Physical Wind `-velocity×dt` reprojection
  - [x] Scene class/depth, Cloud depth, T, bounds·motion·near history rejection과 3×3 clipping
  - [x] resize·camera cut·preset·time jump·shader reload history reset, schema 33와 schema 32→Off 계약
  - [x] Jitter/Motion/History Validity/Weight/Difference/Current Source Validity debug와 F1 Temporal UI
  - [x] 1차 jitter 역보정 뒤에도 세 필터에서 재현된 건물 1픽셀 왕복 원인 분리
  - [x] Full Scene class와 D32 평면 기울기 source validation, 필터별 invalid 제거·3×3 fallback과 invalid-current history 유지
  - [x] 3×3 불투명 current Cloud Depth 범위 history rejection과 scattering/T clipping 재사용
  - [x] CPU math·1080p GPU smoke — F5 경계와 F8 ±yaw/pitch 사선 평면, 세 필터 debug·Off hole·finite·D3D11 오류 검사
  - [x] Stratus/F5·F8/50% 120 warmup+600표본 — Resolve p95 `1.255424/1.314816ms`, Total `3.897344/2.793472ms`
  - [ ] 1080p Full reference 대비 4/8/16프레임 SSIM/RMSE/T 정량화 — Stage 15 최종 품질 측정으로 이관
  - [x] F5 건물 경계와 F8 사선 평면 세 필터·Weight scanline 사용자 재검증
  - [x] 안정성 기준에서 4×4 interleaved ray reduction과 ray-start random jitter 제외
- [ ] 단계 12: Cloud Shadow Map과 Light Cache — 다음 구현 단계
- [ ] 단계 14: 하늘·태양·지면과 구름 조명 통합
- [ ] 단계 15: Low/Medium/High, 1080p 성능, 최종 캡처와 포트폴리오 설명

세부 결정과 수치는 [VOLUMETRIC_CLOUD_PORTFOLIO_PLAN.md](VOLUMETRIC_CLOUD_PORTFOLIO_PLAN.md)를 따른다.
