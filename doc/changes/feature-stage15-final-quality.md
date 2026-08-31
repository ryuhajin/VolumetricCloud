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
- 상수버퍼는 content dirty mask로 중복 Map을 생략하고, root shader와 재귀 include closure 기반
  SHA-256 shader bytecode cache는 warm startup의 D3D compile을 0회로 만든다. hot reload도 변경되지
  않은 variant는 cache에서 재사용하되 전체 세대가 검증된 뒤에만 교체한다.
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
  Cirrus의 Bulk 이동과 방향·두께·profile 실제값을 Stage 15B 제작 컨트롤로 표시한다.
- Stage 15A에서 snapshot을 schema 37로 올렸고, 현재 Stage 15B 전체 snapshot은 schema 38이다.
  schema 36 이하는 Temporal mode 2를 허용하지 않는다. Cirrus는
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

## Stage 15B 형상 계약·제작 UI·Physical Fill·NTE 외곽

Stage 15B는 첨부 화면의 수평 바닥/상단 절단과 퍼진 외곽선을 수정하는 후속
작업이다. 우선순위는 `전역/로컬 두께 → UI Zoom → Physical Fill → NTE rim`이며,
각 단계를 독립 검증 단위로 유지한다. 이 절의 화면·성능은 아직 사용자 미승인이다.

### 15B-1 전역/로컬 두께 계약

- `PhysicalColumnGeometry`가 Weather A/G로 `localThickness`, 저주파 `localBaseLift`,
  `localBottom/localTop/localHeight`를 한 번 계산한다. View, support precheck, Light Ray,
  Deep Shadow가 같은 함수를 쓰며 런타임 top clamp로 잘림을 숨기지 않는다.
- 약한 컬럼일수록 `pow(1-A,1.5)`로 바닥을 올리되 local thickness의 25%로
  제한한다. 고주파 noise를 base lift에 쓰지 않는다.
- `activeMaximumThickness + localBaseLiftMax <= domainThickness`와 Stage 15 일반 콘셉트의
  추가 200m headroom을 CPU preflight에서 검사한다. 콘셉트·Custom·UI 적용은
  실패 시 이전 shape/domain/AABB Y로 원자 복구한다.
- b7 padding offset 56/60을 `localBaseLiftMaxMeters`/`footprintCoverageInfluence`로
  승격해 112바이트를 유지한다. 고정 20% footprint 수식은
  `lerp(1-influence,1,typedFootprint)`로 교체한다.
- Stage 15의 `도메인/로컬/base lift/footprint`은 Urban
  `3700m/2000~3200m/300m/0.50`, Meadow Stratus `5000m/1500~2500m/200m/0.40`,
  Meadow Cumulus `5000m/3000~4600m/200m/0.40`, Snow
  `2500m/1500~2300m/0m/0.20`이다. Desert는 3.5km 도메인 중심형 Cirrus
  0.5~1.5km를 유지하고 lift/footprint는 `0/0`이다.
- b5 offset 24를 LUT 대표 고도로 승격한다. Weather Physical은 도메인 중앙 대신
  `bottom + 0.5×activeMaximumThickness`, 중심형 Cirrus는
  `cloudBottomAltitude + cloudLayerThickness×cirrusVerticalProfileCenter`를 쓴다.
  Physical 환경광 높이 가중은 전역이 아니라 로컬 높이다.
- Weather는 256²를 유지하고 Physical 슬라이더를 `17.6~160km` 로그 범위로
  제한한다. F2 Scale Budget은 texel/파장/3D world size/View step/trace/50km 반복을
  같이 표시한다.

### 15B-2 DPI 기반 ImGui Zoom과 제작 UI 정리

- `effectiveUiScale = WindowsDpi/96 × userZoom`이다. 현재 144 DPI는 150%이고 Zoom은
  F4의 `100/125/150/175/200%` 버튼으로 선택한다. 기존 파일의 중간값은 버튼을 누르기
  전까지 그대로 적용한다.
- 패널이 닫혀도 `WM_DPICHANGED`를 전달하고 backend `NewFrame` 전에 한 번 적용한다.
  새 Dark style에서 매번 재계산하고 vector default font와 논리 픽셀 helper를 쓴다.
- Zoom은 `captures/noise-lab/developer-ui.json` schema 1에 원자 저장하며 렌더
  snapshot/fingerprint/history에 속하지 않는다.
- F1~F3에서 Physical 출력에 무효한 Procedural scale/global fade/독립 wind/16m
  reset/Pipeline Compare/Manual·Direct Reference 세부 튜닝/중복 버튼을 숨긴다. 회귀 코드는
  이전 schema·CLI/GPU fixture를 위해 남긴다. F4도 Physical 제작 경로만 표시하며
  Manual Reference 선택기는 schema/CLI 회귀 코드와 분리해 노출하지 않는다.
- 일반 대화형 실행은 주 모니터 중앙의 물리 1920×1080 client다. 바깥 frame이 작업영역을
  넘는 작은 모니터에서만 가장 큰 정수 16:9 client로 줄인다. 자동 test extent는 바꾸지 않는다.
