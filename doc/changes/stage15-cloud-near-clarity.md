# Stage15 근경 선명도 / Detail 태양 차폐

2026-09-22 사용자 계획 실행 후 **정규화 remap(후보2)을 일반 기본식으로 채택**했다.
사용자 피드백: 외곽이 더 살아나는 느낌은 있으나 **근경 선명도 자체는 아직 개선되지 않았다**.
원본8프리셋/대기2배/High/CB/schema와 Base 태양 차폐는 유지하며 push하지 않는다.
아래 초기 실험의 수치와 채택 전 권고는 당시 기록으로 보존한다. 최신 결정은 마지막 채택 절을 따른다.
새범위: 현재Cumulus 인접9광선의 단계별 계보와 정규화형상에서의 침식 순서/재매핑 비교.
옛2광선/833px 전체변화길이는 재사용하되 단일실루엣폭으로해석하지않는다.
기존실험 E01–E15, NearDetail/전체Detail캐시/Powder/전이폭/해상도/스텝격자는반복하지않는다.

S=weatherThresholdDensity, A=inside×weatherSupport×verticalProfile×densityMultiplier×weatherDensityModifier.
e=Detail×strength×(1−smoothstep(.45,.90,S))×(1−protection×기존core).
기준기존식, 후보1=Shape(saturate(A×saturate(S−e))), 후보2=Shape(saturate(A×saturate((S−e)/(1−e)))).
e≥1은0, Detail비활성은기존경로. Base밖/높이경계0과Base상한을보존한다.
현재큰형태지원영역을보존하지만 내부불투명도/파임은변할수있어사용자판정이필요하다.

근거: Unity HDRP 공식 VolumetricCloudsUtilities의 침식remap 뒤 밀도배율, Light cheapVersion/침식량보정.
https://raw.githubusercontent.com/Unity-Technologies/Graphics/master/Packages/com.unity.render-pipelines.high-definition/Runtime/Lighting/VolumetricClouds/VolumetricCloudsUtilities.hlsl
실제식/계수복제아닌프로젝트구조비교. Unreal의참조직접적분과저비용그림자분리도같이참고했다.
https://dev.epicgames.com/documentation/en-us/unreal-engine/volumetric-cloud-component-in-unreal-engine

검증기준: 기존Composite복원허용치유지. 밀도상한/0계약R16F max .002, T단조성 .001.
태양수렴T MAE≤.001/max≤.01(작은가시표본집합, 화면합격기준아님).
가시표본가중T차이 평균>.01 또는 p95>.03인영역만Detail캐시화면비교.
태양가시표본은실제High의불투명도기여25/50/75%지점,25m계보와분리한다.
성능은일반채택후보선정후에만측정. 초기후보의룩/다른타입/이동최종승인은사용자판정.

## 구현과 재실행 근거

`--cloud-near-clarity-test`/선택형 CTest CloudNearClarity를 추가했다. 일반 UI/CB/schema는 그대로다.
초기 비교 당시 Noise.hlsli의 후보 define은 일반 컴파일에서 제거되었다. 채택 후 기본식은 후보2다.
PS의 High trace hook은 실제 iteration의
거리/불투명도기여/ds/T앞만 기록하며 기존적분을바꾸지않는다. 25m계보와 별개다.
CloudNearClarity.inl은 기존capture/PS/DeepCS를재사용하고 원래CPU상태/셰이더/화면을복원한다.
AnalyzeNearClarity.py는128² ROI HDR/PNG,계보,태양CSV를검산하고비교페이지를생성한다.

현재Cumulus는기존Urban Detail.24와달리Detail.608/보호0/현재저장형상이며,침식순서라는새수식을비교한다.
옛NearDetail/캐시/Powder격자를반복한것이아니다. 새가시표본에서태양표현차이가기준을넘어조건부캐시비교를수행했다.
모든실행은GPU직렬이며일반룩채택/성능주장을하지않는다.

## 최종 산출물

- Release: `build/captures/cloud-near-clarity/26500-9738437/comparison.html`
- Debug: `build/captures/cloud-near-clarity/27192-9848156/comparison.html`
- 각 실행: 330PNG/315개128² HDR, 6개25m계보와6개High추적,6개Detail캐시표본,태양/카메라/ROI통계,원본8프리셋.
- Release 분석 포함 약89.6MiB. 전체화면HDR/모든후보이동촬영은저장하지않았다.
- 입력셰이더FNV/분석입력SHA256/원본8JSON/카메라행렬을기록했다.

