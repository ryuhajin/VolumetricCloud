# High Planar Cloud 레이마칭

현재 셰이더는 meter 단위 Planar layer 하나를 Full resolution에서 추적한다. Stage 1의 AABB 수학은 CPU 학습 테스트로만 남고 런타임 선택지가 아니다.

## 좌표와 단위

| 값 | 단위 |
|---|---|
| 월드 위치, domain 높이, ray 거리, step | meter |
| 바람 속도 | meter/second |
| extinction | `1/meter` |
| Weather UV, coverage, density, transmittance | 무차원 |
| Base/Detail Texture3D 좌표 | 반복되는 0~1 UVW |

광학 깊이의 핵심 곱 `density × extinction × distance`는 무차원이다.

## 카메라 ray와 Scene Depth

Fullscreen pixel의 UV를 NDC로 바꾼 뒤 `invViewProj`로 far point를 복원한다.

```text
ndc = (2u - 1, 1 - 2v, 1, 1)
worldFar = ndc × invViewProj
rayOrigin = cameraPosition
rayDirection = normalize(worldFar.xyz / worldFar.w - rayOrigin)
```

D32 scene depth가 1보다 작으면 같은 UV를 역투영해 카메라부터 첫 불투명 표면까지의 meter 거리를 얻는다. 구름 `tEnd`는 이 거리보다 멀어질 수 없으므로 건물과 지면 뒤의 구름이 새지 않는다.

## Planar layer 교차

```text
bottom = cloudBottomAltitude
top    = bottom + cloudLayerThickness
t0     = (bottom - origin.y) / direction.y
t1     = (top    - origin.y) / direction.y
tStart = max(min(t0, t1), 0)
tEnd   = min(max(t0, t1), externalLimit, domainLimit)
hit    = tEnd > tStart
```

View domain limit는 최대 50km, Light limit는 최대 20km다. 카메라가 layer 안에 있으면 `tStart=0`이다. `abs(direction.y) <= 1e-6`인 수평 ray는 origin Y가 layer 안일 때만 유한 trace limit를 반환한다. 모든 입력은 finite인지 먼저 검사한다.

View 끝의 직선 절단을 감추기 위해 formation이 정한 `fadeStart~maxDistance`에서 다음 값을 density에 곱한다.

```text
u = smoothstep(fadeStart, maxDistance, rayDistance)
distanceFade = 1 - u
```

## Weather와 local column

Weather Map은 월드 XZ와 공통 wind로 이동한다.

```text
stationaryWorld = worldPosition - windDirection × windSpeed × time
weatherUv = frac(stationaryWorld.xz / weatherWorldSize + offset)
```

RGBA 채널:

- R: 구름 support와 수평 coverage
- G: 0 Stratus, 0.5 Mixed, 1 Cumulus
- B: 0.5~1.5 density multiplier
- A: 타입별 min~max 두께를 고르는 potential

한 XZ 기둥의 형상은 다음처럼 만든다.

```text
thickness = lerp(typeMinThickness, typeMaxThickness, weatherA)
baseLift  = min(configuredLift × typeScale × (1-weatherA)^1.5,
                thickness × 0.25)
localBottom = globalBottom + baseLift
localTop    = localBottom + thickness
h = (worldY - localBottom) / thickness
```

Stratus/Mixed/Cumulus의 bottom fade, top fade와 cumulus upper mass를 G로 보간해 vertical profile을 만든다. footprint는 높이에 따라 연속적으로 변하며 `footprintCoverageInfluence`만큼 수평 threshold에 반영된다.

Formation apply 전에는 `maxThickness + maxBaseLift + 200m <= domainThickness`를 검사한다. 이 계약이 상단 절단과 네모난 domain 윤곽을 막는다.

## Base와 Detail density

Weather/local profile이 확실히 0이면 Base Texture3D도 읽지 않는다. 유효한 위치에서 Base density는 다음 순서다.

