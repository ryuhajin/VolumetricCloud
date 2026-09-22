# 기여 / 작업 규칙

## 일반 배포와 로컬 검증 분리

[빌드 안내](BUILDING.md)가 현재 실행 계약이다. 일반 빌드는 `VCLOUD_BUILD_TESTS=OFF`이며
tests 없이 VolumetricCloud를 만든다. 로컬 검증은 ON + 별도 VolumetricCloudTestRunner를 사용한다.
아래 과거 CLI 명령의 실행 파일은 검사 앱 경로로 바꿔 사용한다. 일반 앱에는 테스트 CLI가 없다.
원격은 일반 코드·필수 빌드 설정·프리셋·doc만 반영하며 로컬 테스트/도구/notes/captures는 전송하지 않는다.

## 현재 산출물 관리 계약

[데이터 관리 규칙](DATA_MANAGEMENT.md)을 먼저 적용한다. 완료 실험은 반영·재검증·노트 기록 후
캡처와 덤프를 삭제하며, 진행 중 비교와 실패 자료는 보호한다. 아래 역사적 캡처 경로는 존재를 보장하지 않는다.
기본 슬라이더/결정성 테스트는 성공 후 최신 수치 로그만 남긴다. 상세 조건·정리 도구 사용법은 위 문서에 있다.

## 2026-09-22 대기 합성 진단

`VolumetricCloud.exe --cloud-aerial-composition-test`는 A/B/C/D와 직접100→50m 수렴을 검사한 뒤
조건 통과 시만 거리배율1/1.5/2를 촬영한다. CTest `VolumetricCloud.CloudAerialComposition`은
`VCLOUD_ENABLE_DIAGNOSTIC_TESTS=ON`만 등록한다. GPU직렬, timeout900초. UI/CB/schema 불변.
결과는 작업 디렉터리의 `captures/cloud-aerial-composition/<새ID>`다.
기준/한계는 [합성 진단 기록](changes/stage15-cloud-aerial-composition.md)을 따른다.
렌더 성공 후 `python tests/AnalyzeAerialComposition.py <결과 폴더>`로 픽셀 CSV의 거리별 대비,
공기T/L 재합성과 파일을 검산한다. NumPy/Pillow가 필요하며 `review.html`에서 같은 시점의
1/1.5/2배를 전환한다. 기존 원본 캡처를 바꾸거나 새 GPU 촬영을 하지 않는다.

## 2026-09-21 테스트 산출물 정책

일반 CMake 구성은 `VCLOUD_ENABLE_DIAGNOSTIC_TESTS=OFF`이며 대량 이미지를 만드는
`CloudNearFarDiagnostics`와 `CloudPathLength`를 CTest에 등록하지 않는다. 두 진단이 필요한
경우 별도 빌드 디렉터리를 `-DVCLOUD_ENABLE_DIAGNOSTIC_TESTS=ON`으로 구성하거나 해당 CLI를
명시적으로 실행한다. 진단 출력은 일회성 로컬 자료이며 런타임 입력이나 프리셋이 아니다.

2026-09-21 이전 방향광·림·대기·형상 후보 비교 명령과 분석 스크립트는 완료된 실험 기록이다.
아래 역사적 절차에 적힌 실행 파일과 스크립트는 현재 소스에서 제거되었으며 다시 실행하는
현재 검증 계약으로 사용하지 않는다. 기본 CPU 회귀, GPU smoke, High 성능, 결정성,
프리셋·핫리로드 검증은 계속 유지한다.

이 프로젝트는 학습용이며, **사람과 AI 에이전트가 함께** 작업합니다.
일관성을 위해 아래 규칙을 따릅니다.

## 1. 문서 수정 규칙

문서는 코드와 항상 동기화되어야 합니다.

- **코드 동작을 바꾸면** 관련 `doc/*.md`를 **같은 변경(커밋/PR)에서** 갱신합니다.
- **새 파일/폴더를 추가하면** [FOLDER_STRUCTURE.md](FOLDER_STRUCTURE.md)에 한 줄 추가합니다.
- **새 모듈·렌더 단계·상수버퍼 필드를 추가/변경하면** [ARCHITECTURE.md](ARCHITECTURE.md)를 갱신합니다.
  - 특히 상수버퍼(`CameraCB` ↔ `cbCamera`)는 C++·HLSL·문서 **세 곳을 동시에** 맞춥니다.
- **로드맵 단계를 완료하면** [ROADMAP.md](ROADMAP.md)의 체크박스를 채웁니다.
- 알고리즘/수식을 바꾸면 [RAYMARCHING.md](RAYMARCHING.md)를 갱신합니다.
- 문서 언어는 **한국어**, 코드 주석도 한국어를 기본으로 합니다.

> AI 에이전트는 작업 시작 전 [AGENTS.md](../AGENTS.md)를 먼저 읽고, 끝낼 때 위 동기화를 확인하세요.

## 2. 브랜치 규칙

| 브랜치 | 용도 |
|--------|------|
| `main` | GitHub 기본 브랜치이자 승인된 안정 상태. 직접 commit/push 금지, PR merge commit만 허용 |
| `feature/stage<번호>-<설명>` | 로드맵 단계 전용 작업 (예: `feature/stage12-shadow`) |
| `feature/<설명>` | 단계와 무관한 새 기능 |
| `fix/<설명>` | 버그 수정 (예: `fix/resize-crash`) |
| `fix/rollback-stage<번호>` | 승인 태그에서 시작하는 복구 작업 |
| `doc/<설명>` | 문서만 수정 (예: `doc/raymarching-math`) |

- 로드맵의 각 단계는 최신 `origin/main`에서 만든 전용 `feature/stage<번호>-<설명>` 브랜치에서만
  진행합니다. Stage 12/14/15의 이름은 각각 `feature/stage12-shadow`,
  `feature/stage14-atmosphere-integration`, `feature/stage15-final-quality`입니다.
