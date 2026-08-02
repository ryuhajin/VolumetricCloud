# 아키텍처

현재는 재구축 단계 1이다. 단계 0에서 승인한 불투명 장면·깊이 복원 위에 AABB 교차와 상수 밀도 레이 마칭을 연결한다.

## 모듈과 책임

| 모듈 | 책임 |
|---|---|
| `Window` / `Camera` | Win32 입력, 오빗 카메라, 고정 검증 시점, view/projection 제공 |
| `Renderer` | D3D11 장치, 진단 장면, 색상·깊이 타깃, 풀스크린 구름 패스와 검증 프리셋 |
| `CloudParameters` | 48바이트 AABB·밀도·step 설정과 디버그 모드 |
| `DiagnosticScene.hlsl` | 평면·박스의 불투명 색상과 장치 깊이 출력 |
| `Ray.hlsli` | 평행축 0 나누기를 피하는 slab Ray-AABB 교차 |
| `VolumetricClouds.hlsl` | 깊이 제한, 상수 밀도 Beer-Lambert 적분, 디버그와 합성 |
| `Stage1VolumeMath.h` | GPU와 독립적으로 같은 경계 조건과 투과율을 검사하는 CPU 기준 구현 |

## 프레임 순서

1. `Renderer`가 진단 장면을 `R16G16B16A16_FLOAT` 색상 타깃과 `D32_FLOAT` 깊이에 렌더링한다.
2. 깊이 타깃을 DSV에서 해제하고 `R32_FLOAT` SRV로 전환한다.
3. 풀스크린 삼각형이 장면 색상과 깊이를 읽어 월드 레이, 월드 위치와 장면 거리를 복원한다.
4. 레이와 AABB의 진입·이탈 거리를 구하고 이탈을 Scene Depth 거리로 제한한다.
5. 유효 구간 전체를 상수 밀도로 적분해 `CloudResult`를 만들고 장면에 합성한다.
6. 디버그 모드는 선택한 중간 값을 직접 출력한다.
7. SRV를 해제하고 백버퍼를 Present한다.

## 상수버퍼

### `CameraCB` / `cbCamera` (`b0`, 96바이트)

| 필드 | 타입 | 의미 |
|---|---|---|
| `invViewProj` | `float4x4` | 화면 좌표와 장치 깊이를 월드 좌표로 복원 |
| `cameraPos`, `time` | `float3`, `float` | 월드 레이 원점과 경과 시간 |
| `renderSize` | `float2` | 픽셀 크기와 화면 종횡비 계산 |
| `nearPlane`, `farPlane` | `float`, `float` | meter 단위 카메라 절두체 범위 |

### `CloudParameters` / `CloudCB` (`b1`, 48바이트)

CPU 구조체와 HLSL cbuffer의 16바이트 묶음을 항상 동시에 변경한다.

| 묶음 | 필드 | 기본값과 단계 1 역할 |
|---|---|---|
| 0 | `cloudBoundsMin(float3)`, `cloudDensity` | `(-2,-1,-2)m`, `0.35`; 경계 최소와 상수 밀도 |
| 1 | `cloudBoundsMax(float3)`, `stepSize` | `(2,2,2)m`, `0.10m`; 경계 최대와 목표 간격 |
| 2 | `maxViewSteps`, `extinctionCoefficient`, `transmittanceThreshold`, `debugMode` | `128`, `1.0`, `0.01`, `0`; threshold만 단계 9 예약 |

구조체 크기는 그대로 48바이트다. `transmittanceThreshold`는 단계 9 early exit 전까지 읽지 않는다.

## 디버그 입력

| 키 | 출력 |
|---|---|
| `0` | 실제 AABB 상수 밀도 안개와 장면 합성 |
| `1` | 월드 레이 방향 RGB |
| `2` | Scene Depth에서 복원한 월드 거리 |
| `3` | 복원한 월드 위치의 반복 색상 밴드 |
| `4` | 화면 UV |
| `5` | AABB 진입 거리 |
| `6` | Scene Depth로 제한한 이탈 거리 |
| `7` | 실제 step count |
| `8` | 최종 transmittance |
| `9` | hit 영역의 상수 밀도 |
| `F5`~`F7` / `F8` | 외부 고정 카메라 / AABB 내부 카메라 |
| `Q` / `W` / `E` | 기본 / 얇은 Z / 두꺼운 Z AABB |
| `R` / `T` | fine 0.025m / coarse 0.5m step |

## 의도적으로 제외한 기능

- 3D noise, weather map, 태양광, phase function과 shadow
- `transmittanceThreshold` early exit와 adaptive stepping
- 저해상도, temporal reconstruction, 영구 캐시와 프리셋
