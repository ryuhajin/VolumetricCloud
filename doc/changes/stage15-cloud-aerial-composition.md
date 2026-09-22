# Stage 15 — 대표거리와 원경 대기 합성 검증

2026-09-22 첨부 계획 실행 완료 후 사용자 거리 2배 채택. High 적분/CB/UI/schema/8프리셋 불변. push 없음.
중복 방지 인덱스는 [품질 후속 기록](stage15-cloud-quality-followups.md)에 있다.

## 가설과 분리 방법

구름 산란 `c_i`는 앞 구름T와 구간 적분을 포함하고 불투명도 기여는 `w_i=T_before*(1-T_step)`이다.
표본별 합성은 `Σ(T_air(d_i)*c_i + L_air(d_i)*w_i) + T_cloud*clear_background`다.
대표거리는 `Σ(w_i*d_i)/Σ(w_i)`이며 구름색에 alpha를 다시 곱하지 않는다.
배경은 기존 Scene/대기LUT 그대로 고정해 구름 쪽 근사만 바꾼다.

| 경우 | 거리 | 공기값 |
|---|---|---|
| A | 대표거리 | 현재 Aerial LUT |
| B | High 각 표본 | 현재 Aerial LUT |
| C | 대표거리 | 실제 카메라ray 직접 중점 적분 |
| D | High 각 표본 | 실제 카메라ray 직접 중점 적분 |

직접 참조는 기존 매질/위상/태양T/다중산란 공식을 사용한다. 최대100m/50m 구간으로 나누고
D는 마지막 공기 표본에서 이어 누적한다. 구름 View100m/512·coarse/early exit·조명은 같다.
태양T/Multi LUT는 공유하므로 Aerial LUT의 시선·깊이·4구간 적분 및 저장/보간 오차를 함께 조사한다.
분자 모델 전체나 구름에 의한 대기 그림자의 절대 정답은 아니다.

## 입력·산출물과 사전 기준

저장 Cumulus+환경3,1920×1080,FOV60,71초,바람0/태양재생Off,Original Base,Detail64,Deep80/79.
F5/F6/Near75/Horizon5. Turbidity1.5/Mie높이2km/g.3을 확인했다.
고정 중심 탐색창에서 불투명도 최대와 가까운25% 경계를 찾아320×64 ROI를 선택한다.
ROI좌표/카메라 역행렬, 모든 셰이더 FNV1a64, 원본8JSON 사본/해시를 기록한다.
전체 문맥PNG와 ROI PNG/HDR, 픽셀CSV만 저장하고 대량 이동·성능 반복은 없다.

- 재현/복원: 기존 normalized HDR MAE≤1e-5/max≤1e-3, LDR MAE≤1/255/max≤2/255.
- 100→50m 수렴: normalized HDR MAE≤1e-5/max≤1e-3.
- 합성계약 max≤.002, 같은 Cloud T max≤.001. R16F 양자화/컴파일 경로 오차 포함.
- 새 조사 유의성: normalized RGB MAE>.002 또는 p99>.01이면 배율 보류.
  기존 재현 tolerance 완화가 아니며 화면 합격 기준도 아니다. 극소수 outlier는 max로 별도 보고한다.
- normalized는 각RGB `v/(1+v)` 차이다. 빈 구름/공기 없음/불투명/두 층/앞 건물은
  GPU helper와 CPU 독립식을 비교한다. 실제 Scene 가림ROI의 Cloud alpha도 검사한다.

명시적 `--cloud-aerial-composition-test`, CTest 등록은 `VCLOUD_ENABLE_DIAGNOSTIC_TESTS=ON`만.
실패 시에도 부분CSV/README와 CPU복원, 성공 시 일반PS/CB/LUT를 다시 렌더한다. UI추가 없음.

## 조건부 거리 배율

기준 통과 뒤만1/1.5/2를 비교한다. `VCLOUD_TEST_AERIAL_SCALE`이 있는 Cloud PS만 구름거리
`d'=scale*d`로 Air T/L을 함께 조회한다. 배경/지면/밀도/조명은 그대로다. 전역 물리계수 변화와
다른 예술적 보정이며 근경에도 영향을 준다. 최초 비교에서는 일반값을 유지했고, 아래 사용자 후속 승인으로 일반 합성에 2배를 채택했다.

최종 후보 촬영은 `VCLOUD_TEST_AERIAL_SCALE_RUNTIME` 하나의 DXBC에서 진단번호121/122/123으로
거리1/1.5/2를 전환한다. Cloud T는 그 Composite의 alpha에 함께 저장해 별도T용 셰이더 변형을
비교하지 않는다. 일반UI/CB에는 새 필드가 없다. 기존 상수 `VCLOUD_TEST_AERIAL_SCALE`은 후보
Air T/L 수치 조회에만 쓴다. 분석기는 이 Air값과 raw구름으로 후보HDR을 독립 재합성한다.

