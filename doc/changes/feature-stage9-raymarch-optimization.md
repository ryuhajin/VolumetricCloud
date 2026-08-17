# 단계 9 View/Light 기본 최적화

## 기준과 범위

- 기준: `stage13-approved` / `7f07f64`, 2026-08-17 사용자 승인 평면층.
- 구현: Reference/Optimized PS 분리, View support precheck·empty search·early exit·거리 step,
  deterministic Light cone, F1 비교 UI, schema 31, 자동 화질·성능 측정기.
- 제외: 저해상도, temporal jitter/reprojection, Light Cache, 지면 Cloud Shadow Map.

## 주요 결정

- View와 Light의 고정 비율을 만들지 않고 독립 품질 축으로 둔다.
- Approved Reference는 100m/512 View와 250m/80 Straight Light를 별도 PS로 보존한다.
- cone은 고정 golden angle과 구간 길이 가중치를 써 시간 노이즈 없이 주변 밀도를 읽는다.
- 6탭 Light의 동일 비용 스윕 결과 4°/3°는 실패하고 `2°`, 원거리 구간 `77%`가 세 외형에서
  처음 통과해 Balanced에 반영됐다.
- 사용자 렌더 승인 전에는 자동 테스트를 통과한 후보가 있어도 시작 기본을 Reference로 유지한다.
- 일반 외형 시작은 Stratus, Full Open World 비교 복원값은 Dense Mixed다.

## 검증

- `Stage9OptimizationMath`: 64바이트 ABI, preset, 가변 step 전체 길이, 분석적 Beer-Lambert,
  coarse 되감기, support 0 조건, 5/6/8/12 cone weight 합과 finite 결과.
- `Stage9OptimizationSmoke`: Dense/Stratus/Cumulus의 Final Density, View τ, Light T finite 및
  SSIM/RMSE/MAE/P99 보고.
- `--stage9-performance-test`: Release 1920×1080, 120 warmup, 원시 timestamp 600개,
  일곱 장면과 네 preset을 CSV/JSON으로 기록.
- 2026-08-17 중간 통합에서 `NoiseLab.hlsl`이 b9 선언 없이 `Noise.hlsli`를 읽어 X3004가
  발생했다. Optimization header를 Noise 공통 include로 이동한 뒤 FoundationSmoke를 통과했다.

## 자동 결과와 승인 상태

- Balanced 화질: Dense/Stratus/Cumulus Light T P99 `0.02814/0.01250/0.02899`, 모든 View
  SSIM `0.999999` 이상, RMSE `0.000468` 이하.
- Balanced 성능: 일곱 장면 최대 p95 `8.98ms`; Cumulus Horizon은 Reference
  `19.83ms → 8.98ms`로 약 `54.7%` 개선.
- 자동 화질·성능 gate는 통과했다. 사용자 렌더 승인 전 시작 기본은 Approved Reference다.
