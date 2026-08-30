# Stage 15 품질·콘셉트 사용자 검증 가이드

이 문서는 Stage 15 실행 파일의 자동 검증 상태를 먼저 확인한 뒤 사용자가 직접 보고 최종 승인하는
공식 절차다. Stage 15는 **Quality(렌더 비용과 정밀도)** 와 **Concept(구름·대기·지면의 외형)** 를
분리한다. 검증의 핵심은 다음 5개 계약이 화면에서도 지켜지는지 확인하는 것이다.

1. Concept를 바꿔도 Quality와 Temporal 선택은 유지된다.
2. Quality를 바꿔도 같은 구름의 위치·층 높이·대기·지면은 바뀌지 않는다.
3. Capture/Reference를 끝내면 진입 전 실시간 상태가 정확히 복원된다.
4. 자동 화질·성능 수치와 별개로 최종 외형과 포트폴리오 화면은 사용자가 승인한다.
5. High는 Full RT고 Capture는 exact Native 1080p의 4-sample HDR 누적이다.

에이전트는 포트폴리오 PNG를 만들거나 화면 미학을 합격 처리하지 않는다. 사용자가 같은
카메라·시간·창 크기에서 직접 비교하고 스크린샷을 촬영한다. 내부 성능 회귀 조사 원문은
[`changes/STAGE15_PERFORMANCE_REGRESSION.md`](changes/STAGE15_PERFORMANCE_REGRESSION.md),
수치 계약은 [`PERFORMANCE.md`](PERFORMANCE.md)를 함께 참고한다.

> 검증 증거 상태(2026-08-31): 물리 출력/DPI, High Full RT, Temporal reset/phase,
> Joint4 rejection, Native 1080p Capture 4/4와 Release 96-case 화질 gate는 통과했다.
> Release 전체 CTest 첫 실행은 Stage 12 smoke의 최초 캡처 크기 drift로 `53/54`였고,
> 캡처 전 `96×54`를 명시한 뒤 최종 Debug/Release 전체 CTest가 각각 `54/54`로 통과했다.
> High Full 성능 첫 실행은 외부 게임 PID의 GPU 3D 약 37%와 동시 fixture로 오염되어 무효였다.
> 게임 종료 뒤 clean 48-case와 독립 3-block Stage 14 probe가 모두 통과했다. 사용자 렌더 검증만 남아 있다.

---

## 1. 판정 원칙과 기록

### 정상, 일시 현상, 실패

- **정상**은 같은 조작을 반복해도 같은 결과가 나오고 아래 판정 카드의 소유권과 색·형태
  계약을 만족하는 상태다.
- **허용되는 일시 현상**은 프리셋 전환 직후 Temporal history가 새로 쌓이는 4~16프레임,
  Reference의 낮은 FPS, GPU overlay 한두 프레임의 변동처럼 원인이 정해져 있고 곧 사라지는
  현상이다.
- **실패**는 16프레임 이상 기다리거나 같은 상태로 다시 시작해도 남는 ghost, 서로 다른
  Concept가 섞인 frame, 검정/NaN 화면, 거리 경계의 고정 띠, 잘못된 상태 복원이다.
- 디버그 화면은 Composite와 색의 의미가 다르다. 숫자 `0` Composite 전체가 자홍색이면 오류
  가능성이 높지만 F1 `Temporal History Validity`의 자홍색은 의도된 `clip.w` 거부 사유다.
- 빈 하늘은 밀도·LOD처럼 구름 표본이 맞은 곳만 표시하는 Debug View에서 검정일 수 있다.
  구름 내부까지 전부 검정인지 숫자 `2→3→5`로 Weather, Base, Final을 차례로 확인한다.

### 사용자가 남길 최소 기록

| 항목 | 기록 예 | 이유 |
|---|---|---|
| 실행 파일과 commit | `build/Release/VolumetricCloud.exe` | 서로 다른 코드를 비교하지 않기 위해 |
| 실제 extent | `Physical/Swap/Scene 1920×1080, Cloud 960×540 또는 1920×1080` | DPI·RT 오판을 막기 위해 |
| GPU/driver | 자동 JSON 값 | 다른 장치의 성능 수치를 직접 비교하지 않기 위해 |
| 카메라 | `F5`~`F8` | 위치·고도·시선이 외형을 바꾸기 때문에 |
| Concept/Quality/T | compact overlay 문자열 | 실제 렌더 상태를 재현하기 위해 |
| Diagnostic | None/Capture Still/Reference | 진단은 실시간 품질보다 비싸기 때문에 |
| 시간 | F1 Effective Time, F3 태양 시간 | 구름 이동과 태양색을 고정하기 위해 |
| Tone | ACES, Exposure EV, White Balance | 밝기 차이를 품질 차이로 오인하지 않기 위해 |

일반 수동 실행의 기본 창은 1280×720이어도 괜찮다. 다만 자동 gate의 1920×1080과
동일한 측정이라고 기록해서는 안 된다. Native 1080p에서 Medium Cloud RT는 960×540,
High/Capture Cloud RT는 1920×1080이어야 한다.

---

## 2. 10분 빠른 승인

이 절차는 명백한 실패를 빠르게 찾는 용도다. 전부 통과한 뒤에도 5절의 상세 카드를 완료해야
최종 승인할 수 있다.

1. 새 Release 프로세스를 실행하고 `0`, `F5`를 누른다. F4에서 두 overlay를 켜고 시작값이
   `Urban Fair Weather / Medium / Temporal On / 50% / 100.0m/512 / Cone 6 /
   Balanced 512 / Diagnostic None`인지 확인한다.
2. F4 출력 extent에서 PMv2가 On이고 Physical/Swap/Viewport/Scene/Depth/History가 서로
   같은지 본다. 일반 1280×720이면 Medium Cloud RT 640×360이며, Native 1080p를 켰을
   때는 전자가 1920×1080, Medium이 960×540이어야 한다.
3. F1 `Animation`의 `Pause Time`을 켜고 `Reset Time`을 누른다. F3 `Time of Day`가 재생
   중이면 `Pause Time`, F3 `Tone Mapping`은 `ACES On / 0 EV / 6500 K`로 맞춘다.
4. 네 Concept를 두 차례 빠르게 순환한다. 검은 flash나 이전 지면과 새 구름의 혼합 없이 전체
   장면이 한 번에 바뀌어야 한다.
5. Medium/T On에서 네 Concept×F5~F8의 16화면을 확인한다. Desert의 실제 로컬 Cirrus sheet는
   약 8.0km 위에서 시작하므로 7.8km에서 아래를 보는 F8은 의도적인 빈 negative-control이다.
