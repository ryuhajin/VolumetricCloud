# Stage 15 — 슬롯 프리셋과 구름 품질 후속 개선

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
