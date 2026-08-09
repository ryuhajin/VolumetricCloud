# VolumetricCloud

DirectX 11 + HLSL로 볼류메트릭 클라우드를 기능별로 검증하며 다시 구축하는 학습 프로젝트입니다.

현재는 **재구축 단계 13**입니다. 단계 9 최적화는 소규모 AABB 성능 기준 실패로
보류하고, UE5 Open World 기본 맵과 비슷한 지상형 인디 시뮬레이션을 위해 먼저
대규모 평면 구름층을 확정했습니다.

```text
0~8 완료 → 13 선행 → 9 재개 → 10 → 11 → 12 → 14 → 15
```

## 현재 구현

- XZ 측면 경계가 없는 Y축 평면 구름층
- 구름 바닥 1.5km, 상단 4.5km, View 최대 50km
- 40~50km 원거리 fade와 Light 최대 20km
- meter 내부 단위와 km UI 표시
- 32km periodic Weather Map과 월드 고정 Base/Detail Noise
- View 256×100m, Light 32×250m 기준 표본
- 구름 아래·내부·위 고정 카메라
- Scene Depth 폐색, Beer-Lambert 투과율, 태양 단일 산란
- Dual-lobe HG Phase, 환경광, AO와 광학 깊이 재사용 다중 산란
- Noise Lab 32km 카메라 중심 단면과 schema 13 PNG/JSON export
- 단계 9 Legacy/Optimized 경로와 비동기 GPU 계측 보존

## 빌드와 실행

```powershell
git submodule update --init --recursive
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
.\build\Debug\VolumetricCloud.exe
```

## 단계 13 조작

| 입력 | 동작 |
|---|---|
| `F1` | Noise Lab 표시/숨김 |
| `F2` / `F3` / `F4` | Uniform / Periodic Perlin / Channel Debug Weather |
| `F5` / `F6` | 지상 상향 / 지상 수평선 카메라 |
| `F7` / `F8` | 구름층 내부 / 구름층 상공 카메라 |
| `Q` / `Y` | 기준 50km / 장거리 64km 구름층 |
| `W` / `E` | 1.5km 얇은층 / 6km 두꺼운층 |
| `R` / `T` | fine 50m / coarse 200m View step |
| `0` | 최종 합성 |
| `5` / `6` | 구름층 진입 / 제한 이탈 거리 |
| `7` / `8` / `9` | View step / 투과율 / 대표 밀도 |
| `N`, `A`, `S` | 기본 / sparse / dense coverage |
| `D`, `F`, `G`, `H`, `K` | Base 크기·바람·offset 프리셋 |
| `F9`~`F12` | Detail Off / 기본 / Fine / Strong Erosion |
| `Ctrl+Shift+J/L/P/U/B` | 보류된 단계 9 실행량·skip·Early Exit 진단 |

## 자동 검사

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

`Stage13CloudLayerMath`는 아래·내부·위·수평 레이, Scene Depth·최대 거리 제한,
거리 fade와 32km Weather wrap을 검사합니다. `Stage13Smoke`는 네 카메라와
schema 13 export를 실제 D3D11 경로에서 확인합니다.

단계 13 사용자 승인 후 단계 9 벤치마크를 대규모 장면으로 다시 실행합니다. 자세한
구조와 수식은 [아키텍처](doc/ARCHITECTURE.md), [레이 마칭](doc/RAYMARCHING.md),
[로드맵](doc/ROADMAP.md)을 참고하세요.
