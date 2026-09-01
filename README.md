# VolumetricCloud

DirectX 11과 HLSL로 대규모 볼류메트릭 클라우드의 형성, 조명, 대기 합성을 학습하는 Windows 프로젝트입니다. 현재 작업 브랜치는 실험용 선택지를 걷어 내고 **Full-resolution High 한 경로**만 유지합니다.

사용자 최종 화면 승인은 아직 진행 전입니다. 단계 0~8, 13, 9, 12, 14의 승인 이력은 보존하지만 Stage 10 저해상도 복원, Stage 11 Temporal, Cirrus 실험은 최종 런타임에서 폐기했습니다.

## 최종 렌더링 경로

```text
Atmosphere LUT + Balanced512 Deep Shadow
                    ↓
HDR Opaque Scene + Depth
                    ↓
Full-resolution High Cloud Raymarch + Scene/Atmosphere 합성
                    ↓
Tone Mapping → Back Buffer
```

런타임에는 Low/Medium 품질, 저해상도 Cloud RT, 업샘플링 필터, Temporal history, Detail LOD, NTE Rim, Capture/Reference 경로가 없습니다.

## 현재 기능

- 10km × 10km 지면과 20m × 60m × 20m 건물로 된 단일 진단 장면
- meter 단위 Planar cloud layer와 최대 50km View 추적
- 256² Weather Map RGBA와 Base 128³/Detail 32³ Texture3D
- Weather 기반 물리 두께, local base lift, footprint와 200m 상단 여유 검증
- Stratus, Cumulus, Mixed 타입 formation
- Urban Fair Weather, Meadow Broken Clouds, Snow Overcast 장면 콘셉트
- Rayleigh·Mie·오존 대기 LUT, HDR 지면/구름 조명, ACES tone mapping
- Near/Far Balanced512 Deep Optical-Depth Cache
- include 의존성 단위의 동기·원자적 셰이더 핫 리로드
- formation만 저장하는 `captures/noise-lab/custom-cloud.json` schema 1
- 현재 런타임 상태만 내보내는 Noise Lab snapshot schema 39

## High 고정 계약

| 항목 | 값 |
|---|---:|
| 렌더 해상도 | 창과 같은 Full resolution |
| 기본 View step | 100m |
| 최대 View step 수 | 512 |
| View early exit | `T <= 0.01` |
| 빈 공간 탐색 | 빈 표본 3개, epsilon `0.0001`, 2× coarse, 최대 200m |
| 거리 step | 24~50km에서 1×~1.25× |
| Light fallback | deterministic cone 8 taps, 2°, far fraction 0.77, bias 1m |
| Shadow | Deep Cache Balanced512 |

위 값은 UI나 프리셋으로 바뀌지 않습니다. CPU 기준은 `src/HighCloudQuality.h`, GPU 기준은 `shaders/HighCloudQuality.hlsli`에 있습니다.

## 빌드와 실행

요구 환경은 Windows, Visual Studio 2022, Windows SDK, CMake입니다.

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
.\build\Debug\VolumetricCloud.exe
```

Release 빌드:

```powershell
cmake --build build --config Release
.\build\Release\VolumetricCloud.exe
```

## 조작

| 입력 | 역할 |
|---|---|
| 마우스 왼쪽 드래그 | 카메라 회전 |
| `W/A/S/D`, 마우스 휠 | 카메라 이동 |
| `Shift` | 빠른 이동 |
| `F1` | Formation/Custom, cloud movement speed, VSync |
| `F2` | Weather Map RGBA, Cloud Type 소스 상태와 Base/Detail Texture3D 제작 |
| `F3` | 태양, 환경광, 대기, 지면, Tone, Deep Cache |
| `F4` | 세 콘셉트, 핵심 진단, reload report |
| `F5`~`F8` | 고정 검증 카메라 |
| `0`~`9` | 자주 쓰는 구름 진단 view |

기본 실행 상태는 `Urban Fair Weather + High`입니다.

F1~F4는 창의 X 버튼으로 닫은 뒤 같은 기능 키로 다시 열 수 있습니다. 성능 창은
좌측 상단에 독립적으로 표시하며 FPS, cloud time, CPU/GPU frame과 다섯 GPU 구간을
보여 줍니다. F2의 Cloud Type 제작 슬라이더는 F1 Mixed의 `Weather Map G` 소스일 때만 활성화되고,
F1 구름 파라미터 하단의 `Cloud movement speed`는 Weather/Base/Detail을 함께 이동시키는
0~400m/s 실제 world-space 속도이며 0에서 구름이 멈춥니다. F2는 이동 방향만 편집합니다.
offset은 `경과 시간 × 속도 × 방향`이고 절대 시간을 직접 움직이는 slider는 없습니다.
일반 실행의 VSync 기본값은 On입니다. Off에서는 OS가 tearing을 지원하면
`Present(0, DXGI_PRESENT_ALLOW_TEARING)`을 사용하고, 지원하지 않으면 즉시 Present로
fallback합니다. GPU 자체가 병목이면 VSync를 꺼도 FPS가 오르지 않을 수 있습니다.

## 프리셋 소유권

- `ApplySceneConcept`는 formation과 해당 장면의 태양·환경광·대기·지면을 함께 적용합니다.
- `ApplyCloudType`은 formation만 적용합니다.
- `SaveCustomFormation`과 `LoadCustomFormation`은 formation만 저장·복원합니다.
- 내장 타입과 콘셉트는 파일로 덮어쓸 수 없습니다.
- schema 29/30과 구형 `cloud-presets` 파일은 읽거나 지우지 않고 무시합니다.

## 자동 검증

빠른 CPU 회귀:

```powershell
ctest --test-dir build -C Debug -LE gpu --output-on-failure
```

주요 GPU smoke:

```powershell
ctest --test-dir build -C Debug -R "FormationSmoke|AtmosphereSmoke|NoiseLabSmoke|ShaderCacheSmoke" --output-on-failure
ctest --test-dir build -C Debug -R "HotReloadSmoke$" --output-on-failure
ctest --test-dir build -C Debug -R "HotReloadDependencySmoke$" --output-on-failure
ctest --test-dir build -C Release -R "HighCloudSmoke|HighPerformance" --output-on-failure
```

Hot reload smoke는 build 아래의 표식 있는 shader 복사본만 수정합니다. Tone Map 1개 재로드·오류 rollback·cache 복원과 `Noise.hlsli`의 정확한 3-program dependency closure를 하드 게이트로 검사합니다.

최종 구름 모양, shimmer 허용 수준, 산란과 색은 자동 테스트가 승인하지 않습니다. [사용자 렌더 검증 가이드](doc/STAGE15_PRESET_VALIDATION_GUIDE.md)에 따라 Release 1920×1080 화면을 사용자가 직접 판정합니다.

## 문서

- [아키텍처](doc/ARCHITECTURE.md)
- [레이마칭 수식](doc/RAYMARCHING.md)
- [성능 기준](doc/PERFORMANCE.md)
- [폴더 구조](doc/FOLDER_STRUCTURE.md)
- [로드맵](doc/ROADMAP.md)
- [기여 규칙](doc/CONTRIBUTING.md)
