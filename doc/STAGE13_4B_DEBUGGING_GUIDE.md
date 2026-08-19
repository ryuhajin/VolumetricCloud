# Stage 13-5 / Stage 9 오픈 월드 디버깅 가이드

파일 이름은 과거 13-4B 링크 호환을 위해 유지하지만 내용은 현재 13-5 단일 포트폴리오
씬과 단계 9 최적화 기준이다. 13-4C Local Inspector와 13-4D 단일 씬 이력은 변경 기록에 남아 있으며
현재 런타임에는 scene 전환, 문자 진단 키, F9~F12가 없다. 단계 9 View/Light 기본
최적화의 성능 비교 방법도 이 문서에서 함께 설명한다.

## 1. 첫 실행

1. 프로그램을 실행한다. F5 Hero/Building Depth 카메라와 `Stratus`가 시작 외형이다.
2. `F1`을 열고 맨 위 `Cloud Type Settings`의 현재 상태를 읽는다.
3. 숫자 `0`으로 Composite를 선택한 뒤 F5와 F6을 비교한다.
4. F1에서 `Dense Mixed Default`, `Stratus (층운)`, `Cumulus (적운)`을 차례로 누른다.
5. `F2`를 열어 Weather R 지도를 보고 G mode가 읽기 전용으로 바뀌는지 확인한다.
6. 최종 화면 판정은 사용자가 수행한다. 에이전트 자동 테스트의 finite/hash 통과는 구름의
   미적 품질 승인을 대신하지 않는다.

제목 표시줄에 `Stage 13-5`, 현재 Debug 출력과 `Unified 50km Portfolio Scene`이 보이면
정상이다. 일반 시작은 저장된 Custom을 자동 적용하지 않고 항상 결정적 Stratus를 쓴다.

## 2. 단일 scene과 카메라

- 지면은 Y=0, X/Z `-5000~5000m`의 10km×10km 실제 quad다.
- 원점의 검은 건물은 `20×60×20m`이며 3m×20층 Depth 기준이다.
- 구름층은 `1500~7500m`, View 50km, fade 40~50km, Weather 64km다.
- FOV 60°, near/far `0.1m/60km`이며 지면과 건물은 네 카메라에서 항상 렌더된다.

| 키 | 위치 → 타깃(m) | 무엇을 판정하는가 | 정상 | 실패 징후 |
|---|---|---|---|---|
| F5 | `(0,15,180) → (0,350,-1200)` | 건물 Depth와 hero 외형 | 건물이 뒤 구름을 가리고 좌우 하늘에는 구름이 존재 | 건물 주변 전체가 거대한 빈 Weather 구역 |
| F6 | `(40,2,0) → (40,700,-2200)` | 지면 수평선과 broken-sky | 좌·중·우 구름, 20~40% 푸른 틈 | 한 구역만 구름이 있거나 회색 얇은 띠 |
| F7 | `(40,3000,0) → (40,3300,-1600)` | 구름 내부 | 밀도/투과율이 근거리에서 연속 변화 | 검정, NaN 구멍, 고정 평면 |
| F8 | `(40,7800,0) → (40,6000,-1800)` | 상공 하향 | 가변 상단과 Weather 분포 | 하나의 평평한 천장 |

WASD는 1000m/s, Shift는 4배다. 마우스 왼쪽 드래그는 orbit, 휠은 거리 조절이다.
ImGui 텍스트/숫자 편집 중에는 전역 키가 차단되는 것이 정상이다.

## 3. 13-4E 밀도 계산

Weather는 “어디에 구름이 있는가”, Texture3D는 “안쪽 질량과 경계가 어떤가”를 맡는다.

```text
weatherSupport      = smoothstep(0.02, 0.20, Weather R)
weatherFactor       = lerp(0.70, 1.00, Weather R)
footprintFactor     = lerp(0.80, 1.00, typed footprint)
horizontalCoverage  = global coverage × weatherFactor × footprintFactor
noiseShape          = RemapCoverage(raw noise, horizontalCoverage)
baseDensity         = inside column × weatherSupport × noiseShape
                    × vertical profile × density × Weather B modifier
finalDensity        = DetailErosion(baseDensity)
```

Weather R=0은 비지만 약한 R도 작은 구름으로 남는다. vertical profile은 noise threshold에
다시 곱하지 않으므로 상단이 이중으로 잘리지 않는다. footprint는 20%만 threshold에 반영해
둥근 상단을 만들고, Detail은 이미 생긴 Base 경계만 깎는다.

