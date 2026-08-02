# 레이마칭과 구름 밀도

## 레이와 평면 구름층

픽셀 UV를 NDC로 바꾸고 `invViewProj`로 `ro`, `rd`를 만든다. 전역 구름층의 하단·상단 평면과 교차해 `[t0,t1]`을 구하고 `maxMarchDistance`로 수평선 방향을 제한한다. fine step은 `(t1-t0)/viewSteps`와 `maxViewStepLength` 중 작은 값이며 기본 물리 상한은 0.05km다. 실제 밀도는 fine step, 후보지만 비어 있으면 목표 간격의 2배(최대 0.4km), weather가 비면 4배(최대 1.6km)로 진행한다. 빈 구간 뒤 처음 밀도를 찾으면 직전 구간을 기본 3회 이분 탐색해 경계 진입점을 정제한다. 정적 loop 상한은 2000회지만 구간 종료와 투과율 0.01에서 조기 종료한다.

광역 장면의 표시 규칙은 `1 world unit = 1 km`, `+Y=고도`, `+Z=북쪽`, `+X=동쪽`이다. 정규화된 `rd`를 사용하므로 ray parameter `t`도 km로 해석한다. 예를 들어 `cloudBaseHeight=2`, `cloudThickness=3.8`, `maxMarchDistance=96`은 각각 2 km 하단, 3.8 km 기준 두께, 96 km 제한이다. 다만 `densityMultiplier`와 `lightAbsorption`은 실제 대기 측정 단위를 복원한 값이 아니라 화면 품질을 위한 예술적 계수다.

F1은 기준 두께를 3~16 km로, view step을 48~256으로 조절한다. 지역 두께는 weather A, `thicknessVariation`, cloud type과 `cumulusGrowth`의 영향을 받아 기준 범위를 넘을 수 있다. 높은 step은 목표 간격을 줄이지만 실제 반복 수는 50m 물리 상한, weather 기반 4×/2×/1× 진행과 투과율 조기 종료에 따라 달라진다. 따라서 GPU 시간은 step에 정비례하거나 항상 단조 증가하지 않으며 128/160/192/256을 고정 benchmark와 사용자 화면에서 함께 비교한다.

## Weather map과 월드 좌표

밀도 계층의 첫 검사는 placement다. `placementStrength=1`에서 placement R이 0이면 weather와 base/detail 3D 텍스처를 모두 건너뛴다. placement가 유효한 위치에서만 기존 weather potential과 macro/detail 계층을 평가한다.

별도 512² RGBA8 placement map은 R=중심 proximity union, G=혼합 반경 변이, B=혼합 높이 변이, A=혼합 상단 profile을 저장한다. periodic cellular grid의 주변 3×3 활성 후보에서 R은 최댓값, G/B/A는 support 가중 평균이며 활성 확률은 weather coverage와 placement density를 함께 사용한다. B는 지역 두께를 바꾸고 정규화 높이 0.45 위에서는 A와 `placementTopShrink`가 허용 반경을 단조 감소시킨다. G로 복원한 반경과 cell 수로 최소 1.5 texel 자동 edge 폭을 계산해 softness 0에서도 hard cut을 막는다.

밀도 평가는 비용이 싼 weather potential부터 검사한다. potential이 정확히 0이면 base/detail 3D 텍스처를 모두 건너뛴다. base shape·높이·anvil을 합친 macro density가 양수가 될 수 없으면 detail도 건너뛴다. detail은 밀도를 추가하지 않고 침식만 하므로 이 계층형 early-out은 view, light, 경계 정제 경로의 최종 밀도를 보존한다.

