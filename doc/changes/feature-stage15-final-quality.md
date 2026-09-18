# feature/stage15-final-quality

## 2026-09-05 빌드 검증 역할 분리

기본 OFF Release가 주요 렌더 TC와 성능 측정 대상이며, ON Strict Validation은
필요 시 실행하는 결정적 해시 보조 도구다. Weather의 기존 strictness와 CameraCB 수정은 유지한다.
이하 all-strict 성능 보류는 과거 기록이다. 현재 규약과 검증 상태는
[정책 변경 기록](stage15c-determinism.md) 및 [빌드 규칙](../CONTRIBUTING.md)을 따른다.
최종 HDR/Tone tolerance 이미지 회귀는 규약만 정의했으며 전용 TC는 후속 구현이다.

## 2026-09-05 Stage 15C 학습 주석 정비

아래는 주석 전용 정비 당시의 기록이다. 이후 승인된 결정성 수정은 별도
[조사·수정 기록](stage15c-determinism.md)에 분리했다. 해시 결정성은 수정 후 검증을
통과했지만 High 성능 gate가 남아 있으므로 Stage 15C 최종 완료는 여전히 보류한다.

- 핵심 Weather/Noise, 밀도/광학, Deep Shadow, 대기 LUT, Scene/HDR/Tone 셰이더에
  입력→계산→출력과 단계별 한국어 설명을 추가했다. Fullscreen/NoiseLab 전용 상세화는 제외했다.
- CPU/HLSL 필드에 직접 조절 원본, 강제 범위, 기존 UI/내장값 기반 권장 출발점,
  파생 값/고정 품질/패딩을 표시했다. packed vector는 성분별 뜻을 양쪽에 맞췄다.
- Renderer의 생성 Dispatch와 프레임 패스/Draw/Present 순서, SRV/UAV 해제 이유를 기록했다.
- Weather 생성 b0 160B 참조를 보완하고 windSpeed의 세션 전역 소유권,
  현재 미사용 surfaceShadowEnabled, Base 밀도/phase 범위의 오래된 설명을 정정했다.
- 동작/기본값/API/ABI 변경 없음. 주석 구현은 반영했으며, 아래 최초 실행 해시 변동의
  원인 미확정으로 Stage 15C 최종 완료 판정은 보류한다.

### 자동 검증

- Debug/Release 빌드 성공. Release 전체 CTest **36/36 통과**(31.43초).
- Debug HighCloud/Atmosphere GPU smoke **2/2 통과**. D3D11 debug layer error 없음.
  활성 프로그램 런타임 컴파일과 기존 cbuffer 크기/슬롯 reflection 검증 통과.
- Weather CPU/GPU parity는 기존 허용 오차(채널 최대 1 UNORM 단계) 기준 통과.
  Atmosphere Release MAE=0.00018953, P99=0.00048470.
- High 성능: RTX 4080 SUPER, 1,440 samples, Cloud p95=5.882ms,
  Frame p95=6.818ms. Tone/Noise dependency/Weather rollback 테스트도 통과.
- 변경된 C++/HLSL **44개 파일의 주석·공백 제외 토큰이 HEAD와 동일**하고
  `git diff --check` 통과. 임시 계측 변경은 모두 제거했다.
- 동일 warm-cache 조건의 원본 HEAD 셰이더와 수정 셰이더에서
  **3 scene × 4 camera Weather/HDR 해시 12쌍 일치**.
  기존 `CaptureCloudFrameHash`는 Tone 이후 RGBA8 back buffer를 읽으므로,
  이 검증에서는 임시로 RGBA16F HDR target의 픽셀당 8바이트를 읽었다.
  원본 셰이더는 `build/stage15c-baseline/shaders`에 별도 추출해 override로 실행했다.
- **미해결:** 최초 compile/cache 준비 실행에서는 일부 프레임 해시가 후속 실행과
  달랐고, 원본 HEAD 셰이더에서도 같은 종류의 변동을 관측했다. warm-cache에서
  일치한다는 사실만으로 원인을 드라이버/캐시라고 단정하지 않는다. 첫 실행까지
  포함한 결정성 원인 분석·해결은 주석 외 동작 수정이 필요할 수 있어 별도 판단이 필요하다.
- 로컬 증거: `build/stage15c-ctest.log`, `build/stage15c-debug-gpu.log`,
  `build/stage15c-hdr-before.log`(최초), `build/stage15c-hdr-before-warm.log`,
  `build/stage15c-hdr-after-warm.log`, `build/stage15c-verify.ps1`.

## 기존 Stage 15 목적

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
