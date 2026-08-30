# VolumetricCloud

DirectX 11 + HLSL로 볼류메트릭 클라우드를 기능별로 검증하며 다시 구축하는 학습 프로젝트입니다.

작은 구름의 형태·조명은 사용자 승인된 **재구축 단계 8**을 기준으로 보존하고 있으며,
승인된 **단계 13-0 공간 단위 계약**과 **단계 13-1 AABB/평면층 교차** 위에서
13-3 실제 오픈 월드 스케일은 2026-08-11, 13-4B Weather 기반 가변 두께와 3D texture
형태는 2026-08-14, 13-4C 개발 UI와 Local Cloud Inspector는 2026-08-16 사용자 승인을
받았습니다. 이 이력은 보존되며 현재 런타임은 **단계 13-4D 단일 포트폴리오 디버깅 씬**이
Local Inspector를 대체합니다. 단계 13은 2026-08-17, 단계 9 Balanced는 2026-08-19,
단계 11 Jitter·Temporal Reprojection 안정성 기준은 2026-08-23, 단계 12 Cloud Shadow Map과
Deep Light Cache는 2026-08-25 사용자 승인을 받았습니다. 단계 14의 Rayleigh·Mie·오존 대기 LUT,
지면 재질 반사광, 공통 Aerial Perspective와 HDR/ACES 출력은 2026-08-28 사용자 승인을 받아
`stage14` 태그에 고정했습니다. 현재 `feature/stage15-final-quality`에서 세 품질과 네 콘셉트,
Cirrus 물리층과 재현 가능한 최종 검증 절차를 묶고 있습니다. 일반 실행은 `Urban Fair Weather +
Medium + Temporal On + Balanced512`이며, Concept에 맞는 Planar Layer와 64~128km Weather 배치,
결정적 seed로 생성한 Base `128³ RGBA8`, Detail `32³ RGBA8` Texture3D를 사용합니다.
자동 테스트의 이전 단계 경로는 기존 절차 noise/AABB 기준을 유지합니다.
[포트폴리오 구현 계획](doc/VOLUMETRIC_CLOUD_PORTFOLIO_PLAN.md)은
`13 → 9 → 10 → 11 → 12 → 14 → 15` 순서와 단계별 승인 기준을 정의합니다.

- Y=0의 10km×10km 지면과 원점의 20×60×20m 건물 하나로 구성된 불투명 장면
- 샘플 가능한 Scene Depth와 월드 위치 복원
- 화면 UV에서 월드 공간 카메라 레이 생성
- 안전한 slab 방식 Ray-AABB 교차와 카메라 내부 처리
- 별도 32바이트 DomainCB로 선택하는 AABB 회귀 모드와 Open World 1.5~7.5km Y 평면층 교차
- View 50km·40~50km fade·Light 20km 유한 추적과 교차 구간 디버그 출력
- Open World Light `250m/80` 기본 budget과 `62.5m/320` reference GPU 비교
- 길이·step·바람 `×S`, noise 주파수·extinction `÷S`를 함께 적용하는 13-2 상사 프리셋
- Scene Depth보다 뒤쪽 안개를 제외하는 구간 제한
- 월드 위치 기반 단일 저주파 3D value noise
- coverage threshold와 density multiplier
- 월드 바람 방향·속도에 따른 시간 이동
- AABB 월드 Y를 0~1로 바꾸는 높이 비율과 독립적인 상·하단 fade
- 실행 기본 X/Z ±8m 넓은 볼륨과 Q의 ±2m 수치 검증 볼륨
- 큰 형태와 독립적인 고주파 단일 Value Noise Detail Erosion
- Base가 비었거나 Detail이 꺼진 영역의 Detail 샘플 생략
- 별도 16바이트 CloudLodCB와 실제 Detail volume 평균 기반 32~48km 거리 LOD
- 실제 256² RGBA8 Weather Map Texture2D와 linear-wrap 샘플링
- Weather R coverage, G cloud type, B 0.5~1.5 density modifier
- 층운·기존 혼합형·상향 발달 적운의 수직 프로파일 보간
- 별도 64바이트 LightCB와 표본→태양·카메라→표본 방향 규칙
- Base Density만 적분하는 태양 Light Ray와 Beer-Lambert 태양 투과율
- 태양색·세기·산란계수를 적용한 직접 단일 산란
- Phase Off에서 단계 6을 보존하는 Dual-lobe Henyey-Greenstein Phase Function
- 독립적인 전방·후방 g, 혼합 비율, Phase 강도와 LDR highlight shoulder
- 별도 64바이트 EnvironmentCB와 외부 텍스처 없는 하늘·지면 환경광
- 높이 가중치, 밀도 기반 Ambient Occlusion과 광학 깊이 재사용 다중 산란
- 선택한 50/67/75/100% 축 해상도의 RGBA16F scattering/T + RG32F cloud/scene depth MRT
- Nearest/Bilinear/Depth·Cloud·T Joint4/Joint9 Full-resolution 공간 복원
- 50% 2×2 4-phase jitter와 Full RGBA16F/RG16F temporal history ping-pong
- 대표 Cloud Depth·Physical Wind 기반 재투영, Scene/Cloud/T 거부와 neighborhood clipping
- 좌측 상단 Stage 15 compact 상태와 우측 상단 FPS·CPU/GPU Frame·Cloud Raymarch·
  Spatial/Temporal Resolve·GPU Cloud Total 성능 오버레이
