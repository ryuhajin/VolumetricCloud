# 프로젝트 현황 & 프레임 드로우콜 순서

> 오랜만에 복귀했을 때 "지금 어디까지 됐고, 한 프레임에 뭐가 어떤 순서로 그려지는가"를
> 빠르게 되짚기 위한 온보딩 문서. (Stage 2 기준 / 브랜치 `feature/noise-debug-ui`)

---

## 1. 지금 어디까지 되어 있나

절차식 노이즈 기반 **AABB 박스 볼륨 레이마칭** 볼류메트릭 구름 렌더러가 동작하며,
노이즈를 3D 텍스처로 미리 구워두는 **캐시 시스템**과 실시간 튜닝용 **ImGui 디버그 UI**까지
붙어 있는 상태.

| 영역 | 상태 | 핵심 파일 |
|------|------|-----------|
| 레이마칭 렌더 | ✅ AABB 슬랩 교차 → 64스텝 마칭 | `shaders/VolumetricClouds.hlsl` |
| 절차식 노이즈 | ✅ Perlin-Worley(base) + Worley FBM(detail) | `shaders/CloudNoise.hlsli` |
| 조명 | ✅ 라이트 마칭 셀프섀도우 + Henyey-Greenstein 위상 | `shaders/VolumetricClouds.hlsl` |
| 노이즈 3D 캐시 | ✅ 128³ base + 64³ detail, 컴퓨트로 굽기 | `shaders/NoiseVolumeCS.hlsl`, `src/NoiseCacheManager.*` |
| 영구 캐시 | ✅ `.cso`(셰이더) + `.vcnoise`(볼륨) 저장/로드 | `src/NoiseCacheManager.*` |
| 디버그 UI | ✅ 렌더/노이즈 인스펙터/파라미터/스탯 4탭 + 프리셋 | `src/DebugUI.*` |
| 셰이더 핫리로드 | ✅ 파일 수정 시각 감지 → 자동 재컴파일 | `src/Renderer.cpp` `CheckShaderHotReload` |
| 노이즈 미리보기 | ✅ 3D 볼륨 2D 슬라이스를 4-MRT로 시각화 | `shaders/NoisePreview.hlsl` |

**브랜치 맥락**: 현재 `feature/noise-debug-ui`. `DebugUI`, `NoiseCacheManager`,
`CloudNoise/NoisePreview/NoiseVolumeCS` 셰이더, `imgui` 서브모듈 등 다수 신규 파일이
아직 커밋되지 않은 작업 중 상태(`git status` 참고).

캐시/컴파일 상세는 [SHADER_CACHE.md](./SHADER_CACHE.md) 참고.

---

## 2. CPU 파이프라인 (간략)

```
main.cpp
  └ Window / Camera / Renderer 생성
      └ Renderer::Init()
          ├ D3D11 디바이스 + 스왑체인 생성 (1280x720, R8G8B8A8, FLIP_DISCARD)
          ├ NoiseCacheManager::Init() → LoadPreferred()  ← 캐시 로드 시도
          │     ├ 성공: CreateShadersFromBlobs()  (컴파일 없이 .cso 블롭에서 셰이더 생성)
          │     └ 실패: CreateShaders(true)        (HLSL 런타임 컴파일)
          ├ 상수버퍼 / 샘플러 / 노이즈·프리뷰 텍스처 생성
          └ DebugUI::Init()  (ImGui + Win32 + DX11 백엔드)
      └ while (ProcessMessages()) → Renderer::Render(camera, elapsed)   ← 매 프레임
```

주요 GPU 리소스 바인딩:

| 슬롯 | 이름 | 내용 | 크기/차원 |
|------|------|------|-----------|
| b0 | CameraCB | invViewProj, cameraPos, 볼륨 AABB, densityScale | 112 B |
| b1 | CloudCB | 노이즈/모양/바람/조명 파라미터 (= `CloudParameters`) | 96 B |
| b2 | NoisePreviewCB / NoiseVolumeGenerationCB | 프리뷰 슬라이스 / 볼륨 생성 인자 | 16 B |
| t0 | baseNoiseTexture | base 노이즈 3D 볼륨(실루엣 단일값) | 128³ **R8** (2MB) |
| t1 | detailNoiseTexture | detail 노이즈 3D 볼륨(Worley 옥타브 R/G/B/A) | 64³ RGBA8 (1MB) |
| s0 | noiseVolumeSampler | Linear + Wrap | — |
| u0 | outputVolume | 노이즈 굽기용 RWTexture3D | 128³ or 64³ |

