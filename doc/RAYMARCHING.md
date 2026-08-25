# 단계 1: 상수 밀도 AABB 레이 마칭

단계 0에서 복원한 정규화 월드 레이와 meter 단위 Scene Depth를 사용해, 축 정렬 박스 안의 일정한 안개만 적분한다.

## 단계 13-0에서 고정한 공간 단위 계약

13-0에서는 이후 km 확대가 기존 모양과 광학값을 동시에 깨뜨리지 않도록 단위를 먼저
고정했다. 위치·거리·step은 meter, noise 주파수는 cycle/m,
extinction은 1/m다. 밀도·coverage·UV와 반복 횟수는 무차원이다.

장면을 물리적으로 S배 확대하면서 같은 상대 모양을 유지할 때 길이는 S배, noise 주파수와
extinction은 1/S배한다. 따라서 다음 값들은 확대 전후에 변하지 않는다.

```text
worldPosition × noiseFrequency
worldPosition / weatherWorldSize
density × extinction × pathLength
(1 / noiseFrequency) / stepLength
```

이 수학은 `Stage13ScaleMath` CPU 회귀 테스트와 단계 13-2 런타임 프리셋으로 검증한다.

## 단계 13-1: AABB/평면층 교차 분리

View Ray와 Light Ray는 동일한 `IntersectCloudDomain` 진입점을 사용한다. 내부 `AabbReference`는
기존 slab 교차를 그대로 호출하고, Planar Layer는 X/Z 경계 없이 Y 바닥과 상단만 푼다.

```text
yBottom = 1500m
yTop    = yBottom + 3000m
t0      = (yBottom - rayOrigin.y) / rayDirection.y
t1      = (yTop    - rayOrigin.y) / rayDirection.y
tStart  = max(min(t0, t1), 0)
tEnd    = min(max(t0, t1), sceneDepth, domainTraceLimit)
hit     = tEnd > tStart
```

아래에서 위를 보거나 위에서 아래를 보아도 `min/max`가 진입·이탈 순서를 정렬한다.
카메라가 층 내부면 `tStart=0`이다. `abs(rayDirection.y) <= 1e-6`인 수평 레이는 층 밖이면
miss, 층 안이면 View 50km 또는 Light 20km의 유한 구간을 반환하므로 0 나눗셈과 무한 적분이 없다.
Scene Depth가 더 가까우면 View `tEnd`를 불투명 표면 거리로 잘라 구름 누출을 막는다.

평면층 View 밀도에는 40~50km smoothstep fade를 곱한다.

```text
u = saturate((rayDistance - 40000m) / 10000m)
distanceFade = 1 - u²(3 - 2u)
```

fade는 유한 추적 끝의 딱딱한 절단선만 숨기며 교차 거리 자체는 바꾸지 않는다. 디버그 출력은
Entry, Exit, `Exit-Entry`, `segmentLength / ceil(segmentLength / requestedStep)` 네 값을 분리한다.
km 거리의 가까운 값이 검게 뭉개지지 않도록 평면층 거리 출력만 0~50km 정규화 뒤 제곱근
표시를 사용하며, 내부 AABB 회귀 출력의 기존 선형 0~20m 출력은 유지한다.
13-1의 Planar 합성은 기존 AABB 높이·noise 단위를 아직 사용하므로 미적 품질 판정 대상이 아니다.

### 13-4D 단일 포트폴리오 씬

일반 실행은 `CloudDomainType::PlanarLayer` 하나만 사용한다. 층은 `1500~7500m`, View는
50km, fade는 40~50km, Light는 20km다. Y=0의 10km 정사각형 지면과 원점의
`20×60×20m` 건물이 먼저 깊이를 쓰므로 F5에서는 건물 뒤 구름이 Scene Depth에서 잘린다.
analytic sky는 카메라 기준 50km를 채우며 구형 도메인은 포트폴리오 범위에서 제외했고
실제 대기 산란은 아직 추가하지 않는다.

이전 사용자 출력 Entry/Exit와 Ray Direction/Scene Depth/World Position/Screen UV/step count
ID 1~7은 삭제했다. 교차 수학과 `CloudHitMask`, Segment Length, Actual Step은 자동 회귀에
남는다. 사용자 숫자 0~9는 `Stage13SceneMath`의 명시적 테이블로 기존 HLSL ID
0/10/20/16/17/12/52/32/8/24를 선택하며 1~7 입력은 Composite로 sanitize한다.

### 13-4C Local Cloud Inspector (승인 이력)

F1 UI는 기술 이름 `AABB Reference`를 노출하지 않고 `Local Cloud Inspector`로 표시한다.
Inspector의 View/Light Ray는 별도 셰이더 분기 없이 위의 동일한 AABB 교차와 Weather,
Texture3D, Physical Shape, 조명 함수를 통과한다. AABB는 `(-100,5,-120)m~(100,55,80)m`,
View는 `0.5m×512`, Light는 `1m×256`, extinction은 `0.03/m`다. 따라서 최대 50m 수직
광학 깊이는 `τ=1.5`이며 Open World의 대표 투과율을 작은 공간에서 비교할 수 있다.

아래 설명은 2026-08-16 승인 당시의 이력이다. 13-4D에서는 Inspector Weather·AABB 장면과
Local/Open 전환을 런타임에서 제거했다. 당시 Local/Open 전환은 Weather·Cloud Type·Noise·조명·step을 바꾸지 않고 AABB 또는 Planar
교차에 필요한 기하·trace 범위와 마지막 카메라만 교체한다. 고정 Cloud Type은 CPU Weather
Map의 G만 교체하며 R coverage, B density, A thickness와 HLSL 경로는 유지한다.

진단 평면과 `20×60×20m` 주황 건물은 먼저 불투명 깊이를 쓰고, 구름 적분의 `tEnd`는
복원된 Scene Depth에서 잘린다. 초기 시점에서 건물 뒤 구름이 건물 픽셀에 덧그려지면
Inspector 전용 문제가 아니라 공통 Scene Depth 제한 또는 합성 경로의 회귀다. CPU/HLSL
상수버퍼 크기와 `kCloudDomainAabb` 값은 이 이름 변경으로 달라지지 않는다.

## 단계 13-2: 기존 noise의 단계적 상사 확대

Noise Lab의 `1x/10x/100x/1000x` 버튼은 항상 Stage 8 기준값에서 다시 계산한다. 이전에
어떤 배율을 눌렀는지와 무관하며 길이·속도·역길이 값을 다음처럼 함께 바꾼다.

```text
cloudBounds, layerBottom, layerThickness = Stage8 length × S
viewStep, lightStep, lightBias           = Stage8 length × S
view/light trace distance, Weather size  = Stage8 length × S
base/detail/weather wind speed           = Stage8 speed × S
base/detail frequency, extinction        = Stage8 inverse-length / S
coverage, density, offset, maxSteps       = unchanged
```

예를 들어 1000×에서는 두께 3000m, View step 100m, Base `0.00035 cycle/m`,
extinction `0.001/m`가 된다. 그러면 같은 정규화 위치 `p`에서 다음 값이 동일하다.

```text
(p × S) × (frequency / S) = p × frequency
(windSpeed × S) × (frequency / S) = windSpeed × frequency
density × (extinction / S) × (pathLength × S)
    = density × extinction × pathLength
```

Base 파장당 View 표본은 모든 배율에서 약 `28.57`, Detail은 `4`이고, 밀도 1인 전체
3m 기준층의 대표 광학 깊이 `tau`는 모든 배율에서 `3`이다. F4 `Scale Compare` 카메라는 위치·타깃·
거리를 S배하고 원점의 진단 건물을 피하도록 `x=40×S`를 사용한다. 따라서 투영 실루엣과
투과율을 같은 화면 구도로 비교할 수 있다.

