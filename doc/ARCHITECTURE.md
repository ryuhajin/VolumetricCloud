# 아키텍처

현재는 재구축 단계 5다. CPU 생성 RGBA Weather Map으로 넓은 구름 배치·종류·밀도를 제어한다.

## 모듈과 책임

| 모듈 | 책임 |
|---|---|
| `Window` / `Camera` | Win32 입력, 오빗 카메라, 고정 검증 시점, view/projection 제공 |
| `Renderer` | D3D11 장치, 진단 장면, 구름·Noise Lab 패스와 원자적 셰이더 핫리로드 |
| `NoiseLab` | ImGui 조절, 세 축 512² 단면 타깃, WIC PNG와 JSON 내보내기 |
| `CloudParameters` | 128바이트 AABB·Base·Detail·Weather·step 설정과 디버그 모드 |
| `WeatherMap` | 256² RGBA8 Uniform/2-scale Periodic Perlin/Channel Debug 픽셀 생성과 해시 |
| `DiagnosticScene.hlsl` | 평면·박스의 불투명 색상과 장치 깊이 출력 |
| `Ray.hlsli` | 평행축 0 나누기를 피하는 slab Ray-AABB 교차 |
| `CloudParameters.hlsli` / `Noise.hlsli` / `Weather.hlsli` | 공유 128바이트 설정과 Base/Detail/Weather 밀도 함수 |
| `VolumetricClouds.hlsl` / `NoiseLab.hlsl` | Beer-Lambert 구름 합성 / XY·XZ·YZ 단면 출력 |
| `Stage1VolumeMath.h` | GPU와 독립적으로 같은 경계 조건과 투과율을 검사하는 CPU 기준 구현 |
| `Stage2NoiseMath.h` | value noise, coverage와 바람 좌표의 CPU 기준 구현 |
| `Stage3HeightMath.h` | 정규화 높이, 상·하단 smoothstep과 밀도 결합의 CPU 기준 구현 |
| `Stage4DetailMath.h` | Detail 좌표·바람, subtractive erosion과 샘플 생략 CPU 기준 구현 |
| `Stage5WeatherMath.h` | Weather UV, coverage remap과 cloud type 프로파일 CPU 기준 구현 |

## 프레임 순서

1. `Renderer`가 진단 장면을 `R16G16B16A16_FLOAT` 색상 타깃과 `D32_FLOAT` 깊이에 렌더링한다.
2. 깊이 타깃을 DSV에서 해제하고 `R32_FLOAT` SRV로 전환한다.
3. 풀스크린 삼각형이 장면 색상과 깊이를 읽어 월드 레이, 월드 위치와 장면 거리를 복원한다.
4. 레이와 AABB의 진입·이탈 거리를 구하고 이탈을 Scene Depth 거리로 제한한다.
5. 각 월드 샘플을 바람이 적용된 noise UVW로 바꾸고 단일 value noise를 계산한다.
6. 월드 XZ로 Weather Map R/G/B를 읽고 종류별 높이 cutoff로 수평 footprint를 만든 뒤 coverage·높이·밀도 배율을 적용한다.
7. Weather Base가 있을 때만 Detail Noise로 깎는다.
8. Noise Lab이 같은 CloudCB·Weather SRV·시간으로 15개 단면 출력을 갱신한다.
9. ImGui가 단면과 실제 RGBA 맵을 그리고 백버퍼를 Present한다.

## 상수버퍼

### `CameraCB` / `cbCamera` (`b0`, 96바이트)

| 필드 | 타입 | 의미 |
|---|---|---|
| `invViewProj` | `float4x4` | 화면 좌표와 장치 깊이를 월드 좌표로 복원 |
| `cameraPos`, `time` | `float3`, `float` | 월드 레이 원점과 바람 이동용 경과 시간(s) |
| `renderSize` | `float2` | 픽셀 크기와 화면 종횡비 계산 |
| `nearPlane`, `farPlane` | `float`, `float` | meter 단위 카메라 절두체 범위 |

### `CloudParameters` / `CloudCB` (`b1`, 128바이트)

CPU 구조체와 HLSL cbuffer의 16바이트 묶음을 항상 동시에 변경한다.

| 묶음 | 필드 | 기본값과 단계 5 역할 |
|---|---|---|
| 0 | `cloudBoundsMin(float3)`, `densityMultiplier` | `(-8,-1,-8)m`, `1.0`; 실행 기본 넓은 경계 최소와 threshold 뒤 밀도 배율 |
| 1 | `cloudBoundsMax(float3)`, `stepSize` | `(8,2,8)m`, `0.10m`; 실행 기본 넓은 경계 최대와 목표 간격 |
| 2 | `maxViewSteps`, `extinctionCoefficient`, `transmittanceThreshold`, `debugMode` | `128`, `1.0`, `0.01`, `0`; threshold만 단계 9 예약 |
| 3 | `baseNoiseScale`, `coverage`, `windSpeed`, `noiseOffset` | `0.35 cycle/m`, `0.55`, `0.25m/s`, `0`; noise 형태·이동 |
| 4 | `windDirection(float3)`, `bottomFadeEnd` | 정규화 `(0.9701,0,0.2425)`, `0.20`; 월드 바람 방향과 바닥 fade 종료 높이 |
| 5 | `topFadeStart`, `heightProfilePadding(float3)` | `0.80`, `(0,0,0)`; 꼭대기 fade 시작 높이와 정렬 예약 값 |
| 6 | `detailNoiseScale`, `detailErosionStrength`, `detailWindSpeed`, `detailNoiseOffset` | `2.5 cycle/m`, `0.25`, `0.45m/s`, `17.3`; 독립 표면 침식 |
| 7 | `weatherMapWorldSize`, `weatherMapWindSpeed`, `weatherMapOffset(float2)` | `16m`, `0.10m/s`, `(0,0)`; Weather 반복 크기·이동·UV offset |

