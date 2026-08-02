# 아키텍처

DirectX 11 풀스크린 픽셀 셰이더에서 넓은 평면 구름층을 레이마칭한다. `Renderer`가 카메라, 구름 파라미터, 영구 캐시와 디버그 UI를 연결한다.

## 모듈

| 모듈 | 역할 |
|---|---|
| `Window` / `Camera` | Win32 메시지, 오빗 카메라, 리사이즈. ImGui가 마우스를 캡처하면 카메라 입력 차단 |
| `Renderer` | D3D11 리소스, 캐시 로드, HLSL 핫 리로드, 3D/weather/placement 생성, half-resolution 레이마칭, temporal reconstruction, CPU/GPU 계측, UI 합성 |
| `NoiseCacheManager` | `.cso`, 128³/128³ RGBA8 볼륨, 512² weather/placement map, manifest 검증과 세대형 원자적 저장 |
| `DebugUI` | 크기 조절·접이식 F1 편집기, F2 상시 HUD, 반응형 단면 검사, 파라미터·라이팅·프리셋·캐시 명령 |
| `CloudNoise.hlsli` | periodic Value/Worley/FBM, cellular placement, 공통 바람 좌표, 높이 마스크, 공통 밀도·macro 밀도 함수 |
| `CloudAtmosphere.hlsli` | raymarch와 composite가 공유하는 태양 방향, 분석적 하늘, tone mapping |

## 시작 및 프레임 순서

캐시는 사용자 `%LOCALAPPDATA%\VolumetricCloud\cache`의 active 세대, 저장소 `assets/noise-cache/bundle`, 런타임 생성 순으로 선택한다. `active-bundle.txt`가 없는 기존 캐시는 `bundle/`로 읽어 호환성을 유지한다. 정상 캐시는 `.cso`로 셰이더를 만들고 Texture3D 초기 데이터로 업로드하므로 시작 시 HLSL 컴파일과 compute dispatch가 없다. 소스가 저장 캐시보다 새로우면 마지막 정상 캐시로 첫 프레임을 Present한 뒤 핫 리로드한다.

beauty 프레임 순서는 `UI Begin → 변경 감지 → 필요 시 볼륨/미리보기 갱신 → GPU total 시작 → 0.5배 cloud scattering/metadata MRT → full-resolution temporal resolve → 분석적 하늘 composite → F1·F2 UI → GPU total 종료 → Present → 소스 변경 검사`다. metadata는 첫 구름 거리와 transmittance를 저장한다. debug render mode와 F1 reference 선택은 full-resolution 직접 경로를 사용한다. 한 outer disjoint query 안에서 raymarch, reconstruction과 GPU total timestamp를 함께 수집한다. benchmark는 HUD와 VSync를 끄며 startup diagnostic은 VSync를 유지하고 CPU submit/Present wait를 별도로 기록한다.

## 상수버퍼

### `CameraCB` / `cbCamera` (`b0`, 112바이트)

| 필드 | 타입 |
|---|---|
| `invViewProj` | `float4x4` |
| `cameraPos`, `time` | `float3`, `float` |
| `rayJitterNdc`, `renderSize` | `float2`, `float2` |
| `temporalOutput`, padding | `uint`, `uint3` |

### `TemporalCB` (`b2`, 112바이트)

| 필드 | 타입 |
|---|---|
| `previousViewProj` | `float4x4` |
| `previousCameraPos`, `previousTime` | `float3`, `float` |
| `windDeltaWorld`, `historyWeight`, `historyValid` | `float2`, `float`, `uint` |
| `temporalDebugMode`, padding | `uint`, `uint3` |

### `CloudParameters` / `CloudCB` (`b1`, 272바이트)

CPU 구조체와 HLSL cbuffer의 16바이트 묶음 순서는 반드시 같다.

| 묶음 | 필드 |
|---|---|
| 0 | `noiseWorldScale`, `basePeriod`, `detailPeriod`, `densityMultiplier` |
| 1 | `noiseCutoffThreshold`, `erosionStrength`, `bottomFade`, `topFade` |
| 2 | `windDirection(float2)`, `windSpeed`(km/s, UI는 m/s), `seed` |
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
| 14 | `lightConeRadius`, `ambientOcclusionStrength`, `multiScatterExtinctionAttenuation`, `multiScatterEccentricityAttenuation` |
| 15 | `placementCellCount`, `placementDensity`, `placementRadiusMin`, `placementRadiusMax` |
| 16 | `placementHeightVariation`, `placementTopShrink`, `placementEdgeSoftness`, `placementStrength` |

### `NoisePreviewCB` (`b2`, 16바이트)

`axis`, `slice`, `previewTime`, `source`를 전달한다. 볼륨 생성 시 같은 슬롯에 16바이트 `NoiseVolumeGenerationCB`를 사용한다.

## 캐시 일관성

캐시 v10의 base와 detail은 모두 128³ RGBA8이다. base R에는 Perlin–Worley, G/B/A에는 저·중·고주파 Worley를 저장하고, detail RGBA에는 주파수 6/12/24/48의 침식 옥타브를 저장한다. weather는 coverage/type/base-height/thickness를 저장한다. placement R은 중심 support union이고 G/B/A는 겹친 중심의 radius/height/profile 가중 평균이다. 메인 view/light loop는 캐시만 사용한다.

manifest에는 버전, `CloudAtmosphere.hlsli`를 포함한 전체 셰이더 소스 hash, 노이즈 생성 파라미터 hash, weather/placement 크기와 DXGI format이 들어간다. 저장은 새 `bundle-v10-<generation>.tmp`에 완전한 세트를 쓰고 실제 D3D 리소스로 재로드 검증한 뒤 불변 세대로 게시한다. 마지막에는 `active-bundle.txt`만 `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)`로 교체하며 sharing/access violation은 제한적으로 재시도한다. 활성화에 실패해도 이전 active 세트는 이동하거나 삭제하지 않는다.

캐시되는 셰이더는 main VS/PS, temporal/composite PS, preview VS/PS, base/detail volume CS와 weather CS 아홉 개다. `.cso`는 `D3DReflect`와 실제 shader 생성으로 검증한다.
