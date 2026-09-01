# High 단일화 사용자 렌더 검증 가이드

이 문서는 자동 검증이 끝난 뒤 사용자가 Release 1920×1080 화면을 직접 보고 최종 승인하는 절차다. 에이전트는 빌드, 수치, GPU 오류와 자원 계약을 검사하지만 구름 모양·shimmer 허용 수준·색과 산란의 미학은 승인하지 않는다.

## 준비

```powershell
cmake --build build --config Release
.\build\Release\VolumetricCloud.exe
```

창의 client 영역을 1920×1080으로 맞춘다. 시작 상태는 `Urban Fair Weather + High`다. High는 별도 버튼이 없으며 항상 Full resolution이다.

## 조작표

| 조작 | 위치 | 바뀌는 값 | 검증 목적 | 정상 결과 | 실패 징후 |
|---|---|---|---|---|---|
| Urban / Meadow / Snow | `F4` 상단 | formation + 태양·환경광·대기·지면 | 세 최종 scene transaction | 한 번에 한 장면으로 전환 | 이전 장면 색/구름이 섞임, 검정 frame |
| Stratus / Cumulus / Mixed / Custom | `F1` 상단 | formation만 | 타입 높이와 profile, Custom 복원 | 조명·대기·지면은 그대로 | F3 값도 바뀜, 상하단 칼 절단 |
| Save Custom | `F1` | 현재 formation만 | schema 1 범위 | 저장 뒤 Custom 버튼 활성 | 파일 손상, Custom 비활성 |
| Cloud movement speed | `F1`, 구름 파라미터 하단 | 경과 시간에 곱하는 0~400m/s 속도 | 공통 이동 시작·정지 | 0에서 정지, 100~400에서 Weather/Base/Detail과 그림자가 함께 뚜렷하게 이동 | 높은 값에서도 정지, 일부 구조만 이동 |
| VSync | `F1 > Presentation` | swap-chain Present 방식 | 동기/즉시 표시 상태 | 기본 On, 지원 환경의 Off는 `immediate + tearing allowed` 표시 | 기본 Off, Off인데 interval 1, scene/preset 변경 |
| Preview field | `F1 > Preview` combo | 세 preview의 출력 field | 진단 선택성과 ID | 목록에서 즉시 선택 | slider로 한 칸씩 이동, ID 경고 |
| Weather Map RGBA | `F2 > Weather Generator` 최상단 | CPU 생성 R/G/B/A | 실제 texture와 채널 확인 | 편집 직후 image/hash 변경 | image가 하단에 묻힘, 이전 map 유지 |
| Cloud type source/G | 같은 영역 | F1이 정한 고정 타입 또는 생성 G | G 채널 소유권 | F1 Mixed/Weather Map G에서만 G 편집 활성 | 고정 모드인데 무효 slider 활성 |
| Wind direction | `F2` | Weather/Base/Detail 공통 수평 방향 | 동일한 world-space 이동 | F1 속도로 세 구조가 같은 방향으로 이동 | Weather만 미끄러짐, 별도 offset처럼 동작 |
| `F5`~`F8` | 키보드 | 고정 카메라 | 위/내부/수평/원경 교차 | 즉시 안정된 현재 frame | 과거 시점 잔상, 검정/타일 |
| Density | `F4 > Cloud view` | 최종 density | NaN과 domain 절단 | 덩어리 내부가 연속 | 사각 외곽, 자홍/노랑 오류색 |
| Transmittance | 같은 combo | View T | 적분과 early exit | 빈 하늘 1, 두꺼운 구름은 낮음 | 화면 전체 0/1, 불연속 띠 |
| Cloud Depth | 같은 combo | opacity-weighted 거리 | scene/domain 교차 | 구름 거리 변화가 연속 | 건물 뒤 구름, 고정 거리 판 |
| Near/Far Cache | 같은 combo | cache optical depth | Deep Cache 생성/lookup | 구조가 연속, cascade 전환 완만 | 빈 cache, 타일, 경계 seam |
| Atmosphere LUT | `F4 > Atmosphere view` | LUT/air 결과 | Stage 14 유지 | 유한·연속적인 LUT | 자홍/빨강/노랑 오류색 |
| Performance profiler | 화면 좌측 상단 독립 창 | FPS, Time, CPU/GPU와 GPU 구간 | 삭제 경로·frame 비용 확인 | F1~F4와 무관하게 계속 표시 | F4를 닫으면 사라짐, 폐기 항목 존재 |

