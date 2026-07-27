# feature/world-space-detail-lighting 변경 기록

## 1. 목표와 배경

- 128과 256 view step의 사용자 화면 차이가 거의 없었던 원인을 샘플 수 밖에서 찾고, 경계 침식과 내부 자기 그림자의 실제 정보량을 늘린다.
- `Cumulus Wide` 1280×720에서 GPU total 33.3ms 이하를 유지하는 품질 우선 프리셋을 목표로 한다.
- 시각 품질은 자동 합격 처리하지 않고 사용자가 같은 카메라에서 최종 승인한다.

## 2. 기존 구현과 관찰된 문제

- view ray는 weather 4×/2×/1× 가변 진행을 사용했지만 fine step이 전체 slab 교차 길이와 `viewSteps`에만 의존했고, 빈 구간에서 밀도로 진입한 위치를 다시 찾지 않았다.
- base와 detail이 같은 UVW를 사용했다. Y가 지역 구름 두께로 정규화되어 두께를 키우면 노이즈 특징도 함께 늘어났다.
- 64³ detail cache에 기본 20/40/80 주파수를 저장해 Nyquist 한계보다 높은 채널 정보가 생성 단계에서 손실될 수 있었다.
- light ray 8개를 현재 지점부터 구름층 출구까지 균등 배치해 두껍거나 비스듬한 구름에서 샘플 간격이 수백 m~수 km가 됐다.
- weather type은 높이 profile만 혼합했고 실제 `localTop`을 올리지 않아 적운형 지역의 수직 성장이 제한됐다.

## 3. 원인 분석과 근거

- view step을 늘려도 입력 density cache에 없는 고주파는 복원할 수 없다.
- 두께 정규화 Y 좌표는 3.8km와 16km 구름에서 같은 텍스처 한 주기를 각각 다른 물리 크기로 늘린다.
- 경계 위치를 건너뛴 뒤 현재 샘플부터 적분하면 silhouette 위치가 coarse step에 잠기고 jitter에 따라 흔들릴 수 있다.
- 내부 입체감은 view ray 수뿐 아니라 각 밀도 지점에서 태양 방향 optical depth를 얼마나 지역적으로 측정하는지에 의존한다.

## 4. 검토한 대안

- view step만 256 이상으로 기본화하는 안은 이미 없는 density 대역과 희박한 light ray를 복원하지 못해 제외했다.
- 매 프레임 절차식 Worley를 직접 계산하는 안은 Texture3D 조회보다 HLSL 컴파일과 픽셀 비용이 커 제외했다.
- 전체 light ray를 고정 50m로 적분하는 안은 두꺼운 구름에서 view sample당 비용이 지나치게 커, 근거리 고해상도와 원거리 저해상도 두 구간을 선택했다.
- 반해상도와 temporal reprojection은 포트폴리오의 원본 720p density·lighting 문제를 먼저 해결하기 위해 후속으로 남겼다.

## 5. 선택한 해결 방법

- base/detail을 독립 월드 공간 XYZ 크기로 샘플링하고 `height01`은 envelope에만 사용한다.
- detail cache를 128³으로 높이고 생성 주파수를 기본 6/12/24/48로 제한한다.
- view fine step을 실제 밀도에서 최대 0.05km로 제한한다. 후보/빈 weather는 목표 간격의 2×/4×와 각각 0.4/1.6km 상한을 사용하고, 정적 2000회 상한과 밀도 진입점 이분 탐색을 결합한다.
- light optical depth를 0.6km 근거리 8회와 나머지 원거리 4회로 분리한다. 50m 간격의 인접 dense view sample 두 개는 light visibility를 공유해 약 100m마다 shadow를 갱신한다.
- weather type으로 실제 두께를 성장시키고 상부 anvil shaping을 추가한다.

## 6. 실제 수정 내용

- `CloudParameters`/`CloudCB`를 224바이트로 확장해 독립 노이즈 크기, 물리적 view step, 두 구간 light march, 적운 성장과 경계 폭을 추가했다.
- detail 생성·리소스·저장·로드 크기를 128³ RGBA8로 통일했다.
- 바람을 월드 km 오프셋으로 계산한 뒤 base/detail 크기에 맞게 변환한다. `windSpeed`의 의미는 tile/초에서 km/초로 바뀌며 weather map 자체는 고정된다.
- F1에 Detail erosion width, base/detail XZ/Y size, Cumulus growth, Anvil strength, Max view step length, Boundary refine steps, Local/Far light 설정을 추가했다.
- 이전 사용자 preset은 누락된 새 필드에 기본값을 사용하며 detail period와 step 개수는 새 안전 범위로 clamp한다.

