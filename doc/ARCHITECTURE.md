# 아키텍처

현재는 재구축 단계 9이다. 단계 8 조명 결과를 유지하면서 합성 경로의 빈 표본과 불필요한 후반 적분을 줄인다.

## 모듈과 책임

| 모듈 | 책임 |
|---|---|
| `Window` / `Camera` | Win32 입력, 오빗 카메라, 고정 검증 시점, view/projection 제공 |
| `Renderer` | D3D11 장치, 진단 장면, 구름·Noise Lab 패스와 원자적 셰이더 핫리로드 |
| `NoiseLab` | ImGui 조절, 세 축 512² 단면 타깃, WIC PNG와 JSON 내보내기 |
| `CloudParameters` | 128바이트 AABB·Base·Detail·Weather·step 설정과 디버그 모드 |
| `LightParameters` | 64바이트 태양·Light Ray·Dual-lobe Phase 설정 |
| `EnvironmentParameters` | 64바이트 하늘·지면·AO·다중 산란 설정 |
| `FrameProfiler` | CPU Frame과 8-slot 비동기 D3D11 timestamp query, EMA 성능 통계 |
| `WeatherMap` | 256² RGBA8 Uniform/2-scale Periodic Perlin/Channel Debug 픽셀 생성과 해시 |
| `DiagnosticScene.hlsl` | 평면·박스의 불투명 색상과 장치 깊이 출력 |
| `Ray.hlsli` | 평행축 0 나누기를 피하는 slab Ray-AABB 교차 |
| `CloudParameters.hlsli` / `Noise.hlsli` / `Weather.hlsli` | 공유 128바이트 설정과 Base/Detail/Weather 밀도 함수 |
| `LightParameters.hlsli` / `CloudLighting.hlsli` | 공유 64바이트 조명 설정과 Base-only Light Ray |
| `PhaseFunction.hlsli` | 방향 부호를 고정한 전방·후방 HG와 적용 배율 |
| `CloudEnvironment.hlsli` | 높이 환경광, 밀도 AO와 광학 깊이 재사용 octave |
| `VolumetricClouds.hlsl` / `NoiseLab.hlsl` | Beer-Lambert 구름 합성 / XY·XZ·YZ 단면 출력 |
| `Stage1VolumeMath.h` | GPU와 독립적으로 같은 경계 조건과 투과율을 검사하는 CPU 기준 구현 |
| `Stage2NoiseMath.h` | value noise, coverage와 바람 좌표의 CPU 기준 구현 |
| `Stage3HeightMath.h` | 정규화 높이, 상·하단 smoothstep과 밀도 결합의 CPU 기준 구현 |
| `Stage4DetailMath.h` | Detail 좌표·바람, subtractive erosion과 샘플 생략 CPU 기준 구현 |
| `Stage5WeatherMath.h` | Weather UV, coverage remap과 cloud type 프로파일 CPU 기준 구현 |
| `Stage6LightMath.h` | 광학 깊이, Base 선택과 단일 산란 CPU 기준 구현 |
| `Stage7PhaseMath.h` | HG, 방향 내적, Dual-lobe와 안전 범위 CPU 기준 구현 |
| `Stage8AmbientMath.h` | 높이 가중치, AO와 multiple octave CPU 기준 구현 |

## 프레임 순서

