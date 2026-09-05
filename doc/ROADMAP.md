# 단계별 로드맵

단계 번호와 승인 이력은 학습 과정을 설명하기 위해 보존한다. 최종 런타임 실행 순서는 다음으로 단순화했다.

```text
0~8 기초 → 13 Planar Open World → 9 High 최적화
        → 12 Deep Cache → 14 Atmosphere/HDR → 15 High 단일화
```

Stage 10과 11은 구현·검증 이력만 남으며 실행 경로에는 포함되지 않는다.

## 승인된 기반

- [x] 단계 0~8: D3D11 기반, 교차, Base/Detail/Weather, 태양·Phase·환경광
- [x] 단계 13: meter 단위 Planar layer, 대규모 Weather/Texture3D, 단일 포트폴리오 장면 — 2026-08-17
- [x] 단계 9: support precheck, empty skip, distance step, early exit, deterministic cone — 2026-08-19
- [x] 단계 12: Balanced512 Near/Far Deep Cache와 표면 그림자 — 2026-08-25
- [x] 단계 14: 물리 대기 LUT, 지면 조명, HDR와 Tone Map — 2026-08-28

## 폐기한 실험

- Stage 10 저해상도 Cloud RT와 Nearest/Bilinear/Joint 업샘플링은 Full-resolution High 단일화에서 삭제했다.
- Stage 11 jitter·Temporal reprojection은 사용자가 High 비교에서 실익이 없다고 판단해 history와 함께 삭제했다.
- Low/Medium/Custom 품질, Detail 거리 LOD, Capture/Reference, 별도 Resolve/Composite, NTE Rim을 삭제했다.
- Desert 콘셉트와 방향성 Cirrus shape/noise/UI를 삭제했다. 구름 타입은 Stratus/Cumulus/Mixed만 유지한다.
- Stage 9의 Balanced/Conservative/early-exit 선택 UI는 삭제하고 승인된 수치를 High 상수로 고정했다. early exit 알고리즘 자체는 항상 사용한다.

해당 기능을 다시 활성화하는 호환 분기나 파일 migration은 유지하지 않는다. 과거 동작을 조사할 때만 Git tag와 `doc/changes`의 짧은 이력을 참고한다.

## 현재 단계: High 단일화·프로젝트 간소화

- [x] Stage 15B dirty 상태 checkpoint commit 보존
- [x] Full-resolution direct Cloud PS를 유일한 구름 렌더 경로로 전환
- [x] Stage10/11, Rim, LOD, Capture/Reference와 관련 GPU 자원 삭제
- [x] High 수치 고정과 OptimizationCB 삭제
- [x] ShadowCB b8, Stage14CB b9로 register 압축
- [x] Cirrus/Desert 제거, Stratus/Cumulus/Mixed와 세 scene concept 정리
- [x] motion을 제외한 formation Custom schema 2와 schema 40 snapshot, schema 1 읽기 이관
- [x] manifest 기반 영향 셰이더 원자적 핫 리로드
- [x] Debug/Release 전체 자동 검증 및 1080p 성능 gate 기록 — 2026-09-01
- [ ] 사용자 Release 1920×1080 렌더 검증
- [ ] 사용자 명시적 최종 승인

## 승인 전 금지

- `main` 직접 병합 또는 push
- stage 승인 tag 생성
- 사용자가 하지 않은 화면 미학 판정을 에이전트가 대신 승인

승인 뒤 브랜치·PR·tag 절차는 [CONTRIBUTING.md](CONTRIBUTING.md)를 따른다.
