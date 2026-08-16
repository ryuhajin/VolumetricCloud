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

## 현재 목표: 단계 13 대규모 구름 도메인

- [x] 13-0: Stage 8 기준선과 meter/cycle/m/1/m 단위 계약 — 2026-08-10 사용자 승인
- [x] 13-1: AABB 비교 기준과 Y 평면층 교차 분리 — 2026-08-10 사용자 승인
- [x] 13-2: 기존 noise의 1×/10×/100×/1000× 상사 확대 — 2026-08-11 사용자 승인
- [x] 13-3: 1.5~4.5km 오픈 월드 층, 64km Weather와 50km 유한 추적 — 2026-08-11 사용자 승인
- [x] 13-4B: Weather 기반 1~6km 물리 두께, 타입별 Vertical Profile, Base/Detail 3D texture — 2026-08-14 사용자 승인
- [x] 13-4C: F1~F4 역할 분리와 Local Cloud Inspector — 입력 우선순위, 장면 이동·복원, 5단계 파이프라인 비교와 Weather/Base/Thickness 품질 재조정 — 2026-08-16 사용자 승인
- [ ] 13-4D: Local Inspector를 대체하는 단일 포트폴리오 디버깅 씬 — 구현 이력 보존, 사용자 검증의 점유율·형상·명암 피드백은 13-4E가 대체
- [ ] 13-4E: Dense Broken-Sky와 Stratus/Cumulus/Custom — density 결합·결정적 Compare·schema 29 구현 및 자동 검증, 사용자 렌더 승인 대기
- [ ] 13-5: km 광학, 조명과 Detail 거리 LOD — 기존 결과는 보존하되 13-4E Dense Mixed F6 재승인 대기
- [ ] 13-6: Earth-scale 구형 shell 비교
- [ ] 13-7: 평면/구형 성능 판정과 사용자 최종 승인

중단된 단계 13 v1은 `feature/large-planar-cloud-layer`와 `stage13-paused-20260810`에
보관한다. 새 구현은 코드를 병합하지 않고 수식·테스트·실패 기록만 참고한다.

## 단계 13 승인 후

- [ ] 단계 9: Weather/Base precheck, empty-space skip, early exit와 거리별 step
- [ ] 단계 10: 저해상도 구름 타깃과 depth/transmittance-aware 업샘플링
- [ ] 단계 11: Jitter, Temporal Reprojection과 history rejection
- [ ] 단계 12: Cloud Shadow Map과 Light Cache
- [ ] 단계 14: 하늘·태양·지면과 구름 조명 통합
- [ ] 단계 15: Low/Medium/High, 1080p 성능, 최종 캡처와 포트폴리오 설명

세부 결정과 수치는 [VOLUMETRIC_CLOUD_PORTFOLIO_PLAN.md](VOLUMETRIC_CLOUD_PORTFOLIO_PLAN.md)를 따른다.