13-2의 Planar 고도는 `-1×S~2×S`, Weather는 `16×S`다. 1000×에서 이는
`-1000~2000m`, 16km이며 최종 오픈 월드 값이 아니다. 바닥을 1500m로 올리고 Weather를
64km, extinction을 `0.0005/m`로 재조정하는 작업은 아래 13-3 프리셋에서 한다.

## 단계 13-3: 실제 오픈 월드 시작값

13-3 승인 시점의 일반 실행은 Planar `1500~4500m` 층과 지상 수평선 카메라로 시작했다. 13-4B
현재값은 아래 형상 절에서 설명하는 `1500~7500m`다. 기존 절차적
Base/Detail을 유지하되 실제 길이·이동 속도·광학값을 다음처럼 고정한다.

```text
View required steps  = ceil(50000m / 100m) = 500 <= 512
Light required steps = ceil(20000m / 250m) = 80 <= 80
Base samples/wavelength   = (1 / 0.00035) / 100 = 28.571
Detail samples/wavelength = (1 / 0.0025) / 100 = 4.000
vertical tau = density 1.0 × extinction 0.0005/m × thickness 3000m = 1.5
Weather texel = 64000m / 256 = 250m
```

`maxViewSteps`와 `maxLightSteps`는 목표 간격으로 전체 제한 거리를 덮을 수 있는 안전
상한이다. 실제 segment가 짧으면 `ceil(segment/targetStep)`만 사용한다. View 제한 전체를
사용해도 500 step이므로 512에 걸리지 않으며, Light는 80 step으로 20km를 정확히 덮는다.

`Actual Step Debug`는 miss를 검정, 요청 간격 이내를 파랑→초록으로 표시한다. 초록은
요청한 100m에 가까운 정상값이다. max step 상한으로 실제 간격이 요청값을 초과하면
노랑→빨강으로 바뀌므로 banding 위험을 화면에서 즉시 구분할 수 있다.

40~50km 거리 fade는 13-1과 같은 smoothstep을 사용한다. 이는 실제 대기 원근이 아니라
유한 추적 끝의 절단만 숨기는 장치다. 대기 색·원근, 거리 LOD와 Early Exit는
각각 단계 14, 13-5와 단계 9 범위다.

## 단계 13-4B: Weather 기반 물리 두께와 Base/Detail Texture3D

사용자 카메라의 center ray는 각 프리셋의 `normalize(target-position)`이다. F5/F6은
1,500m 아래, F7은 층 내부, F8은 7,500m 위에서 시작하며 일반 프리셋은 Scene Depth
진단 장면을 끈다. 따라서 구름 교차를 주황 건물 없이 판정하고, 건물 폐색은 F4 `Building Depth`
구도에서만 분리해 검사한다.

Weather는 64km XZ에서 “어디에 구름이 있는가”를 정하고, 3D volume은 구름 안의
“덩어리와 표면 굴곡이 어떤가”를 정한다. 두 역할을 분리하므로 Weather 무늬를 바꿔도
Base/Detail의 내부 질감은 그대로 유지된다.

```text
directionXZ = normalizeOrZero(windDirection.xz)
cloudOffsetXZ = directionXZ * windSpeed * effectiveTime
samplePosition = float3(worldPosition.xz - cloudOffsetXZ, worldPosition.y)

weatherUV = frac(samplePosition.xz / 64000m + weatherOffset)
baseUVW.xyz = frac(samplePosition.xyz / 12000m + offset)
detailUVW.xyz = frac(samplePosition.xyz / 2000m + detailOffset)

base PW = BaseTexture3D(baseUVW).r
base cellular fBm = dot(BaseTexture3D(baseUVW).gba, (0.625, 0.25, 0.125))
detail fBm = dot(DetailTexture3D(detailUVW).rgba, (0.50, 0.30, 0.15, 0.05))
```

Physical Shape는 Weather R/G/A 경계와 Base/Detail을 같은 `f(p-vt)` 위치에서 읽는다.
따라서 구름 전체가 `+directionXZ`로 강체 이동하며 내부 무늬가 고정된 외곽 안에서
미끄러지지 않는다. Y는 그대로라 1,500m 밑면과 타입별 물리 두께가 수직 이동하지 않는다.
`weatherMapWindSpeed`와 `detailWindSpeed`는 Legacy 회귀에서만 독립 속도로 사용한다.

Base R은 낮은 주파수 periodic gradient Perlin과 Worley mass를 결합한 Perlin-Worley다.
Base G/B/A와 Detail RGBA는 서로 다른 주파수의 periodic Worley distance다. Base가
Weather·coverage·높이 프로파일을 거친 다음, Detail은 경계 쪽에서만 subtractive erosion으로
깎는다. Light Ray는 단계 4 계약대로 Detail을 생략하고 같은 Texture3D Base를 읽는다.

Texture 주소 모드는 wrap이고 생성 함수 자체도 정수 cell을 주기 범위로 감싼다. 따라서
`uvw`와 `uvw+(정수 축)`은 같은 위치다. `Tile Wrap Difference`는 이 차이를 육안 확인하기
위해 255배 확대하며 정상 화면은 검정에 가깝다. 자동 검증은 확대 전 실제 최대 차이
`≤1/1024`를 사용한다. 이는 RGBA8 한 양자화 단계 `1/255`보다 엄격하다.

```text
Base frequencies = {4, 9, 17, 23}
Base wavelengths = {3000m, 1333.33m, 705.88m, 521.74m}
Base samples/wavelength at 100m = {30, 13.33, 7.06, 5.22}
Base Perlin octave seed = noiseVolumeSeed + octave * 173u
Detail frequencies = {2, 3, 4, 5}
Detail wavelengths = {1000m, 666.67m, 500m, 400m}
Detail samples/wavelength at 100m = {10, 6.67, 5, 4}
Base max frequency 23 <= 128/2
Detail max frequency 5 <= 32/2
```

Weather A는 XZ별 `localThicknessPotential`이다. Weather 생성기는 독립 높이장을
coverage 중심과 섞되, 빈 영역을 되살리는 밀도 항으로는 사용하지 않는다.

```text
coverageCore = smoothstep(0.05, 0.95, coverage)
weatherA = lerp(independentHeightField, coverageCore, thicknessCoverageInfluence)
default thicknessCoverageInfluence = 0.20

type = saturate(weatherG)
minimumThickness = lerp(1500m, 3000m, type)
maximumThickness = lerp(2500m, 6000m, type)
localThickness = lerp(minimumThickness, maximumThickness, weatherA)
localHeight = (worldY - 1500m) / localThickness
```

`localHeight`가 `[0,1]` 밖이면 밀도는 0이다. 따라서 모든 XZ 기둥의 밑면은 정확히
1,500m이고, 상단은 타입과 Weather A에 따라 달라진다. Dense Mixed의 Stratus/Mixed/
Cumulus bottom/top은 `0.06/0.65`, `0.10/0.86`, `0.08/0.93`이다. Cumulus에는 로컬
높이 `0.08→0.70`에서 `0.65→1.0`으로 증가하는 upper-mass를 곱한다.
타입 `0→0.5→1` 구간에서는 이 세 envelope를 연속 보간한다. Physical mode는 여기에
높이 전체에서 계속 변하는 footprint scale을 곱해 `Typed Shape Profile`을 만든다.

13-4E의 Physical density는 수평 배치와 수직 질량을 다음처럼 분리한다.

```text
weatherSupport    = smoothstep(0.02, 0.20, weatherR)
weatherFactor     = lerp(0.70, 1.00, weatherR)
footprintFactor   = lerp(0.80, 1.00, typedFootprint)
horizontalCoverage = saturate(globalCoverage * weatherFactor * footprintFactor)
noiseShape        = RemapCoverage(rawNoise, horizontalCoverage)
baseDensity       = insideColumn * weatherSupport * noiseShape
                  * typedVerticalProfile * densityMultiplier * weatherDensityModifier
```

