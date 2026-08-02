# 아키텍처

현재는 재구축 단계 0이다. 구름 밀도나 레이 마칭보다 먼저 불투명 장면, 깊이 복원과 구름 전용 풀스크린 합성 경로를 검증한다.

## 모듈과 책임

| 모듈 | 책임 |
|---|---|
| `Window` / `Camera` | Win32 입력, 오빗 카메라, 고정 검증 시점, view/projection 제공 |
| `Renderer` | D3D11 장치, 진단 장면, 색상·깊이 타깃, 풀스크린 단계 0 패스와 합성 |
| `CloudParameters` | 이후 단계까지 확장할 CPU 구름 설정과 디버그 모드 |
| `DiagnosticScene.hlsl` | 평면·박스의 불투명 색상과 장치 깊이 출력 |
| `VolumetricClouds.hlsl` | 카메라 레이·월드 위치 복원, 디버그 출력과 중립적인 구름 합성 인터페이스 |

## 프레임 순서

1. `Renderer`가 진단 장면을 `R16G16B16A16_FLOAT` 색상 타깃과 `D32_FLOAT` 깊이에 렌더링한다.
2. 깊이 타깃을 DSV에서 해제하고 `R32_FLOAT` SRV로 전환한다.
3. 풀스크린 삼각형이 장면 색상과 깊이를 읽어 월드 레이, 월드 위치와 장면 거리를 복원한다.
4. 최종 모드는 단계 0 진단 결과를 배경에 합성하고, 디버그 모드는 선택한 값을 직접 출력한다.
5. SRV를 해제하고 백버퍼를 Present한다.

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

| 묶음 | 필드 |
|---|---|
| 0 | `cloudBoundsMin(float3)`, `cloudDensity` |
| 1 | `cloudBoundsMax(float3)`, `stepSize` |
| 2 | `maxViewSteps`, `extinctionCoefficient`, `transmittanceThreshold`, `debugMode` |

단계 0에서는 경계·밀도·스텝 필드를 사용하지 않는다. 인터페이스를 먼저 고정하고 단계 1에서 활성화한다.

## 디버그 입력

| 키 | 출력 |
|---|---|
| `0` | 장면과 단계 0 반투명 진단 원판 합성 |
| `1` | 월드 레이 방향 RGB |
| `2` | Scene Depth에서 복원한 월드 거리 |
| `3` | 복원한 월드 위치의 반복 색상 밴드 |
| `4` | 화면 UV |
| `F5`~`F7` | 고정 검증 카메라 프리셋 |

## 의도적으로 제외한 기능

- AABB/구형 셸 교차, 밀도와 레이 마칭
- 3D noise, weather map, 태양광과 shadow
- 저해상도, temporal reconstruction, 영구 캐시와 프리셋