6. Urban/F6과 Desert/F6에서 Low→Medium→High를 비교한다. 구름 배치는 같고 High는
   `Full / Nearest / Full Resolution`을 표시하며 Medium보다 작은 윤곽이 더 연속적이어야 한다.
7. Urban/F5에서 T On으로 천천히 회전한 뒤 정지한다. 4~16프레임 뒤 건물 경계 ghost와
   카메라에 붙은 구름이 없어야 한다.
8. Native가 꺼진 일반 창에서 Capture Still을 누른다. 자동 Native 1920×1080 전환 후
   `0/4 → 4/4 Ready`로 증가하며 누적 동안 카메라·시간이 고정되어야 한다.
9. Capture Ready→Reference→Restore Realtime을 확인한다. Capture가 Native를 켰다면
   이전 일반 창으로 돌아와야 하고, 진단 전 Quality/T는 정확히 복원되어야 한다.
10. 실제 캡처 전 F4에서 두 overlay를 모두 끈 뒤 F1~F4를 닫는다. 최종 화면은 사용자가 직접 저장한다.

하나라도 실패하면 6절의 증상별 진단표로 이동한다. 외형 숫자를 임의로 바꿔 실패를 숨기지 말고
같은 fixture와 Debug View를 먼저 기록한다.

---

## 3. 조작 위치와 화면 계약

### F4 Stage 15 조작

| 실제 UI/키 | 직접 바꾸는 것 | 바꾸지 않아야 하는 것 |
|---|---|---|
| `Urban Fair Weather`, `Meadow Broken Clouds`, `Desert Cirrus`, `Snow Overcast` | Weather, 층, shape, 빛, 대기, 지면 | Quality와 Temporal override |
| `Low`, `Medium`, `High` 또는 `Q` | RT/Spatial/Temporal, View, distance, cone, Detail LOD, Shadow | Weather 배치, 층, 조명·대기·지면 |
| `T` | 현재 Off/Stable/Full Resolution Temporal만 임시 반전 | Quality가 소유한 RT 해상도와 Concept |
| `Output / DPI` | Physical/Swap/Viewport/Scene/Depth/Cloud/History, DPI/PMv2/Native를 표시 | 렌더 상태 |
| `Native 1080p` / Restore | exact 1920×1080 borderless client / 저장한 일반 창 | Quality/Concept |
| `Compact Stage 15 Overlay` | 왼쪽 위 상태 창 | 렌더 상태 |
| `Performance Overlay` | 오른쪽 위 CPU/GPU 창 | 렌더 상태 |
| `Advanced Capture / Reference → Capture Still` | exact Native 1080p, Full/T Off, 50m/1024, Balanced512, projection jitter 4개 HDR 누적 | 저장된 실시간 Quality/T |
| `Advanced Capture / Reference → Reference` | Full, T Off, 50m/1024, distance·LOD Off에 Fine Reference와 Direct Reference 추가 | 저장된 실시간 Quality/T |
| `Advanced Capture / Reference → Restore Realtime` | 진입 전 Quality와 T override | 진단 중 선택한 Concept |

Capture Still 또는 Reference 중에는 Quality 버튼이 비활성화되고 Q/T도 무시되는 것이 정상이다.
Concept 변경은 같은 Reference 조건으로 외형을 비교하기 위해 허용한다.

### 숫자 0~9는 Concept 키가 아니다

| 키 | Debug View | 정상 화면 | 실패 의심 화면 |
|---:|---|---|---|
| `0` | Composite | 최종 구름·대기·지면 | 전체 검정, NaN 자홍, 점멸 |
| `1` | Raw Noise | 구름 후보 안의 연속 3D noise | 카메라와 함께 미끄러짐, 단색 |
| `2` | Weather Coverage | Weather 섬과 빈 영역 분리 | 전부 0/1, 전환 뒤 이전 배치 |
| `3` | Base Density | Weather 안의 큰 질량과 profile | Weather는 있는데 내부 전체 검정 |
| `4` | Detail Noise | Base보다 작은 경계 주파수 | Base와 같은 크기, 화면 격자 |
| `5` | Final Density | Detail 침식 뒤 최종 밀도 | Base 양수인데 계속 0, barcode |
| `6` | View Optical Depth | 두꺼운 중심이 밝음 | 밀도와 무관한 띠·NaN |
| `7` | Accumulated Direct | 태양 쪽 외곽과 내부 명암 | 태양과 무관한 단색 |
| `8` | View Transmittance | 빈 하늘 밝음, 밀집 중심 어두움 | 밀도와 반대, 프레임별 반전 |
| `9` | Light Transmittance | 태양 방향으로 연속된 자기 그림자 | cascade seam, 타일, 점멸 |

숫자를 눌렀을 때 Concept나 카메라가 바뀌면 입력 호환 실패다.

### F5~F8 고도 관계

| 키/실제 버튼 | 카메라 | Urban 1.8~5.5km | Meadow 1.5~6.5km | Snow 1.5~4.0km | Desert 7.0~10.5km |
|---|---|---|---|---|---|
| `F5 Hero / Building Depth` | 15m, 건물/하늘 | 아래 | 아래 | 아래 | 아래 |
| `F6 Ground Horizon` | 2m, 수평선 | 아래 | 아래 | 아래 | 아래 |
| `F7 Inside Cloud` | 3.0km | 층 안 | 층 안 | 층 안 | 아래 |
| `F8 Above / Down` | 7.8km, 아래 시선 | 위 | 위 | 위 | 로컬 sheet 아래에서 더 아래를 봄: 의도적 빈 장면 |

“층 안”은 기하학적 고도 관계다. 해당 위치의 Weather가 비면 구름이 약하거나 없을 수 있다.
Desert의 전역 domain은 7.0~10.5km지만 center `8.68km`와 0.5~1.5km 로컬 두께 때문에 실제 sheet
바닥은 약 7.93~8.43km다. 따라서 F8의 빈 화면은 실패가 아니다. `2→3→5`에서 Weather/Base/Final을
확인해 정상적인 구멍·negative-control과 밀도 실패를 구별한다. 실제 GPU 내부 밀도는 숨김 8.5km
수평 자동 카메라가 따로 검사한다.

---

## 4. 공통 fixture

### BOOT-01 — 새 프로세스와 기본 상태

