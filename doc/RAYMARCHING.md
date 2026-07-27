# 레이마칭과 구름 밀도

## 레이와 AABB

픽셀 UV를 NDC로 바꾸고 `invViewProj`로 근·원 평면 점을 역투영해 `ro`, `rd`를 만든다. `RayBox`의 slab 교차로 박스 내부 구간 `[t0,t1]`을 구한다. 48~128 범위의 view step을 사용하며 빈 공간은 2배 거리로 건너뛴다. 화면 픽셀의 고정 gradient noise로 첫 샘플을 이동해 밴딩을 줄이되 프레임마다 값은 바꾸지 않아 temporal shimmer를 만들지 않는다.

## 심리스 periodic 노이즈

Value noise는 lattice cell을 정수 period로 modulo한 뒤 hash한다. Worley는 feature를 만들 cell만 wrap하고, 거리는 원래 이웃 offset으로 계산한다. FBM의 octave마다 좌표 주파수와 period를 함께 두 배로 늘리므로 각 octave의 경계가 일치한다.

```text
base.r = periodic Perlin-Worley(basePeriod)
base.gba = low/mid/high periodic Worley bands
detail = periodic Worley FBM(detailPeriod)
macro  = remap(base.r, cutoff + coverage + height profile)
macro -= dot(base.gba, weights) * baseErosion at boundary
shape  = macro - detail * erosion at boundary
density = saturate(shape) * CumulusHeightProfile(y) * densityMultiplier
```

`coverage`는 cutoff를 이동해 큰 채움 비율을 조절하고 `baseErosion`은 세 Worley 밴드로 거시 경계를 깎는다. detail은 코어가 아니라 경계에만 적용한다. 높이 프로파일은 하단을 좁히고 중단을 부풀린 뒤 상단을 페이드한다. wind는 X/Z에만 더하고 `frac`로 타일을 순환한다.

## Beer–Lambert와 근사 다중 산란

각 view step에서 `stepT = exp(-density * dt)`를 계산하고 front-to-back으로 누적한다. 누적 투과율이 0.01 미만이면 조기 종료한다.

밀도가 있는 위치에서는 태양 방향으로 기본 8회(설정 가능, 1~12) light march하여 self-shadow 투과율을 구한다. 위상은 정규화된 전방 HG와 약한 후방 HG를 혼합한다. `rd`가 카메라에서 샘플을 향하므로 산란각은 `dot(-rd, sunDir)`이다.

```text
visibility *= exp(-lightDensity * lightStep * lightAbsorption)
multiVisibility = weighted_sum(visibility, sqrt(visibility), fourth_root(visibility))
direct = sun * lerp(visibility, multiVisibility, strength) * dualLobePhase
direct += powder + thin-edge silver lining
scattering += viewT * stepOpacity * (heightAmbient + direct)
viewT *= stepT
final = scattering + sky * viewT
```

3-octave 항은 정확한 다중 산란 적분이 아니라 짙은 그림자가 완전히 죽는 것을 막는 저비용 근사다. temporal reprojection과 반해상도 렌더링은 아직 적용하지 않는다.

## 디버그 모드

기존 모드에 Base R/G/B/A, Light Visibility, Dual-Lobe Phase, Ambient, Direct Lighting을 추가했다. Noise Inspector는 XY/XZ/YZ의 256×256 단면 네 장을 만든다. 이 해상도는 UI 검사 이미지일 뿐 메인 구름 렌더 해상도와 무관하다.