- `b13`, `t8~t13`, `s3`의 Transmittance/Multi/Sky View/Sky Irradiance/Aerial R/T LUT
- Physical 태양·하늘광을 공유하는 구름/지면/건물 조명과 Concrete/Grass/Snow/Desert 지면 preset
- Full/Spatial/Temporal 공통 대기 원근, RGBA16F HDR composite와 Exposure/white balance/ACES 최종 출력
- F3 Lighting & Atmosphere의 시간 재생, 대기·지면·Tone 조절과 실제 LUT thumbnail/3D slice 진단
- 원본 noise, threshold, 최종 밀도와 noise UVW 디버그
- ImGui Noise Lab의 XY/XZ/YZ 동기 단면, 높이 출력과 프로파일 곡선
- 공용 `Noise.hlsli` 저장 시 Noise Lab·구름 동시 핫리로드
- 관찰용 512×512 단면 PNG 세 장과 설정 JSON 내보내기

## 빌드와 실행

```powershell
git submodule update --init --recursive
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
.\build\Debug\VolumetricCloud.exe
```

## 실행·Stage 15 검증 조작

처음 실행하는 사용자는 키 목록보다 먼저
[Stage 13-4B 초보자 디버깅 가이드북](doc/STAGE13_4B_DEBUGGING_GUIDE.md)을 따라가세요.
F1~F4 개발 창, 단축키가 표시하는 계산값, 검정/흰색 판독법과 증상별 원인을 설명합니다.
Stage 14의 F3 대기 LUT, 태양 시간, 지면 재질과 HDR 화면 판정은
[Stage 14 대기·지면 조명 사용자 검증 가이드](doc/STAGE14_ATMOSPHERE_VALIDATION_GUIDE.md)를 사용합니다.
Stage 15의 10분 빠른 승인, 품질·콘셉트 상세 판정, 증상별 Debug View와 사용자가 직접 수행하는
최종 캡처는 [Stage 15 프리셋 사용자 검증 가이드](doc/STAGE15_PRESET_VALIDATION_GUIDE.md)를
사용합니다. 가이드는 자동 수치 합격과 사용자가 판단할 화면 미학을 구분합니다.

| 입력 | 동작 |
|---|---|
| 마우스 왼쪽 드래그 | 위치를 고정한 FPS 자유 시점 회전 |
| 휠 | 현재 시선 방향 전진/후진; 이동속도 0.25초분, Shift 4배 |
| `F1` | Noise·밀도·형상·Temporal·VSync·Time 창 표시/숨김 |
| `F2` | Weather Map·Generator 창 표시/숨김 |
| `F3` | Light 후보, XZ 방향광 도식, Phase·환경광 창 표시/숨김 |
| `F4` | Stage 15 프리셋·두 overlay·진단과 카메라 창 표시/숨김 |
| `Q` | Low → Medium → High 품질 순환; Capture/Reference 중에는 무시 |
| `T` | 첫 입력은 Temporal 반전, 두 번째 입력은 진입 전 상태 복원과 override 해제; 진단 중에는 무시 |
| `0` | Composite |
| `1` | Raw Noise |
| `2` | Weather Coverage |
| `3` | Base Density |
| `4` | Detail Noise |
| `5` | Final Density |
| `6` | View Optical Depth |
| `7` | Accumulated Direct |
| `8` | View Transmittance |
| `9` | Light Transmittance |
| `F5` | Hero / Building Depth |
| `F6` | Ground Horizon |
| `F7` | Inside Cloud |
| `F8` | Above / Down |
| `W/S/A/D` | 카메라 전진/후진/좌/우 이동 (`Shift` 4배) |