## 4. Cloud Type Settings

F1의 상태는 다음 다섯 가지다.

| 상태 | 의미 |
|---|---|
| Dense Mixed Default | Full Open World 비교가 복원하는 결정적 혼합형 기준 |
| Stratus | 낮고 넓은 층운 preset |
| Cumulus | 높은 돔과 중심 질량을 가진 적운 preset |
| Custom | 마지막으로 명시 저장한 값 |
| Custom Unsaved | 외형 소유 slider를 바꿨지만 저장하지 않은 값 |

| 값 | Dense Mixed | Stratus | Cumulus |
|---|---:|---:|---:|
| Weather R non-zero/core | 79.62/49.11% | 87.31/56.60% | 73.82/42.07% |
| Coverage | 0.68 | 0.40 | 0.45 |
| Density | 1.15 | 1.20 | 1.25 |
| Extinction `/m` | 0.00035 | 0.00042 | 0.00038 |
| Erosion | 0.18 | 0.12 | 0.18 |
| 두께 | 혼합 1.5~6km | 층운 1.5~2.8km | 적운 3~6km |

Stratus는 낮고 넓은 층과 완만한 상단, Cumulus는 더 높은 돔과 뚜렷한 중심 질량을 보여야
한다. 타입 전환은 Weather seed/period, Texture3D, wind/offset, 카메라·도메인, 태양·Phase·
환경광, View `512×100m`, Light `80×250m`를 바꾸지 않는다.

2026-08-17 사용자 검증에서 기존 Coverage `0.72/0.68`은 양의 밀도 표본을 지나치게 넓혀
Stratus와 Cumulus를 하나의 큰 덩어리처럼 보이게 했다. 새 `0.40/0.45`는 Weather 배치와
광학값을 유지하면서 Base threshold만 좁혀 구름 덩어리 사이의 푸른 틈을 늘린다.

## 5. Custom 저장과 복원

1. F1 Shared cloud parameters에서 Coverage/Density/Extinction, Height profile에서 두께와
   타입 profile, Detail에서 Erosion을 조절한다. F2에서는 threshold/softness, coverage
   bias/contrast, density/thickness link를 조절할 수 있다.
2. 상태가 `Custom Unsaved`와 `Unsaved changes`로 바뀌어야 한다. 자동 저장은 하지 않는다.
3. `Save Current as Custom`을 누른다. 상태가 `Custom`으로 바뀌고
   `captures/noise-lab/custom/noise-settings.json`에 schema 29가 원자 저장된다.
4. Stratus나 Cumulus를 누른 뒤 `Custom`을 눌러 마지막 저장값을 복원한다.
5. 재실행하면 시작 화면은 Stratus다. 그 뒤 Custom을 눌렀을 때 저장값이 복원돼야 한다.

저장하지 않은 상태에서 다른 preset을 누르면 현재 수정값은 대체되며 경고가 표시된다.
파일이 없거나 구버전·손상·NaN/Inf·범위 밖이면 Dense Mixed를 적용하고 이유를 표시해야 한다.
저장 실패도 현재 화면 파라미터를 되돌리면 안 된다.

## 6. 숫자 Debug 출력

상단 숫자와 숫자 패드는 같은 명시적 표를 사용한다.

| 키 | 출력 | 정상 판정 |
|---|---|---|
| 0 | Composite | 배경·구름·Depth가 자연스럽게 합성 |
| 1 | Raw Noise | 하늘 전역에 월드 고정 3D 질량 입력 |
| 2 | Weather Coverage | 20~40% 푸른 틈을 남기는 큰 연속 배치 |
| 3 | Base Density | Weather 안에서 둥근 세로 질량, 바닥/상단 연속 fade |
| 4 | Detail Noise | Base보다 작은 경계 주파수 |
| 5 | Final Density | Base 경계만 침식, 빈 영역은 검정 |
| 6 | View Optical Depth | 중심부가 경계보다 높은 τ |
| 7 | Accumulated Direct | 태양 방향에 따른 밝은 가장자리/어두운 내부 |
| 8 | View Transmittance | 밀집 중심은 어둡고 빈 하늘은 밝음 |
| 9 | Light Transmittance | 자기 그림자 방향이 연속적 |

F1/F3의 Debug View 콤보에는 추가 진단이 있다. `Typed Shape Profile`은
`vertical×footprint`, `Effective Shape Coverage`는 새 horizontal coverage를 표시한다.
문자 키와 Shift/Ctrl 진단 조합, F9~F12가 아무 동작도 하지 않는 것이 정상이다.