ROI는기준Alpha의최대기울기/주변보다낮은골후보/최고불투명도에서자동선정했다.
이름만으로실제3차원외곽/골/몸체임을증명하지않는다. 각중심수평±4pixel의9인접광선을사용한다.
국소폭은기준영상중앙기울기의법선±60pixel에서고정기준10/90%문턱으로측정하며,
다중굴곡/화면잘림/전이부족은미측정이다. 옛전체폭833pixel이나과거351pixel과직접비교하지않는다.

## 형상 결과와 권고

| Near75 영역 | 기준 | 정규화 감산(1) | 정규화 remap(2) |
|---|---:|---:|---:|
| 외곽 후보 Alpha 평균 | .2647 | .3049 | .3843 |
| 외곽 후보 국소 전이폭(px) | 60 | 60 | 46 |
| 골 후보 Alpha 평균 | .1958 | .2869 | .3730 |
| 골 후보 대기제외 휘도대비 | .3500 | .1676 | .1496 |
| 골 후보 국소 전이폭(px) | 48 | 72 | 67 |
| 몸체 Alpha 평균 | .7930 | .8322 | .8996 |

대비는기준Alpha .2–.9 마스크의 (휘도P90−P10)/(P90+P10)이다. 후보별마스크를바꾸지않았다.
후보2의선택외곽폭은줄지만골은더채워지고해당대비는줄었다. 후보1도몸체회복과윤곽개선을동시에입증하지못했다.
F5다른영역에서는대비가증가한다. 한방향/한수치만으로성공을판정하지않는다.
현재수치상두후보모두일반채택을권하지않으며기준형상을유지한다. 최종화면평가는사용자에게남긴다.
유망후보가확정되지않아다른3타입·순광/측광/역광확대·이동·성능측정은조건부미실행이다.

25m계보 Near75의Base양수중최종0비율은67.92%→49.95%/49.95%,
최종/Base밀도적분량은18.25%→22.75%/30.48%. 이는가시불투명도비율이아니며
High종료뒤표본도포함한다. 감산순서에따라밀도가회복되는점과골명암손실을구분했다.

## 태양 그림자 결과와 권고

18개시점/후보/영역모두새조사문턱을넘어동일View의Detail캐시화면을저장했다.
현재기준형상의Near75 외곽/골/몸체 가중SunT차이는 .87035/.58670/.22432였다.
반면같은Base참조와기존캐시의해당오차는 .00011/.00048/.00009로훨씬작다.
따라서이선택표본에서는캐시정밀도보다Base/Detail **밀도표현차이**가크다.
선택표본25/50/75%의구간기여로가중한통계이며전체픽셀전체광선의오차로확대하지않는다.

기준형상Near75 실제ROI Direct평균은Base→Detail 캐시에서
외곽 .013359→.156645, 골 .021641→.087285, 몸체 .041351→.161384.
Sky는같고Ground/Multiple도반응한다. Direct는Rim을포함한다. 이것은밝아진정도이며선명도점수가아니다.
구름ViewT는전픽셀차이0. 지면/건물은같은Detail캐시를쓰므로별도전체문맥Composite에서함께확인한다.

Detail캐시↔같은Detail6.25m참조 가중T MAE는F5 .00464~.00707,
Near75 .00245~.00289. 최대오차는F5 .05371,Near75 .01172까지있다.
이오차를Base/Detail표현차이와구분했으며정밀도격자를추가하지않았다.

**권고:** 현재Base기반그림자는실시간근사로유지하되,현재Cumulus에서표현불일치가작다고말할수는없다.
전체Detail도입은직접광을크게늘리므로선명함해결책으로바로채택하지않는다.
사용자가같은형상의Base/Detail에서파임명암이좋아졌다고판정한경우에만
선택형상의이동/태양각/다른타입/성능을확장한다. 옛NearDetail혼합은복구하지않았다.

## 자동 검증 및 수정 이력

