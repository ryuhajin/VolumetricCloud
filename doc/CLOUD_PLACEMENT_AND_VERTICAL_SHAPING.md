# 구름 배치 필드와 수직 형상

## 왜 slab을 유지하는가

초기 학습 단계에서는 AABB 하나의 입·출구를 구해 그 내부를 레이마칭했다. 이후 수십 km의 하늘을 연속적으로 다루고 카메라가 구름층 위·아래를 오갈 수 있도록 전역 하단·상단 평면으로 이루어진 slab으로 바꿨다. slab은 교차 구간만 정하므로 구름을 납작하게 만드는 원인이 아니다. 실제 형태는 그 구간 안에서 평가하는 밀도 함수가 결정한다.

기존 밀도는 weather와 3D noise가 같은 XZ 위치에서 높이 전체에 비슷한 지지를 제공했다. 이 때문에 위·아래에서 개별 중심과 반경이 잘 읽히지 않고, 수평 시점에서는 연속된 구름 카펫처럼 보일 수 있었다. AABB가 더 입체적으로 보였던 이유는 상자가 암묵적으로 개별 구름의 유한한 XZ 범위를 제공했기 때문이다. 이번 변경은 명시적 월드 오브젝트 없이 그 역할을 placement field로 복원한다.

## 세 계층의 역할

| 계층 | 저장 내용 | 담당 범위 |
|---|---|---|
| weather 512² RGBA8 | coverage, cloud type, base height, thickness | 구름군과 맑은 광역 기상 구역 |
| placement 512² RGBA8 | 중심 support, 반경, 높이, 상단 profile | 개별 적운의 중심·크기·수직 변형 |
| base/detail 128³ RGBA8 | Perlin-Worley 거시 형태와 Worley 침식 | cauliflower 표면과 작은 경계 변화 |

weather R은 저주파 cluster mask와 중주파 variation의 곱이다. 따라서 넓은 맑은 영역과 구름군이 먼저 정해진다. placement는 그 구름군 안에 개별 중심을 만든다. 3D noise는 살아남은 support 내부만 조각한다.

## placement 생성

weather tile을 한 축 `placementCellCount`개의 periodic cellular grid로 나눈다. 각 texel은 주변 3×3 cell을 조사한다. cell hash는 다음 값을 결정한다.

- 중심 jitter
- 활성 여부
- cell 크기 대비 반경
- 높이 변이
- 상단 taper profile

중심 활성 확률은 `placementDensity`와 해당 중심에서 다시 평가한 weather coverage를 함께 사용한다. R은 원래 반경 안의 proximity 최댓값이다. G/B/A는 속성 혼합 반경만 15% 넓힌 `smoothstep(saturate(1.15-normalizedDistance))` 가중 평균이다. footprint 점유율은 바꾸지 않으면서 거의 맞닿은 중심도 경계에 도달하기 전에 높이와 상단 profile을 섞는다.

| 채널 | 의미 |
|---|---|
| R | 중심 1, 반경 경계와 빈 영역 0인 proximity |
| G | 겹친 중심을 support로 혼합한 반경 변이 |
| B | 겹친 중심을 support로 혼합한 높이 변이 |
| A | 겹친 중심을 support로 혼합한 상단 taper/profile 변이 |

맵과 hash는 양 축에서 periodic이다. `weatherSeed`, cell 수, density, 최소·최대 반경이 바뀌면 compute로 weather와 placement를 함께 다시 굽는다.

## 평평한 하단과 좁아지는 상단

placement B는 weather가 만든 지역 두께에 중심별 높이 scale을 곱한다.

```text
heightScale = max(0.25,
    1 + (placement.b * 2 - 1) * placementHeightVariation)
localTop = localBase + weatherThickness * heightScale
```

정규화 높이 `h`가 0.45 이하이면 허용 반경은 원래 반경이다. 그 위에서는 smoothstep으로 축소량을 증가시킨다.

```text
upper = smoothstep(0.45, 1.0, h)
profile = lerp(0.85, 1.15, placement.a)
radiusScale = max(0.25,
    1 - upper * placementTopShrink * profile)
```

R에서 복원한 중심 거리와 `radiusScale`을 비교하고 경계를 부드럽게 만든다. G로 실제 반경을 복원해 placement map의 1.5 texel에 해당하는 자동 AA 폭을 계산하며 최종 폭은 이 값과 `placementEdgeSoftness` 중 큰 값이다. 따라서 Edge Softness 0도 hard cut이 아니라 해상도에 맞춘 최소 AA를 사용한다. 하단은 비교적 평평하고 중단은 넓게 유지되며 상단은 중심으로 좁아진다.

