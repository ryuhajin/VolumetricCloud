# feature/stage15-final-quality

## 목적

Stage 15B 실험 상태를 복구 가능한 commit으로 보존한 뒤, 사용자가 이해하고 유지할 수 있는 Full-resolution High 단일 렌더러로 축소한다. 2026-09-05 사용자 최종 화면 승인을 완료했다.

## 최종 구조

```text
Atmosphere LUT + Balanced512 Deep Shadow
→ HDR Scene + Depth
→ Full-resolution High Cloud Raymarch + 합성
→ Tone Mapping
```

## 유지

- Planar layer, Weather Map, Base/Detail Texture3D
- PhysicalColumnGeometry, base lift, footprint, 200m domain headroom
- Stage 9 support precheck, empty skip, distance step, early exit, cone fallback
- Stage 12 Balanced512 Deep Cache
- Stage 14 물리 대기 LUT, 지면·구름 HDR 조명과 Tone Map
- Urban/Meadow/Snow와 Stratus/Cumulus/Mixed

## 삭제

- Low/Medium/Custom 품질과 quality resolver/UI
- Stage 10 저해상도 target, cloud-data MRT, 업샘플링 filter와 수학/test
- Stage 11 jitter, reprojection, rejection/clip, history ping-pong과 수학/test
- 별도 Resolve/Composite pass와 NTE Rim
- Detail 거리 LOD
- CaptureStill, FineReference, DirectReference와 누적 자원
- OptimizationCB, Balanced/Conservative 등의 런타임 최적화 preset
- Desert concept와 Cirrus shape/noise/debug/UI/document
- schema 29/30 호환과 여러 cloud preset slot

Stage 10/11과 Cirrus는 완성되지 않아서 삭제한 것이 아니라, 최종 High 비교 후 유지 이득보다 구조 비용이 크다고 판단해 폐기했다. Git 이력과 단계별 changes 문서는 실험 사실만 짧게 보존한다.

## High 고정값

- Full resolution
- View 100m / 최대 512
- `T <= 0.01` early exit
- support precheck 항상 사용
- 빈 표본 3개, epsilon 0.0001, coarse 2×, 최대 200m
- 24~50km에서 step 최대 1.25×
- deterministic cone 8 taps, 2°, far fraction 0.77, bias 1m
- Balanced512 Deep Cache

## 프리셋과 저장

- `ApplySceneConcept`: formation + scene 조명/대기/지면
- `ApplyCloudType`: formation only
- `SaveCustomFormation` / `LoadCustomFormation`: formation only
- Custom 경로 `captures/noise-lab/custom-cloud.json`, schema 1
- 내장 preset은 immutable
- 구형 파일은 읽거나 자동 삭제하지 않음
- Noise Lab export는 현재 필드만 가진 schema 39

Stratus/Cumulus/Mixed의 coverage, density, extinction, detail, Weather threshold/softness/link/bias와 vertical profile은 Stage 14 값을 보존한다. 두께, base lift, footprint, Planar domain만 절단 방지 계약으로 보정했다.

## GPU 계약 변경

- CloudCB: 128B → 80B
- LightCB: 80B → 64B
- CloudShapeCB: 112B → 64B
- CloudDomainCB: Planar-only 32B
- ShadowCB: b12 → b8, 160B
- Stage14CB: b13 → b9, 224B
- 삭제: Detail LOD b8, Optimization b9, Upsampling b10, Temporal b11

프로파일 항목은 `Atmosphere / Shadow / Opaque / Cloud / Tone / Frame`으로 축소했다.

## 핫 리로드

- 프로그램별 source/entry/target/defines/dependencies/object manifest
- literal include transitive closure
- content-addressed bytecode cache
- 250ms watcher와 test-only forced scan
- 영향받은 모든 blob/D3D 객체를 임시 생성한 뒤 일괄 commit
- 실패 시 기존 object/hash/dependency/generation 유지
- Atmosphere LUT, Deep Cache, Noise Volume별 후속 invalidation
- F4 report: 결과, 변경 파일, 프로그램/compile/cache 수, 경과 시간

