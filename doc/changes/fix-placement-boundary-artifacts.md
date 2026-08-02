# Placement 경계 절단면 완화

## 배경과 문제

`Edge Softness`를 0에 가깝게 낮추거나 `Placement Density`를 1로 높였을 때 구름에 수직 절단면이 나타났다. placement는 XZ 평면의 footprint이므로 불연속적인 경계가 Y 방향으로 이어지면 slab이나 AABB의 면처럼 보일 수 있다.

기존 생성기는 가장 가까운 활성 중심 하나의 G/B/A를 저장했다. 서로 겹친 중심의 정규화 거리가 같아지는 Voronoi 소유권 경계에서 R은 연속이어도 높이 B와 상단 profile A가 갑자기 다른 중심 값으로 바뀌었다. 또한 낮은 Edge Softness는 placement support를 거의 hard step으로 만들어 512² footprint를 최종 밀도에 날카롭게 곱했다.

## 대안과 선택

- UI 최소값만 높이면 기본 화면은 가릴 수 있지만 0의 의미와 placement 해상도 문제를 해결하지 못한다.
- base 3D noise로 경계를 추가 침식하면 자연스럽지만 구름 점유율과 실루엣이 크게 바뀐다.
- 중심별 월드 오브젝트로 전환하면 편집성은 높지만 현재 넓은 field와 캐시 구조를 벗어난다.

이번 수정은 footprint R은 보존하면서 겹친 중심 속성을 연속 혼합하고, 사용자가 0을 선택해도 최소 texel 폭을 보장하는 자동 AA를 적용한다. base-noise 경계 침식은 후속 후보로 남긴다.

## 구현

- 각 활성 후보의 `candidateR = saturate(1 - normalizedDistance)`를 구한다.
- 출력 R은 모든 후보 R의 최댓값으로 유지한다.
- G/B/A는 footprint보다 15% 넓은 속성 혼합 반경에서 `smoothstep(saturate(1.15-normalizedDistance))` 가중 평균으로 바꾼다. 원래 support만 섞었을 때 R≈0.21로 거의 맞닿은 두 중심에서 117/255 점프가 남아, 3×3 후보 안전 범위 안에서 혼합 반경만 확장했다.
- support가 0인 texel은 G/B/A도 0으로 저장한다.
- G에서 실제 반경을 복원하고 `cellCount / (512 × radius)`로 placement 한 texel의 정규화 거리 폭을 계산한다.
- 자동 edge는 1.5 texel, 최대 0.35이며 최종 폭은 사용자 Edge Softness와 자동 폭의 최댓값이다.
- F1의 0은 `Auto AA`로 표시하고 최소 반경이 4 texel 미만이면 undersampling 경고를 표시한다.
- CloudCB는 272바이트를 유지한다. placement 채널 의미가 바뀌므로 캐시는 v10으로 올린다.

## 사용자 영향

- Density가 높아도 겹친 중심 사이에서 높이와 taper가 급변하지 않는다.
- Edge Softness 0은 가능한 한 선명하되 placement texel alias를 막는 최소 폭을 유지한다.
- Placement Radius/Height 디버그는 단일 중심 속성이 아니라 겹친 중심의 혼합 G/B를 표시한다.
- 기본 Edge Softness 0.15와 Density 0.62는 유지한다.

## 검증

- 동일 seed 결정성, periodic seam, 채널 범위와 support 0의 G/B/A=0을 검사한다.
- Density 1 맵에서 R≥0.2인 인접 texel의 B/A 변화가 0.35 이하인지 검사한다.
- 두 중심의 동률 경계에서 가중 혼합이 유한하고 연속적인지 CPU 수식으로 검사한다.
- 자동 edge가 기본 반경에서 최소 1.5 texel이며 cell 수 증가·반경 감소 시 줄어들지 않는지 검사한다.
- v10 cache round-trip과 v9 manifest 거부를 검사한다.
- 실제 수직 절단면, grid, seam과 이동 중 popping은 사용자가 위·아래·수평 시점에서 승인한다.

구현 후 Debug/Release 빌드, Debug D3D11 warning/error 검사, Release CTest 2개와 v10 cache smoke가 통과했다. Density 1의 R≥0.2 인접 B/A 최대 점프는 32/255였다. Release 1280×720 temporal 120표본의 GPU total은 3.8km/128에서 median 1.6579ms, p95 2.4730ms이고 16km/128에서 median 2.5395ms, p95 3.1150ms로 16.0/16.67ms 기준을 통과했다.
