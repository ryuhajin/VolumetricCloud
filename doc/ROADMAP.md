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

## 저장소 단계 진행 규칙

- [x] GitHub 기본 브랜치를 `main`으로 변경하고 PR 필수·관리자 적용·force push/삭제 금지 보호 설정
- [x] merge commit만 허용하고 squash/rebase merge 비활성화
- [x] 구형 `feature/box-volume` 끝 `92ba69e`를 `archive-box-volume-20260823`으로 보존
- [x] `stage13-approved`를 원격에 보존한 뒤 `feature/box-volume`,
  `feature/km-optics-lighting` 로컬·원격 브랜치 삭제
- [x] Stage 12부터 `feature/stage<번호>-<설명>` → PR merge commit →
  `stage<번호>-approved` → 원격 검증 → 완료 브랜치 삭제 순서로 고정

## 현재 목표: 단계 15 최종 품질·콘셉트 프리셋

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
- [x] 단계 12: Cloud Shadow Map과 Deep Light Cache — 2026-08-25 사용자 승인
  - [x] 화면 해상도와 독립적인 Near 24km×80 / Far 128km×40 `R32_FLOAT` 배열
  - [x] Fast256 30MiB / Balanced512 120MiB 원자 리소스 교체와 b12/t6/t7/s2 계약
  - [x] Base-only top→bottom tau compute, 높이 보간, cascade blend·far neutral fade
  - [x] Cache 구름 자기 그림자와 Full Scene Depth 기반 지면·건물 그림자, Direct fallback
  - [x] F3 mode/preset/surface와 Near/Far cache slice/cascade/Surface/Error 진단, schema 34
  - [x] 2026-08-24 사용자 1차 검증 반영 — 지면·건물 중간 회색, Near/Far raw slice preview와
    exposure, 표면/구름층 월드 위치 기준 cascade로 교체
  - [x] CPU math와 Fast256 GPU smoke — Light T `MAE=0.001635`, `P99=0.029349`,
    Full/50%/resize cache identity와 Near/Far texture structure 유지
  - [x] 1080p 일곱 장면 Direct/Fast/Balanced 성능 gate와 기존 화질 smoke 통과,
    가장 높은 합격 후보 Balanced512 기본값 승격
  - [x] F5~F8·Full/50%·wind/이동 사용자 렌더 승인 — 2026-08-25, Surface T 정렬과
    Direct/Cache 내부 명암, 상단 black slice, magenta/blue cascade 확인
- [x] 단계 14: 하늘·태양·지면과 구름 조명 통합 — 2026-08-28 사용자 승인, `stage14`
  - [x] 224바이트 b13과 Trans/Multi/Sky View/Sky Irradiance/Aerial R/T 여섯 compute LUT
  - [x] 계수·지면·태양·카메라별 hash 무효화, 마지막 정상 LUT 원자 보존과 Manual 폴백
  - [x] face normal/material ID 진단 장면, 지면 preset/건물 콘크리트 Lambert HDR 조명
  - [x] 구름 직접 태양광, Sky Irradiance, 전역 Ground Bounce와 기존 AO/multiple 연결
  - [x] Direct/Spatial/Temporal 공통 Aerial 합성, RGBA16F HDR와 ACES/white balance 최종 패스
  - [x] F3 Sun/Time/Atmosphere/Ground/Tone/LUT UI와 schema 35 snapshot
  - [x] CPU math 및 GPU LUT generation/invalidation D3D smoke
  - [x] GPU Transmittance 전체 RGB readback `MAE=0.000189`, `P99=0.000485`, 여섯 LUT finite/non-negative
  - [x] Debug/Release 빌드, 전체 49 CTest, hot reload, D3D11 debug layer 검증
  - [x] 1080p 일곱 장면 성능 gate — GPU Cloud p95 평균 `7.37616ms`, Stage 12 호환 대비 `+4.325%`
  - [x] 사용자 화면 체크리스트 승인
- [ ] 단계 15: Low/Medium/High, 네 콘셉트와 최종 정량 검증
  - [x] CPU 전용 품질·콘셉트·진단 descriptor와 소유권 분리
  - [x] 112바이트 b7 및 방향성 Cirrus 공통 밀도 경로
  - [x] F4 프리셋/Advanced 진단, Q/T 입력과 compact 상태 overlay
  - [x] schema 37 상태 기록과 48조합 GPU preset smoke — 최신 Debug/Release 48/48·rollback/D3D 통과
  - [x] 15A 물리 출력/DPI smoke — 1280×720→Native 1920×1080, Medium 960×540,
    High Full 1920×1080 및 Capture 4-sample/복원 통과
  - [x] 1080p Full Reference 대비 96개 산란/T 화질 gate — Release 96/96, High 16개 Full,
    Medium→High edge RGB/T와 Temporal edge 4→16 개선, history age/reset·readback self-check/D3D 통과
  - [x] Release Stage10/11/15 targeted `8/8`, Stage 12 focused Debug/Release `2/2` — 최초 캡처 전
    `96×54` 명시 뒤 `MAE=0.001635`, `P99=0.029349`
  - [x] Debug/Release 전체 CTest 각각 `54/54` — Stage 12 explicit 96×54 fixture 수정 포함
  - [x] 48조합 120 warmup+600 timestamp 성능 gate — clean 48/48, High Cloud 최악
    `9.95738ms`, Resolve 최악 `1.12026ms`, Stage 14 독립 3-block median `6.21363ms`/`0.818341×`
  - [ ] 사용자 콘셉트·Temporal 화면 비교와 포트폴리오 캡처 승인

세부 결정과 수치는 [VOLUMETRIC_CLOUD_PORTFOLIO_PLAN.md](VOLUMETRIC_CLOUD_PORTFOLIO_PLAN.md)를 따른다.