- **사전 상태·카메라:** 실행 중인 프로그램을 닫고 Release를 새로 시작한다.
- **조작 위치와 순서:** `0` → `F5` → F4를 열고 두 overlay를 켠다.
- **직접 바뀌는 값:** Composite와 Hero 카메라만 선택한다.
- **검증 대상:** 실행 기본값과 실제 적용값.
- **정상 화면:** `Urban Fair Weather`, `Medium`, `Temporal On`, RT `50%`, View
  `100.0m/512`, Cone 6, Shadow `Balanced 512`, Diagnostic `None`. Concrete와 Physical
  Atmosphere/Ground Bounce가 적용된다.
- **허용되는 일시 현상:** 시작 직후 `GPU warming up`, history invalid와 4~16프레임의 누적.
- **실패 징후:** Custom 시작, Full RT, Fast 256, T Off, 전체 검정/자홍, overlay와 실제값 불일치.
- **실패 시 다음 Debug View:** F1 shader status, 이후 `2→3→5`.

### FIXTURE-01 — 구름 시간·태양·Tone 고정

- **사전 상태·카메라:** BOOT-01 통과, Urban/Medium/T On/F5.
- **조작 위치와 순서:** F1 `Animation → Pause Time` On과 `Reset Time`; F3 `Time of Day`가
  재생 중이면 `Pause Time`; F3 `Tone Mapping`을 ACES On, 0 EV, 6500K로 맞춘다.
- **직접 바뀌는 값:** 구름 advection 시간, 태양 재생, 최종 표시 변환만 고정한다.
- **검증 대상:** 모든 비교가 같은 구름 좌표·태양·노출을 쓰는지.
- **정상 화면:** F1 Effective Time과 F3 태양 각도가 움직이지 않고 history가 최소 16프레임
  다시 쌓인다.
- **허용되는 일시 현상:** Reset/태양 조정 중 한 차례 화면 변화, preset 변경의 history reset.
- **실패 징후:** 구름 또는 태양색이 계속 이동해 같은 상태의 캡처가 달라짐.
- **실패 시 다음 Debug View:** `1 Raw Noise`, F1 Effective Time/Bulk travel, F3 Play/Pause.

---

## 5. 상세 사용자 판정 카드

### PRESET-01 — Concept 원자 전환

- **사전 상태·카메라:** FIXTURE-01, Medium/T On/F6, 숫자 0.
- **조작 위치와 순서:** 네 Concept를 순서·역순으로 누른 뒤 약 1초 간격으로 20회 전환한다.
- **직접 바뀌는 값:** Weather·Domain·Shape·Light·Environment·Atmosphere·Ground가 한
  transaction으로 교체된다.
- **검증 대상:** 부분 적용 없는 commit, history reset, Quality 보존.
- **정상 화면:** 구름·지면·대기가 한 번에 바뀌고 Quality Medium/T On은 유지된다. Concept
  이름도 같은 frame에 갱신된다.
- **허용되는 일시 현상:** 전환 뒤 4~16프레임 재누적, 실제 hash가 달라진 LUT의 한 차례 갱신.
- **실패 징후:** 이전 Weather+새 지면, 검은 flash, 이름 지연, Quality Custom/Low, resource 오류.
- **실패 시 다음 Debug View:** `2`, F3 Atmosphere Status, `9`를 같은 두 Concept에서 비교한다.

### INPUT-01 — Q/T, ImGui capture와 key-repeat

- **사전 상태·카메라:** Diagnostic None, F4가 열린 Urban/Medium/T On.
- **조작 위치와 순서:** Q로 High→Low→Medium. T로 Off `(override)` 후 다시 On/해제. Q와 T를
  각각 2초 누른다. F3 Exposure EV 숫자를 Ctrl+클릭해 편집 중 Q/T도 입력한다.
- **직접 바뀌는 값:** 물리 press 한 번당 Quality 한 단계 또는 T 반전만 요청한다.
- **검증 대상:** 순환, override 왕복, key-repeat, ImGui keyboard capture.
- **정상 화면:** 두 번째 T가 원상 복원하고 `(override)`를 없앤다. 길게 눌러도 반복 순환하지
  않으며 텍스트 편집 중에는 Q/T가 바뀌지 않는다.
- **허용되는 일시 현상:** commit 후 history reset과 재누적.
- **실패 징후:** 두 번째 T 뒤 override 잔류, Q가 Custom 포함, hold 반복, 편집 중 상태 변경.
- **실패 시 다음 Debug View:** F1 Temporal Mode/Phase/history/reset reason과 F4 overlay.

### CONCEPT-01 — 네 Concept×F5~F8 16화면

- **사전 상태·카메라:** FIXTURE-01, Medium/T On, Diagnostic None. 매 전환 뒤 16프레임 대기.
- **조작 위치와 순서:** Concept 하나마다 F5→F6→F7→F8을 누르고 overlay와 Composite를 기록한다.
- **직접 바뀌는 값:** Concept는 외형, F5~F8은 위치와 시선만 바꾼다.
- **검증 대상:** descriptor 외형, 층 교차와 60km 계약, 카메라 보존.
- **정상 화면:** 아래 표와 3절 고도표가 함께 성립한다. 빈 곳도 `2→3→5`에서 Weather 구멍으로
  설명되면 정상이다.
- **허용되는 일시 현상:** 카메라 history reset, 층 안의 뿌연 화면, 특정 Weather 구멍.
- **실패 징후:** 모든 Concept가 같은 지면/대기, 카메라에 따라 층 이동, 50~60km hard clip.
- **실패 시 다음 Debug View:** `2`, Height Fraction, `3`, `5`, F3 Aerial Perspective.

| Concept | 정상 외형 | 대표 실패 징후 |
|---|---|---|
| Urban Fair Weather | 1.8~5.5km 적운, 열린 하늘, Concrete, Earth Clear | 넓은 층운, 다른 지면, 완전 빈 Weather |
| Meadow Broken Clouds | 1.5~6.5km Mixed, Grass, Urban보다 높은 점유율 | Urban과 같은 희소 배치, Concrete 유지 |
| Desert Cirrus | 7.0~10.5km의 얇고 긴 약 20° 결, Desert, Earth Hazy | 낮은 적운, barcode, 맑은 대기/눈 |
| Snow Overcast | 1.5~4.0km 넓은 Stratus, Snow, 밝은 bounce | 고립 적운, 흰 clipping, NaN색, 점멸 |

### CIRRUS-01 — 방향성 밀도와 world anchoring

- **사전 상태·카메라:** Desert/Medium/T Off, F1 Pause Time On, F6 숫자 0. F8은 빈
  negative-control로 따로 확인한다.
