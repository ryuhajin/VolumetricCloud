# feature/view-step-quality 변경 기록

## 1. 목표와 배경

- 두꺼운 광역 구름에서 128 view step보다 높은 샘플 수가 경계와 내부 디테일을 개선하는지 사용자가 직접 비교할 수 있게 한다.
- 캡처 대신 고정된 장면의 DX11 timestamp와 프로세스 반환 코드로 128/160/192/256 step의 비용을 반복 측정한다.
- 기본 품질과 기존 프리셋의 성능은 바꾸지 않고 선택 가능한 고품질 범위만 확장한다.

## 2. 기존 구현과 관찰된 문제

- F1과 HLSL이 모두 128 step으로 제한되어 그 이상의 품질을 실험할 수 없었다.
- HUD는 현재 한 설정의 EMA만 보여주므로 여러 step을 같은 조건에서 반복 비교하고 결과를 보존하기 어려웠다.
- 화면 캡처는 창 위치와 입력 자동화에 영향을 받고 심미적 합격을 코드로 판정할 수도 없었다.

## 3. 원인 분석과 근거

- view step을 늘리면 `baseDt=(t1-t0)/steps`가 작아져 밀도 경계를 더 자주 검사하지만, 실제 반복 수는 weather 기반 4×/2×/1× 진행과 투과율 조기 종료의 영향을 받는다.
- 따라서 GPU 시간은 step에 정비례하거나 항상 단조 증가하지 않는다. 작은 `dt`가 밀도 판정과 조기 종료 위치를 바꾸고 GPU loop divergence와 clock 변동도 측정값에 영향을 준다.
- 품질 평가는 수치만으로 결정할 수 없지만 GPU timestamp는 동일 장면에서 각 설정의 비용을 비교하는 근거가 된다.

## 4. 검토한 대안

- 256을 새 기본값으로 설정: 기존 프리셋 성능이 즉시 바뀌므로 제외했다.
- light step도 함께 증가: 어떤 변화가 view sampling 때문인지 분리할 수 없어 제외했다.
- HUD 값을 사람이 기록: 워밍업, 표본 수와 장면 고정을 보장하기 어려워 자동 CSV를 선택했다.
- 성능이 step과 단조 증가해야 합격 처리: adaptive march와 GPU 변동이 있는 구현에 잘못된 실패를 만들 수 있어 적용하지 않았다.

## 5. 선택한 해결 방법

- F1 범위를 48~256으로 넓히고 128/160/192/256 빠른 선택 버튼을 제공한다.
- HLSL clamp와 정적 loop 상한을 256으로 확장하되 기본 `Cumulus Wide`는 128을 유지한다.
- `--benchmark-view-steps`가 3.8/16 km와 네 step의 8개 조합을 고정된 1280×720 장면에서 측정한다.
- 각 조합은 30 frame 워밍업 후 유효한 timestamp 120개를 모으며 600 frame 안에 확보하지 못하면 실패한다.
- 결과는 `%LOCALAPPDATA%\VolumetricCloud\benchmarks\view-step-quality.csv`에 mean/median/p95와 이론 FPS로 저장한다.

## 6. 실제 수정 내용

- Sampling UI에 48~256 슬라이더와 네 비교 버튼을 추가했다.
- 메인 pixel shader의 view loop를 최대 256회로 확장했다.
- timestamp query별로 benchmark 여부와 같은 frame의 CPU render 시간을 기록해 유효한 query가 resolve될 때 한 표본으로 합친다.
- benchmark 중 HUD를 끄고 `Present(0)`을 사용하며, 고정 카메라·시간·Wide 파라미터로 frame을 반복한다.
- 종료 코드는 정상 0, timestamp 부족 5, 창 종료 6, 비정상 수치 7, CSV 저장 실패 8이다.

## 7. 캐시·호환성·성능 영향

- `viewSteps`는 기존 int 필드를 그대로 사용하므로 CloudCB 176바이트와 preset 형식은 변하지 않는다.
- cache layout과 생성 노이즈가 같아 버전은 v5를 유지한다.
- main pixel shader가 변경되어 배포 `main_ps.cso`와 manifest source hash만 다시 생성했다.
- 기본값은 128이므로 사용자가 높은 step을 선택하지 않으면 기존 렌더 설정을 유지한다.

## 8. 테스트 및 실행 결과

- 최종 benchmark 실행 코드: 0
- CSV: 8행, 각 120표본, GPU cloud/total과 CPU render 모두 양수·유한값

| 두께 | Step | GPU total median | 128 대비 | GPU total p95 | 이론 FPS |
|---:|---:|---:|---:|---:|---:|
| 3.8 km | 128 | 3.3951 ms | 기준 | 4.0960 ms | 294.5 |
| 3.8 km | 160 | 8.0123 ms | +136.0% | 9.7915 ms | 124.8 |
| 3.8 km | 192 | 7.9759 ms | +134.9% | 8.6600 ms | 125.4 |
| 3.8 km | 256 | 7.3610 ms | +116.8% | 8.9149 ms | 135.9 |
| 16 km | 128 | 2.6122 ms | 기준 | 4.0612 ms | 382.8 |
| 16 km | 160 | 4.5348 ms | +73.6% | 4.7575 ms | 220.5 |
| 16 km | 192 | 7.6262 ms | +191.9% | 8.3671 ms | 131.1 |
| 16 km | 256 | 4.8353 ms | +85.1% | 8.3323 ms | 206.8 |

- 시간의 비단조성은 합격/불합격 조건이 아니다. 동일 실행의 상대 비교이며 GPU clock·adaptive march·조기 종료 영향을 포함한다.
- 2026-07-27 Debug/Release 빌드: 통과
- HLSL `ps_5_0` 독립 컴파일: 통과
- Release `ctest`: 2/2 통과, 47.25초
  - `VolumetricCloud.CacheSmoke`: 2.58초
  - `VolumetricCloud.CodeTests`: 44.65초
- Debug `VolumetricCloud.CodeTests`: 통과, 38.50초
  - D3D11 debug queue warning/error가 있으면 실패하는 검사 포함
- 갱신한 기본 cache v5로 CacheSmoke가 통과해 정상 시작의 runtime compile/noise dispatch가 0회임을 확인했다.
- 렌더 캡처는 생성하지 않았고 step별 심미 품질은 사용자 승인 대기다.

## 9. 남은 문제와 후속 개선

- 사용자가 동일 카메라에서 네 step의 밴딩, 경계 디테일과 깜빡임을 비교해 최종값을 선택해야 한다.
- 256이 유의미한 품질 개선 없이 비용만 늘리면 160 또는 192를 고품질 프리셋 후보로 선택한다.
- 결과 변동이 의사결정을 방해하면 여러 실행의 median과 GPU clock 안정화 절차를 추가한다.
- light step, temporal reprojection과 물리적 최대 step 길이는 이번 브랜치 범위에서 제외했다.