Weather R=0은 정확히 비지만 약한 Weather도 작은 support로 남는다. 세로 profile은 noise
threshold를 다시 축소하지 않고 밀도에 한 번만 곱해 상단의 이중 절단을 막는다. footprint는
threshold에 20%만 영향을 줘 둥근 상단을 만들되 질량 전체를 제거하지 않는다. Detail은
이미 만들어진 Base 경계만 침식한다. 두 Texture3D 좌표는 로컬 상단에 맞춰 늘이거나
압축하지 않으므로 카메라 이동 중 무늬가 월드에 고정된다. Dense Mixed의 기준 수직 광학
스케일은 `6000m × 0.00035/m = τ 2.1`이며 실제 τ에는 density/noise/profile이 함께 들어간다.
Similarity와 구형 회귀는 b7 Legacy shape mode의 기존 경로를 유지한다.

F1 파이프라인 비교는 교차 구간을 바꾸지 않고 위 밀도 계산만 누적 교체하며 main pass의
effective time을 `0`으로 고정한다. 일반 Animation 상태나 카메라는 바꾸지 않는다. 1단계는
Procedural+Uniform+Legacy Shape, 2단계는 Texture3D, 3단계는 64km Periodic Weather,
4단계는 물리 두께와 타입 Profile, 5단계는 최신 Open World extinction·View/Light budget과
바람까지 적용한다. 따라서 단계 전환에서 처음 얇아지거나 사라지는 출력이 해당 밀도 하위
경로의 회귀 지점이며 AABB/Planar 교차 문제와 분리해 판정할 수 있다.

Texture3D는 실행 초기 compute shader로 결정적으로 생성한다. 같은 seed·규격은 같은
readback hash를 만들어야 한다. 디스크 cache 포맷은 현재 제품 실행 경로가 아니라 손상
검출과 원자적 교체를 검증하는 테스트 전용이다.

## 단계 13-5: km 광학·조명과 Detail 거리 LOD

13-4C에서 승인한 Weather·물리 두께·Base/Detail 형상은 바꾸지 않는다. View Ray는 계속
최종 밀도, Light Ray는 Detail을 제외한 Base 밀도를 사용하지만 두 경로 모두 같은
`extinctionCoefficient(1/m)`와 실제 meter step을 곱한다.

```text
viewTau  = sum(finalDensity * extinctionPerMeter * actualViewStepMeters)
lightTau = sum(baseDensity  * extinctionPerMeter * actualLightStepMeters)
viewT    = exp(-viewTau)
lightT   = exp(-lightTau)
```

상수 밀도에서는 구간을 몇 step으로 나누더라도 합이 해석해 `density × extinction × length`와
같다. 실제 Noise 밀도에서는 표본 오차가 있으므로 20km Light 경로를 다음 세 후보로 비교한다.

| 용도 | Light step | 최대 steps | Base 최소 521.74m 파장당 표본 |
|---|---:|---:|---:|
| fine reference | 62.5m | 320 | 8.35 |
| 이전 품질 비교 | 125m | 160 | 4.17 |
| Open World 기본 | 250m | 80 | 2.09 |

2026-08-17 사용자 화면 비교에서 세 후보의 형상 차이가 크지 않고 `250m/80`이 가장
빨라 새 기본으로 선택했다. 새 Dense/Stratus/Cumulus F6의 `62.5m/320` reference 대비
Composite MAE는 각각 `0.00002579/0.00000912/0.00001599`, P99는
`0.00029521/0.00010890/0.00017973`로 기존 `0.01/0.03` gate를 통과했다.

Light Ray는 먼저 Weather support, 로컬 높이와 세로 profile이 명백히 0인지 검사해 Base
Texture3D fetch를 생략한다. 나머지는 View Base와 같은 수식을 적분하며, 누적 `lightTau`가
`-ln(0.0001)=9.21034`에 도달하면 중단한다. 반환값은 0으로 강제하지 않고
`exp(-lightTau)`를 유지한다. 이 최적화는 2026-08-17 사용자가 13-5 Light 범위로 승인한
예외이며, View early exit·coarse march는 단계 9까지 미룬다.

Local Inspector는 Base world size가 90m라 가장 짧은 파장이 약 3.91m다. 따라서 km 후보를
적용하면 62.5m조차 파장당 한 표본보다 작아 그림자 방향이 달라지는 alias가 생긴다. Local은
`0.5m/512`, `1m/256`, `2m/128`만 비교하며 품질 기본 1m는 약 3.91표본/최소 파장이다.

Light Ray 시작 bias는 CPU와 HLSL 모두 `0~100m`로 제한한다. 따라서 기본 Open World 1m,
Local Inspector 0.05m와 1000× 상사 프리셋 10m가 셰이더에서 같은 값으로 유지된다.

Detail의 가장 짧은 파장은 400m이고 100m View step에서 파장당 4표본이다. 50km 끝에서도
60° vertical FOV·1080p 기준 약 7.5pixel 크기라 옛 실험의 8~20km LOD를 복사하지 않는다.
현재 품질 기본은 32~48km다.

```text
lod = 1 - smoothstep(32000m, 48000m, sampleDistance)
filteredDetail = lerp(detailNeutralValue, sampledDetail, lod)
```

`detailNeutralValue`는 compute 생성 뒤 실제 `32³ RGBA8`를 readback하고 현재
`(0.50,0.30,0.15,0.05)` weights로 계산한다. 기본 seed의 값은 `0.44994098`이다. 48km
뒤에는 Detail Texture3D를 읽지 않지만 이 평균 침식은 계속 적용하므로 원거리에서 구름이
갑자기 두꺼워지지 않는다. Similarity 프리셋은 LOD를 꺼 이전 상사 회귀를 정확히 보존한다.

직접광, Sky, Ground, Multiple은 같은 View 적분에서 별도로 누적해 F3에서 각각 출력한다.
Phase Off/Environment Off로 자기 그림자를 먼저 확인하고, 이후 Phase와 환경광을 한 성분씩
켜 silver lining과 내부 fill의 원인을 분리한다. 실제 대기 radiance와 지면 재질 입력은
여전히 단계 14 범위다.

13-5 외곽광 보완은 추가 레이를 만들지 않고 각 View 표본이 이미 가진 `lightT`를 재사용한다.
`shadowExponent`는 View opacity를 바꾸지 않고 직접 태양광의 자기 그림자 대비만 조절한다.
`edgeInfluence=0`, `shadowExponent=1`은 이전 계산과 동일하다.

```text
shapedLightT = lightT ^ shadowExponent
surfaceMask = lightT ^ edgeOpticalDepthScale
phaseWeight = lerp(1, surfaceMask, edgeInfluence)
scopedPhase = 1 + (phaseFactor - 1) * phaseWeight
direct = commonDirectInteraction * shapedLightT * scopedPhase
```

Silver Lining Contribution은 `max(scopedPhase-1, 0)`이 실제로 추가한 RGB만 누적한다.
Shaped Sun Visibility와 Ambient Visibility는 산란 상호작용량으로 가중 평균해 F3에서 0~1로
표시한다. 세 출력은 진단용 ALU만 추가하며 Light/Base/Detail texture fetch 수를 늘리지 않는다.

## 안전한 slab AABB 교차

레이는 `p(t) = rayOrigin + rayDirection × t`다. 방향을 길이 1로 정규화했으므로 `t`는 meter다. X, Y, Z 각 축에서 두 평면 사이에 레이가 들어 있는 거리 구간을 구하고 세 구간의 교집합을 취한다.

```text
tNear = max(xNear, yNear, zNear)
tFar  = min(xFar,  yFar,  zFar)
```

방향 성분의 절댓값이 `1e-6`보다 작으면 해당 축으로 거의 움직이지 않는다. 이때 나누지 않고 원점이 slab 안인지 검사한다. 밖이면 miss, 안이면 그 축은 거리 구간을 제한하지 않는다. 이 분기가 `1/0`에서 생기는 Inf와 이후 `0×Inf`의 NaN을 막는다.