- **조작 위치와 순서:** F1 Shape mode와 Cirrus read-only flow/scale/thickness/profile을 기록한다.
  Pause를 잠깐 풀어 Bulk travel 증가를 보고 다시 Pause한다. 카메라를 옆으로 이동 후 돌아온다.
- **직접 바뀌는 값:** 카메라만 움직이며, 시간 재생 중 Weather/Base/Detail은
  `Cloud Wind Speed (Bulk)`로 함께 이동한다.
- **검증 대상:** 방향 basis, 물리층, shared advection.
- **정상 화면:** 약 20°의 길고 얇은 결, F1 `Cirrus Physical Layer`, `Cloud Wind Speed (Bulk)`와
  `Bulk travel` 표시,
  legacy Weather/Detail wind 비활성. 구름이 카메라에 붙지 않는다.
- **허용되는 일시 현상:** F8은 실제 local sheet 아래에서 아래를 보므로 Cirrus가 보이지 않음,
  F6의 Weather/local-thickness 구멍, T 재활성 뒤 누적.
- **실패 징후:** 낮은 적운, barcode, yaw에 맞춰 결 회전, 넓은 Weather 영역에서 Base가 있는데
  Final이 계속 0, Legacy/Weather Physical 표시, legacy wind 활성. Weather만 양수이고 Base가 0인
  작은 구멍은 noise/coverage에 의한 정상 후보일 수 있다.
- **실패 시 다음 Debug View:** `2→1→3→5`. Final만 0이면 coverage/erosion, 모두 0이면 mode/교차.

기본 read-only 기준은 flow `(0.9397, 0.3420)`, Base `20/4/1km`, Detail
`6/1.5/0.5km`, 두께 `0.5~1.5km`, profile `0.48/0.45`다.

### QUALITY-01 — Low/Medium/High

- **사전 상태·카메라:** FIXTURE-01, Diagnostic None, T Off. Urban/F6 후 Desert/F6에서 반복한다.
- **조작 위치와 순서:** Low→Medium→High, 각 16프레임 뒤 같은 외곽·먼 Detail·그림자 비교.
- **직접 바뀌는 값:** 아래 표의 RT/Spatial/Temporal, sampling/LOD/Shadow만 바뀐다.
- **검증 대상:** 단조성, Concept/Weather 소유권, High Full과 cloud-edge RMSE 개선.
- **정상 화면:** 구름 위치·층·Weather 구멍·지면·대기색은 같고 Low의 먼 Detail/그림자가 가장
  단순하며 High가 더 연속적이다.
- **허용되는 일시 현상:** reset, 저밀도 경계의 작은 표본 차이, Low의 의도된 먼 Detail 감소.
- **실패 징후:** 다른 구름/층/지면, High의 새 띠·계단, High가 Low보다 명백히 흐리거나 소실.
- **실패 시 다음 Debug View:** `2`, Detail LOD Factor, `4`, `9`.

| 프리셋 canonical 값(T override 전) | Low | Medium | High |
|---|---:|---:|---:|
| RT/Spatial/Temporal | 50%/Joint4/Stable 4-Phase | 50%/Joint4/Stable 4-Phase | Full/Nearest 1:1/Full Resolution |
| View step/max | 150m/384 | 100m/512 | 100m/512 |
| Distance step | 12~48km ×1.5 | 16~48km ×1.5 | 24~50km ×1.25 |
| Cone / Detail LOD | 5 / 16~32km | 6 / 32~48km | 8 / 48~60km |
| Shadow | Fast 256 | Balanced 512 | Balanced 512 |

이 카드에서 T Off override가 활성이면 Low/Medium은 50%, High는 Full RT를 유지한 채
Temporal만 Off가 된다. override를 해제하면 Low/Medium은 Stable 4-Phase, High는 Full Resolution으로
복원되어야 한다.

### JOINT4-01 — Accepted Tap Count와 경계 번짐

- **사전 상태·카메라:** Urban/Medium/T Off/F5에서 건물과 하늘 경계를 화면에 넣는다.
- **조작 위치와 순서:** F1 공간 복원 진단에서 `Upsample Accepted Tap Count`(ID 80)를
  선택하고 순수 하늘 구름과 건물 윤곽을 따로 본 뒤 Composite로 돌아간다.
- **직접 바뀌는 값:** 알고리즘은 바꾸지 않고 hard-valid tap 0~4를 검정, 25%, 50%,
  75%, 흰색으로 표시한다.
- **검증 대상:** Geometry/Sky hard rejection, Geometry local plane, Sky/Sky Cloud Depth/T soft guide.
- **정상 화면:** 순수 sky는 대부분 2~4 tap의 중간 회색~흰색이다. 건물 윤곽에서
  tap 수가 줄거나 0으로 떨어지는 것은 class crossing을 막는 정상 결과일 수 있다.
- **실패 징후:** 순수 sky 대부분이 0~1 tap이며 Composite에 빈번한 구멍이 생기거나,
  건물 앞으로 하늘 구름이 번지거나, 지면 경계에 하얀 halo가 생긴다.
- **실패 시 다음 Debug View:** Scene Rejection(ID 65), Cloud Depth Weight(ID 66),
  Transmittance Weight(ID 67), 그리고 Full Scene Depth를 같은 카메라에서 기록한다.

### LOD-01 — Approved와 Aggressive

- **사전 상태·카메라:** Urban/Medium/T Off/F6, F3 `Stage 13-5 km Optics & Detail LOD`.
- **조작 위치와 순서:** `Approved 32–48km`와 `Detail LOD Factor`, Composite를 본 뒤
  `Aggressive 24–40km`로 반복한다.
- **직접 바뀌는 값:** Base가 아니라 Detail을 중립값으로 보내는 fade 시작/끝만 바뀐다.
- **검증 대상:** 연속 fade, Medium 승인값, Quality만 Custom인 소유권.
- **정상 화면:** 구름 내부 흰색=원본 Detail, 회색=fade, 검정=먼 중립 Detail. Aggressive는 더
  가까이 시작하고 근거리 큰 형태는 같다. Quality만 Custom, Concept 유지.
- `Approved 32–48km`는 현행 Medium 승인값이다. `Aggressive 24–40km`는 폐기된 legacy가 아니라
  개발 중 실시간 경계를 비교하기 위한 override다. Low/Medium/High를 각각 다시 선택해 F3의
  start/end가 `16–32/32–48/48–60km`로 바뀌는지도 별도로 확인한다.