F5~F8의 정확한 이름은 F4 카메라 표시에서 확인한다. 각 카메라는 카메라가 layer 아래, 지평선 쪽, layer 내부, layer 위에 있는 경우를 포함한다.

F1 창의 X를 누른 뒤 F1을 다시 눌러 같은 창이 열리는지 먼저 확인한다. F2~F4도
같은 규칙이다. F4의 Cloud view 목록에는 `Composite`, `Density`, `Transmittance`,
`Optical Depth`, `Cloud Depth` 등이 각각 한 번만 나타나야 하며 Dear ImGui ID 경고가
나오면 실패다.

## 1. 세 콘셉트 × 네 카메라

각 콘셉트에서 F5, F6, F7, F8을 순서대로 누른다.

- Urban은 비교적 맑은 도시 장면과 분리된 적운형 덩어리가 보여야 한다.
- Meadow는 더 넓은 broken-cloud 분포와 풀 지면 반사색이 보여야 한다.
- Snow는 낮고 넓은 overcast와 밝은 지면 bounce가 보여야 한다.
- 모든 시점에서 건물·지면이 구름 앞뒤 관계를 올바르게 가려야 한다.
- 카메라 이동 직후 과거 frame은 남지 않아야 한다. Temporal을 제거했으므로 ghost가 사라질 때까지 기다리는 정상 구간은 없다.

실패로 기록할 것:

- 이전 카메라 실루엣이 남는 ghost
- 한 frame 전체가 검정 또는 자홍색
- 화면 고정 사각 타일이나 Weather 반복 경계
- 지면 앞에 있어야 할 구름이 지면 뒤에서 보임

## 2. Stratus / Cumulus / Mixed 형상

같은 콘셉트와 카메라를 유지하고 F1 타입 버튼만 바꾼다. F5(위)와 수평·원경 시점을 함께 본다.

- Stratus: 낮고 넓지만 종이처럼 완전히 평평하지 않아야 한다. 물리 두께 1500~2300m, base lift 0m다.
- Cumulus: 2000~3200m 세로 부피와 둥근 상부 질량이 보여야 한다. 약한 column은 최대 300m 들린다.
- Mixed: 낮은 층과 발달한 덩어리가 섞여야 한다. 최대 Cumulus 4600m + lift 200m 뒤에도 domain 상단 200m 여유가 있다.

실패 징후:

- 모든 구름의 상단 또는 하단이 같은 수평선에서 칼로 잘림
- 멀리서 직사각형 Planar domain 외곽이 보임
- Cumulus가 낮고 네모난 판처럼 보임
- Mixed 버튼이 Cirrus처럼 가늘고 방향성 있는 띠를 만듦

## 3. 이동과 shimmer

layer 내부 카메라에서 `W/A/S/D`로 이동하고 F1 `Cloud movement speed`와 F2
`Wind direction`을 확인한다.

- 과거 frame 잔상과 가장자리 끌림은 없어야 한다.
- F1 `Cloud movement speed`를 0으로 하면 구름이 정지한다. 100m/s에서 이동을 확인하고
  400m/s에서는 같은 시간 동안 약 네 배 이동해야 한다. Weather silhouette, Base/Detail
  내부 무늬와 Deep Cache 그림자가 F2 wind 방향으로 함께 움직여야 한다.
