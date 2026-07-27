# 아키텍처

DirectX 11 풀스크린 픽셀 셰이더에서 AABB 볼륨을 레이마칭한다. `Renderer`가 카메라, 구름 파라미터, 영구 캐시와 디버그 UI를 연결한다.

## 모듈

| 모듈 | 역할 |
|---|---|
| `Window` / `Camera` | Win32 메시지, 오빗 카메라, 리사이즈. ImGui가 마우스를 캡처하면 카메라 입력 차단 |
| `Renderer` | D3D11 리소스, 캐시 로드, HLSL 핫 리로드, 3D 노이즈 생성, 레이마칭, UI 합성 |
| `NoiseCacheManager` | `.cso`, 128³/64³ RGBA8 볼륨, manifest의 검증·로드·원자적 저장 |
| `DebugUI` | F1 패널, 단면 검사, 파라미터·라이팅·프리셋·캐시 명령 |
| `CloudNoise.hlsli` | periodic Value/Worley/FBM, 높이 마스크, 공통 밀도 함수 |

## 시작 및 프레임 순서

캐시는 사용자 `%LOCALAPPDATA%\VolumetricCloud\cache\bundle`, 저장소 `assets/noise-cache/bundle`, 런타임 생성 순으로 선택한다. 정상 캐시는 `.cso`로 셰이더를 만들고 Texture3D 초기 데이터로 업로드하므로 시작 시 HLSL 컴파일과 compute dispatch가 없다. 소스가 저장 캐시보다 새로우면 마지막 정상 캐시로 첫 프레임을 Present한 뒤 핫 리로드한다.

프레임 순서는 `UI Begin → 변경 감지 → 필요 시 볼륨/미리보기 갱신 → cloud draw → UI draw → Present → 소스 변경 검사`다. 기본 숨김 상태에서는 4-MRT 미리보기도 만들지 않는다.

## 상수버퍼

### `CameraCB` / `cbCamera` (`b0`, 112바이트)

| 필드 | 타입 |
|---|---|
| `invViewProj` | `float4x4` |
| `cameraPos`, `time` | `float3`, `float` |
| `volumeCenter`, `densityScale` | `float3`, `float` |
| `volumeHalfSize`, `_pad` | `float3`, `float` |

### `CloudParameters` / `CloudCB` (`b1`, 128바이트)

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

### `NoisePreviewCB` (`b2`, 16바이트)

`axis`, `slice`, `previewTime`, `source`를 전달한다. 볼륨 생성 시 같은 슬롯에 16바이트 `NoiseVolumeGenerationCB`를 사용한다.

## 캐시 일관성

캐시 v4의 base는 128³ RGBA8이며 R에 Perlin–Worley, G/B/A에 저·중·고주파 Worley를 저장한다. detail은 64³ RGBA8의 침식 옥타브다. 메인 view/light loop는 항상 캐시를 사용하고, 고비용 절차식 평가는 Inspector와 1회 차이 진단에만 사용한다.

manifest에는 버전, 전체 셰이더 소스 hash, 노이즈 생성 파라미터 hash, 크기와 DXGI format이 들어간다. 저장은 `bundle.tmp`에 완전한 세트를 만든 뒤 기존 세트를 교체한다. HLSL 컴파일이나 생성이 실패하면 현재 정상 리소스는 바꾸지 않는다. `Save Noise Cache`를 누르기 전 변경은 현재 세션에만 존재한다.

캐시되는 셰이더는 main VS/PS, preview VS/PS, base/detail volume CS 여섯 개다. `.cso`는 `D3DReflect`와 실제 shader 생성으로 검증한다.
