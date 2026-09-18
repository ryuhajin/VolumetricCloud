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