구조체는 16바이트 묶음 여덟 개다. `transmittanceThreshold`는 단계 9 early exit 전까지 읽지 않고 `heightProfilePadding`은 GPU 정렬에만 사용한다.

### Weather Map 리소스

`t2`는 CPU 생성 `DXGI_FORMAT_R8G8B8A8_UNORM` 256² Weather Map이고 `s1`은
linear-wrap sampler다. R/G/B/A는 coverage/cloud type/density source/reserved다.
Scene Color `t0`, Scene Depth `t1`, point-clamp `s0`와 register를 분리한다.

Texture2D와 SRV는 초기화 때 `D3D11_USAGE_DEFAULT`, mip 1개로 한 번만 만든다.
F2~F4 전환이나 Noise Lab 생성기 변경은 먼저 `t2` 바인딩을 해제한 뒤
`UpdateSubresource`로 같은 texture에 256² RGBA만 업로드한다. 생성 설정은 CPU
전용 `WeatherMapGeneratorSettings`이며 128바이트 CloudCB에는 들어가지 않는다.
업로드 전 크기와 RGBA 길이를 검증하고 실패하면 기존 맵과 해시를 유지한다.

### `NoiseLabParameters` / `NoiseLabCB` (`b2`, 32바이트)

| 묶음 | 필드 | 의미 |
|---|---|---|
| 0 | `normalizedSlicePosition(float3)`, `outputMode` | AABB 내부 교차점과 15개 density/Weather 출력 선택 |
| 1 | `sliceAxis`, `effectiveTime`, `padding(float2)` | XY/XZ/YZ 축과 구름 패스와 공유하는 시간 |

Noise Lab은 512² 단면 타깃 세 벌과 실제 Weather SRV를 사용한다. Periodic Perlin의
R/G/B seed·주기·가중치·bias·contrast와 coverage threshold/softness, density의
coverage influence를 편집한다. Live Update는 CPU 생성·업로드를 최대 10Hz로
제한하고 조작이 끝난 값은 즉시 반영한다. 내보내기는 세 단면과 256²
`weather-map.png`, 모든 생성 설정과 맵 해시를 담은 schema 5 JSON을 기록한다.
Generator는 `Weather map`과 분리된 최상위 헤더로 기본 펼쳐지고, 그 안의 R/G/B
채널은 각각 기본으로 접힌다. 헤더와 생성 설정은 F2/F4에서도 조작할 수 있으며,
이때 바꾼 값은 보존되고 F3로 돌아오면 Periodic Perlin에 반영된다. 상위 헤더를
접으면 채널과 적용 버튼이 모두 숨겨지지만 sanitize와 보류 중인 Live Update
처리는 표시 상태와 독립적으로 계속된다.

XY/XZ/YZ 단면과 Output·Slice 조작부는 항상 표시한다. 그 아래 `Shared cloud
parameters`, `Height profile`, `Detail erosion`, `Animation` 네 대분류는 독립적으로
접고 펼칠 수 있고 최초에는 모두 펼쳐진다. 헤더를 접어도 CPU 시간, Slice
애니메이션, CloudParameters와 3D preview 갱신은 중단하지 않는다.

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
| `B` / `M` | 대표 위치의 정규화 높이 / 상·하단 높이 마스크 |
| `J` / `L` | Detail 전 Base Density / 실제 샘플된 Detail Noise |
| `P` / `U` | Erosion 양 / Detail 함수 실행 마스크 |
| `I` / `O` | Weather Coverage R / Cloud Type G |
| `Shift+I` / `Shift+O` | Weather Threshold / Typed Height Profile |
| `F2`~`F4` | Uniform Legacy / Periodic Perlin / Channel Debug Weather Map |
| `F5`~`F7` / `F8` | 외부 고정 카메라 / AABB 내부 카메라 |
| `Y` / `Q` | 실행 기본 넓은 XZ / 작은 수치 검증 AABB |
| `W` / `E` | 얇은 Z / 두꺼운 Z AABB |
| `R` / `T` | fine 0.025m / coarse 0.5m step |
| `N`, `A`, `S` | 기본 / sparse / dense coverage |
| `D`, `F` | 큰 / 작은 noise 덩어리 |
| `G`, `H`, `K` | 바람 정지 / 빠른 바람 / noise offset |
| `F9`~`F12` | Detail Off / 기본 / Fine / Strong Erosion |

`F5`~`F12`는 Noise Lab이 표시되고 ImGui 조절이 포커스를 가져도 작동하는 전역
검증 단축키다. Windows가 메뉴 키로 예약한 `F10`의 `WM_SYSKEYDOWN` 경로도
같이 처리한다. Detail 프리셋은 J/L/P/U 디버그 출력을 바꾸지 않아 같은
중간 값에서 `F9 ↔ F12`를 비교할 수 있다. 최종 합성으로 돌아갈 때는 `0`을 누른다.

## 의도적으로 제외한 기능

- 외부 Weather PNG 로딩·페인팅·precipitation, fBm/Worley, 태양광, phase function과 shadow
- `transmittanceThreshold` early exit와 adaptive stepping
- 저해상도, temporal reconstruction, 영구 캐시와 프리셋

실행 기본 `Y` 볼륨은 X/Z `±8m`로 15×15m 진단 바닥을 덮는다. `Q`는 기존
X/Z `±2m` 수치 검증 범위를 보존한다. 두 프리셋의 Y `-1~2m`와 높이 프로파일은
같으며, 넓은 볼륨의 긴 레이는 128 step 상한 때문에 실제 간격이 `0.10m`보다
커질 수 있다.
