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
