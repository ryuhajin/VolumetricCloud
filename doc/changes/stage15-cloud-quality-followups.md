# Stage 15 — 슬롯 프리셋과 구름 품질 후속 개선

## 2026-09-22 완료 실험 인덱스와 중복 방지

`실행 완료`와 `사용자 채택`은 다르다. 미채택 후보를 현재 문제 해결로 확대 해석하지 않는다.
원본 `codex-cloud-near-far-execution-plan.md`는 수정하지 않았다. 아래 근거 접두사는 로컬
`notes/capture-archive/2026-09-21/`이다. 이미지·바이너리는9/21 삭제했고 텍스트만 남았다.
옛 HTML의 이미지 링크는 현재 열 수 있는 증거가 아니다. 보존 원본 경로/SHA-256은 manifest.csv 참조.

| ID | 가설 / 조건·값 | 결과 | 사용자 판정·현재 상태 | 한계 | 재실행 조건 | 보존 근거 |
|---|---|---|---|---|---|---|
| E01 | 대기 미작동? AP-off,Air T/L,4/16/32구간,F5/time71 | 감쇠 존재. Air T 4–32 MAE .000457; 대기 제외해도 넓은 경계 잔존 | 실행 완료, 일반4 유지 | 5km 이내 기여 없음, 대표거리 오차 미검증 | 대기코드/입력 변경 또는 다른 실패 | build-captures/cloud-clarity/diagnostics-34356-20693031/README.md |
| E02 | Detail 해상도? 32³/64³ | 넓은 근경 흐림 미해결 | **64³ 사용자 채택** | 밀도/명암 원인 확정 못함 | texture 생성/조회 변경 | build-captures/cloud-detail-resolution/37548-28189640/README.md |
| E03 | 연무량? Turbidity1/2/3×Mie높이1.2/2/3km | 감쇠 증가, 흰 후광. g변경 후 재비교 완료 | 격자 미채택, 높이2km는 이후 명시 채택 | 평균T는 거리별 대비 아님 | 새 실패/범위 밖 조건/모델 변경 | build-captures/cloud-aerial-tuning/23232-29005640/README.md; 같은 분류37652-31606812/README.md |
| E04 | 후광 집중? g .8/.6/.4, 이후 사용자 .3 | 태양 집중 밝기 감소 | **g .3, 높이2km 채택**, 원경 미완료 | 하늘/환경광도 반응 | 위상 모델/입력 변경 | build-captures/cloud-aerial-phase/38612-30987500/README.md; 본 문서9/19 승인 |
| E05 | shaping 경계? .432/.48/.528 | 불투명도 변화, 경계 개선 제한 | 실행 완료, .48 유지 | ±10% 범위 | 다른 원인 근거/코드 변경 | build-captures/cloud-shape-tuning/25648-32523015/README.md |
| E06 | Detail 크기? 1800/2000/2200m | 굴곡 이동, 넓은 흐림 해결 근거 부족 | 실행 완료, 미채택 | 선명도와 위치 이동 구분 | 좌표/침식 변경 또는 새 실패 | build-captures/cloud-detail-size/38492-33754796/README.md |
| E07 | 전이 폭? 1/.75/.5 | 사용자 평가상 제한적 | **기능 제거 요청 반영** | 현재 수식에 없음 | 별도 재도입 지시와 새 근거 필요 | build-captures/cloud-density-transition/36348-34599671/README.md |
| E08 | View 부족? 100/50/25m | Near75 High–25m T MAE .000513, 부드러움 잔존 | 실행 완료, High 유지 | 선택ROI/광선 | View코드/범위 밖 조건/새 실패 | build-captures/cloud-distance-step/15248-36299828/README.md; cloud-near-far/42308-45389781/README.md |
| E09 | Weather/Base 문턱·대비·옥타브? 단면/5후보 | 전달 확인, 대비·옥타브 증가로 미해결 | 실행 완료, Original 유지 | 모양 분포와 경계 원인 구분 | noise/Weather 변경 또는 새 근거 | build-captures/cloud-base-weather/34548-38669531/README.md; cloud-base-remap/41620-39338562/README.md |
| E10 | 짧은 점유길이=근거리? F5/F6, 광선 계보 | 먼 접선 반례, 선택근경에서 Detail 큰 감산 | 실행 완료 | 25m 계보는 High 종료 뒤도 포함 | 점유/밀도 정의 변경 또는 새 관찰 | build-captures/cloud-path-length/39388-42348312/README.md; cloud-near-far/42308-45389781/rays.csv |
| E11 | Detail 코어 가중치·상한, 네 타입 | 몸체 회복, 얇은 타입 과한 채움. 내부 명암 개선 아님 | 타입별 슬라이더 채택 후 **2026-09-24 제거**(저장값 전부 0, 미사용) | 코어는 공간경계 아닌 밀도추정 | 코어 수식/타입 입력 변경 | build-captures/cloud-detail-core/46784-46441093/README.md; cloud-detail-core-types/2688-2612437/README.md |
| E12 | 대기 조회·합성 경계? 상향ray2~40km | T역전0, 합성max .000473 | 실행 완료, 깊이분포 근사 당시 미검증 | 유한 광선/대수계약 | 실제 깊이분포 비교는 새 범위E14 | build-captures/cloud-near-far/42308-45389781/aerial.csv |
| E13 | 림/캐시/소멸계수/Sky fill/Multiple | 비교 완료, 캐시80/79·Rim2 채택 | 실행 완료/부분 사용자 채택 | 당시Urban과 현재저장값 다름 | 입력·코드 변경 또는 새 실패 | captures/stage15-directional-lighting; [림](stage15-cloud-rim-lighting.md), [방향광](stage15-directional-cloud-lighting.md) |
| E14 | 대표거리/Aerial LUT 오차 A/B/C/D | 4시점 수렴/계약 통과 | 2026-09-22 거리2배 사용자 채택 | 참조도 태양T/Multi LUT 공유 | 이번 결과에 명시 | [합성 진단 기록](stage15-cloud-aerial-composition.md) |
| E15 | 근경 Alpha 경계와 침식 계보 | 전체폭64행 중 왼쪽64측정/오른쪽64미측정, 변화길이 중앙832.5px | 분석 실행 완료, 형상 미채택 | 영상상 첫교차/역사적2광선은 현재전체원인 증명 아님 | 동일경계의 단계별 인접광선 계보 추가 | [근경 경계 분석](stage15-cloud-boundary-analysis.md) |
| E16 | 정규화 형상 침식/인접9광선/현재Cumulus 태양Detail 차이 | 외곽축소와골대비손실혼재 | **후보2 remap 사용자 채택**, Base 그림자 유지, 근경 선명도 미해결 | 작은영역/형상과조명분리, 기존전체폭과다름 | 일반=후보2 채택 검증 및 새 참조 구조 비교 | [근경선명도](stage15-cloud-near-clarity.md) |
| E17 | 동일 Detail 대역 가중합 대 연속 remap/평균 제거량 일치 | k1은제거량약9%감소, 보정k1.09605도외곽/골대비이득없음 | 실행 완료, 일반 가중합 remap 유지 | 동일대역·가중치/고정단면, 참조전체식재현아님 | 대역 생성/공간 구조가 바뀔 때 | [대역 결합](stage15-cloud-detail-band-composition.md) |
| E18 | Detail 원본에2f/4f 추가,64³/2km·가중합 remap 고정 | 고주파 비율 증가하나 분산 감소, 보정 후보 파임 대비 약20% 감소 | **사용자 무보정 fBm 후보1 일반 채택**, 선명도 해결은 별개 | 평균 제거량 일치≠분포 일치, 이동 MAE는 깜빡임 점수 아님 | 일반 채택 경로/비용 검증, 이후 새 입력 변경 근거 | [원본 주파수](stage15-cloud-detail-spectrum.md) |
| E19 | 근경 전용 두 번째 Detail 조회(tile500/250, 평균0 섭동, 화면 footprint로 0), c4는+50m step | c1 근경 굴곡 증가·원경 불변(컴파일 바닥 .00031), c3/c4 천정 방사 줄무늬, c1 비용 최대+.56ms | **2026-09-24 런타임 채택**: 전용 Worley 64³+DUAL+warp, F2 5슬라이더 타입별 저장(기본 strength 0). SOURCE 0/1/2·128³·ROTATE·FINE·SHADOW·SHELL·Extinction 상한 확대 기각 | 작은 tile의 시선 방향 번짐 잔존, 타입별 값·화면 승인 사용자 대기 | tile/가중 규칙 또는 조명 모델 변경 | [근경 미세 Detail](stage15-cloud-near-micro-detail.md) |

재실행은 관련 코드·입력 변경, 기존 범위 밖 조건, 새 관찰 실패 중 하나를 먼저 기록한다.
같은 후보를 단순히 다시 찍는 것은 허용 범위에 포함하지 않는다.

## 2026-09-18 F1/F4 독립 슬롯 프리셋

일반 시작값은 **Cumulus + F4 3번 + High**다. F1은 Stratus/Cumulus/Altocumulus/Custom 형상, F4는 1(가을 아침)/2(해변 노을)/3(밝은 한낮)/4(분홍·라벤더) 조명·환경을 선택한다. 기존 Urban/Meadow/Snow scene은 역사적 테스트 호출에만 남긴다.

양쪽 `Save Preset`은 현재 선택 슬롯에 저장한다. 수정해도 선택 슬롯은 유지한다. 재선택은 저장값(없으면 내장값)을 읽으며 저장하지 않은 편집을 버린다. F1/F2의 formation 편집은 F1에, F3 조명·대기·지면·톤 편집은 F4에 저장한다. Built-in/Saved/Modified와 실제 파일 경로를 표시한다. 잘못된 JSON은 현재 값/선택을 보존하고 오류를 표시한다. 초기 로드 실패는 내장 시작값으로 남는다.

저장 루트는 소스 shaders 기준 `captures/noise-lab`이다. 구름 schema4 파일은 `types/stratus.json`, `types/cumulus.json`, `types/mixed.json`(표시명 Altocumulus), `custom-cloud.json`이다. schema1~3 이관을 유지한다. 조명 schema1은 `lighting/1.json`~`4.json`이며 slot 번호와 이름이 있는 숫자 필드(예: `tone.exposureEv`)를 기록한다. 임시 파일 flush 후 원자 교체하며 실패 시 기존 파일을 유지한다.

조명은 Light/Environment의 비패딩 설정, Atmosphere의 물리 계수·태양 각도, Ground 반사율·bounce, Tone 모드·EV·WB, 표면 그림자 strength/floor를 소유한다. 카메라·바람·재생 시각·진단·고정 품질·캐시 파생값은 제외한다. 적용 시 태양 재생을 끄고 각도를 적용하며 진단 선택은 보존한다. Deep Cache 512·80/79와 CB ABI는 유지한다. F4는 Weather/formation을 변경하지 않는다.

Custom은 첫 일반 실행에서 기존 파일을 `.before-snow.bak`으로 보존하고 기존 SnowOvercast 형상을 복사한다. 완료 표식 `snowDefaultInitialized:1`을 Custom 본문과 원자적으로 저장한다. 이후 저장에도 표식을 유지하므로 재실행 시 사용자 편집이 사라지지 않는다. 파일이 없을 때의 Custom 기본값도 Snow다. 테스트는 임시 루트를 사용하고 일반 초기화를 호출하지 않는다.

형상 후보: Stratus local thickness450~850m/base XZ22000m, Cumulus2000~3200m/base XZ12000m, Altocumulus650~1150m/base XZ4200m·고도3000m. 세 타입 density shaping .70, Base octave1.50 및 High 적분은 유지한다. Custom은 Snow 원본이다. 형상 알고리즘은 변경하지 않는다. 최종 시각적 채택은 사용자 대기다.

