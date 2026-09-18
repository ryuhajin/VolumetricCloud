# Stage 15 — 06 구름 림 개선과 경계 진단

## 문서 범위와 현재 상태

**최신 상태:** 밀도 표현 정합/광학 두께 분리 진단을 실행했다. Detail 정합은 태양 통과를 늘리지만 내부도 함께 밝힌다. 두께 증가는 내부 감쇠와 경계 대비에 영향을 주며, 일반 룩 배수는 아직 채택하지 않았다. 문서 마지막의 후속 결과를 먼저 확인한다.

00~05 기록은 [방향광·내부 명암 기록](stage15-directional-cloud-lighting.md)에 보존한다. 이 문서는 06 림 개선 착수 이후의 구현·진단·사용자 판정을 이어 쓴다. 2026-09-16 사용자 요청으로 아래 기존 기록을 원문 그대로 이전했다. 일반 룩 후보는 미승인이고07은 미착수다.

## 06 림 개선 착수 — 2026-09-15

사용자 승인 계획:06=폐기 기능 정리와 가장자리 조명,07=통합 회귀·성능·최종 촬영. Density shaping/Shadow exponent 등 기존 UI 단위는 유지한다. 05 승인 명암과80/40/3~5도 전환은 고정하고 하부 평탄화는 보류한다.
관찰: 현재 양의 phase가 직접광 림과 Multiple에 함께 전달되어 단순 phase 증폭은 내부 밝기도 바꾼다. Near Detail은 승인 강도0이며, 분석적 Sky/Ground 색/강도는 Physical LUT 경로에서 읽히지 않는다.
수정 전 가설: 폐기 기능 제거는 승인 화면을 바꾸지 않아야 한다. 직접광의 음의 phase 부분과 양의 림을 분리하고 Multiple에는 기존 phase를 전달하면 림 강도/폭만 독립 조절할 수 있다.
고정 조건:Urban Density.70/Base1.50/Shadow1.35/Sky·Ground.85/Multiple.15/Phase.20/Edge2; 시간71/F5/노출0/WB6500; High·Planar·캐시 규격·cone 유지.
계획:06-A Near Detail과 미사용 Environment 필드 제거,06-B Light80B/Environment48B/Shadow160B 및 Rim intensity/depth 1 도입,06-C 상한2.5/4/8→강도1/2/4→깊이.5/1/2 비교. 상한은 테스트 모드 전용이며 기본값2.5 유지. snapshot42,Custom3 유지. 사용자 림 승인 전07 미착수.
자료:06-final의24세트는 '림 이전 승인 화면'이다. 06-rim/approved.exe와approved-shaders에 정리 직전 실행 환경을 보존했다. 00 원본은 변경하지 않는다.

## 06-A/B 실제 변경 — 2026-09-15

### 문제 → 가설 → 변경

림을 밝히려고 기존 Phase intensity를 올리면 같은 P0를 읽는 Multiple까지 함께 바뀌었다. 림을 별도 항으로 분리하면 기존 내부 명암을 고정한 채 비교할 수 있다는 가설로 진행했다. 적용식은 `base=C*(1+min(P0-1,0)*S0)`, `rim=C*gain*max(Prim-1,0)*Srim`, `Direct=base+rim`이다. C는 기존 시선 산란 기여와 태양 입사광, T^shadowExponent의 곱이다. S0는 기존 외곽 가중치, Srim은 그 지수에 depth 배율을 곱한 값이다. Multiple은 기존 P0만 읽고 Silver 진단은 rim을 표시한다.

| 파일/영역 | 실제 변경과 이유 |
|---|---|
| LightParameters / PhaseFunction / CloudLighting / CloudEnvironment | Light80B, 기존 offset0~60 보존, rimIntensity64/rimDepthScale68. 양의 phase 증가분과 기본 직접광을 분리 |
| EnvironmentParameters | Physical LUT에서 읽지 않던 skyColor/skyStrength/groundColor/groundStrength32B 제거. 나머지를48B로 재배치 |
| Shadow 설정 / DeepShadow / NoiseLab / main / tests | Near Detail의 CPU 필드·sanitize·Concept 전달·F3·HLSL 거리 가중치와 추가 Detail fetch·snapshot·CLI·전용 CTest 제거. Near/Far 모두 Base |
| 추가 미사용 항목 | surfaceShadowEnabled를 padding으로 변경. 사용처가 없는 SkyColor/ApplyLdrHighlightShoulder HLSL 함수와 SafeNonNegative CPU 함수 제거. 표면 strength/floor는 실제 사용하므로 보존 |
| 과거 수학 | src/Stage8AmbientMath.h 제거 후 tests/LegacyStage8AmbientMath.h + LegacyEnvironmentParameters.h에서만 과거 분석적 수학 검증. 현재 Environment48B와 구분 |
| NoiseLab / Renderer reflection | F3 Cloud Rim 두 슬라이더와 읽기 전용 cap. snapshot42에 실제 gain/depth/cap. CPU/HLSL 크기·offset 검사 동기화 |
| 과거 후보 조작 | 높이159/79는 테스트 실행기에만 유지. 일반 --shadow-height-2x는 효과 없음. 승인 Base1.50은 일반 UI에서 읽기 전용 |

High step/skip/early exit/cone, Weather, Density.70/Base1.50, 캐시80/40, 3~5도 tau 전환, 노출/WB/Tone은 유지했다. 새 texture·밀도 조회·법선·Bloom·화면 윤곽선은 추가하지 않았다. F4/성능 UI/Weather·Noise preview는 사용 경로가 있어 유지했다. 기본0이라는 이유로 유효 기능을 삭제하지 않았다.

### 데이터와 저장

F3/Concept → Light sanitize → b3(80B) → P0/Prim → base/rim → Direct 및 Silver → 기존 HDR 합성과 Tone 순서다. Environment b4는48B(Physical Sky28/Ground44), Shadow b8은160B(8/152/156 uint padding0). Renderer reflection/CPU offsetof와 공식 ABI 표를 함께 변경했다. Custom Formation은schema3 및 구schema1/2 읽기 유지. 림은 조명 소유이므로 Custom Formation 저장 대상이 아니다. Concept 선택은 기본1/1, Type 전환은 현재 림 값을 유지한다.

### 검증 중 발견한 것과 기각한 시도

원본과 시작값의24조건 비교는 통과했다. 림 변경 시 초기 Sun T 절대 최대 비교는 실패했다. Urban18도의 차이 큰3픽셀 불투명도는 .000488/.000488/.001465였고 평균 T의 작은 분모로 차이가 확대됐다. 캐시 재생성을 빼고 같은 GPU 입력에서 Cloud/Tone만 그리는 검사로 분리했다. Sun T 분기에서 조명 합성을 생략하는 시도는 개선 근거가 없어 최종 코드에 남기지 않았다.

최종 Sun T 검사는 같은 View opacity를 곱한 화면 기여의 가중 평균1e-5/픽셀 최대1e-3 기준이며 원시 정규화 최대도 관찰값으로 기록한다. 입력 전달/수식 불변과 픽셀 bit-exact를 구분한다. 기존 밀도·View T·간접광/승인 이미지 검사는 각각의 기존 허용오차를 유지한다. F4 Direct=base+rim은 L/(1+L) 역변환 뒤 half 양자화 상대 잔차.003 이내 검사로 중복 합산을 확인한다.

별도 수치 probe는 생산 HLSL을 include하고 실제 PS b3를 CS에 전달한다. 처음에는 중첩 include 탐색 실패가 있었고, 기존 Solar probe와 같은 shaders 디렉터리에 테스트 전용 RimLightingProbe를 두어 해결했다. 런타임 manifest에 등록하지 않으며 일반 프레임에서는 dispatch하지 않는다. 미사용 march 함수 파싱용 time 상수는 probe 내부에만 있다.

### 06 사용자 판정과 07 경계