실무 근거: [Skybolt 개발사 설명](https://prograda.com/2021/07/28/rendering-planetwide-volumetric-clouds-in-skybolt/)의
평균거리 기반 대기 적용, [Epic 속성 참조](https://dev.epicgames.com/documentation/en-us/unreal-engine/sky-atmosphere-component-properties-in-unreal-engine)의
Aerial Perspective Distance Scale. 현재 증상 해결의 증거로 사용하지 않는다.

## 검증 결과

최초 비교 Release `build/captures/cloud-aerial-composition/24612-3551390`, Debug `2840-3445390` 모두 PASS.
Debug/Release 빌드와 관련 대기·핫리로드·프리셋 회귀 각각 10/10 통과.

| 시점 | A–B normalized MAE | C–D normalized MAE | A–C normalized MAE | 2배 시 구름–배경 대비 변화 |
|---|---:|---:|---:|---:|
| F5 | .00005404 | .00005402 | .00001054 | +17.25% |
| F6 | .00013140 | .00013174 | .00001521 | −5.32% |
| Near75 | .00003410 | .00003435 | .00000666 | −0.38% |
| Horizon5 | .00007874 | .00007891 | .00001585 | −24.67% |

대비는 geometry=0, alpha>.01인 ROI의 같은 ray 구름 없는 배경과 선형 휘도 절대 차이 평균이다.
방향에 따라 F5처럼 대비가 증가하므로 모든 원경의 대비 감소를 보장하는 보정은 아니다.
선택 조건에서는 대표거리/LUT 근사가 주원인이라는 근거가 부족하다. 모든 광선에 대한 무오류 증명은 아니다.
100→50m 수렴 max≤.00009642, 합성 5계약 max .000390625, 후보 간 Cloud T 차이 0.
건물 가림 6495픽셀 cloud alpha 0, depth-bin 합 오차≤.001206. 진입 상태 복원은 HDR/LDR 차이 0.
분석기로 각 실행의 48PNG/32HDR/4CSV, 원본8JSON, 독립 재합성을 검산했다.

초기 fxc X3531은 동적 vector 성분 쓰기를 명시적 mask로 변경해 해결했다.
별도 컴파일한 T 영상의 소수 픽셀 차이는 동일 DXBC Composite alpha 비교로 분리했다.
기존 재현 tolerance는 완화하지 않았다. 자동71초와 UI시간 혼용 복원 검사는 같은71초로 맞췄다.

## 2026-09-22 사용자 승인과 일반 반영

사용자: “2배가 괜찮아보여. 2배로 확정하고 문서에 남겨둬”. 거리 배율 **2** 채택.
`CloudAerialLookupDepth`가 일반 구름 대표거리에 2를 곱하고 Air T/L을 함께 조회한다.
일반 F4 Air T/L 진단도 이 조정 거리를 사용한다. 실제 대표거리·구름 밀도·Cloud T·하늘/지면 조회는 유지한다.
대기 제외 모드는 보정을 우회한다. 모든 형상/환경 슬롯에 적용하는 셰이더 상수이며 JSON 필드나 새 UI는 없다.
이는 공기 물리계수 변경이 아닌 구름 전용 예술적 보정이다.
A/B/C/D 진단은 명시적 1배 대조군을 보존하고, 일반 2배 영상과 기존 후보 2배의 동일성을 추가 검사한다.
일반 반영 후 빌드/회귀/동일성 검증은 아래에 결과를 누적한다.

다음 단계는 사용자 선택 **근경 윤곽·밀도 경계 분석**이다. 몸체 보호/높이/밀도 후보를 자동 채택하지 않는다.
[근경 경계 분석](stage15-cloud-boundary-analysis.md)에서 현재 캡처와 보존 계보를 구분한다.

## 사용자 확인

- comparison.html 전체 위치로ROI를 확인한 뒤 A↔B와 C↔D를 본다. 같은 구름의 대기색/대비만 달라지는
  것이 예상 결과다. 경계 이동은 실패 징후다. A↔C와 B↔D는 Aerial LUT 근사를 비교한다.
- 배율1/1.5/2 전체화면과 동일ROI를 비교한다. F6/Horizon5 먼 대비가 자연스럽게 줄어드는지,
  Near75 윤곽/파임/내부명암이 남는지, 태양후광/경계 색 불연속이 커지는지 본다.
- Air T는RGB 빛 생존율, Air L은 공기가 더한 선형빛이다. background는 **같은ray의 구름 없는 배경**이며
  옆 하늘 픽셀과 같다는 뜻이 아니다. 네 작은ROI가 모든 조건을 대표하지 않는다.
- [x] 2026-09-22 사용자 거리 2배 채택. 근경 윤곽 개선 및 Stage15 전체 최종 승인과는 별개다.

### 일반 반영 후 최종 검증

Release `build/captures/cloud-aerial-composition/21200-5825265`, Debug `26696-5974687` PASS.
일반2배↔후보2배 normalized HDR MAE≤5.08e-9/max≤.000155915, LDR max1/255.
후보1/1.5/2 사이 Cloud T 차이0, 진입상태 복원 HDR/LDR 차이0. 분석기 두 실행 PASS/원본8JSON 불변.
A/B/C/D의 별도 진단 shader와 일반T 사이 최대차이 .000488282로 .001 기준 이내다.

채택 후 첫 실행 `11704-5628703`은 depth-bin 합 검사 .00213623으로 실패했다.
진단 bin 가중치를 algebraically equivalent한 실제 T전−T후 차이(precise)로 누적해 작은1−exp 상쇄를 피했다.
다시 측정한 bin 합 오차≤.000732422. 일반 High의 T/대표거리/산란 계산은 바꾸지 않았고 tolerance도 유지했다.

최종 Debug/Release 빌드 통과, 관련 대기·핫리로드·프리셋·몸체보호 회귀 각10/10 통과(17.67/13.12초).
이후 전체폭CSV 저장 좌표 수정/Debug실행 완료와 설명문만 반영했으며 렌더 후보를 추가하지 않았다.
Debug 최종 실행에는 Near75의1920×64 T띠를 저장해 다음 근경 분석에 재사용했다.
성능 개선이나 Stage15전체 완료를 주장하지 않는다. 사용자2배승인만 확정, push 없음.
