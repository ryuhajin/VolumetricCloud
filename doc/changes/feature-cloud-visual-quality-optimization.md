# feature/cloud-visual-quality-optimization 변경 기록

## 1. 목표와 배경

- 720p 60 FPS 기준을 유지하면서 half-resolution 외곽 번짐, 씻긴 내부 명암, 균일한 적운 형상을 개선한다.
- 목표는 선명한 실루엣, 태양 가시성에 따른 밝은 가장자리, 깊은 자기 그림자, 높이에 따라 달라지는 적운 billow다.

## 2. 기존 구현과 관찰된 문제

- half-resolution 출력에 하늘까지 합성된 색을 저장하고 bilinear 확대해 구름/하늘 불연속이 섞였다.
- temporal rejection은 깊이만 사용해 얇은 경계의 투과율 변화에 둔감했다.
- 8 local + 4 far 직선 light march와 `sqrt(visibility)` 반복 혼합은 호출 비용과 그림자 대비의 균형이 좋지 않았다.
- base/detail 채널 가중치가 높이에 무관해 하단 연결성과 상단 둥근 융기의 역할 분리가 약했다.

## 3. 원인 분석과 근거

- 낮은 해상도 인상은 128³ 볼륨 자체보다 half-resolution 색 합성과 bilinear 복원에서 먼저 발생한다.
- 다중 산란 에너지를 반복 제곱근으로 복원하면 깊은 그림자까지 빠르게 밝아져 입체감이 줄어든다.
- 적운은 하단의 연결된 덩어리와 중·상단의 Worley billow가 서로 다른 높이 응답을 가져야 한다.

## 4. 검토한 대안

- 256³ base/detail은 RGBA8 메모리와 생성비가 8배이며 현재 view sampling이 추가 대역을 보존하지 못해 제외했다.
- curl/domain warp와 Beer shadow map은 효과가 크지만 별도 샘플·리소스와 품질 단계 설계가 필요해 후속으로 남겼다.
- full-resolution 고정 렌더는 비교 기준으로 유지하되 기본 경로로 쓰기에는 기존 측정상 예산을 초과한다.

## 5. 선택한 해결 방법

- 선형 premultiplied cloud scattering와 transmittance를 분리하고 full-resolution에서 분석적 하늘을 합성한다.
- `(첫 구름 거리, transmittance)` RG16 metadata를 이용한 2×2 경계 보존 복원과 temporal confidence를 사용한다.
- 5개 근거리 cone detail sample, 1개 원거리 macro sample, 감쇠된 두 번째 scattering octave, 위쪽 macro AO를 사용한다.
- 높이별 base 가중치와 하단 detail 반전으로 적운의 수직 형태 역할을 분리한다.

## 6. 실제 수정 내용

- `CloudAtmosphere.hlsli`에 태양 방향, 하늘색, tone mapping을 공통화하고 hot reload/source hash 대상에 넣었다.
- half/current/history metadata를 `R16G16_FLOAT`로 바꾸고 resize와 history reset 검사를 갱신했다.
- `Resolved opacity`, `Ambient occlusion`, `Temporal history confidence` 모드를 F1에 추가했다.
- `CloudParameters/CloudCB`에 cone 반경, AO 강도, 다중 산란 extinction/eccentricity 감쇠를 추가하고 프리셋 직렬화와 UI를 맞췄다.
- `Cumulus Wide Showcase`는 5+1 light 구성과 새 기본값을 사용한다.

## 7. 캐시·호환성·성능 영향

- 상수버퍼가 224바이트에서 240바이트로 바뀌고 공통 HLSL source hash가 변경되어 캐시를 v8로 올렸다.
- 128³ base/detail과 512² weather의 크기와 format은 유지한다.
- temporal metadata는 R16에서 RG16으로 늘어 half target 약 0.88 MiB, 두 full-resolution history 합계 약 3.52 MiB가 추가된다.

## 8. 테스트 및 실행 결과