현재 기본은 Rim intensity1 / Rim depth scale1 / 림 cap2.5이며 강한 후보는 미승인이다. 먼저cap2.5→4(→8)를 gain1/depth1로 비교한다. 다음 강도1/2/4, 마지막 depth1/.5/2 순서로 한 값씩 판정한다. 조작 위치·정상/실패 기준은 [06 사용자 체크리스트](../CLOUD_LIGHTING_TUNING_GUIDE.md#06-사용자-비교--가장자리-조명-화면-승인-대기)를 따른다. 자동 통과로 이 항목을 체크하지 않는다.

07은06 시각적 승인 뒤 Urban→세 Type 조명 유지, Urban/Meadow/Snow×F5~F8 전체 회귀, Custom 저장/읽기, Debug/Release 및 Release 전체 CTest, 기존12case 성능,07-final 전후 비교 촬영을 한다. 기존06-final은 림 이전 승인 화면이고00-baseline 원본은 그대로 보존한다. 하부 평탄화는 해결한 것으로 처리하지 않는다. 이번 변경에서 commit/push는 하지 않았다.

## 06 자동 검증 결과와 사용자 검토 상태 — 2026-09-15

- [x] Debug/Release 빌드 성공. Release OFF 림 전용 검사 통과(131.52초), 별도 관련 회귀14/14 통과(119.82초). 관련 범위는 Light/Phase/Environment/Shadow/Stage15/Formation 저장, High/Formation/NoiseLab smoke, hot reload,01 진단,05 조명 비교,3~5도 전환이다.07 전체 CTest 실행과 구분한다.
- [x] 원본24조건(4 Formation×6태양)의 Rim1/1/cap2.5 재현. 최대 정규화 HDR MAE1.03974e-7, 최대오차.000745083, PNG 최대1/255였다. 허용오차 통과이며 bit-exact 주장 아님.
- [x] 생산 HLSL/실제 PS b3→CS,1024표본×3상한×4강도·깊이 조합. CPU/GPU 최대오차5.72205e-6.1/1/cap2.5에서 기존 식의 대수적 재현, gain0/태양0의 림0, Direct=base+rim 및 중복 합산 없음 검사 통과.
- [x] 림 변경 시 Density/View T/Sky/Ground/Multiple 허용오차 불변. Sun T의 최대 화면 기여 가중 평균7.27908e-7, 가중 픽셀오차.000487804. 원시 정규화 최대.00125508은 관찰값으로 남긴다. 투명 경계의 진단값까지 완전 동일하다고 주장하지 않는다.
- [x] Urban/세 Type의32개 저고도 조건(2.5/2.999/3.001/3.5/4.5/4.999/5.001/5.5도), cap8/gain4/depth2에서 HDR finite와 D3D 오류 검사 통과. 전체 연속 카메라/태양 움직임의 시각적 안정성은 사용자 확인 대상이다.
- [x] 대표6case Release 성능: Cloud p95 최대3.51334ms, Frame p95 최대5.83066ms. 최대 후보에서 각10/16.67ms 예산 통과.60예열/120raw,1920×1080,VSync/UI/캡처 Off,직렬.07의 기존12case를 대체하지 않는다.
- [ ] 06 사용자 림 세기/폭/내부 명암 승인. 현 기본1/1/cap2.5 유지. cap4를 아직 내장 기본값으로 채택하지 않는다.
- [ ] 07 전체 프리셋·회귀·성능·최종07-final 촬영.06 승인 후 진행한다.

### 측정 해석

GPU에서 읽은 View opacity와 CPU에서 같은 카메라 레이/HG 식을 재구성해 추정했다(별도1024표본 CPU/GPU 일치 검사 포함). F5/Urban/방위-108.5/고도18에서 가시 opacity로 가중한 phase 상한 초과 비율은 cap2.5에서48.56%,cap4/8에서0%였다. 이는 화면 전체 픽셀 비율이나 밝은 테두리의 폭이 아니다. Phase.20/rawHG16에서 최대 적용값4라 cap8의 추가 이득이 없다.

같은 조건의 림이 기여하는 표본 픽셀(4픽셀 간격)에서 F4 역변환 HDR 림 휘도 평균은 .0435873→.0638000, Composite HDR 평균 .144931→.161201, 최종 sRGB 코드 가중 평균 .451815→.467519였다(cap2.5→4). 마지막 값은 선형 물리 휘도가 아니다. HDR에서 약46% 증가한 림이 최종 화면 전체를 같은 비율로 밝히지는 않는다. 이 조건의 표본에서 RGB 모두250/255 이상인 픽셀 비율은0이었다. 사진처럼 강한 금빛 가장자리를 달성했다는 미학적 판정은 하지 않았다.

Urban 정면 역광(-90/18)에서는 HDR 림 평균 .0239494→.0288936, 최종 코드 평균 .368596→.372823로 변화가 더 작았다. 측광에서는 상한에 걸리지 않아 cap 변경 효과가 거의 없다. 다음 강도/깊이 비교는 이 차이를 본 사용자 판정 후 진행한다.

### 자료와 실패 이력

비교 안내: captures/stage15-directional-lighting/06-rim/README.md. review-v3는15개 Composite PNG+RGBA16F와15개 Silver PNG다. 검증 로그는 release-rim-pass.log/release-related-pass.log, 성능은 performance.csv, 현 소스·실행파일/이미지 SHA256은 validated-source-sha256.json/review-manifest.json/review-image-sha256.json이다. 이미지 이후의 테스트 probe include/미사용 함수 정리 차이를 manifest에 명시했다. 검증 중간 실패와 수정 이유는 앞 절에 기록했다. SDK 탐색 접근 오류로 실패한 빌드는 단독 재실행으로 복구했고 성공한 실행파일로 검사했다.

기존 미커밋 작업과00-baseline을 보존했다.06-final은 림 이전 승인 상태이며 최종 결과가 아니다. 이번06 화면 승인일은 아직 없고 하부 평탄화는 계속 분석 보류다.

06 Debug 최종 검증: 관련11/11 통과(976.94초, RimLighting822.29초 포함). Debug에서도 동일24구도/림 독립성/CPU·GPU/저고도/D3D 검사를 실행했다. 외부06-final 원본과의 비교 및 성능 판정은 Release에서 수행했다. Debug의 긴 소요시간은 이미지 비교 루프를 포함한 검사 전체 시간이며 GPU frame 성능이 아니다. debug-related-pass.log에 결과를 보존했다. git diff --check 통과.06 사용자 화면 판정은 여전히 대기이고07은 미착수다.

## 06-B 얇은 경계 원인 진단 — 2026-09-16

### 관찰 → 가설 → 실제 변경

사용자는 림 배율보다 얇은 경계가 조명에서도 얇게 인식되는지 우선 확인하도록 승인했다. 기존 Base/Rim 분리를 유지하고 일반 룩 변경을 멈췄다. 가설은 (1) 캐시 태양 차폐 오차 (2) View 샘플/skip 손실 (3) View와 Shadow의 밀도 표현 차이 (4) 내부까지 낮은 광학 두께 (5) 조명/합성 이후 대비 손실이다.

기존 `RunSolarBandingDiagnostics`는 과거 캐시와 Phase0/Shadow4를 함께 비교하므로 이번 기준으로 그대로 쓰지 않았다. `RunRimBoundaryDiagnostics`를 기존 림 테스트 실행기에 추가했다. `VCLOUD_RIM_BOUNDARY_DIR`가 없으면 기존 림 검증을 그대로 실행한다. 일반 프리셋/UI/상수버퍼/캐시80·40/3~5도 전환에는 변경이 없다.

| 실제 책임 | 입력 → 출력 / 확인한 계약 |
|---|---|
| Noise.hlsli::SampleCloudDensity | Weather·Base·Detail → 침식 후 shaping 밀도. View는 카메라 거리 fade도 적용 |
| Noise.hlsli::EvaluateLightCloudDensity | 동일 Weather·Base/profile → shaping Base 밀도. Detail 없음 |
| CloudDeepShadow.hlsl::main | Base 밀도*sigma(1/m)*구간(m) → 누적tau. 저장80/40, 적분 최대250m |
| Stage12Shadow.hlsli::Stage12SampleTau | XY 선형 필터 + 인접 높이slice tau 보간 |
| Stage12Shadow.hlsli::SampleStage12DeepShadow | Near/Far T 혼합, Far 바깥T1 fade; tau=-log(T) |
| CloudLighting.hlsli::ComputeLightTransmittance | 캐시 또는 cone.3~5도는 두 경로 tau 혼합 |
| PhaseFunction.hlsli::EvaluateDualLobePhase | dot(viewRay,toSun), raw HG16, P0 cap2.5, 림 별도 cap. ray 안 위치별 윤곽 검출 아님 |
| CloudLighting.hlsli::ComputeDirectInteractionColor | TviewBefore*albedo*Sun*(1-exp(-rho*sigma*ds)). 구간 기여이므로 rho*ds를 다시 곱하면 안 됨 |
| CloudEnvironment.hlsli::EvaluateEnvironmentLighting | 위 구간 기여에 직접광 T^exponent와 Base/Rim 배율. Multiple은 기존P0/tau, Sky/Ground는 별도 입력 |
| VolumetricClouds.hlsl::RaymarchCloud | 구간 기여 합산 뒤 Tview 갱신. Full-resolution, Temporal/업샘플링 없음 |
| Stage14Atmosphere.hlsli::ComposeStage14Atmosphere | cloudAir+cloudAirT*cloudScattering+Tview*(clearBackground-cloudAir), 이후 Tone |

### 비교 설계와 검증 결과

Urban/F5/71초,1920×1080,방위-108.5,고도18/5,60프레임 예열. Density.70/Base1.50/Shadow1.35/Phase.20/Rim1·1·2.5/EV0/WB6500 고정. 태양 방향y와 승인값을 실제 CPU 입력으로 검사한다. N0는 기존, N1은 Base 태양 직접50/25m, N2는 View50/25m(no coarse/early exit), N3는 둘 다50/25/12.5m다. N2는 step과 최적화 정책을 함께 분리한 참조이며 개별 영향 확정은 아니다. 광학 두께·밀도 표현을 바꾼 참조와 섞지 않았다.

Single 진단은 Tview*(1-Tstep)*albedo*Sun*Tsun*기존P0를 별도로 누적한다. 이 P0는 프로젝트 무차원 배율이며 정규화된 절대 산란 phase가 아니다. Rim boost/Srim/간접광/배경을 제외했다. 기존 F4의 L/(1+L)를 역산하지 않고 선형 HDR를 직접 덤프한다.

- 두 ROI의 N0→N3 직접광 상대MAE는18도 .34~.52%,5도 .57~.83%. N3 25→12.5m는 .009~.012%. 개별 최대오차와 혼합 허용치 실패 비율은 JSON에 별도로 보존했다. 평균 수치로 모든 픽셀의 일치를 주장하지 않는다.
- 전체 N0 구름에서 View tau 최대 .8843, opacity 최대 .5872, opacity p95 .3501. 이 구도의 내부도 광학적으로 아주 두껍지 않다. 다른 Type·구도에 일반화하지 않는다.
- 별도 D 실험: 태양 직접 적분만 Detail 침식 후 밀도로 바꾸면 조밀한 ROI 직접광 평균18도 +21.73%,5도 +41.87%. View T 최대차0. 밀도 표현 차이이며 캐시 정밀도 오차가 아니다. 기존 Near Detail(거리4~8km 혼합) 재도입이나 일반 실행 승인 아님.
- 조밀한 ROI18도에서 얇은 그룹의 Direct/Single 평균 .0254/.0277, opacity>=.5 그룹은 .3153/.3783. 고정밀과 단일 산란으로 바꿔도 내부보다 밝은 얇은 띠가 자동 형성되지는 않는다.
- ROI 평균18도 Sky .00140/Ground .000423/Multiple .0101/Direct .1006. 간접광 하나가 림을 지운다는 근거는 부족하다. Tone 이전부터 대비가 약하며 RGB/배경 합성 전체 영향의 정량 판정은 후속이다.
- 균일 구: R1m/중심tau8/직교 역광의 K*tau*exp(-tau)와 실제 GPU 구간 가중치를 비교.8/32/128/4096구간 최대절대오차 .0008767/.0000548/.00000377/.0000242.4096은 float 누적오차로128보다 최대오차가 커진다. 최소오차/단조 수렴을 과장하지 않는다. 해석적 밝은 가장자리 원리는 재현했다.

참조 수렴 관찰 기준은 absError<=.0005+.005*abs(reference), 분모하한1e-4, opacity>.001이며 run03 탐색 분석에서 정해 run04에 적용했다. N0의 일부 픽셀은 이 기준을 넘는다. 이것은 캐시 정확도 전체 승인 기준이 아니라 두 ROI 참조의 충분성을 보는 탐색 기준이다. 구 테스트는 GPU float/CPU double 비교 .0005 허용치다.

### 실패/한계와 사용자 판정

run01은 초기 준비 프레임 때문에18도 기준이 빈 화면이었다. 근거에서 제외하고 보존했다.60예열 및 실제 태양/값 검사로 재실행했다. Python 도표 라이브러리가 없어 PNG 차트 생성은 생략하고 NumPy CSV 프로파일/JSON을 저장했다. 실제 화면은 렌더러가 저장한 PNG를 사용하며 분석으로 보정하지 않았다.

일반 룩 변경 없이 Debug/Release 빌드 성공. Release 관련4/4(Stage6LightMath/Stage7PhaseMath/HighCloudSmoke/RimLighting) 통과139.31초. 경계 전용 Release 실행은 finite/D3D 오류 검사 및 구 수치 검사 통과. 이 테스트의 시간은 진단 전체 비용이며 GPU frame 성능이 아니다. 07 전체 성능 검증을 대신하지 않는다.

현재 최소 수정 방향은 캐시 해상도 증설보다 View/Shadow 밀도 표현 정합과 내부 광학 두께를 각각 분리해 비교하는 것이다. 어떤 일반 실행 변경도 아직 채택하지 않았다. 단일 참조 프레임을 룩 개선 승인으로 바꾸지 않는다. 전체 Type·연속 저고도 이동·개별 광선 표본 추적·RGB 중간톤/합성 분해는 이 결과로 완료 처리하지 않는다. 하부 평탄화는 계속 보류다.

로컬 증거/사용자 안내: `captures/stage15-directional-lighting/06-boundary/README.md`. 기존00-baseline과06-final은 변경하지 않았다.07은 미착수다. 원리 근거는 PBRT의 Transmittance 및 The Equation of Transfer를 확인했으며 제한된 구 해석식과 실제 구름 모델을 구분했다.

06-B 최종 확인: run05-review에 동일 노출의 Single/Direct/Rim RGB PNG를 추가하고 run04 수치 재현을 확인했다. Debug 동일 경계 진단도 통과했다. Release 대비 pack1 최대 차이는 Direct .0001221/Rim .00003052/Single .0001221/View T0. source-manifest.json에 소스·실행파일·이미지 해시, source-worktree.patch에 기존 추적 파일의 미커밋 상태를 보존했다. 미추적 파일은 해시만으로 식별되므로 patch 단독 복원 가능하다고 주장하지 않는다. 실제 화면 확인에서 Single과 Rim 모두 얇은 띠보다 넓은 덩어리가 밝게 나타났다. 사용자 수정 방향 판정은 대기이며 일반 룩 변경/07은 진행하지 않았다.

## 06-B 후속 — 밀도 표현과 광학 두께의 분리 비교 (2026-09-16)

사용자 승인: 문서를06 림 착수부터 분리하고, 이전 확인 항목을 설명한 뒤 View/Shadow 밀도 표현 정합 → 내부 광학 두께를 각각 비교한다.

이전 수치의 정확한 의미: N0/N1은 View와 Base 밀도·조명식을 고정하고 캐시 태양 차폐를 직접 적분으로 교체한 뒤 **픽셀 Direct 선형 휘도**를 비교했다. .3~.8%는 Tsun의 상대 오차가 아니라 Direct의 상대 MAE다. N0/N2는 View 정밀도를 비교했고, 별도 D는 캐시를 모두 제외한 상태에서 Shadow 밀도 표현만 바꿨다. D의22~42%는 평균 Direct 증가율이며 오차 지표가 아니다.

수정 전 가설: Base→Detail 정합은 보이는 파임을 빛 경로에도 열지만, 전체 광학 두께가 낮으면 내부까지 밝아져 림이 안 생길 수 있다. 따라서 두 축을 함께 조정해 결과를 혼동하지 않는다.

비교 설계: Urban/F5/71초/방위-108.5/18·5도/같은조명. View12.5m,태양직접12.5m,no skip/early exit를 공통으로 한다. B는 Shadow Base,D는 Shadow Detail. 먼저 B1/D1을 비교한 뒤 각 표현 안에서 소멸계수1/2/4/8배를 비교한다. View와 태양 양쪽에 같은 배수를 적용한다. 밀도장·Weather·Detail 침식·Density shaping·형상·phase·Rim·노출은 고정한다. 배수는 테스트CB에만 업로드하며 캐시는 참조하지 않고 일반 기본값은 유지한다.

정합 검사: 같은 배수 B/D의 View T·View tau 일치. 두께 검사: tau는 배수에 비례하고 T≈exp(-tau),기존 마스크로 고정한 경계/중간/내부 그룹의 Direct·Rim·Single·SunT를 비교한다. 그룹은 B1의 opacity .001~.1/.1~.5/>=.5로 고정하여 배수마다 마스크를 옮기지 않는다. 광학 두께 변화에 따라 Direct 외 간접광도 의도적으로 바뀌므로 불변을 요구하지 않는다. 상대 MAE와 평균 증가율을 구분하고, 참조 비용을 런타임 비용이라고 부르지 않는다.

### 분리 비교 결과

- **정합(B1→D1):** View12.5m와 태양직접12.5m를 모두 고정했다. 18도 얇은 그룹의 가중 Sun T 평균 .8863→.9629, 내부 .7458→.8571. 5도는 .7910→.9216 / .6742→.8133. Direct 평균은22%/42% 증가했다. 같은 배수 B/D View T 최대차0으로 확인했다. 얇은 경계의 통로는 열리지만 내부도 밝아지므로 이것만으로 림 문제 해결은 아니다.
- **광학 두께(표현 고정):** 기존 밀도장에 같은 sigma 배수를 View/Sun 양쪽 적용했다. 고정 내부 그룹의 View tau 평균은1/2/4/8배에서 .785/1.569/3.138/6.276이다. tau 배수 관계와 T=exp(-tau)가 half 허용치(.005/.001) 안에서 성립했다.

| 고도·Shadow 표현 | sigma 배수 | 얇은 그룹 Direct | 내부 Direct |
|---|---:|---:|---:|
| 18도 Base | 1 / 2 / 4 / 8 | .0254 / .0411 / .0576 / .0653 | .3147 / .2967 / .1697 / .0647 |
| 18도 Detail | 1 / 2 / 4 / 8 | .0296 / .0538 / .0913 / .1381 | .3927 / .4546 / .3833 / .2816 |
| 5도 Base | 1 / 2 / 4 / 8 | .0124 / .0178 / .0216 / .0215 | .1607 / .1269 / .0461 / .00475 |
| 5도 Detail | 1 / 2 / 4 / 8 | .0164 / .0279 / .0432 / .0588 | .2169 / .2308 / .1690 / .1051 |

마스크는 B1의 View opacity로 고정했으며, 얇은 그룹 .001~.1, 내부>=.5다. 값은 Tone 이전 선형 휘도이고 시각적 밝기 비율은 아니다. 원래 얇은 영역도8배에서는 더 두꺼워지므로 현재두께 그룹으로 재분류하지 않았다. 새로 밝아진 픽셀을 새 경계로 선택해 결과를 유리하게 만들지 않는다.

**해석:** 현재 두께에서 단순히 Shadow Detail을 맞추면 전체가 더 밝아지는 영향이 크다. 내부까지 충분히 감쇠되는 조건이 만들어져야 경계 대비가 커진다. 그러나8배 Base의5도 내부는 크게 어두워지고, 같은8배 Detail은 훨씬 밝다. 둘을 함께 조정하거나8배를 바로 기본값으로 채택할 근거는 없다. 표현정합의 영향과 내부감쇠의 영향을 분리해 확인했다는 진단 결과다. 일반 렌더의 Density shaping·extinction·림 값은 그대로다.

자료는 `captures/stage15-directional-lighting/06-density-optical/run01/`의 density-comparison.json, 실제 sigma 텍스트, 선형 HDR, pack4/5/6(Single/Direct/Rim) PNG, analysis/regions.csv와checks.json이다. PNG는 ROI 부분만 유효하고 나머지 화면은 지운 배경이다. reference/진단을 일반 실행 결과라고 부르지 않는다. 최종 형태·림 폭·색·시간 안정성과 전체 Type/성능 승인은 아직 아니다.

검증: Debug/Release 빌드와 양쪽 분리 진단 실행 통과. NumPy 분석에서 동일 배수 B/D의 View T 일치, tau 배수/Beer 관계 통과. Debug/Release 최대차는 Direct .0002442/Rim .00006104/Single .0002442/View T .00006104. 관련 Release3/3(Stage6LightMath/Stage7PhaseMath/HighCloudSmoke) 통과1.22초. 일반 렌더의 셰이딩 변경은 없고 실험은 기존 테스트 실행기 안의 조건부 경로에 한정했다. git diff --check 통과.06 착수 이후 기존 본문은 새 문서로 원문 보존 이전했으며 관련 링크를 갱신했다.

## 06 캐시 유지 후보의 정확도·비용 확인 — 2026-09-16

사용자가 일반 실행이 직접 적분으로 바뀌었는지 확인했다. 일반 실행은 승인80/40 Base 캐시/저고도cone 경로 그대로이며, 기존 변경은 테스트 실행기와 진단 define에 한정됨을 설명했다. 다음 단계는 실제 적용 전 캐시를 유지한 Detail 표현 후보 평가다. 배수는1로 고정한다.

설계: B-cache(기존Base),D-cache(Detail 밀도 적분)와 각각 같은 밀도 표현의12.5m 직접 참조를 비교한다. View는 모두 기존High. 캐시 해상도/높이/250m적분/3~5도전환/조명/형상은 유지한다. 테스트 define만으로 CS와cone 밀도 표현을 함께 맞춘다. 거리4~8km 혼합의 옛 Near Detail 기능은 복구하지 않는다. 일반 UI/기본값에는 반영하지 않는다.

가설: Detail 캐시는 같은 Detail 직접참조에 충분히 가까우면 표현 정합을 캐시 비용으로 구현할 수 있다. 정확도가 나쁘면 단순히Base참조보다 밝다는 이유로 채택하지 않는다. Release18/5도 F5에서60예열120raw,UI/VSync/캡처Off로 Shadow/Cloud/Frame p95를 측정한다. 성능 초과도 숨기지 않고 관찰값으로 보고한다.07전체성능검증과 구분한다.

### 캐시 후보 결과와 판정

실제 변경은 `VCLOUD_TEST_CACHE_DETAIL` 테스트 define으로 Deep Cache CS와 cone의 밀도 조회를 Detail 표현으로 맞춘 것이다. 일반 컴파일에서는 기존 Base 경로를 유지한다. 직접 적분은 비교 참조이며 일반 실행으로 교체하지 않았다.

선택 ROI에서 같은 표현의 직접 참조 대비 Direct 상대 MAE는 Base가18도 .311%/5도 .821%, Detail이 .270%/.694%였다. Detail의 Sun T 절대 MAE는 .00140/.00327이고 View T 차이는0이다. 이는 ROI 평균 오차이며 모든 픽셀의 개선이나 화면 승인은 아니다. Base 캐시 대비 Detail 캐시는 평균 Direct가21.7%/42.1% 증가하여 내부까지 밝아지는 문제가 여전히 남는다.

Release 대표 측정의 Shadow p95는18도 .768→.871ms,5도1.621→2.204ms였다. Frame p95는2.377→2.527ms/3.153→3.535ms다. 두 구도에서 Cloud≤10ms,Frame≤16.67ms를 만족했지만07의12개 case 검증은 아니다. Cloud 시간 감소는 단일 측정의 GPU 상태·컴파일 차이 등과 분리되지 않아 성능 개선으로 주장하지 않는다.

Debug/Release 빌드, 양쪽 캐시 비교 실행, 관련 Release3/3(Stage6LightMath/Stage12ShadowMath/HighCloudSmoke)가 통과했다. 추가한2.5/2.99/3.01/3.5/4.99/5.01/5.5도 finite·D3D 검사 루프는 Debug에서 실행 통과했다. 이 검사는 연속 움직임의 시각적 안정성을 증명하지 않는다.

자료: `captures/stage15-directional-lighting/06-cache-detail/README.md`. 사용자 화면 판정은 대기다. Detail 캐시·광학 두께 배수·림 기본값은 채택하지 않았고07은 미착수다. 다음 판단은 캐시를 유지한 상태에서 내부 감쇠와 경계 밝기의 대비를 비교하는 것이다.
## 06 캐시 고정 광학 두께 비교 — 착수

관찰: 5도 Detail 캐시의 Direct 상대 MAE .694%는18도 .270%보다 크다. 두 조건의 ROI 평균만으로0~10도 전체 오차가 크다고 일반화하지 않는다. 국소 최대오차·줄무늬·움직임 안정성은 별도 판정이다.

고정 계약: 매 검증마다 사용자가 설정을 다시 지정할 필요가 없다. 승인 Urban/F5/시간71초/바람·태양 재생Off/동일 seed·형상·노출·화이트밸런스/80·40/High를 실행기가 재적용한다. 승인 기준과 미채택 후보를 분리하고 오차를 발견하면 먼저 재현·원인 분리를 하며 자동으로 기본값을 바꾸지 않는다.

가설과 변경: 실제 캐시를 사용할 때도 소멸계수 sigma를 높이면 내부 감쇠가 경계보다 커지는지 비교한다. Base와 Detail 캐시를 각각 고정한 독립 계열에서1/2/4배만 바꾼다. 이전8배는 과도한 어두워짐 관찰이 있어 이번 초기 후보에서 제외한다. 밀도장과 구름층의 기하학적 두께는 바꾸지 않는다. tau=적분(density×sigma×거리)인 광학 두께만 View와 Sun 양쪽에 적용한다. 일반 기본값은 그대로다.

검증: 같은 배수 B/D의 View T 일치, HDR finite와 Beer 관계,1배의 이전 캐시 결과 재현을 확인한다. High의 early exit 때문에 tau의 정확한 배수 관계는 고정밀 진단과 달리 요구하지 않는다. B1의 opacity로 고정한 thin/middle/core 그룹에서 Direct·Rim·간접광 및 경계/내부 비율을 비교한다. 전체 Composite를 함께 보존하여 낮아진 내부 밝기만으로 림 개선을 판정하지 않는다.07은 진행하지 않는다.
### 비교 결과

| 고도·캐시 | 소멸계수 배수 | 얇은 그룹 Direct | 내부 Direct | 얇은 그룹/내부 비율 |
|---|---|---|---|---|
|18도 Base|1 / 2 / 4|.0254 / .0411 / .0576|.3153 / .2970 / .1683|.081 / .138 / .342|
|18도 Detail|1 / 2 / 4|.0296 / .0538 / .0911|.3935 / .4557 / .3836|.075 / .118 / .237|
|5도 Base|1 / 2 / 4|.0124 / .0177 / .0213|.1607 / .1268 / .0459|.077 / .140 / .465|
|5도 Detail|1 / 2 / 4|.0163 / .0279 / .0431|.2169 / .2307 / .1690|.075 / .121 / .255|

수치는 같은 B1 opacity 마스크의 Tone 이전 Direct 평균이다. 비율 상승은 상대 대비 변화이며 사진처럼 경계가 내부보다 밝아졌다는 뜻은 아니다. 모든 평균 비율은 아직1보다 작다. 실제 림 항도 경계 평균보다 내부 평균이 여전히 크다. Detail2배는 내부도 밝아지고4배에서 감소한다. Base4배는 특히5도 내부를 강하게 어둡게 만든다. 광학 두께는 유효한 조정 축이지만 단독 해결은 아니다.

실제 Composite에서4배의 일부 작은 구름에 줄 형태 패턴을 관찰했다. 원인은 미확정이며 캐시 오차 또는 View 샘플링이라고 단정하지 않는다. 이전1배에서 개선한 줄무늬의 재발 여부와 새 대비 강화로 드러난 패턴을 분리할 필요가 있다. 이 후보를 기본값으로 채택하지 않는다.

자동 검증: Debug/Release 빌드·동일 비교 실행·finite/D3D 검사 통과. 같은 배수 B/D View T 차이0, Beer 허용치 통과.1배 pack1~3은 이전 캐시 비교 ROI와 최대차0. Debug/Release 최대차 .0004883. 관련 Release3/3 통과1.24초. 자료는 `captures/stage15-directional-lighting/06-cache-optical/`에 보존했다. 일반 실행의80/40 Base 캐시·소멸계수·림 기본값은 변경하지 않았다. 사용자 화면 판정과07은 대기다.
## 06 광학 두께4배 줄 패턴 원인 분리 — 착수

사용자는4배의 뚜렷한 내부 그림자를 선호한다고 밝혔다. 이를 우선 후보로 기록하되 일반 기본값 채택과 구분한다. Urban/F5/시간71초/기존 조명·형상/80·40 Base 캐시를 유지하고 sigma4배(.00144/m)를 고정한다. 기존4배 화면 오른쪽 작은 구름의 줄을 포함한 ROI [1536,624,1696,720]에서 캐시→태양 직접12.5m, High View→고정25/12.5m(no skip/exit), 둘 다 변경을 분리한다. 캐시 비교 시 동일Base 밀도를 유지한다. Direct RGB·View T·Sun T를 함께 비교하여 시선 샘플링과 태양 차폐를 혼동하지 않는다. 이번 단계는 진단이며 일반 High 상수·기본값은 바꾸지 않는다.
### 원인 분리 결과

5도 문제 ROI에서 View/Sun12.5m 참조 대비 Direct 상대 MAE는 기존4.873%,Sun만 직접 .289%,View만12.5m4.880%다. View25→12.5m 양쪽직접 참조 차이는 .0189%다. 실제 선형Direct 진단 영상에서도 Sun 직접일 때 줄이 크게 줄고 View 변경만으로는 유지됐다. 이번 위치의 주요 원인은 View 표본이 아니라 캐시 차폐다. 기존 .69%와는 ROI·sigma·밀도모델·참조 조건이 달라 단순 비교하지 않는다.

추가 분리: 기존/빛 적분8분할/높이159·79 진단의 Direct 상대 MAE는4.873/4.878/2.336%다. 행 평균 Direct의2차차분 절댓값 평균은 .000949/.000930/.000199로, 높이 표본을 늘렸을 때 수평 잔물결이 크게 감소했다. 직접 참조는 .000187이다. 이 지표에는 실제 형상 변화도 들어 있으므로 절대적인 밴딩 합격 기준이 아니다. Far 강제 조회도 기존과 유사한 줄을 보였고 Near 강제조회는 크게 밝아졌다. Near 범위 밖 clamp도 포함될 수 있어 Near 강제 결과를 채택 후보로 보지 않는다.

결론: 이5도 ROI의 수평 줄은 주로 Far 캐시의 높이 표본/보간 표현에 민감하다. 낮은 태양에서 같은 높이 간격은 광선 방향으로 Δh/sin(고도)의 긴 구간이 된다. slice 사이 선형 tau 보간 오차가 sigma4배에서 강조된다는 해석이 결과와 부합한다. 적분만 세분화하는 방법은 이번 줄을 해결하지 못했다. XY 투영·필터의 잔여 영향까지 배제하지 않으며, 높이2배도 직접 참조와 완전히 일치하지 않는다. 모든 각도/구름 줄 현상의 단일 원인이라고 일반화하지 않는다.

다음 수정 후보는80/40을 유지하는 높이 보간 개선이다. 구간 사이 기울기 꺾임을 줄이되 tau 범위·단조성을 보존하고, 기존 선형조회와 같은 직접 참조로 비교해야 한다.159/79는 원인 확인용이며 기본값으로 채택하지 않았다.4배는 사용자 선호 후보로 보존하고 일반 sigma는1배다.

검증: Debug/Release 빌드 통과. Release View/Sun 분리와 캐시 분리,Debug 캐시 분리 finite/D3D 통과. 캐시 분리 Debug/Release ROI pack1/2/3/5 최대차0. 관련 Release3/3 통과1.29초. 마지막에는 이번Base진단과 무관한 Detail 저고도 루프가 실행되지 않도록 테스트 분기를 정리하고 양쪽을 다시 빌드했다. 일반 셰이더·캐시 규격·UI 변경 없음. 전체 프리셋/연속 움직임/07 판정은 아니다. 자료: `captures/stage15-directional-lighting/06-stripe4/`.
## 06 높이 보간 개선 후보 — 착수

사용자 승인:80/40 높이 보간을 먼저 개선하고 부족하면 캐시 높이 증설을 검토한다. sigma4배를 유지한다. 기존 두 높이 선형 보간 대신 네 높이의 단조 Hermite를 테스트 define에서 구현한다. 이웃 기울기가 같은 부호일 때 조화평균을 사용하고 극값/평탄 구간에서는 기울기를0으로 둔다. 양 끝은 한쪽 차분, 결과는 양 끝 tau 범위로 제한한다. 생성 규격과 밀도/조명은 고정하며 조회는 cascade당2→4회가 된다. 기록된 같은12.5m 참조로 밝기오차와 줄 패턴을 비교한다. 일반 적용·07은 사용자 화면 판정 전 진행하지 않는다.
### 보간 후보 판정 및 캐시 증설 검토

5도 ROI 직접광 참조 상대 MAE: 선형80/40 4.873%,단조Hermite80/40 3.687%,선형159/79 2.336%. 행 평균2차차분 지표는 .000949/.000863/.000199로 보간만으로는 줄 감소가 작았다. 실제 진단 그림에도Hermite 줄이 뚜렷하게 남아 일반 채택하지 않는다.18도 오차는1.828/1.798/1.797%다. 단조 보간은 없는 높이 정보를 복원하지 못하며 이번 조건에서 저장 표본 증설이 훨씬 효과적이었다.

사용자 지시에 따라 높이2배 후보의 전체Composite와 대표 성능도 비교했다. XY512 고정,Near/Far 높이80/40→159/79,두R32 배열 메모리120→238MiB(+118MiB). 18도 Frame p95 1.816→2.009ms,5도2.471→2.734ms. Shadow p95는 .410→.527ms/.996→1.632ms다. Release OFF/1920×1080/60예열120raw/UI·VSync·캡처Off 직렬의 대표2조건이며07전체성능 판정은 아니다. 단일 실행 측정 변동을 포함한다.

현재 판단:Hermite 후보는 기각하고 테스트define에서만 보존한다.159/79는 유력한 다음 후보이나2.34% 잔여오차와미검증 각도·Type·시간안정성이 있어 아직 일반 기본값으로 적용하지 않는다. 필요하면Far만 늘린80/79를 먼저 비교해Near메모리 증설 필요성을 분리할 수 있다. 이번 단계에서는159/79의효과·비용까지만 확인했다. sigma4배는 사용자선호 후보로 유지한다.

검증:Debug/Release 빌드와보간진단 finite/D3D 통과. Debug/Release 및대표성능실행재현 ROI최대차0. 관련Release2/2(Stage12ShadowMath/HighCloudSmoke)1.19초 통과. 마지막변경은전체Composite/성능저장분기이며양쪽재빌드와Release실행확인. 일반 경로는80/40 선형보간·기존sigma 그대로다. 자료run03-hermite/debug-hermite/run04-height-review, hermite-checks.json.
## 06 Far만 증설 비교 — 착수

사용자 요청으로sigma4배/동일Urban/F5/18·5도에서80/40·80/79·159/79를 비교한다. 모두기존선형보간,XY512,Base그림자다. Far79 전용테스트설정은Near80을유지하며자원배열크기와GPU상수slice수를함께검사한다. 일반실행의설정은변경하지않는다. 문제ROI직접참조오차·수평줄지표·전체화면·대표60예열120raw성능을비교한다. 캐시배열메모리는120/159/238MiB이며80/79는양쪽증설보다79MiB를절약한다. 결과를전체Type/각도승인으로확대하지않는다.
### Far79 결과

|항목|80/40|80/79|159/79|
|---|---:|---:|---:|
|5도ROI Direct 상대MAE|4.873%|2.357%|2.336%|
|5도 행2차차분 지표|.000949|.0001994|.0001990|
|18도ROI Direct 상대MAE|1.828%|1.797%|1.797%|
|R32 캐시배열MiB|120|159|238|

80/79는문제ROI에서159/79의줄감소를거의유지한다. 전체Composite의80/79↔159/79 차이는18도8비트평균 .000200/최대1,5도평균 .008164/최대2다. 전체평균은하늘등빈영역을포함하므로국소HDR정확도대체가아니다.80/79는양쪽증설보다79MiB절약한다.

Release대표성능은60예열120raw,UI/VSync/캡처Off직렬2회였다.80/79 Frame p95는18도2.706/1.882ms,5도2.570/3.034ms로변동했고159/79는18도2.028/1.954ms,5도2.675/2.700ms다. 모든대표측정은목표이내지만속도우위는확정하지않는다. 첫18도Cloud스파이크때문에재측정했으며두실행모두보존했다.07전체12case검증아님.

자동검증:Debug/Release빌드·실행finite/D3D통과. 실제textureArray와shadow상수의80/40·80/79·159/79일치검사통과. Debug/Release및반복ROI pack최대차0. 관련Release2/2통과1.19초. Shader/CB ABI는변경없으며CPU검증설정만Far전용분기를추가했다. 실패시자원생성설정복원과일반false기본값을유지한다.

판정:80/79를우선증설후보로추천한다. 이번Far지배영역에서는Near159의추가효과가매우작다. Near/Far이동·근거리구름·세Type·연속저고도검증과사용자판정은남아있다. 일반80/40·sigma1배기본값은아직변경하지않았다. 자료run05-far79/debug-far79/run06-far79-repeat,far79-checks.json.
## 06 Far79 후속 안정성 확인 — 착수

80/79·sigma4 후보를159/79·sigma4와비교한다. Urban및Urban조명세Type,F5/F7에서태양2.5→5.5→2.5도를0.1도간격으로검사한다. F7/고도5에서는태양right축으로카메라를0~12km(200m간격)이동한다. 고정월드anchor의Nearweight가1→0을실제로통과하는지CPU투영계산으로확인한다. 이는anchor영역범위검사이며모든가시구름의기여검사는아니다.60예열후각프레임HDRfinite·D3D·slice수·Type조명유지를확인하고연속LDR변화량을기록한다. 변화량은카메라/태양이동의정상변화도포함하므로자동화질합격기준으로사용하지않는다. 단계중간선택화면만보존하며일반기본값은유지한다.
### 안정성 확인 결과

Debug/Release 각각1464프레임(4formation×2규격×3경로×61) HDRfinite·D3D·slice 계약을통과했다. Type전환의Density.70/Shadow/Rim유지를확인했다. 이동경로의고정anchor Nearweight는각case1→0을통과했다. 이수치통과는모든가시구름에줄이없다는판정이아니다.

근거리F7의Urban 구름에서80/79의약한수평줄이159/79보다더눈에띄었다. 같은5.5도선택화면의전체LDR평균/최대차는Urban .0112/5,Stratus .0155/2,Cumulus .0119/3,Mixed .7247/4(8비트값)다. 값이작아도일부줄은시각적으로보이며전체평균에는하늘도포함된다. 따라서Far지배F5에서충분했던80/79를모든근거리구도에충분하다고확정하지않는다.

Mixed/F7 태양5.4↔5.5도는두규격모두평균LDR변화약6.44/255로크다. Near80→159로이를해결하지못한다. 각도를4프레임유지한Release추가검사122표본에서도인접각도변화는유지됐지만,5.2/5.3/5.4도의왕복전후PNG최대차는두규격모두0이다. 불규칙깜빡임/히스테리시스로확인된것이아니라각도에민감한밝기변화다. 한프레임씩각도를바꾸는실행의지표는갱신지연을포함할수있어고정각도진단과구분한다.

전체Debug/Release LDR변화량지표최대차 .000641. 관련Release3/3(Stage12ShadowMath/Stage15PresetMath/HighCloudSmoke)1.26초통과. Debug실행중재빌드1회는실행파일잠금LNK1168로실패했으며실행종료후재빌드성공했다. 마지막변경은Mixed선별/각도유지테스트분기이며최신Debug/Release빌드와Release실행을확인했다.

현재판정:80/79·sigma4는미승인후보다. 원거리효과·메모리이점은있지만근거리잔여줄의사용자판정이필요하다.159/79도전체무줄무늬/최종승인상태는아니다. 단일F7이동경로및0.1도간격검사이므로모든카메라·각도의연속안정성을증명하지않는다. 일반기본값80/40·sigma1유지,07미착수. 자료captures/stage15-directional-lighting/06-far79-stability/.
## 06 캐시 규격 사용자 승인 — 2026-09-16

사용자 판정: 렌더 캡처에서80/79와159/79의차이가눈에띄지않으므로80/79를일반기본으로선택하고다음단계로진행한다. 근거리잔여줄관찰은기록에남기되사용자의품질선택을우선한다. Near80/Far79·XY512·선형tau보간을일반자원생성/sanitize/진단범위에반영한다. R32캐시배열159MiB,기존대비+39MiB/양쪽증설대비-79MiB다. b8=160B와offset·3~5도전환·High상수·Base그림자는유지한다. Hermite는미채택이다.

다음06작업은선호한sigma4배에서림강도·폭을비교하는것이다. 이번명시적일반기본값승인은캐시80/79이며sigma4배는림비교의고정후보로기록한다. 기존일반소멸계수를이번캐시변경에묶어바꾸지않는다.07은림화면승인뒤진행한다. 기존captures원본은불변이며이후B-cache테스트이름은현재승인80/79를가리킨다.
### 승인 캐시에서 림 비교 착수

80/79 일반반영뒤 `VCLOUD_RIM_APPROVED_CACHE_REVIEW=1` 테스트로sigma4,림depth1,기존Phase.20/Shadow1.35를고정한다. cap2.5/4/8은강도1에서비교하고,강도1/2/4는cap2.5에서비교한다. 변경축을섞지않는다. Urban/F5/71초/18·5도에서HDR림/Direct,ViewT/SunT/간접광불변성과Composite를확인한다. 후보값은일반기본으로자동채택하지않는다.
### 반영 검증과 첫 림 비교 결과

일반 80/79 + 테스트 sigma4 + Rim1 화면은 이전 run05-far79의 승인 비교 PNG와 18/5도 모두 최대차0으로 재현됐다. Debug/Release 빌드와 림 비교 실행의 HDR finite/D3D 검사가 통과했다. 관련 Release Stage12ShadowMath·Stage15PresetMath·HighCloudSmoke 3개가 통과했다. Far debug index78와 배열159MiB를 CPU 회귀 검사에 반영했다. Debug/Release ROI 채널 최대차 .0002442.

조밀한 ROI의 cap2.5/4/8 출력은 동일했다. 이 영역에서는 림 상한이 제한 요인이 아니므로 상한을 올릴 이유가 없다. 이를 모든 역광 구도의 결론으로 확대하지 않는다. Rim intensity1/2/4에서 림 항은 각각 1/2/4배였고 Direct 증가량은 림 증가량과 half 허용치 내 일치했다. View T·Sun T·Sky·Ground·Multiple은 정확히 유지됐다.

18도 ROI Direct 평균은 .1123/.1272/.1571, 5도는 .03454/.03916/.04841이다. 강도4 전체 화면은 얇은 외곽뿐 아니라 넓은 밝은 영역도 강조했다. 아직 목표 사진의 얇은 빛 테두리가 완성됐다고 판정하지 않는다. cap2.5를 유지한 강도 후보 화면을 사용자에게 제공하며, 다음 비교에서는 선택한 강도를 고정하고 Rim depth scale로 범위를 좁히는 효과를 확인한다. 아직 강도 후보 채택·07 진행은 하지 않았다.

자료: captures/stage15-directional-lighting/06-rim-approved-cache/. 소멸계수4배는 테스트 조건이며 일반 기본값에는 이번 변경에서 포함하지 않았다.
## 06 Rim intensity 사용자 승인

사용자는1~2가적절하고4는전체가밝아진다고판정해일반기본값2를승인했다. LightParameters 기본값과비정상입력복원을2로변경한다. 공통조명초기값을사용하는Concept도2를적용하고Type전환은기존조명을보존한다. Rim depth1/cap2.5/80·79는유지한다. LightCB80B·offset64/68·snapshot42는변경없다. 일반소멸계수는유지하며sigma4는지금까지림비교조건이다.

현재목표는1번얇고밝은가장자리의'밝기' 조정이다. 내부광학두께진단은3번어두운내부유지를위한선행작업이었다.1번의'얇음'과2번중간톤연결은아직완료되지않았고4번따뜻한직접광/차가운주변광은아직별도조정하지않았다.

림분리는기존HG방향함수의양의증가분을별도강도·깊이·상한으로제어하는분리다. g/phaseIntensity는공유하며독립된두번째HG함수는아니다. Multiple은기존P0를사용하므로Rim intensity를올려도Multiple 자체는변하지않는다. 외곽가중치는화면실루엣이아니라Sun T 기반이다. 빛을잘받는넓은영역도조건을만족하면림항이증가하므로강도4에서전체가밝아보일수있다.

방향검증은태양을향하는역광에서전방산란/밝은경계,측광에서밝은면→중간톤→그림자,태양을등지는순광에서불필요한발광이없는지를확인한다. 순광을강한실버라이닝의주검증구도로사용하지않는다. 다음작업은승인강도2와비교sigma4를고정하고이구도들에서림범위를검증하는것이다.
반영 검증: Debug/Release 빌드 성공. Release Stage6LightMath·Stage7PhaseMath·Stage15PresetMath·HighCloudSmoke 4/4 통과(7.11초). CPU 비정상 림 입력의 승인 기본 복원값2를 확인했고 GPU smoke에서 활성 HLSL 실행을 확인했다. 화면 강도2 승인은 사용자 피드백으로 기록하며, 얇은 폭/중간톤/주변광 색의 최종 승인과는 구분한다.

## 06 림 폭·중간톤 비교 — 착수

사용자 요청으로 강도2/sigma4/80·79/cap2.5를 고정하고 Rim depth .5/1/2를 비교한다. F5 시선의 수평 방위는-90도이므로 태양 방위-90(역광),0(측광),90(순광),고도18·5도로 실제 방향을 고정한다. 각 구도는 카메라·구름이 같고 태양만 다르다. depth 증가가 Sun T 기반 림 범위를 좁히는지, 내부/중간톤과 대기색을 함께 확인한다. View/Sun T·Sky/Ground/Multiple 불변, Direct 차이=림 차이 검사. 순광은 강한 실버라이닝을 기대하는 구도가 아니라 불필요한 테두리가 없는지 확인한다. depth 기본값1은 승인 전 유지한다.
### 림 폭 비교 결과 — 2026-09-16

테스트 실행기에 역광/측광/순광 × 고도18/5 × depth .5/1/2의 18조건을 추가했다. 일반 렌더 수식과 기본 depth1은 변경하지 않았다. 강도2, 비교 sigma4, 캐시80/79, 상한2.5를 고정했다. 조건별 HDR 성분과 전체 Composite를 저장했다. `depth-contract.json`이 이 모드의 실제 조건이며 공통 `contract.json`의 초기 조건보다 우선한다. 후보명이 없는 Composite는 sigma1 초기 화면이므로 비교에 사용하지 않는다.

| 역광 ROI | depth .5 | depth1 | depth2 |
|---|---:|---:|---:|
| 고도18 Direct 평균 | .12151 | .10202 | .08388 |
| 고도18 Rim 평균 | .07698 | .05750 | .03935 |
| 고도5 Direct 평균 | .03508 | .02894 | .02379 |
| 고도5 Rim 평균 | .02219 | .01605 | .01090 |

depth1→2에서 18도 얇은 View 불투명도 구간의 림 평균은 약20%, 두꺼운 구간은 약34% 감소했다. 5도에서는 각각 약25%/33% 감소했다. 내부의 넓은 밝기를 더 억제하지만 경계 자체도 어두워지는 절충이다. 이 분류는 View 광로의 불투명도이며 실제 물리적 실루엣 두께를 직접 측정한 것이 아니다. Composite 관찰에서도 넓은 밝음 감소는 보이지만 사진 같은 가는 금빛 테두리가 완성됐다고 판정하지 않는다. Sun T 기반 가중치 하나로 화면상의 가장자리 폭을 독립 제어할 수 있다는 증거도 아니다.

자동 검증: Debug/Release 빌드 및 18조건 실행 성공. 모든 후보 HDR finite/D3D 검사 통과. View T·Sun T·Sky·Ground·Multiple은 depth 변경 전후 정확히 같고, Direct 변화와 림 변화의 잔차 최대 .000733으로 half 허용치 .001 이내다. 림 기여는 depth 증가에 대해 단조 감소했다. 측광/순광의 검사 ROI 림 항은0이었다. Debug/Release ROI 최대차 .000489. Release Stage6LightMath/Stage7PhaseMath/HighCloudSmoke 3/3 통과(1.21초). 성능 CSV는 진단 실행의 참고값이며 07 전체 성능 검증을 대체하지 않는다.

사용자 판정 대기: depth 기본1 유지, sigma4는 계속 테스트 조건이다. 강도2 승인은 유지한다. 07 미착수.

- [ ] 역광5/18도 비교 PNG에서 같은 구름을 비교한다. depth2의 밝은 면 축소가 중간톤을 살리는지, 필요한 금빛 가장자리까지 꺼뜨리는지 판정한다.
- [ ] depth .5에서는 빛과 그림자의 연결이 자연스러운지, 넓은 내부가 과하게 밝아지는지 확인한다.
- [ ] 측광/순광 PNG는 불필요한 발광 테두리가 없는지 확인한다. 강한 실버 라이닝이 없어도 실패가 아니다.
- [ ] 실제 F3 Cloud Rim의 Rim depth scale은 .5/1/2를 뜻하며 2일수록 Sun T 기반 가중치를 더 억제한다. 일반 실행은 sigma1이므로 이번 sigma4 PNG와 동일 화면이라고 간주하지 않는다.

분석 실행기: `tests/AnalyzeRimDepth.py`. 자료: `captures/stage15-directional-lighting/06-rim-depth/README.md` 및 release/debug의 depth-analysis.json.

### Rim depth 사용자 판정 — 2026-09-16

사용자는 Rim depth를1로 유지하기로 확정했다. Depth를 높이는 것만으로 사진 같은 얇고 밝은 테두리가 완성되지 않는다는 비교 결론에 동의했다. .5/2 후보는 기본값으로 채택하지 않는다. 현재 코드의 기본값/비정상 입력 복원값이 이미1이므로 렌더 코드 변경은 없다. Rim intensity2, 캐시80/79를 유지한다. 광학 두께4배는 기존 비교 조건이며 이번 판정으로 일반 기본값에 추가 반영하지 않는다.

- [x] Rim depth 기본1 유지: 사용자 명시 승인.
- [x] Depth 단독 조정으로 얇고 밝은 경계를 완성하지 못했다는 한계 기록.

개별 구도 체크리스트 전체 통과나 06 최종 화면 승인으로 확대 해석하지 않는다. 다음 검토 대상은 Sun T 기반 가중치가 빛을 잘 받는 넓은 면과 얇은 경계를 구분하는 방식이다. 구체적인 추가 알고리즘은 아직 채택하지 않았다. 07 미착수.

## 06 넓은 밝은 면과 얇은 경계 — 광로 후보 진단 착수

관찰: 승인 Depth1에서 태양 노출 가중치는 SunT만 사용한다. ViewT는 적분 공통 감쇠에 이미 있지만 림 가중치 자체에는 없다. 동일 SunT를 가진 두 표본의 rimPhase는 View 광로와 관계없이 같다. HG는 방향이며 형상 검출기가 아니다.
가설: 추가 Sun 감쇠를 제거하면 넓게 밝아질 수 있다. 기존 지수의 입력을 midpoint ViewT×SunT로 바꾸면 꺾인 전체 광로가 긴 림을 억제할 수 있지만 얇은 경계도 어두워질 수 있다. 후자는 물리적 산란 법칙이 아니라 제어용 추가 감쇠 실험이며, 전체 View chord나 국소 표면 두께는 아니다.
변경: VCLOUD_TEST_RIM_PATH=1(추가 림 가중치 없음),2(양쪽 광로 가중치)를 테스트 컴파일에서만 사용한다. 기본 경로는 그대로다. View midpoint 보정은 exp(-density*sigma*ds/2). 강도2/Depth1/상한2.5/sigma4/80·79 고정, Urban/F5/71초, 역광·측광·순광 각18/5도. 기본 직접광·환경광·Multiple은 변경하지 않는다. 저장 HDR 성분 불변성과 Direct 차이=Rim 차이를 검사하고 전체 화면을 비교한다. 07 미진행.

### 광로 후보 샘플 결과 — 2026-09-16

Release/Debug 각각 Urban/F5/71초, 역광·측광·순광×고도18/5×기존/추가감쇠없음/양쪽광로의18조건을 실행했다. 현재 기본 화면과 기존 depth1 캡처의 PNG 최대차는18도1/255,5도0이었다. 화면은 실제 렌더 결과이며 후처리로 림을 그려 넣지 않았다.

| 역광 조건 | 기존 Rim 평균 | 추가 감쇠 없음 | 양쪽 광로 |
|---|---:|---:|---:|
| 18도 전체 ROI | .057496 | .117015 | .038228 |
| 5도 전체 ROI | .016047 | .037361 | .010754 |
| 18도 얇은 구간 | .032871 | .044502 | .031238 |
| 18도 두꺼운 구간 | .162535 | .382843 | .091952 |
| 5도 얇은 구간 | .014251 | .021502 | .013590 |
| 5도 두꺼운 구간 | .037433 | .107142 | .019897 |

얇은/두꺼운 구간은 고정 View 불투명도 .001~.1 / .5 이상이며 실제 실루엣 두께를 뜻하지 않는다. 양쪽 광로 후보는 얇은 구간 림95.0~95.4%를 보존하고 두꺼운 구간은53.2~56.6%로 줄였다. 이전 Depth2의 얇은 구간75~80% 보존보다 선택적이다. 추가감쇠 제거는 두꺼운 구간이2.36~2.86배 밝아져 목표와 반대다. 샘플 단계에서는 채택을 권하지 않는다.

관찰/제안: 양쪽 광로가 다음 검토 후보로 더 적합하지만, 실제 화면은 아직 넓은 밝은 면과 부드러운 경계를 갖는다. 날카로운 금빛 실루엣 완성으로 판정하지 않는다. 후보는 새 광선이나 밀도 조회 없이 기존 ViewT·SunT·density·ds를 사용한다. 이후 이 후보의 화면을 비교하고, 유효하면 세 Type과 카메라/저고도 변화에서 같은 대비를 유지하는지 확인한다. 화면 밝기를 올리기 전에 이 선택성을 먼저 판정한다. 필요하면 추가 감쇠의 혼합 비율을 따로 비교하되 이번에는 새 UI나 승인 기본값으로 반영하지 않는다. 밀도 기울기/전체 chord 조회는 이 값만으로 부족한 경우 비용을 분리해 검토할 후속안이다.

자동 검증: 빌드 둘 다 성공. 초기 샌드박스 SDK 접근 오류는 승인된 빌드 재실행으로 해결했다. GPU finite/D3D 검사 통과. 모든 후보에서 ViewT/SunT/Sky/Ground/Multiple이 정확히 유지됐다. Direct 변화−림 변화 최대잔차 .000733. 두 빌드 ROI 최대차 .000977. 같은 구도에서 추가감쇠없음≥기존≥양쪽광로의 림 기여 관계 확인. 측광/순광 ROI의 림은 모두0. Release 관련3개 CTest 통과(1.31초). 테스트 macro 없는 High smoke도 통과했다. 성능 파일은 진단 참고값이며 정식07 성능/다른Type/카메라 안정성 검증은 이번에 수행하지 않았다.

자료는 captures/stage15-directional-lighting/06-rim-path/README.md. path-contract.json이 실제 후보 계약이며 depth-contract.json의 후보 목록이나 초기 actual.txt보다 우선한다. 후보명 없는 초기 Composite는 sigma1이므로 비교에 사용하지 않는다.

사용자 확인 (미승인):
- [ ] 비교 자료의 역광18도 기존/양쪽광로에서 같은 중앙·오른쪽 덩어리를 본다. 얇은 밝음은 남고 넓은 면 밝기만 줄어드는지, 내부가 검게 뭉치지 않는지 판정한다.
- [ ] 역광5도에서 금빛이 단순히 약해진 것인지, 경계와 내부의 대비가 좋아진 것인지 판정한다. 밝은 픽셀 수 감소 자체를 성공 기준으로 삼지 않는다.
- [ ] 측광/순광 후보에서 잘못된 발광이 생기지 않는지 본다. 해당 ROI 림0은 자동 확인됐지만 전체 화면 판정은 별도다.

일반 실행은 Rim2/Depth1과 기존 SunT 가중치 그대로다. sigma4도 테스트 조건이다. 새 후보는 VCLOUD_RIM_PATH_REVIEW 테스트만 사용하며 F3에서 선택할 수 없다. 사용자 판정과 후속 안정성 확인 전 일반 반영/07 진행 없음.

## 06 LUT 하늘광의 태양 차폐 분리 — 착수

사용자는 양쪽 광로 림 후보를 채택하지 않고 기존 림을 유지하도록 지시했다. 이번에는 따뜻한 직접광/상대적으로 차가운 주변광을 다룬다. 기존 SkyIrradiance LUT는 32개 상반구 방향을 cosine 가중 적분한 RGB다. 구름은 대표 고도에서 이를 조회하고 fill/높이/AO뿐 아니라 태양 방향 차폐까지 곱했다. 이로 인해 태양이 가려진 내부의 하늘광도 함께 억제된다.

가설/변경: 하늘광의 가시성을 local AO만으로 바꾸고 높이 가중치는 유지한다. 지면광의 기존 가시성, Direct/Rim/Multiple, LUT 생성/색, 노출/WB는 그대로 둔다. 이는 하늘 전체의 실제 가시성을 계산하는 물리 정답이 아니라 단일 태양 방향을 상반구 차폐로 사용하던 근사의 분리다. 구름 주변의 실제 가림은 추가 조사 없이 복원하지 못한다. 하늘광에 고정 파란색을 곱하지 않는다. LUT가 따뜻하면 변경 후에도 따뜻할 수 있다.

테스트: 일반 sigma1, Urban/F5/71초, 방위-90/0/90×고도18/5, 기존/수정12조건. VCLOUD_TEST_LEGACY_SKY_OCCLUSION은 이전 계산을 테스트에서만 재현한다. ViewT/SunT/Direct/Rim/Ground/Multiple 불변, Sky RGB 및 최종 화면 변화를 검사한다. Rim2/Depth1/80·79 유지. UI/상수버퍼 ABI 변경 없음. 화면 최종 승인/07 미진행.

### LUT 색 분리 구현과 전후 수식

공통 구간 가중치 W=Tview×albedo×(1-exp(-density×sigma×ds)), 국소 AO A=exp(-density×AOstrength), 태양 연결 D=lerp(1,Tsun^ambientExponent,coupling)로 적는다. 높이 H는 기존 skyWeight다.

수정 전: Sky=W×SkyIrradianceLUT×SkyFill×H×A×D.
수정 후: Sky=W×SkyIrradianceLUT×SkyFill×H×A.

Ground는 기존 W×groundIncident×(1-height)×A×D를 유지한다. groundIncident=groundAlbedo×(대기를 통과한 지면 태양광×cos+지면 SkyIrradiance)/pi×bounce×GroundFill이다. Direct/Rim은 기존 대기를 통과한 태양 RGB×구름 차폐×phase 계산이다. Multiple도 기존 태양 RGB×tau 재사용 산란 근사다. 최종 구름 산란은 Direct+Sky+Ground+Multiple이고 림은 Direct 안에 한 번만 들어간다. 이 합은 기존 Aerial/Tone/WB 경로로 전달된다.

LUT의 생성·해상도·고도 조회·색은 변경하지 않았다. 추가 LUT 조회/광선/상수버퍼는 없다. SkyIrradiance는 상반구 cosine 적분이므로 완전한 구름 다방향 산란 해가 아니며, 높이/AO도 근사다. 이번 변경은 태양 한 방향으로 하늘 전체의 가시성을 판단하던 연결만 제거한다. F4 Ambient Visibility는 실제 남은 의미에 맞게 Ground Ambient Visibility로 표시하며 sky 가시성이 아니라는 설명을 추가했다. enum/저장 값은 유지했다. CPU/HLSL의 ambientShadowCoupling/Exponent 주석도 지면 반사광 대상으로 갱신했다.

Release 샘플 관찰: 역광18도 가시 ROI Sky RGB는 (.001058,.002239,.005339)→(.001130,.002392,.005703), 밝기는 약6.8% 증가. 역광5도는 (.000792,.001340,.002774)→(.000895,.001514,.003134), 약13.0% 증가. 태양 Direct RGB는 각각 (.46088,.36296,.25515),(.25578,.13792,.05510)이며 변경 전후 동일했다. LUT 하늘광은 이미 B>R이고 직접광은 R>B다. 즉 색의 방향은 이미 맞고 에너지 비율이 병목이다. 5도 Multiple 밝기 .01115에 비해 수정 후 Sky는 .00150으로 아직 작다. 현재 sigma1 장면에서는 이 변경만으로 강한 따뜻함/차가움 대비가 생겼다고 볼 수 없다.

다음 튜닝은 LUT 색 변경보다 기존 F3 Sky fill과 Multiple attenuation의 상대 기여를 독립 비교하는 것이 적합하다. 이번 작업에서 해당 승인값 .85/.15는 유지했다. 새로운 파란 tint나 태양 다중 산란을 하늘색으로 바꾸는 혼합은 넣지 않았다. 밀도/광학 두께가 달라지면 기여 비율도 달라지므로 sigma4 비교 이력과 현재 sigma1 결과를 섞지 않는다.

사용자 판정 대기: 림 양쪽광로 후보는 미채택으로 보존하며 일반 림은 기존이다. 이번 하늘광 분리는 코드에 반영했지만 06 화면 품질 승인/07 완료는 아니다.

검증 완료: Debug/Release 빌드 성공. 각각12조건 GPU 실행에서 HDR finite/D3D 오류 검사 통과. ROI Direct/Rim/Single/ViewT/SunT/Ground/Multiple은 전후 정확히 동일하고 Sky는 감소하지 않았다. Debug/Release HDR ROI 최대차 .000489. Release Stage6LightMath/Stage8AmbientMath/Stage15PresetMath/HighCloudSmoke 4/4 통과(7.42초). Stage8AmbientMath는 역사적 수학 회귀이며 새 Sky 분리의 직접 검증은 12조건 GPU 전후 비교다. 최종 색 대비 화면 승인과 전체 회귀/07은 남아 있다.

## 06 Sky fill/Multiple attenuation 두 차폐 방식 비교 — 착수

하늘광 태양 차폐 제거만으로 차이가 작다는 사용자 피드백을 기록한다. 이번에는 차폐 유지/제거를 각각 고정하고 동일한 네 후보를 비교한다: base(.85,.15), sky(2,.15), multi(.85,.075), both(2,.075). 값 순서는 Sky fill/Multiple attenuation이다. sky/multi는 독립 효과, both는 결합 효과 확인용이다. 현재 일반 sigma1, Urban/F5/71초, 방위-90, 고도18/5, Rim2/Depth1/cap2.5/80·79, GroundFill.85와 노출/WB를 고정한다. 총16조건이다. 일반 UI/기본값/셰이더 수식은 이번에 바꾸지 않는다.

Sky fill은 context.skyIncident에만 곱해지며 Multiple에 전달되지 않는다. Multiple attenuation은 산란 octave의 에너지 a,a²,…에 사용된다. 태양의 대기 투과 후 RGB×구름 tau 재사용 근사×시선 구간 가중치로 누적되며 SkyIrradiance를 사용하지 않는다. 따라서 a를 절반으로 바꾸면 Multiple 전체가 정확히 절반인 것은 아니다. Direct/Rim/Ground/ViewT/SunT 불변, Sky 단독 후보에서 Multiple 불변, Multiple 단독 후보에서 Sky 불변을 검사한다. 기존 저장물은 덮어쓰지 않으며 07은 미진행이다.

### 두 차폐 방식의 비중 비교 결과 — 2026-09-16

Debug/Release 각각16조건을 실행했다. 후보들은 실제 GPU 렌더 캡처이며 일반 환경광 기본 .85/.15는 유지한다. 기존 일반 하늘광 태양 차폐 제거 상태도 이번 테스트에서 새로 변경하지 않았다. 두 방식의 비교는 테스트 macro로 분리했다. 림은 기존 경로이며 양쪽광로 실험은 켜지 않았다.

| 역광5도 | 차폐 유지 Sky/Multiple | 차폐 제거 Sky/Multiple |
|---|---:|---:|
| 기준 (.85/.15) | .119 | .134 |
| 하늘광 증가 (2/.15) | .280 | .316 |
| Multiple 감소 (.85/.075) | .254 | .287 |
| 결합 (2/.075) | .598 | .675 |

수치는 유효 ROI의 View 불투명도>.1 표본에서 평균 Sky 밝기/평균 Multiple 밝기다. 전체 구름의 색 비율이나 픽셀별 비율 평균이 아니다. 18도는 기준 .117/.125,결합 .588/.629였다. 하늘광은2/.85=2.353배 증가했고 Multiple은 약46.9%로 감소했다. Multiple의 energy a,a²를 각각 절반/4분의1로 바꾸므로 합이 정확히 절반이 되지 않는다. 결합 후보의 상대 비중 변화는 약5배지만 직접광은 그대로이고 여전히 큰 비중을 차지한다.

Composite 관찰: 따뜻한 전체 인상이 크게 바뀌지는 않았다. 기준→결합의 전체 LDR 평균 절대차는18도 약.064/255,5도 약.076/255이며 최대차4~5/255다. ROI 평균차도 약.216~.330/255다. 배경이 포함된 전체 평균은 효과를 작게 보이게 할 수 있으므로 ROI와 원본을 함께 본다. Sky/Multiple 비율 상승만으로 미학적 색 대비를 달성했다고 판정하지 않는다. 두 방식 모두 사용자가 원본 비교 후 판정할 수 있도록 보존한다.

자동 검증: 모든 후보에서 Direct/Rim/Single/ViewT/SunT/Ground 불변. Sky 단독 조정에서 Multiple 불변, Multiple 단독 조정에서 Sky 불변, 결합은 각 독립 성분 결과와 동일. 차폐 유무에 따라 Multiple은 변하지 않았다. HDR finite/D3D 오류 검사 통과. Debug/Release HDR 최대차 .000489. 관련 Release Stage6LightMath/Stage8AmbientMath/Stage15PresetMath/HighCloudSmoke 4/4 통과(1.29초). 이 수학 테스트가 새 후보 화면 승인을 의미하지는 않는다. 07 전체 성능/회귀는 실행하지 않았다.

사용자 비교 순서: captures/stage15-directional-lighting/06-sky-balance/README.md 및 gallery.html에서 고도5도의 차폐 유지 네 후보→차폐 제거 네 후보를 먼저 본다. 같은 후보끼리 차폐 방식 차이도 비교한다. 마지막18도에서 중간톤/내부 보존을 확인한다. 선택은 아직 미승인이다.

## 06 기본 직접광 비중 비교 — 착수

사용자는 Sky fill/Multiple attenuation 비중 변경의 화면 변화가 미미하므로 기본 .85/.15를 유지하고 기본 직접광 비중 실험을 승인했다. 후보 식은 BaseDirect×k+Rim+Sky+Ground+Multiple, k=1/.75/.5다. 태양 세기 조절이 아니며 림과 Multiple은 그대로 둔다. 테스트 macro VCLOUD_TEST_BASE_DIRECT_SCALE만 추가하고 일반 수식/기본값은 변경하지 않는다.

Urban/F5/71초, 방위-90, 고도5/18, 일반 sigma1, 하늘광 태양 차폐 제거 상태, Sky/Ground fill.85, Multiple.15, Rim2/Depth1/cap2.5,80·79를 고정한다. 총6조건. Direct차이=(k-1)×기준BaseDirect, Rim/환경광/Multiple/ViewT/SunT 불변을 검사한다. 기본 직접광 감소가 색 대비를 드러내는지, 단순 어두워짐인지, 림만 뜨는지 사용자 비교용 gallery.html에 원본을 연결한다. 승인 전 후보 채택 및07 진행 없음.

### 기본 직접광 비중 샘플 결과

Release의6조건에서 k=1/.75/.5를 비교했다. 역광18도 ROI Direct 평균은 .37599/.34355/.31110이고 림은 .24620으로 고정됐다. 5도 Direct는 .15700/.14247/.12794,림은 .09889로 고정됐다. 현재 역광 ROI에서 림은 기준 Direct의 약63~65%이므로 BaseDirect를 절반으로 줄여도 합산 Direct 감소는 약17~19%다. 림이라는 이름이 물리적 실루엣에만 에너지가 있다는 뜻은 아니며, 기존 SunT/phase 가중치의 양의 항이다.

Rim/Sky/Ground/Multiple/ViewT/SunT는 정확히 유지됐다. k×(기준Direct−Rim)+Rim과 실제 Direct의 half 저장 잔차 최대 .000428. 화면 관찰은 따뜻한 밝음이 줄어드는 효과이며, 차가운 내부 색 대비가 완성됐다고 판정하지 않는다. 기준과 동일 구름/조명을 유지했고 phase/림 강도/추가 색 보정으로 보상하지 않았다.

캡처는 captures/stage15-directional-lighting/06-base-direct/gallery.html에 고도별3열로 배치했다. 이미지를 클릭하면 원본 PNG가 열린다. 이미지6개의 링크 존재를 확인했다. 사용자 판정은 내부 색/중간톤 유지/림만 뜨는지에 대한 비교이며 아직 후보 채택은 하지 않았다.

검증 완료: Debug/Release 빌드 및 각각6조건 GPU 실행 성공. HDR finite/D3D 오류 검사 통과. 두 빌드 HDR ROI 최대차 .000489. Release Stage6LightMath/Stage7PhaseMath/HighCloudSmoke 3/3 통과(1.29초). 일반 기본값은 유지하고 사용자 화면 판정/07은 미진행이다.

### 기본 직접광 사용자 판정 및 Powder 조사

사용자는 기본 직접광 k1 유지를 선택했다. .75/.5는 환경광/내부 그림자 개선이 두드러지지 않아 미채택. 일반 경로가 이미 k1이므로 코드 수정은 없다.
Powder는 별도 질감 텍스처가 아니라 기존 밀도/방향을 이용한 국소 산란 대비 근사임을 Guerrilla PDF와 Unity HDRP 공식 소스로 확인했다. 순광의 어두운 덩어리 가장자리·Detail 표현에 유용할 가능성이 있으며 역광의 밝은 실버 라이닝과 구분한다. 현재 Multiple/AO와 효과 중복 가능성, 밀도 단위·방향 부호·적용 대상 차이를 기록했다. 상세 근거/후속 최소 샘플 제안은 doc/research/stage15-cloud-lighting-research.md의 Powder 조사에 추가했다. 이번은 조사만 수행했으며 구현/화면 승인/07 진행은 하지 않았다.

## 06 Powder 기본 직접광 샘플 — 착수

사용자 승인으로 기본 직접광에 Powder 함수를 적용하는 테스트를 구현한다. curve=saturate(2×(1-exp(-4×finalDensity))), angle=1-smoothstep(-.5,.5,cosTheta), weight=lerp(1,curve,strength×angle). 기존 샘플 밀도와 방향만 사용하고 ds/소멸계수/밀도장/캐시는 변경하지 않는다. 곡선4는 첫 비교 후보이며 밀도 약.1733 이상에서 포화하므로 조밀한 내부의 효과는 제한될 수 있다. Unity 공개 방식의 밀도/방향 가중을 참고하되 여기서는 BaseDirect에만 곱한다. Rim/Sky/Ground/Multiple은 고정한다.

강도0/.25/.5, Urban/F5/71초, 순광90/측광0/역광-90 방위, 고도18/5도 총18조건. 일반 sigma1, Sky/Ground.85,Multiple.15,Rim2/Depth1,80·79,하늘광 태양차폐 제거 상태를 고정한다. 테스트 macro 외에는 새 조작/기본값을 추가하지 않는다. 역광은 중립1,순광이 주 효과다. 강도0 복원·투과율/림/환경광 불변·기본 직접광 감소 단조성 및 후보 변화의 강도 비례를 검사하고 gallery.html로 사용자에게 제시한다. 그림자가 생겼다는 이유만으로 품질 승인으로 간주하지 않는다.


### Powder 샘플 결과 — 2026-09-16

Debug/Release 각각 18조건 GPU 실행과 빌드를 완료했다. 강도 0은 이전 Sky 색 비교의 승인 기준 pack1/2/3/5/7과 HDR 차이 0으로 복원됐다. 후보 간 Rim/Sky/Ground/Multiple/ViewT/SunT는 정확히 불변이며 Direct 감소의 단조성과 강도 비례 검사를 통과했다. HDR finite/D3D 오류 검사 통과, Debug/Release ROI HDR 최대차 .00048828125. 관련 Release Stage6LightMath/Stage7PhaseMath/HighCloudSmoke 3/3 통과(1.30초). 첫 빌드에서 JSON raw string 종료 구분자가 수식과 충돌한 문제는 명시적 json 구분자로 수정 후 재빌드했다.

순광 ROI Direct 평균은 18도 강도0/.25/.5에서 .119129/.116410/.113692, 5도 .058864/.057477/.056089다. 강도 .5는 각각 약4.56%/4.71% 감소했다. 측광 감소는 약.75%/.85%, 역광은 변화0이다. 수치는 View 불투명도>.1 ROI 평균이며 국소 대비 개선의 증명은 아니다. 낮은 밀도 경계에 영향을 주지만 밀도 약.1733부터 곡선이 포화하므로 두꺼운 내부의 새로운 명암을 만드는 효과는 제한적이다.

captures/stage15-directional-lighting/06-powder/gallery.html에 순광→측광→역광, 고도18/5도, 강도0/.25/.5의 원본 PNG18장을 배치했다. 링크 존재를 검사하고 순광 후보 원본을 확인했다. 변경은 테스트 전용이며 일반 기본값은 유지한다. 사용자 판정은 미완료: 순광의 파임 대비가 좋아지는지, 단순 감광/검은 껍질/얇은 구름 소실처럼 보이는지 비교한다. 림 강화나 주변광 색 변경 효과로 해석하지 않는다. 07 전체 회귀·성능 검증은 아직 진행하지 않았다.


## 06 Detail 파임과 그림자 표현 분리 — 착수

사용자는 Powder 변화가 미미하다고 판단하고 Detail 증가 비교를 요청했다. 일반 Powder 미적용을 유지한다. 현재 View는 Detail 침식 후 밀도, Shadow는 Base를 쓴다. 침식은 raw Base의 1-smoothstep(.45,.90,Base)로 제한되어 조밀한 내부를 보호한다. Detail 강도를 올리는 것은 새 밀도 덩어리를 추가하는 작업이 아니라 경계를 깎는 작업이다.

VCLOUD_DETAIL_REVIEW 테스트에서 Urban/F5/71초, 방위0/90,고도18/5,Detail0/현재값/.5/1을 비교한다. 마지막 D-detail1은 View를 그대로 두고 캐시/cone에 기존 테스트 전용 Detail 밀도를 적용하여 표현 차이를 분리한다. 일반 그림자 정책은 변경하지 않는다. sigma1,shaping.70,Base1.50,Rim2/Depth1 및 조명/톤/노이즈 주파수 고정. ROI 투과율과 광학 두께,Direct와 Composite를 기록한다. View 가중 SunT는 Detail 변화로 가중 위치가 바뀌므로 Base캐시가 같아도 수치가 달라질 수 있다. 평균 명암 차이만으로 샤프함 달성을 판정하지 않는다.


### Detail 비교 결과 — 2026-09-16

현재 Urban Detail erosion 실제값은 .24였다. Debug/Release 각각20조건 캡처와 빌드 성공. 기존 .24의 순광/측광 pack1 Direct 수치는 이전 Powder0 기준과 일치한다. 마지막 B-detail1/D-detail1 쌍의 ViewT/ViewTau/phase 불변 검사 통과. HDR finite/D3D 오류 검사 통과, 두 빌드 ROI HDR 최대차 .00048828125. Release Stage4DetailMath/Stage12ShadowMath/HighCloudSmoke 3/3 통과(1.30초). 원본20장 링크와 대표 Composite를 확인했다.

| 침식 강도 | 고정 ROI 평균 불투명도 | 평균 View tau |
|---|---:|---:|
| 0 | .5493 | .8401 |
| .24 현재 | .2561 | .3095 |
| .5 | .07684 | .08259 |
| 1 | .003487 | .003416 |

ROI는 x624~879,y560~751 중 현재.24의 불투명도>.1인 동일 픽셀 집합이다. 네 조명 구도의 View 결과는 같다. 강한 침식에서는 대부분 소실되므로 이 값은 남은 구름만의 밀도 평균이 아니다. Direct 평균 감소를 그림자가 강해졌다고 해석해서는 안 된다. 화면에서도 Detail1은 대부분 사라졌다. 따라서 현재 강도만 높이는 방향은 조밀한 덩어리/선명한 파임을 얻기보다 광학 두께를 크게 깎는다.

D-detail1은 같은 View에 Detail 그림자를 적용하여 남은 구름의 Direct가 더 밝아졌으며(측광18도 .001513→.002091) View는 불변이었다. 침식된 태양 경로가 더 많은 빛을 통과시키는 방향과 맞는다. 그러나 대부분 소실된 극단값이라 현재.24에서 그림자 표현 정합 효과의 크기/품질을 대신하지 못한다. 가중 SunT 평균에는 사라진 픽셀의 진단0이 포함되므로 큰 강도의 평균값을 실제 매질 차폐 강화로 해석하지 않는다.

관찰과 추정 구분: Detail 변화가 없다는 가설은 이번 강도 범위에서 기각된다. Detail을 더 넣으면 내부 그림자가 선명해진다는 보장도 없다. Base의 조밀한 핵과 희박한 경계의 분포, 침식이 경계에 한정되는지, noise 크기/화면 투영 및 샘플 간격은 후속 분리 대상이다. Base가 평탄한 것이 단일 원인이라고 확정하지 않았다. 조명/밀도 기본값과 일반 Base 그림자 경로는 유지한다. 사용자 후보 채택 및07 미진행.

비교: captures/stage15-directional-lighting/06-detail-erosion/gallery.html. 첫 네 장에서 외곽/골 변화와 단순 소실을 구분하고, 마지막 두 Detail1 화면에서 같은 형상에 차폐 표현만 바뀌는지 확인한다. 미세 노이즈 과장·검은 껍질·작은 구름 소실은 실패 징후다. 사용자 판정은 미완료.


## 06 카메라 구도 변경 — 2026-09-16

사용자는 구름 외형을 지상 사람 시점과 상공에서 평가하기 위해 F5/F7 변경을 요청했다. F5 기존(0,15,180)→target(0,350,-1200)은 높이15m여서 사람 눈높이가 아니었다. F5를 position(0,1.7,180),target(0,60,-200)으로 변경하여 20×60×20m 건물을 앞에 두고 약8.7도 올려다본다. F7은 내부 시점을 폐기하고 CloudOverview position(40,7800,0),target(40,3000,-3000),약58도 하향으로 변경한다. F6/F8 좌표는 유지한다.

수직FOV60도(16:9 수평약91도),near.1m,far60000m는 기존 원근을 유지하고 공통 상수로 명시한다. 보편적으로 하나의 정답인 FPS 원근값은 아니며 이 씬의50km 렌더 범위에 맞춘 선택이다. 창 비율은 실제 크기를 따른다. 지상 눈높이 시작점일 뿐 지형 충돌/캐릭터 이동기는 추가하지 않는다. 조명/밀도는 변경하지 않는다.

기존00-baseline 및06 캡처는 불변이다. 이후 공유 프리셋을 읽는 테스트/성능case의 F5/F7 좌표도 달라지므로 기존 이미지와 직접 회귀 비교하지 않는다. 과거 재현은 저장된 position/target/FOV를 명시적으로 복원해야 한다. 07은 새 구도를 별도로 명시해야 한다.

사용자 확인: F5에서 건물이 정면에 보이고 눈높이가 지면1.7m인지 확인한다. F6은 이전 구도 유지, F7은 구름 위에서 아래를 보는지 확인한다. F8은 기존 완만한 하향 구도다. 건물 내부 시작/지면 아래 시점/클리핑은 실패 징후다. 화면 품질 승인은 미완료.

카메라 변경 자동 검증: Debug/Release 빌드 성공. 두 구성에서 Stage13CameraControlMath와 HighCloudSmoke 각각2/2 통과. 기존 중심 레이의 건물 교차, 프리셋 위치/시선 복원 및 행렬 finite 검사 통과. F7 검증 계약을 상공 하향으로 갱신했다. 실제 새 구도의 미학적 화면 승인은 사용자 확인 대기다.


## 2026-09-17 — 지상 근접 구도와 태양 방향 조감

관찰: 사용자는 F5가 여전히 멀고 건물이 시야를 가린다고 보고했다. F7 고정 조감도 원하는 빛 방향을 읽기에 애매했다. 목적은 건물 아래에서 하늘을 올려다보고 태양 쪽에서 구름을 관찰하는 것이다.

변경: F5 position(12,1.7,60),target(0,42,-40),상향약22도. 건물 중심까지 수평약61m이며 기존180m보다 가깝다. 건물 폭/깊이는20m,높이는60→30m(3m×10층). Renderer 정점 생성은 Stage13SceneMath의 치수를 직접 사용하여 CPU 계약과 실제 메시가 어긋나지 않게 했다. F6/F8 좌표와 FOV60/near.1/far60000은 유지한다.

F7은 FromSunDirection으로 현재 directionToSun 쪽 20km에서 target(0,3000,-1200)을 본다. 정상 고도에서는 시선=-태양방향이다. 태양고도 약14.5도 아래(야간 포함)는 방위는 유지하고 방향Y를 최소.25로 보정하여 카메라높이8000m 이상을 확보한다. 천정은 특이 시선행렬을 피하도록Y상한.9999,유효하지 않은 벡터는 안전한 대체방향을 사용한다. F7을 누를 때만 적용하며 계속 태양을 추적하지 않는다. 자동 main 카메라 순회에도 같은 resolver를 사용한다. 정적 Get(CloudOverview)은 과거 진단용 fallback이며 실시간 F7 결과와 같다고 해석하지 않는다.

검증 기준: F5에서 건물 앞/옆면을 보며 눈높이에서 올려다보는지 확인한다. 10층 박스는 지면에 붙어 있어야 한다. F3 태양방위/고도를 바꾸고 F7을 다시 누르면 태양 쪽으로 구도가 바뀌어야 한다. 낮은 태양에서는 수평시점 대신 안전한 하향각이 적용된다. F6/F8 위치는 유지된다. 자동 검증은 좌표/태양 정렬/저고도·천정finite/건물치수와GPU smoke이며 화면 승인은 사용자 대기다.

기존 캡처는 덮어쓰지 않는다. 건물과F5/F7이 변경되어 과거 이미지와 직접 회귀 비교할 수 없다. 밀도/조명/그림자 기본값은 변경하지 않았다.

자동 검증: Debug/Release 빌드 성공. 두 구성에서 Stage13CameraControlMath/Stage13UnifiedSceneMath/HighCloudSmoke 각각3/3 통과. git diff --check 통과. 화면 품질은 사용자 확인 대기.


### 2026-09-17 F5 정면 정렬 / F8 수평선

사용자 캡처 피드백: F5가 건물 측면으로 치우쳐 보이고 F8은 하향보다 수평선 구도가 필요했다. F5 position의X를12→0으로 바꿔 건물 중심과 target의X=0에 정렬한다. position(0,1.7,60),target(0,42,-40)이며 기존 상향 구도와 거리를 유지한다. F8은 position(40,7800,0)을 유지하고 target(40,7800,-1800)으로 바꿔 pitch0도를 만든다. 이름은 상공 수평선(F8). F6/F7,10층 건물,원근/구름/조명 설정은 유지한다.

사용자 확인: F5에서 건물 정면이 중앙에 좌우 대칭으로 보이는지 확인한다. 올려다보는 원근에 따른 위쪽 폭 감소는 정상이며 좌우 비대칭 기울기는 실패 징후다. F8에서 상공 수평선과 아래 구름이 함께 보이는지 확인한다. 평면 구름층 위 수평 중심 레이는 구름에 교차하지 않는 것이 정상이며 화면 아래쪽 하향 레이가 구름을 본다. 기존 캡처는 보존하며 화면 승인은 사용자 확인 대기다.

F5/F8 자동 검증: Debug/Release 빌드 성공, 두 구성에서 Stage13CameraControlMath/HighCloudSmoke 각각2/2 통과. F8 수평 중심 레이 계약으로 기존 하향 검사를 갱신했다. diff --check 통과.


### 2026-09-17 F8 하향각 재조정

사용자는 수평선 위 하늘보다 아래쪽 구름/지면 방향을 더 크게 보길 요청했다. F8 위치(40,7800,0),FOV60은 유지하고 target을(40,7417.4,-1800)으로 내려 pitch 약-12도로 조정했다. 수평 방향은 화면 상단 약32%에 놓이므로 아래 방향이 약68%를 차지하는 구도다(실제 대기 지평선은 지구 곡률 등으로 다를 수 있음). 표시 이름은 상공 완만한 하향(F8). F5/F6/F7 및 장면/조명은 유지한다.

사용자 확인: 재실행 후 F8에서 수평선이 화면 위쪽으로 올라가고 아래쪽 구름이 더 넓게 보이는지 확인한다. 지면 메시의 면적 확대나 새로운 지형 추가는 하지 않았다. 화면 승인은 사용자 확인 대기. 기존 캡처는 보존한다.

F8 하향각 자동 검증: Debug/Release 빌드 성공. 카메라 수학/GPU smoke 각각2/2 통과. diff --check 통과.


### 2026-09-17 F6: F5와 같은 위치에서 반대 방위

사용자 요청으로 F6을 F5와 같은 position(0,1.7,60)에 두고 target(0,42,160)으로 변경했다. F5 target(0,42,-40)의 Z 방향을 반전한 구도다. 방위만180도 돌리고 상향각 약22도는 유지한다. 현재 F5는-Z, F6은+Z를 본다. 이름은 지상 눈높이 · 건물 반대편(F6).

사용자 확인: 재실행 후 F5/F6 전환 시 위치 이동 없이 건물 쪽과 반대쪽 하늘을 번갈아 보는지 확인한다. 눈높이/상향각/FOV는 같아야 한다. F7/F8과 조명·밀도는 유지한다. 기존 캡처는 보존하고 새 F6은 과거 구도와 직접 이미지 비교하지 않는다.

F6 변경 자동 검증: Debug/Release 빌드 성공. 두 구성에서 카메라 수학/GPU smoke 각각2/2 통과. diff --check 통과. 사용자 화면 확인 대기.


## 2026-09-17 F1·F2 UI 정리와 범위 정합

문제: Cumulus 두께가 활성 타입에서 영향이 없거나 domain 적용이 거부되는 경우 미사용처럼 보였다. 실제 HLSL Weather는 타입에 따라 두 범위를 보간하고 Base lift를 local bottom에 반영한다. Domain height는 추적 영역 높이이므로 영역 안의 구름 두께와 별개다. F2 UI는 Macro1~64/Detail1~128/Bias-1~1을 허용했지만 canonical은1~8/2~16/-.5~.5여서 조작 후 되돌아갔다.

변경: Cloud Local은 실제 typeSelection에 따라 한 쌍 또는Thin-form/Thick-form 두 쌍을 표시한다. 각 ImGui ID는 타입 전환에도 안정적이다. Max base lift/Footprint influence는 Local,Domain bottom altitude/height는Ray Domain으로 분리했다. F1 상시 설명문은 제거하고 상태/실패/Preview는 유지했다.

두께 제약: thinMin=[1,min(thinMax,thickMin)],thinMax=[thinMin,thickMax],thickMin=[thinMin,thickMax],thickMax=[max(thinMax,thickMin),6000]. 숨겨진 값을 보존하기 위한 상호 제약이며 단일 타입에서도 숨겨진 값이 상한/하한에 영향을 줄 수 있다. Domain height 하한은 기존EvaluateFit의 활성최대두께+최대lift+200m. 다른 형상 편집이 영역을 넘으면 기존원자적 거부를 유지하며 자동확장하지 않는다.

공유 FormationParameterRanges는 기존허용값을UI/검증/sanitize에 연결한다. Density0~5,Extinction1e-6~.01,Softness.02~.8,Base size1~200000m,Detail size1~100000m,Domain bottom±100000m,height최소적합값~100000m,lift0~2000m,구름속도0~1000m. 큰양수거리와Extinction은로그슬라이더. Weather채널Macro1~8/Detail2~16/weight0~1/Bias-.5~.5/Contrast.25~3. 직접입력AlwaysClamp,정수는임시int로편집하고unsigned에명시변환한다. 풍향정규화는유지한다.

ABI/Custom schema/렌더수식/승인기본값 변경없음. 숨김/표시만으로 설정을 쓰지 않는다. 범위밖Custom파일 거부는 그대로다. F3/F4와07통합검증은범위밖.

사용자 확인 대기: F1 Urban/Stratus/Cumulus/Mixed 전환 후실제타입에맞는항목만보이는지확인. Cloud Local 두께를현재domain안에서바꾸면구름높이가변해야하며,영역을넘으면오류표시와기존값유지가정상이다. Ray Domain bottom은구름기준고도도변경하며height증가만으로로컬두께가커지지않는것이정상. F2 각채널Macro8/Detail16/Bias±.5/Contrast.25·3에서끝점과표시값이일치해야한다. Ctrl+클릭범위밖입력은허용끝값으로제한되어야한다. Custom저장/재로드에서숨겨진두께도유지되어야한다.


자동 검증 결과: Debug/Release 빌드 성공. 두 구성에서 Stage5WeatherMath, CloudFormationPresetStore, Stage15PresetMath, HighCloudSmoke 각각4/4 통과. 새 범위 끝값의 검증 통과·sanitize 불변성과 범위밖 period 거부를 기존 세 타입 테스트에 추가했다. Custom 저장/읽기·실패 시 보존·프리셋/domain 적합성 검증을 유지했다. diff --check 통과.

한계: 실제 마우스 드래그와 Ctrl+클릭 UI 조작은 자동 실행하지 않았다. AlwaysClamp 연결과 표시 분기는 코드로 확인했고 값 계약은 테스트했으나, 새 UI의 조작성과 화면 변화는 위 사용자 체크리스트로 확인해야 한다. 이를 화면 승인이나07완료로 기록하지 않는다.


### 2026-09-17 Domain UI 조작 범위 축소 / lift 의미 확인

사용자 요청으로 F1 Domain bottom altitude 슬라이더 하한을0m,Domain height 상한을10000m로 제한했다. height하한은활성최대두께+최대lift+200m를유지한다. 기존canonical도메인상한은100000m였으며1000000m가아니다. 저장호환을위해canonical/Custom허용범위는유지하고UI조작범위만의도적으로축소한다. 기존범위밖Custom을불러온뒤UI를여는것만으로값을덮어쓰지않는다. 편집할때새UI범위로제한된다.

Max base lift 실제상한은2000m,300m는내장프리셋값이다. 따라서lift상한은유지한다. HLSL lift=min(maxLift*lerp(.15,1,type)*(1-WeatherA)^1.5,localThickness*.25). 독립적인구름별난수가아니라두께채널에연결된지역별양수상승이다. 범위증가는위아래진동이아니라전체상승과지역차를함께키운다. 덩어리별랜덤상하이동은이번수정범위가아니다.

Domain height는구름local두께가아니라ray교차영역이다. 최대시선거리,512회반복,누적투과율early exit,empty-space skipping,scene depth종료가별도로작동한다. 따라서높이와GPU비용은선형관계가아니다. Shadow cache의고정높이slice간격에도영향을줄수있어영역을불필요하게늘리는것은품질개선이아니다.

사용자확인: F1 Ray Domain에서bottom음수직접입력이0으로제한되고height끝값이10000인지확인한다. lift의300/2000을비교할때그값은실제상승량이아니라상승한계이며타입과WeatherA에따라더작게적용되는것이정상이다. 화면승인은사용자확인대기다.

추가 확인과 실제 UI 수정: canonical lift상한2000m와별개로domain적합성은 lift<=domainHeight-activeMaxThickness-200m를요구한다. 사용자가관찰한300m제한은현재영역여유일수있으며300m하드코딩은아니다. Max base lift UI상한을clamp(domainHeight-activeMaxThickness-200,0,2000)으로동기화했다. 더큰lift가필요하면먼저Domain height를늘린다. 기존설정을여는것만으로보정하지않고적용거부정책도유지한다.

검증: Debug/Release빌드성공. Release Custom/Formation,Stage15PresetMath,HighCloudSmoke 3/3통과. diff --check통과. 실제UI드래그와화면확인은사용자대기.


### 2026-09-17 F2 Texture3D world size 하한

사용자 요청으로 Base world size/Base vertical size의 UI 하한을3000m,Detail world size는300m로 변경했다. 기존 UI 상한200000m/100000m와 로그조작은 유지한다. 저장/CPU허용범위와기본값은유지하여과거Custom을읽을수있으며UI를여는것만으로값을보정하지않는다.

의미: 세항목은Weather Map이아닌Base/Detail Texture3D의UV전체0~1한주기를월드에펼치는길이다. Base world size는XZ,Base vertical size는Y,Detail world size는XYZ공통이다. uvw=frac(world/size+offset)이며Base Y는domain bottom을뺀높이를쓴다. size3000m이면UV0.1차이는300m다. 텍스처내부에여러noise대역이있으므로한구름덩어리크기와size는같지않다. Weather의XZ UV는별도의Weather world size를사용한다.

확인: F2세슬라이더왼쪽끝과직접입력하한을확인한다. 값을키우면무늬가늘어나고작게하면촘촘해진다. Texture3D해상도나Noise내용을재생성하는설정이아니다. 사용자화면확인대기.

검증: Debug/Release 빌드, Release HighCloudSmoke, diff --check 통과. 실제 슬라이더 조작은 사용자 확인 대기.


## 2026-09-18 F2 Weather scale와 채널 미리보기

요청: Weather world size를3000~64000m로노출하고크기관련항목을분류하며RGBA를분리해이해할수있게한다. 기존F2원본Image는A(두께잠재값)를투명도로소비하여빈영역의검정을RGB0으로오인할수있었다.

변경: Base / Detail Noise Scale, Weather Map, Weather Generator로분류. Weather map scale은기존CloudCB weatherMapWorldSize를수정하고기존Formation원자적캡처/적용/Custom저장경로를사용한다. Formation하한17600→3000m로낮춰UI요청이거부되지않게했고상한160000m와기존파일호환은유지한다. UI상한은64000m. CB크기/schema/기본64000은변경없음. scale은샘플좌표의월드매핑을바꾸며새레이나밀도조회는추가하지않는다.

Density coverage link와Thickness coverage link를Weather Map으로이동했다. B=lerp(독립densityNoise,R,densityLink), A=lerp(독립thicknessNoise,smoothstep(.05,.95,R),thicknessLink).0이면독립noise,1이면Coverage를따른다. 값변경은Weather생성결과를바꾸는설정이다. G아래깨지는혼합언어안내문을제거했다.

GPU미리보기: 기존NoiseLab PS에F2전용output100(RGB),101~104(RGBA흑백)를추가했다. 원본Weather UV전체를샘플하고출력alpha를항상1로둔다.512² RGBA8타깃5장(약5MiB)을UI가소유하며F2열림시에만추가5draw한다. CPUreadback은없고SRV/RTV를분리하며종료때해제한다. F1출력선택값은로컬복사본으로유지한다. 최종렌더와성능측정UI-Off경로에는추가draw가없다.

채널의미: R은coverage/support재료이지최종구름불투명도가아니다. G는지역타입0층운~1적운이며Fixed모드에서는저장되지만최종타입에는쓰지않는다. B는최종밀도에곱할0.5~1.5배의재료(0~1저장). A는local두께min~max를보간하고lift에도사용되는잠재값이며투명도가아니다. 빈영역에는(거의0,.5,.5,0)을저장하므로검정이RGBA모두0을뜻하지않는다.

사용자확인: F2 Weather map scale에서3000/64000을비교하면배치의월드반복크기가바뀌되원본텍스처그림은같을수있다. 두link의0/1에서각각B/A미리보기가R분포를따르는지확인한다. R/G/B/A는검정0~흰색1이고A가낮아도RGB미리보기는투명해지지않아야한다. 흑백G는Fixed상태에서도보존된다. 화면승인은사용자대기.

검증 완료: Debug/Release 빌드 성공. F2를 실제로 여는 NoiseLabSmoke를 포함하여 Debug3/3(Custom/High/NoiseLab),Release4/4(추가WeatherMath) 통과. Weather scale3000/64000의canonical통과·sanitize불변성도기존끝값테스트에포함했다. diff --check통과. 채널색의수동시각확인은사용자대기이며GPU픽셀별원본대조검사는이번에추가하지않았다.


## 2026-09-18 F2 반복 무늬 피드백과 UI 하한 조정

관찰: 사용자는 Weather 64000m, Base XZ/Y 3000m에서 격자 무늬가 잘 보인다고 보고했다. 직전 UI 작업은 샘플링 수식을 바꾸지 않았으며 Base/Detail의 UI 하한만 3000/300m로 노출했다. 정확한 격자 원인은 아직 진단하지 않았다. 짧아진 반복 주기가 후보 원인이지만 캐시/적분 표본 문제와 구분하지 않은 상태다.

변경: Weather map scale UI 하한을 10000m, Detail world size UI 하한을 700m로 올렸다. 상한은 각각 64000/100000m를 유지한다. Base XZ/Y는 기존 3000~200000m를 유지하고, 실용 비교 범위로 XZ 6000~32000m, Y 6000~24000m, Detail 700~6000m를 제안한다. 이는 검증된 품질 한계나 채택 기본값이 아닌 후보 구간이다. 비교 시작점은 기존 Base XZ/Y 12000m, Detail 2000m다.

고정: 기존 저장/검증 범위, Custom 호환, 승인 기본값, UV 수식, Texture3D 생성, High 샘플 간격은 변경하지 않는다. UI를 여는 것만으로 기존 값을 보정하지 않는다. Weather가 이미64000m이면 하한 변경은 그 화면의 격자를 없애지 않는다.

사용자 확인: F2 Weather scale과 Detail world size의 왼쪽 끝/직접 입력에서 각각10000m/700m로 제한되는지 확인한다. Base 3000m에서의 반복은 Base12000m로 되돌려 비교하며, 무늬 간격이 world size와 함께 변하는지 확인한다. 바뀌지 않으면 반복 타일 원인으로 단정하지 않는다. 화면 판정은 사용자 확인 대기다.

자동 검증: Debug/Release 빌드 및 diff --check 통과. UI 하한 두 곳만 수정하여 별도 테스트는 추가하지 않았다. 실제 드래그와 격자 원인 판정은 미검증이다.


## 2026-09-18 Noise scale 상한과 Ray Domain 실용 범위

문제/요청: 넓은 canonical 허용 범위가 UI 튜닝 범위로 노출되어 과도한 크기까지 조절된다. 사용자 요청으로 Base XZ/Y 상한40000m, Detail 상한6000m를 적용한다. 하한은 Base3000m/Detail700m 유지.

Ray Domain bottom UI는0~4000m. Height는 절대 상단 고도가 아니라 바닥부터의 두께이므로 상단=bottom+height다. Height UI 상한은max(5000m, 기존EvaluateFit이 계산한 최소영역높이)다. 최소영역높이=활성최대두께+최대lift+200m 계약을 유지한다. 최소가5000m보다 큰 기존/편집 설정에서는 역전 범위를 피하기 위해 최소치까지 허용한다. 예: bottom4000m/height5000m이면 상단9000m. 영역을 자동 확대하거나 기존 설정을 자동 변경하지 않는다.

근거: Unreal API도 LayerHeight를 바닥 고도 위의 두께로 정의한다(https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UVolumetricCloudComponent). Horizon 공개 발표는 해당 게임의 체적 층을1500~4000m에 배치한 사례다(https://advances.realtimerendering.com/s2015/The%20Real-time%20Volumetric%20Cloudscapes%20of%20Horizon%20-%20Zero%20Dawn%20-%20ARTR.pdf). 특정 게임의 층 범위는 보편적인 엔진 최대값이 아니다. 여기의4000/5000m는 현재 장면을 위한 UI 정책이다.

고정: 저장/validation/Custom 허용 범위와 승인 기본값, 셰이더 UV/밀도/레이 수식, ABI는 변경하지 않는다. UI를 열기만 해도 기존 값이 바뀌지 않는다.

사용자 확인: F2 Base 두 항목 끝값40000m, Detail6000m, F1 bottom4000m를 확인한다. Height는 보통5000m이며 최소적합높이가 이를 넘으면 그 최소치까지 허용되는 것이 정상이다. Ctrl+클릭 입력에도 같은 UI 제한을 적용한다. 렌더 품질 판정은 사용자 확인 대기.

검증: Release 빌드와 diff --check 통과. Debug는 컴파일 후 링크에서 실행 파일 잠금(LNK1168)으로 갱신하지 못했다. 실행 중 사용자 앱을 종료하지 않았다. 실제 UI 조작 확인은 사용자 대기.


### 2026-09-18 UI 범위 미반영 신고 후 재확인

사용자는 이전 Debug 실행으로 확인하지 못한 것 같다고 설명하고 현재 Release로 확인 중이라고 알렸다. 이전 Debug 링크 실패로 두 구성의 실행 파일이 달랐다. 최신 Release의 요청 범위는 유지하고 Debug도 동일 소스로 갱신한다. 조사 중 추가한 범위 라벨/중복 clamp는 채택하지 않고 원래 UI와 AlwaysClamp를 유지한다. 실제 UI 자동 접근은 도구 승인 거부로 실행하지 못했다. 사용자 화면 확인 중이며 최종 승인으로 간주하지 않는다.


2026-09-18 후속 요청: Domain bottom altitude UI 상한을4000→7000m로 확대했다. Height는 두께이므로 bottom7000+height5000이면 추적 상단12000m다. 실제 구름의 local 상단은 Weather 두께와 lift에 따라 이 영역 안에서 결정된다. Height 적합성 하한/상한 예외, 저장 범위, 승인 기본값과 렌더 수식은 유지한다.


## 2026-09-18 F1 상태 문구 제거와 Weather R 설정 이동

요청: Cloud Local의 Effective Cloud Type 문구를 제거하고 Weather 채널 생성 설정은 F2로 모은다.
확인: coverageThreshold/coverageSoftness는 WeatherMapCompute의 R=smoothstep(threshold-softness/2,threshold+softness/2,coverageField)에 쓰인다. F1 Coverage의 Base noise 문턱과는 별개다.
변경: F1 Cloud Local의 Effective Cloud Type 한 줄을 제거. F1 Formation의 Weather threshold/softness를 F2 Weather Generator의 Coverage 채널 설정 바로 위로 이동하고 Coverage threshold (R)/Coverage softness (R)로 표시한다. 기존 범위0~1/.02~.8, AlwaysClamp, m_formationEdited 및 원자적 설정 적용 경로를 유지한다. 설정값/기본값/Custom/셰이더 수식은 변경하지 않는다.
사용자 확인: F1에 세 항목이 사라지고 F2 Weather Generator에 R threshold/softness 두 항목이 보이는지 확인한다. Threshold를 올리면 R에서 구름 허용 영역이 감소하고 Softness는 전이 폭을 바꾼다. R에 연결된 B/A 및 빈 영역 중립 처리는 기존대로다. 두께 통합/G 제거/profile 편집은 이 UI 이동과 별개의 후속 변경이며 이 기록에서 완료로 취급하지 않는다.

검증: Debug/Release 빌드 성공, Release NoiseLabSmoke/CloudFormationPresetStore 2/2 통과, diff --check 통과. 실제 화면 확인은 사용자 대기.


## 2026-09-18 공통 두께·고정 타입·공통 프로파일 구현 시작

관찰: 타입별 두께 네 항목과 Weather G가 설정 이해를 어렵게 한다. 사용자 요청 순서대로 공통 Min/Max, Regional/G 제거, 공통 profile UI를 구현한다.
가설/고정: Fixed Stratus/Cumulus의 활성 두께와 곡선을 공통 필드로 옮기면 외형을 유지할 수 있다. Regional(Mixed/Meadow 및 이전 Custom)은 고정Mixed로 이관하므로 지역별 타입 차이가 사라지는 것은 의도된 변화다. R/B/A와 노이즈·조명·High 상수는 유지한다.
구현 계획: Custom schema4, 이전1~3의 두께/곡선은 선택 타입으로 이관. G는 생성하지 않고 RGBA의 예약 중립 채널로 남긴다. b10=32B,b7=48B는 예약칸으로 크기를 유지하되 새 필드 계약을 문서화한다. 공통 profile은 bottom fade/top fade/lower density/upper transition start/end로 기존 고정 곡선 재현과 직접 편집을 제공한다. 07 전체 승인 단계와 구분한다.


### 실제 변경 — 공통 두께 / Weather G 제거 / 공통 Vertical Profile

1. `WeatherColumnSettings`는 minimumThicknessMeters, maximumThicknessMeters, maximumBaseLiftMeters 세 필드다. F1 Cloud Local은 항상 Cloud min thickness / Cloud max thickness 두께 두 항목만 표시한다. 실제 두께는 `min+(max-min)*Weather A`이며 타입을 바꾸어도 같은 min/max에서 두께 공식은 같다. 프리셋 버튼은 자기 시작값을 이 공통 필드에 넣는다.
2. Fixed Stratus/Mixed/Cumulus만 남겼다. Regional Blend enum/CPU·GPU 혼합 경로, G 생성기/sanitize/hash/저장/UI를 제거했다. G는 RGBA8의128(중립0.5) 예약값이며 읽지 않는다. R/B/A seed와 생성식은 유지한다. F2는 RBA 합성+R/B/A 흑백4장만 표시한다. 합성의 화면 RGB는 데이터 R/B/A를 각각 배치하므로 이전 RGB합성 색과 다르다. 최종 구름 조명에는 영향을 주지 않는다.
3. F1 Vertical Profile에 공통 곡선5항목과 미리보기를 추가했다. Height-based narrowing은 이전 Footprint influence의 새 이름이다. 하단/상단 fade와 밀도 상승 높이는 지역 높이의%로 표시한다. 곡선 미리보기는 가로축=바닥0→상단1, 세로축=밀도배율0→1이다.

공통 곡선:
`P(h)=smoothstep(0,bottomFadeEnd,h)*(1-smoothstep(topFadeStart,1,h))*lerp(lowerDensityScale,1,smoothstep(upperTransitionStart,upperTransitionEnd,h))`.
두께는 이 곡선이 실제로 펼쳐질 세로 길이를 정한다. 예: 두께2000m이면50%는local bottom+1000m다. Lower density=1이면 밀도 상승 구간은 효과가 없는 것이 정상이다. 프로파일은 밀도 배율을 바꾸고 Height-based narrowing은 타입별 높이 단면의 노이즈 문턱 영향을 조절한다. 노이즈 자체/월드 UV/적분 step은 바꾸지 않는다.

기본값과 호환:
- Urban/Cumulus: 두께2000~3200m, fade.08/.94, lower.65, rise.08/.70. Stratus/Snow:1500~2300m, fade.05/.72, lower1. 기존 고정 타입의 활성 계산을 유지한다.
- Mixed/Meadow:2250~3550m, fade.10/.86, lower1, 고정Mixed. 이전 Regional의 지역별형상과 동일한 화면을 보장하지 않는다. 제거 기능에 따른 의도적 변화다.
- Custom schema4는 새 공통 필드/RBA만 저장한다. schema1~3는 로더 내부에서만 옛 두께4개/profile9개/G필드를 해석한다. 고정타입은 활성 범위/곡선을 선택하고 Regional3은 FixedMixed1 및 두께 범위 평균으로 이관한다. schema1/2 densityShaping=0, schema3은 저장값을 유지한다. 읽기만으로 옛 파일을 덮어쓰지 않는다. 지역별 G를 재현할 수 없다는 제한은 로드 상태문구에 표시한다.
- Snapshot43에 thicknessMeters2개/verticalProfile5개를 기록한다. b7=48B,b10=32B 크기는 유지하지만 필드 의미가 바뀌므로 새exe/셰이더를 같이 사용해야 한다. offset 표는 CBUFFER_REFERENCE/ARCHITECTURE에 갱신했다. Weather compute b0=160B의channels[1]은 예약칸이다.

CPU→GPU 흐름/책임: NoiseLab F1 편집 → Formation 후보 전체 검증/원자적 적용 → shape b7 및 column b10 → Weather A를 local두께/lift/정규화높이로 해석 → 공통 profile과 타입별footprint로Base밀도 → 기존Detail/shaping/View/Shadow. CloudFormationPresetStore는 시작값/Custom 이관을, WeatherMap.cpp/WeatherMapCompute는RBA 생성을, CloudShapeParameters CPU/HLSL은같은profile 곡선을 소유한다.

사용자 렌더 체크리스트(승인 대기):
- [ ] F1 Stratus/Cumulus/Mixed 및 F4 Urban/Meadow/Snow를 선택한다. Cloud Local에 두께 슬라이더2개만 나타나야 한다. Thin/Thick4개나 Regional 표시가 남으면 실패다.
- [ ] F2에서 G설정/흑백이 사라지고 RBA/R/B/A만 표시되는지 확인한다. RBA합성은색상표현이며구름색이 아니다. 채널설정은해당R/B/A만 기존link규칙대로 반영되어야 한다.
- [ ] F1 Cloud Local에서 min=max를 같은 적합두께로 설정하면 A로 인한 두께변화가 사라진다(lift/base noise 변화는남는다). Domain이작으면적용거부가 정상이며UI를열기만해값이바뀌면실패다.
- [ ] F1 Vertical Profile에서 Bottom fade end를8→25%로 올린다. 바닥에서차오르는구간이길어져낮은부분이희박해지는것이정상이다. Top fade start를94→65%로낮추면상단이일찍사라져야한다.
- [ ] Lower density를1→.4로내리면상부대비하부가희박해진다. Density rise start/end를조절하면밀도증가높이가이동한다. lower1에서rise조절영향이없는것은정상이다. 좌우복잡한파임을새로추가하는슬라이더가아니다.
- [ ] Height-based narrowing0/1을비교하면높이별폭차이가바뀌되Vertical profile은계속적용된다.0에서도상하fade가남는것이정상이다.
- [ ] Save Custom후Type을바꾸고Custom을불러오면두께2개/profile5개가복원되어야한다. 이전Regional Custom은고정Mixed로이관되어지역타입차이가없어지는것이정상이며정확한옛화면복원이아니다.

미룬 항목: 사용자 화면 승인, 하부 평탄화 추가분석,07 전체회귀/성능/최종포트폴리오촬영. 이번 변경은 새로운 두께/프로파일 조작을 제공하며 림·노출·조명 승인값을 바꾸지 않는다.

검증 완료: 최종 Debug/Release 빌드 성공. Debug 관련9/9, Release 관련10/10 통과(WeatherHotReload 포함). Weather CPU/GPU, Formation 적용, Custom1~3 이관/4 round-trip, 공통profile CPU/GPU 오차<=1e-5, High 렌더 및 F1/F2 NoiseLab smoke를 확인했다. 최초 실패는 폐기G/Regional 기대값 및 snapshot42 기대값이 남은 테스트였으며 새계약으로 수정후 통과했다. 결과로그는 captures/stage15-directional-lighting/06-formation-refactor/에 보존. diff --check 통과. 실제 마우스 조작/화면 품질은 사용자 승인대기이며07전체검증은 수행하지 않았다.