- **허용되는 일시 현상:** 빈 하늘/Weather 구멍 검정, Aggressive의 먼 미세 경계 감소.
- **실패 징후:** 24/32/40/48km hard ring·pop, 근거리 전체 밀도 변화, Concept Custom.
- **실패 시 다음 Debug View:** Detail LOD Factor→`4→5→0`, F3 start/end meter 기록.

### TEMPORAL-01 — 정지·회전·이동

- **사전 상태·카메라:** Urban/Medium/T On/F5, 0EV/6500K, 시간 고정, accumulated 16 이상.
  Desert/F6에서도 반복한다.
- **조작 위치와 순서:** 5초 정지→천천히 회전→2초 정지→WASD 이동→정지. T Off와도 비교한다.
- **직접 바뀌는 값:** T는 Temporal history 재투영/EMA만 반전하고 Weather와 RT는
  바꾸지 않는다. Medium은 T Off/On 모두 50%, High는 모두 Full이다.
- **검증 대상:** 4-phase 누적, wind-aware reprojection, geometry depth 거부.
- **정상 화면:** On은 정지 뒤 안정되고 건물/Cirrus가 새 위치에 ghost 없이 수렴한다.
- **허용되는 일시 현상:** 첫 4-phase, 경계의 좁은 거부색, Difference의 미세한 비검정 값.
- **실패 징후:** 16프레임 뒤 꼬리·이중 외곽·카메라 고정 구름·파란 hole·넓은 점멸.
- **실패 시 다음 Debug View:** Motion→Current Source→History Validity→Weight→Difference.

### TEMPORAL-02 — F1 색 범례

- **사전 상태·카메라:** TEMPORAL-01, T On, accumulated 16 이상.
- **조작 위치와 순서:** 아래 여섯 Temporal Debug View에서 정지·회전·정지를 반복한다.
- **직접 바뀌는 값:** 상태를 색으로 표시할 뿐 history 알고리즘은 바꾸지 않는다.
- **검증 대상:** phase, motion, source, history 거부·가중치·차이.
- **정상 화면:** 정지 구름 내부는 대부분 초록이고 geometry 거부색은 좁다. Weight는 안정된 먼
  구름에서 밝고 거부 영역에서 어둡다.
- **허용되는 일시 현상:** reset 회색, 화면 밖 파랑, 빠른 motion 청록, 경계 주황/빨강/노랑,
  Difference의 작은 빨강·초록. jitter 때문에 완전 검정일 필요는 없다.
- **실패 징후:** 정지 뒤 내부 전체 거부색, non-finite 분홍, 반대 motion, 넓은 파랑, 영구 Weight 0.
- **실패 시 다음 Debug View:** geometry는 `0`/F5, 밀도는 `3 Base Density→5 Final Density`,
  깊이 가중치는 F1 `Cloud Depth Weight`, T는 `8`, non-finite는 shader 로그.

| Debug View | 색의 의미 |
|---|---|
| Temporal Jitter Phase | phase 0/1/2/3 빨강/초록/파랑/노랑 |
| Temporal Reprojection Motion | 중립 회색=0, R/G=±X/±Y, 어두운 회색=history/구름 없음, 파랑=projection invalid |
| Temporal History Validity | 초록 accepted, 회색 history 없음, 자홍 clip.w, 파랑 UV 밖, 청록 최대 motion, 주황 Scene, 노랑 Cloud Depth, 빨강 T, 보라 near fade, 분홍 non-finite |
| Temporal History Weight | 검정 0, 흰색 1 |
| Temporal Current/History Difference | 빨강 scattering 차이, 초록 T 차이, 파랑 current/history 없음 |
| Temporal Current Source Validity | 초록 valid, 회색 history 유지, 빨강 Scene class, 노랑 surface/plane, 파랑 valid 후보 없음 |

### TONE-01 — Exposure/WB와 history 분리

- **사전 상태·카메라:** Urban/Medium/T On/F5, history valid, accumulated 16 이상.
- **조작 위치와 순서:** F3 `Tone Mapping`에서 Exposure `-2→0→+2`, WB
  `3500→6500→10000K`, 같은 곳의 `Temporal Status (read-only)`를 본다.
- **직접 바뀌는 값:** 최종 밝기와 색온도만 바뀐다.
- **검증 대상:** Tone과 HDR history/LUT 소유권 분리.
- **정상 화면:** 최종 화면만 연속 변화하고 History valid/accumulated는 reset 없이 유지된다.
- **허용되는 일시 현상:** slider의 즉시 표시 변화, ACES Off Linear Debug의 highlight clipping.
- **실패 징후:** 매 변경 history invalid/0, 구름 위치 변경, LUT generation 증가, 일부 물체만 WB 적용.
- **실패 시 다음 Debug View:** F3 Phase/reset reason, F1 Temporal, LUT generation 기록.

### DIAGNOSTIC-01 — Capture Still과 Reference

> **15A 적용:** 아래의 Full/T Off/50m/1024/Balanced512 값에 더해 Capture Still은 exact
> Native 1920×1080과 output-pixel projection jitter 4개의 HDR 평균을 필수로 사용한다.
> Native가 꺼진 일반 창에서 Capture를 누르고 `PendingNative → Accumulating 0/4 → Ready 4/4`를
> 확인한다. 누적 동안 WASD/마우스/F5~F8/시간은 잠겨야 한다. exact extent를 만들지
> 못하면 실패 사유를 표시하고 720p로 조용히 fallback하지 않는다. Reference는 이 4-sample
> Capture가 아니라 별도 FineReference/DirectReference 수치 경로다.

- **사전 상태·카메라:** Meadow/Low/T Off `(override)`/F6처럼 복원을 구별할 상태를 기록한다.
- **조작 위치와 순서:** Capture Still 후 Reference. F1 `Optimization`/`Temporal`과 F3 Stage 12를 확인한다.
- **직접 바뀌는 값:** Capture는 Full/T Off/50m/1024/distance·LOD Off/Balanced 512,
  Reference는 Full/T Off/Fine Reference/Direct Reference.
- **검증 대상:** 일반 Quality와 분리된 진단, 입력 보호.
- **정상 화면:** Quality 이름은 Low를 보존하고 Diagnostic만 바뀐다. 두 모드 모두 Full/T Off/
  50m/1024/distance·Detail LOD Off이며, Capture는 Balanced 512를 쓴다. Reference에서는 여기에
  F1 Master `Fine Reference`, F3 Shadow Mode `Direct Reference`가 더해진다. Quality 버튼/Q/T는
  차단된다.
