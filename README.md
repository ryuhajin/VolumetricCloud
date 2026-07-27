# VolumetricCloud

DirectX 11 + HLSL로 광역 weather map, periodic 3D noise, 가변 레이마칭과 근사 다중 산란을 구현하는 볼류메트릭 클라우드 학습 프로젝트입니다.

## 현재 기능

- 평면 구름층 교차와 최대 거리 제한, 50m 물리 상한·경계 정제를 갖는 최대 2000회 가변 ray marching
- 512² RGBA weather map: coverage, cloud type, base-height, thickness
- 심리스 periodic Value/Worley/FBM 및 Perlin–Worley 밀도장
- 128³ base/detail RGBA8 Texture3D와 독립 월드 공간 XYZ 크기
- 128³ base RGBA 형태 밴드와 128³ detail RGBA 침식 옥타브
- 근거리/원거리 태양 light march, dual-lobe HG, powder, silver lining과 3-octave 다중 산란 근사
- F1 Dear ImGui 편집기와 F2 상시 HUD: 성능·태양·구름층·마칭·캐시 상태
- 분석적 하늘, 수평선 haze, 태양 glow와 CPU/DX11 GPU timestamp 계측
- 검증된 `.cso`, 3D volume, 2D weather map 영구 캐시 v6
- HLSL 핫 리로드 실패 시 마지막 정상 리소스 유지

## 빌드

```powershell
git clone --recurse-submodules <this-repo-url>
cd VolumetricCloud
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
.\build\Debug\VolumetricCloud.exe
```

기존 clone은 `git submodule update --init --recursive`로 Dear ImGui를 받습니다.

## 조작과 캐시

| 입력 | 동작 |
|---|---|
| 마우스 왼쪽 드래그 | 오빗 회전 |
| 휠 | 줌 |
| F1 | Cloud Debug UI 토글 |
| F2 | 오른쪽 위 성능·상태 HUD 토글 |

시작 시 사용자 캐시 → 배포 기본 캐시 → 런타임 생성 순으로 선택합니다. HLSL을 수정하면 첫 정상 화면 이후 런타임 컴파일과 임시 volume 생성을 수행합니다. 변경은 `Save Noise Cache`를 눌러야 `%LOCALAPPDATA%\VolumetricCloud\cache`에 불변 세대로 저장되고, 검증을 통과한 뒤 작은 active 포인터가 교체됩니다. `Revert to Saved`는 마지막 정상 세트를 복원합니다. 별도 로딩 화면은 없습니다.

256×256 Noise Inspector 이미지는 진단용 단면이며 메인 화면 해상도를 제한하지 않습니다.

## 자동 검사

```powershell
ctest --test-dir build -C Release --output-on-failure
```

`VolumetricCloud.CacheSmoke`는 GPU 성능과 무관하게 초기화까지만 수행하고 정상 캐시 시작에서 컴파일과 noise dispatch가 0회인지 검사합니다. `VolumetricCloud.CodeTests`는 평면 교차, GPU periodic seam, 3D/weather RGBA 분산, 밀도 포화, 캐시 저장/재로드와 산란 수치를 검사합니다. 화면 품질 비교는 포함하지 않습니다.

View step 비용은 `.\build\Release\VolumetricCloud.exe --benchmark-view-steps`로 측정합니다. 고정된 1280×720 장면에서 3.8/16 km와 128/160/192/256 step을 각각 120회 측정하고 `%LOCALAPPDATA%\VolumetricCloud\benchmarks\view-step-quality.csv`에 GPU/CPU mean·median·p95를 기록합니다. 에이전트는 캡처를 만들지 않으며 최종 화면 품질은 사용자가 평가합니다.

자세한 구조와 수식은 [아키텍처](doc/ARCHITECTURE.md), [레이마칭](doc/RAYMARCHING.md), [로드맵](doc/ROADMAP.md)을 참고하세요. 브랜치별 문제 분석과 설계 이유는 [`doc/changes/`](doc/changes/)에 기록합니다.