- Temporal 제거로 생길 수 있는 단일-frame shimmer는 사용자가 허용 가능한지 직접 판단한다.
- shimmer를 확인하려고 VSync를 켰다 꺼도 formation이나 shader generation이 바뀌면 안 된다.
  Off에서 FPS가 그대로라면 Performance 창의 GPU Frame을 먼저 본다. GPU 시간이 한 frame
  예산을 이미 채우면 렌더 병목이므로 FPS가 오르지 않는 것이 정상이다. F1에
  `immediate + tearing allowed`가 보이는데 CPU Frame만 여전히 주사율에 고정되면
  드라이버 강제 VSync나 외부 frame limiter를 실패 원인과 함께 기록한다.
- 일정한 줄무늬가 태양 방향으로 고정되면 cone/cache sampling 문제로 기록한다.

## 4. 태양·Deep Cache·대기·Tone

Urban의 태양이 보이는 시점에서 시작한다.

1. Final에서 태양 방향 주 산란과 구름 내부 명암이 있는지 본다.
2. Near/Far Cache와 Cascade view로 전환해 광학 깊이가 빈 검정 texture가 아닌지 본다.
3. Surface Transmittance에서 지면 그림자가 구름 분포와 함께 움직이는지 본다.
4. Atmosphere LUT와 HDR Before Tone을 순서대로 보고 Final로 돌아온다.

정상 결과는 먼 물체와 구름에 같은 대기 원근이 적용되고, 지면 그림자와 구름의 태양 방향이 일치하며, Tone Map 뒤 highlight가 유한한 것이다. NTE Rim은 삭제됐으므로 rim 효과 자체를 기대하지 않는다. 대신 기본 phase 기반 주 산란이 사라지면 실패다.

## 5. Custom formation 범위

1. F1에서 Mixed를 누른다.
2. Coverage, 두께 또는 Weather threshold를 눈에 띄게 바꾼다.
3. `Save Custom`을 누른다.
4. F4에서 Snow를 적용해 조명·대기·지면도 바꾼다.
5. F1 상단에서 `Custom`을 누른다.

정상 결과:

- 저장한 Mixed formation의 분포·두께·Weather·wind가 돌아온다.
- Snow의 태양·대기·눈 지면·Tone·카메라는 그대로다.
- 파일은 `captures/noise-lab/custom-cloud.json` 하나다.

실패 징후:

- Load가 scene concept까지 Urban으로 되돌림
- 구형 `cloud-presets`나 schema 29/30 파일을 자동 적용
- 잘못된 domain 두께 파일을 clamp해 부분 적용
- 저장 실패 뒤 기존 Custom 파일이 손상

## 6. 성능과 제거 확인

좌측 상단 Performance 창의 GPU 구간에는 다음 항목만 있어야 한다.

```text
Atmosphere / Shadow / Opaque / Cloud / Tone / Frame
```

Release 1920×1080 자동 gate 목표는 Cloud p95 10ms 이하, Frame p95 16.67ms 이하다. 화면을 보는 동안 일시적인 LUT/cache 재생성 frame과 steady-state를 구분한다.

## 사용자 판정 기록

| 날짜 | 해상도/GPU | 콘셉트·카메라 | 관찰 | 판정 |
|---|---|---|---|---|
| - | - | - | - | 미검증 |

- [ ] Urban/Meadow/Snow × F5~F8
- [ ] Stratus/Cumulus/Mixed 절단·네모 윤곽 없음
- [ ] 이동·바람에서 ghost 없음, shimmer 허용 가능
- [ ] 주 산란·Deep Cache 그림자·대기 원근·Tone 유지
- [ ] Custom Load가 formation만 복원
- [ ] F1 X 닫기 뒤 F1로 재개방, Preview combo 선택 정상
- [ ] F1 Animate/Time으로 공통 구름 이동 시작·정지·scrub 정상
- [ ] F1 VSync 기본 On, 지원 PC의 Off에서 `immediate + tearing allowed`, On/Off에서 formation과 shader generation 불변
- [ ] F2 RGBA 최상단·Cloud Type G 소스·공통 advection 동작 정상
- [ ] 좌측 상단 Performance 창과 F4 Cloud view ID 경고 없음
- [ ] 사용자가 최종 승인