Open World의 Base와 Detail은 GPU compute shader가 만든 주기적 Texture3D를 사용합니다.
Base R은 Perlin-Worley, G/B/A는 서로 다른 Worley 주파수이며, Detail RGBA는 네 Worley
대역입니다. Similarity 회귀 프리셋은 구형 절차 Value Noise를 유지합니다. Weather Map은
CPU가 만드는 별도 RGBA8 Texture2D이고, Light Ray는 Weather·Type·Height가 적용된 Base만
읽으며 Detail은 생략합니다.

개발 UI는 실제 구름 위에 뜨는 네 독립 창입니다. F1/F3은 각각 하나의 메인 Debug View
콤보를 제공하고 F2는 Periodic Perlin·Channel Debug와 Cloud Type Mode를 관리합니다.
F4 상단은 Stage 15 Concept/Quality, Temporal override, compact/performance overlay와 Advanced
Capture/Reference/Restore를 관리하고, 아래에서 네 고정 카메라와 현재/저장 카메라를 관리합니다.
좌클릭 FPS free-look과 휠·`WASD` 이동을 지원하며 Shift는 표시 속도의 4배입니다.
Base/Detail 파장당 표본, 대표 광학 깊이, View/Light budget과 Weather texel 크기를 함께 표시합니다.
Noise Lab의 `3D Noise Volumes`에서 현재 noise source를 확인하고 `Regenerate 3D Noise`를
실행할 수 있습니다. Procedural Legacy는 Pipeline Compare와 자동 회귀 내부에서만 선택됩니다.
F1의 `Open World Render Pipeline Compare`는 현재 기하와 카메라를 고정한 채 Legacy
1000x→Texture3D→Periodic Weather→Physical Shape→Full Open World를 누적 적용합니다.
Open World 시작값과 `Open World Render Defaults`는 항상 마지막 최신 경로입니다.
F1의 `Temporal`에서 Off/Stable 4-Phase, history weight, near-cloud fade와 reset을 조작하고
phase/history valid/누적 프레임을 확인할 수 있습니다. F3 독립 `Tone Mapping`에도 Exposure와
White Balance 변경 때 history가 유지되는지 바로 볼 수 있는 읽기 전용 Temporal 상태를 표시합니다.
Stage 15 일반 시작값은 `Urban Fair Weather + Medium + Temporal On + Balanced512`입니다.
F1 형상 상태는 `Legacy Normalized Layer`, `Weather Physical Thickness`, `Cirrus Physical Layer`를
구분합니다. Cirrus에서는
Weather/Detail 전용 wind 대신 공통 `Cloud Wind Speed (Bulk)`와 `Bulk travel`을 표시하고, flow·Base/Detail
방향 축척·물리 두께·profile을 읽기 전용으로 확인할 수 있습니다.
F4의 `Compact Stage 15 Overlay`와 `Performance Overlay`는 기본 On입니다. 사용자가 UI 없는
포트폴리오 화면을 찍을 때는 둘 다 Off로 바꾸고 F1~F4 창도 숨깁니다. Capture Still 또는
Reference 중에는 Low/Medium/High와 Q/T를 받지 않습니다. Capture Still은 4개 표본의 장면을
고정해야 하므로 Concept도 잠그고, Reference에서만 Concept 비교를 허용합니다. `Restore
Realtime`은 진입 전에 저장한 Quality와 Temporal 상태·override를 그대로 되돌립니다.
Temporal On/Off의 50% 공간 복원은 Full Scene과 geometry/sky class·D32 surface plane이 맞는
low-res source만 사용하며, On에서 유효 current가 없으면 검증된 Full history를 유지합니다.
`Current Source Validity` debug에서 valid 초록, class 빨강, surface/plane 노랑,
guide/후보 없음 파랑, history 유지 회색을 확인할 수 있습니다.
사용자가 누르는 `Export 4 PNG + JSON`은 전체 snapshot schema 37로 `sceneContract`, 현재/저장
카메라, 단계 9~14 실제 설정과 Stage 15 quality/concept/diagnostic/Temporal override를 저장합니다.
Cirrus는 `cloudShape.mode=cirrusPhysicalLayer`와 flow·Base/Detail 축척·두께·profile의 실제 적용값을
기록하므로 Legacy/Weather Physical로 잘못 해석되지 않습니다.
자동 Stage 15 명령은 네 PNG readback을 호출하지 않는다. preset smoke만 schema 계약을 위해
metadata-only export를 `%TEMP%/VolumetricCloudStage15Schema37-*`에 한 번 쓰고, 나머지 결과는
JSON/CSV로 기록한다. Custom 외형 전용 파일은 schema 29를 유지합니다.

