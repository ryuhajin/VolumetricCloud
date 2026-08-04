# VolumetricCloud

DirectX 11 + HLSL로 볼류메트릭 클라우드를 기능별로 검증하며 다시 구축하는 학습 프로젝트입니다.

현재는 **재구축 단계 5**로, CPU 생성 RGBA Weather Map으로 넓은 구름 배치와 종류를 검증합니다.

- 평면과 두 박스로 구성된 불투명 진단 장면
- 샘플 가능한 Scene Depth와 월드 위치 복원
- 화면 UV에서 월드 공간 카메라 레이 생성
- 안전한 slab 방식 Ray-AABB 교차와 카메라 내부 처리
- Scene Depth보다 뒤쪽 안개를 제외하는 구간 제한
- 월드 위치 기반 단일 저주파 3D value noise
- coverage threshold와 density multiplier
- 월드 바람 방향·속도에 따른 시간 이동
- AABB 월드 Y를 0~1로 바꾸는 높이 비율과 독립적인 상·하단 fade
- 실행 기본 X/Z ±8m 넓은 볼륨과 Q의 ±2m 수치 검증 볼륨
- 큰 형태와 독립적인 고주파 단일 Value Noise Detail Erosion
- Base가 비었거나 Detail이 꺼진 영역의 Detail 샘플 생략
- 실제 256² RGBA8 Weather Map Texture2D와 linear-wrap 샘플링
- Weather R coverage, G cloud type, B 0.5~1.5 density modifier
- 층운·기존 혼합형·상향 발달 적운의 수직 프로파일 보간
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

## 단계 5 조작

| 입력 | 동작 |
|---|---|
| 마우스 왼쪽 드래그 | 오빗 회전 |
| 휠 | 줌 |
| `F1` | Noise Lab 표시/숨김 |
| `F2` / `F3` / `F4` | Uniform Legacy / Periodic Perlin / Channel Debug Weather Map |
| `0` | 실제 AABB 안개와 장면 합성 |
| `1` | 월드 레이 방향 RGB |
| `2` | 복원된 월드 거리 |
| `3` | 복원된 월드 위치 밴드 |
| `4` | 화면 UV |
| `5` | AABB 진입 거리 |
| `6` | Scene Depth로 제한한 이탈 거리 |
| `7` | 실제 view step 수 |
| `8` | 최종 투과율 |
| `9` | 대표 샘플의 최종 noise 밀도 |
| `Z` / `X` | 원본 noise / coverage threshold 결과 |
| `C` / `V` | 최종 밀도 / noise UVW RGB |
| `B` / `M` | 대표 샘플의 높이 비율 / 높이 프로파일 |
| `J` / `L` | Detail 전 Base Density / 실제 Detail Noise |
| `P` / `U` | Erosion 양 / Detail Sample 실행 영역 |
| `I` / `O` | Weather Coverage / Cloud Type |
| `Shift+I` / `Shift+O` | Weather Threshold / Typed Height Profile |
| `F5`~`F7` | 외부 고정 검증 카메라 |
| `F8` | AABB 내부 카메라 |
| `Y` / `Q` | 넓은 XZ 볼륨 / 기본 수치 검증 볼륨 |
| `W` / `E` | 얇은 Z / 두꺼운 Z 볼륨 |
| `R` / `T` | fine 0.025m / coarse 0.5m step |
| `N` | 기본 noise 설정 |
| `A` / `S` | sparse / dense coverage |
| `D` / `F` | 큰 / 작은 noise 덩어리 |
| `G` / `H` | 정지 / 빠른 바람 |
| `K` | noise offset 변경 |
| `F9` / `F10` | Detail Off / 기본 Detail |
| `F11` / `F12` | Fine Detail / Strong Erosion |

현재 Base와 Detail은 각각 단일 절차적 Value Noise를 사용하며 고정 산란색만 적용합니다. Weather Map은 CPU가 만드는 실제 RGBA8 Texture2D지만 외부 PNG 로딩은 하지 않습니다. fBm, Worley, 태양광과 early exit는 이후 단계까지 의도적으로 구현하지 않습니다.

Noise Lab은 실제 구름 위에 떠 있는 개발 창입니다. 15개 밀도·Weather 출력을 전환하고 실제 RGBA 맵을 미리 보며 F3 Periodic Perlin 생성기를 실시간 조절할 수 있습니다. `Export 4 PNG + JSON`은 세 단면과 `weather-map.png`, schema 5 생성 설정을 저장합니다.

## 자동 검사

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

`Foundation*`부터 `Stage4*`까지는 이전 단계 회귀를 검사합니다. `Stage5WeatherMath`는 CPU 맵 채널·월드 UV·coverage·cloud type 수학을, `Stage5Smoke`는 Weather 디버그와 F2~F4의 D3D11 경로를 검사합니다. `NoiseLabSmoke`는 15개 출력과 PNG/JSON을 검사합니다.

자세한 구조와 단계는 [아키텍처](doc/ARCHITECTURE.md), [AABB 레이 마칭](doc/RAYMARCHING.md), [로드맵](doc/ROADMAP.md)을 참고하세요.
