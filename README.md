# VolumetricCloud

DirectX 11 + HLSL로 periodic 3D noise, 가변 레이마칭과 근사 다중 산란을 구현하는 볼류메트릭 클라우드 학습 프로젝트입니다.

## 현재 기능

- AABB 내부 48~128-step 가변 ray marching, 고정 jitter와 Beer–Lambert 조기 종료
- 심리스 periodic Value/Worley/FBM 및 Perlin–Worley 밀도장
- 128³ base, 64³ detail RGBA8 Texture3D
- 128³ base RGBA 형태 밴드와 64³ detail RGBA 침식 옥타브
- 태양 light march, dual-lobe HG, powder, silver lining과 3-octave 다중 산란 근사
- F1 Dear ImGui 패널: 노이즈·조명 성분, seam/cache difference, 파라미터와 Showcase 프리셋
- 검증된 `.cso`와 3D volume 영구 캐시로 빠른 시작
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

시작 시 사용자 캐시 → 배포 기본 캐시 → 런타임 생성 순으로 선택합니다. HLSL을 수정하면 첫 정상 화면 이후 런타임 컴파일과 임시 volume 생성을 수행합니다. 변경은 `Save Noise Cache`를 눌러야 `%LOCALAPPDATA%\VolumetricCloud\cache`에 영구 저장됩니다. `Revert to Saved`는 마지막 정상 세트를 복원합니다. 별도 로딩 화면은 없습니다.

256×256 Noise Inspector 이미지는 진단용 단면이며 메인 화면 해상도를 제한하지 않습니다.

## 자동 검사

```powershell
ctest --test-dir build -C Release --output-on-failure
```

`VolumetricCloud.CacheSmoke`는 정상 캐시 시작에서 컴파일과 noise dispatch가 0회인지 검사합니다. `VolumetricCloud.CodeTests`는 GPU periodic seam, RGBA 채널 분산과 밀도 포화, 캐시 저장/재로드, Beer–Lambert/dual-lobe phase/다중 산란 수치를 검사합니다. 화면 품질 비교는 포함하지 않습니다.

자세한 구조와 수식은 [아키텍처](doc/ARCHITECTURE.md), [레이마칭](doc/RAYMARCHING.md), [로드맵](doc/ROADMAP.md)을 참고하세요. 브랜치별 문제 분석과 설계 이유는 [`doc/changes/`](doc/changes/)에 기록합니다.