`tFar > max(tNear, 0)`일 때만 카메라 앞쪽에 양의 길이를 가진 볼륨이 있다. 접점뿐인 tangent와 뒤쪽 교차는 렌더링하지 않는다. 카메라가 내부이면 실제 시작은 `tStart=max(tNear,0)`이다.

## Scene Depth 제한

불투명 장면의 장치 깊이를 월드 위치로 역투영하고 카메라와의 길이를 재서 `sceneDistance`를 얻는다.

```text
tStart = max(tNear, 0)
tEnd = min(tFar, sceneDistance)
```

`tEnd <= tStart`면 물체가 볼륨 앞에 있거나 볼륨을 완전히 가린 것이므로 중립 결과를 반환한다.

```text
scattering = (0,0,0)
transmittance = 1
```

따라서 합성 결과는 원래 배경과 정확히 같다.

단계 13-1의 F4 `Building Depth` DOMAIN-DEPTH 프리셋은 카메라 앞에 `20×60×20m` 주황
진단 건물을 놓고, 건물 바깥 레이는 1.5km 평면층으로 계속 진행하게 만든다. 건물 픽셀은
Scene Depth가 평면층 진입 거리보다 먼저이므로 유효 구름 구간이 없어 Entry/Exit/Segment
진단에서 검게 보이고, 주변 하늘 픽셀은 거리 회색값을 유지해야 한다. 합성에서는 건물
표면 위로 뒤쪽 구름이 덮이면 실패다.

## 전체 구간을 덮는 step

`stepSize`는 목표 간격이고 `maxViewSteps`는 반복 안전 상한이다. 마지막 자투리를 버리지 않도록 step 수를 정한 뒤 실제 간격을 다시 계산한다.

```text
segmentLength = tEnd - tStart
stepCount = min(maxViewSteps, ceil(segmentLength / stepSize))
actualStepLength = segmentLength / stepCount
```

실행 기본 `Y` 넓은 볼륨은 X/Z가 `±8m`이므로 비스듬한 레이의 구간이
`maxViewSteps × stepSize = 12.8m`보다 길 수 있다. 이 경우 구간을 잘라 버리지
않고 128개로 다시 나누므로 `actualStepLength`가 `0.10m`보다 커진다. `Q`의
X/Z `±2m` 볼륨은 기존 수치·step 회귀 기준으로 남겨 둔다. 빈 공간 건너뛰기와
원거리 step 최적화는 단계 9 범위다.

각 샘플 위치는 구간 중앙이다.

```text
sampleDistance = tStart + (stepIndex + 0.5) × actualStepLength
samplePosition = rayOrigin + rayDirection × sampleDistance
```

이번 단계의 밀도는 위치와 무관하게 항상 `0.35`다. `samplePosition`은 단계 2에서 3D noise를 읽을 연결점이다.

## Beer-Lambert 투과율과 산란

한 step을 통과한 배경빛의 비율은 다음과 같다.

```text
stepT = exp(-density × extinctionCoefficient × actualStepLength)
```

밀도, 소광계수 또는 통과 거리가 커지면 투과율은 작아진다. 고정 linear RGB 안개색 `(0.82, 0.86, 0.92)`을 사용해 front-to-back으로 누적한다.

```text
scattering += currentT × fogColor × (1 - stepT)
currentT *= stepT
```

상수 밀도에서는 모든 step의 곱이 분석식 `exp(-density × extinction × segmentLength)`과 같다. 따라서 fine/coarse step의 step count는 달라도 최종 투과율은 거의 같아야 한다.

최종 합성은 이전 단계와 같다.

```text
finalColor = cloudScattering + backgroundColor × cloudTransmittance
```

`transmittanceThreshold=0.01`은 단계 9 Optimized View에서 충분히 불투명해진 레이를 일찍
끝내는 값이다. Reference와 단계 1 회귀는 정확한 전체 구간 비교를 위해 사용하지 않는다.

## 단계 2: 월드 공간 단일 3D noise 밀도

단계 2는 교차·step·합성식을 바꾸지 않고 `density`만 위치별 값으로 교체한다. 화면 UV나 카메라 위치가 아니라 각 step의 `samplePosition`을 입력으로 사용한다.

### 바람과 noise 좌표

```text
safeWindDirection = normalize(windDirection)  // 0 벡터는 (0,0,0)
stationaryWorld = worldPosition - safeWindDirection × windSpeed × time
noiseUVW = stationaryWorld × baseNoiseScale + noiseOffset
```

`baseNoiseScale`의 단위는 cycle/m다. 값이 커지면 같은 월드 거리에서 더 많은 noise 셀을 지나므로 덩어리가 작아진다. `f(x-vt)` 형태로 조회하면 화면의 무늬는 `+v` 월드 방향으로 이동한다. 같은 월드 위치와 같은 시간은 카메라와 무관하게 항상 같은 noise 좌표를 갖는다.

### 단일 3D value noise

`floor(noiseUVW)`가 가리키는 셀의 여덟 모서리를 hash해 각각 `0~1` 값을 만든다. 셀 내부 좌표에는 다음 Hermite 곡선을 적용한다.

```text
smooth = local × local × (3 - 2 × local)
```

여덟 값을 X, Y, Z 순서로 삼선형 보간해 원본 noise 하나를 얻는다. 여러 octave, Worley, detail noise나 texture를 혼합하지 않는다.

### coverage remap

```text
threshold = 1 - coverage
thresholdDensity = saturate((rawNoise - threshold) / max(coverage, 1e-4))
finalDensity = saturate(thresholdDensity × densityMultiplier)
```

coverage가 0이면 별도 분기로 밀도를 0으로 만든다. coverage가 커질수록 threshold가 낮아져 0보다 큰 밀도를 가진 공간이 증가한다. `densityMultiplier`는 남은 덩어리의 농도만 바꾸며 최종 값은 `0~1`로 제한한다.

레이 마칭의 각 step은 `finalDensity`로 Beer-Lambert 투과율을 계산한다. 단계 1과 달리 위치마다 밀도가 다르므로 step 크기에 따른 근사 오차가 생길 수 있지만, adaptive stepping과 빈 공간 건너뛰기는 단계 9까지 추가하지 않는다.

### Noise Lab 단면과 실제 구름의 관계

3D noise는 한 장의 이미지가 아니라 `noise(x,y,z)` 함수다. Noise Lab은 AABB를 정규화한 교차점에서 XY, XZ, YZ 평면을 각각 512×512로 잘라 같은 `SampleCloudDensity(worldPosition, effectiveTime)` 함수를 평가한다. 세 화면의 빨간 crosshair는 같은 3D 위치를 가리킨다.

ImGui의 scale·coverage·density·offset·wind 값은 CPU `CloudParameters`를 바꾸므로 재컴파일 없이 단면과 구름에 같은 프레임에 반영된다. hash와 보간 코드는 `Noise.hlsli` 하나에 있고, 저장 시 Noise Lab PS와 Cloud PS를 함께 컴파일·교체한다.

내보낸 `xy.png`, `xz.png`, `yz.png`는 선택 시점의 2D 단면 기록이다. Z축 전체를 담지 않으므로 구름 셰이더가 다시 읽는 밀도 texture가 아니다. 향후 2D Weather Map은 PNG를 사용할 수 있지만, 3D noise를 굽는 기능은 Texture3D 또는 여러 단면 atlas가 필요하다.

## 단계 3: 상·하단 높이 프로파일

단계 2의 noise는 AABB 경계까지 그대로 남으므로 구름의 바닥과 천장이 평평하게 잘려 보인다. 단계 3은 X/Z 덩어리 위치는 유지하고 월드 Y 높이에 따른 마스크만 곱한다.

```text
cloudThickness = cloudBoundsMax.y - cloudBoundsMin.y
heightFraction = saturate((worldPosition.y - cloudBoundsMin.y) / cloudThickness)
```

`heightFraction`은 바닥에서 0, 천장에서 1이다. `cloudThickness <= 1e-6`인 퇴화·역전 AABB는 나누지 않고 height fraction과 profile을 모두 0으로 반환한다.