snapshot44는 formationSlot/formationSource와 lightingSlot/lightingSource를 별도로 기록한다. UI에는 과거 scene 이름을 노출하지 않는다.

## 검증 기록
사용자 화면 승인은 대기다. 기존06/07 승인으로 간주하지 않는다.

## 2026-09-18 구현·검증 결과

- Debug/Release 빌드 성공, 기본 VCLOUD_STRICT_VALIDATION=OFF.
- 전체 Release CTest49/49 통과(368.45초). 이후 JSON 문법 강화와 마지막 튜닝에 대해 영향 범위 Release8/8(12.79초), Debug5/5(3.29초)를 재검증했다.
- 8슬롯 JSON round-trip, 파일 격리, malformed/schema/slot/range 거부, 저장 I/O 실패 시 기존 파일 보존, Custom 이전schema 이관 및 일회 초기화 후 편집 보존 통과.
- PresetSlotsSmoke에서 F4 적용이 formation·Weather generation·진단 선택을 보존하고 양쪽 저장이 반대 영역을 보존함을 확인했다. 고정512·80/79를 사용한다.
- 1920×1080 16조합 캡처 생성, HDR finite와 D3D11 오류 없음. 최종 후보는 `captures/preset-slots/6011531/`의 PNG와 사례별 JSON이다. `comparison.html`은4×4 비교, `README.md`는 원본 이미지 목록이다. 중앙 검은 사각형은 기존 진단 건물이며 이번 변경에서 새로 추가하지 않았다.
- 첫 후보 Altocumulus의 큰 판 연결을 줄이려고 Coverage .68→.42, Weather threshold .50→.35로 조정했다. Stratus Coverage .60. 3번 sun1.7/EV.9, 4번 고도12/EV1.3/solar(1.6,1.3,1.8)로 밝기를 보완했다. 파라미터만 바꾸었으며 셰이더 수식은 변경하지 않았다.
- 현재 `captures/noise-lab/custom-cloud.json`은 최종 캡처의 Snow formation과 동일하게 교체했다. 이전schema3 원본은 `custom-cloud.json.before-snow.bak`에 바이트 그대로 보존·검증했다. 이후 실행은 완료 표식으로 이 파일을 초기화하지 않는다.
- 매 프레임 편집 비교는 이름이 있는62개 필드의 고정 배열을 사용하여 힙 할당을 피한다. 실제 JSON은 의미 있는 필드명으로 기록한다.
- 실제 마우스로8슬롯을 조작하는 사용자 확인과 최종 형상·색감 승인은 대기다. 새로운 이미지의 픽셀 일치를 과거 baseline과 요구하지 않는다. Stage07 완료/승인으로 처리하지 않는다.

## 2026-09-18 프리셋 보존과 구름 거리감 — 문제·해결 추적

### 현상과 가설
사용자 화면에서 가까운 구름은 경계가 넓게 퍼지고 먼 구름은 상대적으로 선명해 보인다. 목표는 근경 형태·명암을 유지하고 원경은 대기로 대비가 줄어드는 화면이다. 긴 구름 경로의 누적 불투명도, 경계의 화면상 축소, 낮은 View tau와 대기 강도/적분 오차는 조사 가설이며 확정 원인이 아니다.