## 7. Optimization 버튼과 성능

F1 `Optimization`의 Master는 여러 행을 한 번에 안전한 조합으로 맞춘다. 개별 행의 버튼은
누른 행만 바꾸고 상태를 `Custom`으로 만들기 때문에, 성능 비교를 시작할 때는 먼저 Master를
눌러 기준을 초기화한다. 일반 시작은 단계 9 사용자 승인 전까지 `Approved Reference`다.

### 7.1 Master preset

아래 p95는 RTX 4080 SUPER, 드라이버 `32.0.15.9186`, Release 1920×1080, VSync/UI/preview
Off, time 0, 120프레임 워밍업 뒤 장면별 GPU Cloud timestamp 600개를 측정한 값이다.
Fine Reference는 실시간 후보가 아니므로 이 측정에서 의도적으로 제외했다.

| Master 버튼 | View / Light 핵심 설정 | 용도 | 7장면 GPU Cloud p95 |
|---|---|---|---|
| Balanced | Search 2×, Exit 1%, `100→150m`, Cone 6·2°·77% | 자동 화질·성능을 통과한 가장 싼 실시간 후보 | `7.44~9.41ms`, 최대 `9.41ms` |
| Conservative | Search 2×, Exit 0.5%, `100m fixed`, Cone 12 | 품질 여유가 더 큰 비교 후보 | `6.88~16.73ms`, 최대 `16.73ms` |
| Approved Reference | 최적화 Off, `512×100m`, Straight `80×250m` | 단계 13 승인 화면과 비교하는 기준 | `11.22~25.33ms`, 최대 `25.33ms` |
| Fine Reference | 최적화 Off, `1024×50m`, Straight `320×62.5m` | 오프라인 화질 정답 생성 | 실시간 성능 측정 제외, 매우 무거움 |

버튼 순서는 계산 예산 기준이다. 실제 GPU ms는 장면의 빈 공간, 조기 종료와 측정 변동 때문에
모든 장면에서 완전히 단조 증가하지 않을 수 있으므로 같은 장면의 p95와 최대값을 함께 본다.

`Fine Reference`는 “더 좋은 실시간 옵션”이 아니다. View 간격을 절반으로 줄여 최대 횟수를
2배로 만들고 Light 표본도 80에서 320으로 4배 늘린 오프라인 기준이다. 따라서 Composite에서
사용하면 프레임 부하가 급증하는 것이 정상이다.

### 7.2 개별 View 비용 버튼

| 행 / 버튼 | 바뀌는 계산 | 상대 비용과 사용법 |
|---|---|---|
| Empty Search `2×` | Base가 3회 연속 빈 뒤 최대 200m 간격으로 후보 탐색 | 빈 공간 Texture3D 조회를 줄이는 승인 후보 |
| Empty Search `Off` | 선택된 View 간격으로 끝까지 Full march | 2×보다 비싸지만 coarse 누락 비교 기준 |
| Early Exit `2%` | 누적 View T가 0.02 이하면 종료 | 가장 싸지만 마지막 2% 배경 기여 손실 가능 |
| Early Exit `1%` | T가 0.01 이하면 종료 | Balanced 승인값 |
| Early Exit `0.5%` | T가 0.005 이하면 종료 | 더 오래 계산하는 Conservative 값 |
| Early Exit `Off` | 불투명해져도 View 끝까지 진행 | 가장 비싼 View 종료 기준 |
| View `100→200m` | 16~48km에서 100m가 200m로 증가 | 가장 싼 거리 step, 단독 화질 승인은 아님 |
| View `100→150m` | 16~48km에서 100m가 150m로 증가 | Balanced 승인값 |
| View `100m Fixed` | 전 거리 100m | 원거리도 같은 품질, 더 많은 View 표본 |
| View `50m Fine` | 전 거리 50m, 최대 1024회 | View 오프라인 기준, 100m 대비 최대 2배 표본 |
| Support Precheck `On` | Weather/높이/profile이 확실히 0이면 Base/Detail을 읽지 않음 | 결과가 0인 곳만 생략하는 저비용 경로 |
| Support Precheck `Off` | 모든 후보에서 Base 경로 평가 | 더 비싼 비교 기준 |

Fast와 Empty Search 4×는 400m deterministic 표본이 Base 최단 파장을 놓쳐 등고선·물결무늬를
만들었으므로 2026-08-19 탈락했고 활성 버튼에서 제거했다.

### 7.3 개별 Light 비용 버튼