```text
baseUVW   = frac(stationaryWorld / baseWorldSize + offset)
baseNoise = remap(Base Texture3D RGBA)
coverage' = globalCoverage × weatherFactor × footprintFactor
threshold = remapCoverage(baseNoise, coverage')
baseDensity = insideColumn × weatherSupport × threshold
              × verticalProfile × densityMultiplier × weatherDensity
```

`baseDensity > 0`이고 detail erosion이 0보다 클 때만 Detail Texture3D를 읽는다.

```text
detailUVW = frac(stationaryWorld / detailWorldSize + detailOffset)
boundary  = 1 - smoothstep(0.45, 0.90, baseDensity)
erosion   = detailNoise × detailStrength × boundary
finalDensity = saturate(baseDensity - erosion)
```

모든 거리에서 같은 Detail 경로를 사용한다. 거리 LOD나 fade는 없다.

## High View march

고정 수치:

```text
base step       = 100m
max iterations  = 512
early exit      = T <= 0.01
empty threshold = Base density <= 0.0001
coarse trigger  = 3 consecutive empty samples
coarse step     = min(normal step × 2, 200m)
far step        = 24~50km에서 1×~1.25×
```

거리 step은 다음과 같다.

```text
w = smoothstep(24000, 50000, sampleDistance)
step = 100 × lerp(1, 1.25, w)
```

정상 표본에서 Beer–Lambert 적분:

```text
deltaTau = density × extinction × marchLength
stepT    = exp(-deltaTau)
opacityContribution = accumulatedT × (1 - stepT)
accumulatedT *= stepT
```

대표 cloud depth는 opacity contribution을 weight로 사용한 거리 moment다.

```text
cloudDepth = Σ(distance × opacityContribution)
             / Σ(opacityContribution)
```

빈 표본이 3개 연속이면 coarse search로 전환한다. coarse 위치에서 Base support를 찾으면 cursor를 직전 coarse 구간만큼 되돌리고 정상 step으로 다시 적분한다. 이 rewind가 얇은 구름을 건너뛰는 오류를 막는다.

## 태양광과 Deep Cache

기본 직접광은 Balanced512 Near/Far cache에서 태양 방향 optical depth를 읽는다.

```text
Tsun = exp(-opticalDepth)
```

cache 밖이거나 사용할 수 없는 표본은 8개 deterministic cone interval을 사용한다.

- cone angle 2°
- far sample fraction 0.77
- 시작 bias 1m
- golden-angle disk offset
- 각 tap은 담당 interval 길이로 optical depth를 가중
- optical depth 9.21034037, 즉 `T <= 0.0001`에서 종료

Straight/Reference light ray와 런타임 light sampling preset은 없다.

## 산란과 합성

한 View step의 직접 단일 산란은 다음 관계를 사용한다.

```text
interaction = accumulatedT × incidentSun × albedo × (1 - stepT)
direct = interaction × shapedSunTransmittance × dualLobePhase
```

여기에 physical sky fill, ground bounce, AO와 optical-depth 기반 multiple scattering을 더한다. Cloud PS가 계산한 최종 HDR은 다음과 같다.

```text
cloudOpacity = 1 - cloudT
behindDistance = min(cloudDepth, sceneDistance)
background = HDR scene 또는 atmosphere sky
background = aerialPerspective(background, behindDistance)
hdrCloud = cloudScattering + cloudT × background
```

이 `hdrCloud` 하나를 Tone Map PS가 exposure, Bradford white balance, ACES, sRGB와 dither 순서로 Back Buffer에 출력한다.

## 수치 안전성

- 잘못된 교차, 0 두께, 비정상 방향은 `scattering=0`, `T=1` 중립 결과다.
- 분모는 물리 의미에 맞는 epsilon으로 보호한다.
- density와 optical depth는 음수가 되지 않게 한다.
- 디버그 출력은 NaN/Inf를 자홍색, 음수를 빨강, half-float 초과를 노랑으로 표시한다.
- CPU 테스트는 교차, early exit, skip/rewind, cone interval, formation domain-fit을 독립 검증한다.