```text
safeBottomEnd = clamp(bottomFadeEnd, 0.01, 0.99)
safeTopStart = clamp(topFadeStart, 0.01, 0.99)
bottomFade = smoothstep(0, safeBottomEnd, heightFraction)
topFade = 1 - smoothstep(safeTopStart, 1, heightFraction)
heightProfile = saturate(bottomFade × topFade)
```

기본값 `0.20/0.80`은 아래 20%에서 밀도가 올라오고 위 20%에서 사라지며 중앙 60%는 1을 유지한다. 두 경계는 독립적이다. `bottomFadeEnd > topFadeStart`도 유효하지만 두 fade가 겹쳐 중앙의 완전한 밀도 구간이 사라진다.

최종 적분 밀도는 다음과 같다.

```text
finalDensity = saturate(thresholdDensity × heightProfile × densityMultiplier)
```

Noise Lab과 구름 PS는 모두 `Noise.hlsli`의 `EvaluateHeightFraction`, `EvaluateHeightProfileFromFraction`과 `SampleCloudDensity`를 호출한다. B/M 화면 디버그는 레이 교차 구간의 중간 대표 위치를 보여 주고, 정확한 수직 분포는 Noise Lab 단면을 사용한다. XY와 YZ는 월드 Y가 세로축이라 변화하며, XZ는 Y를 고정하므로 높이 전용 출력이 단색인 것이 정상이다.

## 단계 4: Base Shape와 Detail Erosion

Base Density는 단계 2 noise와 단계 3 높이를 합친 큰 형태다. Detail 설정은 이 값을 만들 때 관여하지 않는다.

```text
baseDensity = saturate(thresholdDensity × heightProfile × densityMultiplier)
```

Legacy Detail은 같은 Value Noise 수학을 별도 고주파 좌표에서 평가한다. 바람 방향은 공유하지만 속도·scale·offset은 독립적이다. Physical Shape는 위 Stage 13-4B 공통 Bulk 변위를 사용한다.

```text
detailStationaryWorld = worldPosition - windDirection × detailWindSpeed × time
detailUVW = detailStationaryWorld × detailNoiseScale + detailNoiseOffset
detailNoise = SampleDetailErosionNoise(detailUVW)
```

Detail을 Base에 더하면 빈 공간에 새 구름이 생겨 큰 실루엣과 작은 표면의 역할이 섞인다. 따라서 오직 빼는 침식으로 사용한다.

```text
erosion = detailNoise × detailErosionStrength
finalDensity = saturate(baseDensity - erosion)
```

`sampleDetail=false`, `baseDensity<=0`, `detailErosionStrength<=0`이면 Detail 함수 자체를 호출하지 않고 `finalDensity=baseDensity`를 반환한다. 이는 한 밀도 평가 안의 필수 분기이며 큰 step, adaptive stepping과 같은 단계 9 최적화는 아니다.

`SampleValueNoise3D`는 공통 수학, `SampleBaseShapeNoise`는 큰 형태, `SampleDetailErosionNoise`는 표면 전략을 담당한다. 이후 Detail을 fBm이나 Worley로 교체할 때 마지막 함수의 내부만 바꾸고 레이마칭과 `CloudDensitySample`은 유지한다.

## 단계 5: Weather Map과 구름 종류

Legacy Weather Map은 월드 XZ를 16m 주기의 UV로 바꾸어 `t2` RGBA8 texture를 읽는다.
기존 바람 방향의 XZ를 공유하지만 `weatherMapWindSpeed`는 독립적이다. Physical Shape는
`weatherMapWindSpeed` 대신 Stage 13-4B의 공통 `windSpeed` 변위를 사용한다.

```text
weatherUV = frac((worldXZ - normalize(windXZ) × weatherSpeed × time)
                 / weatherWorldSize + weatherOffset)
```

R은 global coverage에 곱해 Base threshold를 바꾸고, B는 `0.5+B`로 해석해
0.5~1.5 밀도 배율을 만든다. 8-bit에서 중립 0.5는 128/255이므로 셰이더가
0/0.5/1 기준값을 복원해 Uniform Legacy가 단계 4와 정확히 같게 한다.

Physical mode는 같은 Weather XZ를 유지한 채 높이별 threshold만 바꾼다.

```text
typedEnvelope = TypedVerticalEnvelope(localHeight, cloudType)
cutoff = ContinuousTypedFootprintCutoff(localHeight, cloudType)
footprintScale = (1 - cutoff) / (1 - middleCutoff)
shapeProfile = saturate(typedEnvelope × footprintScale)
effectiveCoverage = saturate(globalCoverage × weatherR × shapeProfile)
weatherThreshold = RemapCoverage(rawNoise, effectiveCoverage)
weatherDensityMultiplier = 0.5 + weatherB
```

Stratus/Mixed/Cumulus cutoff의 바닥→최대 폭→상단은 각각
`0.16→0.08→0.22@h=0.45`, `0.22→0.04→0.38@h=0.50`,
`0.32→0.03→0.62@h=0.58`이다. 두 구간을 전체 높이에 걸쳐 smoothstep으로 연결해
긴 constant-width plateau를 없앤다. Weather R=1도 `shapeProfile<1`이면 그대로 1로
남지 않는다. Legacy mode만 기존 height cutoff와 profile 밀도 곱을 유지한다.

G는 수직 종류를 정한다. 0은 높이 0.55 전에 사라지는 층운, 0.5는 단계 3
프로파일 그대로, 1은 상단 fade가 늦고 위쪽 질량이 큰 적운이다. 0.5 양쪽을
구간별 보간하므로 기존 높이 기준이 끊기지 않는다.

```text
baseDensity = weatherThreshold × densityMultiplier × weatherDensityMultiplier
finalDensity = saturate(baseDensity - detailErosion)
```

Weather R이 Base를 0으로 만들면 단계 4의 Detail sample skip도 그대로 작동한다.
Weather, Base Noise, 기본 Height, Typed Shape, Effective Shape Coverage,
Base Support와 Detail 중간값은 각각 별도
디버그 출력으로 유지한다.

F2 Weather 창의 Periodic Perlin 프리셋은 CPU에서 채널마다 독립적인 macro/detail 2D gradient
Perlin을 만든다. lattice 좌표를 integer period로 modulo 처리하고 quintic fade를
사용하므로 UV 0/1에서 값과 기울기가 이어진다.

```text
field = lerp(macroNoise, detailNoise, detailWeight)
adjusted = saturate((field - 0.5) × contrast + 0.5 + bias)
R = smoothstep(threshold - softness/2, threshold + softness/2, adjustedR)
B = lerp(adjustedB, R, coverageInfluence)
```

G는 독립 Type field다. R이 1/255 이하인 빈 곳의 G/B는 중립 0.5로 저장하며
A는 항상 1이다. 같은 F2 창의 Channel Debug 프리셋은 canonical 채널 검증을 위해 기존
원형 R과 G/B 띠를 유지한다.

## 단계 6: 태양 Light Ray와 단일 산란

View Ray의 최종 밀도가 0보다 큰 위치에서만 현재 표본→태양 방향으로 보조 레이를
만든다. AABB 이탈점까지의 구간을 최대 16개로 다시 나누며, 고주파 Detail을 제외한
`EvaluateBaseCloudDensity`만 읽는다. 따라서 Weather 배치·Cloud Type·Height가 만든
큰 그늘은 보존하면서 Detail 샘플 비용과 고주파 깜박임은 분리된다.

```text
lightOpticalDepth = Σ(baseDensity × extinctionCoefficient × actualLightStep)
lightTransmittance = exp(-lightOpticalDepth)
```

View 구간의 투과율은 이전 단계와 같다. `extinctionCoefficient=σt`는 소멸 계수
`1/m`, `singleScatteringAlbedo=ω=σs/σt`는 무차원 `[0,1]`이고 실제 산란 계수는
`σs=ωσt (1/m)`다. 한 구간에서 소멸된 빛의 비율에 알베도를 적용한다.