Debug/Release빌드와새진단모두PASS. 원본8JSON불변/유한HDR/D3D오류없음/일반상태복원PASS.
관련 CTest 회귀는 Release 17/17(16.74초), Debug 17/17(24.30초) 통과했다.
밀도·그림자 수학, High/Formation/Atmosphere/NoiseLab smoke, 핫 리로드와 의존성,
프리셋 배포, Detail core UI 검사를 포함한다. 실행 로그는
`build/near-clarity-release-regressions.log`, `build/near-clarity-debug-regressions.log`에 남겼다.
밀도경계/빈영역/Detail0/보호0,1/Base상한/지원precheck 검사최대오차 .00024415 이내.
25m GPU계보의독립CPU수식검산은half입력/출력의양자화를포함한새기준max .003을사용했고통과했다.
실제High기여합과영상Alpha최대차≤.000673,태양12.5→6.25m수렴최대 .000488282.
그림자변경ViewT차이0,진입상태HDR/LDR복원0. Release F6복원max .00009601/LDR1단계로기존기준통과.
Debug↔Release ROI선형HDR 최대차 .0009765625, 가장큰ROI MAE .000002236.
분석기/PNG복호화/HDR규격/HTML자산목록/JavaScript문법/로컬HTTP200 확인.
CTest기본OFF일때새진단미등록,ON일때1200초/gpu잠금/diagnostic라벨등록확인.

초기HLSL의예약어point를selectedSample로변경해컴파일했다.
초기3080-9393125의조명출력번호가실제enum보다어긋난것을발견해수정했고,
진행중Debug13232-9637578을중단후양쪽최종재실행했다. 두중간폴더는SUPERSEDED로표시해최종근거에서제외했다.
이는새후보반복이아닌진단구현오류수정이다. 기존재현허용치완화는없다.

## 사용자 비교 순서

1. comparison.html에서Near75/외곽/Alpha/Base그림자를선택한다. 검정투명/흰색불투명이며0/1/2동일위치를본다.
2. 골후보/대기제외로바꿔파임이메워지는지본다. 모양이더채워져도골대비가줄면자동합격이아니다.
3. 같은형상번호에서Base↔Detail그림자를전환한다. Alpha는같아야하며명암/밝기만달라져야한다.
4. 전체문맥/F5에서지면과건물그림자도확인한다. 구름자체그림자의선명함과지면그림자의적절함을따로판정한다.
5. F6에서원경이과도하게차거나뭉치지않는지본다. 전체문맥은Composite만표시한다.

- [x] 사용자 형상 후보 선택: 후보2 remap 일반 기본식 채택(근경 선명도 해결 승인은 아님)
- [ ] 사용자 Detail 그림자 필요성/외형 판정(현재Base유지)
- [ ] 유망 후보가 생긴 뒤 다른 타입/이동/태양각/성능 검증

## 2026-09-22 후속 채택: 무엇을 해결했고 무엇이 남았나

**사용자 결정:** “정규화 remap 버전이 더 외곽이 살아나는 느낌”을 근거로 후보2를
일반 기본식으로 채택했다. 동시에 “근경 선명도 개선이 된 건 아니다”라는 한계를 명시했다.
이는 앞 절의 에이전트 보류 권고 이후 내려진 사용자 선택이다.

### 기존 문제 → 변경 원리 → 결과

기존에는 형상 S에 높이·Weather·밀도 배율 A를 곱한 뒤 Detail을 절대량으로 뺐다.
같은 형상이라도 A가 낮아지면 감산량의 상대 비중이 커지고, 경계 가중치도 A×S에 의존했다.
따라서 형상 침식과 밀도 크기 조절이 얽혀 있었다. 단순히 Detail 해상도를 올려서는 이 관계가 바뀌지 않는다.

변경 후에는 S에서 e를 정하고 `saturate((S-e)/(1-e))`를 계산한 다음 A를 곱한다.
여기서 remap은 **형상값의 입력 구간 [e,1]을 출력 구간 [0,1]로 대응시키는 것**이다.
예를 들어 e=.3이면 S=.3/.65/1이 0/.5/1이 된다. 단순 감산의 0/.35/.7과 달리
남은 형상의 상단 값이 유지된다. e는 위치마다 바뀌며 e≥1은0이다.
이것은 화면 픽셀을 날카롭게 만드는 후처리나 삭제된 최종 Density transition width가 아니다.

큰 구름 지원 영역·두께 설정·높이 곡선은 유지하지만, 각 위치의 최종 밀도와 불투명도는 바뀐다.
초기 비교의 Near75 외곽 전이60→46px는 이득이고, 골 대비.350→.150/전이48→67px는 손실이다.
따라서 해결한 것은 **침식과 밀도 배율의 연산 분리 및 사용자가 선호한 외곽 표현**이며,
해결하지 못한 것은 **근경 전체의 또렷한 파임·내부 명암·이동 중 표현**이다.