> `CameraCB`는 HLSL의 `mul(vector, matrix)` 규약에 맞추려고 DirectXMath 행렬을
> **transpose 해서** 업로드한다 (`Renderer.cpp:529`).

---

## 3. 한 프레임에 GPU가 하는 일 (드로우콜 순서)

`Renderer::Render()` (`src/Renderer.cpp:467-598`) 실제 순서.
굵게 표시한 항목이 실제 **디스패치/드로우콜**.

```
① ImGui BeginFrame → DebugUI.Draw()
      └ 파라미터 변경 감지 → 노이즈 생성 파라미터가 바뀌면 m_noiseCacheDirty = true
② 캐시 액션 처리 (rebuild / revert / save 버튼)
③ 상수버퍼 갱신:  CameraCB(b0) 업로드 + CloudCB(b1) 업로드
④ [dirty 시]  GenerateNoiseVolumes()  ★ 컴퓨트 디스패치 ×2
                ├ 128³ base 볼륨   → Dispatch(32,32,32)   (numthreads 4×4×4)
                └ 64³  detail 볼륨 → Dispatch(16,16,16)
⑤ [패널 열림 & dirty 시]  RenderNoisePreview()  ★ 4-MRT 드로우 1회 (256×256)
⑥ 뷰포트 설정 → RTV(백버퍼) 클리어 (검정)
⑦ 메인 패스:
      VSSetShader(Fullscreen)  +  PSSetShader(VolumetricClouds)
      바인딩: b0/b1, t0/t1, s0
      ★ Draw(3, 0)   — 화면을 덮는 삼각형 1개 = 픽셀마다 레이마칭
⑧ DebugUI.EndFrame()  (ImGui 오버레이 합성)
   → Present(1, 0)  (vsync)
   → CheckShaderHotReload()  (HLSL 파일 수정됐으면 재컴파일)
```

핵심 포인트:
- **정점 버퍼가 없다.** VS가 `SV_VertexID`로 풀스크린 삼각형을 직접 만든다
  (`IASetInputLayout(nullptr)`).
- ④·⑤는 **조건부**다. 노이즈 파라미터를 바꾸거나 디버그 패널이 열려 있을 때만 실행되고,
  평상시엔 ⑥⑦만 돈다 → 매 프레임 실제 드로우콜은 사실상 **메인 레이마칭 1개 + ImGui**.
- ④에서 SRV(t0/t1)를 먼저 null로 풀고 UAV(u0)로 바인딩한다. 같은 텍스처를
  읽기(SRV)/쓰기(UAV)로 동시에 걸 수 없기 때문 (`Renderer.cpp:370-371`).

---

## 4. HLSL 셰이더별 역할

| 파일 | 스테이지 | 엔트리 | 무슨 셰이더인가 |
|------|----------|--------|-----------------|
| `Fullscreen.hlsl` | VS | `main` | `SV_VertexID`로 화면 덮는 삼각형 생성 (정점버퍼 불필요) |
| `VolumetricClouds.hlsl` | PS | `main` | **메인 구름 레이마칭** (아래 상세) |
| `Ray.hlsli` | (include) | — | `RaySphere` / `RayBox`(슬랩) 광선-도형 교차 유틸 |
| `CloudNoise.hlsli` | (include) | — | 노이즈 함수 + 구름 밀도 평가 + HG 위상 함수 |
| `NoiseVolumeCS.hlsl` | CS | `CSMain` | 노이즈를 3D 텍스처(u0)에 **구워 캐시** |
| `NoisePreview.hlsl` | VS+PS | `VSMain`/`PSMain` | 3D 볼륨의 2D 슬라이스를 4-MRT로 **미리보기** |