1. `Renderer`가 진단 장면을 `R16G16B16A16_FLOAT` 색상 타깃과 `D32_FLOAT` 깊이에 렌더링한다.
2. 깊이 타깃을 DSV에서 해제하고 `R32_FLOAT` SRV로 전환한다.
3. 풀스크린 삼각형이 장면 색상과 깊이를 읽어 월드 레이, 월드 위치와 장면 거리를 복원한다.
4. 레이와 AABB의 진입·이탈 거리를 구하고 이탈을 Scene Depth 거리로 제한한다.
5. 각 월드 샘플을 바람이 적용된 noise UVW로 바꾸고 단일 value noise를 계산한다.
6. 월드 XZ로 Weather Map R/G/B를 읽고 종류별 높이 cutoff로 수평 footprint를 만든 뒤 coverage·높이·밀도 배율을 적용한다.
7. Weather Base가 있을 때만 Detail Noise로 깎는다.
8. 최종 밀도가 있는 View 표본에서 태양 방향 AABB 이탈까지 Base Density를 적분한다.
9. 카메라→표본과 표본→태양 방향 내적으로 픽셀당 Dual-lobe Phase Factor를 한 번 계산한다.
10. 높이·밀도로 하늘/지면 환경광과 AO를 계산하고 기존 광학 깊이로 다중 산란을 근사한다.
11. Direct/Sky/Ground/Multiple을 같은 View 구간에 누적한다.
12. Noise Lab이 단면·조명·환경광 UI와 성능 오버레이를 그린다.
13. GPU Frame timestamp를 닫은 뒤 VSync 설정에 따라 Present한다.

## 프레임 성능 계측

`FrameProfiler`의 CPU 범위는 `Renderer::Render` 시작부터 `Present` 반환까지라서 VSync 대기를
포함한다. GPU Frame 범위는 진단 장면 직전부터 ImGui draw 직후까지이며 Present는 포함하지
않는다. GPU Cloud는 그 안의 `RenderCloudPass`만 측정한다.

GPU 시간은 8개 query 슬롯을 순환하며 완료된 과거 프레임만
`D3D11_ASYNC_GETDATA_DONOTFLUSH`로 읽는다. 준비되지 않은 query 때문에 CPU나 GPU를 기다리지
않으며, ring이 모두 사용 중이면 해당 프레임 계측만 생략한다. 리사이즈 시 통계를 reset하고
이전 세대 결과를 폐기한다. 자세한 비교 절차는 [PERFORMANCE.md](PERFORMANCE.md)를 따른다.

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

| 묶음 | 필드 | 기본값과 현재 역할 |
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

### `LightParameters` / `LightCB` (`b3`, 64바이트)

| 묶음 | 필드 | 기본값과 역할 |
|---|---|---|
| 0 | `directionToSun(float3)`, `sunIntensity` | normalize `(0.45,0.80,0.35)`, `1.0`; 표본→태양 월드 방향과 세기 |
| 1 | `sunColor(float3)`, `scatteringCoefficient` | `(1,0.95,0.85)`, `1.0`; linear RGB와 직접 산란 강도 |
| 2 | `maxLightSteps`, `lightStepSize`, `lightRayBias`, `phaseEnabled` | `16`, `0.25m`, `0.01m`, `0`; Light 품질과 Phase Off 기본값 |
| 3 | `forwardScatteringG`, `backwardScatteringG`, `phaseBlend`, `phaseIntensity` | `0.65`, `-0.25`, `0.80`, `0.25`; 전방·후방 HG와 적용 강도 |

LightCB는 CloudCB와 분리해 `b3`에 바인딩한다. 방향은 빛의 진행 방향이 아니라
현재 표본에서 태양으로 나가는 방향이다. CPU는 방향을 정규화하고 음수·비정상
값을 안전 범위로 제한한다. Light Ray는 `EvaluateBaseCloudDensity`만 호출한다. Phase는
직접 산란량에만 적용하며 Light 투과율과 광학 깊이를 바꾸지 않는다. Phase Off에서는
최종 배율이 정확히 1이다.

### `EnvironmentParameters` / `EnvironmentCB` (`b4`, 64바이트)

