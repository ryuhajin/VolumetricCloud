# Stage 15 — 형상·조명 슬롯 프리셋

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