512² RGBA map은 R=coverage, G=cloud type, B=base-height variation, A=thickness variation이다. XZ 월드 좌표에 시간에 따른 바람 오프셋을 더한 뒤 `weatherWorldSize`로 나눠 반복 샘플링하고, B/A로 각 지점의 실제 하단과 기준 두께를 만든다. 같은 월드 바람 오프셋을 weather, base, detail에 공통 적용하므로 큰 coverage 실루엣과 내부 형태가 함께 이동한다. G가 적운형일수록 `cumulusGrowth`가 실제 상단을 높이고 `anvilStrength`가 상부 형태를 넓힌다.

Base와 detail은 같은 UVW를 공유하지 않는다. Base XZ는 `cloudNoiseWorldSize`, Y는 `baseNoiseVerticalSize`, detail XZ는 `detailNoiseWorldSize`, Y는 `detailNoiseVerticalSize`로 나눈다. Y 좌표는 local base에서의 물리적 km 오프셋을 사용하고 `height01`은 envelope에만 쓰므로 cloud thickness를 바꿔도 노이즈 특징 크기가 늘어나지 않는다. 바람도 월드 km 오프셋을 먼저 계산한 뒤 각 크기로 변환한다.

## 심리스 periodic 노이즈

Value noise는 lattice cell을 정수 period로 modulo한 뒤 hash한다. Worley는 feature를 만들 cell만 wrap하고, 거리는 원래 이웃 offset으로 계산한다. FBM의 octave마다 좌표 주파수와 period를 함께 두 배로 늘리므로 각 octave의 경계가 일치한다.

```text
placement = samplePlacement(worldXZ)
if placement.r == 0 and placementStrength == 1: early out
weather = sampleWeather(worldXZ)
localCoverage = coverage + (weather.r - 0.5) * weatherCoverageStrength
localBottom / localTop = weather.b / weather.a와 placement.b로 변형
height01 = remap(worldY, localBottom, localTop)
base.r = periodic Perlin-Worley(basePeriod)
base.gba = low/mid/high periodic Worley bands
detail.rgba = periodic Worley(detailPeriod * 1/2/4/8)
macro  = remap(base.r, cutoff + localCoverage + height profile)
macro -= dot(base.gba, weights) * baseErosion at boundary
placementSupport = taper(placement.r, placement.a, height01)
macro += weatherType * anvil(height01) * anvilStrength
macro *= placementSupport
shape  = macro - detail * erosion inside detailErosionWidth
density = saturate(shape) * CumulusHeightProfile(height01) * densityMultiplier
```

전역 `coverage`는 전체 채움 비율, weather R은 지역별 맑음/흐림을 조절한다. weather G는 층운과 적운 높이 프로파일을 혼합한다. `baseErosion`은 거시 경계를, detail은 코어가 아닌 경계만 깎는다.

## Beer–Lambert와 근사 다중 산란

각 view step에서 `stepT = exp(-density * dt)`를 계산하고 front-to-back으로 누적한다. 누적 투과율이 0.01 미만이면 조기 종료한다.

밀도가 있는 위치에서는 태양 방향으로 별도 light ray를 만든다. `localLightDistance=0.6km` 구간의 기본 5개 sample은 태양 축을 따라 진행하면서 golden-angle cone offset을 적용하고 detail density까지 평가한다. 출구 쪽은 기본 1개 sample로 macro density만 평가해 큰 덩어리의 차폐를 싼 비용으로 얻는다. 두 구간의 optical depth를 합친 뒤 Beer–Lambert 투과율을 계산한다. 50m 간격의 인접 dense view sample 두 개는 light 결과를 공유해 light ray를 약 100m마다 갱신한다. 위상은 정규화된 전방 HG와 약한 후방 HG를 혼합한다.

기존 세 번의 제곱근 visibility 혼합 대신 single scattering에 extinction과 phase eccentricity가 감쇠된 두 번째 octave 하나를 더한다. 짧은 상향 macro-density sample 2개는 sky ambient visibility를 만들고 `ambientOcclusionStrength`로 내부·하단 ambient를 감쇠한다. powder는 local optical depth와 시선–태양 관계에 반응하고, silver lining은 낮은 밀도와 실제 light visibility가 모두 있을 때만 추가된다.