## 7. 캐시·호환성·성능 영향

- cache v6은 base 8MiB + detail 8MiB + weather 1MiB로 텍스처 원본 데이터가 약 17MiB다. v5의 약 10MiB보다 7MiB 증가한다.
- v5 manifest와 64³ detail은 거부하고 기본 v6 bundle로 폴백한다.
- `detailPeriod`, octave, seed 등 생성 파라미터는 계속 재생성을 요구한다. 월드 크기·마칭·조명 값은 CloudCB로 즉시 반영된다.
- 실제 비용은 density를 만난 view sample 수와 `(local light steps + far light steps)`에 좌우된다. 2000은 고정 실행 횟수가 아니라 안전 상한이다.

## 8. 테스트 및 실행 결과

- HLSL `ps_5_0` 독립 컴파일: 통과, warning 0건
- Debug/Release 빌드: 통과
- cache v6 flat bundle: base/detail 각각 8,388,640바이트, manifest 272바이트. CacheSmoke 통과로 정상 시작 runtime compile/noise dispatch 0회를 확인했다.
- Release `ctest`: 2/2 통과, 48.23초
  - `VolumetricCloud.CacheSmoke`: 2.60초
  - `VolumetricCloud.CodeTests`: 45.60초
- Debug `--run-code-tests`: 종료 코드 0. D3D11 warning/error, detail RGBA 분산, world-space 좌표 불변성, Nyquist, 적운 bounds, adaptive march, light 구간과 신규 preset 10필드 round-trip을 포함한다.
- 1차 benchmark는 3.8km/128에서 GPU total median 59.4406ms, 16km/128에서 123.2512ms였다. 50m 상한을 빈 구간에도 적용해 수평선 ray가 과도하게 반복된 원인을 확인하고, 50m를 실제 밀도에만 적용하도록 수정했다.
- 2차 benchmark는 빈 공간 skip 수정 후 3.8km/128에서 42.6911ms, 16km/128에서 82.1356ms였다. 33.3ms 목표를 위해 인접 dense sample의 light visibility를 한 번 재사용하도록 수정했다.
- 최종 benchmark CSV는 8행이며 각 행에 유효한 timestamp 120개와 양수·유한한 GPU/CPU 시간이 있다.

| 두께 | View step | GPU total median | GPU total p95 | 33.3ms |
|---:|---:|---:|---:|---:|
| 3.8km | 128 | 31.2975ms | 32.5878ms | 통과 |
| 3.8km | 160 | 31.4388ms | 32.3922ms | 통과 |
| 3.8km | 192 | 31.9053ms | 33.4295ms | median 통과 |
| 3.8km | 256 | 32.4726ms | 33.9210ms | median 통과 |
| 16km | 128 | 58.0106ms | 59.8692ms | 실패 |
| 16km | 160 | 58.6496ms | 60.2665ms | 실패 |
| 16km | 192 | 58.5247ms | 60.6382ms | 실패 |
| 16km | 256 | 58.2287ms | 60.3832ms | 실패 |

- 3.8km 기본 Wide/128은 GPU total median 31.2975ms로 목표를 통과했다. 16km는 허용된 실험 범위지만 30 FPS 프리셋은 아니며 후속 최적화가 필요하다.
- 128~256 결과가 비슷한 이유는 수평선 밀도 구간에서 네 설정 모두 `maxViewStepLength=0.05km`에 의해 같은 fine step을 사용하기 때문이다.
- 사용자 시각 승인: 대기

## 9. 남은 문제와 후속 개선

- 반해상도 렌더링, temporal reprojection, VR 양안 최적화, scene depth 합성과 구형 대기층은 제외한다.
- 128과 256 중 최종 기본값은 새 density와 lighting을 사용자가 같은 카메라에서 비교한 뒤 정한다. 현재 기본값은 128이다.