- **허용되는 일시 현상:** 매우 낮은 FPS, GPU Cloud 증가, history reset.
- **실패 징후:** 검정/NaN/device 오류, Reference의 Deep Cache, 진단 중 Quality/Q/T 변경.
- **실패 시 다음 Debug View:** `3`, `8`, `9`, F1 shader status와 F3 Shadow Mode.

### RESTORE-01 — 실시간 상태 복원

> **15A 적용:** Capture가 Native 1080p 전환을 직접 시작했다면 Restore에서 저장한 일반
> 창 크기·style·placement로 돌아가야 한다. 사용자가 Capture 전에 수동으로 Native를 켠다면
> Capture는 그 상태를 소유하지 않으므로 Restore가 Native를 끄지 않는다.

- **사전 상태·카메라:** Low/T Off `(override)`/Urban/F6를 기록하고 Capture Still 진입.
- **조작 위치와 순서:** 진단 중 Snow로 바꾼 뒤 `Restore Realtime`.
- **직접 바뀌는 값:** 진입 전 Quality/T만 복원하며 진단 중 Concept는 남긴다.
- **검증 대상:** 진단 snapshot 범위와 정확한 복원.
- **정상 화면:** None, Low 50%/150m/384/Fast 256, T Off `(override)`, Concept Snow. 다시 T를
  누르면 On으로 돌아가고 override 해제.
- **허용되는 일시 현상:** Restore 직후 reset과 Snow 재누적.
- **실패 징후:** Medium/Full/50m 잔류, override 손실, Concept Urban 복귀, Diagnostic 잔류.
- **실패 시 다음 Debug View:** 진입 전/진단/복원 후 overlay와 F1/F3 실제값 세 장.

### OWNERSHIP-01 — 수동 편집의 Custom 판정

- **사전 상태·카메라:** Urban/Medium/T On, Diagnostic None.
- **조작 위치와 순서:** F3 Aggressive 24–40km, 다시 Medium, Ground/Atmosphere 외형 하나와
  `Surface Cloud Shadow` 또는 `Surface Ambient Floor`, 다시 Urban, F1 Temporal Off를 각각
  한 번씩 바꾼다.
- **직접 바뀌는 값:** sampling/LOD/Temporal은 Quality, Ground/Atmosphere와 Surface 합성 외형은
  Concept 소유다.
- **검증 대상:** 관련 preset만 Custom으로 만드는지.
- **정상 화면:** LOD와 F1 Temporal은 Quality만 Custom, Ground/Atmosphere/Surface 외형은 Concept만
  Custom. named Urban을 다시 적용하면 Surface On/Strength/Ambient Floor도 descriptor 값으로
  복원된다. F1 Temporal 직접 편집은 임시 override를 해제한다.
- **허용되는 일시 현상:** history reset, 실제 atmosphere hash 변경의 LUT 재생성.
- **실패 징후:** 둘 다 Custom, Ground가 Quality 변경, F1 직접 편집 뒤 override 잔류.
- **실패 시 다음 Debug View:** 변경 전후 overlay와 실제 필드, Medium/Urban canonical 복원 확인.

### EXPORT-01 — schema 37

- **사전 상태·카메라:** Desert/High/T On/F6. F4 `Save Current Position` 후 카메라를 조금 옮긴다.
- **조작 위치와 순서:** `Export 4 PNG + JSON`, 생성된 `noise-settings.json`을 연다.
- **직접 바뀌는 값:** 상태는 유지하고 진단 PNG 4장과 JSON만 쓴다.
- **검증 대상:** schema, 현재/저장 카메라, Stage 15와 Cirrus 실제값.
- **정상 화면:** `schemaVersion=37`, `implementationStage="15"`, `stage15` 선택과
  `camera.current`/`camera.savedPosition`이 있다.
  `cloudShape.mode="cirrusPhysicalLayer"`; `cloudShape.cirrus`에 `flowDirectionXZ`,
  `baseScaleMeters`, `detailScaleMeters`, `thicknessMeters`, `verticalProfile` 배열이 있고 Bulk 값은
  `effectiveBulkWindSpeedMetersPerSecond`, `bulkWindSpeedMetersPerEffectiveSecond`,
  `bulkAdvectionDistanceMeters`에서 null이 아니다. 함께 생성된 네 PNG는 Noise Lab의
  XY/XZ/YZ 단면과 Weather Map 진단 자료이며
  포트폴리오 최종 화면이 아니다.
- **허용되는 일시 현상:** 저장하지 않았다면 savedPosition null, 짧은 readback 지연.
- **실패 징후:** schema≤36, High Full Resolution 누락, legacy/weather mode,
  Cirrus 누락/NaN, export가 preset 변경.
- **실패 시 다음 Debug View:** F1 Shape/Cirrus read-only와 JSON을 대조한다. schema 29와 혼동 금지.

### CAPTURE-01 — 포트폴리오 캡처

> Capture Still 결과를 찍는다면 F4에서 Physical/Swap/Scene/Cloud 1920×1080과
> `Ready 4/4`를 먼저 기록한다. 그 뒤 두 overlay와 F1~F4를 숨기고 사용자가 직접
> 촬영한다. Ready 전 sample이나 Reference를 Capture 최종 영상처럼 기록하지 않는다.

- **사전 상태·카메라:** 상세 카드 통과. 원하는 상태를 overlay가 보일 때 먼저 기록한다.
- **조작 위치와 순서:** F4에서 두 overlay를 먼저 모두 Off로 바꾼 뒤 F4를 포함한 F1~F4를 닫고,
  사용자가 직접 촬영한다.
- **직접 바뀌는 값:** checkbox는 UI만 숨긴다. Capture Still이면 진단 상태도 기록한다.
- **검증 대상:** UI 없는 재현 가능한 결과와 사용자 미학 승인.
- **정상 화면:** overlay/ImGui가 없고 기록한 카메라·Concept·시간과 같다.
- **허용되는 일시 현상:** overlay 표시 비용에 따른 순간 수치 변동, OS 캡처 도구의 색관리 차이.
- **실패 징후:** 상태 재현 불가, overlay 잔류, Reference를 기본처럼 촬영, 비교 중 fixture 변경.
- **실패 시 다음 Debug View:** overlay로 상태부터 확인하고 외형 문제는 `2/3/4/5/7/9`로 분리한다.

---

## 6. 증상별 단일 실험

