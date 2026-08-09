# 단계 1: 상수 밀도 AABB 레이 마칭

단계 0에서 복원한 정규화 월드 레이와 meter 단위 Scene Depth를 사용해, 축 정렬 박스 안의 일정한 안개만 적분한다.

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

`transmittanceThreshold=0.01`은 단계 9에서 충분히 불투명해진 레이를 일찍 끝내기 위한 예약 값이다. 단계 1에서는 정확한 전체 구간 비교를 위해 사용하지 않는다.

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

Detail은 같은 Value Noise 수학을 별도 고주파 좌표에서 평가한다. 바람 방향은 공유하지만 속도·scale·offset은 독립적이다.

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

Weather Map은 월드 XZ를 16m 주기의 UV로 바꾸어 `t2` RGBA8 texture를 읽는다.
기존 바람 방향의 XZ를 공유하지만 `weatherMapWindSpeed`는 독립적이다.

```text
weatherUV = frac((worldXZ - normalize(windXZ) × weatherSpeed × time)
                 / weatherWorldSize + weatherOffset)
```

R은 global coverage에 곱해 Base threshold를 바꾸고, B는 `0.5+B`로 해석해
0.5~1.5 밀도 배율을 만든다. 8-bit에서 중립 0.5는 128/255이므로 셰이더가
0/0.5/1 기준값을 복원해 Uniform Legacy가 단계 4와 정확히 같게 한다.

```text
cutoff = TypedFootprintCutoff(heightFraction, cloudType)
shapedWeatherR = saturate((weatherR - cutoff) / (1 - cutoff))
effectiveCoverage = globalCoverage × shapedWeatherR
weatherThreshold = RemapCoverage(rawNoise, effectiveCoverage)
weatherDensityMultiplier = 0.5 + weatherB
```

층운 cutoff는 `0.10`으로 일정하다. 혼합형은 바닥/중간/상단 `0.22/0.04/0.38`,
적운은 `0.32/0.03/0.62`를 smoothstep으로 잇는다. 따라서 혼합형과 적운은
중간이 넓고 위아래가 좁아지며, R=1인 Uniform Legacy는 모든 높이에서 정확히
1을 유지한다.

G는 수직 종류를 정한다. 0은 높이 0.55 전에 사라지는 층운, 0.5는 단계 3
프로파일 그대로, 1은 상단 fade가 늦고 위쪽 질량이 큰 적운이다. 0.5 양쪽을
구간별 보간하므로 기존 높이 기준이 끊기지 않는다.

```text
baseDensity = weatherThreshold × typedHeightProfile
              × densityMultiplier × weatherDensityMultiplier
finalDensity = saturate(baseDensity - detailErosion)
```

Weather R이 Base를 0으로 만들면 단계 4의 Detail sample skip도 그대로 작동한다.
Weather, Base Noise, 기본 Height, Typed Height와 Detail 중간값은 각각 별도
디버그 출력으로 유지한다.

F3 Periodic Perlin은 CPU에서 채널마다 독립적인 macro/detail 2D gradient
Perlin을 만든다. lattice 좌표를 integer period로 modulo 처리하고 quintic fade를
사용하므로 UV 0/1에서 값과 기울기가 이어진다.

```text
field = lerp(macroNoise, detailNoise, detailWeight)
adjusted = saturate((field - 0.5) × contrast + 0.5 + bias)
R = smoothstep(threshold - softness/2, threshold + softness/2, adjustedR)
B = lerp(adjustedB, R, coverageInfluence)
```

G는 독립 Type field다. R이 1/255 이하인 빈 곳의 G/B는 중립 0.5로 저장하며
A는 항상 1이다. F4는 canonical 채널 검증을 위해 기존 원형 R과 G/B 띠를 유지한다.

## 단계 6: 태양 Light Ray와 단일 산란

View Ray의 최종 밀도가 0보다 큰 위치에서만 현재 표본→태양 방향으로 보조 레이를
만든다. AABB 이탈점까지의 구간을 최대 16개로 다시 나누며, 고주파 Detail을 제외한
`EvaluateBaseCloudDensity`만 읽는다. 따라서 Weather 배치·Cloud Type·Height가 만든
큰 그늘은 보존하면서 Detail 샘플 비용과 고주파 깜박임은 분리된다.

```text
lightOpticalDepth = Σ(baseDensity × extinctionCoefficient × actualLightStep)
lightTransmittance = exp(-lightOpticalDepth)
```

View 구간의 투과율은 이전 단계와 같고, 직접 산란 적분에는 작은 extinction에서
0으로 나누지 않는 분석적 구간 적분을 쓴다.

```text
viewStepT = exp(-finalDensity × extinction × viewStepLength)
densityIntegral = extinction > epsilon
    ? (1 - viewStepT) / extinction
    : finalDensity × viewStepLength

stepScattering = viewTransmittance
    × sunColor × sunIntensity
    × lightTransmittance
    × scatteringCoefficient
    × densityIntegral
```

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

stepScattering = stage6StepScattering × phaseFactor
```

Phase Off나 Intensity 0은 배율 1이므로 단계 6 결과를 보존한다. Phase는 직접 산란량만
바꾸고 View/Light 투과율, 광학 깊이와 step 수에는 영향을 주지 않는다. 방향은 View Ray
전체에서 일정하므로 픽셀당 한 번만 계산한다. 환경광과 다중 산란은 단계 8에서 별도로 더한다.

## 단계 8: 분석적 환경광과 저비용 다중 산란

외부 Cube Map 없이 현재 표본의 정규화 높이 `h`와 최종 밀도로 하늘·지면광을 만든다.

```text
skyWeight = lerp(1, h, ambientHeightInfluence)
groundWeight = 1 - h
ambientOcclusion = exp(-finalDensity × ambientOcclusionStrength)
sky = skyColor × skyStrength × skyWeight × ambientOcclusion
ground = groundColor × groundStrength × groundWeight × ambientOcclusion
```

하늘·지면광은 방향이 없는 근사이므로 Phase를 적용하지 않는다. 각 색은 직접광과 같은
`viewTransmittance × densityIntegral × scatteringCoefficient`로 적분한다.

다중 산란은 추가 Light Ray를 쏘지 않고 이미 계산된 `lightOpticalDepth`를 최대 네 번
다른 감쇠율로 재해석한다. 반복할수록 에너지, 소멸과 Phase 방향성이 감소한다.

```text
octaveLightT = exp(-lightOpticalDepth × extinctionScale)
octavePhase = lerp(1, phaseFactor, phaseScale)
multiple += sunRadiance × energy × octaveLightT × octavePhase
```

Off는 Sky/Ground/Multiple을 정확히 0으로 만들어 단계 7 직접광을 보존한다. 이 근사는
실제 간접광 맵이나 IBL이 아니며 단계 14에서 대기·Cube Map 입력으로 교체할 수 있다.
