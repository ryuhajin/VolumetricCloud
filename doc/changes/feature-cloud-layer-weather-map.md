# feature/cloud-layer-weather-map 변경 기록

## 1. 목표와 배경

단일 AABB 안의 고립된 구름을 넓은 수평 구름층으로 확장해, 여러 구름군과 빈 하늘이 수평선까지 이어지는 쇼케이스 화면을 만든다.

## 2. 기존 구현과 관찰된 문제

2026-07-27 사용자 캡처의 구름은 형태와 조명 코드가 동작하지만 하나의 흐린 덩어리로 보였다. 원점의 `4.0 × 2.4 × 2.4` AABB가 분포 범위를 제한하고, 구름과 배경의 명도 차이가 작아 거리와 태양 방향이 읽히지 않았다.

## 3. 원인 분석과 근거

- 3D 노이즈는 국소 밀도 형태만 제공하며 넓은 지역의 구름 유무와 종류를 결정하지 않는다.
- 고정 AABB는 수평선 방향 레이를 일찍 종료해 광역 구름층을 표현할 수 없다.
- 높은 ambient와 근사 다중 산란 값은 내부 그림자를 밝게 만들어 밀도 구조를 약하게 보이게 한다.
- 단순 하늘 그라데이션에는 수평선 haze와 태양 glow가 없어 구름의 거리 단서가 부족하다.

## 4. 검토한 대안

- AABB 확대: 구현은 단순하지만 모서리와 박스 종료 거리가 보이고 월드 좌표 분포를 만들기 어렵다.
- 구형 대기층: 장거리 비행에는 적합하지만 이번 화면 목표에는 좌표·정밀도·곡률 검증 비용이 크다.
- 평면 구름층: 지상 시점 쇼케이스를 가장 빠르게 만들 수 있어 이번 브랜치에서 채택한다.

## 5. 선택한 해결 방법

상·하단 평면으로 레이 구간을 구하고 최대 거리로 수평선을 제한한다. 512² RGBA weather map으로 coverage, type, base-height, thickness를 제어하며, 기존 3D 캐시는 월드 XZ와 정규화 높이 좌표에서 반복 샘플링한다.

## 6. 실제 수정 내용

- `CloudParameters`/`CloudCB`를 176바이트로 확장하고 구름층·weather·수평선 필드 12개를 추가했다.
- AABB 교차를 평면 구름층 교차로 교체하고 view/light ray가 같은 구간 규칙을 사용하게 했다.
- 512² RGBA8 weather compute, Texture2D SRV/UAV와 저장·복원을 추가했다.
- weather R/G/B/A 진단 모드와 Inspector preview를 추가했다.
- weather 빈 영역 4배, 후보 영역 2배, 실제 밀도 영역 1배 가변 진행을 구현했다.
- 분석적 하늘, horizon haze, sun glow와 지수 tone mapping을 추가했다.
- DX11 timestamp/disjoint query 두 세트로 stall 없는 GPU 시간을 UI에 표시한다.
- `Cumulus Wide Showcase`와 구름층 아래에서 수평선을 올려다보는 기본 카메라를 추가했다.
- 모든 파라미터의 튜닝 순서를 로컬 전용 `notes/interview/cloud-parameter-guide.md`에 기록했다.

## 7. 캐시·호환성·성능 영향

weather map과 compute shader를 bundle에 추가해 캐시를 v5로 올렸다. base 8 MiB, detail 1 MiB, weather 1 MiB로 텍스처 캐시는 총 약 10 MiB다. 캐시 셰이더는 6개에서 7개, 생성 dispatch는 2개에서 3개가 됐다. `weatherSeed`도 생성 hash에 포함한다.

## 8. 테스트 및 실행 결과

최종 검증:

- 직접 fxc `ps_5_0` main과 `cs_5_0` CSWeather 컴파일 성공
- 2026-07-27 기준 Debug/Release C++ 빌드 재확인 성공
- 확장 코드 테스트에서 weather RGBA 분산, periodic seam, slab 교차와 weather hash 검사 통과
- v5 bundle 생성 성공
- Release `ctest` 2/2 통과
  - `VolumetricCloud.CacheSmoke` 5.39초
  - `VolumetricCloud.CodeTests` 52.17초
- GPU timestamp query UI 구현 완료, 33.3ms 합격값은 사용자 장비에서 확인 대기

시각 관찰:

- 날짜: 2026-07-27
- 프리셋: `Cumulus Wide Showcase`
- 카메라: 구름층 아래 기본 오빗 카메라
- 실행 구성: Release, 1280×720 렌더 영역
- 캡처: `cloud-layer-approval-baseline.png`
- 관찰: 박스 경계 없이 구름층이 수평선 전체로 확장되고, weather에 따른 빈 하늘과 밝은 태양측 영역이 생겼다. 화면 왼쪽은 두꺼운 구름 하단과 내부 음영, 오른쪽은 얇은 투광 영역으로 분리된다. 기존 단일 AABB보다 범위와 거리 단서는 개선됐지만 목표 화면 대비 개별 적운의 수직 발달과 선명한 silver lining은 부족하다. 이 캡처는 다음 브랜치의 회귀 기준이며 심미적 합격 판정은 아니다.
- 사용자 최종 승인: 대기

## 9. 남은 문제와 후속 개선

- 구형 대기층과 지구 곡률
- 장면 depth 합성
- 반해상도 temporal reprojection