일반 셰이더는 후보2를 기본으로 쓰며 시험0/1/2와 옛 코어 실험은 명시적 정의로 보존한다.
Base 그림자, Detail64³, 대기2배, High, CB/UI/schema, 원본8JSON은 유지한다. push 없음.
Detail core protection의0은 이제 “새 remap에서 보호 없음”이고 .65는 “같은 remap에서 코어 침식량35%”다.
옛 감산식의 .65와 같은 화면이라는 뜻은 아니다.

### 채택 검증

새 Release 출력은 `build/captures/cloud-near-clarity/12772-11835781`이다.
재실행 이유는 새 일반 컴파일이 이미 비교한 후보2와 같은지, 원본·복원 계약과 채택 후 비용을 확인하기 위함이다.
ROI는 옛 식0에서 고정하고 일반 실행=후보2를 비교했다. 과거 출력은 덮어쓰지 않았다.
일반↔후보2 HDR max는 F5 .00015622, Near75/F6 0으로 기존 허용치 안이다.
GPU 진단과 독립 분석 PASS, 원본8JSON 불변, 최종 진입 화면 복원 차이0.
밀도 경계/지원 영역/보호0,1/Detail0/Base상한/참조 수렴/그림자만 변경 시 View T 불변 검사도 통과했다.
Debug/Release 빌드 PASS, 관련 CTest 각각17/17 PASS(Debug25.69초, Release19.12초).
로그: `build/remap-adoption-debug-regressions.log`, `build/remap-adoption-release-regressions.log`.

Release OFF, RTX4080 SUPER/driver32.0.15.9186, 1920×1080, 현재 Cumulus/환경3,
시간71/바람0, VSync/UI/preview off. 각 조건120프레임 예열 뒤 중복 없는600 GPU 표본.

| 정지 시점 | 옛 식 Cloud p95 | remap Cloud p95 | 증가 | remap Frame p95 |
|---|---:|---:|---:|---:|
| F5 | 3.326ms | 4.435ms | +1.109ms | 5.091ms |
| Near75 | 1.555ms | 1.884ms | +.329ms | 2.349ms |
| F6 | 3.492ms | 3.621ms | +.129ms | 4.345ms |

모두 Cloud≤10ms/Frame≤16.67ms. 한 번의 정지 비교이며 이동/다른 타입 성능 보장은 아니다.
밀도가 바뀌면 활성 표본·조명 계산·조기 종료도 달라지므로 나눗셈 한 번의 비용으로 해석하지 않는다.
`VCLOUD_CLARITY_MEASURE_PERFORMANCE=1`에서만 이 계측을 추가하며 CSV에 전체 표본을 보존했다.

## 참조 DirectX11 프로젝트 코드 비교

