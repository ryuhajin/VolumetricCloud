# 아키텍처

현재는 재구축 단계 2다. 승인된 AABB 구간과 깊이 제한을 유지하면서 상수 밀도를 월드 공간 단일 3D value noise 밀도로 교체한다.

## 모듈과 책임

| 모듈 | 책임 |
|---|---|
| `Window` / `Camera` | Win32 입력, 오빗 카메라, 고정 검증 시점, view/projection 제공 |
| `Renderer` | D3D11 장치, 진단 장면, 구름·Noise Lab 패스와 원자적 셰이더 핫리로드 |
| `NoiseLab` | ImGui 조절, 세 축 512² 단면 타깃, WIC PNG와 JSON 내보내기 |
| `CloudParameters` | 80바이트 AABB·noise·바람·step 설정과 디버그 모드 |
| `DiagnosticScene.hlsl` | 평면·박스의 불투명 색상과 장치 깊이 출력 |
| `Ray.hlsli` | 평행축 0 나누기를 피하는 slab Ray-AABB 교차 |
| `CloudParameters.hlsli` / `Noise.hlsli` | 공유 80바이트 CloudCB와 절차적 3D noise·밀도 함수 |
| `VolumetricClouds.hlsl` / `NoiseLab.hlsl` | Beer-Lambert 구름 합성 / XY·XZ·YZ 단면 출력 |
| `Stage1VolumeMath.h` | GPU와 독립적으로 같은 경계 조건과 투과율을 검사하는 CPU 기준 구현 |
| `Stage2NoiseMath.h` | value noise, coverage와 바람 좌표의 CPU 기준 구현 |

## 프레임 순서

1. `Renderer`가 진단 장면을 `R16G16B16A16_FLOAT` 색상 타깃과 `D32_FLOAT` 깊이에 렌더링한다.
2. 깊이 타깃을 DSV에서 해제하고 `R32_FLOAT` SRV로 전환한다.
3. 풀스크린 삼각형이 장면 색상과 깊이를 읽어 월드 레이, 월드 위치와 장면 거리를 복원한다.
4. 레이와 AABB의 진입·이탈 거리를 구하고 이탈을 Scene Depth 거리로 제한한다.
5. 각 월드 샘플을 바람이 적용된 noise UVW로 바꾸고 단일 value noise를 계산한다.
6. coverage threshold와 density multiplier를 적용한 위치별 밀도를 적분한다.
7. Noise Lab이 같은 `CloudCB`와 유효 시간을 사용해 XY/XZ/YZ 단면 타깃을 갱신한다.
8. ImGui가 단면 SRV와 조절 창을 구름 위에 그리고 백버퍼를 Present한다.

## 상수버퍼

### `CameraCB` / `cbCamera` (`b0`, 96바이트)

| 필드 | 타입 | 의미 |
|---|---|---|
| `invViewProj` | `float4x4` | 화면 좌표와 장치 깊이를 월드 좌표로 복원 |
| `cameraPos`, `time` | `float3`, `float` | 월드 레이 원점과 바람 이동용 경과 시간(s) |
| `renderSize` | `float2` | 픽셀 크기와 화면 종횡비 계산 |
| `nearPlane`, `farPlane` | `float`, `float` | meter 단위 카메라 절두체 범위 |

### `CloudParameters` / `CloudCB` (`b1`, 80바이트)

CPU 구조체와 HLSL cbuffer의 16바이트 묶음을 항상 동시에 변경한다.

| 묶음 | 필드 | 기본값과 단계 2 역할 |
|---|---|---|
| 0 | `cloudBoundsMin(float3)`, `densityMultiplier` | `(-2,-1,-2)m`, `1.0`; 경계 최소와 threshold 뒤 밀도 배율 |
| 1 | `cloudBoundsMax(float3)`, `stepSize` | `(2,2,2)m`, `0.10m`; 경계 최대와 목표 간격 |
| 2 | `maxViewSteps`, `extinctionCoefficient`, `transmittanceThreshold`, `debugMode` | `128`, `1.0`, `0.01`, `0`; threshold만 단계 9 예약 |
| 3 | `baseNoiseScale`, `coverage`, `windSpeed`, `noiseOffset` | `0.35 cycle/m`, `0.55`, `0.25m/s`, `0`; noise 형태·이동 |
| 4 | `windDirection(float3)`, `padding` | 정규화 `(0.9701,0,0.2425)`, `0`; 월드 바람 방향과 정렬 |

구조체는 16바이트 묶음 다섯 개다. `transmittanceThreshold`는 단계 9 early exit 전까지 읽지 않고 `padding`은 GPU 정렬에만 사용한다.

### `NoiseLabParameters` / `NoiseLabCB` (`b2`, 32바이트)

| 묶음 | 필드 | 의미 |
|---|---|---|
| 0 | `normalizedSlicePosition(float3)`, `outputMode` | AABB 내부 0~1 교차점과 raw/threshold/final 선택 |
| 1 | `sliceAxis`, `effectiveTime`, `padding(float2)` | XY/XZ/YZ 축과 구름 패스와 공유하는 시간 |

Noise Lab은 `R8G8B8A8_UNORM` 512×512 render target/SRV/staging texture 세 벌을 사용한다. PNG는 이 단면을 WIC로 복사한 관찰용 산출물이며 구름 밀도 입력은 계속 `Noise.hlsli`의 3D 함수다.

## 셰이더 핫리로드

`shaders/` 아래 모든 `.hlsl`과 `.hlsli`의 수정 시간을 재귀 감시한다. 변경 시 모든 VS/PS를 임시 객체로 컴파일하고 전부 성공할 때만 generation 하나로 교체한다. 실패하면 직전 generation을 유지하고 ImGui에 오류를 표시하므로 Noise Lab과 구름이 서로 다른 noise 알고리즘을 사용하는 프레임은 없다.

## 디버그 입력

| 키 | 출력 |
|---|---|
| `0` | 단일 3D noise 구름과 장면 합성 |
| `F1` | 실제 구름 위 Noise Lab 표시/숨김 |
| `1` | 월드 레이 방향 RGB |
| `2` | Scene Depth에서 복원한 월드 거리 |
| `3` | 복원한 월드 위치의 반복 색상 밴드 |
| `4` | 화면 UV |
| `5` | AABB 진입 거리 |
| `6` | Scene Depth로 제한한 이탈 거리 |
| `7` | 실제 step count |
| `8` | 최종 transmittance |
| `9` | 대표 샘플의 최종 noise 밀도 |
| `Z` / `X` | raw noise / coverage threshold 밀도 |
| `C` / `V` | 최종 밀도 / `frac(noiseUVW)` RGB |
| `F5`~`F7` / `F8` | 외부 고정 카메라 / AABB 내부 카메라 |
| `Q` / `W` / `E` | 기본 / 얇은 Z / 두꺼운 Z AABB |
| `R` / `T` | fine 0.025m / coarse 0.5m step |
| `N`, `A`, `S` | 기본 / sparse / dense coverage |
| `D`, `F` | 큰 / 작은 noise 덩어리 |
| `G`, `H`, `K` | 바람 정지 / 빠른 바람 / noise offset |

## 의도적으로 제외한 기능

- 높이 profile, detail noise, weather map, 태양광, phase function과 shadow
- `transmittanceThreshold` early exit와 adaptive stepping
- 저해상도, temporal reconstruction, 영구 캐시와 프리셋