| 묶음 | 필드 | 기본값과 역할 |
|---|---|---|
| 0 | `skyColor(float3)`, `skyStrength` | `(0.35,0.50,0.75)`, `0.20`; linear 하늘색과 세기 |
| 1 | `groundColor(float3)`, `groundStrength` | `(0.18,0.12,0.08)`, `0.08`; linear 지면색과 세기 |
| 2 | `ambientOcclusionStrength`, `ambientHeightInfluence`, `multipleScatteringEnabled`, `multipleScatteringOctaves` | `1.25`, `0.65`, `1`, `2`; AO·높이·octave 제어 |
| 3 | `multipleScatteringAttenuation`, `multipleScatteringExtinctionFactor`, `multipleScatteringPhaseFactor`, padding | `0.35`, `0.50`, `0.50`, `0`; 반복 에너지·광학 깊이·방향성 감소 |

EnvironmentCB는 `b4`에 바인딩하며 외부 SRV나 sampler를 추가하지 않는다. 기본 Balanced
프리셋은 환경광을 켜고 Off는 Sky/Ground/Multiple을 0으로 만들어 단계 7 결과를 보존한다.
Cube Map이나 실제 대기 입력은 단계 14에서 `skyColor` 평가만 교체할 수 있다.

### `OptimizationParameters` / `OptimizationCB` (`b5`, 32바이트)

| 16바이트 묶음 | CPU/HLSL 필드 |
|---|---|
| 0 | `earlyExitEnabled`, `supportPrecheckEnabled`, `emptySpaceSkippingEnabled`, `emptySamplesBeforeCoarse` |
| 1 | `baseDensityEpsilon`, `coarseStepMultiplier`, padding 2개 |

`b5`는 렌더 품질 파라미터와 분리된 실행 정책이다. 합성 모드 0과 Optimization On은 `mainOptimized`, Off 또는 진단 모드는 `mainLegacy`를 사용한다. 두 PS는 핫 리로드 시 모두 컴파일·생성된 경우에만 함께 교체된다. Release 런타임 컴파일은 O3, Debug는 Od를 사용한다.
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
`weather-map.png`, 모든 생성 설정·맵 해시·Light/Environment 설정을 담은 schema 8 JSON을 기록한다.
Generator는 `Weather map`과 분리된 최상위 헤더로 기본 펼쳐지고, 그 안의 R/G/B
채널은 각각 기본으로 접힌다. 헤더와 생성 설정은 F2/F4에서도 조작할 수 있으며,
이때 바꾼 값은 보존되고 F3로 돌아오면 Periodic Perlin에 반영된다. 상위 헤더를
접으면 채널과 적용 버튼이 모두 숨겨지지만 sanitize와 보류 중인 Live Update
처리는 표시 상태와 독립적으로 계속된다.

XY/XZ/YZ 단면과 Output·Slice 조작부는 항상 표시한다. 그 아래 `Shared cloud
parameters`, `Height profile`, `Detail erosion`, `Performance`, `Directional light`, `Phase Function`, `Environment & Multiple Scattering`, `Animation` 대분류는 독립적으로
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
| `Shift+J` / `Shift+L` | Light Transmittance / Light Optical Depth |
| `Shift+P` / `Shift+U` | Total Light Samples heatmap / Direct Single Scattering |
| `Shift+B` / `Shift+M` | Phase cosTheta / Forward HG lobe |
| `Shift+C` / `Shift+V` | Backward HG lobe / 최종 Dual Phase Factor |
| `Ctrl+J` | 누적 Direct |
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

- 외부 Weather PNG 로딩·페인팅·precipitation, fBm/Worley와 shadow
- Cube Map/IBL·실제 대기 입력, Light Ray Detail Erosion
- `transmittanceThreshold` early exit와 adaptive stepping
- 저해상도, temporal reconstruction, 영구 캐시와 프리셋

실행 기본 `Y` 볼륨은 X/Z `±8m`로 15×15m 진단 바닥을 덮는다. `Q`는 기존
X/Z `±2m` 수치 검증 범위를 보존한다. 두 프리셋의 Y `-1~2m`와 높이 프로파일은
같으며, 넓은 볼륨의 긴 레이는 128 step 상한 때문에 실제 간격이 `0.10m`보다
커질 수 있다.