```text
viewStepT = exp(-finalDensity × σt × viewStepLength)
interactionFraction = 1 - viewStepT

stepScattering = viewTransmittance
    × sunColor × sunIntensity
    × lightTransmittance
    × singleScatteringAlbedo
    × interactionFraction
```

이는 `∫ρσs exp(-ρσt s)ds = ω(1-exp(-ρσtΔs))`의 균질 구간 해다.
`σt=0`이면 `interactionFraction=0`이므로 투과율은 1이고 산란도 0이다. 이전의
`density×length` fallback은 물리적으로 `σs=0`인 조건과 충돌하므로 사용하지 않는다.

`directionToSun`은 빛이 내려오는 방향이 아니라 표본에서 태양으로 향하는 정규화
월드 방향이다. Light Ray 시작점은 그 방향으로 기본 `0.01m` 이동해 경계 자기
교차를 피한다. 단계 6은 등방성 직접광만 계산한다. 시선/태양 각도의 Phase
Function은 단계 7, 환경광·다중 산란은 단계 8, Early Exit는 단계 9에서 추가한다.

## 단계 7: Dual-lobe Henyey-Greenstein Phase Function

Phase Function은 빛이 구름 표본에서 어느 방향으로 잘 흩어지는지를 정한다. 방향은
다음 두 월드 단위 벡터로 고정한다.

```text
viewRayDirection = 카메라 → 구름 표본
directionToSun   = 구름 표본 → 태양
cosTheta = dot(viewRayDirection, directionToSun)
```

카메라가 태양을 바라보면 두 벡터가 나란하므로 `cosTheta=+1`이다. 이는 태양에서
표본으로 온 광자가 카메라 방향으로 계속 진행하는 전방 산란에 해당한다. 태양 반대편은
`-1`, 직각은 `0`이다. 코드와 디버그 출력은 이 부호를 반대로 바꾸지 않는다.

`1/(4π)`를 생략한 isotropic-relative Henyey-Greenstein 식을 사용한다. 따라서 `g=0`은
모든 각도에서 정확히 1이며 단계 6 등방성 밝기를 기준으로 비교할 수 있다.

```text
HG(cosTheta, g) =
    (1 - g²) / max(1 + g² - 2g cosTheta, 1e-4)^(3/2)

forward  = HG(cosTheta, clamp(forwardG, 0, 0.95))
backward = HG(cosTheta, clamp(backwardG, -0.95, 0))
dual     = lerp(backward, forward, saturate(phaseBlend))
```

`g`가 +1에 가까우면 태양을 바라보는 좁은 영역에 전방 봉우리가 생기고, -1에 가까우면
반대 방향에 후방 봉우리가 생긴다. 정확히 ±1은 분모가 0이 될 수 있으므로 ±0.95로 제한한다.

```text
phaseFactor = phaseEnabled
    ? lerp(1, clamp(dual, 0, 16), saturate(phaseIntensity))
    : 1

surfaceMask = lightTransmittance ^ edgeOpticalDepthScale
scopedPhase = 1 + (phaseFactor - 1)
    × lerp(1, surfaceMask, edgeInfluence)
stepScattering = stage6StepScattering × scopedPhase
```

Phase Off나 Intensity 0은 배율 1이므로 단계 6 결과를 보존한다. Phase는 직접 산란량만
바꾸고 View/Light 투과율, 광학 깊이와 step 수에는 영향을 주지 않는다. 방향은 View Ray
전체에서 일정하므로 픽셀당 한 번만 계산한다. raw lobe 진단은 16까지 표시하지만 현재 LDR
합성에 적용하는 각도 Phase는 최대 2.5다. Silver Lining은 `edgeInfluence=0.85`로 이 배율을
태양 투과율이 높은 표면에 제한하며 Balanced/Off의 중립값은 기존 결과를 보존한다.
환경광과 다중 산란은 단계 8에서 별도로 더한다.

## 단계 8: 분석적 환경광과 저비용 다중 산란

외부 Cube Map 없이 현재 표본의 정규화 높이 `h`와 최종 밀도로 하늘·지면광을 만든다.

```text
skyWeight = lerp(1, h, ambientHeightInfluence)
groundWeight = 1 - h
localVisibility = exp(-finalDensity × ambientOcclusionStrength)
sunVisibility = lightTransmittance ^ ambientShadowExponent
ambientVisibility = localVisibility
    × lerp(1, sunVisibility, ambientShadowCoupling)
sky = skyColor × skyStrength × skyWeight × ambientVisibility
ground = groundColor × groundStrength × groundWeight × ambientVisibility
```

하늘·지면광은 방향이 없는 근사이므로 Phase를 적용하지 않는다. 각 색은 직접광과 같은
`viewTransmittance × singleScatteringAlbedo × interactionFraction`으로 적분한다.

다중 산란은 추가 Light Ray를 쏘지 않고 이미 계산된 `lightOpticalDepth`를 최대 네 번
다른 감쇠율로 재해석한다. 반복할수록 에너지, 소멸과 Phase 방향성이 감소한다.

```text
octaveLightT = exp(-lightOpticalDepth × extinctionScale)
octavePhase = lerp(1, phaseFactor, phaseScale)
interiorWeight = lerp(1, 1-lightTransmittance,
                      multipleScatteringInteriorBlend)
multiple += sunRadiance × energy × octaveLightT
            × octavePhase × interiorWeight
```

13-5 사용자 검증에서 Balanced의 간접광이 자기 그림자보다 강하고 Silver Lining의 선형 RGB가
LDR 화면의 1을 넘어 흰색으로 잘리는 현상을 확인했다. Portfolio Hero는 차가운 하늘 fill을
남기면서 태양 차폐 내부의 Ambient를 줄이고 Multiple을 밝은 외곽에서 내부로 옮긴다.
Balanced 기본은 새 결합값 0으로 이전 결과를 보존한다. Sky/Ground
`0.12/0.05`, AO `1.50`, Multiple energy/phase decay `0.20/0.25`로 낮췄다. 최종 합성은
RGB peak 0.8 이하는 그대로 두고 그 위만 다음 shoulder로 `[0.8,1)`에 압축한다.

```text
excess = peak - 0.8
mappedPeak = 0.8 + excess × 0.2 / (excess + 0.2)
displayRGB = linearRGB × mappedPeak / peak
```

이는 실제 대기 노출·tone mapping을 대신하지 않는 13-5 LDR 안전장치다. 단계 14에서 실제
하늘 radiance와 출력 변환을 정할 때 교체한다.

다중 산란 octave마다 알베도 거듭제곱을 다시 적용하지 않는다. 단계 13-2에서는 기존
octave 감쇠 모델을 유지하고 카메라로 들어오는 최종 산란 사건에 알베도를 한 번 적용한다.

Off는 Sky/Ground/Multiple을 정확히 0으로 만들어 단계 7 직접광을 보존한다. 이 근사는
실제 간접광 맵이나 IBL이 아니며 단계 14에서 대기·Cube Map 입력으로 교체할 수 있다.
## 단계 13-2 배율 블록화 수치 진단

`Stage13SimilarityGpu`는 320×180 `R32G32B32A32_FLOAT` 출력으로 1×와 나머지
배율을 비교한다. Ray Direction은 각도 오차, Noise UV는 `frac`의 0/1 경계를 원형
거리로 계산하고, Raw Noise와 Final Density는 MAE·P99·임계 초과율과 4방향 연결
mismatch 영역을 계산한다. `CloudHitMask`로 평면층과 교차하지 않은 픽셀은 Noise와
Density 통계에서 제외한다. Lighting과 Composite는 텍스트 보고만 하며 PNG는 만들지 않는다.

