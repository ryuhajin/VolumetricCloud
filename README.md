# VolumetricCloud

DirectX 11 + HLSL로 볼류메트릭 클라우드를 기능별로 검증하며 다시 구축하는 학습 프로젝트입니다.

현재는 **재구축 단계 3**으로, 승인된 3D noise 밀도장 위에 높이 프로파일을 검증합니다.

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

## 단계 3 조작

| 입력 | 동작 |
|---|---|
| 마우스 왼쪽 드래그 | 오빗 회전 |
| 휠 | 줌 |
| `F1` | Noise Lab 표시/숨김 |
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

현재는 절차적 value noise 하나, 월드 Y 높이 프로파일과 고정 산란색만 사용합니다. detail noise, weather map, 태양광과 early exit는 이후 단계까지 의도적으로 구현하지 않습니다.

Noise Lab은 실제 구름 위에 떠 있는 개발 창입니다. 세 단면의 교차점을 클릭하거나 `Slice XYZ`를 움직여 3D 공간을 탐색하고, Raw/Threshold/Final/Height 출력을 전환할 수 있습니다. Bottom/Top Fade 슬라이더와 64표본 곡선은 같은 높이 수식을 보여 줍니다. `Export 3 PNG + JSON`은 관찰 자료만 저장하며 실제 구름은 PNG가 아니라 같은 절차적 3D 함수를 계속 사용합니다.

## 자동 검사

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

`Foundation*`, `Stage1*`, `Stage2*`는 이전 단계 회귀를 검사합니다. `Stage3HeightMath`는 높이 비율·fade·퇴화 AABB를, `Stage3Smoke`는 B/M 모드와 여러 fade 조합의 D3D11 경로를 검사합니다. `NoiseLabSmoke`는 다섯 출력과 PNG/JSON을, `ShaderHotReloadSmoke`는 공용 include 변경·컴파일 실패·복구 시 구름과 단면의 원자적 교체를 검사합니다.

자세한 구조와 단계는 [아키텍처](doc/ARCHITECTURE.md), [AABB 레이 마칭](doc/RAYMARCHING.md), [로드맵](doc/ROADMAP.md)을 참고하세요.