검증 fixture에서 Tone Map은 정확히 1개가 선택되고 오류 rollback과 원본 cache 복원을 통과했다. `Noise.hlsli` 변경은 Cloud PS, Deep Shadow CS, NoiseLab PS 정확히 3개만 선택했다.

## 검증 상태

- checkpoint: `d030efd`
- direct High 전환 commit: `5c06fae`
- Debug/Release 빌드 통과
- CPU 회귀 25개 통과. High C++/HLSL 고정값 parity도 검사
- Release 전체 CTest 33/33 통과
- High GPU smoke: 세 concept × F5~F8, 세 type, Deep Cache/LUT 진단 통과
- Custom GPU smoke: formation만 복원하고 scene 상태를 보존함을 확인
- schema 39 GPU smoke: 현행 Weather/Shape/Texture3D 필드와 폐기 키 부재 확인
- shader reflection: CloudCB 80B, LightCB 64B, EnvironmentCB 80B,
  DomainCB 32B, NoiseVolumeCB 96B, ShapeCB 64B, ShadowCB 160B(b8),
  Stage14CB 224B(b9) 통과
- shader cache warm compile 0, cache hit 14
- Tone reload transaction: 영향 1-program, 오류 rollback, cache 복원,
  warm 전체 `0.024s` 통과
- Noise dependency reload: Cloud/Deep Shadow/NoiseLab 정확히 3-program,
  warm 전체 `0.224s` 통과
- 1920×1080 Release, RTX 4080 SUPER: 실행당 1,440 samples로 3회 연속 통과.
  반복 최악 전체 Cloud p95 `5.153ms`, Frame p95 `5.984ms`; 최악 단일 case
  `8.888/9.788ms`로 12 case 각각의 gate도 통과
- 삭제 대상 심볼과 삭제 파일의 `src/shaders/tests/CMake` 참조 0건
- 사용자 Release 화면 검증 및 최종 승인은 미완료

## 2026-09-01 사용자 검증 UI 보완

- F1이 ImGui 메시지 뒤로 빠져 X로 닫은 창을 다시 열지 못하던 입력 순서를 고쳤다.
  F1~F4를 공통 전역 함수키 매핑으로 처리하고 CPU 회귀를 추가했다.
- F1 상단에 저장된 formation을 불러오는 `Custom` 버튼을 복원하고 Preview field를
  slider에서 combo 목록으로 바꿨다.
- F2 Weather Map RGBA를 Weather Generator 최상단으로 옮겼다. F1이 소유한 현재
  Cloud Type 소스를 표시하며 고정 타입에서는 무효인 G 제작 slider를 비활성화한다.
- 기존 Wind speed는 실제 Weather/Base/Detail 공통 advection임을 확인해 이름과
  설명을 바꿨다. 별도 Weather speed나 독립 offset 시스템은 추가하지 않았다.
- GPU profiler를 F4에서 분리해 좌측 상단 Performance 창으로 옮겼고 FPS, cloud time,
  CPU/GPU frame과 Atmosphere/Shadow/Opaque/Cloud/Tone 시간을 표시한다.
- `CloudDepth`의 누락된 표시 이름 때문에 두 번째 `Composite`가 생긴 문제를 고쳤다.
  `Optical Depth`/`Cloud Depth`를 구분하고 각 selectable에 enum ID를 부여했다.
- UI 보완 후 Debug/Release 빌드, Debug CPU `25/25`, Release 전체 CTest
  `33/33`을 통과했다. NoiseLab smoke는 Cloud view와 Preview label 중복을 하드
  게이트로 검사한다.
- 사용자 화면 재검증과 최종 승인은 아직 미완료다.

### F1 이동 속도와 presentation 후속 보완