조사 대상은 [chihirobelmo/volumetric-cloud-for-directx11](https://github.com/chihirobelmo/volumetric-cloud-for-directx11),
2026-09-22 조회 commit `9702a0d17564a22977b56b03a72186b36140648c`다.
소스를 읽은 비교이며 같은 장면·카메라·노출로 두 실행을 대조하지 않았다. 사용자가 본 영상의 commit도 미확인이다.
아래는 **확인한 구조 차이**와 **선명도에 대한 가설**을 구분한다.

| 항목 | 참조 코드에서 확인한 사실 | 현재 프로젝트와 해석 |
|---|---|---|
| 높이 처리 | 높이 곡선을 `RemapClamp(dense,1-layer,1,0,1)`의 문턱으로 사용 | 우리는 높이 곡선을 밀도 배율로 곱한다. 참조는 약한 형상 자체를 잘라내고, 우리는 넓고 옅은 밀도를 남길 수 있다. 단, 아래 계보 통계상 외곽 전체의 원인으로 단정할 수 없음 |
| 다중 대역 | Base G/B/A와 Detail R/G/B 각각에 연속 remap | 우리는 대역을 가중합한 scalar로 한 번 침식한다. remap이라는 이름은 같아도 e의 분포와 파임 연결 구조가 다름 |
| 광학 밀도 | 최종 형상/64를 Beer–Lambert에 직접 사용 | 우리 현재 Cumulus는 최종 밀도×.003065/m. 형상1의 소멸량 상한은 참조.015625/m로 약5.1배지만 실제 평균 밀도/점유 길이는 다름. 숫자를 그대로 복사할 근거는 아님 |
| 태양 차폐 | Detail 포함 CloudDensity를 400m 구간/8표본(간격50m)으로 사다리꼴 적분 | 우리는 최대20km 도메인 제한의 Base Deep Cache/cone. 차이는 Detail 여부뿐 아니라 차폐를 보는 길이와 공간 필터도 포함 |
| 조명 | 직접광 적분 뒤 irradiance 환경광×Alpha를 더함. Powder 곱은 주석 처리, Energy의 HG 인자는 결과에 쓰이지 않음 | 우리 Direct/Rim/Sky/Ground/Multiple과 같은 조명 모델이 아니다. 세부 명암이 비슷해야 한다는 전제 불가 |
| 해상도/간격 | Base128³/Detail32³. 실제 near render target 가로1024, 근경10km 내 기본50m/최대2000회(높이 밖 거리 skip 가능), FXAA 후처리 | 우리는 Detail64³/full1920/High100m·512. 참조가 무조건 더 높은 텍스처·화면 해상도라서 선명한 것은 아님. 우리50/25m 비교는 이미 효과 제한을 확인 |
| 구름 대기 합성 | 읽은 RayMarch/merge 경로는 직접 구름 산란·Alpha 합성이고 별도 구름 Air T/L 감쇠가 없음 | 우리 구름 Air T/L과 승인 거리2배는 대비를 바꿀 수 있음. 다만 우리 AP-off에서도 흐림이 남아 대기만 원인이라고 볼 수 없음 |

밀도/높이/Detail/태양 적분의 근거:
[RayMarch.hlsl](https://github.com/chihirobelmo/volumetric-cloud-for-directx11/blob/9702a0d17564a22977b56b03a72186b36140648c/VolumetricCloud/shaders/RayMarch.hlsl#L248).
실제 리소스/근경 해상도의 근거:
[VolumetricCloud.cpp](https://github.com/chihirobelmo/volumetric-cloud-for-directx11/blob/9702a0d17564a22977b56b03a72186b36140648c/VolumetricCloud/src/VolumetricCloud.cpp#L129).
합성과 후처리 근거:
[MergePrimitiveAndCloud.hlsl](https://github.com/chihirobelmo/volumetric-cloud-for-directx11/blob/9702a0d17564a22977b56b03a72186b36140648c/VolumetricCloud/shaders/MergePrimitiveAndCloud.hlsl),
[PostAA.hlsl](https://github.com/chihirobelmo/volumetric-cloud-for-directx11/blob/9702a0d17564a22977b56b03a72186b36140648c/VolumetricCloud/shaders/PostAA.hlsl).

연속 remap은 입력과 문턱이[0,1]이면 유효 문턱 `1−∏(1−e_i)`의 한 remap으로 해석할 수 있다.
따라서 “세 번이니까 세 배 선명”이 아니라 대역 합성의 통계·공간 구조가 다르다는 뜻이다.
이번 조사는 참조 셰이더를 실행 경로로 복사하거나 일반 노이즈/높이 설정을 바꾸지 않았다.

## 다음 근경 선명도 작업의 우선순위 — 아직 구현하지 않은 제안

1. **옅은 외피가 어디서 생기는지 분리한다.** 새 remap의 기존 계보/Alpha/AP-off를 먼저 재사용한다.
   같은 파임을 가로지르는 3D 단면에서 S, 높이 profile, Weather 배율, 최종 밀도를 나란히 보고,
   실제 High 가시 기여가 낮은 밀도의 긴 구간에 퍼지는지 확인한다. 낮은 S와 낮은 높이 배율을 구분한다.
   단순 불투명도 증가와 비교하지 말고 중심 위치/구름 부피/파임 깊이/국소 전이 폭을 함께 기록한다.

2. **우선 대역의 결합 방식만 비교한다.** 기존 Detail 표본의 가중합으로 e를 만드는 방식과,
   각 대역의 문턱을 연속 remap으로 결합하는 방식을 제한된 진단 후보로 비교한다.
   같은 noise/크기/높이/조명/적분을 사용하고, 참조 코드의 계수는 그대로 복사하지 않는다.
   기준 영역의 평균 제거량을 맞춘 비교를 함께 두어 침식 강도 증가와 공간적인 파임 구조의 차이를 분리한다.
   먼저 고정 3D 단면의 연결된 파임·외피 두께를 보고, 화면 Alpha와 AP-off 대비를 확인한다.
   기존 E06 크기/E09 Base 대비·옥타브/E11 코어 보호와 다른 입력 연산이다. 몸체 소실이나 파편화만 늘면 기각한다.

3. **높이 감쇠가 원인인 영역에 한해, 높이의 역할만 한 번 비교한다.** 현재 곱셈식 대 높이 기반 형상 문턱식을
   작은 진단 범위에서 비교한다. 기존 Base 대비·옥타브·최종 전이 폭 재시도가 아니다.
   13-4E에서 높이/profile 이중 threshold가 상단 소실과 평탄화를 만든 이력이 있으므로,
   곱셈과 문턱을 중복 적용하지 않고 기존 자료의 실패 조건을 함께 확인한다.
   이 변경은 이번 Detail 단계보다 큰 **Base 지원 형상 변경**이다. 성공을 보장하지 않으며,
   Base 상한·View/Light 일치·empty skip/Weather support 계약을 새 형상 기준으로 다시 검증해야 한다.
   높이·배치 보존 요구에 어긋나는 후보는 일반에 적용하지 않는다.

4. **형상은 있는데 명암이 묻힌 경우에만 국소 태양 차폐를 분리한다.** 같은 가시 표본에서
   Base 긴 구간 / Detail 긴 구간(기존 자료) / 태양 방향 앞400m만 Detail인 참조를 비교한다.
   새 참조는 `tauHybrid=tauBase(전체)−tauBase(앞400m)+tauDetail(앞400m)`이며 동일 구간의
   직접 적분으로 먼저 검사한다. 원거리 Base 차폐를 남겨 단순한 전체 밝아짐과 국소 파임 표현을 구분한다.
   이는 옛 카메라거리4–8km NearDetail 혼합과 다르다. 작은 ROI 참조만 만들고 일반 고비용 직접 적분은 추가하지 않는다.
   이득이 없으면 종료하며, 이득이 있어도 캐시 근사·태양각·이동·비용 검증 뒤 사용자 선택이 필요하다.

기존25m 계보를 새 촬영 없이 다시 계산하면 remap Near75의 기여 가중 높이 profile은
외곽.909/골.628/몸체.894이며, profile<.5 구간의 기여 비중은0%/23.6%/3.1%다.
외곽의 기여 가중 S는.350, 최종 밀도.188, shaping된 Base는.608이다.
이는25m 계보 적분의 통계로 실제 High/3D 법선 외피 두께의 증명은 아니지만,
**높이 감쇠만 외곽 흐림의 주원인이라고 단정할 수 없다는 반대 근거**다.
따라서 순서는 1→2, 높이 기여가 확인된 영역만3, 형상 통과 후4다.
해상도/스텝/Detail 크기/Powder/대기 격자는 재실행하지 않는다.
지금의 remap 채택과 별개로 근경 선명도 해결 체크는 계속 미완료다.

## 이전 기록을 읽는 순서

2026-09-22 후속 실행: 위 대역 결합 제안은 [E17 실험](stage15-cloud-detail-band-composition.md)으로
완료했다. 제거량 보정 후 근경 대비 이득이 없어 일반 가중합 remap을 유지한다.
참조의 채널 내부 fBm과 현재 단일 Worley 차이를 다음 가설로 기록했다. 높이/태양 후보는 미실행이다.

- [품질 후속 실험 인덱스 E01–E16](stage15-cloud-quality-followups.md): 가설·결과·채택/기각·재실행 조건.
- [근경 경계 분석](stage15-cloud-boundary-analysis.md): 왜 전체폭833px를 단일 윤곽 폭으로 볼 수 없는가.
- [대기 합성/거리2배 승인](stage15-cloud-aerial-composition.md): 대기와 밀도 원인을 분리한 근거.
- [방향광 00–05](stage15-directional-cloud-lighting.md), [림/그림자 후속](stage15-cloud-rim-lighting.md): 이전 조명 실험.
- 로컬 학습 문서 `notes/15단계-근경선명도-침식그림자검증.md`: 수식의 의미와 실행/화면 확인 방법.

9/21 삭제된 옛 이미지는 복구된 것처럼 링크하지 않는다. 인덱스의 보존 텍스트/수치와
현재 남아 있는9/22 비교 자료를 구분해서 읽는다.
