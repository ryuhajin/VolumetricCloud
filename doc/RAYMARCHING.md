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
