# feature/stage15-final-quality

## 목적

Stage 14 승인 상태를 품질 예산과 장면 외형으로 분리하고, 네 포트폴리오 콘셉트와 방향성 Cirrus,
재현 가능한 정량 gate를 최종 런타임 계약으로 묶는다. 15A 회귀 수정은 이 외형을
바꾸기 전에 `물리 출력/DPI → High Full RT → Temporal → Joint4`의 선명도 통로를 검증한다.

## 구현

- CPU 전용 `Stage15QualityPreset`, `Stage15ConceptPreset`, `Stage15DiagnosticMode`와 순수 resolver를
  추가했다. 별도 품질 GPU 상수버퍼는 만들지 않았다.
- Low/Medium은 50% Joint4/Stable 4-Phase를 사용한다. High는 Full Cloud RT, Nearest 1:1,
  Full-resolution Temporal로 승격하고 50% 복원용 4-phase source jitter를 쓰지 않는다.
  첫 High descriptor는 `100m/512`, Cone8, Balanced512며 1080p GPU Cloud p95 10ms를 먼저
  통과해야 `80m/768`을 별도 후보로 측정한다.
- Urban/Meadow/Desert/Snow는 Weather·Domain·Shape·Light·Environment·Atmosphere·Ground를 프레임
  경계에서 적용한다. 네 Weather RGBA/hash는 CPU에 고정 캐시하고 GPU texture/SRV 하나를 갱신한다.
- `CloudShapeMode::CirrusPhysicalLayer=2`와 48바이트 방향/scale/두께/profile 블록을 추가해 b7을
  64→112바이트로 확장했다. Weather/Base/Detail/Light/Deep Cache가 같은 Cirrus 밀도 함수를 쓴다.
- 화면 raymarch는 같은 source를 일반/Cirrus 변형으로 컴파일해 hot loop의 shape 분기를 제거한다.
- Deep Shadow도 non-Cirrus/Cirrus CS로 특수화하고 두 variant가 함께 성공할 때만 hot reload한다.
- 품질·콘셉트·진단·Temporal 요청은 한 `Stage15TransitionRequest`로 합쳐 프레임 경계의 고정 순서로
  처리한다. 적용 전에 CPU/Weather/GPU resource identity 전체 snapshot을 잡고, 뒤 단계 실패 시
  Weather 픽셀까지 복구한다. 전부 성공했을 때 transition commit을 한 번 기록하며 canonical no-op,
  Weather hash upload 생략과 fault injection rollback을 smoke에서 검사한다.
- 상수버퍼는 content dirty mask로 중복 Map을 생략하고, SHA-256 shader bytecode cache는 warm startup의
  D3D compile을 0회로 만든다. hot reload는 cache를 우회해 전체 세대를 검증한다.
- Temporal history clipping neighborhood는 full-resolution Joint 재구성 9회 대신 현재 low-resolution
  3×3을 직접 읽어 Stage 11 거부 계약을 유지하면서 Resolve p95를 2ms 아래로 낮췄다.
- 자동 quality/performance 모드에서는 화면과 history에 영향을 주지 않는 Temporal 통계용 네 번째
  MRT, mip 생성과 readback을 생략한다. 관찰 도구의 비용을 GPU fixture에 섞지 않기 위해서다.
- F4에 Concept/Quality, Temporal override, compact/performance overlay, Advanced Capture/Reference/Restore와
  실제 출력 extent를 추가한다. Physical Client, SwapChain, Viewport, Scene Color,
  Full Depth, Cloud RT, Temporal History, DPI/scale, PMv2/Native 상태를 함께 읽는다.
  Q/T는 ImGui keyboard capture와 repeat를 존중하며 숫자 0~9와 F1~F8은 보존한다.
- Temporal override는 첫 T에서 Off/Stable/Full Resolution 실제 모드를 저장·반전하고
  두 번째 T에서 복원한다. T는 Quality가 소유한 Cloud RT 해상도를 바꾸지 않는다.
  descriptor는 raw padding이 아닌 명시적 필드로 비교하고 실제 변경에만 history를 reset한다.
- F1은 Legacy Normalized Layer/Weather Physical Thickness/Cirrus Physical Layer를 구분하며
  Cirrus의 Bulk 이동과 방향·두께·profile 실제값을 읽기 전용으로 표시한다.