Light 계산은 최종 밀도가 양수인 각 View 표본마다 다시 실행된다. 따라서 Light 표본 수 차이는
화면 픽셀 전체에서 중첩되며 View 버튼보다 부하 차이가 크게 보일 수 있다.

| Light 버튼 | 표본 예산 | Cone 6 대비 대략적인 밀도 평가 수 | 용도 |
|---|---:|---:|---|
| Cone 5 | 5 | `0.83×` | 가장 싼 실험값, 자동 화질 승인 아님 |
| Cone 6 | 6 | `1×` | Balanced 승인값 |
| Cone 8 | 8 | `1.33×` | Light 품질 비교 |
| Cone 12 | 12 | `2×` | Conservative 값 |
| Straight 80 | 최대 80 | 최대 `13.3×` | 단계 13 승인 Reference |
| Straight 320 Fine | 최대 320 | 최대 `53.3×`, Straight 80의 4배 | 오프라인 Light 정답 |

`Straight 320 Fine`은 320개의 광선을 만드는 것이 아니라, 밀도가 있는 View 표본 하나마다
태양 방향 직선 광선 하나를 최대 320곳에서 평가한다. 현재 View 최대 512회와 결합한 이론적
상한은 픽셀당 `512×320=163,840` Light 밀도 평가다. Master `Fine Reference`는 View 최대가
1024회이므로 상한이 `327,680`까지 커진다. 실제 실행은 빈 View 표본, 구름층 이탈과 Light
광학 깊이 조기 종료로 줄지만 실시간용으로 사용하지 않는다.

Cone Angle `4/3/2/1°`와 Cone Far Fraction `75/77/85/95%`는 같은 tap 수에서 표본 위치만
바꾸므로 계산량이 같다. 각도와 비율은 그림자 모양·오차 비교용이며 Balanced 승인값은
`2°/77%`다.

성능을 눈으로 비교할 때는 우측 오버레이의 `GPU Cloud`를 읽는다. `CPU Frame`은 VSync 대기를
포함하므로 알고리즘 비교값으로 쓰지 않는다. 정식 판정은 VSync Off와 120프레임 워밍업 뒤
`captures/stage9/performance.json`의 p95를 사용한다.

## 8. Pipeline Compare

F1 `Open World Render Pipeline Compare`의 1~5는 Procedural/Uniform → Texture3D →
Periodic Weather → Physical Shape → Full Open World를 누적한다. 버튼으로 Compare에 들어가면
main pass의 샘플 time만 0으로 고정하므로 바람 때문에 위치가 바뀌지 않는다. 일반 Animation
상태, 카메라, 도메인, 지면과 건물은 바뀌지 않는다. 외형 preset이나 slider를 조절해 Compare를
벗어나면 정상 time으로 즉시 복귀한다. 5는 항상 Dense Mixed와 현재 13-5 sampling/lighting을
복원한다.

## 9. 사용자 최종 검증

- [ ] F5: 건물이 구름을 정확히 가리고 양옆 하늘이 하나의 거대한 공백이 아니다.
- [ ] F6 Dense: 좌·중·우에 구름이 있고 푸른 틈이 약 20~40%다.
- [ ] Dense/Cumulus: 밝은 가장자리와 어두운 내부가 함께 있다. 전체가 흰 수증기면 실패다.
- [ ] Stratus: 낮고 넓으며 상단 변화가 완만하다.
- [ ] Cumulus: Stratus보다 높은 돔과 중심 질량이 뚜렷하다.
- [ ] 전환 중 카메라와 Weather 공간 위치가 움직이지 않는다.
- [ ] F7/F8: 내부와 상공에서 NaN, 검정 frame, 평평한 천장이 없다.
- [ ] Custom: 수정→저장→다른 preset→복원→재실행 후 Custom 복원이 모두 된다.
- [ ] Compare 1~5에서 건물/지면/카메라는 고정되고 구름 하위 계산만 누적 변경된다.
- [ ] 제거된 F2 타입 버튼, 문자 키와 F9~F12는 동작하지 않는다.
- [ ] F1 Optimization의 Balanced가 가장 왼쪽 Master이고 Empty Search가 `2×/Off`만 제공한다.
- [ ] Fine Reference와 Straight 320 Fine은 오프라인 기준임을 이해하고 일반 플레이 후보로 사용하지 않는다.

13-4E 렌더와 새 Dense Mixed F6 기준 13-5는 2026-08-17 승인됐다. 구형 shell은 제외했고 단계 9
Balanced 사용자 최종 승인을 기다린다.