| 증상 | 첫 가설 | 단일 실험 | 증거 | 정상 / 이상 결론 |
|---|---|---|---|---|
| Composite 검정 | 층 미교차/밀도 0 | `2→3→5` | 세 화면+Concept/카메라 | Weather 0은 배치, Base 0은 shape/coverage, Final만 0은 erosion |
| Composite 자홍/점멸 | NaN/셰이더/device | 시간 고정, F1 shader status | Composite+오류 로그 | Temporal debug의 의도색이 아니라 Composite면 실패 |
| Concept 혼합 | transaction 부분 적용 | Urban↔Snow 20회 | 전환 영상+overlay | 한 frame 혼합도 실패 |
| Quality마다 배치 다름 | Weather/시간 침범 | T Off/Pause 후 `2` L/M/H | 같은 픽셀 세 장 | Weather가 다르면 실패 |
| Cirrus 적운/barcode | mode/basis/coverage | F6 `1→3→5`+Shape | flow/scale+density | yaw와 같이 회전하거나 mode 오류면 실패 |
| F7/F8 빈 화면 | 구멍 또는 층 관계 | 고도표 후 `2→3→5` | camera+세 debug | Desert/F8은 의도적 빈 장면. 넓은 영역에서 Base가 있는데 Final이 계속 0이면 실패 |
| 거리 ring/pop | LOD 불연속 | LOD Factor A/B | start/end+띠 | 연속 회색 정상, 경계 고정 띠 실패 |
| 건물 ghost | depth 거부 | Current Source→Validity | 경계색+ghost | 좁은 거부 정상, ghost 위 초록 지속 실패 |
| 정지 점멸 | 반복 reset/current hole | accumulated+Current Source | reset reason+색 | 증가/초록 정상, 반복 reset/넓은 파랑 실패 |
| 그림자 타일 | cache 좌표/variant | `9`+Near/Far debug | cache/cascade | 연속 정상, 정사각 타일/점멸 실패 |
| Reference 검정 | reference 경로 | Capture와 같은 상태 비교 | Fine/Direct 표시 | 느린 것 정상, 검정은 실패 |
| Restore 불일치 | snapshot 손실 | RESTORE-01 1회 | 세 상태 overlay | Quality/T 복원+Concept 유지가 정상 |
| FPS만 순간 악화 | warmup/외부 GPU | GPU Engine 확인 | 16프레임+프로세스 | 순간 EMA로 회귀 판정 금지 |

---

## 7. 자동 JSON/CSV 판독

자동 결과는 수학·리소스·성능 계약의 증거이지 미학 승인서가 아니다. 자동 명령은 ImGui,
overlay, preview와 PNG export를 생략한다.

| 명령/산출물 | 확인 필드와 gate | 2026-08-31 증거 상태 |
|---|---|---|
| `--stage15-preset-smoke-test` / `preset-smoke.json` | 실제 extent, High Full, FullResolution/schema37, Capture 4 jitter/state, 48 cases, rollback/idempotency, D3D 0 | Debug/Release 48/48 통과. Release Stage10/11/15 targeted suite도 8/8 통과 |
| `--stage15-quality-test` / `quality.json,csv` | 전체·cloudMask·cloudEdgeMask SSIM/RMSE/T MAE, High Full 필수, High edge 개선, Temporal 4/8/16 age/reset | Release 96/96 통과. High 16개가 모두 Full이고 edge/Temporal 수렴 gate 통과 |
| `--stage15-performance-test` / `performance.json,csv` | 48×(120 warmup+600 unique); Frame/Cloud/Resolve p95 `16.67/10/2ms`; Low≥3% faster; High Full Cloud≤10ms; Stage14≤103% | clean 48/48 통과. 최악 Frame/Cloud `10.0014/9.95738ms`, 전체 Resolve 최악 `1.12026ms`, 내부 Stage14 `6.37645ms`/`0.839784×` |
| `--stage15-stage14-regression-probe` / `regression_probe.json,csv` | 3 blocks, 동일 fingerprint, Cloud≈Shadow+Raymarch+Resolve, median≤`7.8207488ms` | 최신 source clean 3-block `6.57203/6.15219/6.21363ms`, 중앙값 `6.21363ms`, ratio `0.818341`, invariant/D3D 통과 |

Low/Medium은 같은 50% 구조라 `Shadow+Raymarch` 상대 비교를 유지한다. High는 Full RT로
구조가 다르므로 이전 High/Medium 180% gate 대신 Cloud p95 10ms 절대 gate를 쓴다.

화질 JSON의 SSIM은 전체 이미지 통계 기반 global luminance SSIM이다. 작은 LOD ring이나 건물 경계
ghost를 국소 window로 찾는 검사는 아니므로, 자동 합격 뒤에도 LOD-01과 TEMPORAL-01/02를 생략하지
않는다.

known-good 장치는 RTX 4080 SUPER, driver `32.0.15.9186`이다. 다른 GPU의 보편 수치가 아니다.
재측정은 Release 1920×1080, VSync/UI/preview Off에서 수행하고 게임·영상·캡처 앱을 닫는다.
Codex의 `ChatGPT.exe` 3D UI도 11~28% GPU를 쓸 수 있으므로 측정 동안 창을 최소화한다.
순간 overlay나 다른 날 EMA를 600-sample p95와 비교하지 않고 fingerprint/shader hash/component
invariant가 모두 같을 때만 판정한다.

---

## 8. 실제 디버깅 회고

