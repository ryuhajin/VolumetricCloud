# VolumetricCloud

DirectX 11 + HLSL로 볼류메트릭 클라우드를 기능별로 검증하며 다시 구축하는 학습 프로젝트입니다.

현재는 **재구축 단계 7**로, 태양과 카메라 각도에 따른 Dual-lobe HG 방향성 산란을 검증합니다.

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
- 별도 64바이트 LightCB와 표본→태양·카메라→표본 방향 규칙
- Base Density만 적분하는 태양 Light Ray와 Beer-Lambert 태양 투과율
- 태양색·세기·산란계수를 적용한 직접 단일 산란
- Phase Off에서 단계 6을 보존하는 Dual-lobe Henyey-Greenstein Phase Function
- 독립적인 전방·후방 g, 혼합 비율과 Phase 강도
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

## 단계 7 조작

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
| `Shift+J` / `Shift+L` | 태양 투과율 / 태양 방향 광학 깊이 |
| `Shift+P` / `Shift+U` | 전체 Light Sample 비용 / 직접 단일 산란 |
| `Shift+B` / `Shift+M` | Phase cosTheta / Forward HG lobe |
| `Shift+C` / `Shift+V` | Backward HG lobe / 최종 Dual Phase Factor |
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

현재 Base와 Detail은 각각 단일 절차적 Value Noise를 사용합니다. Weather Map은 CPU가 만드는 실제 RGBA8 Texture2D이며 외부 PNG 로딩은 하지 않습니다. Light Ray는 Weather·Type·Height가 적용된 Base만 읽고 Detail은 생략합니다. Phase는 직접 산란량만 바꾸며 환경광·다중 산란과 early exit는 이후 단계까지 의도적으로 구현하지 않습니다.

Noise Lab은 실제 구름 위에 떠 있는 개발 창입니다. 15개 밀도·Weather 출력과 실제 RGBA 맵, 태양 설정뿐 아니라 Phase Off/Balanced/Silver Lining/Backscatter Check 프리셋과 HG 곡선을 제공합니다. 기본은 Phase Off이며 `Export 4 PNG + JSON`은 schema 7 방향 정의와 Phase 설정을 저장합니다.

우측 상단 성능 오버레이는 `F1`로 Noise Lab을 숨겨도 유지됩니다. Noise Lab의 `Performance`
항목에서 VSync를 켜거나 끌 수 있습니다. CPU Frame은 `Present`와 VSync 대기를 포함하지만
GPU Frame은 Present를 제외하며, View/Light Step 비용 비교에는 `GPU Cloud ms`를 사용합니다.
재현 가능한 측정 절차는 [성능 측정 기준](doc/PERFORMANCE.md)에 정리되어 있습니다.

## 자동 검사

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

`Foundation*`부터 `Stage6*`까지는 이전 단계 회귀를 검사합니다. `Stage7PhaseMath`는 방향 부호·HG·Dual-lobe와 안정성을, `Stage7Smoke`는 64바이트 LightCB, Phase 디버그·프리셋과 schema 7을 검사합니다. `FrameProfilerMath`와 `PerformanceOverlaySmoke`는 비동기 GPU 계측 회귀를 유지합니다.

자세한 구조와 단계는 [아키텍처](doc/ARCHITECTURE.md), [AABB 레이 마칭](doc/RAYMARCHING.md), [로드맵](doc/ROADMAP.md)을 참고하세요.