- Debug 빌드: 통과.
- Debug `--run-code-tests`: 31개 항목 통과. runtime SM5 셰이더 bundle 생성, 균일 복원, 산란 에너지 상한, D3D11 debug warning 검사 포함.
- Release 빌드: 통과. post-build의 선택적 `pwsh.exe` 탐색 메시지는 있었지만 MSBuild 반환 코드 0과 실행 파일·셰이더 복사는 정상이다.
- v8 기본 flat bundle을 재생성했고 `--cache-smoke-test` 반환 코드 0을 확인했다.
- 최종 Debug/Release 재빌드: 모두 반환 코드 0.
- 최종 Release `ctest --test-dir build -C Release --output-on-failure`: 2/2 통과. `CacheSmoke` 3.22초, `CodeTests` 49.48초.
- 1280×720 temporal benchmark의 3.8km/128, 120표본은 raymarch median 8.8950ms, reconstruction median 0.4762ms, GPU total median 9.7736ms, p95 11.6204ms다. 목표 16.0/16.67ms를 모두 통과했다.
- 같은 실행의 16km/128은 GPU total median 14.0467ms, p95 16.0993ms다. 전체 8개 조합은 유한한 120표본을 기록했다.

## Weather Field·Placement Texture 후속 개선

### 이유와 선택

- slab은 레이 적분 구간만 정하지만 기존 weather/base가 높이 전체에 비슷한 XZ 지지를 제공해 위·아래에서 개별 중심과 반경이 읽히지 않았다.
- 명시적 AABB/ellipsoid 목록은 편집성은 좋지만 다수 교차와 가속 구조가 필요해 이번 넓은 하늘 범위에서는 제외했다.
- weather는 구름군, placement는 개별 중심·반경·높이, 3D noise는 표면 조각이라는 세 계층으로 분리했다.

### 구현

- weather R을 저주파 cluster mask와 중주파 variation의 곱으로 변경했다.
- 같은 `CSWeather` dispatch가 512² RGBA8 placement를 `u4`에 생성한다. periodic cell hash가 중심 jitter, 활성, 반경, 높이와 상단 profile을 정하고 주변 3×3 후보 중 가장 가까운 중심을 선택한다.
- PS `t3` placement를 weather보다 먼저 평가한다. 빈 support는 weather와 base/detail을 생략한다.
- placement B로 중심별 두께를 만들고 높이 0.45 이후 A profile에 따라 반경을 줄인다. `placementStrength=0`은 기존 밀도 수식과 같은 비교 경로다.
- `CloudCB`를 272바이트로 확장하고 8개 파라미터, F1 placement preview와 Support/Radius/Height 모드, preset 저장·로드를 연결했다.
- `Cumulus Wide Showcase`의 `anvilStrength`는 0.35에서 0.08로 낮췄다.
- placement resource와 manifest size/format을 포함하도록 캐시를 v9로 올리고 `placement.vcnoise`를 추가했다.

### 자동 검증

- 동일 seed byte 결정성, X/Y periodic seam, RGBA8 규격·범위·분산, 활성/빈 영역, 상단 support 단조 감소, strength 0 호환, R=0 제거와 bounds 포함 관계를 코드 테스트한다.
- placement 생성 파라미터 hash와 runtime 파라미터 hash 제외, preset 8필드 round-trip과 clamp를 검사한다.
- v9 cache byte round-trip, placement 누락·손상·format 불일치와 v8 manifest 거부를 검사한다.
- Debug/Release 빌드, Debug code test의 D3D11 warning/error 검사, Release CTest 2개, v9 flat bundle cache smoke가 통과했다.
- Release 1280×720 temporal 120표본에서 3.8km/128 GPU total median 2.1371ms, p95 4.8128ms, 16km/128 median 4.0422ms, p95 5.1128ms를 기록해 16.0/16.67ms 기준을 통과했다.
- 최종 화면의 중심, 평평한 하단, 좁아지는 상단, grid/seam, popping과 ghosting은 사용자가 직접 승인한다.
- 심미적 자동 평가는 수행하지 않는다. 사용자가 고정 카메라에서 외곽, 림, 내부 음영, ghosting을 승인해야 한다.

## 9. 남은 문제와 후속 개선

- scene depth 교차, 구형 대기층, ground contribution과 Beer shadow map은 범위에서 제외했다.
- curl/domain warp와 256³ 볼륨은 현재 단계의 성능 결과와 사용자 화면 승인 뒤 재평가한다.