- F1~F4는 한 번에 하나만 열고 새 패널은 우측 하단에 anchor한다. overlay와 교차할 때는
  패널 높이만 줄이고 내부 scroll을 사용하며 overlay Off에서 다시 확장한다.

### 15B-2A 공통 formation·독립 F1/F4 저장·Cirrus 제작

- CPU 전용 `CloudFormationSettings`가 coverage/density/extinction/erosion, Weather 전체,
  world size, shape, domain/trace, Base/Detail 월드 sampling size와 wind를 소유한다.
  Quality/step/Temporal, scene lighting, camera/time/offset과 Texture3D 생성 규격은 제외한다.
- F1/F4는 모두 `ApplyCloudFormationAtomic()`의 strict validation, sanitize, shape/type,
  200m domain fit, LUT 대표 고도, 임시 Weather 생성·검증, CPU/GPU commit과 reset 1회를 쓴다.
  실패하면 현재 runtime과 target을 유지한다.
- 저장소는 F4 Concept 4개, F1 Type 3개, Custom 1개의 schema 1 파일이다. 임시 파일 뒤
  `MoveFileEx`로 슬롯 하나만 원자 교체한다. 손상된 Concept/Type은 문제 파일을 보존하고
  내장값으로 fallback하며 Custom에는 내장 fallback이 없다. 자동 smoke/performance는 사용자
  override를 무시한다. Custom의 `saved` 표시는 파일 존재가 아니라 전체 preflight 성공으로
  정하며, 누락은 `No saved Custom`, 손상·범위·domain 오류는 파일을 보존한 채 거부 사유로 구분한다.
- F1 Stratus/Cumulus/Cirrus 최초 내장값은 Stage 15B 시작 시 F4 Snow/Urban/Desert formation을
  복사했지만 별도 resolver에 값으로 정의했다. 이후 한쪽 저장은 다른 쪽을 바꾸지 않는다.
- F1은 `Stratus | Cumulus | Cirrus | Custom`, Target/Source/dirty와 `Save to Preset`,
  `Save as Custom`, 확인형 `Restore Built-in`을 제공한다. F4 target에서 F1/F2 구름 값을
  편집하면 그 Concept target을 유지하고, F1 Type 버튼은 Concept target을 해제한다.
- Cirrus는 `CloudShapeMode::CirrusPhysicalLayer`를 유지한다. 같은 128³ Perlin-Worley Base와
  32³ Worley Detail을 flow basis의 Along/Across/Vertical meter scale로 다시 읽으며 texture를
  재생성하지 않는다. Flow, Base/Detail aspect, 50~6000m 두께, domain center와 중심 profile을
  F1에서 편집한다. 자세한 튜닝은
  [Cirrus 제작 가이드](../CIRRUS_CLOUD_AUTHORING.md)에 기록했다.

### 15B-3 Physical cloud-only fill

- EnvironmentCB offset 60/76을 `physicalSkyFillScale`/`physicalGroundFillScale`로 승격해
  80바이트를 유지한다. 배율은 `0~2`, 기본 `1/1`이다.
- Physical은 `SkyIrradianceLUT×skyScale`과 `GroundIrradianceLUT×albedo/PI×bounce×groundScale`를
  구름에만 적용한다. 대기 배경/Aerial/Sky View는 변하지 않고 b4 업로드와
  Temporal reset만 발생한다.
- Off/Balanced/Strong/Ground Check/Portfolio는 `0/0`, `1/1`, `1.25/1.25`, `0/1`, `1/1`이다.
  콘셉트는 Urban `0.85`, Meadow `0.95`, Desert `1.0`, Snow `1.1`의 sky/ground 동일
  배율을 쓴다. 기존 Sky/Ground Strength는 Manual Reference 절대색 모델만 소유한다.

### 15B-4 Full-resolution NTE형 rim

- OptimizationCB b9 offset 44는 `optimizationPadding0`으로 유지한다. View march는
  승인된 Stage 9 coarse→한 구간 rewind→fine 계약을 그대로 사용한다.
- Spatial/Temporal은 full-resolution cloud color/T+aux pair만 만든다. 새 `CloudComposite`가
  scene/atmosphere 합성과 rim을 후처리한다. Temporal On은 write-history pair, Off/Capture는
  같은 규격의 spatial pair를 쓰며 rim은 history에 저장하지 않는다. Composite 모드는 resolve의
  세 번째 HDR MRT를 바인딩하지 않아 최종 target을 중복 clear/write하지 않는다.
- 독립 48바이트 `CloudRimCB`는 enabled, width/intensity, opacity band, sun alignment/power,
  cloud-depth rejection, tint/clamp를 소유한다. D3D11 SM5 PS는 b0~b13만 지원해
  초기 계획 b14를 바인딩할 수 없다. 따라서 CB는 독립으로 유지하되
  UpsamplingCB를 읽지 않는 Composite pass에서만 b10을 재사용한다.