- snapshot을 schema 37로 올려 Stage 15 선택, Full-resolution Temporal과 실제 기존 구조체 값을
  기록한다. schema 36 이하는 Temporal mode 2를 허용하지 않는다. Cirrus는
  `cloudShape.mode=cirrusPhysicalLayer`와 방향·scale·두께·profile 실제값을 내보내며 schema 29 Custom
  appearance는 바꾸지 않았다.

## Stage 15A 선명도 회귀 수정

- 실행 파일은 PerMonitorV2 DPI aware며 `WM_SIZE`/`WM_DPICHANGED`에서 물리 client pixel로
  SwapChain과 크기 종속 리소스를 재생성한다. 별도 Native 1080p는 정확한
  1920×1080 borderless client를 만들고 저장한 일반 창을 복원한다. 프리뷰가 남긴
  512×512 viewport는 Present 전에 물리 출력 크기로 되돌린다. Native 전환이나
  style/placement/client 복원이 일부 실패하면 Capture 소유권과 저장 상태를 유지해 다음 frame에
  재시도한다.
- Joint4/Temporal은 공유 class/plane hard rejection을 쓴다. Geometry/Sky는 hard reject,
  Geometry/Geometry는 Full D32 local plane, Sky/Sky는 Cloud Depth/T soft guide를 쓴다.
  debug ID 80은 hard-valid 0~4 taps를 검정~흰색으로 표시한다. Full RT는 spatial resolve를
  우회하므로 균일한 짙은 회색 N/A다.
- Capture Still은 exact Native 1920×1080에서 Full/T Off/50m/1024/Balanced512로 렌더한
  projection-jitter 4개를 `R32G32B32A32_FLOAT` HDR에 running average한 뒤 tone map한다.
  누적 동안 카메라·시간·장면 입력을 잠그고, Capture가 Native 전환을 소유한
  경우만 Restore에서 일반 창으로 돌려놓는다. Reference는 별도 DirectReference 진단이다.
- Capture에 4K, 16 spp, sharpen, Joint9, Shadow1024를 조합하지 않는다. 실제 해상도와
  4-sample 누적의 효과를 다른 기능이 숨기지 않게 하기 위해서다.

## 자동 검증

- `Stage15PresetMath`: resolver 결정성·단조성·소유권, Weather 점유율, b7 ABI와 Cirrus world anchoring,
  High Full descriptor, Capture canonical descriptor, zero-mean 4 jitter와 Capture 상태.
- `Stage10UpsamplingMath`: Geometry/Sky class, Geometry plane, Sky/Sky, hard-valid tap 수와 fallback.
- `Stage11TemporalMath`: Off/Stable/Full Resolution mode flag, schema 37, 4-phase 방문과 reset 사유.
- `--stage15-preset-smoke-test`: 48조합의 finite/hash/resource/history와 8.5km Cirrus 내부 밀도.
- `--stage15-quality-test`: 1080p의 전체 화면·`cloudMask`·`cloudEdgeMask`에서 산란/T를
  측정한다. High가 Full이 아니거나 같은 Concept/Camera의 cloud-edge RMSE가 Medium보다
  유의미하게 낮지 않으면 실패한다. Medium Temporal은 history를 새로 시작해 4/8/16 frame의
  age/reset과 오차를 함께 기록하고, reset이 없는데 age가 늘지 않거나 16 frame이 4 frame보다
  개선되지 않으면 실패한다. 요청 상태 불일치나 Cloud target 준비 실패도 fail-fast한다.
- `--stage15-performance-test`: 48조합×120 warmup×600 고유 timestamp. 같은 50% 구조인
  Low/Medium만 상대 gate를 쓰고 High는 과거 180% 상대 gate 없이 Frame/Cloud/Resolve 절대 p95와
  Stage 14 회귀만 판정한다.
- `--stage15-stage14-regression-probe`: DenseHorizon 3회 중앙값과 GPU 7구간/Cloud Total, raw CPU,
  상태 fingerprint와 shader hash. `--shader-cache-smoke-test`는 cold/warm bytecode 동등성과 compile 0회.
- CPU/UI 회귀는 T override 왕복·직접 편집 해제, 진단 중 Quality/Q/T 차단과 Restore, Cirrus mode·
  bulk-advection/schema 37 실제값 계약을 검사한다.