- 단계 코드·테스트·공식 문서는 같은 단계 브랜치에 둡니다. 다른 단계 구현을 섞지 않습니다.
- 작업 완료 뒤 브랜치를 최신 `origin/main` 기준으로 갱신하고 Debug/Release 빌드, 전체 CTest,
  해당 GPU smoke와 사용자 렌더 승인을 다시 확인합니다.
- GitHub PR은 **merge commit** 방식으로만 병합합니다. squash/rebase merge와 main 직접 push는
  사용하지 않습니다.
- 병합된 `origin/main` commit에 annotated `stage<번호>-approved` 태그를 만들고 원격에 push합니다.
  태그의 dereference SHA와 `origin/main` SHA가 같은지 확인한 뒤 단계 브랜치를 로컬·원격에서
  삭제합니다. 기존 `stage11`은 이름을 바꾸지 않는 역사적 예외입니다.

### 2.1 `main` 보호 계약

GitHub의 기본 브랜치는 `main`이며 다음 branch protection을 유지합니다.

- PR 필수, 필수 승인 리뷰 수 0, conversation resolution 필수
- 관리자에게도 규칙 적용
- force push와 branch deletion 금지
- merge commit 허용, squash/rebase merge와 linear history 요구는 비활성
- CI 도입 전에는 required status check를 지정하지 않고 로컬 검증 결과를 PR에 기록

### 2.2 승인 태그와 롤백

- 승인 태그는 이동하거나 덮어쓰지 않습니다. 같은 이름이 이미 있으면 작업을 중단합니다.
- 과거 버전 확인은 `git switch --detach <승인 태그>`로 수행합니다.
- 실제 롤백은 이전 승인 태그에서 `fix/rollback-stage<번호>`를 만들고 새 PR로 병합합니다.
- `main`에 `reset --hard` 또는 force push를 사용해 이력을 되감지 않습니다.

## 3. 커밋 규칙 (Conventional Commits)

`<type>: <요약>` 형식을 사용합니다.

| type | 사용 시점 |
|------|-----------|
| `feat` | 새 기능 |
| `fix` | 버그 수정 |
| `docs` | 문서만 변경 |
| `refactor` | 동작 변화 없는 구조 개선 |
| `build` | 빌드/CMake/의존성 변경 |
| `perf` | 성능 개선 |

예시:
```
feat: add worley noise to cloud density
fix: prevent crash on window minimize
docs: explain beer-lambert integration
```

- **셰이더(HLSL)를 바꾼 커밋**은 본문에 시각적 결과를 한 줄 요약합니다.
  (예: `구 가장자리가 더 부드러워짐`)
- 한 커밋은 한 가지 일만 담습니다.

## 4. 빌드 확인

### 06 얇은 경계 진단 (일반 룩 변경 없음)

캐시를 유지한 Detail 후보 비교는 `VCLOUD_RIM_CACHE_COMPARE=1`을 추가한다(`VCLOUD_RIM_DENSITY_COMPARE`와 동시 사용 불가). 같은 High View에서 Base/Detail 캐시를 각각 같은 밀도의12.5m 직접 참조와 비교한다. Release는 두 고도에서60예열120표본의 Shadow/Cloud/Frame p95도 기록한다. 이는 테스트 define으로만 동작하며 일반 실행을 직접 적분이나 Detail 캐시로 전환하지 않는다. 낮은 태양7조건의 finite/D3D 검사도 수행하되 시각적 안정성 승인을 대신하지 않는다.

밀도 표현/광학 두께 후속 비교는 `VCLOUD_RIM_DENSITY_COMPARE=1`을 추가한다. 태양 직접12.5m와 View12.5m를 고정하고 Shadow Base/Detail × 소멸계수1/2/4/8을 각각 검사한다. 배수는 테스트 b1에만 적용하고 프리셋·일반 캐시는 변경하지 않는다. 분석은 NumPy가 있는 Python으로 `tests/AnalyzeCloudOpticalDepth.py <출력 경로>`를 실행한다. 동일 배수의 B/D View T 일치, tau 배수 관계와 Beer 관계를 검사한다. 기본값 채택과 구분한다.06 이후 공식 기록은 [림 개선 문서](changes/stage15-cloud-rim-lighting.md)를 따른다.

`VCLOUD_RIM_BOUNDARY_DIR`를 존재하지 않는 로컬 출력 경로로 지정하고 Release `VolumetricCloud.exe --rim-lighting-test`를 실행하면 기존 RimLighting 대신 경계 진단을 수행한다. Urban/F5/71초/방위-108.5/고도18·5, 60프레임 예열 뒤 캐시와 CB를 고정한다. 동일 모델 N0/N1(태양 직접)/N2(View 고정밀)/N3(둘 다), 선형 채널 덤프와 균일 구 수송 검사를 실행한다. 일반 설정·프리셋·CB 크기는 바꾸지 않는다. 출력 contract.json의 ROI 밖 참조 픽셀은 분석하지 않는다. 수렴과 화면 개선은 finite/D3D 통과와 별도로 판정한다. 환경변수를 제거하면 기존 림 회귀를 실행한다.

### Visual Studio의 셰이더 탐색

`CMakeLists.txt`의 `VCLOUD_SHADER_SOURCES`는 원본 `.hlsl`·`.hlsli`를 앱 프로젝트의
`Shaders` 그룹에 표시하는 명시적 목록이다. `HEADER_FILE_ONLY`로 빌드 시 컴파일을
제외하며, 실행 중 `D3DCompileFromFile`·캐시·핫 리로드와 빌드 후 복사는 그대로 유지한다.
셰이더 파일을 추가/삭제하면 이 목록도 갱신하고 CMake를 다시 실행한다. 생성된
`.vcxproj`·`.filters`는 직접 편집하지 않는다. 탐색기 등록만으로 일반 CPU 디버거가
HLSL 중단점에 멈추는 것은 아니며, 픽셀 계산은 그래픽 캡처의 HLSL 디버거로 확인한다.

