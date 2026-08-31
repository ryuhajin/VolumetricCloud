# feature/stage15-final-quality

## 목적

Stage 15B 실험 상태를 복구 가능한 commit으로 보존한 뒤, 사용자가 이해하고 유지할 수 있는 Full-resolution High 단일 렌더러로 축소한다. 화면 최종 승인은 아직 사용자 검증 전이다.

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
