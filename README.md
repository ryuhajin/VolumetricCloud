# VolumetricCloud

DirectX 11 + HLSL로 볼류메트릭 클라우드를 기능별로 검증하며 다시 구축하는 학습 프로젝트입니다.

작은 구름의 형태·조명은 사용자 승인된 **재구축 단계 8**을 기준으로 보존하고 있으며,
승인된 **단계 13-0 공간 단위 계약**과 **단계 13-1 AABB/평면층 교차** 위에서
13-3 실제 오픈 월드 스케일은 2026-08-11, 13-4B Weather 기반 가변 두께와 3D texture
형태는 2026-08-14, 13-4C 개발 UI와 Local Cloud Inspector는 2026-08-16 사용자 승인을
받았습니다. 이 이력은 보존되며 현재 런타임은 **단계 13-4D 단일 포트폴리오 디버깅 씬**이
Local Inspector를 대체합니다. 기존 13-5 자동 결과는 보존하지만 새 씬에서 재검증·사용자
승인을 기다립니다. 일반 실행은
Planar Layer, 1.5~7.5km 전역 층과 XZ별 1~6km 로컬 두께, 64km Periodic Perlin Weather와 함께 결정적 seed로
생성한 Base `128³ RGBA8`, Detail `32³ RGBA8` Texture3D를 사용합니다.
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
- 우측 상단 FPS·CPU/GPU Frame·GPU Cloud 실시간 성능 오버레이
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

## 단계 13-4D 조작

처음 실행하는 사용자는 키 목록보다 먼저
[Stage 13-4B 초보자 디버깅 가이드북](doc/STAGE13_4B_DEBUGGING_GUIDE.md)을 따라가세요.
F1~F4 개발 창, 단축키가 표시하는 계산값, 검정/흰색 판독법과 증상별 원인을 설명합니다.

| 입력 | 동작 |
|---|---|
| 마우스 왼쪽 드래그 | 오빗 회전 |
| 휠 | 한 notch마다 1.25× 일반 줌 |
| `F1` | Noise·밀도·VSync·Time 창 표시/숨김 |
| `F2` | Weather Map·Generator 창 표시/숨김 |
| `F3` | Light 후보, XZ 방향광 도식, Phase·환경광 창 표시/숨김 |
| `F4` | 카메라 값·고정 뷰·저장 위치 창 표시/숨김 |
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
F4는 네 고정 카메라와 현재/저장 카메라만 관리합니다. 마우스 오빗·휠과 `WASD` 이동을
지원하며 Shift는 표시 속도의 4배입니다.
Base/Detail 파장당 표본, 대표 광학 깊이, View/Light budget과 Weather texel 크기를 함께 표시합니다.
Noise Lab의 `3D Noise Volumes`에서 현재 noise source를 확인하고 `Regenerate 3D Noise`를
실행할 수 있습니다. Procedural Legacy는 Pipeline Compare와 자동 회귀 내부에서만 선택됩니다.
F1의 `Open World Render Pipeline Compare`는 현재 기하와 카메라를 고정한 채 Legacy
1000x→Texture3D→Periodic Weather→Physical Shape→Full Open World를 누적 적용합니다.
Open World 시작값과 `Open World Render Defaults`는 항상 마지막 최신 경로입니다.
`Export 4 PNG + JSON`은 schema 28로 `sceneContract`, 현재/저장 카메라,
`cloudTypeMode`, `openWorldPipelinePreset`, noise volume과 광학·LOD 설정을 저장합니다.

우측 상단 성능 오버레이는 `F1` 창을 숨겨도 유지됩니다. F1의 `Performance`
항목에서 VSync를 켜거나 끌 수 있습니다. CPU Frame은 `Present`와 VSync 대기를 포함하지만
GPU Frame은 Present를 제외하며, View/Light Step 비용 비교에는 `GPU Cloud ms`를 사용합니다.
재현 가능한 측정 절차는 [성능 측정 기준](doc/PERFORMANCE.md)에 정리되어 있습니다.

### 13-4B Weather 기반 세로 형상

Weather Map A는 `Local Thickness Potential`이다. Open World는 밑면 1,500m를 공유하고
Type 0의 층운은 1~2km, Type 1의 적운은 2~6km 범위에서 A로 실제 두께를 고른다.
Type은 두께뿐 아니라 Stratus/Mixed/Cumulus shape profile과 높이별 footprint를
함께 구동한다. Profile은 최종 밀도에 곱하지 않고 Base Noise threshold를 높이별로 바꾼다.
Base Texture3D는 XYZ 12km 등방 타일과 `4/9/17/23` 주파수, octave seed 간격 173을 사용하고,
Detail은 2km와 `2/3/4/5`를 사용한다. 100m View step에서 가장 높은 대역도 Base 5.22,
Detail 4 samples/wavelength를 확보한다. Noise Lab의 `Height profile`에서
`Weather Thickness Potential`, `Local Thickness`, `Local Height`, `Typed Shape Profile`,
`Effective Shape Coverage`, `Base Support Before Density`, `Base Shape Before Detail`을
확인할 수 있다. Similarity는 Legacy shape mode를 유지한다.
Physical Shape의 `Cloud Wind Speed (Bulk)`는 Weather R/G/A와 Base/Detail Texture3D를
같은 XZ 거리로 이동시킨다. Weather/Detail 전용 속도는 Legacy에서만 사용하므로 Open
World의 구름 외곽과 내부 밀도가 서로 미끄러지지 않는다.
Coverage 기본은 `4/11/0.42/-0.02/1.15`, Threshold/Softness `0.56/0.14`이고 F2의
`Thickness-Coverage Link` 기본 `0.20`은 독립 두께 80%와 Coverage 중심 20%를 섞는다.

## 자동 검사

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

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

```powershell
ctest --test-dir build -C Debug -R "Stage13Similarity" -V
```

자세한 구조와 단계는 [아키텍처](doc/ARCHITECTURE.md), [AABB 레이 마칭](doc/RAYMARCHING.md), [로드맵](doc/ROADMAP.md)을 참고하세요.