### 주력 Release와 선택형 Strict Validation

기본 `VCLOUD_STRICT_VALIDATION=OFF`는 실제 사용자 실행 파일이며 주요 렌더 TC와
성능 측정의 주력이다. 일반 패스는 `/Gis`를 사용하지 않는다. 단, Weather는 이번 분리 전부터
존재하던 CPU/GPU parity 계약을 위해 `/Gis`를 유지한다. CameraCB 캐시 무효화 수정도 양쪽에 유지한다.
옵션은 C++ 빌드에 고정되며 실행 중 HLSL을 컴파일할 때 적용된다. UI 품질 옵션이 아니다.

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64 -DVCLOUD_STRICT_VALIDATION=OFF
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --build build --config Debug
```

Strict Validation은 필요할 때 별도 디렉터리에 Release 최적화를 기반으로 빌드한다.
모든 활성 셰이더에 `/Gis`를 적용하며 상태관리/알고리즘 디버깅과 결정적 해시 검증의 보조 도구다.
Debug/Release 설정과 독립적인 CMake 옵션이므로 ON 디렉터리는 Debug도 엄격 정책을 사용한다.

```powershell
cmake -B build/strict-validation -G "Visual Studio 17 2022" -A x64 -DVCLOUD_STRICT_VALIDATION=ON
cmake --build build/strict-validation --config Release
ctest --test-dir build/strict-validation -C Release -R "Determinism|RendererState" --output-on-failure
```

- `DeterminismSmoke`는 ON일 때만 CTest에 등록한다. `RendererStateSmoke`와 주요 렌더 TC는 양쪽에 등록한다.
- 기본 OFF에서도 수동 해시 캡처는 진단용으로 가능하지만 bit-exact 통과를 요구하거나 보장하지 않는다.
- Strict의 cold/warm 해시 통과를 배포 Release 검증으로 대체하지 않는다. GPU 테스트는 직렬 실행한다.
- 성능 gate는 OFF Release에 적용한다. Strict의 성능 결과는 참고 측정이며 배포 gate가 아니다.
- Release 이미지 회귀는 원본 HDR/화면의 tolerance 기반 비교를 사용한다. 승인된 baseline,
  해상도/시간/카메라/환경, 오차 지표·임계값을 테스트 전에 명시하고 실패 후 임의로 완화하지 않는다.
  누락·캡처 실패·NaN/Inf는 tolerance와 무관하게 실패다. 화면 외관 최종 승인은 사용자 몫이다.
- 현재 전용 최종 HDR/Tone baseline 이미지 회귀 TC는 미구현이다. 기존 Atmosphere 수치 오차 및
  Weather parity TC가 이를 대체한다고 기록하지 않는다. 이번 변경은 빌드/등록/규약 분리이며
  이미지 baseline과 허용 오차 확정·비교 TC 추가는 후속 작업으로 남긴다.
- Strict와 Release의 출력 해시를 서로 비교하지 않는다. `/Gis`는 서로 다른 GPU/드라이버까지
  동일 출력을 보장하지 않으므로 환경별로 검증한다. 테스트 결과에는 옵션 값을 함께 기록한다.

PR 전 아래가 통과하는지 확인합니다.

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

빌드 에러 0, 셰이더 런타임 컴파일 성공(실행 시 오류 MessageBox 없음)이어야 합니다.

### Stage 15 방향광 baseline (명시적 실행 전용)

`tests/DirectionalLightingBaseline.cpp`는 일반 UI와 분리된 고정 조건 실행기다.
최초 촬영은 OFF Release에서만 허용하고, 먼저 보존한 source-manifest의 셰이더와
현재 소스가 일치해야 한다. 기존 clean 디렉터리는 덮어쓰지 않는다.

```powershell
# 최초 보존 시 단 한 번 실행. 현재 00-baseline은 이미 촬영 완료되었다.
powershell -NoProfile -ExecutionPolicy Bypass -File tests/RunDirectionalLightingBaseline.ps1
# 이후에는 아래로 원본을 메모리에서 비교한다. 새 PNG/HDR 저장 없음.
powershell -NoProfile -ExecutionPolicy Bypass -File tests/RunDirectionalLightingBaseline.ps1 -VerifyOnly
```

기본 경로는 `captures/stage15-directional-lighting/00-baseline`이다.
`-Root`, `-Exe`로 보존한 root/실행 파일을 지정할 수 있다. 실행기는 snapshot을
불러오는 대신 코드로 동일 설정과 프레임 순서를 다시 적용한다.
`frame.rgba16f`는 1920×1080 RGBA half-float little-endian, 행 15360바이트다.
PNG는 Tone/UI 이전이 아닌 **Tone 이후·UI/Present 이전** 출력이다.
고정 시간 71s·바람 0·태양 재생 Off, F5, Urban 및 Urban 조명 세 Type에 각 6개 태양 조건을 쓴다.
재현 검사 허용 오차는 정규화 HDR MAE≤1e-5/max≤1e-3, LDR RGB MAE≤1/255/max≤2/255다.
이는 변경 전 동일 입력 재현 전용이며, 최종 HDR/Tone 전용 전체 회귀 TC를 대체하지 않는다.
00 승인 뒤의 01/02 단계는 추가 촬영을 필수 작업으로 넣지 않는다.

### 방향광 01 진단 (이미지 저장 없음)

```powershell
ctest --test-dir build -C Release -R 'DirectionalLightingDiagnostics$' --output-on-failure -j 1
ctest --test-dir build -C Debug -R 'DirectionalLightingDiagnostics$|NoiseLabSmoke$' --output-on-failure -j 1
```

이 테스트는 1920×1080 Urban/F5와 Urban 조명의 세 Type, 고도 18/45/70°를 검사한다.
각 case에서 정상 Render로 Weather/Noise/Shadow/LUT/Scene depth를 준비한 뒤 고정하여
default/간접광 Off/phase 중립/둘 모두의 Cloud/Tone만 다시 실행한다.
밀도·View T·Visible Sun T의 동일 입력 비교는 00의 HDR/LDR tolerance를 사용한다.
albedo=0에서도 Visible Sun T를 유지하고 빈 밀도에서는 0으로 반환해야 한다.
추가로 실제 PS b3/b8/b9 방향을 GPU probe로 읽어 CPU/각도 기준 오차≤1e-5를 검사한다.
probe는 소스 `tests/DirectionalLightingProbe.hlsl`이 필요하며 일반 배포 프리셋에는 포함하지 않는다.
RGBA16F 진단의 `L/(1+L)`을 역변환한 근사 선형 휘도와 불투명도>0.1 픽셀의
`(p90-p10)/(p90+p10)` 대비를 기록한다. 이 통계는 Tone/대기 합성된 화면의 미학적 합격 기준이 아니다.
정상 전체 Render와 Composite 회귀는 baseline `-VerifyOnly` 및 기존 smoke/성능에서 따로 검사한다.

방향광 02는 `ctest --test-dir build -C Release -R "DensityShaping|Stage15PresetMath|CloudFormationPresetStore" --output-on-failure -j 1`로 검사한다.
`--density-shaping-test`는 Urban/세 Type × 고도 18/45/70 × 강도 0/.35/.70을 1920×1080에서 직렬 실행한다.
스크린샷 없이 HDR finite/범위, GPU 곡선/CPU 오차 1e-5, b7 offset40/48B, 강도0 복원과 태양 T 반응을 검사한다.
기존 baseline 원본 검증은 `tests/RunDirectionalLightingBaseline.ps1 -VerifyOnly`이며 원본을 덮어쓰지 않는다.

### 방향광 02 후보 성능

기존 12 case, 60 frame 예열/120 GPU raw 표본, 1920×1080 OFF Release/VSync·UI·캡처 Off를 유지한다.
`--high-performance-test --density-shaping-035` 또는 `--density-shaping-070`은 테스트 중에만
각 Concept의 Formation 밀도 강도를 덮어쓴다. 일반 실행에는 적용되지 않으며 Custom을 저장하지 않는다.
인자가 없으면 강도 0을 측정한다. 각 case Cloud p95≤10ms, Frame p95≤16.67ms를 요구한다.

### 03 Base 굴곡 검사

`ctest --test-dir build -C Release -R "BaseOctaves|Stage13NoiseVolumeMath|Stage15PresetMath" --output-on-failure -j 1`.
Debug에서도 BaseOctaves/NoiseLabSmoke를 실행한다. 03 진단은 이미지를 파일로 저장하지 않는다.
12 case 성능은 `--high-performance-test --base-octaves-125` 및 `--base-octaves-150`.
밀도 override 인자가 없으면 승인된 Concept 기본값을 유지한다(Urban .70, Meadow/Snow 0).

### 04 — 폐기된 Near Detail 그림자

06-A에서 사용자 요청으로 UI·설정·거리 가중치·Detail 조회·전용 테스트를 제거했다. Near/Far/cone는 모두 Base 그림자 밀도를 사용한다. 실험 원인·결과는 [06 림 변경 기록](changes/stage15-cloud-rim-lighting.md)에 보존한다. ShadowCB 160B의 offset152/156은 uint padding이며 0으로 초기화한다. 미사용 surfaceShadowEnabled도 offset8의 uint padding으로 바꿨다. 실제 지면 그림자 strength/floor는 유지한다.

## 05 직접광·간접광·림 비교 — 2026-09-15

F3에 기존 필드 Shadow exponent(b3 offset60), Edge optical depth scale(b3 offset56),
Multiple attenuation(b4 offset16)을 노출했다. 기존 CPU/HLSL sanitize 범위는 각각 .5~4, .25~8,0~1이다.
06 현재 ABI는 LightCB80B/EnvironmentCB48B이며 ShadowCB160B다. 기존 Phase intensity/Sky fill/Ground fill을 함께 비교한다.
Reset approved Urban lighting은 위 여섯 명암 값과 Rim intensity/depth를 Urban 기준으로 복원한다. Type과 태양각·형상·노출·화이트밸런스는 유지한다.
단, 기존 F3 편집 처리와 동일하게 UI preset 표시는 Custom이 된다. Concept 재선택은 전체 장면 조명을 복원한다.
snapshot42 lighting 객체는 shadowExponent/edgeOpticalDepthScale/phaseIntensity/phaseCap/multipleAttenuation을 추가 기록한다.
Custom Formation schema3은 조명을 저장하지 않는다.

일반 Phase 상한은2.5다. `VCLOUD_TEST_PHASE_CAP`은05 검사에서만4/8을 컴파일하는 상수 override이며
UI 옵션이나 승인된 런타임 기본값이 아니다. 기존 shader manifest/include closure는 해당 소스 변경을 감지한다.
01 진단/밀도 불변 회귀를 유지하고05는 고정 물리 입력에서 성분/최종HDR 평균·대비를 보고한다.
`ctest --test-dir build -C Release -R LightingTuning --output-on-failure`로 실행한다.
4 Formation×3고도×3방위각×11독립후보=396조건. 방위각 -108.5/-18.5/71.5도, 시간71초, F5, Base 그림자/Base1.50/밀도.70.
가시 구름은 View 불투명도>.1인 픽셀을4픽셀 간격으로 집계한다. Phase diagnostic의2.5 색을 세어
실제 가시 표본 포화를 발견한 조건에만 Phase.40/상한4,8을 추가 비교한다. HDR finite·D3D오류 검사는 필수다.
수치 대비 상승이 미학적 개선이나 림 품질 승인을 뜻하지는 않는다.
`VolumetricCloud.SolarOcclusion` (`--solar-occlusion-test`): 05 차폐 오차 관찰용 180조건 및 재현 화면 독립 비교 GPU 검사. finite/실행 오류를 검증하며 출력 오차를 화면 품질 합격으로 해석하지 않는다.
`VolumetricCloud.SolarBanding` (`--solar-banding-test`)는05-B 전체화면 직접 적분/캐시 경로 분리 검사다. VCLOUD_SOLAR_BANDING_DIR 지정 시에만 새 원본을 저장한다. GPU 타일 참조이므로 성능 판정용이 아니다.

## 05-B 저고도 캐시 참조 검증

일반 실행은80/40이다. 폐기 후보159/79는 --high-performance-test --shadow-height-2x와 SolarBanding/ShadowHeightPerformance 테스트 실행기에서만 사용한다. 일반 조작에서 --shadow-height-2x를 전달해도 규격을 바꾸지 않는다. 원본 비교 이력은 방향광 변경 기록을 따른다.

## 06 림 검사와 사용자 비교

Debug/Release 빌드 후 `ctest --test-dir build -C Release -R RimLighting --output-on-failure -j 1`로 관련 GPU 검증과 대표6case 성능을 실행한다. Debug에서는 성능 판정을 생략한다. `VCLOUD_RIM_APPROVED_ROOT`에 기존06-final을 지정하면24case 승인 원본 비교도 한다. `VCLOUD_RIM_CAPTURE_DIR`은 선택한 비교 사진만 저장할 새 폴더이며 기존 파일은 덮어쓰지 않는다.
일반 실행의 림 상한은2.5다. `build/Release/VolumetricCloud.exe --rim-review-cap-4` 또는 `--rim-review-cap-8`로 림 상한만 비교한다. F3 Cloud Rim의 실제 상한 표시를 확인한다. Type/Concept/핫 리로드 중에도 해당 실행의 비교 상한은 유지하며 Custom에 저장하지 않는다. 인자 없이 재실행하면2.5다.
07의 전체 CTest/12case 성능/07-final 촬영은06 사용자 승인 뒤 수행한다.

캐시 고정 광학 두께 비교: `VCLOUD_RIM_BOUNDARY_DIR`에 새 출력 폴더, `VCLOUD_RIM_BOUNDARY_DENSE_ROI=1`, `VCLOUD_RIM_CACHE_OPTICAL=1`을 설정하고 `--rim-lighting-test`를 실행한다. Density compare와 동시 사용하지 않는다. Urban/F5/71초/고도18·5에서 Base/Detail 캐시 각각 sigma1/2/4배를 View/Sun 공통 적용한다. View는 일반High이고 직접 적분 참조는 실행하지 않는다. `tests/AnalyzeCloudOpticalDepth.py <출력폴더> --cache`로 고정 마스크의 성분·Beer·View 일치를 분석한다. High early exit 때문에 tau 배수 오차는 관찰만 한다. 일반 실행 기본값은 바꾸지 않는다.
4배 줄 원인 분리: 새 `VCLOUD_RIM_BOUNDARY_DIR`와 `VCLOUD_RIM_STRIPE_DIAG=1`로 `--rim-lighting-test`를 실행한다. 다른 비교 모드 env는 제거한다. sigma4/Urban/F5/18·5도/ROI[1536,624,1696,720],Base 밀도 고정으로 View/Sun 독립 참조를 검사한다. `VCLOUD_RIM_STRIPE_CACHE=1`을 추가하면 기존/Near/Far/빛8분할/높이2배의 캐시 원인 분리만 실행한다. 높이2배는159/79 원인 진단이며 일반 채택이 아니다. pack5는 Direct RGB,pack1~3은 기존 채널 계약이다. 각 후보 이름과 stripe.json을 해석에 함께 사용한다. 출력 루트의 baseline Composite는1배 초기 상태이며4배 판정은 후보 pack 파일을 사용한다.
높이 보간 후보: `VCLOUD_RIM_STRIPE_DIAG=1`과 `VCLOUD_RIM_HEIGHT_INTERPOLATION=1`을 함께 설정한다.80/40 선형·80/40 단조Hermite·159/79 선형을sigma4배로비교한다. `VCLOUD_TEST_MONOTONE_HEIGHT`는테스트가컴파일할때만활성화된다. 전체Composite와Release대표성능도저장하며일반기본값은바꾸지않는다. 보간후보는줄감소부족으로미채택이다.
Far79 비교는새출력경로에서 `VCLOUD_RIM_STRIPE_DIAG=1`, `VCLOUD_RIM_FAR_HEIGHT=1`과 `--rim-lighting-test`를사용한다.80/40·80/79·159/79의실제배열/상수slice수일치를검사하고각slices.txt에남긴다. `SetShadowHeightRefinementForValidation(true,true)`만Far전용이며일반기본값과기존true호출의159/79의미는유지한다. false는Far전용상태도해제한다. ABI변경없음.
80/79 안정성 후속: 새 `VCLOUD_RIM_BOUNDARY_DIR`와 `VCLOUD_RIM_FAR_STABILITY=1`로 `--rim-lighting-test` 실행. Urban+세Type ×Near80/159·Far79 ×F5태양왕복/F7태양왕복/F7카메라이동,각61프레임 총1464프레임이다. Type전환시Density shaping .70와Shadow/Rim유지를검사하고sigma4를다시적용한다. 태양2.5~5.5도0.1도간격왕복,이동은5도에서lightRight방향0~12km/200m간격. anchor의Nearweight 1→0범위검사는CPU영역검사이며가시기여추적을대체하지않는다. LDR delta는16픽셀간격RGB평균절대변화이며실제구름/하늘변화도포함한다. 자동화질합격임계값으로쓰지않는다. `VCLOUD_RIM_STABILITY_FOCUS=1`을추가하면관찰된Mixed/F7최대변화주변만선별저장한다.
80/79승인 이후 `VCLOUD_RIM_APPROVED_CACHE_REVIEW=1` 비교는현재일반캐시를사용하며sigma4를테스트에만적용한다. 강도1/cap2.5·4·8과cap2.5/강도2·4를각각실행한다. `VCLOUD_RIM_BOUNDARY_DENSE_ROI=1`을함께지정하면조밀한ROI성분을저장한다. 일반림기본1/1/2.5와일반소멸계수는유지한다. 기존B-cache는이제현재승인80/79이며과거80/40캡처는해당source-manifest로구분한다.
림 폭 비교: 새 VCLOUD_RIM_BOUNDARY_DIR, VCLOUD_RIM_BOUNDARY_DENSE_ROI=1, VCLOUD_RIM_DEPTH_REVIEW=1을 설정하고 --rim-lighting-test를 실행한다. 다른 비교 env는 해제한다. F5/71초/Urban, 방위-90·0·90, 고도18·5, 강도2/상한2.5/sigma4/80·79에서 depth .5/1/2를 비교한다. 일반 depth1과 소멸계수는 바꾸지 않는다. `python tests/AnalyzeRimDepth.py <출력폴더> [다른빌드폴더]`로 고정 ROI 불변성과 림 성분 차이를 검사한다. 실제 조건은 depth-contract.json이며 후보명 없는 초기 Composite는 비교에서 제외한다. 현재 일반 림 강도는 사용자 승인2이고, 위의 강도1 기록은 이전 비교 당시 상태다.

양쪽 광로 샘플 비교: 새 VCLOUD_RIM_BOUNDARY_DIR, VCLOUD_RIM_BOUNDARY_DENSE_ROI=1, VCLOUD_RIM_PATH_REVIEW=1로 --rim-lighting-test 실행. 다른 비교 env는 해제한다. current/transport/joint 각18조건이며 path-contract.json이 후보값을 정의한다. AnalyzeRimDepth.py가 해당 파일을 발견하면 path 모드로 분석한다. 일반 렌더 macro는 없으며 F3 기본값을 변경하지 않는다.

Sky 색 비교: 새 VCLOUD_RIM_BOUNDARY_DIR, VCLOUD_RIM_BOUNDARY_DENSE_ROI=1, VCLOUD_SKY_COLOR_REVIEW=1로 --rim-lighting-test 실행한다. 다른 비교 env는 제거한다. sigma1/Urban/F5/71초/18·5도와 방위-90·0·90에서 before/after를 비교한다. sky-contract.json이 실제 계약이며 depth-contract의 후보 목록보다 우선한다. pack7=Sky RGB+ViewT. python tests/AnalyzeSkyColor.py <출력폴더> [다른빌드폴더]로 불변성/색을 검사한다.

두 차폐 방식 비중 비교: VCLOUD_SKY_BALANCE_REVIEW=1, VCLOUD_RIM_BOUNDARY_DENSE_ROI=1, 새 VCLOUD_RIM_BOUNDARY_DIR에서 --rim-lighting-test 실행. 다른 비교 env 해제. Urban/F5/71초/방위-90/18·5도, sigma1, before/after × base/sky/multi/both 총16조건. balance-contract.json을 사용한다. python tests/AnalyzeSkyBalance.py <출력폴더> [다른빌드폴더]로 성분 독립성을 검증한다. 일반 수식/UI/기본값 변경 없음.

기본 직접광 비중 비교: 새 VCLOUD_RIM_BOUNDARY_DIR, VCLOUD_RIM_BOUNDARY_DENSE_ROI=1, VCLOUD_BASE_DIRECT_REVIEW=1로 --rim-lighting-test 실행한다. 다른 비교 env는 해제한다. Urban/F5/71초/방위-90/18·5도, sigma1, k1/.75/.5. direct-contract.json이 실제 계약이다. python tests/AnalyzeBaseDirect.py <출력폴더> [다른빌드폴더]로 BaseDirect만 k배이고 나머지는 불변인지 검사한다. 일반 실행에는 k UI/기본값을 추가하지 않았다.

Powder 비교: 새 VCLOUD_RIM_BOUNDARY_DIR, VCLOUD_RIM_BOUNDARY_DENSE_ROI=1, VCLOUD_POWDER_REVIEW=1로 --rim-lighting-test를 실행한다. 다른 비교 env는 해제한다. Urban/F5/71초, 방위90/0/-90, 고도18/5, 강도0/.25/.5의18조건. powder-contract.json이 실제 계약이다. python tests/AnalyzePowder.py <출력폴더> [다른빌드폴더]로 불변성·감광 단조성·강도 비례·빌드 차이를 검사한다. 일반 실행 Powder UI/기본값은 추가하지 않는다.

Detail 비교: 새 VCLOUD_RIM_BOUNDARY_DIR, VCLOUD_RIM_BOUNDARY_DENSE_ROI=1, VCLOUD_DETAIL_REVIEW=1로 --rim-lighting-test 실행. 다른 비교 env 해제. Urban/F5/71초, 순광/측광18·5도, 침식0/현재.24/.5/1 및 Detail1 그림자 정합 진단. detail-contract.json이 실제 계약이며 일반 기본값 변경 없음. python tests/AnalyzeDetailReview.py <release출력폴더> [debug출력폴더]는 ROI 수치와 상위 gallery.html을 생성한다.

### F1/F4 슬롯 회귀와 비교 촬영
`ctest --test-dir build -C Release -R "LightingPresetStore|PresetSlotsSmoke" --output-on-failure`는 임시 루트만 사용한다. `build/Release/VolumetricCloud.exe --preset-slots-capture`는 1920×1080/F5/71초/바람0/8프레임 예열에서 내장4형상×4조명을 PNG와 설정 JSON으로 `captures/preset-slots/<실행ID>`에 보존한다. 기존 캡처는 덮어쓰지 않는다. 이 자료는 새 후보 비교이며 이전 baseline과 동일 픽셀을 요구하지 않는다. 화면 승인은 사용자 몫이며07-final이 아니다.

## 2026-09-18 프리셋 경로 갱신
현재 활성 JSON은 저장소 presets/의 형상4개·조명4개다. 개발 실행은 원본을 읽고 Save Preset으로 수정한다. 소스 루트가 없는 배포에서는 exe 옆 presets/를 사용한다. CMake 빌드마다8개를 copy_if_different로 배치하며 JSON만 수정해도 복사한다. 일반 시작의 Snow 자동 교체는 제거했다. 이전 captures/noise-lab 저장/초기화 설명은 역사적 동작이다. snapshot 출력과 UI 설정은 captures에 유지한다. schema/CB/승인 기본값은 변경하지 않는다. 상세: [문제와 해결 기록](changes/stage15-cloud-quality-followups.md).

01 진단: OFF Release VolumetricCloud.exe --cloud-clarity-test 또는 CTest CloudClarity. 저장프리셋은 읽기만 하고 captures/cloud-clarity/diagnostics-<pid>-<tick>에 조건/JSON/PNG/HDR/수치/성능을 쓴다. VCLOUD_CLARITY_REFERENCE_SHADERS는 선택형 변경 전 셰이더 폴더이며 Composite tolerance를 확인한다. 일반 Aerial4는 유지하고 테스트에서만16/32로 재컴파일한다.32도 정확한 해로 단정하지 않는다.

### Detail 해상도 비교
OFF Release에서 ctest --test-dir build -C Release -R "^VolumetricCloud.CloudDetailResolution$" --output-on-failure -j 1을 실행한다. 앱 직접 실행은 --cloud-detail-resolution-test다. 현재 Cumulus JSON의 Detail 크기2000m를 요구하며 원본을 보정/저장하지 않는다. CTest 결과는 build/captures/cloud-detail-resolution/<PID>-<tick>/comparison.html에 생성한다. 일반32³을 유지하고 후보64³은 테스트 안에서만 생성한다. GPU 성능 측정은 다른 GPU 작업과 겹치지 않게 실행한다. 상세 조건/한계는 stage15-cloud-quality-followups.md를 따른다.

### 원경 대기9조합
--cloud-aerial-tuning-test 또는 CTest VolumetricCloud.CloudAerialTuning은 Detail64³에서 현재 환경3 baseline과9조합을 생성한다. 출력은 CTest 기준 build/captures/cloud-aerial-tuning/<PID>-<tick>/comparison.html이다. GPU 테스트는 직렬 실행한다. 일반 Detail64³ 채택 후 과거32/64 비교 테스트도64³ 원래 상태로 복원한다.

후광 비교: --cloud-aerial-phase-test / CTest VolumetricCloud.CloudAerialPhase는 기존 대기 비교기에서 Mie g0.8/0.6/0.4만 변경한다. CTest 출력은 build/captures/cloud-aerial-phase/<PID>-<tick>/comparison.html. Release만 성능 반복을 수행하고 Debug는 이미지·D3D 검증을 수행한다.

근경 형상 비교: --cloud-shape-tuning-test / CTest VolumetricCloud.CloudShapeTuning. 현재 Cumulus와 환경3을 읽고 Density shaping±10%만 변경한다. build/captures/cloud-shape-tuning/<PID>-<tick>/에8개 원본 사본, 후보JSON, F5/F6 정지4모드와24프레임 회전/전진,8배 차이PNG, 비교HTML을 저장한다. GPU 직렬 실행. Release만 warm60+유효300샘플×3회 정지/회전 계측. 원본 비파괴/노이즈 해시/유한HDR/D3D오류/기준복원 tolerance를 검사한다.

Detail 크기 비교: --cloud-detail-size-test / CTest VolumetricCloud.CloudDetailSize. 기존 형상 비교기를 사용하되 Density shaping/침식/profile을 고정하고 Detail world size만 ±10%로 바꾼다. 출력 build/captures/cloud-detail-size/<PID>-<tick>/. 노이즈 재생성 없이 동일64³ 데이터 사용. GPU 직렬 검증/성능 조건은 CloudShapeTuning과 같다.

밀도 전이 시험: --cloud-density-transition-test / VolumetricCloud.CloudDensityTransition. 폭1/.75/.5 비교. 출력 build/captures/cloud-density-transition/<PID>-<tick>. 기존 형상 실행기 검증/성능 조건 사용.

거리별 View step 진단: --cloud-distance-step-test / VolumetricCloud.CloudDistanceStep. build/captures/cloud-distance-step/<PID>-<tick>. 전이폭1 고정으로 기준100m/512 대 근경50m/25m(5km까지,15km에서기존복귀,4096회)을 비교한다. 일반 High와 원본 JSON 유지. CPU 빌드 완료 후 GPU 테스트 직렬, 성능 중 다른 빌드 금지.

Weather/Base 분리: VolumetricCloud.exe --cloud-base-weather-test 또는 CTest CloudBaseWeather. GPU 직렬 실행. captures/cloud-base-weather 아래 원본8JSON/HDR12개/선형PNG48개/통계/비교페이지. 테스트 shader define 전용이며 일반 렌더 경로/CB는 유지한다.

Base 재매핑 비교: --cloud-base-remap-test / CTest CloudBaseRemap. F5/F6의 문턱/대비/중간옥타브5케이스. Release에서60예열+300GPU표본×정지/회전×3회×5케이스. GPU직렬실행, 원본프리셋불변, shader/Texture3D복원검사. 출력captures/cloud-base-remap.

임시Base UI검증: --base-candidate-ui-smoke / CTest BaseCandidateUiSmoke. 실제전환/전체핫리로드/Original복원/프리셋불변검사. 클릭/환경별화면품질은사용자검증.

--cloud-path-length-test / CloudPathLength: F5/F6길이·밀도·거리진단,소멸계수독립성/Composite복원/원본불변. 일반렌더step은유지.

P0–P2 근경/원경 보완: `ctest --test-dir build -C Release -R CloudNearFarDiagnostics --output-on-failure -j1` (`--cloud-near-far-diagnostics-test`). 기존후보격자재실행없이실제View적분/광선밀도/대기계약검사. 캡처는build/captures/cloud-near-far. 느린진단이며성능후보아님. 일반기본값/프리셋변경없음.

`--cloud-detail-core-test` / CTest `VolumetricCloud.CloudDetailCore`: 기존형상비교기로Detail코어가중치/제거상한을비교한다. 일반기본값불변. 결과build/captures/cloud-detail-core. GPU직렬,Release성능측정중다른빌드/테스트금지.


`--cloud-detail-core-types-test` / CTest `VolumetricCloud.CloudDetailCoreTypes`: 기존 형상 실행기로 저장된 네 타입의 기존/가중치35%를 직렬 비교한다. 결과 cloud-detail-core-types/<ID>/comparison.html. 일반 실행 변경 없음. Debug/Release 사용 가능, 성능 재측정 제외.


2026-09-21: `--detail-core-slider-smoke` / DetailCoreSliderSmoke는0/.325/.65/1전달,이전후보동등성,Base태양T보존,핫리로드,원상복원,원본8JSON불변검사. `--cloud-density-transition-test`/CloudDensityTransition은렌더수식과함께폐기. 과거자료는보존.

근경 경계 재분석: `python tests/AnalyzeCloudBoundary.py <대기합성결과> <보존계보폴더> <출력폴더>`. NumPy 필요. 대기 진단은 Near752배의 화면 전체 폭1920×64 Cloud T를 near-boundary-strip.csv에 추가 저장한다.

## 근경 선명도 / Detail 태양 차폐 진단
`build/Release/VolumetricCloud.exe --cloud-near-clarity-test`를 build 작업폴더에서 실행한다. CTest VolumetricCloud.CloudNearClarity는 VCLOUD_ENABLE_DIAGNOSTIC_TESTS=ON일때만등록(1200초/GPU직렬).
`python tests/AnalyzeNearClarity.py <결과폴더>`(NumPy/Pillow)로 독립검산과 comparison.html을생성한다. 결과는 captures/cloud-near-clarity/<고유ID>. 일반값/8프리셋불변. 기존격자재실행이나 일반형상채택이아니다. 현재Cumulus 진단뒤 유망후보가없으면 다른타입/이동/성능비교를확대하지않는다.

2026-09-22 후보2 사용자 채택 후, 일반 셰이더=후보2를 검증하고 ROI 기준은 역사적 후보0으로
고정한다. 채택 검증 실행의 Release에서만 `VCLOUD_CLARITY_MEASURE_PERFORMANCE=1`을 주면
F5/Near75/F6의 후보0/2를 120프레임 예열+600개 고유 GPU 표본으로 측정한다. GPU 작업은 직렬로
실행하며 `performance.csv`와 README의 p95/기준 판정을 남긴다. 시간71/바람0 정지 비용이며
다른 타입·태양각·이동 안정성/성능을 검증한 것으로 확대하지 않는다. 환경변수는 실행 뒤 해제한다.

## Detail 대역 결합 후속

build 작업폴더에서 `Release/VolumetricCloud.exe --cloud-detail-bands-test`를 실행한다.
출력은 `build/captures/cloud-detail-bands/<고유ID>`이며 원본8JSON과 일반 remap을 보존한다.
루트에서 `python tests/AnalyzeDetailBands.py <출력폴더>`로 NumPy/Pillow 검산과 comparison.html을 만든다.
CTest `VolumetricCloud.CloudDetailBands`는 `VCLOUD_ENABLE_DIAGNOSTIC_TESTS=ON`만 등록한다.
GPU 직렬/timeout900초. 후보0은 현재 remap,1은 연속 remap,2는 평균 제거량 보정이다.
이전 NearClarity 후보 번호와 혼동하지 않는다. 상세: [E17 기록](changes/stage15-cloud-detail-band-composition.md).

## Detail 원본 주파수 후속

build 작업폴더에서 `Release/VolumetricCloud.exe --cloud-detail-spectrum-test`를 실행한다.
같은 실행기에 새64³ fBm 생성기와 현재/보정후보의 짧은 이동 비교를 추가했다.
출력은 `build/captures/cloud-detail-spectrum/<고유ID>`이며 분석 명령은
`python tests/AnalyzeDetailBands.py <출력폴더>`다. README의 실험 표식으로 E17/E18을 구분한다.
CTest `VolumetricCloud.CloudDetailSpectrum`은 대량 진단 옵션 ON에서만 등록한다(900초/vcloud_gpu 직렬).
후보0=단일 Worley,1=fBm,2=평균 제거량 보정 fBm. 일반 생성기/8프리셋은 복원·보존한다.
회전/전진 각24위치의 화면 차이는 의도한 이동을 포함하므로 인접 Alpha MAE를 깜빡임 점수로 해석하지 않는다.
상세: [E18 기록](changes/stage15-cloud-detail-spectrum.md).

2026-09-22 사용자 채택 후 일반=후보1(무보정 fBm)이다. 진단은 정의0으로 옛 단일 Worley를
생성해 기준0을 보존하고 일반=1 동등성/복원을 검사한다. 회전·전진은0/1을 촬영한다.
Release 채택 성능은 `VCLOUD_SPECTRUM_MEASURE_PERFORMANCE=1` 환경변수를 준 위 명령으로 실행한다.
Near75/F5/F6의0/1을120프레임 예열+600개 고유 GPU 표본으로 측정하고 performance.csv/README에 기록한다.
실행 뒤 환경변수를 해제한다. 측정 중 다른 빌드/GPU 테스트는 실행하지 않는다.