우측 상단 성능 오버레이는 `F1` 창을 숨겨도 유지되며 F4의 `Performance Overlay`로 따로 숨깁니다.
F1의 `Performance` 항목에서는 VSync를 켜거나 끌 수 있습니다. CPU Frame은 `Present`와 VSync 대기를 포함하지만
GPU Frame은 Present를 제외하며, 최적화 비교에는 Shadow Cache+Raymarch+Resolve 세 구간 합인
`GPU Cloud Total ms`를 사용합니다.
재현 가능한 측정 절차는 [성능 측정 기준](doc/PERFORMANCE.md)에 정리되어 있습니다.

### 13-4B Weather 기반 세로 형상(승인 당시 기준)

이 절의 수치는 Stage 13-4B/13-4E 승인 당시 Open World 기준이며 Stage 15 named Concept의
현재값은 아니다. Stage 15도 같은 Weather/세로 형상 알고리즘을 재사용하지만, 기본 Urban은
1.8~5.5km·Threshold `0.53`, Meadow는 1.5~6.5km·`0.485`, Desert Cirrus는
7.0~10.5km·`0.52`, Snow는 1.5~4.0km·`0.46`을 각 Concept descriptor에서 적용한다.

Weather Map A는 `Local Thickness Potential`이다. 승인 당시 Open World는 밑면 1,500m를 공유하고
Type 0의 층운은 1~2km, Type 1의 적운은 2~6km 범위에서 A로 실제 두께를 고른다.
Type은 두께뿐 아니라 Stratus/Mixed/Cumulus shape profile과 높이별 footprint를
함께 구동한다. 높이별 footprint는 Base Noise 통과 coverage에 20%만 반영하고, 타입별 세로
profile은 Base density에 정확히 한 번 곱한다.
Base Texture3D는 XYZ 12km 등방 타일과 `4/9/17/23` 주파수, octave seed 간격 173을 사용하고,
Detail은 2km와 `2/3/4/5`를 사용한다. 100m View step에서 가장 높은 대역도 Base 5.22,
Detail 4 samples/wavelength를 확보한다. Noise Lab의 `Height profile`에서
`Weather Thickness Potential`, `Local Thickness`, `Local Height`, `Typed Shape Profile`,
`Effective Shape Coverage`, `Base Support Before Density`, `Base Shape Before Detail`을
확인할 수 있다. Similarity는 Legacy shape mode를 유지한다.
Physical Shape의 `Cloud Wind Speed (Bulk)`는 Weather R/G/A와 Base/Detail Texture3D를
같은 XZ 거리로 이동시킨다. Weather/Detail 전용 속도는 Legacy에서만 사용하므로 Open
World의 구름 외곽과 내부 밀도가 서로 미끄러지지 않는다.
Stage 13-4B 승인 Coverage 기본은 `4/11/0.42/-0.02/1.15`, Threshold/Softness `0.56/0.14`이고 F2의
`Thickness-Coverage Link` 기본 `0.20`은 독립 두께 80%와 Coverage 중심 20%를 섞는다.

