# 레이마칭과 구름 밀도

## 레이와 평면 구름층

픽셀 UV를 NDC로 바꾸고 `invViewProj`로 `ro`, `rd`를 만든다. 전역 구름층의 하단·상단 평면과 교차해 `[t0,t1]`을 구하고 `maxMarchDistance`로 수평선 방향을 제한한다. weather coverage가 없는 곳은 4배, 후보 영역은 2배, 실제 밀도는 기본 스텝으로 진행한다. 고정 gradient jitter는 밴딩을 줄이되 temporal shimmer를 만들지 않는다.

광역 장면의 표시 규칙은 `1 world unit = 1 km`, `+Y=고도`, `+Z=북쪽`, `+X=동쪽`이다. 정규화된 `rd`를 사용하므로 ray parameter `t`도 km로 해석한다. 예를 들어 `cloudBaseHeight=2`, `cloudThickness=3.8`, `maxMarchDistance=96`은 각각 2 km 하단, 3.8 km 기준 두께, 96 km 제한이다. 다만 `densityMultiplier`와 `lightAbsorption`은 실제 대기 측정 단위를 복원한 값이 아니라 화면 품질을 위한 예술적 계수다.

F1은 기준 두께를 3~16 km로, view step을 48~256으로 조절한다. 지역 두께는 weather A와 `thicknessVariation`의 영향을 받아 기준 범위를 넘을 수 있다. 높은 step은 `baseDt`를 줄이지만 실제 반복 수는 weather 기반 4×/2×/1× 진행과 투과율 조기 종료에 따라 달라진다. 따라서 GPU 시간은 step에 정비례하거나 항상 단조 증가하지 않으며 128/160/192/256을 고정 benchmark와 사용자 화면에서 함께 비교한다.

## Weather map과 월드 좌표

512² RGBA map은 R=coverage, G=cloud type, B=base-height variation, A=thickness variation이다. XZ 월드 좌표를 `weatherWorldSize`로 나눠 반복 샘플링하고, B/A로 각 지점의 실제 하단과 상단을 만든다. 3D 노이즈는 `worldXZ / cloudNoiseWorldSize`와 지역 높이 비율을 사용하므로 유한 AABB 모서리가 없다.

## 심리스 periodic 노이즈

Value noise는 lattice cell을 정수 period로 modulo한 뒤 hash한다. Worley는 feature를 만들 cell만 wrap하고, 거리는 원래 이웃 offset으로 계산한다. FBM의 octave마다 좌표 주파수와 period를 함께 두 배로 늘리므로 각 octave의 경계가 일치한다.

```text
weather = sampleWeather(worldXZ)
localCoverage = coverage + (weather.r - 0.5) * weatherCoverageStrength
localBottom / localTop = weather.b / weather.a로 변형
height01 = remap(worldY, localBottom, localTop)
base.r = periodic Perlin-Worley(basePeriod)
base.gba = low/mid/high periodic Worley bands
detail = periodic Worley FBM(detailPeriod)
macro  = remap(base.r, cutoff + localCoverage + height profile)
macro -= dot(base.gba, weights) * baseErosion at boundary
shape  = macro - detail * erosion at boundary
density = saturate(shape) * CumulusHeightProfile(y) * densityMultiplier
```

전역 `coverage`는 전체 채움 비율, weather R은 지역별 맑음/흐림을 조절한다. weather G는 층운과 적운 높이 프로파일을 혼합한다. `baseErosion`은 거시 경계를, detail은 코어가 아닌 경계만 깎는다.

## Beer–Lambert와 근사 다중 산란

각 view step에서 `stepT = exp(-density * dt)`를 계산하고 front-to-back으로 누적한다. 누적 투과율이 0.01 미만이면 조기 종료한다.

밀도가 있는 위치에서는 태양 방향으로 구름층 상단까지 기본 8회 light march하여 self-shadow를 구한다. 위상은 정규화된 전방 HG와 약한 후방 HG를 혼합한다. 최종 하늘과 산란광에는 지수 tone mapping을 적용해 밝은 가장자리 포화를 완화한다.

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

Base R/G/B/A, Light Visibility, Phase, Ambient, Direct와 Weather Coverage/Type/Base Height/Thickness 모드를 제공한다. Noise Inspector는 3D 단면 네 장과 weather map을 256×256으로 표시하며, F1 가용 폭에 따라 1열/2열로 전환한다.