수정 전에는 고정 clip과 `x=40×S` target 경로의 최대 CPU 레이 오차가 1000×에서
`1.84654°`까지 증가했다. 레이 생성을 View Space 역투영과 inverse View 회전으로
분리한 뒤 1×~1000× 최대 오차는 모두 `0.000017°` 이하다. 1000× GPU Final Density도
MAE `0.00000025`, 마스크 불일치 `0`, 연결 mismatch `0`으로 자동 게이트를 통과한다.

```text
viewH = NDC × inverseProjection
viewDirection = normalize(viewH.xyz / viewH.w)
worldDirection = normalize(float4(viewDirection, 0) × inverseViewRotation)
```

고정 meter 진단 지면은 배율 대상이 아니므로 순수 구름 상사 GPU 테스트에서는 끈다.
Scene Depth와 건물 폐색은 별도 `Stage13DomainSmoke`가 계속 검사한다. 조명은
`ω×(1-stepTransmittance)`로 전환했다. S배 확대할 때 길이는 S배, `σt`는 `1/S`배,
`ω`는 그대로이므로 직접광·환경광 공통 진폭도 상사 불변이다. Current 1000×의 보고 전용
Accumulated Direct/Composite MAE는 각각 `0.00036347`/`0.00070073`으로 목표 `0.01` 이하다.

## 단계 9: 평면층 View 기본 최적화와 deterministic Light cone

단계 13 승인값 `100m/512 View`, `250m/80 Straight Light`는 `mainReference`에 그대로 남는다.
`mainOptimized`만 다음 순서로 비용을 줄인다.

2026-08-19 사용자 승인 기본값은 Balanced이며 `2×` empty search, `1%` early exit,
`100→150m` 거리 step과 `6탭/2°/원거리 77%` cone을 사용한다. Reference는 시작값이 아니라
이후 저해상도·temporal 단계의 정확도 비교 기준이다.

1. Weather, 로컬 높이와 타입 세로 profile 중 Base 식에 곱해지는 값이 정확히 0이면
   Base/Detail Texture3D를 읽지 않는다. 0이 아닐 때는 같은 Weather 표본과 새 Base 표본을
   `ComposeBaseCloudDensity`에 넣어 Weather를 두 번 읽지 않는다.
2. Full march에서 Base가 epsilon 이하인 표본이 기본 3회 이어지면 Search로 전환한다.
   Search 간격은 `min(fullStep×multiplier, 400m)`이고 Base 후보만 평가한다. 후보를 만나면
   coarse 한 구간을 되감고 Full로 돌아가 경계를 100m 간격으로 다시 읽는다.
3. View 간격은 16~48km에서 `smoothstep`으로 `100m → farMultiplier×100m`가 된다.
   마지막 구간은 `min(step,tEnd-cursor)`로 잘라 전체 거리를 빠짐없이 덮는다.
4. 적분 뒤 `viewT <= transmittanceThreshold`이면 남은 배경 기여가 임계값 이하므로 끝낸다.

각 실제 구간 길이 `Δs_i`를 사용하므로 균일 밀도에서는 가변 분할도
`T = exp(-rho × sigma_t × sum(Δs_i))`와 같다.

4× Search의 400m deterministic midpoint는 Base Texture3D의 최단 파장 약 522m를
Nyquist 조건보다 성기게 읽는다. 2026-08-19 사용자 검증에서 카메라 중심의 거리 껍질과
coarse/full 전환이 등고선·물결무늬로 드러났으므로 Fast/4×는 탈락했다. 활성 최저 비용은
최대 200m인 Balanced/2×이며 4× 구현은 schema 31과 실패 이력 재현에만 남긴다.

Light cone은 시간 jitter 없이 고정 golden angle `2.39996323 rad`를 사용한다. 5/6/8/12개
구간을 근거리 쪽에 `pow(x,1.5)`로 모으고 마지막 표본이 나머지 거리를 담당한다. 각 표본은
담당 길이 `w_i`를 곱하므로 `tau_light = sigma_t × sum(rho_i × w_i)`이고
`sum(w_i)`는 Light 구간 길이와 같다. cone 반경은 `sampleDistance × tan(coneAngle)`이다.
1~4°는 표본 수가 같아 계산량도 같고, 각도를 키우면 태양 직선 주변의 더 넓은 밀도 덩어리를
읽어 평행 대각선 띠를 부드럽게 한다. 기존 Base-only Light 밀도와 `tau>=9.21034` 종료는
유지한다. 자동 화질 스윕에서 6탭 4°/3°는 Cumulus Light T P99 기준을 넘었고, 동일한 6탭
비용의 2°와 원거리 구간 비율 77%가 Dense/Stratus/Cumulus에서 처음으로 P99 0.03 이하를
만족해 Balanced 값이 되었다. temporal jitter, 저해상도, Light Cache와 지면 Cloud Shadow
Map은 단계 10~12 범위다.

## 단계 10: 저해상도 구름 데이터와 공간 업샘플링

단계 10은 View/Light 적분식을 바꾸지 않고 실행하는 화면 레이 수를 줄인다. 선택한 축 비율
`r`에서 구름 픽셀 수는 Full의 `r²`이다. 초기 비교의 50/67/75%는 각각 약
25/44.4/56.25%였으며, 사용자 검증 뒤 활성 후보는 정확한 2:1 확대인 50%와 Full만 남겼다. 각 레이는
최종 장면색 대신 `scattering.rgb`, View `T`, 대표 구름 깊이와 해당 원본 ray의 scene limit을
두 MRT에 쓴다.

대표 깊이는 교차 구간 중점이 아니라 각 View 구간이 만든 불투명도 기여도다.

```text
alpha_i = T_before_i * (1 - T_step_i)
cloudDepth = sum(sampleDistance_i * alpha_i) / sum(alpha_i)
```

`sum(alpha_i)`가 `1e-6` 이하면 빈 레이로 보고 cloud depth와 source scene limit에 같은 장면
제한 거리를 기록한다. 이 값은 색 적분을 바꾸지 않고 업샘플 경계 guide로만 사용한다.

Nearest는 한 texel, Bilinear는 네 texel의 공간 가중합이다. 활성 Joint4는 여기에 다음
가중치를 곱한다.

```text
w = w_spatial * w_sceneClass * w_sceneDepth * w_cloudDepth * w_T
w_depth(a,b,sigma) = exp(-0.5 * (abs(a-b) / (sigma * max(abs(a),abs(b),1m)))^2)
w_T = exp(-0.5 * (abs(Ta-Tb) / sigma_T)^2)
```

하늘/불투명 분류가 다르면 `w_sceneClass=0`이다. 합이 `minimumWeight`보다 작으면 물체 픽셀은
`scattering=0,T=1`로 구름 번짐을 막고 하늘은 최근접 유효 구름 표본을 쓴다. Full은 같은
MRT와 resolve를 지나되 1:1 최근접으로 복원한다. 현재 프레임의 공간 정보만 사용하며 jitter,
history buffer, reprojection과 ghosting rejection은 단계 11에 남긴다.

초기 Joint9 3×3 경로와 67/75% enum은 schema 32와 실패 이력 재현을 위해 보존하지만 활성
F1·자동 후보에서는 제외한다. 정지 화면에서 Nearest/Bilinear/Joint4 차이가 크지 않아 가장 싼
`50% Axis + Nearest`가 2026-08-19 잠정 최종 후보가 됐다.

## 단계 11: 4-phase jitter와 Temporal Reprojection

Stable 4-Phase는 50% 저해상도 texel 중심에 다음 offset을 순환 적용한다.

```text
(-0.25,-0.25), (+0.25,+0.25), (+0.25,-0.25), (-0.25,+0.25)
jitteredUv = cloudUv + jitterLowResTexels / cloudRenderSize
sourcePosition = fullUv * cloudRenderSize - 0.5 - jitterLowResTexels
```

