# 단계별 재구축 로드맵

한 단계의 자동 검증과 사용자 렌더 승인이 끝나야 다음 단계로 진행한다. 단계 번호는
기존 학습 기록과 링크를 보존하고, 실행 순서만 아래처럼 바꾼다.

```text
0~8 완료 → 13 선행 → 9 재개 → 10 → 11 → 12 → 14 → 15
```

## 목표 1~3: 승인 완료

- [x] 단계 0~1: 기반 구성과 상수 밀도 AABB
- [x] 단계 2~5: Base/Detail/Weather 구름 형태
- [x] 단계 6~8: 태양광, Phase, 환경광과 다중 산란

## 현재 목표: 인디 오픈월드용 대규모 구름층

- [ ] **단계 13: 대규모 평면 구름층 - 구현 완료, 사용자 검증 대기**
  - 유한 AABB를 XZ 경계 없는 Y 평면층으로 교체
  - 바닥 1.5km, 상단 4.5km, Weather 32km, View 50km 기준
  - 40~50km 거리 fade와 Light 20km 상한
  - 구름 아래·내부·위 카메라 지원
  - km 광학값과 에너지 보존형 단일산란 알베도 보정
  - 8~20km Detail LOD와 20km 이후 noise 호출 생략

## 단계 13 승인 후 실시간 최적화

- [ ] 단계 9: 레이 마칭 기본 최적화와 계측 - 소규모 AABB 성능 기준 실패로 보류
- [ ] 단계 10: 저해상도 렌더링과 depth-aware 업샘플링
- [ ] 단계 11: Jitter와 Temporal Reprojection
- [ ] 단계 12: 근거리 ray march와 Cloud Shadow Map 혼합

단계 9는 단계 13의 Optimization Off 출력을 새 기준으로 삼는다. 새 벤치마크는
`GroundZenithDense`, `GroundHorizonDense`, `SparseHorizon`, `DepthOccluded`,
`InsideLayer`이며, 수평 Dense p95 15% 개선과 나머지 3% 이내 회귀가 기준이다.

## 최종 통합

- [ ] 단계 14: 대기와 지면 조명 통합
- [ ] 단계 15: 품질 프리셋, 성능 조정과 신규 캐시 확정

구형 행성, 지상→우주 전환과 원점 재배치는 포트폴리오 목표에서 제외한다. 단계 15의
최종 목표는 Release High 1920×1080에서 Present 제외 전체 GPU p95 16.67ms 이하다.