| 순서 | 증상·가설 | 단일 변경 | 증거 | 결론 |
|---:|---|---|---|---|
| 1 | Desert인데 shape가 Weather Physical로 복귀 | appearance 뒤 Cirrus shape commit | smoke shapeMode | 적용 순서 버그 수정 |
| 2 | Cirrus 내부 밀도 0 | coverage `0.22→0.42` | Base/Final readback | RemapCoverage cutoff, Final max `0.065738` |
| 3 | 초기 Cloud 약 17.7% 회귀 | 화면 PS variant 분리 | Cloud p95 | 약 6.8%까지 감소 |
| 4 | `/O3`가 나을 가설 | FXC flag만 변경 | 약 20.4% 악화 | `/O1` 원복 |
| 5 | Resolve 2ms 초과 | low-res 3×3 직접 읽기 | Resolve+Stage11 smoke | gate 복구 |
| 6 | non-Cirrus PS에 추가 연산 | 두 분기 compile-time 복원 | 118,048B+SHA | Stage14 PS 동일 |
| 7 | Shadow CS에 Cirrus 분기 | CS 0/1 variant | 28,264B+SHA | Stage14 일반 CS 동일 |
| 8 | probe 변동 | 외부 GPU 제거 | 게임 8~24%, ChatGPT 11~28% | fixture 오염 확인 |
| 9 | 오염 없는 probe | ChatGPT 최소화 | median `5.659648ms` | 상한 `7.8207488ms` 통과 |
| 10 | 빈 Desert/F8 Frame 순서 역전 | 상대 metric만 Shadow+Raymarch | Resolve scheduling 변동 | threshold/descriptor 유지 |
| 11 | quality 96개가 비정상적으로 쉽게 통과 | 숨김 `Stage15ResolvedCloud=79`가 sanitizer에서 Composite로 바뀌고 Temporal Off의 `CloudUpsample`도 ID 8/79를 분리하지 않음 | debug mode와 spatial resolve 출력 추적 | sanitizer는 내부 79만 허용하고 spatial resolve는 8=T, 79=scattering을 명시; 기존 96-result 폐기·재실행 |
| 12 | 실패한 전환/target 할당이 다른 후보로 조용히 측정될 가능성 | 요청 뒤 enum을 검사하지 않고 resolved capture가 Full direct로 fallback | transition status와 capture 분기 추적 | Concept/Quality/Diagnostic/Temporal/Shadow 상태를 매 요청 뒤 확인하고 target 준비 실패는 capture 실패로 처리 |
| 13 | 올바른 산란/T 재측정 전체 실패 | 절대 gate 또는 기존 RMSE/T MAE not-worse | 96행과 두 상대 flag 분리 | 96개 절대·High 상대 gate는 통과; Meadow Inside Temporal16 RMSE 증가 `0.00011572`가 tolerance `0.00011`을 `0.00000572` 초과. gate/descriptor는 바꾸지 않고 실패로 보존 |
| 14 | 전체 평균이 선명도 수렴을 가림 | reference T로 cloud/cloud-edge mask 고정 | 동일 edge 1,876,893픽셀과 4/16 frame 집계 | 최신 Release 96/96 통과. Medium→High edge RGB/T와 Temporal edge 두 지표 모두 개선 |
| 15 | High Full 성능 첫 재실행 실패 | Windows GPU Engine과 동시 fixture 확인 | 외부 게임 PID 약 37% 3D, 별도 VolumetricCloud 인스턴스 | 생성된 실패 수치는 무효. 프로세스 단독 clean 재실행 전에는 성능 미검증 |
| 16 | 게임 종료 뒤 clean 재실행 | 시작 전 외부 3D peak 약 1.7%, 단독 fixture | 48/48와 독립 3-block probe | High Cloud 최악 `9.95738ms`, Stage14 median ratio `0.818341`; 성능 gate 통과 |

- Raymarch PS SHA-256:
  `E0B473599E65AA42FC5E6AAAF1921613EDFD900285A90F182D83E7397289DD9F`
- Deep Shadow CS SHA-256:
  `8BA92CF46FAD586EEEDAE7A6F2E8AC19370530E5E7F2CD9B77BC3C56401CCB01`

exact Shadow cache는 도입하지 않았다. 강제 재생성 상태로 3% gate가 통과했고 Shadow가 주 병목도
아니었기 때문이다. 재발 방지를 위해 mode 추가 시 PS/CS를 함께 특수화하고 bytecode hash,
temps/div/rsq와 b7 범위를 비교한다. 성능은 먼저 Shadow/Raymarch/Resolve로 분해하고 변수 하나만
바꾸며, 앱 내부 UI와 외부 GPU 프로세스를 모두 제거한 뒤 측정한다.

---

## 9. 최종 승인 기록

### 자동 증거

- [x] PMv2·Physical/Swap/Viewport/Scene/Depth/Cloud/History extent와 Native 1080p/restore smoke
- [x] High Full/Nearest/Full Resolution, T의 RT 불변, schema37 통과
- [x] Temporal 4-phase 방문, history age 증가, no-op/reset count/reason 통과
- [x] Joint4 class/plane/sky/fallback과 sky accepted tap 평균 2 이상 통과
- [x] Capture exact 1080p 4/4 HDR, 입력/시간 lock, Native 소유권 복원 통과
- [x] 새 descriptor의 Debug/Release preset smoke 48 cases·D3D11 0·Cirrus 양수 통과
- [x] 전체·cloudMask·cloudEdgeMask quality와 Temporal 4/8/16 통과
- [x] High Full `GPU Cloud p95 ≤ 10ms`와 performance 48 cases — 최악 `9.95738ms`, 실패 0
- [x] 최신 shader Stage 14 regression 3-block 중앙값 — `6.21363ms`, ratio `0.818341`
- [x] Release Stage10/11/15 targeted 8/8과 Stage 12 focused Debug/Release 2/2 재확인
- [x] Debug/Release 전체 CTest 각각 54/54 — Stage 12 fixture를 explicit 96×54로 고정한 최종 source

### 사용자 화면

- [ ] BOOT-01/FIXTURE-01 기본 상태
- [ ] PRESET-01 원자 전환
- [ ] INPUT-01 Q/T 왕복, repeat, ImGui capture
- [ ] CONCEPT-01 네 Concept×F5~F8 16화면
- [ ] CIRRUS-01 방향성·물리층·`Cloud Wind Speed (Bulk)`·world anchoring
- [ ] QUALITY-01 외형 불변과 Detail/그림자 차이
- [ ] JOINT4-01 순수 sky 2~4 tap과 Geometry/Sky 경계 번짐 없음
- [ ] LOD-01 연속 fade와 ring/pop 없음
- [ ] TEMPORAL-01/02 이동 안정성과 색 범례
- [ ] TONE-01 Exposure/WB/history 분리
- [ ] DIAGNOSTIC-01/RESTORE-01 입력 차단과 복원
- [ ] 실제 extent 1080p·High Full·Capture 4/4 Ready와 Native 소유권 복원
- [ ] OWNERSHIP-01 Quality/Concept Custom 분리
- [ ] EXPORT-01 schema 37, High Full Resolution과 Cirrus/카메라 값
- [ ] 두 overlay와 F1~F4를 숨기고 사용자가 직접 촬영
- [ ] 사용자 최종 미학 승인

### 승인 후 작성

- 승인일:
- 검증 commit:
- client 크기/GPU/driver:
- 최종 Concept·Quality·카메라:
- 사용자 피드백:
- 수정 descriptor와 자동 재검증 결과:
- 최종 승인 여부:

사용자 최종 승인 전에는 `main` 병합이나 `stage15-approved` 태그를 만들지 않는다.