## 자동 검사

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
build/Release/VolumetricCloud.exe --stage14-atmosphere-smoke-test
build/Release/VolumetricCloud.exe --stage14-performance-test
build/Release/VolumetricCloud.exe --stage15-preset-smoke-test
build/Release/VolumetricCloud.exe --stage15-quality-test
build/Release/VolumetricCloud.exe --stage15-performance-test
build/Release/VolumetricCloud.exe --stage15-stage14-regression-probe
build/Release/VolumetricCloud.exe --shader-cache-smoke-test
```

Stage 14 성능 명령은 1920×1080에서 UI/VSync를 끄고 Stage 12와 같은 일곱 장면의
`ManualReference` 호환 경로와 `Physical EarthClear` 경로를 각각 120프레임 warmup 뒤
600개의 고유 GPU timestamp로 측정합니다. PNG를 만들지 않으며 로컬 JSON/CSV만 저장합니다.
Stage 15 smoke는 네 콘셉트×세 품질×F5~F8 48조합과 8.5km Cirrus 내부 카메라를,
quality는 1080p Full Reference 대비 산란 SSIM/정규화 RMSE와 T MAE/P99를 검사합니다.
performance는 같은 48조합을 각 120 warmup+600개의 고유 timestamp로 측정하며 주 결과를
`captures/stage15/*.json,csv`에 기록합니다. preset smoke의 schema 검사용 metadata-only JSON은
위 임시 폴더에 따로 남지만, 자동 명령은 포트폴리오 PNG를 만들지 않습니다.
regression probe는 DenseHorizon을 같은 표본 수로 세 번 측정해 Cloud p95 중앙값과
Shadow/Raymarch/Resolve 분해값을 기록합니다. shader-cache smoke는 cold bytecode hash를
보존한 채 warm validation의 런타임 compile 호출이 0회인지 검사합니다.

`Foundation*`부터 `Stage8*`까지는 승인된 이전 단계 회귀를 검사합니다.
`Stage13ScaleMath`는 단위 계약과 바람 좌표 불변식을, `Stage13CloudDomainMath`는 평면층
경계 조건을, `Stage13DomainSmoke`는 b1/b3/b5의 1×~1000× 런타임 프리셋과 HLSL 출력을 검사합니다.
단계 13-4B는 기존 31개 회귀와 `Stage13WeatherShapeMath`, `Stage13WeatherShapeGpu`,
`Stage13CameraControlMath`를 합친 Debug/Release 34개 테스트를 사용합니다. 새 테스트는
메모리 규격, Nyquist, CPU/GPU 기준 복셀, 채널 분산, 결정적 재생성 hash, 주기 seam,
테스트 전용 cache 왕복, Weather 두께 분포, 타입별 세로 질량, 공통 이동 불변식, GPU 단면과 카메라·줌을
PNG 없이 텍스트로 검사합니다.
`Stage13SimilarityCameraMath`는 double 기준과 translation-free float 레이를 비교하고,
`Stage13SimilarityGpu`는 320×180 float 출력에서 1× 대비 10×/100×/1000× 오차를
텍스트로 보고합니다. PNG는 만들지 않습니다. View Space 역투영과 회전-only World 변환으로
수정한 뒤 Ray·Noise·Density 자동 게이트는 모든 배율에서 통과합니다. 조명 적분은
`singleScatteringAlbedo × (1-stepTransmittance)`로 바꿨으며, 보고 전용 1000×
Accumulated Direct/Composite MAE도 각각 `0.00036347`/`0.00070073`으로 목표 `0.01` 이하다.
단계 11 추가 뒤 전체 Debug 회귀는 45개다. `Stage11TemporalMath`는 4-phase와 jitter Full guide pixel,
D32 plane source gate, 3×3 Cloud Depth 범위, 필터 fallback, invalid-current 정책, wind-aware
reprojection, clip/EMA와 b11 ABI를 검사한다. `Stage11TemporalSmoke`는 Stratus/F5와 F8
기본·yaw ±3°·pitch ±2°의 1920×1080 Scene·960×540 Cloud Data에서 세 필터 4-phase 경계·사선 평면,
디버그 ID 68~73, Off hole, F5/F8 600표본 성능, resize/toggle과 D3D11 오류를 검사한다.

```powershell
ctest --test-dir build -C Debug -R "Stage13Similarity" -V
```

자세한 구조와 단계는 [아키텍처](doc/ARCHITECTURE.md), [AABB 레이 마칭](doc/RAYMARCHING.md), [로드맵](doc/ROADMAP.md)을 참고하세요.
