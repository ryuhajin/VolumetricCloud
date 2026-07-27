# 아키텍처

DirectX 11 풀스크린 픽셀 셰이더에서 넓은 평면 구름층을 레이마칭한다. `Renderer`가 카메라, 구름 파라미터, 영구 캐시와 디버그 UI를 연결한다.

## 모듈

| 모듈 | 역할 |
|---|---|
| `Window` / `Camera` | Win32 메시지, 오빗 카메라, 리사이즈. ImGui가 마우스를 캡처하면 카메라 입력 차단 |
| `Renderer` | D3D11 리소스, 캐시 로드, HLSL 핫 리로드, 3D/weather 생성, 레이마칭, CPU/GPU 계측, UI 합성 |
| `NoiseCacheManager` | `.cso`, 128³/128³ RGBA8 볼륨, 512² weather map, manifest 검증과 세대형 원자적 저장 |
| `DebugUI` | 크기 조절·접이식 F1 편집기, F2 상시 HUD, 반응형 단면 검사, 파라미터·라이팅·프리셋·캐시 명령 |
| `CloudNoise.hlsli` | periodic Value/Worley/FBM, 높이 마스크, 공통 밀도 함수 |

## 시작 및 프레임 순서

캐시는 사용자 `%LOCALAPPDATA%\VolumetricCloud\cache`의 active 세대, 저장소 `assets/noise-cache/bundle`, 런타임 생성 순으로 선택한다. `active-bundle.txt`가 없는 기존 캐시는 `bundle/`로 읽어 호환성을 유지한다. 정상 캐시는 `.cso`로 셰이더를 만들고 Texture3D 초기 데이터로 업로드하므로 시작 시 HLSL 컴파일과 compute dispatch가 없다. 소스가 저장 캐시보다 새로우면 마지막 정상 캐시로 첫 프레임을 Present한 뒤 핫 리로드한다.

프레임 순서는 `UI Begin → 변경 감지 → 필요 시 볼륨/미리보기 갱신 → GPU total 시작 → cloud timestamp/draw → F1·F2 UI draw → GPU total 종료 → Present → 소스 변경 검사`다. 기본 숨김 상태에서는 4-MRT 미리보기도 만들지 않는다. 한 outer disjoint query 안의 timestamp 네 개로 GPU 전체와 cloud draw 시간을 함께 구해 중첩 disjoint query를 피한다. benchmark mode는 query 슬롯에 같은 frame의 CPU 시간과 수집 여부를 기록해 resolve 시 표본을 만들며 HUD와 VSync를 끈다.

## 상수버퍼

### `CameraCB` / `cbCamera` (`b0`, 112바이트)

| 필드 | 타입 |
|---|---|
| `invViewProj` | `float4x4` |
| `cameraPos`, `time` | `float3`, `float` |
| `volumeCenter`, `densityScale` | `float3`, `float` |
| `volumeHalfSize`, `_pad` | `float3`, `float` |

### `CloudParameters` / `CloudCB` (`b1`, 224바이트)

CPU 구조체와 HLSL cbuffer의 16바이트 묶음 순서는 반드시 같다.

| 묶음 | 필드 |
|---|---|
| 0 | `noiseWorldScale`, `basePeriod`, `detailPeriod`, `densityMultiplier` |
| 1 | `noiseCutoffThreshold`, `erosionStrength`, `bottomFade`, `topFade` |
| 2 | `windDirection(float2)`, `windSpeed`, `seed` |
| 3 | `baseOctaves`, `detailOctaves`, `renderMode`, `useTextureCache` |
| 4 | `showBounds`, `lightSteps`, `sunAzimuth`, `sunElevation` |
| 5 | `sunIntensity`, `ambientIntensity`, `phaseG`, `lightAbsorption` |
| 6 | `coverage`, `baseErosion`, `powderStrength`, `multiScatterStrength` |
| 7 | `silverLiningStrength`, `jitterStrength`, `viewSteps`, `skyExposure` |
| 8 | `cloudBaseHeight`, `cloudThickness`, `cloudNoiseWorldSize`, `maxMarchDistance` |
| 9 | `weatherWorldSize`, `weatherCoverageStrength`, `weatherTypeBias`, `heightVariation` |
| 10 | `thicknessVariation`, `horizonFadeStart`, `horizonFadeEnd`, `weatherSeed` |
| 11 | `detailNoiseWorldSize`, `baseNoiseVerticalSize`, `detailNoiseVerticalSize`, `maxViewStepLength` |
| 12 | `localLightDistance`, `cumulusGrowth`, `anvilStrength`, `detailErosionWidth` |
| 13 | `farLightSteps`, `boundaryRefineSteps`, 패딩 2개 |

### `NoisePreviewCB` (`b2`, 16바이트)

`axis`, `slice`, `previewTime`, `source`를 전달한다. 볼륨 생성 시 같은 슬롯에 16바이트 `NoiseVolumeGenerationCB`를 사용한다.

## 캐시 일관성

캐시 v6의 base와 detail은 모두 128³ RGBA8이다. base R에는 Perlin–Worley, G/B/A에는 저·중·고주파 Worley를 저장하고, detail RGBA에는 주파수 6/12/24/48의 침식 옥타브를 저장한다. weather는 512² RGBA8이며 coverage/type/base-height/thickness를 저장한다. 메인 view/light loop는 캐시만 사용한다.

manifest에는 버전, 전체 셰이더 소스 hash, 노이즈 생성 파라미터 hash, 크기와 DXGI format이 들어간다. 저장은 새 `bundle-v6-<generation>.tmp`에 완전한 세트를 쓰고 실제 D3D 리소스로 재로드 검증한 뒤 불변 세대로 게시한다. 마지막에는 `active-bundle.txt`만 `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)`로 교체하며 sharing/access violation은 제한적으로 재시도한다. 활성화에 실패해도 이전 active 세트는 이동하거나 삭제하지 않는다.

캐시되는 셰이더는 main VS/PS, preview VS/PS, base/detail volume CS와 weather CS 일곱 개다. `.cso`는 `D3DReflect`와 실제 shader 생성으로 검증한다.