같은 `jitteredUv`로 View Ray와 Full Scene Depth를 읽으므로 한 저해상도 레이의 구름 구간과
장면 제한이 어긋나지 않는다. Full Scene geometry의 projection 자체는 jitter하지 않는다.
저해상도 texel `i`는 `(i+0.5+jitter)/size` 위치를 측정했으므로 Full 공간 복원에서는 jitter를
반드시 뺀다. 이 역보정이 없으면 current가 phase마다 `±0.5 Full pixel` 이동한 것으로 해석되어
불투명 건물 경계가 한 픽셀 왕복한다.

역보정만으로는 건물 바로 옆 low-res ray가 phase마다 geometry와 sky를 번갈아 보는 문제를
해결할 수 없다. Stage 11 resolve는 각 source의 Scene Limit이 finite이고 `[0,far]`인지 확인한 뒤
Full 픽셀과 geometry/sky class가 같은 source만 쓴다. 가까운 동일 표면은 다음 meter fast path로
추가 Full Depth 조회 없이 통과한다.

```text
allowedDepthError = clamp(fullSceneLimit * 0.01, 1m, 10m)
abs(sourceSceneLimit - fullSceneLimit) <= allowedDepthError
```

이 식은 최종 거부 기준이 아니다. 2026-08-22 F8 하향 사선 구도에서는 같은 10km 지면의 인접
ray 거리도 10m 이상 달라져 모든 필터에 수평 invalid 줄이 생겼다. fast path를 넘으면 low-res tap이
실제로 raymarch한 Full 픽셀을 `(tap+0.5+jitter)/lowSize`로 찾고, target D32의 Center/L/R/U/D에서
절댓값이 작은 one-sided slope를 골라 source device depth를 예측한다.

```text
predictedDepth = targetDepth + slopeX*deltaX + slopeY*deltaY
tolerance = 8e-7 + 2e-7*(abs(deltaX)+abs(deltaY))
abs(sourceFullDepth-predictedDepth) <= tolerance
```

Perspective 투영에서 한 삼각형의 device depth는 화면 좌표에 대해 affine이므로 넓은 사선 평면은
통과하고, 건물·지면·하늘의 depth discontinuity는 거부된다. 필요한 축의 slope가 없으면
보수적으로 invalid 처리한다.

Nearest의 최근접 source가 실패하면 3×3을 y-major/x-major로 탐색해 공간상 가장 가까운 유효
source를 고른다. Bilinear는 invalid tap의 weight를 0으로 만들고 나머지를 재정규화하며,
Joint4/보존 Joint9는 soft weight 전에 같은 hard validation을 적용한다. Joint 공간 거리는
`tapPixelIndex-sourcePosition`으로 계산해 texel-index 중심 규칙을 통일한다.

공간 복원한 현재 대표 깊이 `d_cloud`로 월드 위치를 만들고, Physical Shape가 한 프레임 동안
이동한 만큼 반대로 옮겨 같은 밀도 특징의 이전 위치를 찾는다.

```text
P_current  = cameraPosition + rayDirection * d_cloud
P_previous = P_current - normalize(windDirection) * windSpeed * deltaTime
clip_previous = P_previous * previousViewProjection
uv_previous = (clip.xy / clip.w) * (0.5,-0.5) + (0.5,0.5)
```

`clip.w<=0`, 화면 밖, 96px 초과 motion, Scene geometry/sky 불일치, Scene/Cloud 깊이 차이,
T 차이와 1km 이하 near fade는 history를 거부한다. Cloud Depth는 중심 한 값 대신 `T<0.99`인
현재 Full 3×3 대표 깊이의 최소·최대와 상대 margin을 사용한다. current/history가 거의 투명하면
대표 깊이 검사를 생략하고 T와 clipping에 맡긴다. 같은 3×3 `scattering/T` 최소·최대 범위로
history를 clamp한 뒤 다음 EMA로 합친다.

```text
historyClipped = clamp(history, neighborhoodMin, neighborhoodMax)
resolved = lerp(current, historyClipped, historyWeight * nearFade)
```

history에는 합성된 장면색이 아니라 `scattering/T`와 현재 Cloud Depth/Scene Limit을 저장한다.
따라서 다음 프레임에도 구름과 불투명 장면의 경계를 따로 검사할 수 있다. resize, preset,
카메라 cut, 큰 time jump와 shader reload 뒤 첫 프레임은 항상 current 100%다.

유효한 current source가 하나도 없을 때는 invalid Cloud Depth/T를 rejection에 넣지 않는다.
geometry는 Full Scene surface, sky는 far-plane ray point로 history UV를 계산하고 Scene 검사까지
통과한 history를 100% 유지한다. 이때 이전 Cloud Depth는 보존하고 Scene Limit만 현재 Full
픽셀 값으로 갱신한다. history도 읽을 수 없으면 `scattering=0,T=1`로 시작한다.

## 단계 12: Deep Optical-Depth Cache와 표면 Cloud Shadow

태양 방향 단위 벡터를 `L`, 태양에 수직인 두 축을 `R/U`라 한다. Cache 좌표는 다음과 같아
같은 태양 레이 위의 모든 점이 같은 열을 읽는다.

```text
uv(P) = (dot(P-C,R), dot(P-C,U)) / width + 0.5
uv(P+sL) = uv(P)
```

중심 `C`는 카메라 XZ와 층 중간 높이에서 시작해 light-space의 cache texel 단위로 반올림한다.
Near/Far 폭은 반경이 아니라 각각 24km/128km 정사각형 전체 폭이다. Full과 50% Cloud Data는
같은 `C`, 폭, resolution을 사용한다.

Compute 한 thread는 한 XY 열을 맡는다. top slice의 `tau=0`에서 시작해 아래로 내려오며 인접
높이 구간 중앙의 Base-only 밀도를 누적한다. Detail Erosion은 기존 Light Ray와 같이 제외한다.

```text
dy = (top-bottom)/(sliceCount-1)
ds = dy / L.y
tau[i] = min(9.21034, tau[i+1] + densityBase(Pmid) * extinction * ds)
T = exp(-tau)
```

조회할 때 XY는 hardware bilinear, 높이는 인접 두 array slice를 직접 lerp한다. 구름층 아래
표면은 높이를 bottom으로 clamp해 전체 기둥 투과율을 읽는다. Near 가장자리 정규화 거리
0.80까지 Near만, 0.80~0.95에서 Far로 smoothstep 전환하고 Far 0.90~1.00은 `T=1`로 fade한다.
표면 진단 합성은 다음 고정식이며 Cloud history 밖에서 Full Scene 배경에만 적용한다.

```text
surfaceFactor = lerp(1,
    surfaceAmbientFloor + (1-surfaceAmbientFloor)*Tcloud,
    surfaceShadowStrength)
default: ambientFloor=0.35, strength=1.0
```

법선 기반 `N dot L`과 정식 태양·하늘·지면 조명은 단계 14 범위다. 태양 `L.y<sin(3°)`, AABB,
cache 실패에서는 구름이 기존 Direct Light Ray를 사용하고 표면은 중립 `T=1`을 사용한다.

### Cache 배열 디버그 표시

Near/Far 진단은 카메라 레이 중간 표본을 보여 주지 않고 선택한 `Texture2DArray` slice의 XY를
화면 전체에 직접 펼친다. 실제 tau는 최대값 9.21034보다 훨씬 작은 영역이 많아 단순
`tau/9.21034`가 거의 검게 보이므로 다음 노출식을 사용한다.

```text
preview = 1 - exp(-tau * debugExposure)   // 기본 exposure=4
```

검정은 빈 열, 밝은 회색·흰색은 누적 밀도가 있는 열이다. slice 0은 전체 구름층을 지난 표면
Cloud Shadow Map이며, slice 번호가 커질수록 더 높은 지점부터 태양까지의 Light Cache를 본다.
최상단 slice는 정의상 tau 0이므로 검정이 정상이다. Cascade 진단은 불투명 픽셀에는 복원한
표면 월드 위치를, 하늘에는 구름층 중간 높이와 카메라 레이의 교차점을 사용한다.