- 4방향 full-resolution opacity/depth 이웃, fractional linear 표본의 scene footprint 전체,
  geometry class/scene limit과 화면 태양 방향을 거친
  내부 경계만 premultiplied scattering에 더한다. Urban `1.5px/0.45`, Meadow
  `1.25px/0.30`, Snow/Desert Off이며 Low/Reference는 실효 Off다.
- Composite PS는 일반 rim과 `VCLOUD_DISABLE_RIM=1` no-rim variant를 함께 컴파일한다.
  실효 Off는 no-rim variant를 선택해 이웃 평가와 rim 기여를 compile-time 제거하고,
  hot reload는 두 variant가 모두 성공해야 원자 교체한다.
- 기존 enum 뒤의 ID 81 Local Base Offset, 83 NTE Rim Mask, 84 NTE Rim Contribution을
  사용한다. ID 82는 철회한 boundary 실험의 예약/무효 번호이며 Noise Lab slice에는 81만 추가한다.

Boundary refinement는 구현 실험까지 진행했지만 채택하지 않았다. 실제 GPU contour 개선은
Medium 약 `0.003%`, High 약 `0.99%`였고 빠른 Raymarch 대체 측정은 약 `+18~35%` 느렸다.
FXC `/O1` optimized PS도 temps 약 `60→166`, instruction slots `22,436→60,933`, bytecode
`589KB→1.58MB`로 증가했다. 비단조 noise volume의 매 내부 전환을 이분 탐색하면서 pending
구간 상태를 오래 유지하는 방식이 D3D11/FXC hot shader에 맞지 않았기 때문이다. 합성
fixture의 큰 개선값은 실제 GPU 채택 근거로 사용하지 않고 관련 CLI·quality gate도 제거했다.
향후 연구 후보는 conservative occupancy/max-mip 또는 제한적인 final-density quadrature이며,
이번 단계에는 구현하지 않는다.

전체 snapshot은 schema 38, Custom Appearance는 schema 30이다. schema 29 Custom은 lift `0m`,
footprint `0.20`으로 migration한다. Developer UI Zoom은 독립 schema 1이다.

## 자동 검증

- `Stage15PresetMath`: resolver 결정성·단조성·소유권, Weather 점유율, b7 ABI와 Cirrus world anchoring,
  High Full descriptor, Capture canonical descriptor, zero-mean 4 jitter와 Capture 상태.
- `CloudFormationPresetStore`: schema 1 round-trip, 8파일 독립성, 손상/range/domain/원자 replace
  실패 rollback, schema 29/30 migration과 Cirrus texture 생성 규격 불변.
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
  상태 fingerprint, Composite 통계와 raymarch/deep-shadow/Composite shader hash를 JSON/CSV에
  기록한다. Composite hash는 일반/no-rim bytecode pair를 묶는다. `--shader-cache-smoke-test`는
  cold/warm bytecode 동등성과 compile 0회를 확인한다.
- CPU/UI 회귀는 T override 왕복·직접 편집 해제, 진단 중 Quality/Q/T 차단과 Restore,
  같은 formation의 F1/F4 renderer hash, Cirrus texture hash 불변, 일반 1920×1080과
  96/144/192 DPI×다섯 Zoom·단일 패널/overlay 계약을 검사한다.
- 자동 명령은 JSON/CSV만 쓰며 포트폴리오 PNG와 화면 미학 판정은 사용자에게 남긴다.

자동 extent fixture의 기존 Debug smoke에서 물리 1280×720 → Native 1920×1080 → High Full → 일반 창 복원과
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

Stage 15B 최신 preset smoke는 schema 38/15B metadata, Cirrus 내부 밀도, rim 진단,
rollback/idempotency와 D3D11 debug를 포함한 144/144를 통과했다. Debug/Release 전체 CTest는
각각 56/56, Stage 15 화질은 96/96을 통과했다. 2026-08-31 Release 1920×1080 48-case
재측정도 48/48이며 Stage 14 Cloud p95 `6.966272ms`/`0.917465×`, 전체 최악 Cloud p95
`9.224192ms`, Composite/rim 최악 p95 `0.167936ms`로 `0.75ms` 게이트 안이다.
이 수치는 P0~P3 구현 기준이다. 이후 추가한 독립 formation 저장소, Cirrus 제작 UI,
일반 1920×1080 창과 단일 패널 변경은 전용 CPU/renderer/UI smoke까지 추가해 최신 worktree의
Debug/Release 전체 CTest 56/56을 모두 통과했다. 실제 DPI 144 일반 실행은 물리 client
1920×1080, 바깥 창 1942×1136, 작업영역 가로 50.6%, 중앙 오차 0px였다. 포트폴리오 화면 비교와
사용자 승인만 별도 대기다.

## 사용자 승인

아직 미승인이다. [Stage 15 프리셋 사용자 검증 가이드](../STAGE15_PRESET_VALIDATION_GUIDE.md)의
10분 빠른 승인, 상세 판정 카드와 증상별 진단을 수행하고 사용자가 직접 캡처한 뒤 승인일·피드백·
재검증 결과를 기록한다.
