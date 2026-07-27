# 레이마칭과 구름 밀도

## 레이와 AABB

픽셀 UV를 NDC로 바꾸고 `invViewProj`로 근·원 평면 점을 역투영해 `ro`, `rd`를 만든다. `RayBox`의 slab 교차로 박스 내부 구간 `[t0,t1]`을 구하고 64개 view step의 중앙에서 샘플한다.

## 심리스 periodic 노이즈

Value noise는 lattice cell을 정수 period로 modulo한 뒤 hash한다. Worley는 feature를 만들 cell만 wrap하고, 거리는 원래 이웃 offset으로 계산한다. FBM의 octave마다 좌표 주파수와 period를 함께 두 배로 늘리므로 각 octave의 경계가 일치한다.

```text
base   = periodic Perlin-Worley(basePeriod)
detail = periodic Worley FBM(detailPeriod)
shape  = remap(base, noiseCutoffThreshold) - detail * erosion
density = saturate(shape) * HeightGradient(y) * densityMultiplier
```

`noiseCutoffThreshold`보다 낮은 base 값은 빈 공간으로 제거한다. `noiseWorldScale`은 반복 개수, `basePeriod/detailPeriod`는 타일 내부 lattice 복잡도를 제어한다. wind는 X/Z에만 더하고 `frac`로 타일을 순환한다. 높이 마스크는 Y에서 반복하지 않는다. 캐시 텍스처는 texel center에서 생성하고 WRAP sampler로 읽는다.

## Beer–Lambert와 single scattering

각 view step에서 `stepT = exp(-density * dt)`를 계산하고 front-to-back으로 누적한다. 누적 투과율이 0.01 미만이면 조기 종료한다.

밀도가 있는 위치에서는 태양 방향으로 최대 6회(설정 가능, 1~12) light march하여 self-shadow 투과율을 구한다. 직접광은 Henyey–Greenstein 위상 함수, 태양 세기와 light visibility를 곱하고, 하늘색 ambient를 더한다.

```text
visibility *= exp(-lightDensity * lightStep * lightAbsorption)
scattering += viewT * stepOpacity * (ambient + sun * visibility * HG)
viewT *= stepT
final = scattering + sky * viewT
```

다중 산란, temporal reprojection, 반해상도 렌더링은 아직 적용하지 않는다.

## 디버그 모드

Lit Cloud, Final Density, Base Shape, Detail Noise, Height Mask, Transmittance, Procedural/Cache Difference, Seam Difference를 제공한다. Noise Inspector는 같은 공통 밀도 함수로 XY/XZ/YZ의 256×256 단면 네 장을 만든다. 이 해상도는 UI 검사 이미지일 뿐 메인 구름 렌더 해상도와 무관하다.