### 조사와 선택
실무 자료: Unreal Sky Atmosphere의 Mie 높이/거리 배율(https://dev.epicgames.com/documentation/en-us/unreal-engine/sky-atmosphere-component-properties-in-unreal-engine), Unity HDRP의 Shape/Erosion 분리(https://docs.unity.cn/Packages/com.unity.render-pipelines.high-definition%4017.6/manual/volumetric-clouds-volume-override-reference.html), Ghost of Tsushima의 구름 AA/연무 분리(https://www.advances.realtimerendering.com/s2021/jpatry_advances2021/index.html). 먼저 기존 대기와 밀도를 분리해 진단한다. 화면 블러/거리별 밀도 변경/Temporal/Near Detail 복구는 채택하지 않는다.

### 00 체크포인트와 저장 문제
기존 captures/noise-lab JSON은 Git에서 제외되어 다른 PC에서 clone/build하면 사용자 룩이 사라졌다. CMake는 셰이더만 복사했다. 사용자 요청으로 현재 소스 checkpoint e739ce646f794619d131f6c75a047d344b48ebb4를 원격 feature/stage15-final-quality에 push하고 원격 SHA를 확인했다. 변경 전 Debug/Release 빌드 및 OFF Release CTest49/49 통과(285.52초). 원본8파일은 captures/cloud-clarity/baseline-20260918-204704에 SHA256과 보존했다.

### 00 해결 구현
활성8파일을 Git 관리 presets/types/{stratus,cumulus,mixed}.json, presets/custom-cloud.json, presets/lighting/{1,2,3,4}.json으로 바이트 그대로 이관한다. 개발 읽기/Save Preset은 CMake가 지정한 원본 presets를 사용하고 소스 루트가 없으면 exe 옆 presets 하나를 사용한다. 현재 작업 디렉터리/셰이더 위치와 무관하다. 접근 오류/개별 파일 누락 때문에 다른 루트에 저장하지 않는다. 누락은 내장값, 불량 선택은 현재값 보존, 저장 실패는 기존 파일 보존 정책을 유지한다.
일반 시작의 Snow 일회 교체를 제거했다. Custom과 다른 JSON은 실행만으로 쓰지 않는다. 기존 표식과 schema4/lighting1은 호환을 위해 유지한다. 일반 시작 슬롯은 Cumulus+환경3이다. 이전 Snow 초기화 설명은 위의 역사 기록이며 이후 일반 실행에는 적용되지 않는다.
CMake의 별도 PresetSync 타깃이 매 build 8개만 copy_if_different로 exe 옆에 배치한다. JSON만 수정해도 갱신되며 배포 사본을 수정한 뒤 다시 빌드하면 소스 원본으로 덮어쓴다. 캡처/스냅샷/UI/과거 실험 파일은 captures에 남는다. 구름 렌더 수식·CB ABI·승인 파라미터는 변경하지 않는다.

### 순차 진행/승인 상태
00 이관 검증 및 push → 01 실제 구름 거리 Air T/Air L/대기 제외 진단과4/16/32적분 비교 → 사용자 확인 → 02 Mie 높이 UI와9조합 → 사용자 확인 → 03 기존 형상 파라미터 비교. 밀도 전이 구간/대기 거리 배율 추가는 필요할 때 별도 계획이다. 현재 화면 승인/Stage07 완료 아님. 검증 결과와 사용자 피드백을 아래에 계속 추가한다.

### 00 자동 검증 결과
Debug/Release 빌드 성공. Release 관련10/10(5.43초), Debug8/8(4.95초) 통과. 원본/백업/Release8파일 SHA256 동일. JSON에 임시 공백만 추가한 무코드 재빌드에서 copy 갱신 확인 후 원본 바이트와 사본 복원. PresetDeployment는 source 우선/소스 없음 exe fallback/임의CWD/내장 fallback/선택루트 저장과 실제8파일 로드를 검사한다. PresetSlotsSmoke는 초기화 표식0인 Custom의 시작 전후 바이트 동일을 확인한다. 기존 슬롯 격리·원자 저장 실패 회귀도 통과. 화면 룩은 변경하지 않았으며 사용자 확인 대기.

### 01 구현과 사용자 확인 방법
F4 Cloud view에 Cloud without aerial perspective(90), Air T at cloud depth(91), Air L at cloud depth(92)를 추가했다. 기존82/83 폐기 번호는 유지한다. 일반 Composite와 프리셋 값은 그대로다. Stage14CB renderFlags.x에0/91/92를 전달해 Air 진단만 기존 debug exposure/channel로 표시하고 EV/WB/ACES를 우회한다. no-air는 기존 Tone을 유지한다. Air 무기여 영역은회색이며 검은색을구름없음으로해석하지않는다.

사용자 확인(미승인):
- [ ] Release 실행 후 F4 Atmosphere view=None, Cloud view=Composite에서 F5와 Cumulus/환경3을 확인한다. 현재 저장된 JSON을 쓰므로 과거 내장 기본값과 화면이 다를 수 있다.
- [ ] 카메라/바람을 고정하고 Cloud without aerial perspective를 선택한다. 구름 앞 공기만 제거되므로 원경 명암이 더 강해질 수 있다. 근경 경계가 여전히 넓게 퍼지면 밀도 형상의 영향이다. 하늘/건물이 통째로 바뀌거나 구름 모양이 움직이면 비교 조건 또는 구현 실패다.
- [ ] Air T at cloud depth를 선택한다. RGB0~1에서 밝으면 공기 통과율이 높고 어두우면 감쇠가 크다. 회색은구름 기여없음이다. Cloud Depth의밝은원경과함께비교하고태양방향/고도차이때문에화면위치만으로단조성을요구하지않는다.
- [ ] Air L at cloud depth는 구름 자체 밝기가 아니라 카메라까지 누적된 공기빛이다. F4 Debug exposure/channel을조절한다. 표시식1-exp(-L*exposure)이며 F3 EV는 영향을 주지 않아야 한다. 회색 무기여 표시와 빛이 낮아 검은 영역을 구별한다.
- [ ] 4/16/32 Composite 캡처를 같은 배율로 비교한다. 32도정확한해가아닌비교기준이며자동채택하지않는다. 근경경계/원경색차이를사용자가판정한뒤02 Mie높이UI/9조합으로이동한다.

### 01 실험 조건과 결과 — 2026-09-18
프리셋 이관 커밋 `99b33bfe3398052fc61fb00c314daee05e23a014`는 원격 SHA 일치까지 확인했다. 이후 진단만 추가했으며 프리셋 값은 변경하지 않았다.

Release/OFF, 1920×1080, UI/VSync Off, Cumulus + 환경 3, F5, FOV 60°, 시각 71초, 바람 0, 태양 재생 Off로 비교했다. 카메라(m)는 (0, 1.7, 60), 목표는 (0, 42, -40)이다. 정확한 formation/lighting JSON, PNG, 원본 RGBA16F와 측정 보고서는 로컬 `build/captures/cloud-clarity/diagnostics-34356-20693031/`에 보존했다. CTest 작업 디렉터리가 build이므로 캡처도 build/captures에 생긴다. 중앙 검은 물체는 기존 진단 건물이다.

| 항목 | 결과 |
|---|---|
| 변경 전 셰이더 대비 Composite | HDR 정규화 MAE 5.15e-9 / 최대 0.000156, LDR MAE 1.83e-8 / 최대 1/255: 기존 tolerance 통과 |
| Air T: 4구간 대 32구간 | 선형 RGB MAE 0.000457 / 최대 0.00244 |
| Air T: 16구간 대 32구간 | MAE 0.0000208 / 최대 0.000488 |
| Air L: 4구간 대 32구간 | MAE 0.000172 / 최대 0.00250 |
| Air L: 16구간 대 32구간 | MAE 0.00000826 / 최대 0.000183 |
| 대표 거리 5~15km | 평균 Air T(G) 0.900, 구름 불투명도 0.453 |
| 대표 거리 15km 이상 | 평균 Air T(G) 0.694, 구름 불투명도 0.598 |

0~5km 구름 기여 픽셀은 없었다. 따라서 이 실험의 “근경”은 사진 촬영 거리의 근경이 아니라 화면에서 상대적으로 가까운 구름이다. 거리 구간의 불투명도 차이는 경로 누적과 일치하지만 서로 다른 구름을 비교하므로 원인 하나를 입증하지는 않는다.

정지/회전 각각 96프레임에서 초기 32프레임을 제외하고 고유 GPU 샘플을 수집했다. 4/16/32구간의 회전 시 Atmosphere p95는 각각 0.00717/0.01229/0.01638ms였다. 정지 시 LUT 재생성이 없어 이 패스는 0ms였다. Frame p95는 정지 3.84/4.36/3.23ms, 회전 2.88/3.25/2.80ms로 짧은 측정의 변동이 크다. 이 수치로 전체 프레임 성능 우열을 판정하지 않는다.

Debug/Release 빌드 성공. Release 관련 8/8(20.81초), 마스크/톤 독립성/거리 통계 보강 후 CloudClarity 재검증 통과(11.92초). 최종 Debug 관련 8/8(14.90초) 통과. 셰이더 컴파일·핫 리로드 회귀, HDR finite, D3D 오류 검사, 진단 왕복 Composite tolerance, 무구름 배경/중립 마스크, Air 진단의 EV/WB 독립성도 통과했다. GPU 테스트는 직렬로 실행했다.

### 01 해석과 채택/보류 이유
- 기존 대기는 원경에서 더 큰 감쇠를 적용하고 있다. “거리 대기가 없다”는 가설은 이번 조건에서 맞지 않는다.
- 대기 제외 이미지에도 큰 구름의 부드러운 경계가 남는다. 밀도 전이와 투영 크기의 영향을 우선 조사할 근거이며, 정확한 파라미터 원인은 아직 확정하지 않았다.
- 4구간의 평균 수치 오차가 작아 일반 실행의 4구간을 유지한다. 32구간도 정답이 아니라 비교 기준이다. 다른 태양 각도/고도/연무에서는 재검증이 필요하다.
- 진단 기능만 채택했다. Mie 높이 UI·9조합 및 밀도 파라미터 조절은 계획상의 사용자 화면 확인 이후 진행한다. 기존 프리셋, High 적분, Base 그림자, Deep Cache 80/79를 유지했다.

### 01 한계와 사용자 피드백
자동 검증 통과와 화면 승인은 별개다. 사용자 제공 첫 사진과 카메라가 완전히 동일하지 않으며, 실제 5km 이내 구름과 이동 중 형상 깜빡임은 이번 고정 조건으로 판정하지 않았다. 사용자 화면 승인 날짜와 선택 후보는 아직 없음. 비교 페이지의 Composite/대기 제외 및 4/16/32 차이를 확인한 뒤 피드백과 다음 단계 결과를 이 문서에 누적한다.

## 2026-09-18 Detail 해상도 비교 — 사용자 확인 전 실험

### 현상 → 가설 → 조사 근거
근경의 부드러운 경계에 저해상도 Detail의 선형 보간이 영향을 줄 수 있다는 사용자 가설을 검사한다. 현재 Base128³/12,000m는 텍셀 간격93.75m, Detail32³/2,000m는62.5m, Weather256²/64,000m는250m다. 밀도값0.1은 거리0.1이 아니며, 텍스처 좌표를0.1씩 건너뛰지도 않는다. 100m 기본 레이마칭과 텍스처 저장 간격은 서로 다른 한계다.

### 실험 조건과 구현
기준 커밋 aacef54에서 테스트 전용 --cloud-detail-resolution-test를 추가했다. 기존 CSDetail 생성기로 같은 시드·주파수·가중치의32³/64³을 새로 만든다. 일반32³, Base128³, Weather256², High100m/512, Base 그림자와 Deep80/79는 유지한다. b6 96B/JSON schema/UI 변경 없음. 프리셋의 Detail world size가2000m가 아니면 임의 보정하지 않고 테스트를 중단한다.

현재8개 JSON을 바이트 그대로 로컬 실험 폴더에 복사하고 실행 전후 동일성을 확인한다. F5/F6,1920×1080,FOV60,시간71초,바람0,태양 재생/UI/VSync Off에서 Composite/대기 제외/Cloud T/tau PNG와 HDR 원본을 저장한다. 비교 페이지는 원본 좌표640×360 확대, RGB 절대 차이8배, 회전·전진24프레임 동기 비교를 제공한다. 회전은 프레임마다 마우스4픽셀, 전진은30m이며 경로는 paths.csv에 기록한다. 10fps 슬라이드 재생은 연속 실제 프레임 캡처가 아니므로 더 빠른 깜빡임까지 검증했다고 간주하지 않는다.

성능은 readback 없이 F5 일반 Composite에서 정지/회전 각각60프레임 워밍업 후 유효 고유 GPU 샘플300개를3회 수집한다. 회전은 렌더마다 마우스0.5픽셀이다. Cloud/Frame 중앙값·p95와 원시 CSV를 보존한다. 생성 시간은 기존 생성기의 Base+Detail 생성과 GPU readback을 포함하므로 Detail 단독 생성 비용으로 해석하지 않는다. GPU 텍스처 데이터는32³=0.125MiB,64³=1MiB이며 드라이버 부가 메모리는 제외한다.

### 자동 검증과 채택 기준
실제 Texture3D 크기/포맷/바이트, 동일 해상도의 두 번 재생성 바이트 동일성, Base hash 불변,32³ 재생성과 기존 Composite tolerance, 원래 GPU 리소스 복원 후 Composite tolerance, HDR finite/D3D 오류,8프리셋 비파괴를 검사한다. 의도적32³/64³ 차이에 픽셀 동일성은 요구하지 않는다.

- [ ] 사용자: comparison.html의 F5/F6에서 같은 경계를 클릭한다. 아래 확대는 같은 원본 좌표다.64³에서 작은 굴곡이 자연스럽고 넓은 경계가 실제 개선되는지 확인한다. 단순 차이8배의 밝음은 개선 점수가 아니다.
- [ ] 사용자: 구름 대기 제외와 Composite를 비교한다. 전자는 형상과 조명을 유지하면서 구름 앞 공기 효과만 제외한다. 하늘까지 완전히 달라지면 비교 조건을 확인한다.
- [ ] 사용자: 회전/전진 경로를 선택하고 재생 또는 프레임 슬라이더를 조작한다. 양쪽의 카메라는 동일하다.64³만 작은 구름이 갑자기 사라지거나 경계가 점멸하면 채택을 보류한다.
- [ ] 사용자:64³ 채택 또는32³ 유지 선택. 선택 전 일반 설정/프리셋 변경 없음. 선택 뒤 Mie 높이 UI/9조합, 이어서 밀도 shaping→밀도/소멸계수→수직profile→Detail 크기/침식 비교로 진행한다.

### Detail 비교 결과·한계·사용자 피드백
진단 구현 커밋: d6973fedbb218cb4f20b8c7ff9802cfb12b0b94d. 측정일2026-09-18, RTX4080 SUPER / 드라이버32.0.15.9186, VCLOUD_STRICT_VALIDATION=OFF Release. 최종 결과: build/captures/cloud-detail-resolution/37548-28189640/comparison.html. README에는 원본8파일 hash와 모든 개별 측정이 있으며 presets/에는 원본 JSON 사본, paths.csv에는 공통 카메라 경로, gpu-samples.csv에는12그룹×300개 유효 GPU 표본이 있다.

| 결과 | 32³ | 64³ |
|---|---:|---:|
| Detail 데이터 메모리 | 0.125MiB | 1MiB |
| 정지 Cloud 중앙값의3회 중앙값 | 2.675ms | 2.896ms |
| 회전 Cloud 중앙값의3회 중앙값 | 2.703ms | 2.946ms |
| 정지 Frame 중앙값의3회 중앙값 | 3.241ms | 3.457ms |
| 회전 Frame 중앙값의3회 중앙값 | 3.293ms | 3.533ms |
| 정지 Cloud p95의3회 범위 | 2.761~3.064ms | 3.363~4.146ms |
| 회전 Cloud p95의3회 범위 | 3.120~3.201ms | 3.480~3.787ms |

F5/F6 Composite의 전체 화면 평균 RGB 절대 차이는0.000356/0.000295(8비트 단계로0.091/0.075), 최대 차이는8/255와9/255였다. 넓은 하늘 영역을 포함한 평균이므로 구름 경계만의 품질 점수가 아니다. 차이는 주로 경계 주변에 있고 큰 구름의 넓고 부드러운 전이는64³에서도 남는다. 이 조건에서는 Detail 해상도만으로 근경 흐림을 크게 해결했다고 볼 근거가 부족하다.

Debug/Release 빌드 성공. Debug 관련14/14(94.56초), Release 관련13/13(4.52초), 최종 Release 해상도 비교1/1(51.51초) 통과. GPU/빌드 표기 추가 뒤 양 구성 재빌드와 최종 Release 비교를 수행했다. 실제 리소스·재생성 동일성·Base hash·원본8파일 불변 모두 통과. 원래32³ 복원 Composite HDR 정규화 MAE4.83e-8/최대0.000271, LDR 최대1/255로 기존 tolerance 통과. 셰이더 런타임 수식은 변경하지 않았다.

비교 HTML JavaScript 문법과 이미지 참조를 확인하고 PNG를 직접 검토했다. 브라우저 자동 검사는 도구의 로컬 file URL 정책으로 차단되어 드롭다운·재생·확대의 실제 브라우저 조작 검증은 미완료다. HTML을 직접 열어 확인할 수 있다.

성능은32³을 먼저,64³을 나중에 측정하여 온도·클럭 변화가 섞일 수 있다. 따라서 약0.22~0.24ms 차이를 순수한 해상도 증가 비용으로 확정하지 않는다. 이동 검사는24개 고정 위치의 슬라이드이며 더 촘촘한 연속 움직임 검증을 대신하지 않는다. 사용자 화면 승인일과 피드백은 아직 없음. 일반32³ 유지,64³ 채택은 대기다. 대기9조합·밀도 조절은 아직 착수하지 않았다.

## 2026-09-18 사용자 승인: Detail64³ 채택 / 원경 대기 조절

사용자가 Detail64³ 채택을 명시적으로 승인했다. 일반 Texture3D 생성 기본값은64³ RGBA8(1MiB)로 바뀐다. Base128³/Weather256², 프리셋8파일, High100m/512, Base 그림자와 Deep80/79는 유지한다. 이전32³ 유지/승인대기 문장은 당시 기록이다. 통합 완료 전까지 push하지 말라는 사용자 지시에 따라 이번 이후 변경은 로컬에만 보존한다.

### 현상·가설·조사 근거
Detail 해상도만으로 넓은 근경 전이가 해결되지는 않았다. 기존 Air T 진단은 원경 감쇠가 이미 있음을 보여줬다. 다음 가설은 Mie의 양과 고도 분포가 현재 구름 높이에서 목표한 원경 대비를 충분히 낮추지 못한다는 것이다. 아직 특정 후보나 원인을 확정하지 않는다.

### 실험 조건·해결 방법
F3 Atmosphere의 Mie scale height (km)를0.5~4km로 노출한다. Ctrl+클릭 직접 입력도 AlwaysClamp를 적용하고 기존 CPU sanitize로 재검증한다. 내장1.2km, b9 필드와 기존 lighting schema1 저장을 재사용한다. JSON/CB ABI 변경 없음.
--cloud-aerial-tuning-test는 승인된 Detail64³에서 현재 Cumulus+환경3 저장값을 기준으로 Turbidity1/2/3×Mie높이1.2/2/3km의9조합을 만든다. baseline과 후보를 분리하고 source-presets에 원본8파일을 바이트 그대로 보존한다. 각 후보의 lighting/3.json은 실험 폴더에만 저장하며 읽기 왕복을 검사한다.
F5/F6,1920×1080,FOV60,시간71초,바람0,태양 재생/UI/VSync Off. 기존4구간 Aerial과 대표거리 합성을 유지한다. Composite/Air T/Air L/Cloud T의 PNG·HDR을 생성한다. Cloud T 불변,9개 Air T 결과 구별,원래 대기 복원Composite tolerance,원본8파일 동일성을 자동 확인한다.
성능은 각 후보와 기준의 F5 정지/회전에서60프레임 워밍업 후 유효300표본×3회씩 수집한다. Cloud/Frame 중앙값·p95와 Atmosphere p95,원시CSV를 기록한다. 순차 후보 측정의 클럭/온도 변동 한계는 유지한다.

### 사용자 확인
- [ ] comparison.html에서 F5/F6을 선택하고 후보를 고른다. 왼쪽은 현재 저장값, 오른쪽은 후보다.9조합 목록은 행이Turbidity1/2/3,열이높이1.2/2/3km다. 기본 표시T2/H2km는 탐색 시작점이며 추천·채택을 뜻하지 않는다.
- [ ] Composite에서 먼 구름의 대비가 줄고 근경 형상이 남는지, 하늘·건물·태양 주변이 과도하게 희거나 탁해지지 않는지 확인한다. 이미지를 클릭하면 원본 크기로 비교한다.
- [ ] Air T는 밝을수록 공기 통과율이 높고 Air L은 추가되는 공기빛이다. 회색은 구름 기여 없음이다. 구름 자체 투과율은 후보 사이에서 같아야 하며 다르면 형상 조건 또는 구현 문제다.
- [ ] 일반 Release 앱 F3 Atmosphere에서 Mie scale height를 조절한다. 범위0.5~4km 밖을 직접 입력해도 제한돼야 한다. 값을 바꾸면 기존 LUT 갱신으로 구름·하늘·건물에 반영된다. F4 Save Preset은 원본을 수정하므로 후보 결정 전에는 누르지 않는다.
- [ ] 사용자 후보 선택 후에만 환경3에 저장한다. 근경 밀도 경계 조절은 이 선택 이후이며 현재 미착수다.

### 대기9조합 자동 결과와 해석
OFF Release 결과는 build/captures/cloud-aerial-tuning/23232-29005640/comparison.html에 보존했다. RTX4080 SUPER/드라이버32.0.15.9186, 기준 환경3은 Turbidity1.5/Mie높이1.2km다. PNG80장·HDR80개, 원본8파일 사본, 기준+9후보 lighting JSON,60그룹×300개 GPU 표본이 생성됐다.
F5 구름 기여 픽셀의 평균 Air T(G)는 기준0.8453이며 아래와 같다. 이는 전체 구름 영역의 평균이고 거리별 통계나 품질 점수가 아니다. 무기여 경계1픽셀의 부동소수점 차이는 있었지만 Cloud T는 모든 후보에서 기존 tolerance 내에 유지됐다.

| Turbidity | 높이1.2km | 높이2km | 높이3km |
|---|---:|---:|---:|
| 1 | 0.8533 | 0.8463 | 0.8410 |
| 2 | 0.8374 | 0.8239 | 0.8138 |
| 3 | 0.8220 | 0.8024 | 0.7879 |

Mie의 양과 높이를 늘리면 이번 조건에서 공기 감쇠가 강화된다. 그러나 사진처럼 보이는지, 하늘/건물의 변화가 적절한지는 사용자 선택 대상이다. 근경의 넓은 밀도 전이는 이번 단계에서 조절하지 않았다. 기본 표시 T2/H2는 비교 시작점일 뿐 채택값이 아니다.

Debug/Release 빌드 통과. Release 관련16/16(165.86초:64³ 기본값에서 과거 해상도 비교51.43초, 대기9조합85.74초 포함), Debug 관련15/15(53.32초, 대기9조합26.10초 포함) 통과. Debug는 이미지/리소스/D3D 검사만 수행하고 성능 반복은 OFF Release에서만 수행한다. 해당 Debug 전용 생략 분기 추가 후 Release 바이너리도 재빌드했다.
F5/F6 기준 복원 HDR 정규화 최대오차0.000277/0.000185, LDR 최대1/255로 기존 tolerance 통과. 후보 JSON 읽기 왕복,9개 Air T 결과 구별, 원본8파일 동일성 통과. 별도 SHA256 검사로 원본 프리셋이 최초 백업과 모두 같음을 확인했다.
HTML JavaScript 문법과80개 PNG 참조를 확인했다. 도구의 file URL 제한으로 자동 브라우저 조작은 수행하지 않았으며 화면 사용성/룩 최종 확인은 사용자 대기다. 대기 후보의 저장·채택 및 근경 밀도 조절은 아직 하지 않았다. 사용자 승인일은 Detail64³만2026-09-18이며 대기 후보는 미승인이다. 원격 push 없음.
구현·측정 대응 로컬 커밋: bea78aeb7e24d3d06719cf4eab24fc15844ac0c3. 원격에는 아직 올리지 않았다.

## 2026-09-18 후광 과다 피드백 → 대기 방향 집중도 분리

### 현상·가설·조사 근거
사용자는 Turbidity와 Mie 높이를 키운9조합이 수평 원경 연무보다 태양 주변의 흰 원형 후광을 키워 어색하다고 평가했다.9조합에서 후보를 채택하지 않는다. 평균 Air T 감소는 확인했지만 원하는 화면 거리감의 달성과는 별개였다.
현재 Mie g0.8은 태양 방향의 전방 산란을 강하게 만든다. 양/높이 증가는 감쇠와 산란광을 함께 늘린다. scale height는 고도별로 직접 바꾸는 값이 아니라 exp(-고도/H)의 감소 속도다. 수평선의 긴 경로와 태양 방향 위상 효과를 구분한다.

### 구현·실험 조건
F3 Atmosphere에 Mie anisotropy (g)0~0.95를 추가하고 직접 입력도 AlwaysClamp로 제한한다. 기존 mieG CPU/GPU/lighting JSON 필드를 재사용하며 CB/schema는 변경하지 않는다. 이것은 구름 Light의 Phase와 별개다.
--cloud-aerial-phase-test는 기존 비교기를 재사용해 T1.5/H1.2km를 비롯한 저장값을 고정하고 g0.8/0.6/0.4만 바꾼다. Detail64³,High100m/512,Base그림자,Deep80/79,카메라·시간·태양·노출은 유지한다. 대기 산란광과 환경광은 g에 반응할 수 있으며 조명 설정을 별도로 보정하지 않는다.
원본8JSON 비파괴, 로컬 후보JSON 왕복, Cloud T 불변, Air T 불변, Air L 변화와 기존baseline 복원을 검사한다. 구름 차폐를 대기 태양광에 추가하거나 별도 수평 안개층을 넣지는 않는다. 사용자 화면 확인 전 g후보를 원본에 저장하지 않는다. 통합 완료 전 push 금지 유지.

### 사용자 확인
- [ ] 비교 페이지 F5에서 흰 원형 밝기가 적절히 줄고 구름 명암이 드러나는지 본다. g를 낮춰 다른 방향으로 빛이 퍼지는 변화도 있으므로 F6에서 전체 하늘·구름이 과하게 밝아지지 않는지 함께 본다.
- [ ] Air T와 구름 자체 투과율은 변화가 없어야 하고 Air L은 태양 방향 집중이 줄어드는지 본다. 회색은 구름 기여 없는 픽셀이다.
- [ ] F3 Mie anisotropy (g)는 공기 산란의 방향성이다.밀도/높이/구름 Phase를 조절하는 항목이 아니다.후보 선택 전 F4 Save Preset을 누르지 않는다.
- [ ] g후보 채택. 아직 미승인.별도 수평 연무와 근경 경계 조절은 이후 결과에 따라 진행한다.

### 2026-09-19 자동 검증 결과와 한계
Debug/Release 앱 빌드 통과. Release CloudAerialPhase 35.32초, Debug 관련6/6(기존9조합 포함)44.25초 통과. Release 대기 수학/AtmosphereSmoke/NoiseLabSmoke/LightingPresetStore도 통과했다.
Release 결과: build/captures/cloud-aerial-phase/38612-30987500/comparison.html. 32 PNG와 HDR 원본, 후보 JSON, 정지/회전 각60 warm-up+300유효샘플×3회 CSV를 저장했다. HTML JS 문법 검사 통과. 자동 브라우저 조작은 file URL 제한으로 수행하지 않았다.
Air T 공통 구름 영역 최대 차이0.00048828125, 복원 HDR 최대0.000250/LDR1단계로 tolerance 통과. 거의 투명한 경계의 기여 마스크가1픽셀 달라지는 초기 실패를 조사했다. Cloud T=0.99951171875(R16F에서1 바로 아래 값)인 경계였으며, 공통 영역의 RGB 오차와 마스크 차이를 분리해 검사한다. 마스크 차이는 최대4픽셀, 해당 위치 불투명도는 R16F1간격(1/2048)이하로 제한한다. 원본8파일 비파괴·후보 왕복·Cloud T 불변·Air L 변화 검증 통과.
F5/F6 Composite를 직접 검토했으나 기존 저장 대기량에서 전체 변화는 작다. 후광의 각도 분포 조절 기능을 추가한 결과이며 수평 원경 거리감 개선의 완료로 판정하지 않는다. g0.6은 비교 페이지 초기 선택일 뿐 채택값이 아니다. 원본 프리셋 및 일반 기본값 유지, 사용자 승인일 미정, push 없음.

## 2026-09-19 g0.3 사용자 채택과 수평 원경 재비교
사용자는 g를 낮추면 태양 주변 원형 집중 밝기가 감소함을 확인하고 기본값0.3을 지정했다. CPU 내장값/대기 preset reset/비정상값 fallback과 활성 환경3 JSON을0.3으로 맞춘다. 다른 저장 슬롯의 개별 설정은 유지한다. 승인일2026-09-19. 기존8파일 비파괴 기준은 이 명시적 승인 변경 이후의 파일로 갱신한다.
원경 거리감은 g0.3을 고정하고 기존 Turbidity1/2/3 × Mie높이1.2/2/3km의9조합을 다시 비교한다. 기존 exp(-고도/H) 분포와 경로 길이에 따른 감쇠를 먼저 평가한다. Detail64/High100m512/Base그림자/Deep80·79/Aerial4/대표거리 합성은 유지한다. 후보 T/H는 승인 전 저장하지 않는다. 이 단계의 가설은 후광 집중이 완화되면 기존 대기량 조절의 거리감 효과를 더 잘 평가할 수 있다는 것이다. 수평 거리감 달성은 아직 미검증이며 사용자 화면 판정 대상이다.

### 재비교 결과
Release 결과는 build/captures/cloud-aerial-tuning/37652-31606812/comparison.html(80 PNG/HDR)이다. 모든 후보 g0.3. Release GPU 관련5/5(대기9조합88.89초), Debug CPU 대기/프리셋2/2 통과. 두 구성 빌드 통과. 원본8파일(승인 g변경 이후 기준) 비파괴, 후보JSON 왕복, Cloud T 불변,9개 Air T 구별, 기준 Composite 복원 tolerance 통과. 복원 HDR 최대0.000302미만, LDR최대1/255. HTML JS 문법 통과.
F6 구름 기여 픽셀의 평균 Air T 녹색 채널은 기준0.864925 → T2/H2 0.846141 → T3/H3 0.814521. 이것은 전체 구름 영역 통계이며 거리별 구간 통계나 화면 대비 감소율은 아니다. F6 강한 후보에서 원경의 청회색 덮임이 증가하나 근경도 영향을 받는다. 자연스러운 정도/채택은 사용자 대기다.
성능 CSV는 정지/회전 warm60+300샘플×3회로 저장했으나 이번 실행 초기 Debug 빌드가 겹쳤으므로 공식 성능 채택 자료로 사용하지 않는다. 최종 통합 시 다른 빌드 없이 다시 측정한다. 거리 배율/적분횟수/별도 안개층은 추가하지 않았다. g외 T/H의 저장과 push 없음.
최종 Debug GPU4/4(32.56초,9조합27.28초) 통과. HLSL 기본값 주석까지 동기화한 빌드로 검증했다.

## 2026-09-19 Mie 높이2km 확정・근경 형상 착수
사용자는 Mie scale height2.0km를 기본으로 지정했고 근경 밀도 경계를 다듬은 뒤 원경을 재평가하기로 했다. 내장값/reset/fallback 및 환경3 JSON에2km를 반영한다. g0.3/Turbidity1.5는 고정하며 다른 슬롯의 개별 저장값은 유지한다.
첫 그룹은 Density shaping만 현재0.48, -10%=0.432, +10%=0.528로 비교한다. 구름 밀도/소멸/수직profile/detail은 고정한다. 후보는 로컬 JSON에만 보관하고 사용자 선택 전 Cumulus 원본을 수정하지 않는다. Composite/대기제외/Cloud T/Optical Depth와 F5/F6 및 동일 회전·전진 경로를 비교한다. 값 증가가 화면 품질 개선임을 전제하지 않는다. 자동수치 통과와 화면 승인을 분리한다.

조사 근거: shaders/CloudShapeParameters.hlsli의 shaping은 lerp(q, smoothstep(0,0.4,q), strength)다. 강도를 올리면 모든 밀도가 일률 증가하는 것이 아니며 q구간별 영향이 다르다. Renderer::RenderDeepShadowCaches는 매 프레임 현재 ShapeCB를 적용해 Base cache를 생성하므로 이번 형상 값은 view뿐 아니라 그림자에도 반영된다. Detail 포함 그림자나 Deep 증설은 하지 않는다.

### 자동 검증과 사용자 판정
Debug/Release 앱 빌드 통과. Debug 대기/NoiseLab/핫리로드/형상비교5/5(104.26초; 형상98.43초) 통과. 이후 Release 성능 중앙값을 짝수 표본 두 중앙값 평균으로 맞추고 두 구성 재빌드했다. Debug는 성능 코드를 실행하지 않는다.
Debug 결과 build/captures/cloud-shape-tuning/30732-32391250. 328PNG(정지24+차이16+이동288)와 정지HDR24, 원본8사본, 후보3JSON을 생성했다. JS문법 통과. 원본8파일 비파괴/노이즈해시 불변/각후보 Cloud T 차별/유한HDR/D3D검사/기준복원 통과. 복원HDR 최대0.000270미만, LDR최대1/255.
Mie2km 채택 커밋834e097. 형상 후보는 아직 미승인이다. ±10% 비교는 변화 방향을 분리하기 위한 첫 단계이며 근경 선명도 개선 완료를 의미하지 않는다. 사용자가 이 그룹을 선택하거나 변화 부족 피드백을 준 후 다음 그룹(밀도/소멸) 또는 비교 범위를 정한다. 원경 대기 재평가는 근경 형상 조절 후 진행한다. push 없음.

Release 형상비교83.09초 통과. 최종 화면: build/captures/cloud-shape-tuning/25648-32523015/comparison.html. OFF Release,1920×1080,UI/VSync Off,정지/회전 각각60 warm-up+유효300샘플×3회×3후보=5400샘플을 gpu-samples.csv에 기록했다. 성능 계측 중 별도 빌드를 하지 않았다. 각반복 Cloud/Frame 중앙값·p95는 README.md 참조. 원본8파일/노이즈/기준복원 검사 모두 통과. 정지/이동312PNG 참조와 HDR24 및 JS문법 검증 완료. 차이16PNG 포함 총328PNG. 자동 브라우저 조작은 수행하지 않았고 최종 화면/이동 품질 승인은 사용자 대기다.

## 2026-09-19 높이 profile 보존, Detail 크기 비교
사용자 피드백: Density shaping 증가 시 근경 내부와 원경이 더 불투명해지지만 흐린 경계 폭은 그대로다. 증가 후보는 채택하지 않고 원본0.48 유지. 사용자는 수직 profile 변경을 생략하고 Detail 크기·침식·경계 조사로 진행하도록 지시했다.
먼저 Detail 월드 크기2000m의 ±10%(1800/2200m)만 비교한다. 텍스처64³의 생성 데이터/시드/주파수는 그대로 두고 월드 샘플 좌표 스케일만 바꾼다. 침식0.608/높이profile/Density shaping0.48/대기 g0.3·높이2km·T1.5를 고정한다. 원본은 읽기만 하며 후보는 캡처 폴더에 저장한다.
--cloud-detail-size-test는 기존 형상 비교기를 재사용한다. 정지4모드, F5/F6, 동일24프레임 회전/전진, 차이8배 및 원본크기 조회를 제공한다. 작게 만들면 더 촘촘한 굴곡이 생길 수 있으나 고정100m 적분에서 깜빡임/작은 구름 소실이 증가할 가능성을 함께 확인한다. 크기 사용자 선택 뒤 침식 강도를 별도로 비교한다. 그림자는 기존 Base를 유지하며 Detail형상과 그림자 불일치도 사용자 확인 대상이다.

코드 조사: Noise.hlsli는 약한 Base 경계에 큰 침식 가중치(1-smoothstep(0.45,0.90,Base))를 주고 Base-Detail×strength×boundary를 clamp한다. 따라서 크기 변화는 공간 무늬의 간격을 바꾸며 경계 전이 폭 자체를 직접 지정하는 파라미터는 아니다. 크기 비교만으로 충분한 선명도가 나오지 않으면 침식 강도를 다음 단독 비교로 진행하고, 그래도 부족할 때 밀도 전이 구간 노출을 후속 검토한다.

Debug/Release 빌드 통과. Debug NoiseLab/노이즈 핫리로드/Detail크기3/3(104.42초, 비교100.09초) 통과. Debug 결과 build/captures/cloud-detail-size/38492-33754796. 원본8파일 비파괴/노이즈 해시 불변/유한HDR/D3D/기준복원 통과. 복원HDR최대0.000270미만, LDR최대1/255. 후보1800/2200m와 Density shaping0.48 고정 기록을 확인했고 HTML 표시/JS 문법을 검사했다.
F6 대기제외 이미지에서 Detail 크기 변경은 굴곡의 위치와 모양도 바꾼다. 같은 경계점이 단순히 더 날카로워지는 비교로 해석하면 안 된다. 넓은 흐림 해소 여부와 작은 구름 유지 여부는 사용자 판정 대기다.

Release 비교75.10초 통과. 최종 결과 build/captures/cloud-detail-size/15820-33869843/comparison.html. 정지24HDR/PNG+이동288PNG+차이16PNG 참조와 JS문법 확인. 후보JSON은 크기2000/1800/2200m, 침식0.60799998 동일. OFF Release,1920×1080,UI/VSync Off,정지·회전 warm60+300유효샘플×3회×3후보 기록은 gpu-samples.csv/README.md에 보존한다. 다른 빌드/GPU 테스트와 겹치지 않았다. 원본 비파괴/노이즈 데이터 불변/복원 tolerance 통과. 최종 화면 품질과 이동 안정성은 사용자 확인 대기. 원본 크기2000m·침식0.608 유지, push 없음.

## 2026-09-19 밀도 전이 폭 조절 구현
사용자는 Detail 크기 실험 대신 밀도 전이 구간을 직접 조절하도록 요청했다. F1 Cloud Local의 Density transition width(0.05~1, 직접입력 clamp)를 추가한다. 기본1은 기존 결과를 그대로 반환한다. 1보다 작으면 기존 Density shaping 이후 q<0.2의 밀도를 중심0.1 주변에서 선형 재매핑한다: 0.2*saturate((q-0.1*(1-w))/(0.2*w)). q>=0.2는 유지한다. w0.5에서는 q0.05이하 제거/q0.15이상0.2 도달. 이는 월드 meter 폭이나 h 범위를 바꾸는 값이 아니라 정규화 밀도 전이 폭이다.
Detail 침식 후 View 최종 밀도에 적용하며 Base light/shadow도 공통함수로 적용한다. Detail 포함 그림자는 추가하지 않는다. 거리/높이profile/step/Weather 생성은 그대로다. 작은 구름 소실/경계이동/고정100m에서 깜빡임은 한계이며 선명도 개선 채택은 사용자 판정이다.
b7 offset44 예약을 densityTransitionWidth로 사용하며 크기48B 유지. formation schema4는 선택 필드로 추가(구파일 누락시1), 범위/비유한 검증. Save Preset 시 명시 저장, 시작/시험은 원본 수정 없음. 기존 snapshot44에도 설명용 필드를 추가한다.
--cloud-density-transition-test는 같은 카메라/시간과 고정 대기에서 폭1/.75/.5를 비교하고 원본/노이즈 불변/복원/HDR/D3D/이동/성능을 기존 도구로 검사한다. 자동기본은1, 후보는 채택 전 저장하지 않는다.

### 검증 진행
Debug 전체 빌드와 관련6/6(113.74초; 전이비교106.53초) 통과. 최종 offset44 reflection/snapshot 변경 후 Debug 앱 재빌드 및 NoiseLab/노이즈 핫리로드2/2 통과. 실행 중 Debug exe 잠금으로 링크1회 실패했으나 시험 종료 후 재빌드 성공했다.
새 기본폭1과 구현 전 Debug baseline을 직접 비교: F5 HDR 정규화MAE5.36e-8/max0.000270, F6 MAE2.93e-8/max0.000156으로 기존tolerance 이내. 즉 기본값은 기존룩을 보존한다. 조절 후보의 시각적 효과는 의도적 변경이므로 픽셀 동일성을 요구하지 않는다.

Release 관련4/4(78.64초,전이78.41초) 통과. 최종 캡처 build/captures/cloud-density-transition/33284-34739453/comparison.html. 328PNG/24HDR/3후보JSON(1/.75/.5), 원본8사본,정지/회전 warm60+300유효샘플×3회×3후보 성능 CSV 보존. 다른 빌드와 겹치지 않은 OFF Release 측정이다. 원본8파일 비파괴/노이즈 해시/기준복원 tolerance 모두 통과. 페이지JS 문법/파일참조 확인. 최종 메타데이터 정리에서 snapshot 중복 출력 한 줄을 제거했고 보고서 제목을 전이 폭으로 교정했다. 렌더식/후보값은 동일하다.
사용자 확인: F1 Cloud Local의 폭1로 기존룩 확인 후 .75/.5 비교. 경계폭 개선과 작은 구름 존속/회전·전진 깜빡임/Base그림자 불일치를 함께 판정한다. 기본폭1 유지. 후보 프리셋 저장/원격 push 없음. 화면 승인일 미정.

## 2026-09-19 거리별 View 적분 간격 착수
사용자 피드백: 전이 폭 효과는 흥미롭지만 근경 선명도 개선에 도움이 크지 않았다. ideas/density-transition-width.md에 수식/구현/재사용/제거 체크를 보존한다. 전이 슬라이더는 유지하고 거리별 step 조절 뒤 재비교하여 불필요하면 렌더/UI에서 제거한다.
첫 진단은 전이폭1 고정, 기존100m/512 대비 근경50m/25m 두 후보. 광선 cursor(카메라에서 현재 표본까지 거리)5km 이내는 촘촘하게,5~15km는 smoothstep으로 기존 간격에 연결한다. 24~50km의100→125m는 유지한다. 후보는 최대4096회로512회 예산에 의한 조기 잘림을 피한다. 캐시/빛 적분/밀도/노이즈/대기는 고정한다. 일반High는 사용자 비교 전 변경하지 않는다.
테스트 전용 VCLOUD_TEST_NEAR_STEP_METERS 매크로와 --cloud-distance-step-test. 동일F5/F6 정지·회전·전진/성능 조건 사용. 목표는 샘플 간격 부족과 밀도자체 흐림을 구분하는 것이다. 원경을 일부러 흐리게 하기 위해 샘플을 과도하게 줄이지 않는다.

Debug/Release 빌드 및 Debug 관련3/3(112.63초,거리비교106.22초) 통과. 출력 build/captures/cloud-distance-step/27532-36162500. 원본8파일 비파괴/노이즈 불변/유한HDR/D3D/일반셰이더 복원 tolerance 통과. 전이폭1과Detail2000m 고정 확인. 50/25m 시험PS는 기존cache/light 셰이더를 교체하지 않는다. 시험 종료/실패 때 기존PS를 복원한다. JS문법 검사 통과. 근경50m 정지 캡처에서 넓은 부드러움은 여전히 관찰되므로 촘촘한 적분만으로 해결됐다고 단정하지 않는다.

Release 거리비교88.91초 통과. 최종 결과 build/captures/cloud-distance-step/15248-36299828/comparison.html. OFF Release,1920×1080,UI/VSync Off,정지/회전60warm+300유효샘플×3회×3후보. Cloud GPU 반복별중앙값의중앙값: 정지100m3.035648/50m3.598848/25m4.509696ms, 회전3.104256/3.746816/4.802560ms. p95/Frame은 README와5400샘플CSV에 보존했다. 순차측정의 클록/부하변동 한계가 있다.
일반 baseline HDR은 직전 전이폭 실험의 Release baseline과 F5/F6 모두정확히일치(MAE/max0). 후보후 복원HDR최대0.000270/LDR1단계로 tolerance 통과. 원본8/노이즈불변,유한HDR/D3D 통과.328PNG/24HDR 참조와JS문법 검사 완료. 수치 통과는 화면 승인과 구별한다. 일반High는100m/512유지, 전이폭슬라이더는 보존. 다음은50/25m 화면 선택 후 그 조건에서 전이폭 재비교이며 필요없으면 함수/UI 제거한다. push 없음.

## 2026-09-19 Weather / Base 역할 분리 진단
현상: Detail 크기는 외곽 굴곡만 바꾸고 근경 몸체를 회복하지 못했다는 사용자 피드백. 가설: Weather의 두께/밀도/문턱 영향에 비해 Base의 독립 형태가 약할 수 있다. 확정 원인 아님.
조사 근거: 기존 Raw 진단은 support precheck가 생략한 위치에서0이며 일반 Tone을 거쳐 원본 분포 판정에 부적합. Weather는 배치 외 R 문턱/B 배율/A 두께에도 관여한다.
실험: --cloud-base-weather-test. 실제 GPU Texture3D를 조회 생략 없이 읽고 XZ 세 높이 및 XY 단면에서12필드를 HDR 덤프/선형 흑백으로 비교. Weather 중립 R1/B1/A0.5와 일정 Base0.65 대조군 포함. 기존 밀도 함수 재사용. 일반 룩/프리셋 불변. 결과/사용자 승인은 검증 후 기록.
한계: 균일 Weather에도 프로파일/footprint가 남는다. Base0.65는 실제 평균이 아니다. 서로 다른 축척의 단면이며 입체 실루엣/화면 승인/성능 검증을 대신하지 않는다.

결과: Release build/captures/cloud-base-weather/34548-38669531. 기준 HEAD a127b91 위 진단 변경. Debug/Release 빌드 및 각 CloudBaseWeather/HotReloadDependencySmoke 2/2 통과. HDR 유한/비음수, D3D 오류 없음, Composite 복원 tolerance, 원본8파일 동일성 통과.
XZ25/50에서 Raw Base 범위0.536~0.786 /0.530~0.778, 평균0.663/0.665. coverage0.617의 단순 문턱0.383보다 모두 높아 Base+coverage만으로 빈 공간이 생기지 않는다. 중립 Weather의 Base 밀도 최소0.160/0.183, 0.01초과 면적100%, 0.2초과99.46%. XY에서도 높이 마스크가 만든 띠 내부에 완만한 변화가 남는다. 상부XZ75 중립 Weather0은 local 상단 밖인 결과이며 Base 고유형태 부재 증거로 쓰지 않는다.
일정 Base0.65 대조군과 실제 Base는 큰 배치가 시각적으로 유사하다. Base가 무효라는 뜻은 아니지만, 검사 단면에서는 Weather가 큰형태를 지배하고 Base는 내부밀도변화를 주는 경향을 확인했다. 단면은전체3D/최종화면을대표하지 않으며 정량적 상관계수는 측정하지 않았다.
채택/기각: 일반 룩 채택 없음. 다음 후보는 Base 조회 조합/값분포와 coverage 문턱 관계를 바로잡는 비교다. 소멸/스텝/Detail 증설을 먼저 채택하지 않는다. Weather support만 좁히면 Weather 외곽을 자를 뿐 Base 독립형태 부족을 직접 해결하지 못한다. 화면승인일 없음, 사용자피드백 대기.

## 2026-09-19 Base 재매핑 / 중간 굴곡 5케이스
현상: Weather가 큰 배치를 정하는 것은 정상이며 연속 Base층만으로 결함이나 근경밀도부족을 확정할 수 없다는 설명을 정정했다. 사용자 요청으로 Base 값분포/문턱/중간굴곡을 카메라 영상에서 비교한다.
가설/조건: 기존, 문턱+0.05(분모고정), raw 중심0.65대비2배, 생성 중간옥타브9/17진폭1.5→2.5배, 대비+옥타브 조합. 마지막만 조합이고 나머지는 한 항씩. Weather/Detail/소멸/높이/전이폭 원본값 고정. 임의 후보로 품질보장값이 아니다. 중간가중치변경은 정규화도 달라져 평균/분포가 완전히 보존되지 않는다.
실험: --cloud-base-remap-test로 기존 F5/F6 정지4모드/동일이동24프레임/Release성능측정 재사용. View와 Deep Shadow에 같은 문턱/대비 매크로 적용. 중간굴곡은 원본생성함수를 같은seed/주파수/해상도로 다시생성한다. 후보JSON만으로 테스트매크로를 재현할 수 없으므로 README의케이스정의와 실행기를 함께 보존한다. 일반실행/원본JSON 변경없음. 결과/채택/사용자피드백은 후속기록.

Release 결과: build/captures/cloud-base-remap/41620-39338562/comparison.html. 기준HEAD7a2a229 위 변경. HotReloadDependencySmoke+CloudBaseRemap 2/2(168.90초). 552PNG/40HDR, GPU9000유효표본. 페이지경로/JS문법검증통과. 5개CloudT해시서로다름, 중간옥타브두후보Base해시동일(16602041024179322486), Detail해시전부동일. 원본8JSON불변.
일반baseline을이전거리step비교와검사: F5 HDR정규화MAE/max0/0, F6약3.36e-8/0.000155로기존tolerance통과. 실행후복원도통과. Cloud중앙값(3회중앙값의중앙값,정지/회전ms): 기존2.160/2.162,문턱1.883/1.948,대비2.873/2.926,중간2.883/2.950,조합2.958/2.994. Frame/p95는실험README/CSV참조. 순차측정으로GPU클록영향을분리하지못하므로보편적성능비율로해석하지않는다.
화면관찰/한계: 대비/조합에서도근경의넓은부드러운경계가남는다. 형상변화가있다는것과문제해결은별개다. 스케일/소멸/프로파일변경없이후보별몸체소실/원경뭉침/이동팝핑은사용자확인대기. 채택없음/푸시없음.

Debug 결과: build/captures/cloud-base-remap/31032-39559218. Debug 빌드 및 HotReloadDependencySmoke+CloudBaseRemap 2/2(210.22초). 성능측정은Release만수행. 사용자채택대기.

## 2026-09-19 임시 Base 후보 실시간 UI
요청: 사진만으로 판정이 어려워 환경/카메라를 직접 바꾸며5후보 비교. F2 Temporary Base comparison에선택과Restore original Base추가. 기존카메라/환경/Weather/형상/High step유지. 후보선택은세션전용,일반시작Original,프리셋/UI설정저장대상아님. 스냅샷은관찰용temporaryBaseCandidate를추가기록한다.
CPU UI요청→기존원자적ReloadShaderPrograms→Cloud/NoiseLab/Deep/Base생성프로그램을같은매크로로준비→성공시에만프로그램/생성볼륨커밋. 실패시이전후보유지와오류표시. 핫리로드에도같은후보매크로유지. 중간옥타브는동일seed/해상도로재생성. CB/schema프리셋변경없음.
사용자게이트: 유효후보가없으면임시UI/후보를제거하고Original유지. 유효후보선택시해당값만기본채택하고비교기능정리. 현단계에서채택하지않음.

자동검증결과: 기준ce78b6f 위 임시UI. Debug/Release 빌드통과, 각 NoiseLabSmoke/HotReloadDependencySmoke/BaseCandidateUiSmoke 3/3통과(Release3.85초/Debug6.56초). 실제5후보의HDR해시상이/유한값/D3D오류없음,후보4전체셰이더재로드후영상tolerance일치,범위밖번호거부,Original복원영상/노이즈hash일치,형상/조명값불변,원본8파일불변. 마우스로UI선택/여러환경의품질판정은사용자확인대기. 성능재측정은하지않음. 푸시없음.

## 2026-09-19 구름 속 통과 길이 분리
요청: 짧은구름속길이를근경으로볼수있는지확인. 둘은다르다. 가까운두꺼운구름/먼구름의접선이반례. F4 Cloud view93~95에점유길이/점유구간평균밀도/첫점유거리추가. 진단전용100m고정/최대4096회/조기종료와empty skip없음. 거리fade전최종밀도0.001초과구간의합이며가려진뒤구름도포함. 100m표본과문턱에의존하는추정이지정확한물리표면측정아님. 일반Composite/High변경없음.
표시: 길이/첫거리0~10km선형회색,평균밀도0~1,청록구름없음. 원시HDR은미터/밀도를보존. 기존층교차길이33과불투명도가중거리56과구분. 후보선택시선택한밀도로측정하지만자동비교는Original고정. --cloud-path-length-test에F5/F6영상/원본8JSON/수치저장. 채택/결과는후속기록.

검증/결과: Release최종build/captures/cloud-path-length/39388-42348312/comparison.html. Debug/Release빌드,각 NoiseLabSmoke/HotReloadDependencySmoke/CloudPathLength3/3,SceneMath단위검증통과. F5/F6일반Composite는이전Base비교baseline HDR정규화오차0. 점유길이는소멸계수절반에서도불변,원본8파일불변,진단후Composite복원통과. PNG선형눈금/청록표시와HDR수치일치검사추가. 초기진단ID누락과Tone전달누락은검증중수정했으며이전실패/중간캡처는채택하지않는다.
F5에서첫거리15km초과이지만점유길이500m미만21130픽셀,F6에서는11927픽셀. 짧은점유길이=근경가정의반례다. 점유길이를근원경보정값으로자동사용하지않는다. 형상두께/밀도전이/카메라거리기반예술보정은서로다른후속안이며아직적용없음. 사용자화면승인대기/푸시없음.

## 2026-09-19 P0–P2 근경·원경 원인 분리 보완
현상: Base 후보/Detail 크기/근경 step 변경에도 근경 몸체의 넓은 흐림이 남는다는 사용자 피드백. 가설: 실제 밀도 감쇠, 최적화된 View 적분, 조명 채움, 대기 조회 중 어디서 대비가 사라지는지 아직 분리되지 않았다.
조사 근거: 불투명도 기여 가중 대표거리 및 Tc*B+Ta*C+(1-Tc)*Sa 동등식은 이미 존재한다. 단일 Mip0/Full-resolution이며 Temporal/업샘플링 없음. 과거 별도 Compute 균일 구 검사는 실제 View PS 검사를 대체하지 않는다.

|계획 항목|재사용 결과|이번 제외|
|---|---|---|
|P1 후보 전달|Base remap 41620-39338562, UI smoke|5후보 전체 재비교|
|P1/P2 step|distance-step 15248-36299828|기존100/50/25m 사진·성능 반복|
|P1/P3 단면|base-weather 34548-38669531|동일12필드 단면 반복|
|P2 거리|path-length 39388-42348312|점유길이=근경 가정|
|P3 형상|shape/detail-size/density-transition 기록|동일후보 반복|
|P5 대기|기존4/16/32 및Turbidity/Mie 격자|동일격자 재실행|
|P6 복원|현재 코드의 경로 부재|Temporal/upsample/Mip 후보 추가|

실험 조건: Original Base, Cumulus+환경3,1920x1080,FOV60,time71,wind0,UI/VSync/재생Off. F5/F6+같은F5위치/방위의75도·5도. 일반값 변경 없음. 새 --cloud-near-far-diagnostics-test는 실제 View 구/완만한 구 입력, High와 최적화 없는 ROI, 광선별 밀도 계보, 실제 LUT 조회·합성 계약을 검사한다. 일반UI/CB/schema변경없음.
결과: 구현·검증중. 통과/원인확정으로 취급하지 않는다. 기본값 채택/원격push없음. 원본 사용자 실행계획서는 수정하지 않는다.
한계: 광선계보25m/48km는 별도 전체구간 계측으로 High의 종료 이후 뒤 구름도 포함. 경계폭은 선택한 수평 scanline에서 국소최대alpha로 정규화하며 교차없음=-1. 실제 겹친구름 전체를 대표하지 않는다. 림은직접광에포함된부분이므로직접광에다시더하지않는다.
사용자 피드백: 비교기준 Original유지 선택. 첫 결과 확인 후 다음 수정 하나만 선택한다.

### P0–P2 결과와 다음 수정 제안
최종 Release: `build/captures/cloud-near-far/42308-45389781/comparison.html`. 기준157e931+이번진단 변경이며실행README에셰이더5개해시/원본8파일해시를보존한다. 카메라행렬은cameras.csv. 기존F5/F6 Composite/T는8JSON동일및F5기준HDR tolerance(MAE5.39e-8/max0.000270)확인후재사용했다. 중복후보격자·성능재측정은하지않았다.

확인된 사실:
- 실제View적분의균일구에서100/50/25m의평균T절대오차0.005119/0.002482/0.001217로감소했다. 부드러운구의최대오차0.001506/0.000636/0.000512. 후자는반정밀저장오차근처이며평균오차의엄격한단조감소를주장하지않는다. 구간잘림0.
- 실제Near75의중심·경계를포함한512x128 ROI에서High와precheck/coarse/earlyexit없는25m의T차이MAE0.000513/max0.002441. 고정100m참조와High의오차통계가같다. 이ROI에서넓은흐림을샘플수/skip문제로설명할근거는작다. 모든구름/시점의최적화가완전하다는증명은아니다.
- 같은Near75중심광선(1471,284)의Base(shaping후)적분992.79m→Detail최종308.53m,31.1%잔존. 경계광선(1224,284)은829.13m→54.67m,6.59%잔존. 이는같은광선에서침식이큰감쇠를만든다는근거다. 중심최종alpha약0.611/경계0.154. 카메라거리보정은하지않았다.
- 중심광선3612.5~3912.5m에서수직profile0.917~0.990인데도침식전Base0.400~0.464에erosion0.144~0.198이작용한다. `1-smoothstep(0.45,0.90,baseDensity)`가이런몸체에도거의최대가중치를준다. 높이profile만의문제로확정할수없으며,기존Detail크기조절과는별개의감산문제다.
- 조사한상향광선에서거리증가에따른Air T역전0. 수평선5도중심은2km RGB T=(0.976,0.961,0.926),40km=(0.737,0.579,0.316). 밝은/어두운입력을따로GPU저장한뒤CPU검산한합성오차최대0.000473. 구름없음/불투명경계오차0,AP우회도허용범위. 현재대기경로가작동하지않는다는가설은이범위에서반박된다. 실제대표거리근사오차와원경색감의충분함은미검증이다.

조명분리: Near75 ROI의선형평균Direct0.01225/Sky0.00593/Ground0.00550/Multiple0.01781. 채움광이명암을줄일가능성은남지만,T단계에서도약한몸체/넓은경계가남으므로조명만을주원인으로확정하지않는다. Rim은Direct에포함되므로별도로합산하지않는다. ROI간카메라각/구름이달라근원경수치를동일물체비교로사용하지않는다.

다음한가지제안: **코어를보존하는Detail침식후보**. 현재의형성/높이/월드크기/대기는고정하고,몸체까지거의최대감산하는가중치/상한을국소코어와외곽으로분리하는후속비교가우선이다. 이진단에서코드수정은채택하지않았다. 밀도가증가해도넓은경계가날카로워진다는보장은없으며평판화/작은구름증가/Base그림자와의차이를함께평가해야한다. step감소·전역소멸증가·원경블러는이번근거의우선수정이아니다.

한계: 선택한두광선과ROI기반이다. 실제구름계보는25m참조/최대48km,가려진뒤구름도포함한다. 선형10–90%경계폭은한수평scanline의국소최대기준이며화면끝에걸린경계는-1이다. 예컨대Near75는좌측351px/우측미측정. 전체입체선명도를단일폭으로판정하지않는다. 숫자진단RGBA16F의양자화를포함한다. 실시간성능후보를추가하지않았으므로성능채택판정도없다.

검증: Debug/Release빌드및각AtmosphereSmoke/NoiseLabSmoke/HotReloadDependencySmoke/신규진단4/4통과. 최종경계ROI확대후Release신규진단17.24초통과. Debug최종재검증결과는추가기록. 원본8JSON불변/Composite복원/HDR유한/D3D검사통과. 화면승인일미정. 사용자비교후다음수정범위를선택한다. push없음.

최종 Debug 진단51.23초통과: build/captures/cloud-near-far/42912-45457109. Release최종페이지에해석/한계와분석문서를연결했고62PNG/62HDR참조및JS문법확인. 원본8JSON SHA-256 동일성재확인. 코드진단과화면품질승인은분리하며본단계에서중단한다.

## 2026-09-19 Detail 코어 가중치·상한 비교
현상/가설: 사용자는외곽을깎는Detail이몸체까지침식하는문제를확인하고몸체보존후보진행을요청했다. P0–P2 선택근경광선에서31.1%/6.59%잔존을확인했지만전체구름원인을확정하지않는다.
조사근거: 기존boundary=1-smoothstep(.45,.90,baseDensity)는높이·Weather·밀도배율까지적용된밀도값이다. 실제공간의경계거리가아니므로내부가약하면외곽으로취급한다.
실험조건: Original Base/Cumulus+환경3/Detail64/High100m512/Base그림자/Deep80·79고정. core=smoothstep(.15,.40,weatherThresholdDensity)×smoothstep(.15,.75,verticalProfile)×WeatherSupport. 기존,가중치Eold×lerp(1,.35,core),상한min(Eold,base×lerp(1,.35,core))의3케이스. 후속clamp/shaping은기존대로. 임의비교상수이며물리적경계추정이라고주장하지않는다. core0은기존최종밀도동일,밀도floor없음.
--cloud-detail-core-test는기존형상실행기를확장한다. F5/F6/Near75정지4모드,동일회전·전진24프레임,Release정지/회전60warm+300×3. Near75고정ROI평균alpha/선형휘도·표준편차를기록한다. XY GPU단면에서기존≤후보≤Base,Base0이면후보0,core0이면기존동일을검사한다. 후보JSON은매크로를저장하지않으므로실행기/README케이스정의를함께보존한다.
결과/채택: 구현·검증중. 일반UI/기본값/원본8JSON/그림자정책불변. 사용자선택전채택없음/push없음. 몸체불투명도회복과경계선명도는별개이며평판화·작은구름증가·원경불투명화·그림자차이를사용자가판정한다.

### 코어 비교 결과 — 일반 채택 전
Release 결과: build/captures/cloud-detail-core/46784-46441093/comparison.html. Debug/Release빌드통과. Release NoiseLabSmoke/HotReloadDependencySmoke/CloudDetailCore3/3(114.90초;비교107.28초)통과. 492PNG/39HDR(정지36+단면3),GPU유효5400표본,원본8JSON동일성/노이즈해시불변/3시점Composite복원/유한HDR/D3D검사통과. 페이지동적참조전체및JS문법확인. 이전P0–P2의3시점Composite와새baseline은HDR정규화MAE최대4.4e-8/max0.000307로기존tolerance이내.
Near75고정ROI평균alpha: 기존0.40165 → 가중치0.74933 / 상한0.70163. 몸체불투명도회복은확인했다. 같은ROI의대기제외선형휘도표준편차는0.012604 →0.010072/0.010193으로감소했다. 따라서밀도증가를내부명암·선명도성공으로판정하지않는다. 배경/경계를포함한ROI이므로이수치를구름내부만의대비로해석하지도않는다.
Near75 alpha>0.01화면픽셀: 기존809720 →가중치943778(+16.6%)/상한1179719(+45.7%). 상한은몸체외에이전침식으로없어진얇은Base지원영역도많이되살린다. 전체상한방식이상대적으로넓은희미한영역을채울수있다는한계다. Base가0인공간에는생성하지않았고core0동일성은통과했다.
수식상정확한표현: 상한후보는Base×0.65×core의상대적하한을만든다. 전역고정밀도floor를추가한것은아니지만,하한효과자체가없다고표현하면틀리다. 이효과가얇은부분증가의가능한기전이다.
GPU Cloud(반복별중앙값의중앙값,정지/회전ms): 기존3.059/3.089,가중치2.849/2.917,상한3.254/3.376. 구름량/earlyexit변화와순차측정클록편차를포함한다. 보편적속도개선으로주장하지않는다. Frame/p95는README,원시값은gpu-samples.csv. 측정중다른빌드/GPU시험없음.
사용자판정제안: 가중치후보부터보되몸체회복과경계·굴곡보존을별도로본다. 상한후보의넓은채움/원경불투명화를함께확인한다. 선택전일반프로그램은기존침식,원본프리셋변경없음/push없음. 기본값채택·화면승인일미정.

Debug 결과 build/captures/cloud-detail-core/46980-46589375. 관련3/3(169.16초;비교159.92초)통과. 이후README의Near75명칭/상한의상대적하한설명만정정하고Debug/Release재빌드통과. 렌더수식/후보/계측코드변경없어무거운GPU비교는반복하지않았다. 일반채택없음.


## 2026-09-21 타입별 Detail 침식 비교 착수
가설: Cumulus에서 회복된 몸체가 다른 타입에서는 깊은 파임을 메울 수 있다. 저장된 네 타입을 환경3/Original/Detail64/기존 High로 비교한다. 후보는 기존 침식과 가중치35% 두 개뿐이다. CPU가 슬롯을 읽고 View PS 후보만 교체하며 Base 태양 차폐는 유지한다. Alpha는 GPU T를 읽어 CPU에서1−T로 변환한 선형 회색이며 흰색불투명/검정투명이다. 일반 UI/CB/schema/프리셋 변경 없음.
사용자 체크: comparison.html 타입 선택→F5/F6/Near75 선택→Alpha에서 몸체 회복, Composite/대기제외에서 파임·구멍·타입 고유모양 확인. 이미지를 클릭하면 동일 좌표480×320 원본 픽셀 확대. 회전/전진 프레임을 움직여 경계 급변 확인. 몸체가 회복되어도 구름이 서로 붙거나 둥글게 평판화되면 실패 징후다.24위치 표본은 실시간 깜빡임의 완전 검증이 아니다. 경계폭/태양Detail차폐는 승인 이후이며 미구현.
자동검증: 진행중. 사용자화면승인: 대기. push금지.

### 타입별 비교 Release 결과
Release: `build/captures/cloud-detail-core-types/2688-2612437/comparison.html`. NoiseLabSmoke/HotReloadDependencySmoke/CloudDetailCoreTypes 3/3 통과(239.98초, 새 비교237.74초).1320PNG/128HDR의 파일 수·유한값·alpha=1−T·선형PNG양자화·후보/타입 간 동일294개 카메라 목록·원본8JSON SHA-256 동일성을 추가 검사했다. 페이지 JS 문법 통과. 후보별 밀도 불변식과 타입별3시점/타입순회 후 일반Composite 복원 통과.
Near75 전체화면 평균alpha: Stratus0.007405→0.008343, Cumulus0.154649→0.277320, Altocumulus0.298026→0.308820, Custom0.483524→0.484426. 타입별 반응 크기는 다르다. 이 수치는 경계 선명함이나 파임 보존의 성공 판정이 아니다. Stratus의 기존 Cumulus 기준 고정ROI에는 구름이 없으므로 ROIalpha0은 실패로 해석하지 않는다. 사용자 품질 승인 대기.
기존 Cumulus 캡처는 전체 셰이더/카메라 fingerprint 부족으로 재사용하지 않았고 새 결과의 기존 수치 일치를 확인했다. 성능은 재측정하지 않았다. 캡처 중 Debug 빌드를 병행했으므로 이 실행의 시간은 성능 비교 근거가 아니다. 비교 종료 후 ROI 해석 설명과 텍스처해시 보고만 보완했으며 렌더·계측식 변경 없음.

Debug 결과: `build/captures/cloud-detail-core-types/6356-2889296/comparison.html`. 관련3/3 통과(410.74초; 새 비교407.94초). Debug 산출물도1320PNG/128HDR/alpha변환/동일카메라/8JSON SHA-256 검증 통과. Release 페이지624개 동적 선택의 모든 파일 링크 검사 통과. 이후 CSV 카메라 행렬16열의 헤더 이름만 명확히 하고 Debug/Release 재빌드 통과; 렌더 변경이 없어 GPU 재촬영은 반복하지 않았다. 비교 캡처의 헤더도 같은 형식으로 정리했다.
첫 승인 지점에서 중단: 타입별 파임·구멍·둥글어짐·이동 판정 대기. 일반 기본값/프리셋8개/태양차폐 불변. 경계폭조사와 거리별Detail태양차폐 미착수. 사용자 제공 실행계획서 미수정/원격push 없음.


## 2026-09-21 타입별 몸체 보호 슬라이더 / 전이 폭 제거
사용자 피드백: Cumulus 가중치 후보는 어울리지만 얇은 Stratus/Altocumulus는 과하고 잘린 외곽처럼 보임. 모든 타입 공통35% 채택은 보류. 원경 대비 문제도 미해결이다. F1 Cloud Local에 Detail core protection [0,1] 추가:0기존/.65이전가중치/1최대몸체보호. CPU shape→b7 offset44→View Detail감산→alpha/조명. 기존 코어 추정 그대로 E=Eold*(1−p*core). Weather/높이/원경대기/태양Base차폐 불변.
사용자 명시 선택으로 Density transition width UI/밀도재매핑/전용CLI테스트 제거. 옛선택JSON필드는 무시하고 새 detailCoreProtection 누락은0. Save Preset만 현재 슬롯 원본을 수정한다. 저장 전 슬롯전환/재실행은 저장된값으로 복귀한다. 일반 시작값과원본8JSON자동수정없음. ideas/density-transition-width.md는회고용보존.
체크리스트: F1 Cloud Local→0과.65비교→Alpha/Composite로몸체회복과파임보존을분리→Stratus/Altocumulus는0에서조금씩올려판단→마음에드는경우해당슬롯Save Preset. 외곽틈이메워지거나칼로잘린듯하면보호를낮춘다. 멀리흐리게만드는슬라이더아님. 검증중/푸시없음.

### 슬라이더/제거 검증 완료
Debug/Release 전체 빌드 통과. Release 관련7개 회귀 통과 후 신규 DetailCoreSliderSmoke 최종2.93초 통과; Debug 관련8/8(19.36초)통과. 선택JSON누락0/범위밖거부/원자저장라운드트립/옛width필드무시/슬롯격리/배포조회/핫리로드 확인. 원본8파일불변. 일반p0 Composite와 이전타입비교F5 HDR 정규화MAE6.16e-9/max0.000154로기존tolerance통과.
GPU검사산출물: build/captures/detail-core-slider/4876718(Release),4963015(Debug). 0/.325/.65/1은서로다른T결과이며Base Sun T보존과0복원통과. 초기후보동등성검사에서는전체Composite용LDR/max기준이실패했다. 선형T수치검사로분리해MAE≤1e-5/max≤.003/차이.001초과픽셀≤전체.01%를명시한다. Release실제MAE3.24e-7/max.002216/10픽셀. 상수/동적셰이더와전체리로드간소수픽셀차이가남으며bit-exact동등이라고주장하지않는다. 일반Composite복원허용치는완화하지않았다. 화면품질과직접마우스조작은사용자확인대기,성능재측정없음.

## 2026-09-21 테스트 산출물 정리

프로젝트 74.318GiB 중 루트 `captures` 33.94GiB와 `build/captures` 36.70GiB가 대부분이었다.
주원인은 1920×1080 RGBA16F와 PNG를 후보·디버그 모드·이동 프레임마다 새 실행 폴더에
누적한 것이다. 텍스트 결과 1,928개(24.8MiB)는 로컬
`notes/capture-archive/2026-09-21`에 상대 경로와 SHA-256을 보존하고 이미지·바이너리
70.637GiB를 삭제했다. 루트 `captures/noise-lab`의 UI 설정과 Custom 기록은 유지했다.

완료된 방향광·림·대기·해상도·형상·Detail·Base 후보 비교의 CLI와 CTest 등록, 전용 분석기와
probe를 폐기했다. 현재 미해결 근경/원경 분리용 `CloudNearFarDiagnostics`,
`CloudPathLength`와 현재 기능 회귀인 `BaseCandidateUiSmoke`는 유지한다(`DetailCoreSliderSmoke`는 2026-09-24 슬라이더와 함께 폐기).
대량 진단 두 개는 `VCLOUD_ENABLE_DIAGNOSTIC_TESTS=ON`일 때만 CTest에 등록되며 명시적 CLI는
항상 사용할 수 있다. 렌더 기본값, 프리셋 8개와 사용자 화면 승인 상태는 바꾸지 않았다.

## 2026-09-22 구름 대기 거리 2배 채택 / 근경 경계 분석
사용자 비교 승인으로 구름 Air T/L 조회 거리를 일반 2배로 확정했다. 배경/지면, 실제 대표거리, 구름 T와 밀도, 원본8JSON은 유지한다. F4 Air T/L은 일반 합성과 같은 보정 거리다. CB/UI/schema 추가 없음. [결과와 승인](stage15-cloud-aerial-composition.md).
사용자는 다음 순서를 근경 윤곽·밀도 경계 분석으로 선택했다. [분석 기록](stage15-cloud-boundary-analysis.md). 원격 push 금지 지속.

## 2026-09-22 근경 침식 순서/Detail 태양 차폐 검증 완료
[새진단결과](stage15-cloud-near-clarity.md). 기준/정규화감산/remap만비교했고옛격자는반복하지않았다. 두후보모두근경골대비손실이있어일반형상미채택. 현재Cumulus의Base/Detail태양표현차이는크지만전체Detail은직접광을크게늘려일반Base유지권고/화면판정대기. 원본8개/대기2배유지/push없음.

## 2026-09-22 후속 사용자 결정: 정규화 remap 기본식 채택
위 미채택 권고 이후 사용자는 후보2의 외곽 인상을 선호하여 일반 반영을 지시했다.
높이·밀도 배율 적용 후 절대량 감산하던 식을, 정규화 형상 [e,1]→[0,1] remap 뒤 배율 적용으로 바꿨다.
**외곽 표현 개선의 채택이며 근경 선명도 문제는 미해결**이다. 문제·해결 원리·남은 손실·검증과
참조 DirectX11 프로젝트 비교/다음 순서는 [같은 해결 기록](stage15-cloud-near-clarity.md)에 작성한다.
원본8JSON/High/대기2배/Base 태양 차폐를 유지하고 push하지 않는다.

## 2026-09-24 F2 Base 비교 UI 정리

사용자 요청: F2의 `Base / Detail Noise Scale` 제목 아래 슬라이더가 없어 보인다는 지적과 임시 Base 후보 정리. 슬라이더(Base world size/Base vertical size/Detail world size)는 제거된 적이 없고 2026-09-19 임시 비교 블록이 제목과 슬라이더 사이에 삽입되어 있었다. 슬라이더와 Regenerate/hash 표시를 제목 바로 아래로 옮기고 `Temporary Base comparison`을 그 뒤로 분리했다.
`Base candidate`는 Original / Mid octaves x2.5 두 개만 남긴다(Threshold +0.05, Contrast x2, Contrast x2 + Mid x2.5 제거). 런타임 후보 번호는 0/1이며 스냅샷 `temporaryBaseCandidate`도 0/1이다. 후보 전환은 Base 생성 프로그램만 재컴파일해 Base 볼륨을 재생성한다. 셰이더의 `VCLOUD_TEST_BASE_THRESHOLD_OFFSET`/`VCLOUD_TEST_BASE_CONTRAST` 매크로는 로컬 실행기 Base remap 비교 재현용으로만 남긴다. 세션 전용·프리셋 비저장 계약은 유지.
검증: Release/Debug 빌드, BaseCandidateUiSmoke(2후보 해시 상이·재로드 유지·범위 밖 2 거부·Original 복원)/NoiseLabSmoke/HotReloadDependencySmoke 통과, Release CTest 46/46.

## 2026-09-24 Detail core protection 제거

사용자 요청: F1 Cloud Local의 Detail core protection을 쓰지 않는 것 같으니 확인 후 런타임/UI에서 삭제. 확인 결과 원본 타입 프리셋 3개(stratus/cumulus/mixed)는 모두 0, custom-cloud.json은 키 없음(=0)이라 일반 실행에서 셰이더 분기 `detailCoreProtection>0`이 한 번도 켜지지 않았다.
2026-09-24: Detail core protection 제거. 저장된 모든 타입 값이 0(Custom은 키 없음=0)이라 일반 화면에 영향이 없었다. b7 offset44는 예약 `cloudShapeReserved44`(CPU `reserved44`, 항상0)로 바꾸고 48B/나머지 offset은 유지한다. F1 슬라이더, JSON 쓰기·범위 검사, snapshot 필드, DetailCoreSliderSmoke를 제거했다. 옛 JSON의 `detailCoreProtection` 키는 값과 무관하게 무시하며 원본 프리셋 파일은 자동 수정하지 않는다(다음 Save Preset에서 빠진다). 일반 침식식은 `(1-p*core)` 항 없이 `e=D*s*(1-smoothstep(.45,.90,S))`이다. 과거 .65 비교는 시험 정의 `VCLOUD_TEST_DETAIL_CORE_MODE`로만 재현한다.
검증: Debug/Release 빌드(Release 앱 exe는 실행 중이라 링크 잠김, TestRunner/Debug는 갱신), Release·Debug CTest 45/45(DetailCoreSliderSmoke 등록 해제로 1개 감소). CloudFormationPresetStore는 새 파일에 키를 쓰지 않음, 옛 키 0/.65/1.5 모두 무시하고 나머지 값 동일 로드를 검사한다. HighPerformance는 사용자 exe 동시 실행 중 1회 실패 후 단독 재실행 통과.