- 잘못 복원한 절대 `Time` scrub과 `Animate clouds`를 제거했다. 첫 보완에서는 F1을
  0~4배 time scale로 연결했지만 기본 12m/s와 12~64km noise 규모에서는 최대 48m/s가
  화면상 거의 정지처럼 보여 사용자 검증에 실패했다.
- 최종 F1 `Cloud movement speed`는 `CloudCB.windSpeed`를 0~400m/s로 직접 편집한다.
  effective time은 실제 delta time으로 누적하고 GPU offset은
  `direction × speed × time`이다. F2의 중복 속도 slider는 제거하고 방향만 남겼다.
  Weather/Base/Detail과 Deep Cache에는 별도 offset이나 속도가 없다.
- 일반 실행의 VSync 기본은 기존과 동일하게 On이다. 시작할 때 tearing 지원을 조회해
  swap chain 생성 플래그까지 설정하고, Off는 지원 환경에서
  `Present(0, DXGI_PRESENT_ALLOW_TEARING)`을 사용한다. F1은 현재 표시 방식과 미지원
  fallback을 직접 보여 준다.
- F2 ImGui 창 제목을 `F2 Noise and Weather`에서 `F2 Weather Map`으로 바꿨다.
- NoiseLab smoke는 일반 초기 VSync On과 effective time 증가를 검사한다. 추가 GPU gate는
  속도 0에서 서로 다른 두 시점의 HDR frame hash가 같고 400m/s에서는 달라지는지 검사해
  UI가 사용하는 CloudCB→HLSL 이동 경로를 고정한다. 현재 검증 PC는 tearing 지원으로
  확인됐다. Present CPU 테스트는 VSync/tearing/fallback 세 조합을 고정한다.
- 보완 후 Debug/Release 빌드, Debug CPU `25/25`, Release 전체 CTest `33/33`을 통과했다.
- 사용자 화면 재검증과 최종 승인은 아직 미완료다.
## 2026-09-04 Weather GPU·소유권 후속 정리

- Weather Map RGBA 생성을 8×8 Compute Shader로 이전하고, 임시 UAV 성공 결과만
  안정된 공개 texture/SRV에 복사한다.
- G는 모든 타입에서 지역별 원본을 저장하며 Fixed/Regional 선택은 새 b10에서 해석한다.
- b7을 48B profile 전용으로 줄이고 두께·lift·selection을 32B b10으로 분리했다.
- 구름 방향/속도를 세션 전역 Motion으로 분리해 타입·콘셉트·Custom 전환 시 보존한다.
- Custom schema 2는 motion을 저장하지 않고 selection을 저장하며 schema 1을 이관한다.
- 표준 FNV-1a 64-bit offset basis로 공통화하고 Weather/Atmosphere LUT GPU timing을
  별도 표시한다. F3 표시명은 `Sun altitude`이며 내부 elevation 키는 유지한다.
- CPU/GPU Weather RGBA8는 최대 1 LSB 차이만 허용하고, selection/motion 변경은
  generation을 건너뛰며 generator 변경은 정확히 한 번 생성하는 smoke로 고정했다.
- Weather shader의 정상 hot reload는 공개 texture/SRV identity를 유지하고, 강제 컴파일
  오류는 shader object·texture hash·generation·resource identity를 모두 보존한다.
- Debug/Release 빌드와 Release 전체 CTest `36/36`을 통과했다. RTX 4080 SUPER의
  최종 1080p High 1,440표본은 Cloud/Frame p95 `6.036/6.866ms`였다.

## 2026-09-05 사용자 최종 승인

- 사용자가 최종 렌더 결과와 Stage 15 Weather GPU·파라미터 소유권 정리를 승인했다.
- 자동 검증은 Debug/Release 빌드, Release 전체 CTest `36/36`, Weather hot-reload
  rollback, D3D11 오류 0과 1080p High 성능 gate 통과 상태다.
- 이 커밋을 원격 `feature/stage15-final-quality`에 게시한 뒤 PR merge commit으로만
  `main`에 병합하고, 병합된 main에 annotated `stage15-approved` 태그를 붙인다.