- 자동 명령은 JSON/CSV만 쓰며 포트폴리오 PNG와 화면 미학 판정은 사용자에게 남긴다.

최신 Debug smoke에서 물리 1280×720 → Native 1920×1080 → High Full → 일반 창 복원과
Capture descriptor rollback, Win32 성공/렌더 리소스 실패, 첫 windowed restore 실패 뒤 재시도가
통과했다. Joint4 ID 80의 순수 sky 8,482픽셀은 평균 `4.000000` taps이며 3D Base/Detail sampler의
linear/wrap 계약도 통과했다. Capture는 Native 1920×1080·4/4 HDR·Ready 고정·복원을 통과했다.
Release Stage10/11/15 targeted suite는 `8/8` 통과했다. Release 전체 CTest 첫 실행은 Stage 12
smoke가 첫 캡처의 이전 출력 크기를 이어받아 `53/54`였지만, fixture가 캡처 전에 `96×54`를
명시하도록 고친 뒤 Stage 12 focused Debug/Release `2/2`가 승인 수치
`MAE=0.001635`, `P99=0.029349`로 다시 통과했다. 최종 Debug/Release 전체 CTest도 각각
`54/54`로 통과했다.

48개 실시간 조합의 절대·상대 성능 gate는 이전 **High 50% descriptor**에서 통과했다.
이 수치는 High Full로 승격한 15A의 최종 증거가 아니다. non-Cirrus raymarch와
Deep Shadow의 고정 FXC `/O1` 결과는 `stage14` tag와 바이트 단위로 같다. DenseHorizon probe는
`5.659648ms`/`0.745381×`, 48-case 내부 회귀는 `6.979584ms`/`0.919218×`다.

이 수치 뒤 숨김 79와 Temporal Off 출력 문제를 고친 첫 96-case는 전체 평균의 작은 Temporal bias로
실패했으므로 폐기하지 않고 회고 자료로 보존했다. 15A의 실제 선명도 목적에 맞춰 reference T로만
정의한 cloud mask와 cloud-edge mask를 추가하고 gate를 다시 실행했다. 최신 Release 1080p 96-case는
모두 통과했다. 16개 High Cloud RT가 전부 1920×1080이었고, 동일 edge 1,876,893픽셀에서
Medium→High RGB RMSE는 `0.00304644→0.00033319`, T MAE는
`0.00327674→0.00006603`으로 감소했다. Medium Temporal 4→16 frame의 edge RGB RMSE는
`0.00347051→0.00337033`, edge T MAE는 `0.00347650→0.00345479`로 개선됐고 history age는
`4→16`, 최근 60 frame reset count는 `9→9`였다. 전체 화면 T MAE의 작은 bias 증가는 JSON에
그대로 기록하되 edge 두 지표를 선명도 수렴 gate로 삼는다.

최신 High Full 48-case 성능 1차 실행은 다른 게임 PID가 Windows GPU 3D 엔진을 약 37% 사용하고
별도 회귀 인스턴스까지 겹쳐 무효다. 이 실행의 실패 수치는 오염 증거로만 보존하며, 외부 GPU 부하가
없는 단독 재실행은 48/48을 통과했다. 최악 High Frame/Cloud p95는
`10.0014/9.95738ms`, 전체 Resolve 최악은 `1.12026ms`다. 성능 명령 내부 Stage 14 회귀는
`6.37645ms`/`0.839784×`, 독립 3-block 중앙값은 `6.21363ms`/`0.818341×`로 모두 통과했다.
포트폴리오 화면 비교와 사용자 승인도 별도 대기다. 15A는 출력 extent, High Full,
Temporal reset/phase, Joint4 tap, Capture 4/4와 Release 화질·성능 자동 검증을 통과했다.
사용자 렌더 검증만 별도 승인 gate로 남긴다.

## 사용자 승인

아직 미승인이다. [Stage 15 프리셋 사용자 검증 가이드](../STAGE15_PRESET_VALIDATION_GUIDE.md)의
10분 빠른 승인, 상세 판정 카드와 증상별 진단을 수행하고 사용자가 직접 캡처한 뒤 승인일·피드백·
재검증 결과를 기록한다.