### 메인 픽셀 셰이더 — `VolumetricClouds.hlsl` 흐름
1. **레이 생성**: 화면 UV → NDC → `invViewProj`로 역투영해 near/far 지점 구함 →
   `ro = cameraPos`, `rd = normalize(far - near)`.
2. **하늘 배경**: 광선 고도로 지평선↔천정 그라디언트 계산 (구름에 안 맞으면 이 색).
3. **볼륨 교차**: `RayBox`(슬랩법)로 AABB 진입/탈출 구간 `[t0, t1]` 계산.
4. **레이마칭**: `[t0,t1]`을 **64스텝** 등간격 행진. 각 지점을 `[0,1]` uvw로 정규화 →
   `EvaluateCloudComponents`로 밀도 샘플 (절차식 계산 or 캐시 텍스처 — `useTextureCache`로 분기).
5. **셀프섀도우**: 밀도가 있는 스텝마다 태양 방향으로 `LightTransmittance` 라이트 마칭
   (`lightSteps` 1~12회) → 그림자.
6. **합성**: Beer-Lambert 투과율 + Henyey-Greenstein 위상 함수로 단일 산란 누적.
7. **디버그 모드 8종** (`renderMode`): 최종 구름 / 밀도 / base / detail / 높이 마스크 /
   투과율 / 캐시vs절차식 차이 / 심(seam) 오차 시각화.

### `CloudNoise.hlsli` 가 제공하는 것
- 노이즈: `Hash31`, `PeriodicValueNoise3D`, `PeriodicFBM`, `PeriodicWorley3D`,
  `PeriodicWorleyFBM`, `PeriodicPerlinWorley` — 전부 **주기적(타일 이음매 없음)** 버전.
- 밀도 조각: `ShapeCloudComponents`(noise cutoff 임계), 침식(erosion), `HeightGradient`(상하 페이드).
- `HenyeyGreenstein` 위상 함수, `EvaluatePeriodicSeamError`(심 디버그).

---

## 5. 디버그 UI & 파라미터

`src/DebugUI.cpp` — **F1**로 토글. 4개 탭:
- **Render**: 렌더모드 선택, 캐시 텍스처 on/off, AABB 표시, 애니메이션 정지+시간 슬라이더.
- **Noise Inspector**: 슬라이스 축(XY/XZ/YZ)·위치, 4장 미리보기(base/detail/height/density).
- **Parameters**: 노이즈·모양·바람·조명 전 파라미터 + 프리셋 저장/불러오기.
- **Stats**: FPS, CPU 프레임타임, 캐시 상태, 캐시 저장/되돌리기/재생성 버튼.

프리셋은 `%LOCALAPPDATA%\VolumetricCloud\cloud-presets.ini` (INI 포맷).

`CloudParameters` (`src/CloudParameters.h`, 96 B — HLSL `CloudCB`와 바이트 일치):

| 그룹 | 필드 |
|------|------|
| 노이즈 | `noiseWorldScale`, `basePeriod`, `detailPeriod`, `baseOctaves`, `detailOctaves`, `seed` |
| 모양 | `noiseCutoffThreshold`, `erosionStrength`, `bottomFade`, `topFade`, `densityMultiplier` |
| 바람 | `windDirection`(2D), `windSpeed` |
| 조명 | `sunAzimuth`, `sunElevation`, `sunIntensity`, `ambientIntensity`, `phaseG`, `lightAbsorption`, `lightSteps` |
| 렌더 | `renderMode`, `useTextureCache`, `showBounds` |

기본 프리셋: `DefaultCloudParameters` / `CumulusCloudParameters` / `StratusCloudParameters`.

---

## 참조 코드
- 프레임 순서: `src/Renderer.cpp:467-598`
- 노이즈 굽기 디스패치: `src/Renderer.cpp:368-397`
- 노이즈 미리보기(4-MRT): `src/Renderer.cpp:399-430`
- 셰이더 생성/컴파일: `src/Renderer.cpp:205-275`
- 핫리로드: `src/Renderer.cpp:287-313`