`placementStrength=0`이면 support가 항상 1이고 placement 높이 변화도 최종 밀도에 영향을 주지 않는 비교 경로다. `placementStrength=1`이고 R이 0이면 weather와 두 3D texture 샘플을 건너뛴다.

## 파라미터

| F1 이름 | 기본값 | 범위 | 역할 | 재생성 |
|---|---:|---:|---|---|
| Cell count | 16 | 8–48 | tile 한 축의 후보 중심 수 | 예 |
| Placement density | 0.62 | 0.15–1.0 | 후보 중심 활성 확률 | 예 |
| Radius min | 0.35 | 0.20–0.80 | cell 대비 최소 반경 | 예 |
| Radius max | 0.85 | 0.30–1.00 | cell 대비 최대 반경 | 예 |
| Height variation | 0.35 | 0–0.75 | 중심별 두께 차이 | 아니요 |
| Top shrink | 0.55 | 0–0.75 | 상단 반경 축소 | 아니요 |
| Edge softness | 0.15 | 0–0.35 | support 경계 페이드. 0은 Auto AA | 아니요 |
| Placement strength | 1.0 | 0–1 | 기존 field와 혼합 | 아니요 |

UI와 preset load는 `Radius min ≤ Radius max ≤ 1`을 강제한다.

권장 튜닝 순서는 Cell count와 Density로 중심 수를 맞추고, Radius min/max로 겹침을 조절한 뒤 Height variation과 Top shrink로 실루엣을 만든다. 마지막에 Edge Softness를 조절한다. 0은 자동 AA만 사용하며 일반 장면은 0.10–0.20을 권장한다. 최소 반경이 4 placement texel 미만이면 F1 경고에 따라 cell 수를 줄이거나 Radius min을 높인다.

## 디버그 모드

- `Placement Support`: ray가 만난 최대 높이 조건부 support. 상단에서 반경이 줄어드는지 확인한다.
- `Placement Radius`: ray에서 최대 support를 만난 위치의 혼합 G 채널.
- `Placement Height`: 같은 위치의 혼합 B 채널.
- Noise Inspector의 placement RGBA preview: cellular grid, 원형 stamp, 반복 seam과 채널 분산을 확인한다.

## 비용과 early-out

placement map은 1 MiB이고 weather와 같은 512² dispatch에서 생성한다. view/light/AO sample마다 2D fetch 한 번이 먼저 추가되지만, 빈 support에서는 weather와 base/detail 3D fetch를 제거한다. 상단 taper도 texture 재생성 없이 산술 연산으로 평가한다. 전역 slab 교차와 최대 march 거리는 그대로다.

## 명시적 도형 목록과 비교

| 방식 | 장점 | 단점 |
|---|---|---|
| AABB/ellipsoid 인스턴스 목록 | 개별 중심과 bounds가 명확하고 국소 구름 편집이 쉽다 | 많은 인스턴스의 ray 교차, 정렬·가속 구조와 경계 blending이 필요하다 |
| placement texture | 한 번의 periodic lookup으로 대규모 군집을 만들고 weather/캐시에 자연스럽게 결합된다 | 개별 구름을 월드 오브젝트처럼 선택하기 어렵고 texture 해상도와 반복 tile의 제약이 있다 |

이번 프로젝트는 넓은 하늘, DX11 SM5, 720p 60 FPS가 우선이므로 placement texture를 선택했다. 영웅 구름을 수동 배치하거나 상호작용해야 할 때는 placement field와 소수의 ellipsoid/AABB 목록을 혼합하는 방식이 후속 후보이다.

## 검증과 시각 승인

코드 테스트는 동일 seed byte 결정성, periodic seam, RGBA8 규격과 채널 범위, 활성·빈 영역과 비퇴화 분산, Density 1 속성 연속성, 자동 edge 폭, 상단 support 단조 감소, strength 0 호환, R=0 제거, local bounds와 전역 envelope 포함 관계를 검사한다. 캐시는 placement 누락·손상·format 불일치와 v9 manifest를 거부하고 v10 byte round-trip을 검사한다.

최종 화면은 사용자가 같은 시간과 `Cumulus Wide Showcase`에서 위·아래·수평 시점을 직접 승인한다. 확인 항목은 개별 중심과 빈 하늘, 평평한 하단, 넓은 중단, 좁아지는 cauliflower 상단, grid/seam, 이동 중 popping과 temporal ghosting이다.