```text
opticalDepth += lightDensity * segmentStep * lightAbsorption
visibility = exp(-opticalDepth)
secondVisibility = exp(-opticalDepth * multiScatterExtinctionAttenuation)
direct = sun * (visibility * phase
       + secondVisibility * attenuatedPhase * 0.5 * multiScatterStrength)
ambient *= lerp(1, upwardMacroVisibility, ambientOcclusionStrength)
direct *= optical-depth powder
direct += sun * visible thin-edge silver lining
scattering += viewT * stepOpacity * (ambient + direct)
viewT *= stepT
half output = premultiplied scattering, (firstDepth, viewT)
full composite = scattering + analyticalSky * viewT
```

두 번째 octave는 정확한 다중 산란 적분은 아니며 짙은 그림자가 완전히 죽는 것을 막되 대비가 씻기지 않도록 extinction과 eccentricity를 각각 감쇠하는 저비용 근사다.

## 반해상도 raymarch와 temporal reconstruction

beauty 모드는 가로·세로 0.5배의 `R11G11B10_FLOAT` 선형 premultiplied cloud scattering과 `R16G16_FLOAT` `(첫 유효 구름 거리, transmittance)` MRT에서 raymarch한다. 하늘은 이 단계에 넣지 않고 full-resolution composite에서 공통 분석적 하늘 함수로 합성한 뒤 tone mapping한다. 정지 animation에서는 4-frame jitter로 서로 다른 서브픽셀 위치를 평가한 뒤 full-resolution scattering/metadata 두 세트를 ping-pong한다.

resolve는 가장 가까운 half sample을 anchor로 삼아 2×2 이웃을 복원한다. bilinear 공간 가중치에 opacity 차이와 depth 차이 가중치를 곱하고, cloud/sky 분류가 다른 sample은 거의 배제한다. 따라서 밝은 하늘이 어두운 외곽으로 번지는 것을 줄이면서 균일 영역에서는 bilinear와 같은 결과를 낸다.

animation 중에는 weather/base/detail의 월드 이동 자체가 시간별 sample phase를 제공하므로 별도 camera jitter를 0으로 둔다. 두 시간 변화를 동시에 적용해 얇은 상·하 경계가 흔들리는 현상을 피한다. `windSpeed`의 내부 단위는 km/s지만 UI는 m/s로 표시하며 기본값은 100m/s, 편집 범위는 0~2,000m/s다. UI는 `worldSize / windSpeed`로 계산한 weather/base/detail 완전 반복 시간을 함께 표시하며, 300m/s 초과는 물리적 바람보다 animation·temporal stress test 용도다.

현재 첫 구름 위치를 world position으로 복원하고 바람 이동을 더해 이전 view-projection에 투영한다. 이전 UV가 화면 밖이거나 깊이 차이가 `max(0.1km, 5%)`를 넘으면 history를 거부한다. 깊이가 맞아도 현재/이전 transmittance 차이가 커지면 confidence를 선형으로 낮춘다. 유효 history scattering은 현재 반해상도 3×3 범위로 clamp하고 최대 0.85 가중치로 혼합하며 metadata도 같은 confidence를 사용한다. resize, temporal/reference 전환, 렌더 모드·프리셋·밀도 변경, 1km를 넘는 카메라 이동은 history를 초기화한다.

F1의 debug render mode와 reference 선택은 원본 채널을 정확히 관찰하도록 full-resolution 직접 경로를 사용한다.

## 디버그 모드

Base R/G/B/A, Light Visibility, Phase, Ambient, Direct, Weather Coverage/Type/Base Height/Thickness, Placement Support/Radius/Height, Ambient Occlusion, Resolved Opacity, Temporal History Confidence 모드를 제공한다. Noise Inspector는 3D 단면 네 장과 weather/placement map을 표시하며, F1 가용 폭에 따라 1열/2열로 전환한다.
